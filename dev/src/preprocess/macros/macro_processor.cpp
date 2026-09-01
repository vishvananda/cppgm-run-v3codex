#include "preprocess/macros/macro_processor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "preprocess/expressions/control_expression.h"
#include "support/containers/flat_hash_map.h"
#include "preprocess/hosted/preprocessor_probes.h"
#include "preprocess/tokens/IPPTokenStream.h"
#include "preprocess/macros/macro_operator_code.h"
#include "preprocess/tokens/pp_tokenizer.h"
#include "support/exceptions.h"

namespace cppgm
{
namespace
{

typedef std::uint32_t SpellingId;
typedef std::uint32_t PaintId;

using detail::FlatHashMap;
using detail::MixedHash;
using namespace macro_detail;

const std::size_t kNoParameter = std::numeric_limits<std::size_t>::max();
const PaintId kSingletonPaint = UINT32_C(0x80000000);

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
	SpellingId source_file;
	SpellingId physical_file;
	std::size_t source_line, source_column;
	std::size_t matching_distance;
	std::uint8_t tracked_operator;
	bool leading_space;
	bool from_variadic;
	bool parameter_origin;
	bool matching_comma;

	Token()
		: kind(TK_PLACEMARKER), spelling(0), borrowed_spelling(0), paint(0),
		  blocked(0), origin(0), source_file(0), physical_file(0),
		  source_line(0), source_column(0), matching_distance(0),
		  tracked_operator(TRACKED_OPERATOR_NONE),
		  leading_space(false),
		  from_variadic(false), parameter_origin(false), matching_comma(false)
	{}

	Token(TokenKind token_kind, SpellingId token_spelling,
		bool has_leading_space)
		: kind(token_kind), spelling(token_spelling), borrowed_spelling(0),
		  paint(0), blocked(0), origin(0), source_file(0), physical_file(0),
		  source_line(0), source_column(0), matching_distance(0),
		  tracked_operator(TRACKED_OPERATOR_NONE),
		  leading_space(has_leading_space), from_variadic(false),
		  parameter_origin(false), matching_comma(false)
	{}

	Token(TokenKind token_kind, const std::string& token_spelling,
		bool has_leading_space)
		: kind(token_kind), spelling(0), owned_spelling(token_spelling),
		  borrowed_spelling(0), paint(0), blocked(0), origin(0),
		  source_file(0), physical_file(0), source_line(0), source_column(0),
		  matching_distance(0), tracked_operator(token_kind == TK_OPERATOR ?
			TrackOperatorSpelling(token_spelling) : TRACKED_OPERATOR_NONE),
		  leading_space(has_leading_space), from_variadic(false),
		  parameter_origin(false), matching_comma(false)
	{}
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
		// The high bit is reserved for allocation-free singleton paint sets.
		if (spellings_.size() >= static_cast<std::size_t>(kSingletonPaint))
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
	explicit PaintTable(MacroProcessingStats* stats)
		: stats_(stats), singleton_count_(0)
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
		if (IsSingleton(paint))
			return SingletonName(paint) == macro_name;
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
		PaintId id;
		if (paint == 0)
			id = MakeSingleton(macro_name);
		else if (IsSingleton(paint) && SingletonName(paint) == macro_name)
			id = paint;
		else
		{
			if (IsSingleton(paint))
				paint = MaterializeSingleton(SingletonName(paint));
			id = AddAt(paint, macro_name, 31);
		}
		add_cache_.Insert(key, id);
		RegisterRoot(id);
		return id;
	}

	PaintId Merge(PaintId first, PaintId second)
	{
		Validate(first);
		Validate(second);
		if (first == second || second == 0)
		{
			RegisterRoot(first);
			return first;
		}
		if (first == 0)
		{
			RegisterRoot(second);
			return second;
		}
		if (IsSingleton(first))
			first = MaterializeSingleton(SingletonName(first));
		if (IsSingleton(second))
			second = MaterializeSingleton(SingletonName(second));
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

	static bool IsSingleton(PaintId paint)
	{
		return (paint & kSingletonPaint) != 0;
	}

	static SpellingId SingletonName(PaintId paint)
	{
		return paint & ~kSingletonPaint;
	}

	static PaintId MakeSingleton(SpellingId name)
	{
		if (name == 0 || (name & kSingletonPaint) != 0)
			throw std::logic_error("invalid singleton paint name");
		return kSingletonPaint | name;
	}

	void Validate(PaintId paint) const
	{
		if (IsSingleton(paint))
		{
			if (SingletonName(paint) == 0)
				throw std::logic_error("invalid singleton paint ID");
			return;
		}
		if (paint >= nodes_.size())
			throw std::logic_error("invalid macro paint ID");
	}

	PaintId InternNode(PaintId zero, PaintId one)
	{
		if (zero == 0 && one == 0)
			return 0;
		if (nodes_.size() >= static_cast<std::size_t>(kSingletonPaint))
			throw std::runtime_error("too many macro paint trie nodes");
		const PaintId id = static_cast<PaintId>(nodes_.size());
		nodes_.push_back(Node(zero, one));
		if (stats_)
			root_flags_.push_back(0);
		UpdateStats();
		return id;
	}

	PaintId MaterializeSingleton(SpellingId name)
	{
		const PaintId* cached = singleton_tries_.Find(name);
		if (cached)
			return *cached;
		const PaintId result = AddAt(0, name, 31);
		singleton_tries_.Insert(name, result);
		return result;
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
		if (IsSingleton(root))
		{
			const SpellingId name = SingletonName(root);
			if (!singleton_roots_.Find(name))
			{
				singleton_roots_.Insert(name, 1);
				++singleton_count_;
				++root_count_;
				UpdateStats();
			}
			return;
		}
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
		stats_->paint_singletons = singleton_count_;
		stats_->paint_nodes = nodes_.size();
	}

	MacroProcessingStats* stats_;
	std::vector<Node> nodes_;
	std::vector<unsigned char> root_flags_;
	std::size_t root_count_;
	std::size_t singleton_count_;
	FlatHashMap<std::uint64_t, PaintId> add_cache_;
	FlatHashMap<std::uint64_t, PaintId> merge_cache_;
	FlatHashMap<SpellingId, PaintId> singleton_tries_;
	FlatHashMap<SpellingId, unsigned char> singleton_roots_;
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
	SpellingId named_variadic_parameter;
	std::vector<SpellingId> parameters;
	std::vector<ReplacementToken> replacement;

	Macro() : name(0), function_like(false), variadic(false),
		named_variadic_parameter(0) {}
};

struct FileIdentity
{
	std::uint64_t device;
	std::uint64_t inode;

	bool operator==(const FileIdentity& other) const
	{
		return device == other.device && inode == other.inode;
	}
};

struct FileIdentityHash
{
	std::size_t operator()(const FileIdentity& id) const
	{
		return MixedHash(static_cast<std::size_t>(id.device)) ^
			(MixedHash(static_cast<std::size_t>(id.inode)) << 1);
	}
};

struct SourceFrame
{
	SpellingId physical_file;
	SpellingId presumed_file;
	std::size_t physical_base;
	std::size_t presumed_base;
	std::size_t current_physical_line, current_physical_column;
	std::size_t conditional_base;
	bool line_override;
	std::size_t override_line;
	SpellingId override_file;
	std::size_t include_search_index;
	bool has_include_search_index;

	SourceFrame()
		: physical_file(0), presumed_file(0), physical_base(1),
		  presumed_base(1), current_physical_line(1), current_physical_column(1),
		  conditional_base(0),
		  line_override(false), override_line(1), override_file(0),
		  include_search_index(0), has_include_search_index(false)
	{}
};

struct ConditionalFrame
{
	bool parent_active;
	bool active;
	bool branch_taken;
	bool saw_else;

	ConditionalFrame(bool parent, bool current, bool taken)
		: parent_active(parent), active(current), branch_taken(taken),
		  saw_else(false)
	{}
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
		bool parameter_origin, bool leading_space, SpellingId source_file,
		SpellingId physical_file, std::size_t source_line,
		std::size_t source_column)
		: spellings_(spellings), paint_(paint),
		  blocked_(blocked), origin_(origin),
		  parameter_origin_(parameter_origin),
		  leading_space_(leading_space), source_file_(source_file),
		  physical_file_(physical_file), source_line_(source_line),
		  source_column_(source_column), count_(0)
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

	bool TakeResult(Token* result)
	{
		if (count_ != 1)
			return false;
		*result = std::move(result_);
		return true;
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
		result_.source_file = source_file_;
		result_.physical_file = physical_file_;
		result_.source_line = source_line_;
		result_.source_column = source_column_;
		result_.parameter_origin = parameter_origin_;
	}

	SpellingTable& spellings_;
	PaintId paint_;
	PaintId blocked_;
	std::uint64_t origin_;
	bool parameter_origin_;
	bool leading_space_;
	SpellingId source_file_;
	SpellingId physical_file_;
	std::size_t source_line_, source_column_;
	std::size_t count_;
	Token result_;
};

class MacroProcessor : public IPPTokenStream
{
public:
	MacroProcessor(IPostTokenStream& output, MacroProcessingStats* stats)
		: stats_(stats), spellings_(stats), paints_(stats), post_tokens_(output,
			stats ? &stats->postprocessing : 0), condition_evaluator_(0),
		  condition_post_tokens_(condition_evaluator_, 0),
		  pending_space_(false), boundary_space_(false),
		  line_spelling_bytes_(0), retained_replacement_tokens_(0),
		  next_origin_(1), full_preprocessing_(false), preprocessing_stats_(0),
		  options_(0), live_source_bytes_(0), capture_(0), counter_(0)
	{
		InitializeNames();
	}

	MacroProcessor(IPostTokenStream& output,
		const PreprocessingOptions& options, PreprocessingStats* stats)
		: stats_(stats ? &stats->macros : 0),
		  spellings_(stats ? &stats->macros : 0),
		  paints_(stats ? &stats->macros : 0),
		  post_tokens_(output, stats ? &stats->macros.postprocessing : 0),
		  condition_evaluator_(stats ? &stats->condition_evaluation : 0),
		  condition_post_tokens_(condition_evaluator_,
			stats ? &stats->condition_evaluation.tokenization : 0),
		  pending_space_(false), boundary_space_(false),
		  line_spelling_bytes_(0), retained_replacement_tokens_(0), next_origin_(1),
		  full_preprocessing_(true), preprocessing_stats_(stats),
		  options_(&options), live_source_bytes_(0), capture_(0), counter_(0)
	{
		InitializeNames();
		InstallPredefinedMacros();
	}

	void set_source_line(std::size_t line)
	{
		if (full_preprocessing_ && !sources_.empty())
			sources_.back().current_physical_line = line;
	}

	void set_source_location(std::size_t line, std::size_t column)
	{
		if (full_preprocessing_ && !sources_.empty())
		{
			sources_.back().current_physical_line = line;
			sources_.back().current_physical_column = column;
		}
	}

	void emit_whitespace_sequence()
	{
		pending_space_ = true;
	}

	void emit_new_line()
	{
		ProcessLine();
		line_.clear();
		line_spelling_bytes_ = 0;
		pending_space_ = false;
		if (full_preprocessing_ && !sources_.empty())
			ApplyLineTransition();
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
		if (full_preprocessing_)
		{
			if (sources_.empty() ||
				conditionals_.size() != sources_.back().conditional_base)
				throw std::runtime_error("unterminated conditional inclusion");
			line_.clear();
			line_spelling_bytes_ = 0;
			return;
		}
		Drain(rescan_, true, &pending_invocation_);
		post_tokens_.emit_eof();
	}

	void ProcessPrimary(const std::string& path, const std::string& source)
	{
		if (!full_preprocessing_ || !sources_.empty())
			throw std::logic_error("invalid primary preprocessing session");
		if (options_->hosted_predefined_source &&
			*options_->hosted_predefined_source)
			ProcessSource("<built-in>", options_->hosted_predefined_source,
				false, 0);
		ApplyCommandLineActions();
		for (std::size_t i = 0; i < options_->forced_includes.size(); ++i)
			ProcessForcedInclude(options_->forced_includes[i]);
		ProcessSource(path, source, false, 0);
		Drain(rescan_, true, &pending_invocation_);
		RequireCompletePragmaOperator();
		post_tokens_.emit_eof();
	}

private:
	enum InvocationState
	{
		NOT_AN_INVOCATION,
		INCOMPLETE_INVOCATION,
		COMPLETE_INVOCATION
	};

	void InitializeNames()
	{
		id_define_ = spellings_.Intern("define");
		id_undef_ = spellings_.Intern("undef");
		id_if_ = spellings_.Intern("if");
		id_ifdef_ = spellings_.Intern("ifdef");
		id_ifndef_ = spellings_.Intern("ifndef");
		id_elif_ = spellings_.Intern("elif");
		id_else_ = spellings_.Intern("else");
		id_endif_ = spellings_.Intern("endif");
		id_include_ = spellings_.Intern("include");
		id_include_next_ = spellings_.Intern("include_next");
		id_line_ = spellings_.Intern("line");
		id_error_ = spellings_.Intern("error");
		id_warning_ = spellings_.Intern("warning");
		id_pragma_ = spellings_.Intern("pragma");
		id_once_ = spellings_.Intern("once");
		id_defined_ = spellings_.Intern("defined");
		id_va_args_ = spellings_.Intern("__VA_ARGS__");
		id_file_macro_ = spellings_.Intern("__FILE__");
		id_line_macro_ = spellings_.Intern("__LINE__");
		id_counter_macro_ = spellings_.Intern("__COUNTER__");
		id_pragma_operator_ = spellings_.Intern("_Pragma");
	}

	void AddBuiltinObject(const char* name, TokenKind kind,
		const std::string& replacement)
	{
		Macro macro;
		macro.name = spellings_.Intern(name);
		ReplacementToken token;
		token.token = kind == TK_IDENTIFIER ?
			Token(kind, spellings_.Intern(replacement), false) :
			Token(kind, replacement, false);
		macro.replacement.push_back(token);
		retained_replacement_tokens_ += macro.replacement.size();
		macros_.Insert(macro.name, std::move(macro));
	}

	void InstallPredefinedMacros()
	{
		AddBuiltinObject("__CPPGM__", TK_PP_NUMBER, "201303L");
		AddBuiltinObject("__cplusplus", TK_PP_NUMBER, "201103L");
		AddBuiltinObject("__STDC_HOSTED__", TK_PP_NUMBER, "1");
		AddBuiltinObject("__x86_64__", TK_PP_NUMBER, "1");
		AddBuiltinObject("__linux__", TK_PP_NUMBER, "1");
		AddBuiltinObject("__CPPGM_AUTHOR__", TK_STRING,
			QuoteString(options_->author));
		AddBuiltinObject("__DATE__", TK_STRING,
			QuoteString(options_->build_date));
		AddBuiltinObject("__TIME__", TK_STRING,
			QuoteString(options_->build_time));
		AddBuiltinMarker("__has_attribute");
		AddBuiltinMarker("__has_builtin");
		AddBuiltinMarker("__has_cpp_attribute");
		AddBuiltinMarker("__has_extension");
		AddBuiltinMarker("__has_feature");
		AddBuiltinMarker("__has_include");
		AddBuiltinMarker("__has_include_next");
		AddBuiltinMarker("__is_identifier");
		AddBuiltinMarker("__building_module");
		if (stats_)
			stats_->peak_retained_replacement_tokens = std::max(
				stats_->peak_retained_replacement_tokens,
				retained_replacement_tokens_);
	}

	void AddBuiltinMarker(const char* name)
	{
		builtin_probe_markers_.Insert(spellings_.Intern(name), 1);
	}
	static bool IsCommandLineMacroName(const std::string& name)
	{
		if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) ||
			name[0] == '_'))
			return false;
		for (std::size_t i = 1; i < name.size(); ++i)
			if (!(std::isalnum(static_cast<unsigned char>(name[i])) ||
				name[i] == '_'))
				return false;
		return true;
	}

	void RemoveMacro(const std::string& name)
	{
		if (!IsCommandLineMacroName(name))
			throw std::runtime_error("invalid command-line macro name");
		const SpellingId id = spellings_.Intern(name);
		Macro* old = macros_.Find(id);
		if (old)
		{
			retained_replacement_tokens_ -= old->replacement.size();
			macros_.Erase(id);
		}
		builtin_probe_markers_.Erase(id);
		replaceable_command_line_macros_.Erase(id);
	}

	void ApplyCommandLineActions()
	{
		for (std::size_t i = 0; i < options_->macro_actions.size(); ++i)
		{
			const PreprocessingOptions::MacroAction& action =
				options_->macro_actions[i];
			if (!action.define)
			{
				RemoveMacro(action.argument);
				continue;
			}
			const std::size_t equal = action.argument.find('=');
			const std::string name = action.argument.substr(0, equal);
			if (!IsCommandLineMacroName(name))
				throw std::runtime_error("invalid command-line macro name");
			RemoveMacro(name);
			const std::string replacement = equal == std::string::npos ?
				"1" : action.argument.substr(equal + 1);
			ProcessSource("<command-line>",
				"#define " + name + " " + replacement + "\n", false, 0);
			replaceable_command_line_macros_.Insert(spellings_.Intern(name), 1);
		}
	}

	static bool GetFileIdentity(const std::string& path, FileIdentity* result)
	{
		struct stat data;
		if (stat(path.c_str(), &data) != 0)
			return false;
		result->device = static_cast<std::uint64_t>(data.st_dev);
		result->inode = static_cast<std::uint64_t>(data.st_ino);
		return true;
	}

	static std::string ReadSource(const std::string& path)
	{
		std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
		if (!input)
			throw std::runtime_error("unable to open source file: " + path);
		input.seekg(0, std::ios::end);
		const std::streamoff size = input.tellg();
		if (size < 0)
			return std::string(std::istreambuf_iterator<char>(input),
				std::istreambuf_iterator<char>());
		std::string result(static_cast<std::size_t>(size), '\0');
		input.seekg(0, std::ios::beg);
		if (size != 0)
			input.read(&result[0], size);
		if (!input)
			throw std::runtime_error("unable to read source file: " + path);
		return result;
	}

	void AccumulateTokenization(const PPTokenizationStats& value)
	{
		if (!stats_)
			return;
		PPTokenizationStats& total = stats_->tokenization;
		total.source_bytes += value.source_bytes;
		total.decoded_code_points += value.decoded_code_points;
		total.translated_code_points += value.translated_code_points;
		total.emitted_tokens += value.emitted_tokens;
		total.emitted_token_bytes += value.emitted_token_bytes;
		total.peak_token_buffer_bytes = std::max(
			total.peak_token_buffer_bytes, value.peak_token_buffer_bytes);
		total.elapsed_nanoseconds += value.elapsed_nanoseconds;
	}

	void ProcessSource(const std::string& path, const std::string& source,
		bool has_include_search_index, std::size_t include_search_index)
	{
		if (sources_.size() >= 256)
			throw std::runtime_error("source inclusion depth exceeded");
		SourceFrame frame;
		frame.physical_file = spellings_.Intern(path);
		frame.presumed_file = frame.physical_file;
		frame.conditional_base = conditionals_.size();
		frame.has_include_search_index = has_include_search_index;
		frame.include_search_index = include_search_index;
		sources_.push_back(frame);
		live_source_bytes_ += source.size();
		if (preprocessing_stats_)
		{
			++preprocessing_stats_->source_files;
			preprocessing_stats_->source_bytes += source.size();
			preprocessing_stats_->peak_live_source_bytes = std::max(
				preprocessing_stats_->peak_live_source_bytes,
				live_source_bytes_);
			preprocessing_stats_->peak_include_depth = std::max(
				preprocessing_stats_->peak_include_depth, sources_.size());
		}
		const bool saved_space = pending_space_;
		pending_space_ = false;
		PPTokenizationStats tokenization;
		TokenizePreprocessingFile(source, *this, stats_ ? &tokenization : 0);
		AccumulateTokenization(tokenization);
		pending_space_ = saved_space;
		if (!line_.empty())
			throw std::logic_error("source line escaped tokenizer EOF");
		sources_.pop_back();
		if (source.size() > live_source_bytes_)
			throw std::logic_error("invalid source storage accounting");
		live_source_bytes_ -= source.size();
	}

	void ApplyLineTransition()
	{
		SourceFrame& source = sources_.back();
		if (!source.line_override)
			return;
		source.physical_base = source.current_physical_line + 1;
		source.presumed_base = source.override_line;
		if (source.override_file != 0)
			source.presumed_file = source.override_file;
		source.line_override = false;
		source.override_file = 0;
	}

	std::size_t PresumedLine(const SourceFrame& source) const
	{
		if (source.current_physical_line < source.physical_base)
			throw std::logic_error("physical source line moved backwards");
		return source.presumed_base +
			(source.current_physical_line - source.physical_base);
	}

	bool IsActive() const
	{
		return conditionals_.empty() || conditionals_.back().active;
	}

	void AddSourceToken(TokenKind kind, const std::string& spelling)
	{
		Token token = kind == TK_IDENTIFIER ?
			Token(kind, spellings_.Intern(spelling), pending_space_) :
			Token(kind, spelling, pending_space_);
		if (full_preprocessing_)
		{
			if (sources_.empty())
				throw std::logic_error("source token outside a source frame");
			const SourceFrame& source = sources_.back();
			token.source_file = source.presumed_file;
			token.physical_file = source.physical_file;
			token.source_line = PresumedLine(source);
			token.source_column = source.current_physical_column;
		}
		line_.push_back(std::move(token));
		line_spelling_bytes_ += line_.back().owned_spelling.size();
		pending_space_ = false;
		if (stats_)
		{
			++stats_->source_tokens;
			stats_->peak_line_tokens = std::max(stats_->peak_line_tokens,
				line_.size());
			stats_->peak_line_storage_bytes = std::max(
				stats_->peak_line_storage_bytes,
				line_.capacity() * sizeof(Token) + line_spelling_bytes_);
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
		result.source_file = source.source_file;
		result.physical_file = source.physical_file;
		result.source_line = source.source_line;
		result.source_column = source.source_column;
		result.matching_distance = source.matching_distance;
		result.tracked_operator = source.tracked_operator;
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
		const TrackedOperatorCode expected = ExpectedOperatorCode(spelling);
		if (expected != TRACKED_OPERATOR_NONE)
			return token.tracked_operator == expected;
		const std::string& actual = Spell(token);
		return actual == spelling;
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
			RequireCompletePragmaOperator();
			boundary_space_ = false;
			std::vector<Token> directive;
			directive.swap(line_);
			if (full_preprocessing_)
				ParsePreprocessingDirective(directive);
			else
				ParseDirective(directive);
			if (stats_)
				++stats_->directive_lines;
			return;
		}
		if (full_preprocessing_ && !IsActive())
			return;

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

	void ParsePreprocessingDirective(const std::vector<Token>& directive)
	{
		if (directive.size() == 1)
			return;
		if (directive[1].kind != TK_IDENTIFIER)
		{
			if (IsActive())
				throw std::runtime_error("invalid preprocessing directive");
			return;
		}
		const SpellingId name = directive[1].spelling;
		if (name == id_if_ || name == id_ifdef_ || name == id_ifndef_)
		{
			ParseIfDirective(directive, name);
			return;
		}
		if (name == id_elif_)
		{
			ParseElifDirective(directive);
			return;
		}
		if (name == id_else_)
		{
			ParseElseDirective(directive);
			return;
		}
		if (name == id_endif_)
		{
			ParseEndifDirective(directive);
			return;
		}
		if (!IsActive())
			return;
		if (name == id_define_)
			ParseDefine(directive);
		else if (name == id_undef_)
			ParseUndef(directive);
		else if (name == id_include_ || name == id_include_next_)
			ParseInclude(directive, name == id_include_next_);
		else if (name == id_line_)
			ParseLineDirective(directive);
		else if (name == id_error_)
			throw std::runtime_error("#error directive");
		else if (name == id_warning_)
			ParseWarningDirective(directive);
		else if (name == id_pragma_)
			ParsePragmaDirective(directive);
		else
			throw std::runtime_error("invalid preprocessing directive");
	}

	bool IsIdentifierLike(const Token& token) const
	{
		if (token.kind == TK_IDENTIFIER)
			return true;
		if (token.kind != TK_OPERATOR)
			return false;
		const std::string& spelling = Spell(token);
		return !spelling.empty() &&
			((spelling[0] >= 'A' && spelling[0] <= 'Z') ||
			 (spelling[0] >= 'a' && spelling[0] <= 'z') || spelling[0] == '_');
	}

	SpellingId IdentifierLikeSpelling(const Token& token)
	{
		return token.kind == TK_IDENTIFIER ? token.spelling :
			spellings_.Intern(Spell(token));
	}

	Token ProbeValueToken(const Token& source, bool value) const
	{
		Token result(TK_PP_NUMBER, value ? "1" : "0", source.leading_space);
		result.source_file = source.source_file;
		result.physical_file = source.physical_file;
		result.source_line = source.source_line;
		result.source_column = source.source_column;
		return result;
	}

	bool ParseHeaderOperand(const std::vector<Token>& tokens,
		std::size_t begin, std::size_t end, std::string* name,
		bool* quoted) const
	{
		if (begin + 1 == end && tokens[begin].kind == TK_HEADER)
		{
			const std::string& spelling = Spell(tokens[begin]);
			if (spelling.size() < 3)
				return false;
			*quoted = spelling[0] == '"';
			*name = spelling.substr(1, spelling.size() - 2);
			return true;
		}
		if (begin + 1 == end && tokens[begin].kind == TK_STRING)
		{
			*quoted = true;
			*name = DecodeDirectiveString(Spell(tokens[begin]));
			return name->find('\0') == std::string::npos;
		}
		if (begin >= end || !IsOperator(tokens[begin], "<") ||
			!IsOperator(tokens[end - 1], ">"))
			return false;
		*quoted = false;
		name->clear();
		for (std::size_t i = begin + 1; i + 1 < end; ++i)
			*name += Spell(tokens[i]);
		return !name->empty() && name->find('\0') == std::string::npos;
	}

	bool EvaluateBuiltinProbe(const Token& probe,
		const std::vector<Token>& directive, std::size_t begin,
		std::size_t end)
	{
		const std::string& name = Spell(probe);
		if (name == "__has_include" || name == "__has_include_next")
		{
			std::string header;
			bool quoted = false;
			if (!ParseHeaderOperand(directive, begin, end, &header, &quoted))
				throw std::runtime_error("invalid include probe operand");
			if (preprocessing_stats_) ++preprocessing_stats_->include_probes;
			return FindInclude(header, quoted, name == "__has_include_next", 0);
		}
		if (begin + 1 != end || !IsIdentifierLike(directive[begin]))
			throw std::runtime_error("invalid builtin probe operand");
		const std::string& operand = Spell(directive[begin]);
		if (preprocessing_stats_) ++preprocessing_stats_->builtin_probes;
		if (name == "__has_builtin")
			return IsSupportedBuiltinProbe(operand);
		if (name == "__has_feature" || name == "__has_extension")
		{
			if (operand == "cxx_exceptions")
				return IsMacroDefined(spellings_.Intern("__EXCEPTIONS"));
			if (operand == "cxx_rtti")
				return IsMacroDefined(spellings_.Intern("__GXX_RTTI"));
			return IsSupportedFeatureProbe(operand);
		}
		if (name == "__has_attribute")
			return IsSupportedAttributeProbe(operand);
		if (name == "__is_identifier")
			return !IsCompilerIdentifier(operand);
		if (name == "__has_cpp_attribute" || name == "__building_module")
			return false;
		throw std::logic_error("unknown builtin probe");
	}

	bool IsBuiltinProbe(const Token& token) const
	{
		if (token.kind != TK_IDENTIFIER ||
			!builtin_probe_markers_.Find(token.spelling))
			return false;
		const std::string& name = Spell(token);
		return name == "__has_attribute" || name == "__has_builtin" ||
			name == "__has_cpp_attribute" || name == "__has_extension" ||
			name == "__has_feature" || name == "__has_include" ||
			name == "__has_include_next" || name == "__is_identifier" ||
			name == "__building_module";
	}

	void RewriteConditionOperators(const std::vector<Token>& directive,
		std::size_t begin, std::vector<Token>* expression)
	{
		for (std::size_t i = begin; i < directive.size(); ++i)
		{
			const Token& token = directive[i];
			if (IsBuiltinProbe(token))
			{
				const std::size_t open = i + 1;
				if (open >= directive.size() ||
					!IsOperator(directive[open], "(") ||
					directive[open].matching_distance == 0)
					throw std::runtime_error("invalid builtin probe invocation");
				const std::size_t close = open +
					directive[open].matching_distance;
				if (close >= directive.size() ||
					!IsOperator(directive[close], ")"))
					throw std::runtime_error("invalid builtin probe invocation");
				expression->push_back(ProbeValueToken(token,
					EvaluateBuiltinProbe(token, directive, open + 1, close)));
				i = close;
				continue;
			}
			if (token.kind != TK_IDENTIFIER || token.spelling != id_defined_)
			{
				expression->push_back(token);
				continue;
			}
			std::size_t operand = i + 1;
			bool parenthesized = false;
			if (operand < directive.size() &&
				IsOperator(directive[operand], "("))
			{
				parenthesized = true;
				++operand;
			}
			if (operand >= directive.size() ||
				!IsIdentifierLike(directive[operand]))
				throw std::runtime_error("invalid defined operator");
			const SpellingId queried = IdentifierLikeSpelling(directive[operand]);
			std::size_t next = operand + 1;
			if (parenthesized)
			{
				if (next >= directive.size() ||
					!IsOperator(directive[next], ")"))
					throw std::runtime_error("invalid defined operator");
				++next;
			}
			Token value(TK_PP_NUMBER, IsMacroDefined(queried) ? "1" : "0",
				token.leading_space);
			value.source_file = token.source_file;
			value.physical_file = token.physical_file;
			value.source_line = token.source_line;
			value.source_column = token.source_column;
			expression->push_back(std::move(value));
			i = next - 1;
		}
	}

	void ExpandTokens(std::vector<Token>* tokens,
		std::vector<Token>* expanded)
	{
		AnnotateParentheses(tokens);
		std::deque<Token> input;
		for (std::size_t i = 0; i < tokens->size(); ++i)
			input.push_back(std::move((*tokens)[i]));
		capture_ = expanded;
		Drain(input, true, 0);
		capture_ = 0;
	}

	void ExpandDirectiveTail(const std::vector<Token>& directive,
		std::size_t begin, std::vector<Token>* expanded)
	{
		std::vector<Token> input;
		input.reserve(directive.size() - begin);
		for (std::size_t i = begin; i < directive.size(); ++i)
			input.push_back(directive[i]);
		ExpandTokens(&input, expanded);
	}

	bool EvaluateCondition(const std::vector<Token>& directive,
		std::size_t begin)
	{
		const std::chrono::steady_clock::time_point start =
			preprocessing_stats_ ? std::chrono::steady_clock::now() :
			std::chrono::steady_clock::time_point();
		std::vector<Token> input;
		RewriteConditionOperators(directive, begin, &input);
		std::vector<Token> expanded;
		ExpandTokens(&input, &expanded);
		AnnotateParentheses(&expanded);
		std::vector<Token> rewritten;
		RewriteConditionOperators(expanded, 0, &rewritten);
		for (std::size_t i = 0; i < rewritten.size(); ++i)
			FeedPreprocessingToken(rewritten[i], condition_post_tokens_);
		condition_post_tokens_.FlushPendingTokens();
		bool value = false;
		const bool valid = condition_evaluator_.Finish(&value);
		if (preprocessing_stats_)
		{
			++preprocessing_stats_->controlling_expressions;
			preprocessing_stats_->condition_evaluation.elapsed_nanoseconds +=
				static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - start).count());
		}
		if (!valid)
			throw std::runtime_error("invalid controlling expression");
		return value;
	}

	void ParseIfDirective(const std::vector<Token>& directive,
		SpellingId directive_name)
	{
		const bool parent = IsActive();
		bool condition = false;
		if (parent)
		{
			if (directive_name == id_if_)
				condition = EvaluateCondition(directive, 2);
			else
			{
				if (directive.size() != 3 ||
					directive[2].kind != TK_IDENTIFIER)
					throw std::runtime_error("invalid conditional directive");
				condition = IsMacroDefined(directive[2].spelling);
				if (directive_name == id_ifndef_)
					condition = !condition;
			}
		}
		conditionals_.push_back(ConditionalFrame(parent,
			parent && condition, parent && condition));
		if (preprocessing_stats_)
		{
			++preprocessing_stats_->conditional_directives;
			preprocessing_stats_->peak_conditional_depth = std::max(
				preprocessing_stats_->peak_conditional_depth,
				conditionals_.size());
		}
	}

	ConditionalFrame& CurrentConditional()
	{
		if (sources_.empty() || conditionals_.size() <=
			sources_.back().conditional_base)
			throw std::runtime_error("unmatched conditional directive");
		return conditionals_.back();
	}

	void ParseElifDirective(const std::vector<Token>& directive)
	{
		ConditionalFrame& frame = CurrentConditional();
		if (frame.saw_else)
			throw std::runtime_error("#elif after #else");
		bool condition = false;
		if (frame.parent_active && !frame.branch_taken)
			condition = EvaluateCondition(directive, 2);
		frame.active = frame.parent_active && !frame.branch_taken && condition;
		frame.branch_taken = frame.branch_taken || frame.active;
		if (preprocessing_stats_)
			++preprocessing_stats_->conditional_directives;
	}

	void ParseElseDirective(const std::vector<Token>& directive)
	{
		ConditionalFrame& frame = CurrentConditional();
		if (frame.saw_else)
			throw std::runtime_error("duplicate #else");
		if (frame.parent_active && directive.size() != 2)
			throw std::runtime_error("tokens after #else");
		frame.saw_else = true;
		frame.active = frame.parent_active && !frame.branch_taken;
		frame.branch_taken = frame.branch_taken || frame.active;
		if (preprocessing_stats_)
			++preprocessing_stats_->conditional_directives;
	}

	void ParseEndifDirective(const std::vector<Token>& directive)
	{
		ConditionalFrame& frame = CurrentConditional();
		if (frame.parent_active && directive.size() != 2)
			throw std::runtime_error("tokens after #endif");
		conditionals_.pop_back();
		if (preprocessing_stats_)
			++preprocessing_stats_->conditional_directives;
	}

	static std::string DecodeDirectiveString(const std::string& spelling)
	{
		std::string result;
		if (!DecodeOrdinaryStringLiteral(spelling, &result))
			throw std::runtime_error("expected ordinary string literal");
		return result;
	}

	struct ResolvedInclude
	{
		std::string path;
		FileIdentity identity;
		std::size_t search_index;
		bool has_search_index;

		ResolvedInclude() : search_index(0), has_search_index(false) {}
	};

	std::size_t IncludeSearchPathCount() const
	{
		return options_->include_search_paths.size() +
			options_->system_include_search_paths.size();
	}

	const std::string& IncludeSearchPath(std::size_t index) const
	{
		if (index < options_->include_search_paths.size())
			return options_->include_search_paths[index];
		return options_->system_include_search_paths[
			index - options_->include_search_paths.size()];
	}

	bool ResolveCandidate(const std::string& path, std::size_t search_index,
		bool has_search_index, ResolvedInclude* result) const
	{
		FileIdentity identity;
		if (!GetFileIdentity(path, &identity))
			return false;
		if (result)
		{
			result->path = path;
			result->identity = identity;
			result->search_index = search_index;
			result->has_search_index = has_search_index;
		}
		return true;
	}

	bool FindInclude(const std::string& name, bool quoted, bool include_next,
		ResolvedInclude* result) const
	{
		if (sources_.empty())
			throw std::logic_error("include lookup outside source");
		if (!include_next && quoted)
		{
			const std::string current =
				spellings_.Get(sources_.back().physical_file);
			const std::size_t slash = current.find_last_of('/');
			if (slash != std::string::npos && ResolveCandidate(
				current.substr(0, slash + 1) + name, 0, false, result))
				return true;
		}

		std::size_t first = 0;
		if (include_next && sources_.back().has_include_search_index)
			first = sources_.back().include_search_index + 1;
		for (std::size_t i = first; i < IncludeSearchPathCount(); ++i)
		{
			std::string candidate = IncludeSearchPath(i);
			if (!candidate.empty() && candidate[candidate.size() - 1] != '/')
				candidate.push_back('/');
			candidate += name;
			if (ResolveCandidate(candidate, i, true, result))
				return true;
		}
		return !include_next && ResolveCandidate(name, 0, false, result);
	}

	void ProcessResolvedInclude(const ResolvedInclude& resolved)
	{
		if (once_files_.Find(resolved.identity))
		{
			if (preprocessing_stats_)
				++preprocessing_stats_->skipped_once_includes;
			return;
		}
		if (preprocessing_stats_) ++preprocessing_stats_->includes;
		const std::string source = ReadSource(resolved.path);
		ProcessSource(resolved.path, source, resolved.has_search_index,
			resolved.search_index);
	}

	void ProcessForcedInclude(const std::string& name)
	{
		// Give command-line includes a primary-like source frame for relative
		// lookup while preserving the same macro and output stream session.
		SourceFrame frame;
		frame.physical_file = spellings_.Intern("<command-line>");
		frame.presumed_file = frame.physical_file;
		sources_.push_back(frame);
		ResolvedInclude resolved;
		const bool found = FindInclude(name, true, false, &resolved);
		sources_.pop_back();
		if (!found)
			throw std::runtime_error("included file not found: " + name);
		ProcessResolvedInclude(resolved);
	}

	void ParseInclude(const std::vector<Token>& directive, bool include_next)
	{
		std::vector<Token> expanded;
		ExpandDirectiveTail(directive, 2, &expanded);
		std::string name;
		bool quoted = false;
		if (!ParseHeaderOperand(expanded, 0, expanded.size(), &name, &quoted))
			throw std::runtime_error("invalid #include operand");
		ResolvedInclude resolved;
		if (!FindInclude(name, quoted, include_next, &resolved))
			throw std::runtime_error("included file not found: " + name);
		ProcessResolvedInclude(resolved);
	}

	void ParseWarningDirective(const std::vector<Token>& directive)
	{
		if (!options_->diagnostics)
			return;
		const SourceFrame& source = sources_.back();
		*options_->diagnostics << spellings_.Get(source.presumed_file) << ':'
			<< (directive.empty() ? source.current_physical_line :
				directive[0].source_line)
			<< ": warning: #warning";
		for (std::size_t i = 2; i < directive.size(); ++i)
		{
			if (directive[i].leading_space) *options_->diagnostics << ' ';
			*options_->diagnostics << Spell(directive[i]);
		}
		*options_->diagnostics << '\n';
	}

	void ParseLineDirective(const std::vector<Token>& directive)
	{
		std::vector<Token> expanded;
		ExpandDirectiveTail(directive, 2, &expanded);
		if (expanded.empty() || expanded.size() > 2 ||
			expanded[0].kind != TK_PP_NUMBER ||
			(expanded.size() == 2 && expanded[1].kind != TK_STRING))
			throw std::runtime_error("invalid #line directive");
		const std::string& spelling = Spell(expanded[0]);
		char* end = 0;
		const unsigned long long parsed = std::strtoull(spelling.c_str(),
			&end, 10);
		if (!end || *end != '\0' || parsed == 0 ||
			parsed > std::numeric_limits<std::size_t>::max())
			throw std::runtime_error("invalid #line number");
		SourceFrame& source = sources_.back();
		source.line_override = true;
		source.override_line = static_cast<std::size_t>(parsed);
		if (expanded.size() == 2)
			source.override_file = spellings_.Intern(
				DecodeDirectiveString(Spell(expanded[1])));
	}

	void ParsePragmaDirective(const std::vector<Token>& directive)
	{
		if (directive.size() == 3 && directive[2].kind == TK_IDENTIFIER &&
			directive[2].spelling == id_once_)
		{
			MarkPragmaOnce(spellings_.Get(sources_.back().physical_file));
			return;
		}
		if (directive.size() < 6 || Spell(directive[2]) != "pack" ||
			Spell(directive[3]) != "(" || Spell(directive.back()) != ")")
			return;
		if (directive.size() == 6 && Spell(directive[4]) == "pop")
		{
			post_tokens_.EmitPragmaPackPop();
			return;
		}
		if (directive.size() == 8 && Spell(directive[4]) == "push" &&
			Spell(directive[5]) == "," && directive[6].kind == TK_PP_NUMBER)
		{
			const std::string& text = Spell(directive[6]);
			char* end = 0;
			const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
			if (!end || *end != '\0' || parsed == 0 ||
				parsed > std::numeric_limits<std::size_t>::max() ||
				(parsed & (parsed - 1)) != 0)
				throw std::runtime_error("invalid #pragma pack alignment");
			post_tokens_.EmitPragmaPackPush(static_cast<std::size_t>(parsed));
		}
	}

	void ParseDirective(const std::vector<Token>& directive)
	{
		if (directive.size() < 2 || directive[1].kind != TK_IDENTIFIER)
			throw std::runtime_error("invalid preprocessing directive");
		if (directive[1].spelling == id_define_)
			ParseDefine(directive);
		else if (directive[1].spelling == id_undef_)
			ParseUndef(directive);
		else
			throw std::runtime_error("unsupported preprocessing directive");
	}

	void ParseUndef(const std::vector<Token>& directive)
	{
		if (directive.size() != 3 || directive[2].kind != TK_IDENTIFIER ||
			directive[2].spelling == id_va_args_)
			throw std::runtime_error("invalid #undef directive");
		Macro* found = macros_.Find(directive[2].spelling);
		if (found)
		{
			retained_replacement_tokens_ -= found->replacement.size();
			macros_.Erase(directive[2].spelling);
		}
		builtin_probe_markers_.Erase(directive[2].spelling);
		replaceable_command_line_macros_.Erase(directive[2].spelling);
		if (stats_)
			++stats_->macro_undefinitions;
	}

	void ParseDefine(const std::vector<Token>& directive)
	{
		if (directive.size() < 3 || directive[2].kind != TK_IDENTIFIER ||
			directive[2].spelling == id_va_args_)
			throw std::runtime_error("missing or invalid macro name");
		Macro macro;
		macro.name = directive[2].spelling;
		builtin_probe_markers_.Erase(macro.name);
		std::size_t replacement_begin = 3;
		if (directive.size() > 3 && IsOperator(directive[3], "(") &&
			!directive[3].leading_space)
		{
			macro.function_like = true;
			replacement_begin = ParseParameters(directive, 4, &macro);
		}
		else if (directive.size() > 3 && !directive[3].leading_space)
			throw std::runtime_error(
				"object-like macro replacement requires whitespace");

		BuildReplacement(directive, replacement_begin, &macro);
		Macro* old = macros_.Find(macro.name);
		if (old)
		{
			if (!Equivalent(*old, macro))
			{
				if (!replaceable_command_line_macros_.Find(macro.name))
					throw std::runtime_error("incompatible macro redefinition");
				retained_replacement_tokens_ -= old->replacement.size();
				macros_.Erase(macro.name);
				retained_replacement_tokens_ += macro.replacement.size();
				const SpellingId name = macro.name;
				macros_.Insert(name, std::move(macro));
			}
			replaceable_command_line_macros_.Erase(macro.name);
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

	std::size_t ParseParameters(const std::vector<Token>& directive,
		std::size_t position, Macro* macro)
	{
		FlatHashMap<SpellingId, unsigned char> seen;
		if (position >= directive.size())
			throw std::runtime_error("unterminated macro parameter list");
		if (IsOperator(directive[position], ")"))
			return position + 1;
		if (IsOperator(directive[position], "..."))
			return ParseVariadicClose(directive, position, macro);

		while (true)
		{
			if (position >= directive.size() ||
				directive[position].kind != TK_IDENTIFIER ||
				directive[position].spelling == id_va_args_ ||
				seen.Find(directive[position].spelling))
				throw std::runtime_error("invalid macro parameter");
			seen.Insert(directive[position].spelling, 1);
			macro->parameters.push_back(directive[position].spelling);
			++position;
			if (position >= directive.size())
				throw std::runtime_error("unterminated macro parameter list");
			// GNU `name...` denotes the final variadic slot; hosted Linux headers
			// use this spelling and expansion still uses the contiguous tail.
			if (IsOperator(directive[position], "..."))
			{
				macro->variadic = true;
				macro->named_variadic_parameter = macro->parameters.back();
				macro->parameters.pop_back();
				++position;
				if (position >= directive.size() || !IsOperator(
					directive[position], ")"))
					throw std::runtime_error("invalid variadic parameter list");
				return position + 1;
			}
			if (IsOperator(directive[position], ")"))
				return position + 1;
			if (!IsOperator(directive[position], ","))
				throw std::runtime_error("invalid macro parameter separator");
			++position;
			if (position >= directive.size())
				throw std::runtime_error("unterminated macro parameter list");
			if (IsOperator(directive[position], "..."))
				return ParseVariadicClose(directive, position, macro);
		}
	}

	std::size_t ParseVariadicClose(const std::vector<Token>& directive,
		std::size_t position, Macro* macro)
	{
		macro->variadic = true;
		++position;
		if (position >= directive.size() ||
			!IsOperator(directive[position], ")"))
			throw std::runtime_error("invalid variadic parameter list");
		return position + 1;
	}

	void BuildReplacement(const std::vector<Token>& directive,
		std::size_t begin, Macro* macro)
	{
		FlatHashMap<SpellingId, std::size_t> parameter_index;
		for (std::size_t i = 0; i < macro->parameters.size(); ++i)
			parameter_index.Insert(macro->parameters[i], i);
		if (macro->variadic)
		{
			parameter_index.Insert(id_va_args_, macro->parameters.size());
			if (macro->named_variadic_parameter != 0)
				parameter_index.Insert(macro->named_variadic_parameter,
					macro->parameters.size());
		}

		for (std::size_t i = begin; i < directive.size(); ++i)
		{
			ReplacementToken replacement;
			replacement.token = directive[i];
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
			first.named_variadic_parameter != second.named_variadic_parameter ||
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
			variadic.raw_begin = first == parsed->arguments.size() ? pending->raw_tokens.size() : parsed->arguments[first].begin;
			if (first != parsed->arguments.size()) {
				const ParsedArgument& last = parsed->arguments.back();
				variadic.raw_size = last.begin + last.size - variadic.raw_begin;
			}
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
		PaintId paint, PaintId blocked, std::uint64_t origin,
		SpellingId source_file, SpellingId physical_file,
		std::size_t source_line, std::size_t source_column)
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
		result.source_file = source_file;
		result.physical_file = physical_file;
		result.source_line = source_line;
		result.source_column = source_column;
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
					head.blocked, origin, head.source_file, head.physical_file,
					head.source_line, head.source_column), &paste_pending, result);
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
			literal.source_file = head.source_file;
			literal.physical_file = head.physical_file;
			literal.source_line = head.source_line;
			literal.source_column = head.source_column;
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
			left.leading_space, left.source_file, left.physical_file,
			left.source_line, left.source_column);
		TokenizeGeneratedPreprocessingToken(spelling, collector);
		if (stats_)
		{
			++stats_->pasted_tokens;
			stats_->pasted_spelling_bytes += spelling.size();
		}
		Token result;
		if (!collector.TakeResult(&result))
			throw SourceError("invalid token paste: " + spelling,
				CompilerErrorDomain::PREPROCESSING);
		return result;
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

	bool IsDynamicMacro(SpellingId name) const
	{
		return full_preprocessing_ && (name == id_file_macro_ ||
			name == id_line_macro_ || name == id_counter_macro_);
	}

	bool IsMacroDefined(SpellingId name) const
	{
		return IsDynamicMacro(name) || macros_.Find(name) != 0 ||
			builtin_probe_markers_.Find(name) != 0;
	}

	Token ExpandDynamicMacro(const Token& head)
	{
		Token result;
		if (head.spelling == id_file_macro_)
			result = Token(TK_STRING,
				QuoteString(spellings_.Get(head.source_file)), head.leading_space);
		else
		{
			const std::size_t value = head.spelling == id_line_macro_ ?
				head.source_line : counter_++;
			std::ostringstream spelling;
			spelling << value;
			result = Token(TK_PP_NUMBER, spelling.str(), head.leading_space);
		}
		result.paint = head.paint;
		result.blocked = head.blocked;
		result.origin = head.origin;
		result.source_file = head.source_file;
		result.physical_file = head.physical_file;
		result.source_line = head.source_line;
		result.source_column = head.source_column;
		return result;
	}

	bool TryExpandDynamicMacro(ExpansionFrame* frame)
	{
		const Token& head = frame->input.front();
		if (!IsDynamicMacro(head.spelling) ||
			paints_.Contains(head.paint, head.spelling) ||
			paints_.Contains(head.blocked, head.spelling))
			return false;
		Token expanded = ExpandDynamicMacro(head);
		frame->input.pop_front();
		frame->input.push_front(std::move(expanded));
		if (stats_)
		{
			++stats_->macro_invocations;
			++stats_->expanded_tokens;
		}
		return true;
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
				if (stats_) ++stats_->argument_prescans;
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
				if (TryExpandDynamicMacro(&frame))
					continue;
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
		if (capture_)
		{
			Token retained = token;
			if (retained.spelling == 0)
			{
				retained.owned_spelling = Spell(token);
				retained.borrowed_spelling = 0;
			}
			capture_->push_back(std::move(retained));
			return;
		}
		if (full_preprocessing_)
		{
			if (!pragma_operator_.empty())
			{
				pragma_operator_.push_back(token);
				const std::size_t count = pragma_operator_.size();
				if ((count == 2 && !IsOperator(token, "(")) ||
					(count == 3 && token.kind != TK_STRING) ||
					(count == 4 && !IsOperator(token, ")")) || count > 4)
					throw std::runtime_error("invalid _Pragma operator");
				if (count == 4)
				{
					ExecutePragmaLiteral(pragma_operator_[2],
						pragma_operator_[0].physical_file);
					pragma_operator_.clear();
				}
				return;
			}
			if (token.kind == TK_IDENTIFIER &&
				token.spelling == id_pragma_operator_)
			{
				pragma_operator_.push_back(token);
				return;
			}
		}
		if (token.source_file != 0)
			post_tokens_.SetSourceLocation(
				spellings_.Get(token.source_file), token.source_line,
				token.source_column);
		FeedPreprocessingToken(token, post_tokens_);
	}

	void FeedPreprocessingToken(const Token& token,
		PostTokenizationSession& output)
	{
		const std::string& spelling = Spell(token);
		switch (token.kind)
		{
		case TK_IDENTIFIER:
			output.emit_identifier_id(spelling, token.spelling); break;
		case TK_PP_NUMBER: output.emit_pp_number(spelling); break;
		case TK_CHARACTER:
			output.emit_character_literal(spelling); break;
		case TK_USER_CHARACTER:
			output.emit_user_defined_character_literal(spelling); break;
		case TK_STRING: output.emit_string_literal(spelling); break;
		case TK_USER_STRING:
			output.emit_user_defined_string_literal(spelling); break;
		case TK_OPERATOR:
			output.emit_preprocessing_op_or_punc_id(
				spelling, token.spelling); break;
		case TK_NON_WHITESPACE:
			output.emit_non_whitespace_char(spelling); break;
		case TK_HEADER: output.emit_header_name(spelling); break;
		case TK_PLACEMARKER:
			throw std::logic_error("placemarker escaped paste resolution");
		}
	}

	static std::string DestringizePragma(const std::string& spelling)
	{
		std::size_t quote = spelling.find('"');
		if (quote == std::string::npos || spelling.size() <= quote + 1 ||
			spelling[spelling.size() - 1] != '"')
			throw std::runtime_error("invalid _Pragma string literal");
		std::string result;
		for (std::size_t i = quote + 1; i + 1 < spelling.size(); ++i)
		{
			if (spelling[i] == '\\' && i + 2 < spelling.size() &&
				(spelling[i + 1] == '\\' || spelling[i + 1] == '"'))
				++i;
			result.push_back(spelling[i]);
		}
		return result;
	}

	void ExecutePragmaLiteral(const Token& literal, SpellingId physical_file)
	{
		const std::string pragma = DestringizePragma(Spell(literal));
		if (preprocessing_stats_)
			++preprocessing_stats_->pragma_operators;
		std::size_t begin = pragma.find_first_not_of(" \t\v\f\r\n");
		std::size_t end = pragma.find_last_not_of(" \t\v\f\r\n");
		if (begin != std::string::npos &&
			pragma.substr(begin, end - begin + 1) == "once")
			MarkPragmaOnce(spellings_.Get(physical_file));
	}

	void MarkPragmaOnce(const std::string& path)
	{
		FileIdentity identity;
		if (!GetFileIdentity(path, &identity))
			throw std::runtime_error("unable to identify pragma-once file");
		if (!once_files_.Find(identity))
		{
			once_files_.Insert(identity, 1);
			if (preprocessing_stats_)
				++preprocessing_stats_->pragma_once_files;
		}
	}

	void RequireCompletePragmaOperator() const
	{
		if (!pragma_operator_.empty())
			throw std::runtime_error("incomplete _Pragma operator");
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
	ControllingExpressionEvaluator condition_evaluator_;
	PostTokenizationSession condition_post_tokens_;
	FlatHashMap<SpellingId, Macro> macros_;
	FlatHashMap<SpellingId, unsigned char> builtin_probe_markers_;
	FlatHashMap<SpellingId, unsigned char> replaceable_command_line_macros_;
	std::vector<Token> line_;
	std::deque<Token> rescan_;
	InvocationScan pending_invocation_;
	bool pending_space_;
	bool boundary_space_;
	std::size_t line_spelling_bytes_;
	std::size_t retained_replacement_tokens_;
	std::uint64_t next_origin_;
	bool full_preprocessing_;
	PreprocessingStats* preprocessing_stats_;
	const PreprocessingOptions* options_;
	std::vector<SourceFrame> sources_;
	std::vector<ConditionalFrame> conditionals_;
	FlatHashMap<FileIdentity, unsigned char, FileIdentityHash> once_files_;
	std::size_t live_source_bytes_;
	std::vector<Token>* capture_;
	std::vector<Token> pragma_operator_;
	std::size_t counter_;
	SpellingId id_define_;
	SpellingId id_undef_;
	SpellingId id_if_;
	SpellingId id_ifdef_;
	SpellingId id_ifndef_;
	SpellingId id_elif_;
	SpellingId id_else_;
	SpellingId id_endif_;
	SpellingId id_include_;
	SpellingId id_include_next_;
	SpellingId id_line_;
	SpellingId id_error_;
	SpellingId id_warning_;
	SpellingId id_pragma_;
	SpellingId id_once_;
	SpellingId id_defined_;
	SpellingId id_va_args_;
	SpellingId id_file_macro_;
	SpellingId id_line_macro_;
	SpellingId id_counter_macro_;
	SpellingId id_pragma_operator_;
};

}

MacroProcessingStats::MacroProcessingStats()
	: logical_lines(0), directive_lines(0), source_tokens(0),
	  interned_identifiers(0), interned_identifier_bytes(0),
	  macro_definitions(0), macro_undefinitions(0), macro_lookups(0),
	  macro_invocations(0), argument_prescans(0), expanded_tokens(0),
	  pasted_tokens(0), pasted_spelling_bytes(0), peak_line_tokens(0),
	  peak_line_storage_bytes(0), peak_rescan_tokens(0),
	  peak_retained_replacement_tokens(0),
	  peak_expansion_frames(0), peak_argument_storage_bytes(0),
	  paint_roots(0), paint_singletons(0), paint_nodes(0),
	  elapsed_nanoseconds(0)
{}

PreprocessingOptions::PreprocessingOptions()
	: hosted_predefined_source(0), diagnostics(0)
{}

PreprocessingStats::PreprocessingStats()
	: source_files(0), source_bytes(0), peak_live_source_bytes(0),
	  conditional_directives(0),
	  controlling_expressions(0), includes(0), skipped_once_includes(0),
	  pragma_once_files(0), pragma_operators(0), builtin_probes(0),
	  include_probes(0), peak_include_depth(0),
	  peak_conditional_depth(0), elapsed_nanoseconds(0)
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

void PreprocessFile(const std::string& path, const std::string& source,
	IPostTokenStream& output, const PreprocessingOptions& options,
	PreprocessingStats* stats)
{
	const std::chrono::steady_clock::time_point start = stats ?
		std::chrono::steady_clock::now() :
		std::chrono::steady_clock::time_point();
	if (stats)
		*stats = PreprocessingStats();
	MacroProcessor processor(output, options, stats);
	processor.ProcessPrimary(path, source);
	if (stats)
	{
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - start).count());
		stats->macros.elapsed_nanoseconds = stats->elapsed_nanoseconds;
	}
}

}
