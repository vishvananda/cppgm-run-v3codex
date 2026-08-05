#include "macro_processor.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "IPPTokenStream.h"
#include "pp_tokenizer.h"

namespace cppgm
{
namespace
{

typedef std::uint32_t SpellingId;
typedef std::uint32_t PaintId;

const std::size_t kNoParameter = std::numeric_limits<std::size_t>::max();

enum TokenKind
{
	TK_IDENTIFIER,
	TK_PP_NUMBER,
	TK_CHARACTER,
	TK_USER_CHARACTER,
	TK_STRING,
	TK_USER_STRING,
	TK_OPERATOR,
	TK_NON_WHITESPACE,
	TK_HEADER,
	TK_PLACEMARKER
};

struct Token
{
	TokenKind kind;
	SpellingId spelling;
	std::string owned_spelling;
	const std::string* borrowed_spelling;
	PaintId paint;
	PaintId blocked;
	std::uint64_t origin;
	std::size_t matching_distance;
	bool leading_space;
	bool from_variadic;
	bool parameter_origin;
	bool matching_comma;

	Token()
		: kind(TK_PLACEMARKER), spelling(0), borrowed_spelling(0), paint(0),
		  blocked(0), origin(0), matching_distance(0), leading_space(false),
		  from_variadic(false), parameter_origin(false), matching_comma(false)
	{}

	Token(TokenKind token_kind, SpellingId token_spelling,
		bool has_leading_space)
		: kind(token_kind), spelling(token_spelling), borrowed_spelling(0),
		  paint(0), blocked(0), origin(0), matching_distance(0),
		  leading_space(has_leading_space), from_variadic(false),
		  parameter_origin(false), matching_comma(false)
	{}

	Token(TokenKind token_kind, const std::string& token_spelling,
		bool has_leading_space)
		: kind(token_kind), spelling(0), owned_spelling(token_spelling),
		  borrowed_spelling(0), paint(0), blocked(0), origin(0),
		  matching_distance(0),
		  leading_space(has_leading_space), from_variadic(false),
		  parameter_origin(false), matching_comma(false)
	{}
};

std::size_t MixedHash(std::size_t hash)
{
	std::uint64_t value = static_cast<std::uint64_t>(hash);
	value ^= value >> 30;
	value *= UINT64_C(0xBF58476D1CE4E5B9);
	value ^= value >> 27;
	value *= UINT64_C(0x94D049BB133111EB);
	value ^= value >> 31;
	return static_cast<std::size_t>(value);
}

template <typename Key, typename Value>
class FlatHashMap
{
public:
	FlatHashMap() : size_(0), tombstones_(0), slots_(16) {}

	Value* Find(const Key& key)
	{
		return const_cast<Value*>(
			static_cast<const FlatHashMap&>(*this).Find(key));
	}

	const Value* Find(const Key& key) const
	{
		std::size_t position = Bucket(key);
		while (slots_[position].state != EMPTY)
		{
			if (slots_[position].state == OCCUPIED &&
				slots_[position].key == key)
				return &slots_[position].value;
			position = (position + 1) & (slots_.size() - 1);
		}
		return 0;
	}

	Value* Insert(const Key& key, Value value)
	{
		PrepareInsert();
		std::size_t position = Bucket(key);
		std::size_t available = slots_.size();
		while (slots_[position].state != EMPTY)
		{
			if (slots_[position].state == OCCUPIED &&
				slots_[position].key == key)
				return &slots_[position].value;
			if (available == slots_.size() &&
				slots_[position].state == TOMBSTONE)
				available = position;
			position = (position + 1) & (slots_.size() - 1);
		}
		if (available != slots_.size())
			position = available;
		Slot& slot = slots_[position];
		if (slot.state == TOMBSTONE)
			--tombstones_;
		slot.key = key;
		slot.value = std::move(value);
		slot.state = OCCUPIED;
		++size_;
		return &slot.value;
	}

	bool Erase(const Key& key)
	{
		std::size_t position = Bucket(key);
		while (slots_[position].state != EMPTY)
		{
			Slot& slot = slots_[position];
			if (slot.state == OCCUPIED && slot.key == key)
			{
				slot.value = Value();
				slot.state = TOMBSTONE;
				--size_;
				++tombstones_;
				return true;
			}
			position = (position + 1) & (slots_.size() - 1);
		}
		return false;
	}

private:
	enum SlotState
	{
		EMPTY,
		OCCUPIED,
		TOMBSTONE
	};

	struct Slot
	{
		Key key;
		Value value;
		SlotState state;

		Slot() : key(), value(), state(EMPTY) {}
	};

	std::size_t Bucket(const Key& key) const
	{
		return MixedHash(std::hash<Key>()(key)) & (slots_.size() - 1);
	}

	void PrepareInsert()
	{
		if ((size_ + tombstones_ + 1) * 10 < slots_.size() * 7)
			return;
		const std::size_t capacity = size_ * 10 < slots_.size() * 3 ?
			slots_.size() : slots_.size() * 2;
		Rehash(capacity);
	}

	void Rehash(std::size_t capacity)
	{
		std::vector<Slot> old;
		old.swap(slots_);
		slots_.resize(capacity);
		size_ = 0;
		tombstones_ = 0;
		for (std::size_t i = 0; i < old.size(); ++i)
		{
			if (old[i].state != OCCUPIED)
				continue;
			std::size_t position = Bucket(old[i].key);
			while (slots_[position].state == OCCUPIED)
				position = (position + 1) & (slots_.size() - 1);
			slots_[position].key = old[i].key;
			slots_[position].value = std::move(old[i].value);
			slots_[position].state = OCCUPIED;
			++size_;
		}
	}

	std::size_t size_;
	std::size_t tombstones_;
	std::vector<Slot> slots_;
};

class SpellingTable
{
public:
	explicit SpellingTable(MacroProcessingStats* stats)
		: slots_(16, 0), stats_(stats), identifier_bytes_(0)
	{
		spellings_.push_back(std::string());
	}

	SpellingId Intern(const std::string& spelling)
	{
		std::size_t position = FindPosition(spelling);
		if (slots_[position] != 0)
			return slots_[position];
		if (spellings_.size() >=
			static_cast<std::size_t>(std::numeric_limits<SpellingId>::max()))
			throw std::runtime_error("too many distinct preprocessing spellings");
		if ((spellings_.size() + 1) * 10 >= slots_.size() * 7)
		{
			Rehash(slots_.size() * 2);
			position = FindPosition(spelling);
		}
		const SpellingId id = static_cast<SpellingId>(spellings_.size());
		spellings_.push_back(spelling);
		slots_[position] = id;
		identifier_bytes_ += spelling.size();
		if (stats_)
		{
			stats_->interned_identifiers = spellings_.size() - 1;
			stats_->interned_identifier_bytes = identifier_bytes_;
		}
		return id;
	}

	const std::string& Get(SpellingId id) const
	{
		if (id >= spellings_.size())
			throw std::logic_error("invalid interned spelling ID");
		return spellings_[id];
	}

private:
	std::size_t FindPosition(const std::string& spelling) const
	{
		std::size_t position = MixedHash(std::hash<std::string>()(spelling)) &
			(slots_.size() - 1);
		while (slots_[position] != 0 &&
			spellings_[slots_[position]] != spelling)
			position = (position + 1) & (slots_.size() - 1);
		return position;
	}

	void Rehash(std::size_t capacity)
	{
		slots_.assign(capacity, 0);
		for (SpellingId id = 1; id < spellings_.size(); ++id)
			slots_[FindPosition(spellings_[id])] = id;
	}

	std::vector<std::string> spellings_;
	std::vector<SpellingId> slots_;
	MacroProcessingStats* stats_;
	std::size_t identifier_bytes_;
};

std::uint64_t PairKey(std::uint32_t first, std::uint32_t second)
{
	return (static_cast<std::uint64_t>(first) << 32) | second;
}

class PaintTable
{
public:
	explicit PaintTable(MacroProcessingStats* stats) : stats_(stats)
	{
		// Node 0 is the empty trie and node 1 is the terminal membership
		// marker. Internal nodes form persistent paths in one contiguous arena;
		// complete add/merge transitions are memoized at their compact roots.
		nodes_.push_back(Node());
		nodes_.push_back(Node());
		root_count_ = 0;
		if (stats_)
		{
			root_flags_.push_back(1);
			root_flags_.push_back(0);
			root_count_ = 1;
		}
		UpdateStats();
	}

	bool Contains(PaintId paint, SpellingId macro_name) const
	{
		Validate(paint);
		PaintId node = paint;
		for (int bit = 31; bit >= 0; --bit)
		{
			if (node == 0 || node == 1)
				return false;
			node = ((macro_name >> bit) & 1U) ?
				nodes_[node].one : nodes_[node].zero;
		}
		return node == 1;
	}

	PaintId Add(PaintId paint, SpellingId macro_name)
	{
		const std::uint64_t key = PairKey(paint, macro_name);
		const PaintId* cached = add_cache_.Find(key);
		if (cached)
			return *cached;
		Validate(paint);
		const PaintId id = AddAt(paint, macro_name, 31);
		add_cache_.Insert(key, id);
		RegisterRoot(id);
		return id;
	}

	PaintId Merge(PaintId first, PaintId second)
	{
		Validate(first);
		Validate(second);
		const PaintId id = MergeAt(first, second, 31);
		RegisterRoot(id);
		return id;
	}

private:
	struct Node
	{
		PaintId zero;
		PaintId one;

		Node() : zero(0), one(0) {}
		Node(PaintId zero_child, PaintId one_child)
			: zero(zero_child), one(one_child)
		{}
	};

	void Validate(PaintId paint) const
	{
		if (paint >= nodes_.size())
			throw std::logic_error("invalid macro paint ID");
	}

	PaintId InternNode(PaintId zero, PaintId one)
	{
		if (zero == 0 && one == 0)
			return 0;
		if (nodes_.size() >=
			static_cast<std::size_t>(std::numeric_limits<PaintId>::max()))
			throw std::runtime_error("too many macro paint trie nodes");
		const PaintId id = static_cast<PaintId>(nodes_.size());
		nodes_.push_back(Node(zero, one));
		if (stats_)
			root_flags_.push_back(0);
		UpdateStats();
		return id;
	}

	PaintId AddAt(PaintId node, SpellingId name, int bit)
	{
		if (bit < 0)
			return 1;
		const PaintId zero = node == 0 ? 0 : nodes_[node].zero;
		const PaintId one = node == 0 ? 0 : nodes_[node].one;
		if ((name >> bit) & 1U)
		{
			const PaintId added = AddAt(one, name, bit - 1);
			return added == one ? node : InternNode(zero, added);
		}
		const PaintId added = AddAt(zero, name, bit - 1);
		return added == zero ? node : InternNode(added, one);
	}

	PaintId MergeAt(PaintId first, PaintId second, int bit)
	{
		if (first == second || second == 0)
			return first;
		if (first == 0)
			return second;
		if (bit < 0)
			return 1;
		const PaintId low = std::min(first, second);
		const PaintId high = std::max(first, second);
		const std::uint64_t key = PairKey(low, high);
		const PaintId* cached = merge_cache_.Find(key);
		if (cached)
			return *cached;
		const PaintId zero = MergeAt(nodes_[first].zero,
			nodes_[second].zero, bit - 1);
		const PaintId one = MergeAt(nodes_[first].one,
			nodes_[second].one, bit - 1);
		PaintId result;
		if (zero == nodes_[first].zero && one == nodes_[first].one)
			result = first;
		else if (zero == nodes_[second].zero && one == nodes_[second].one)
			result = second;
		else
			result = InternNode(zero, one);
		merge_cache_.Insert(key, result);
		return result;
	}

	void RegisterRoot(PaintId root)
	{
		if (!stats_)
			return;
		if (!root_flags_[root])
		{
			root_flags_[root] = 1;
			++root_count_;
			UpdateStats();
		}
	}

	void UpdateStats()
	{
		if (!stats_)
			return;
		stats_->paint_roots = root_count_;
		stats_->paint_nodes = nodes_.size();
	}

	MacroProcessingStats* stats_;
	std::vector<Node> nodes_;
	std::vector<unsigned char> root_flags_;
	std::size_t root_count_;
	FlatHashMap<std::uint64_t, PaintId> add_cache_;
	FlatHashMap<std::uint64_t, PaintId> merge_cache_;
};

struct ReplacementToken
{
	Token token;
	std::size_t parameter;

	ReplacementToken() : parameter(kNoParameter) {}
};

struct Macro
{
	SpellingId name;
	bool function_like;
	bool variadic;
	std::vector<SpellingId> parameters;
	std::vector<ReplacementToken> replacement;

	Macro() : name(0), function_like(false), variadic(false) {}
};

struct Argument
{
	std::size_t raw_begin;
	std::size_t raw_size;
	std::size_t expanded_begin;
	std::size_t expanded_size;
	bool expanded_ready;
	bool preserve_raw;

	Argument()
		: raw_begin(0), raw_size(0), expanded_begin(0), expanded_size(0),
		  expanded_ready(false), preserve_raw(false)
	{}
};

struct ParsedArgument
{
	std::size_t begin;
	std::size_t size;

	ParsedArgument(std::size_t argument_begin, std::size_t argument_size)
		: begin(argument_begin), size(argument_size)
	{}
};

struct ParsedArguments
{
	// Top-level separator tokens remain between adjacent ranges.  This lets a
	// variadic binding use one contiguous slice, including its commas, while
	// fixed arguments refer to slices that exclude separators.
	std::vector<Token> tokens;
	std::vector<ParsedArgument> arguments;
};

struct InvocationScan
{
	bool active;
	std::size_t next_index;
	std::size_t depth;

	InvocationScan() : active(false), next_index(2), depth(0) {}

	void Reset()
	{
		active = false;
		next_index = 2;
		depth = 0;
	}
};

struct PendingExpansion
{
	bool active;
	SpellingId macro_name;
	Token head;
	std::vector<Token> raw_tokens;
	std::vector<Token> expanded_tokens;
	std::vector<Argument> arguments;
	std::vector<std::size_t> demanded_arguments;
	std::size_t raw_consumers;
	std::size_t next_demand;
	std::size_t active_argument;

	PendingExpansion()
		: active(false), macro_name(0), raw_consumers(0), next_demand(0),
		  active_argument(0)
	{}
};

struct ExpansionFrame
{
	std::deque<Token> input;
	std::vector<Token> output;
	std::vector<Token> replacement;
	InvocationScan scan;
	PendingExpansion pending;
	bool root;
	bool final;

	ExpansionFrame(bool is_root, bool is_final)
		: root(is_root), final(is_final)
	{}

	ExpansionFrame(ExpansionFrame&& other) noexcept
		: input(std::move(other.input)), output(std::move(other.output)),
		  replacement(std::move(other.replacement)), scan(other.scan),
		  pending(std::move(other.pending)), root(other.root), final(other.final)
	{}

private:
	ExpansionFrame(const ExpansionFrame&);
	ExpansionFrame& operator=(const ExpansionFrame&);
};

class GeneratedTokenCollector : public IPPTokenStream
{
public:
	GeneratedTokenCollector(SpellingTable& spellings,
		PaintId paint, PaintId blocked, std::uint64_t origin,
		bool parameter_origin, bool leading_space)
		: spellings_(spellings), paint_(paint),
		  blocked_(blocked), origin_(origin),
		  parameter_origin_(parameter_origin),
		  leading_space_(leading_space), count_(0)
	{}

	void emit_whitespace_sequence() {}
	void emit_new_line() {}
	void emit_eof() {}

	void emit_header_name(const std::string& data)
	{
		Add(TK_HEADER, data);
	}
	void emit_identifier(const std::string& data)
	{
		Add(TK_IDENTIFIER, data);
	}
	void emit_pp_number(const std::string& data)
	{
		Add(TK_PP_NUMBER, data);
	}
	void emit_character_literal(const std::string& data)
	{
		Add(TK_CHARACTER, data);
	}
	void emit_user_defined_character_literal(const std::string& data)
	{
		Add(TK_USER_CHARACTER, data);
	}
	void emit_string_literal(const std::string& data)
	{
		Add(TK_STRING, data);
	}
	void emit_user_defined_string_literal(const std::string& data)
	{
		Add(TK_USER_STRING, data);
	}
	void emit_preprocessing_op_or_punc(const std::string& data)
	{
		Add(TK_OPERATOR, data);
	}
	void emit_non_whitespace_char(const std::string& data)
	{
		Add(TK_NON_WHITESPACE, data);
	}

	Token Result()
	{
		if (count_ != 1)
			throw std::runtime_error(
				"token paste did not produce one preprocessing token");
		return std::move(result_);
	}

private:
	void Add(TokenKind kind, const std::string& spelling)
	{
		++count_;
		if (count_ != 1)
			return;
		result_ = kind == TK_IDENTIFIER ?
			Token(kind, spellings_.Intern(spelling), leading_space_) :
			Token(kind, spelling, leading_space_);
		result_.paint = paint_;
		result_.blocked = blocked_;
		result_.origin = origin_;
		result_.parameter_origin = parameter_origin_;
	}

	SpellingTable& spellings_;
	PaintId paint_;
	PaintId blocked_;
	std::uint64_t origin_;
	bool parameter_origin_;
	bool leading_space_;
	std::size_t count_;
	Token result_;
};

class MacroProcessor : public IPPTokenStream
{
public:
	MacroProcessor(IPostTokenStream& output, MacroProcessingStats* stats)
		: stats_(stats), spellings_(stats), paints_(stats), post_tokens_(output,
			stats ? &stats->postprocessing : 0), pending_space_(false),
		  boundary_space_(false), retained_replacement_tokens_(0),
		  next_origin_(1)
	{
		id_define_ = spellings_.Intern("define");
		id_undef_ = spellings_.Intern("undef");
		id_va_args_ = spellings_.Intern("__VA_ARGS__");
	}

	void emit_whitespace_sequence()
	{
		pending_space_ = true;
	}

	void emit_new_line()
	{
		ProcessLine();
		line_.clear();
		pending_space_ = false;
		if (stats_)
			++stats_->logical_lines;
	}

	void emit_header_name(const std::string& data)
	{
		AddSourceToken(TK_HEADER, data);
	}
	void emit_identifier(const std::string& data)
	{
		AddSourceToken(TK_IDENTIFIER, data);
	}
	void emit_pp_number(const std::string& data)
	{
		AddSourceToken(TK_PP_NUMBER, data);
	}
	void emit_character_literal(const std::string& data)
	{
		AddSourceToken(TK_CHARACTER, data);
	}
	void emit_user_defined_character_literal(const std::string& data)
	{
		AddSourceToken(TK_USER_CHARACTER, data);
	}
	void emit_string_literal(const std::string& data)
	{
		AddSourceToken(TK_STRING, data);
	}
	void emit_user_defined_string_literal(const std::string& data)
	{
		AddSourceToken(TK_USER_STRING, data);
	}
	void emit_preprocessing_op_or_punc(const std::string& data)
	{
		AddSourceToken(TK_OPERATOR, data);
	}
	void emit_non_whitespace_char(const std::string& data)
	{
		AddSourceToken(TK_NON_WHITESPACE, data);
	}

	void emit_eof()
	{
		if (!line_.empty())
			ProcessLine();
		Drain(rescan_, true, &pending_invocation_);
		post_tokens_.emit_eof();
	}

private:
	enum InvocationState
	{
		NOT_AN_INVOCATION,
		INCOMPLETE_INVOCATION,
		COMPLETE_INVOCATION
	};

	void AddSourceToken(TokenKind kind, const std::string& spelling)
	{
		line_.push_back(kind == TK_IDENTIFIER ?
			Token(kind, spellings_.Intern(spelling), pending_space_) :
			Token(kind, spelling, pending_space_));
		pending_space_ = false;
		if (stats_)
		{
			++stats_->source_tokens;
			stats_->peak_line_tokens = std::max(stats_->peak_line_tokens,
				line_.size());
		}
	}

	const std::string& Spell(const Token& token) const
	{
		if (token.spelling != 0)
			return spellings_.Get(token.spelling);
		return token.borrowed_spelling ? *token.borrowed_spelling :
			token.owned_spelling;
	}

	Token BorrowReplacementToken(const Token& source) const
	{
		Token result;
		result.kind = source.kind;
		result.spelling = source.spelling;
		result.borrowed_spelling = source.spelling == 0 ?
			(source.borrowed_spelling ? source.borrowed_spelling :
			 &source.owned_spelling) : 0;
		result.paint = source.paint;
		result.blocked = source.blocked;
		result.origin = source.origin;
		result.matching_distance = source.matching_distance;
		result.leading_space = source.leading_space;
		result.from_variadic = source.from_variadic;
		result.parameter_origin = source.parameter_origin;
		result.matching_comma = source.matching_comma;
		return result;
	}

	bool IsOperator(const Token& token, const char* spelling) const
	{
		if (token.kind != TK_OPERATOR)
			return false;
		const std::string& actual = Spell(token);
		if (actual == spelling)
			return true;
		return (spelling[0] == '#' && spelling[1] == '\0' && actual == "%:") ||
			(spelling[0] == '#' && spelling[1] == '#' &&
			 spelling[2] == '\0' && actual == "%:%:");
	}

	bool IsIdentifier(const Token& token, SpellingId spelling) const
	{
		return token.kind == TK_IDENTIFIER && token.spelling == spelling;
	}

	void AnnotateParentheses(std::vector<Token>* tokens)
	{
		std::vector<std::size_t> openings;
		for (std::size_t i = 0; i < tokens->size(); ++i)
		{
			(*tokens)[i].matching_distance = 0;
			(*tokens)[i].matching_comma = false;
			if (IsOperator((*tokens)[i], "("))
				openings.push_back(i);
			else if (IsOperator((*tokens)[i], ",") && !openings.empty())
				(*tokens)[openings.back()].matching_comma = true;
			else if (IsOperator((*tokens)[i], ")") && !openings.empty())
			{
				const std::size_t opening = openings.back();
				openings.pop_back();
				(*tokens)[opening].matching_distance = i - opening;
			}
		}
	}

	void ProcessLine()
	{
		AnnotateParentheses(&line_);
		if (!line_.empty() && IsOperator(line_[0], "#"))
		{
			Drain(rescan_, true, &pending_invocation_);
			boundary_space_ = false;
			ParseDirective();
			if (stats_)
				++stats_->directive_lines;
			return;
		}

		for (std::size_t i = 0; i < line_.size(); ++i)
			if (IsIdentifier(line_[i], id_va_args_))
				throw std::runtime_error(
					"__VA_ARGS__ outside a variadic replacement list");
		if (!line_.empty() && boundary_space_)
			line_[0].leading_space = true;
		for (std::size_t i = 0; i < line_.size(); ++i)
			rescan_.push_back(std::move(line_[i]));
		UpdatePeakRescan(rescan_.size());
		Drain(rescan_, false, &pending_invocation_);
		boundary_space_ = true;
	}

	void ParseDirective()
	{
		if (line_.size() < 2 || line_[1].kind != TK_IDENTIFIER)
			throw std::runtime_error("invalid preprocessing directive");
		if (line_[1].spelling == id_define_)
			ParseDefine();
		else if (line_[1].spelling == id_undef_)
			ParseUndef();
		else
			throw std::runtime_error("unsupported preprocessing directive");
	}

	void ParseUndef()
	{
		if (line_.size() != 3 || line_[2].kind != TK_IDENTIFIER ||
			line_[2].spelling == id_va_args_)
			throw std::runtime_error("invalid #undef directive");
		Macro* found = macros_.Find(line_[2].spelling);
		if (found)
		{
			retained_replacement_tokens_ -= found->replacement.size();
			macros_.Erase(line_[2].spelling);
		}
		if (stats_)
			++stats_->macro_undefinitions;
	}

	void ParseDefine()
	{
		if (line_.size() < 3 || line_[2].kind != TK_IDENTIFIER ||
			line_[2].spelling == id_va_args_)
			throw std::runtime_error("missing or invalid macro name");
		Macro macro;
		macro.name = line_[2].spelling;
		std::size_t replacement_begin = 3;
		if (line_.size() > 3 && IsOperator(line_[3], "(") &&
			!line_[3].leading_space)
		{
			macro.function_like = true;
			replacement_begin = ParseParameters(4, &macro);
		}
		else if (line_.size() > 3 && !line_[3].leading_space)
			throw std::runtime_error(
				"object-like macro replacement requires whitespace");

		BuildReplacement(replacement_begin, &macro);
		Macro* old = macros_.Find(macro.name);
		if (old)
		{
			if (!Equivalent(*old, macro))
				throw std::runtime_error("incompatible macro redefinition");
		}
		else
		{
			retained_replacement_tokens_ += macro.replacement.size();
			const SpellingId name = macro.name;
			macros_.Insert(name, std::move(macro));
			if (stats_)
				stats_->peak_retained_replacement_tokens = std::max(
					stats_->peak_retained_replacement_tokens,
					retained_replacement_tokens_);
		}
		if (stats_)
			++stats_->macro_definitions;
	}

	std::size_t ParseParameters(std::size_t position, Macro* macro)
	{
		FlatHashMap<SpellingId, unsigned char> seen;
		if (position >= line_.size())
			throw std::runtime_error("unterminated macro parameter list");
		if (IsOperator(line_[position], ")"))
			return position + 1;
		if (IsOperator(line_[position], "..."))
		{
			macro->variadic = true;
			++position;
			if (position >= line_.size() || !IsOperator(line_[position], ")"))
				throw std::runtime_error("invalid variadic parameter list");
			return position + 1;
		}

		while (true)
		{
			if (position >= line_.size() ||
				line_[position].kind != TK_IDENTIFIER ||
				line_[position].spelling == id_va_args_ ||
				seen.Find(line_[position].spelling))
				throw std::runtime_error("invalid macro parameter");
			seen.Insert(line_[position].spelling, 1);
			macro->parameters.push_back(line_[position].spelling);
			++position;
			if (position >= line_.size())
				throw std::runtime_error("unterminated macro parameter list");
			if (IsOperator(line_[position], ")"))
				return position + 1;
			if (!IsOperator(line_[position], ","))
				throw std::runtime_error("invalid macro parameter separator");
			++position;
			if (position >= line_.size())
				throw std::runtime_error("unterminated macro parameter list");
			if (IsOperator(line_[position], "..."))
			{
				macro->variadic = true;
				++position;
				if (position >= line_.size() ||
					!IsOperator(line_[position], ")"))
					throw std::runtime_error("invalid variadic parameter list");
				return position + 1;
			}
		}
	}

	void BuildReplacement(std::size_t begin, Macro* macro)
	{
		FlatHashMap<SpellingId, std::size_t> parameter_index;
		for (std::size_t i = 0; i < macro->parameters.size(); ++i)
			parameter_index.Insert(macro->parameters[i], i);
		if (macro->variadic)
			parameter_index.Insert(id_va_args_, macro->parameters.size());

		for (std::size_t i = begin; i < line_.size(); ++i)
		{
			ReplacementToken replacement;
			replacement.token = line_[i];
			if (i == begin)
				replacement.token.leading_space = false;
			if (replacement.token.kind == TK_IDENTIFIER)
			{
				const std::size_t* found = parameter_index.Find(
					replacement.token.spelling);
				if (found)
					replacement.parameter = *found;
				else if (replacement.token.spelling == id_va_args_)
					throw std::runtime_error(
						"__VA_ARGS__ in a non-variadic replacement list");
			}
			macro->replacement.push_back(replacement);
		}

		if (!macro->replacement.empty() &&
			(IsOperator(macro->replacement.front().token, "##") ||
			 IsOperator(macro->replacement.back().token, "##")))
			throw std::runtime_error("## at replacement-list boundary");
		for (std::size_t i = 0; i < macro->replacement.size(); ++i)
		{
			if (IsOperator(macro->replacement[i].token, "##") &&
				i + 1 < macro->replacement.size() &&
				IsOperator(macro->replacement[i + 1].token, "##"))
				throw std::runtime_error("adjacent ## operators");
			if (!macro->function_like ||
				!IsOperator(macro->replacement[i].token, "#"))
				continue;
			if (i + 1 >= macro->replacement.size() ||
				macro->replacement[i + 1].parameter == kNoParameter)
				throw std::runtime_error("# must precede a macro parameter");
		}
	}

	bool Equivalent(const Macro& first, const Macro& second) const
	{
		if (first.function_like != second.function_like ||
			first.variadic != second.variadic ||
			first.parameters != second.parameters ||
			first.replacement.size() != second.replacement.size())
			return false;
		for (std::size_t i = 0; i < first.replacement.size(); ++i)
		{
			const ReplacementToken& a = first.replacement[i];
			const ReplacementToken& b = second.replacement[i];
			if (a.token.kind != b.token.kind || Spell(a.token) != Spell(b.token) ||
				a.token.leading_space != b.token.leading_space ||
				a.parameter != b.parameter)
				return false;
		}
		return true;
	}

	InvocationState FindInvocation(const std::deque<Token>& input,
		std::size_t* close, InvocationScan* scan)
	{
		if (input.size() < 2)
			return INCOMPLETE_INVOCATION;
		if (!IsOperator(input[1], "("))
			return NOT_AN_INVOCATION;
		if (input[1].matching_distance != 0 &&
			1 + input[1].matching_distance < input.size() &&
			IsOperator(input[1 + input[1].matching_distance], ")"))
		{
			*close = 1 + input[1].matching_distance;
			if (scan)
				scan->Reset();
			return COMPLETE_INVOCATION;
		}
		std::size_t position = scan && scan->active ? scan->next_index : 2;
		std::size_t depth = scan && scan->active ? scan->depth : 0;
		for (; position < input.size(); ++position)
		{
			if (IsOperator(input[position], "("))
				++depth;
			else if (IsOperator(input[position], ")"))
			{
				if (depth == 0)
				{
					*close = position;
					if (scan)
						scan->Reset();
					return COMPLETE_INVOCATION;
				}
				--depth;
			}
		}
		if (scan)
		{
			scan->active = true;
			scan->next_index = position;
			scan->depth = depth;
		}
		return INCOMPLETE_INVOCATION;
	}

	bool ParameterPreservesRaw(const Macro& macro,
		std::size_t parameter) const
	{
		for (std::size_t i = 0; i < macro.replacement.size(); ++i)
		{
			const ReplacementToken& replacement = macro.replacement[i];
			if (macro.function_like && IsOperator(replacement.token, "#"))
			{
				if (macro.replacement[++i].parameter == parameter)
					return true;
				continue;
			}
			if (replacement.parameter == parameter &&
				((i != 0 && IsOperator(macro.replacement[i - 1].token, "##")) ||
				 (i + 1 < macro.replacement.size() &&
				  IsOperator(macro.replacement[i + 1].token, "##"))))
				return true;
		}
		return false;
	}

	bool CanHandOffSingleArgument(const Macro& macro,
		const std::deque<Token>& input, std::size_t close,
		const std::vector<std::size_t>& demanded) const
	{
		const std::size_t bindings = macro.parameters.size() +
			(macro.variadic ? 1 : 0);
		if (bindings != 1 || demanded.size() != 1 || demanded[0] != 0 ||
			ParameterPreservesRaw(macro, 0) || close + 1 != input.size())
			return false;
		if (macro.variadic && macro.parameters.empty())
			return true;
		return !macro.variadic && macro.parameters.size() == 1 &&
			input[1].matching_distance != 0 &&
			close == 1 + input[1].matching_distance &&
			!input[1].matching_comma;
	}

	void ExtractArguments(std::deque<Token>* input, std::size_t close,
		ParsedArguments* parsed)
	{
		input->pop_front();
		input->pop_front();
		std::size_t argument_begin = 0;
		std::size_t depth = 0;
		const std::size_t content_tokens = close - 2;
		parsed->tokens.reserve(content_tokens);
		for (std::size_t i = 0; i < content_tokens; ++i)
		{
			Token token = std::move(input->front());
			input->pop_front();
			if (IsOperator(token, "("))
				++depth;
			else if (IsOperator(token, ")"))
				--depth;
			if (depth == 0 && IsOperator(token, ","))
			{
				parsed->arguments.push_back(ParsedArgument(argument_begin,
					parsed->tokens.size() - argument_begin));
				parsed->tokens.push_back(token);
				argument_begin = parsed->tokens.size();
			}
			else
				parsed->tokens.push_back(token);
		}
		parsed->arguments.push_back(ParsedArgument(argument_begin,
			parsed->tokens.size() - argument_begin));
		input->pop_front();
	}

	void BindArguments(const Macro& macro, const Token& head,
		ParsedArguments* parsed, PendingExpansion* pending)
	{
		if (!macro.variadic && macro.parameters.empty() &&
			parsed->arguments.size() == 1 && parsed->arguments[0].size == 0)
			parsed->arguments.clear();
		if ((!macro.variadic &&
			 parsed->arguments.size() != macro.parameters.size()) ||
			(macro.variadic &&
			 parsed->arguments.size() < macro.parameters.size()))
			throw std::runtime_error("wrong number of macro arguments");

		const std::size_t binding_count = macro.parameters.size() +
			(macro.variadic ? 1 : 0);
		pending->raw_tokens.swap(parsed->tokens);
		pending->arguments.assign(binding_count, Argument());
		for (std::size_t i = 0; i < macro.parameters.size(); ++i)
		{
			pending->arguments[i].raw_begin = parsed->arguments[i].begin;
			pending->arguments[i].raw_size = parsed->arguments[i].size;
		}
		if (macro.variadic)
		{
			Argument& variadic = pending->arguments.back();
			const std::size_t first = macro.parameters.size();
			variadic.raw_begin = parsed->arguments[first].begin;
			const ParsedArgument& last = parsed->arguments.back();
			variadic.raw_size = last.begin + last.size - variadic.raw_begin;
		}
		for (std::size_t i = 0; i < macro.replacement.size(); ++i)
		{
			const ReplacementToken& replacement = macro.replacement[i];
			if (macro.function_like && IsOperator(replacement.token, "#"))
			{
				pending->arguments[
					macro.replacement[++i].parameter].preserve_raw = true;
				continue;
			}
			if (replacement.parameter != kNoParameter &&
				((i != 0 && IsOperator(macro.replacement[i - 1].token, "##")) ||
				 (i + 1 < macro.replacement.size() &&
				  IsOperator(macro.replacement[i + 1].token, "##"))))
					pending->arguments[
						replacement.parameter].preserve_raw = true;
		}

		// Argument prescan occurs before substitution into this replacement.
		// It inherits paint already carried by the invocation head, but the
		// current macro becomes unavailable only when the resulting argument
		// tokens are actually substituted.  Painting raw arguments eagerly
		// incorrectly suppresses nested calls such as f(g(x)).
		const PaintId inherited = head.paint;
		for (std::size_t i = 0; i < pending->arguments.size(); ++i)
		{
			Argument& argument = pending->arguments[i];
			if (inherited != 0 || head.blocked != 0)
			{
				for (std::size_t j = 0; j < argument.raw_size; ++j)
				{
					Token& token = pending->raw_tokens[argument.raw_begin + j];
					token.paint = paints_.Merge(token.paint, inherited);
					token.blocked = paints_.Merge(token.blocked, head.blocked);
				}
			}
			if (argument.raw_size != 0)
				pending->raw_tokens[argument.raw_begin].leading_space = false;
		}
	}

	std::vector<std::size_t> DemandedArguments(const Macro& macro) const
	{
		const std::size_t count = macro.parameters.size() +
			(macro.variadic ? 1 : 0);
		std::vector<bool> demanded(count, false);
		for (std::size_t i = 0; i < macro.replacement.size(); ++i)
		{
			const ReplacementToken& replacement = macro.replacement[i];
			if (macro.function_like && IsOperator(replacement.token, "#"))
			{
				++i;
				continue;
			}
			if (replacement.parameter == kNoParameter)
				continue;
			const bool adjacent =
				(i != 0 && IsOperator(macro.replacement[i - 1].token, "##")) ||
				(i + 1 < macro.replacement.size() &&
				 IsOperator(macro.replacement[i + 1].token, "##"));
			if (!adjacent)
				demanded[replacement.parameter] = true;
		}
		std::vector<std::size_t> result;
		for (std::size_t i = 0; i < demanded.size(); ++i)
			if (demanded[i])
				result.push_back(i);
		return result;
	}

	void PrepareRawArgumentLifetime(PendingExpansion* pending)
	{
		for (std::size_t i = 0; i < pending->arguments.size(); ++i)
			if (pending->arguments[i].preserve_raw)
				++pending->raw_consumers;
		for (std::size_t i = 0; i < pending->demanded_arguments.size(); ++i)
			if (!pending->arguments[
				pending->demanded_arguments[i]].preserve_raw)
				++pending->raw_consumers;
		if (pending->raw_consumers == 0)
			std::vector<Token>().swap(pending->raw_tokens);
	}

	Token Stringize(const Argument& argument,
		const std::vector<Token>& raw_tokens, bool leading_space,
		PaintId paint, PaintId blocked, std::uint64_t origin)
	{
		std::string spelling("\"");
		for (std::size_t i = 0; i < argument.raw_size; ++i)
		{
			const Token& token = raw_tokens[argument.raw_begin + i];
			if (i != 0 && token.leading_space)
				spelling.push_back(' ');
			const std::string& source = Spell(token);
			const bool escape = token.kind == TK_CHARACTER ||
				token.kind == TK_USER_CHARACTER || token.kind == TK_STRING ||
				token.kind == TK_USER_STRING;
			for (std::size_t j = 0; j < source.size(); ++j)
			{
				if (escape && (source[j] == '\\' || source[j] == '\"'))
					spelling.push_back('\\');
				spelling.push_back(source[j]);
			}
		}
		spelling.push_back('\"');
		Token result(TK_STRING, spelling, leading_space);
		result.paint = paint;
		result.blocked = blocked;
		result.origin = origin;
		return result;
	}

	void Instantiate(const Macro& macro, const Token& head,
		PendingExpansion* pending, std::vector<Token>* result)
	{
		const std::uint64_t origin = next_origin_++;
		const PaintId direct_paint = paints_.Add(
			head.parameter_origin ? 0 : head.paint, macro.name);
		const PaintId parameter_paint = paints_.Add(
			head.parameter_origin ? head.paint : 0, macro.name);
		result->clear();
		result->reserve(macro.replacement.size());
		bool paste_pending = false;
		for (std::size_t i = 0; i < macro.replacement.size(); ++i)
		{
			const ReplacementToken& replacement = macro.replacement[i];
			if (macro.function_like && IsOperator(replacement.token, "#"))
			{
				const ReplacementToken& parameter = macro.replacement[++i];
				AppendReplacementToken(Stringize(
					pending->arguments[parameter.parameter], pending->raw_tokens,
					replacement.token.leading_space, direct_paint,
					head.blocked, origin), &paste_pending, result);
				continue;
			}
			if (IsOperator(replacement.token, "##"))
			{
				if (paste_pending || result->empty())
					throw std::logic_error("invalid internal paste sequence");
				paste_pending = true;
				continue;
			}
			if (replacement.parameter != kNoParameter)
			{
				const bool adjacent =
					(i != 0 && IsOperator(macro.replacement[i - 1].token, "##")) ||
					(i + 1 < macro.replacement.size() &&
					 IsOperator(macro.replacement[i + 1].token, "##"));
				Argument& argument = pending->arguments[replacement.parameter];
				if (!adjacent && !argument.expanded_ready)
					throw std::logic_error("undemanded macro argument expansion");
				const std::vector<Token>& value = adjacent ?
					pending->raw_tokens : pending->expanded_tokens;
				const std::size_t value_begin = adjacent ?
					argument.raw_begin : argument.expanded_begin;
				const std::size_t value_size = adjacent ?
					argument.raw_size : argument.expanded_size;
				if (value_size == 0 && adjacent)
				{
					Token place;
					place.paint = direct_paint;
					place.blocked = head.blocked;
					place.origin = origin;
					place.parameter_origin = true;
					place.leading_space = replacement.token.leading_space;
					place.from_variadic = macro.variadic &&
						replacement.parameter == macro.parameters.size();
						AppendReplacementToken(std::move(place), &paste_pending,
							result);
				}
				else
				{
					for (std::size_t j = 0; j < value_size; ++j)
					{
						Token token = value[value_begin + j];
						// Parameter substitution breaks temporary nesting.  Start a
						// fresh ancestry at this invocation, while retaining the
						// permanent marks of identifiers that were already rejected.
						token.paint = adjacent ? 0 : parameter_paint;
						token.blocked = paints_.Merge(token.blocked, head.blocked);
						token.origin = origin;
						token.parameter_origin = true;
						if (j == 0)
						{
							token.leading_space = replacement.token.leading_space;
							token.from_variadic = macro.variadic &&
								replacement.parameter == macro.parameters.size();
						}
							AppendReplacementToken(std::move(token), &paste_pending,
								result);
					}
				}
				continue;
			}
			Token literal = BorrowReplacementToken(replacement.token);
			literal.paint = direct_paint;
			literal.blocked = head.blocked;
			literal.origin = origin;
			literal.parameter_origin = false;
			AppendReplacementToken(std::move(literal), &paste_pending, result);
		}
		if (paste_pending)
			throw std::logic_error("invalid internal paste sequence");
		result->erase(std::remove_if(result->begin(), result->end(),
			IsPlacemarker), result->end());
		for (std::size_t i = 0; i < result->size(); ++i)
			(*result)[i].from_variadic = false;
		if (!result->empty())
			(*result)[0].leading_space =
				(*result)[0].leading_space || head.leading_space;
		AnnotateParentheses(result);
	}

	void AppendReplacementToken(Token token, bool* paste_pending,
		std::vector<Token>* output)
	{
		if (!*paste_pending)
		{
			output->push_back(std::move(token));
			return;
		}
		if (output->empty())
			throw std::logic_error("invalid internal paste sequence");
		Token left = std::move(output->back());
		output->pop_back();
		if (token.from_variadic && left.kind != TK_PLACEMARKER &&
			IsOperator(left, ","))
		{
			if (token.kind == TK_PLACEMARKER)
			{
				Token place;
				place.leading_space = left.leading_space;
				place.paint = paints_.Merge(left.paint, token.paint);
				place.blocked = paints_.Merge(left.blocked, token.blocked);
				place.origin = left.origin;
				place.parameter_origin = left.parameter_origin &&
					token.parameter_origin;
				output->push_back(std::move(place));
			}
			else
			{
				output->push_back(std::move(left));
				output->push_back(std::move(token));
			}
		}
		else
			output->push_back(Paste(std::move(left), std::move(token)));
		*paste_pending = false;
	}

	static bool IsPlacemarker(const Token& token)
	{
		return token.kind == TK_PLACEMARKER;
	}

	Token Paste(Token left, Token right)
	{
		if (left.kind == TK_PLACEMARKER)
		{
			Token result = std::move(right);
			result.leading_space = left.leading_space || right.leading_space;
			result.paint = paints_.Merge(left.paint, right.paint);
			result.blocked = paints_.Merge(left.blocked, right.blocked);
			result.parameter_origin = left.parameter_origin &&
				right.parameter_origin;
			return result;
		}
		if (right.kind == TK_PLACEMARKER)
		{
			Token result = std::move(left);
			result.paint = paints_.Merge(left.paint, right.paint);
			result.blocked = paints_.Merge(left.blocked, right.blocked);
			result.parameter_origin = left.parameter_origin &&
				right.parameter_origin;
			return result;
		}
		const std::string spelling = Spell(left) + Spell(right);
		GeneratedTokenCollector collector(spellings_,
			paints_.Merge(left.paint, right.paint),
			paints_.Merge(left.blocked, right.blocked), left.origin,
			left.parameter_origin && right.parameter_origin,
			left.leading_space);
		TokenizeGeneratedPreprocessingToken(spelling, collector);
		if (stats_)
		{
			++stats_->pasted_tokens;
			stats_->pasted_spelling_bytes += spelling.size();
		}
		try
		{
			return collector.Result();
		}
		catch (const std::runtime_error&)
		{
			throw std::runtime_error("invalid token paste: " + spelling);
		}
	}

	static std::size_t ArgumentStorageBytes(const PendingExpansion& pending)
	{
		return pending.raw_tokens.capacity() * sizeof(Token) +
			pending.expanded_tokens.capacity() * sizeof(Token) +
			pending.arguments.capacity() * sizeof(Argument) +
			pending.demanded_arguments.capacity() * sizeof(std::size_t);
	}

	void UpdateExpansionFramePeak(std::size_t frame_count)
	{
		if (stats_)
			stats_->peak_expansion_frames = std::max(
				stats_->peak_expansion_frames, frame_count);
	}

	void UpdateArgumentStoragePeak(std::size_t bytes)
	{
		if (stats_)
			stats_->peak_argument_storage_bytes = std::max(
				stats_->peak_argument_storage_bytes, bytes);
	}

	static void ReplaceLiveArgumentStorage(std::size_t old_bytes,
		std::size_t new_bytes, std::size_t* live_bytes)
	{
		if (new_bytes >= old_bytes)
			*live_bytes += new_bytes - old_bytes;
		else
		{
			const std::size_t released = old_bytes - new_bytes;
			if (released > *live_bytes)
				throw std::logic_error("invalid argument storage accounting");
			*live_bytes -= released;
		}
	}

	ExpansionFrame HandOffSingleArgument(const Macro& macro,
		const Token& invocation_head, std::vector<std::size_t>* demanded,
		ExpansionFrame* frame, std::size_t* live_argument_storage)
	{
		frame->input.pop_front();
		frame->input.pop_front();
		frame->input.pop_back();
		if (invocation_head.paint != 0 || invocation_head.blocked != 0)
		{
			for (std::size_t i = 0; i < frame->input.size(); ++i)
			{
				frame->input[i].paint = paints_.Merge(
					frame->input[i].paint, invocation_head.paint);
				frame->input[i].blocked = paints_.Merge(
					frame->input[i].blocked, invocation_head.blocked);
			}
		}
		if (!frame->input.empty())
			frame->input.front().leading_space = false;
		frame->scan.Reset();
		frame->pending.active = true;
		frame->pending.macro_name = macro.name;
		frame->pending.head = invocation_head;
		frame->pending.arguments.assign(1, Argument());
		frame->pending.demanded_arguments.swap(*demanded);
		frame->pending.next_demand = 1;
		frame->pending.active_argument = 0;
		const std::size_t pending_storage =
			ArgumentStorageBytes(frame->pending);
		*live_argument_storage += pending_storage;
		UpdateArgumentStoragePeak(*live_argument_storage);
		ExpansionFrame child(false, true);
		child.input.swap(frame->input);
		return child;
	}

	void Drain(std::deque<Token>& input, bool final, InvocationScan* scan)
	{
		std::size_t live_argument_storage = 0;
		std::vector<ExpansionFrame> frames;
		frames.push_back(ExpansionFrame(true, final));
		UpdateExpansionFramePeak(frames.size());
		frames.back().input.swap(input);
		if (scan)
			frames.back().scan = *scan;
		while (!frames.empty())
		{
			ExpansionFrame& frame = frames.back();
			if (frame.pending.active)
			{
				if (frame.pending.next_demand <
					frame.pending.demanded_arguments.size())
				{
					const std::size_t argument_index =
						frame.pending.demanded_arguments[
							frame.pending.next_demand++];
					Argument& argument =
						frame.pending.arguments[argument_index];
					if (argument.expanded_ready)
						continue;
					frame.pending.active_argument = argument_index;
					ExpansionFrame child(false, true);
					const std::size_t old_storage =
						ArgumentStorageBytes(frame.pending);
					for (std::size_t i = 0; i < argument.raw_size; ++i)
					{
						Token& token = frame.pending.raw_tokens[
							argument.raw_begin + i];
						if (argument.preserve_raw)
							child.input.push_back(token);
						else
							child.input.push_back(std::move(token));
					}
					if (!argument.preserve_raw)
					{
						if (frame.pending.raw_consumers == 0)
							throw std::logic_error("missing raw argument consumer");
						--frame.pending.raw_consumers;
						if (frame.pending.raw_consumers == 0)
							std::vector<Token>().swap(
								frame.pending.raw_tokens);
					}
					const std::size_t new_storage =
						ArgumentStorageBytes(frame.pending);
					ReplaceLiveArgumentStorage(old_storage, new_storage,
						&live_argument_storage);
					UpdatePeakRescan(child.input.size());
					frames.push_back(std::move(child));
					UpdateExpansionFramePeak(frames.size());
					continue;
				}
				const Macro* found = macros_.Find(frame.pending.macro_name);
				if (!found)
					throw std::logic_error("pending macro definition disappeared");
				const Token invocation_head = frame.pending.head;
				Instantiate(*found, invocation_head, &frame.pending,
					&frame.replacement);
				const std::size_t released = ArgumentStorageBytes(frame.pending);
				if (released > live_argument_storage)
					throw std::logic_error("invalid argument storage accounting");
				live_argument_storage -= released;
				frame.pending = PendingExpansion();
				if (frame.replacement.empty() && invocation_head.leading_space &&
					!frame.input.empty())
					frame.input.front().leading_space = true;
				for (std::vector<Token>::reverse_iterator i =
					frame.replacement.rbegin(); i != frame.replacement.rend(); ++i)
					frame.input.push_front(std::move(*i));
				frame.scan.Reset();
				if (stats_)
				{
					++stats_->macro_invocations;
					stats_->expanded_tokens += frame.replacement.size();
				}
				UpdatePeakRescan(frame.input.size());
				continue;
			}
			if (frame.input.empty())
			{
				if (frame.root)
				{
					if (scan)
						*scan = frame.scan;
					frame.input.swap(input);
					frames.pop_back();
					continue;
				}
				std::vector<Token> result;
				result.swap(frame.output);
				frames.pop_back();
				ExpansionFrame& parent = frames.back();
				Argument& argument = parent.pending.arguments[
					parent.pending.active_argument];
				const std::size_t old_storage =
					ArgumentStorageBytes(parent.pending);
				argument.expanded_begin =
					parent.pending.expanded_tokens.size();
				argument.expanded_size = result.size();
				if (parent.pending.expanded_tokens.empty())
					parent.pending.expanded_tokens.swap(result);
				else
					parent.pending.expanded_tokens.insert(
						parent.pending.expanded_tokens.end(),
						std::make_move_iterator(result.begin()),
						std::make_move_iterator(result.end()));
				argument.expanded_ready = true;
				const std::size_t new_storage =
					ArgumentStorageBytes(parent.pending);
				ReplaceLiveArgumentStorage(old_storage, new_storage,
					&live_argument_storage);
				UpdateArgumentStoragePeak(live_argument_storage);
				if (stats_)
					++stats_->argument_prescans;
				continue;
			}
			const Token& head = frame.input.front();
			if (head.kind == TK_IDENTIFIER)
			{
				if (head.spelling == id_va_args_)
					throw std::runtime_error(
						"__VA_ARGS__ outside a variadic replacement list");
				if (stats_)
					++stats_->macro_lookups;
				const Macro* found = macros_.Find(head.spelling);
				if (found &&
					!paints_.Contains(head.paint, head.spelling) &&
					!paints_.Contains(head.blocked, head.spelling))
				{
					const Macro& macro = *found;
					std::size_t close = 0;
					Token invocation_head = head;
					if (macro.function_like)
					{
						const InvocationState state = FindInvocation(frame.input,
							&close, &frame.scan);
						if (state == INCOMPLETE_INVOCATION)
						{
							if (!frame.final)
							{
								if (!frame.root || frames.size() != 1)
									throw std::logic_error(
										"nonfinal nested expansion frame");
								if (scan)
									*scan = frame.scan;
								frame.input.swap(input);
								return;
							}
							if (frame.input.size() == 1)
								goto emit_head;
							throw std::runtime_error("unterminated macro invocation");
						}
						if (state == NOT_AN_INVOCATION)
							goto emit_head;
						// A function name written directly by one replacement can
						// become callable only when it meets a parenthesis from the
						// surrounding stream.  That tail call begins a fresh nesting
						// boundary (the FILLER_0/FILLER_1 deferral pattern).  A name
						// selected through a parameter deliberately retains its
						// caller ancestry, as required by g(f)(g)(3).
						if (head.origin != 0 && !head.parameter_origin &&
							frame.input[1].origin != head.origin)
							invocation_head.paint = 0;
					}
					if (macro.function_like)
					{
						std::vector<std::size_t> demanded =
							DemandedArguments(macro);
						if (CanHandOffSingleArgument(macro, frame.input, close,
							demanded))
						{
							ExpansionFrame child = HandOffSingleArgument(macro,
								invocation_head, &demanded, &frame,
								&live_argument_storage);
							UpdatePeakRescan(child.input.size());
							frames.push_back(std::move(child));
							UpdateExpansionFramePeak(frames.size());
							continue;
						}
						ParsedArguments parsed;
						ExtractArguments(&frame.input, close, &parsed);
						frame.scan.Reset();
						frame.pending.active = true;
						frame.pending.macro_name = macro.name;
						frame.pending.head = invocation_head;
						BindArguments(macro, invocation_head, &parsed,
							&frame.pending);
						frame.pending.demanded_arguments.swap(demanded);
						PrepareRawArgumentLifetime(&frame.pending);
						const std::size_t pending_storage =
							ArgumentStorageBytes(frame.pending);
						UpdateArgumentStoragePeak(live_argument_storage +
							pending_storage + parsed.arguments.capacity() *
								sizeof(ParsedArgument));
						live_argument_storage += pending_storage;
						continue;
					}
					else
					{
						frame.input.pop_front();
						PendingExpansion no_arguments;
						Instantiate(macro, invocation_head, &no_arguments,
							&frame.replacement);
						if (frame.replacement.empty() &&
							invocation_head.leading_space &&
							!frame.input.empty())
							frame.input.front().leading_space = true;
						for (std::vector<Token>::reverse_iterator i =
							frame.replacement.rbegin();
							i != frame.replacement.rend(); ++i)
							frame.input.push_front(std::move(*i));
						frame.scan.Reset();
						if (stats_)
						{
							++stats_->macro_invocations;
							stats_->expanded_tokens += frame.replacement.size();
						}
						UpdatePeakRescan(frame.input.size());
						continue;
					}
				}
			}
	emit_head:
			Token emitted = std::move(frame.input.front());
			frame.input.pop_front();
			frame.scan.Reset();
			if (emitted.kind == TK_IDENTIFIER &&
				paints_.Contains(emitted.paint, emitted.spelling))
				emitted.blocked = paints_.Add(emitted.blocked, emitted.spelling);
			if (!frame.root)
				frame.output.push_back(std::move(emitted));
			else
				EmitPostToken(emitted);
		}
		if (live_argument_storage != 0)
			throw std::logic_error("macro argument storage escaped expansion");
	}

	void EmitPostToken(const Token& token)
	{
		const std::string& spelling = Spell(token);
		switch (token.kind)
		{
		case TK_IDENTIFIER: post_tokens_.emit_identifier(spelling); break;
		case TK_PP_NUMBER: post_tokens_.emit_pp_number(spelling); break;
		case TK_CHARACTER:
			post_tokens_.emit_character_literal(spelling); break;
		case TK_USER_CHARACTER:
			post_tokens_.emit_user_defined_character_literal(spelling); break;
		case TK_STRING: post_tokens_.emit_string_literal(spelling); break;
		case TK_USER_STRING:
			post_tokens_.emit_user_defined_string_literal(spelling); break;
		case TK_OPERATOR:
			post_tokens_.emit_preprocessing_op_or_punc(spelling); break;
		case TK_NON_WHITESPACE:
			post_tokens_.emit_non_whitespace_char(spelling); break;
		case TK_HEADER: post_tokens_.emit_header_name(spelling); break;
		case TK_PLACEMARKER:
			throw std::logic_error("placemarker escaped paste resolution");
		}
	}

	void UpdatePeakRescan(std::size_t size)
	{
		if (stats_)
			stats_->peak_rescan_tokens = std::max(
				stats_->peak_rescan_tokens, size);
	}

	MacroProcessingStats* stats_;
	SpellingTable spellings_;
	PaintTable paints_;
	PostTokenizationSession post_tokens_;
	FlatHashMap<SpellingId, Macro> macros_;
	std::vector<Token> line_;
	std::deque<Token> rescan_;
	InvocationScan pending_invocation_;
	bool pending_space_;
	bool boundary_space_;
	std::size_t retained_replacement_tokens_;
	std::uint64_t next_origin_;
	SpellingId id_define_;
	SpellingId id_undef_;
	SpellingId id_va_args_;
};

}

MacroProcessingStats::MacroProcessingStats()
	: logical_lines(0), directive_lines(0), source_tokens(0),
	  interned_identifiers(0), interned_identifier_bytes(0),
	  macro_definitions(0), macro_undefinitions(0), macro_lookups(0),
	  macro_invocations(0), argument_prescans(0), expanded_tokens(0),
	  pasted_tokens(0), pasted_spelling_bytes(0), peak_line_tokens(0),
	  peak_rescan_tokens(0), peak_retained_replacement_tokens(0),
	  peak_expansion_frames(0), peak_argument_storage_bytes(0),
	  paint_roots(0), paint_nodes(0), elapsed_nanoseconds(0)
{}

void ProcessMacros(const std::string& source, IPostTokenStream& output,
	MacroProcessingStats* stats)
{
	const std::chrono::steady_clock::time_point start = stats ?
		std::chrono::steady_clock::now() :
		std::chrono::steady_clock::time_point();
	if (stats)
		*stats = MacroProcessingStats();
	MacroProcessor processor(output, stats);
	TokenizePreprocessingFile(source, processor,
		stats ? &stats->tokenization : 0);
	if (stats)
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - start).count());
}

}
