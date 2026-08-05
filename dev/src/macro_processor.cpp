#include "macro_processor.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
	PaintId paint;
	PaintId blocked;
	std::uint64_t origin;
	std::size_t matching_distance;
	std::size_t matching_commas;
	bool leading_space;
	bool from_variadic;
	bool parameter_origin;

	Token()
		: kind(TK_PLACEMARKER), spelling(0), paint(0), blocked(0), origin(0),
		  matching_distance(0), matching_commas(0),
		  leading_space(false), from_variadic(false), parameter_origin(false)
	{}

	Token(TokenKind token_kind, SpellingId token_spelling,
		bool has_leading_space)
		: kind(token_kind), spelling(token_spelling), paint(0), blocked(0),
		  origin(0), matching_distance(0), matching_commas(0),
		  leading_space(has_leading_space), from_variadic(false),
		  parameter_origin(false)
	{}
};

class SpellingTable
{
public:
	SpellingTable()
	{
		spellings_.push_back(std::string());
		ids_.insert(std::make_pair(std::string(), 0));
	}

	SpellingId Intern(const std::string& spelling)
	{
		std::unordered_map<std::string, SpellingId>::const_iterator found =
			ids_.find(spelling);
		if (found != ids_.end())
			return found->second;
		if (spellings_.size() >=
			static_cast<std::size_t>(std::numeric_limits<SpellingId>::max()))
			throw std::runtime_error("too many distinct preprocessing spellings");
		const SpellingId id = static_cast<SpellingId>(spellings_.size());
		spellings_.push_back(spelling);
		ids_.insert(std::make_pair(spelling, id));
		return id;
	}

	const std::string& Get(SpellingId id) const
	{
		if (id >= spellings_.size())
			throw std::logic_error("invalid interned spelling ID");
		return spellings_[id];
	}

private:
	std::unordered_map<std::string, SpellingId> ids_;
	std::vector<std::string> spellings_;
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
		if (stats_)
			roots_.insert(0);
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
		std::unordered_map<std::uint64_t, PaintId>::const_iterator cached =
			add_cache_.find(key);
		if (cached != add_cache_.end())
			return cached->second;
		Validate(paint);
		const PaintId id = AddAt(paint, macro_name, 31);
		add_cache_[key] = id;
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
		std::unordered_map<std::uint64_t, PaintId>::const_iterator cached =
			merge_cache_.find(key);
		if (cached != merge_cache_.end())
			return cached->second;
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
		merge_cache_[key] = result;
		return result;
	}

	void RegisterRoot(PaintId root)
	{
		if (!stats_)
			return;
		if (roots_.insert(root).second)
			UpdateStats();
	}

	void UpdateStats()
	{
		if (!stats_)
			return;
		stats_->paint_roots = roots_.size();
		stats_->paint_nodes = nodes_.size();
	}

	MacroProcessingStats* stats_;
	std::vector<Node> nodes_;
	std::unordered_set<PaintId> roots_;
	std::unordered_map<std::uint64_t, PaintId> add_cache_;
	std::unordered_map<std::uint64_t, PaintId> merge_cache_;
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
	std::deque<Token> raw;
	std::deque<Token> expanded;
	bool expanded_ready;
	bool preserve_raw;

	Argument() : expanded_ready(false), preserve_raw(false) {}
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
	std::vector<Argument> arguments;
	std::vector<std::size_t> demanded_arguments;
	std::size_t next_demand;
	std::size_t active_argument;

	PendingExpansion()
		: active(false), macro_name(0), next_demand(0), active_argument(0)
	{}
};

struct ExpansionFrame
{
	std::deque<Token> input;
	std::deque<Token> output;
	InvocationScan scan;
	PendingExpansion pending;
	bool root;
	bool final;

	ExpansionFrame(bool is_root, bool is_final)
		: root(is_root), final(is_final)
	{}
};

struct Piece
{
	Token token;
	bool paste_operator;

	Piece(const Token& value, bool is_paste)
		: token(value), paste_operator(is_paste)
	{}
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

	Token Result() const
	{
		if (count_ != 1)
			throw std::runtime_error(
				"token paste did not produce one preprocessing token");
		return result_;
	}

private:
	void Add(TokenKind kind, const std::string& spelling)
	{
		++count_;
		if (count_ != 1)
			return;
		result_ = Token(kind, spellings_.Intern(spelling), leading_space_);
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
		: stats_(stats), paints_(stats), post_tokens_(output,
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
		Drain(rescan_, 0, true, &pending_invocation_);
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
		line_.push_back(Token(kind, spellings_.Intern(spelling), pending_space_));
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
		return spellings_.Get(token.spelling);
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
			(*tokens)[i].matching_commas = 0;
			if (IsOperator((*tokens)[i], "("))
				openings.push_back(i);
			else if (IsOperator((*tokens)[i], ",") && !openings.empty())
				++(*tokens)[openings.back()].matching_commas;
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
			Drain(rescan_, 0, true, &pending_invocation_);
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
			rescan_.push_back(line_[i]);
		UpdatePeakRescan(rescan_.size());
		Drain(rescan_, 0, false, &pending_invocation_);
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
		std::unordered_map<SpellingId, Macro>::iterator found =
			macros_.find(line_[2].spelling);
		if (found != macros_.end())
		{
			retained_replacement_tokens_ -= found->second.replacement.size();
			macros_.erase(found);
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
		std::unordered_map<SpellingId, Macro>::iterator old =
			macros_.find(macro.name);
		if (old != macros_.end())
		{
			if (!Equivalent(old->second, macro))
				throw std::runtime_error("incompatible macro redefinition");
		}
		else
		{
			retained_replacement_tokens_ += macro.replacement.size();
			macros_.insert(std::make_pair(macro.name, macro));
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
		std::unordered_set<SpellingId> seen;
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
				!seen.insert(line_[position].spelling).second)
				throw std::runtime_error("invalid macro parameter");
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
		std::unordered_map<SpellingId, std::size_t> parameter_index;
		for (std::size_t i = 0; i < macro->parameters.size(); ++i)
			parameter_index[macro->parameters[i]] = i;
		if (macro->variadic)
			parameter_index[id_va_args_] = macro->parameters.size();

		for (std::size_t i = begin; i < line_.size(); ++i)
		{
			ReplacementToken replacement;
			replacement.token = line_[i];
			if (i == begin)
				replacement.token.leading_space = false;
			if (replacement.token.kind == TK_IDENTIFIER)
			{
				std::unordered_map<SpellingId, std::size_t>::const_iterator found =
					parameter_index.find(replacement.token.spelling);
				if (found != parameter_index.end())
					replacement.parameter = found->second;
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
			if (a.token.kind != b.token.kind ||
				a.token.spelling != b.token.spelling ||
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

	void ExtractArguments(std::deque<Token>* input, std::size_t close,
		const Macro& macro, std::vector<std::deque<Token> >* arguments,
		std::vector<Token>* separators)
	{
		const bool annotated_invocation =
			(*input)[1].matching_distance != 0 &&
			close == 1 + (*input)[1].matching_distance;
		const bool one_unsplit_argument =
			(!macro.variadic && macro.parameters.size() == 1 &&
			 annotated_invocation && (*input)[1].matching_commas == 0) ||
			(macro.variadic && macro.parameters.empty());
		if (one_unsplit_argument && close + 1 == input->size())
		{
			arguments->push_back(std::deque<Token>());
			arguments->back().swap(*input);
			arguments->back().pop_front();
			arguments->back().pop_front();
			arguments->back().pop_back();
			return;
		}

		input->pop_front();
		input->pop_front();
		arguments->push_back(std::deque<Token>());
		std::size_t depth = 0;
		const std::size_t content_tokens = close - 2;
		for (std::size_t i = 0; i < content_tokens; ++i)
		{
			Token token = input->front();
			input->pop_front();
			if (IsOperator(token, "("))
				++depth;
			else if (IsOperator(token, ")"))
				--depth;
			if (depth == 0 && IsOperator(token, ","))
			{
				separators->push_back(token);
				arguments->push_back(std::deque<Token>());
			}
			else
				arguments->back().push_back(token);
		}
		input->pop_front();
	}

	std::vector<Argument> BindArguments(const Macro& macro,
		const Token& head,
		std::vector<std::deque<Token> >* parsed,
		const std::vector<Token>& separators)
	{
		if (!macro.variadic && macro.parameters.empty() &&
			parsed->size() == 1 && (*parsed)[0].empty())
			parsed->clear();
		if ((!macro.variadic && parsed->size() != macro.parameters.size()) ||
			(macro.variadic && parsed->size() < macro.parameters.size()))
			throw std::runtime_error("wrong number of macro arguments");

		const std::size_t binding_count = macro.parameters.size() +
			(macro.variadic ? 1 : 0);
		std::vector<Argument> result(binding_count);
		for (std::size_t i = 0; i < macro.parameters.size(); ++i)
			result[i].raw.swap((*parsed)[i]);
		if (macro.variadic)
		{
			std::deque<Token>& variadic = result.back().raw;
			for (std::size_t i = macro.parameters.size();
				i < parsed->size(); ++i)
			{
				if (i != macro.parameters.size())
					variadic.push_back(separators[i - 1]);
				while (!(*parsed)[i].empty())
				{
					variadic.push_back((*parsed)[i].front());
					(*parsed)[i].pop_front();
				}
			}
		}
		for (std::size_t i = 0; i < macro.replacement.size(); ++i)
		{
			const ReplacementToken& replacement = macro.replacement[i];
			if (macro.function_like && IsOperator(replacement.token, "#"))
			{
				result[macro.replacement[++i].parameter].preserve_raw = true;
				continue;
			}
			if (replacement.parameter != kNoParameter &&
				((i != 0 && IsOperator(macro.replacement[i - 1].token, "##")) ||
				 (i + 1 < macro.replacement.size() &&
				  IsOperator(macro.replacement[i + 1].token, "##"))))
				result[replacement.parameter].preserve_raw = true;
		}

		// Argument prescan occurs before substitution into this replacement.
		// It inherits paint already carried by the invocation head, but the
		// current macro becomes unavailable only when the resulting argument
		// tokens are actually substituted.  Painting raw arguments eagerly
		// incorrectly suppresses nested calls such as f(g(x)).
		const PaintId inherited = head.paint;
		for (std::size_t i = 0; i < result.size(); ++i)
		{
			if (inherited != 0 || head.blocked != 0)
			{
				for (std::size_t j = 0; j < result[i].raw.size(); ++j)
				{
					result[i].raw[j].paint = paints_.Merge(
						result[i].raw[j].paint, inherited);
					result[i].raw[j].blocked = paints_.Merge(
						result[i].raw[j].blocked, head.blocked);
				}
			}
			if (!result[i].raw.empty())
				result[i].raw[0].leading_space = false;
		}
		return result;
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

	Token Stringize(const Argument& argument, bool leading_space,
		PaintId paint, PaintId blocked, std::uint64_t origin)
	{
		std::string spelling("\"");
		for (std::size_t i = 0; i < argument.raw.size(); ++i)
		{
			const Token& token = argument.raw[i];
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
		Token result(TK_STRING, spellings_.Intern(spelling), leading_space);
		result.paint = paint;
		result.blocked = blocked;
		result.origin = origin;
		return result;
	}

	std::vector<Token> Instantiate(const Macro& macro, const Token& head,
		std::vector<Argument>* arguments)
	{
		const std::uint64_t origin = next_origin_++;
		const PaintId direct_paint = paints_.Add(
			head.parameter_origin ? 0 : head.paint, macro.name);
		const PaintId parameter_paint = paints_.Add(
			head.parameter_origin ? head.paint : 0, macro.name);
		std::vector<Piece> pieces;
		for (std::size_t i = 0; i < macro.replacement.size(); ++i)
		{
			const ReplacementToken& replacement = macro.replacement[i];
			if (macro.function_like && IsOperator(replacement.token, "#"))
			{
				const ReplacementToken& parameter = macro.replacement[++i];
				pieces.push_back(Piece(Stringize((*arguments)[parameter.parameter],
					replacement.token.leading_space, direct_paint,
					head.blocked, origin), false));
				continue;
			}
			if (IsOperator(replacement.token, "##"))
			{
				Token marker = replacement.token;
				marker.paint = direct_paint;
				marker.blocked = head.blocked;
				marker.origin = origin;
				pieces.push_back(Piece(marker, true));
				continue;
			}
			if (replacement.parameter != kNoParameter)
			{
				const bool adjacent =
					(i != 0 && IsOperator(macro.replacement[i - 1].token, "##")) ||
					(i + 1 < macro.replacement.size() &&
					 IsOperator(macro.replacement[i + 1].token, "##"));
				if (!adjacent &&
					!(*arguments)[replacement.parameter].expanded_ready)
					throw std::logic_error("undemanded macro argument expansion");
				const std::deque<Token>& value = adjacent ?
					(*arguments)[replacement.parameter].raw :
					(*arguments)[replacement.parameter].expanded;
				if (value.empty() && adjacent)
				{
					Token place;
					place.paint = direct_paint;
					place.blocked = head.blocked;
					place.origin = origin;
					place.parameter_origin = true;
					place.leading_space = replacement.token.leading_space;
					place.from_variadic = macro.variadic &&
						replacement.parameter == macro.parameters.size();
					pieces.push_back(Piece(place, false));
				}
				else
				{
					for (std::size_t j = 0; j < value.size(); ++j)
					{
						Token token = value[j];
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
						pieces.push_back(Piece(token, false));
					}
				}
				continue;
			}
			Token literal = replacement.token;
			literal.paint = direct_paint;
			literal.blocked = head.blocked;
			literal.origin = origin;
			literal.parameter_origin = false;
			pieces.push_back(Piece(literal, false));
		}
		std::vector<Token> result = ResolvePastes(pieces);
		if (!result.empty())
			result[0].leading_space = result[0].leading_space || head.leading_space;
		AnnotateParentheses(&result);
		return result;
	}

	std::vector<Token> ResolvePastes(const std::vector<Piece>& pieces)
	{
		std::vector<Token> output;
		for (std::size_t i = 0; i < pieces.size(); ++i)
		{
			if (!pieces[i].paste_operator)
			{
				output.push_back(pieces[i].token);
				continue;
			}
			if (output.empty() || i + 1 >= pieces.size() ||
				pieces[i + 1].paste_operator)
				throw std::logic_error("invalid internal paste sequence");
			Token left = output.back();
			output.pop_back();
			Token right = pieces[++i].token;
			if (right.from_variadic && left.kind != TK_PLACEMARKER &&
				IsOperator(left, ","))
			{
				if (right.kind == TK_PLACEMARKER)
				{
					Token place;
					place.leading_space = left.leading_space;
					place.paint = paints_.Merge(left.paint, right.paint);
					place.blocked = paints_.Merge(left.blocked, right.blocked);
					place.origin = left.origin;
					place.parameter_origin = left.parameter_origin &&
						right.parameter_origin;
					output.push_back(place);
				}
				else
				{
					output.push_back(left);
					output.push_back(right);
				}
				continue;
			}
			output.push_back(Paste(left, right));
		}
		output.erase(std::remove_if(output.begin(), output.end(),
			IsPlacemarker), output.end());
		for (std::size_t i = 0; i < output.size(); ++i)
			output[i].from_variadic = false;
		return output;
	}

	static bool IsPlacemarker(const Token& token)
	{
		return token.kind == TK_PLACEMARKER;
	}

	Token Paste(const Token& left, const Token& right)
	{
		if (left.kind == TK_PLACEMARKER)
		{
			Token result = right;
			result.leading_space = left.leading_space || right.leading_space;
			result.paint = paints_.Merge(left.paint, right.paint);
			result.blocked = paints_.Merge(left.blocked, right.blocked);
			result.parameter_origin = left.parameter_origin &&
				right.parameter_origin;
			return result;
		}
		if (right.kind == TK_PLACEMARKER)
		{
			Token result = left;
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

	void Drain(std::deque<Token>& input, std::deque<Token>* collected,
		bool final, InvocationScan* scan)
	{
		std::vector<ExpansionFrame> frames;
		frames.push_back(ExpansionFrame(true, final));
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
					if (argument.preserve_raw)
						child.input = argument.raw;
					else
						child.input.swap(argument.raw);
					UpdatePeakRescan(child.input.size());
					frames.push_back(std::move(child));
					continue;
				}

				std::unordered_map<SpellingId, Macro>::const_iterator found =
					macros_.find(frame.pending.macro_name);
				if (found == macros_.end())
					throw std::logic_error("pending macro definition disappeared");
				const Token invocation_head = frame.pending.head;
				std::vector<Token> replacement = Instantiate(found->second,
					invocation_head, &frame.pending.arguments);
				frame.pending = PendingExpansion();
				if (replacement.empty() && invocation_head.leading_space &&
					!frame.input.empty())
					frame.input.front().leading_space = true;
				for (std::vector<Token>::reverse_iterator i = replacement.rbegin();
					i != replacement.rend(); ++i)
					frame.input.push_front(*i);
				frame.scan.Reset();
				if (stats_)
				{
					++stats_->macro_invocations;
					stats_->expanded_tokens += replacement.size();
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
				std::deque<Token> result;
				result.swap(frame.output);
				frames.pop_back();
				ExpansionFrame& parent = frames.back();
				Argument& argument = parent.pending.arguments[
					parent.pending.active_argument];
				argument.expanded.swap(result);
				argument.expanded_ready = true;
				if (stats_)
					++stats_->argument_prescans;
				continue;
			}

			const Token head = frame.input.front();
			if (head.kind == TK_IDENTIFIER)
			{
				if (head.spelling == id_va_args_)
					throw std::runtime_error(
						"__VA_ARGS__ outside a variadic replacement list");
				if (stats_)
					++stats_->macro_lookups;
				std::unordered_map<SpellingId, Macro>::const_iterator found =
					macros_.find(head.spelling);
				if (found != macros_.end() &&
					!paints_.Contains(head.paint, head.spelling) &&
					!paints_.Contains(head.blocked, head.spelling))
				{
					const Macro& macro = found->second;
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
						std::vector<std::deque<Token> > parsed;
						std::vector<Token> separators;
						ExtractArguments(&frame.input, close, macro, &parsed,
							&separators);
						frame.scan.Reset();
						frame.pending.active = true;
						frame.pending.macro_name = macro.name;
						frame.pending.head = invocation_head;
						frame.pending.arguments = BindArguments(macro,
							invocation_head, &parsed, separators);
						frame.pending.demanded_arguments =
							DemandedArguments(macro);
						continue;
					}
					else
					{
						frame.input.pop_front();
						std::vector<Argument> no_arguments;
						std::vector<Token> replacement = Instantiate(macro,
							invocation_head,
							&no_arguments);
						if (replacement.empty() && head.leading_space &&
							!frame.input.empty())
							frame.input.front().leading_space = true;
						for (std::vector<Token>::reverse_iterator i =
							replacement.rbegin(); i != replacement.rend(); ++i)
							frame.input.push_front(*i);
						frame.scan.Reset();
						if (stats_)
						{
							++stats_->macro_invocations;
							stats_->expanded_tokens += replacement.size();
						}
						UpdatePeakRescan(frame.input.size());
						continue;
					}
				}
			}

	emit_head:
			frame.input.pop_front();
			frame.scan.Reset();
			Token emitted = head;
			if (emitted.kind == TK_IDENTIFIER &&
				paints_.Contains(emitted.paint, emitted.spelling))
				emitted.blocked = paints_.Add(emitted.blocked, emitted.spelling);
			if (!frame.root)
				frame.output.push_back(emitted);
			else if (collected)
				collected->push_back(emitted);
			else
				EmitPostToken(emitted);
		}
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
	std::unordered_map<SpellingId, Macro> macros_;
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
	  macro_definitions(0), macro_undefinitions(0), macro_lookups(0),
	  macro_invocations(0), argument_prescans(0), expanded_tokens(0),
	  pasted_tokens(0), pasted_spelling_bytes(0), peak_line_tokens(0),
	  peak_rescan_tokens(0), peak_retained_replacement_tokens(0),
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
