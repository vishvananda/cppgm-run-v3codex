#include "namespace_initialization/program.h"

#include "support/exceptions.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace cppgm
{
namespace namespace_initialization
{

namespace
{

const std::uint16_t kIdentifierToken =
	static_cast<std::uint16_t>(OP_ARROW) + 1;
const std::uint16_t kLiteralToken = kIdentifierToken + 1;
const std::uint16_t kEofToken = kIdentifierToken + 2;

enum DeclaratorMode
{
	DECLARATOR_NAMED,
	DECLARATOR_ABSTRACT
};

// C++ permits implementation limits; this stays above the recommended minimum
// while bounding the one remaining recursive grammar relationship under
// sanitizer-inflated stack frames. Parenthesized declarators themselves use an
// explicit frame stack and are not subject to this limit.
const std::size_t kMaxDeclaratorCallDepth = 512;

struct DeclaratorMemoEntry
{
	std::size_t start;
	ScopeId scope;
	DeclaratorMode mode;
	std::size_t end;
	bool success;
	Declarator declarator;

	DeclaratorMemoEntry(std::size_t start_value, ScopeId scope_value,
		DeclaratorMode mode_value, std::size_t end_value, bool success_value,
		Declarator&& declarator_value)
		: start(start_value), scope(scope_value), mode(mode_value), end(end_value),
		  success(success_value), declarator(std::move(declarator_value)) {}
};

struct DeclaratorMemoSlot
{
	std::uint32_t entry;
	std::uint32_t generation;

	DeclaratorMemoSlot() : entry(0), generation(0) {}
};

struct FundamentalSpecifiers
{
	unsigned int character;
	unsigned int character16;
	unsigned int character32;
	unsigned int wide_character;
	unsigned int boolean;
	unsigned int short_count;
	unsigned int int_count;
	unsigned int signed_count;
	unsigned int unsigned_count;
	unsigned int float_count;
	unsigned int double_count;
	unsigned int void_count;
	unsigned int long_count;

	FundamentalSpecifiers()
		: character(0), character16(0), character32(0), wide_character(0),
		  boolean(0), short_count(0), int_count(0), signed_count(0),
		  unsigned_count(0), float_count(0), double_count(0), void_count(0),
		  long_count(0) {}

	bool Empty() const
	{
		return character == 0 && character16 == 0 && character32 == 0 &&
			wide_character == 0 && boolean == 0 && short_count == 0 &&
			int_count == 0 && signed_count == 0 && unsigned_count == 0 &&
			float_count == 0 && double_count == 0 && void_count == 0 &&
			long_count == 0;
	}
};

class TranslationUnitParser
{
public:
	TranslationUnitParser(const TokenBuffer& input, Model& model, ScopeId root,
		std::uint32_t unit)
		: input_(input), model_(model), position_(0), current_scope_(root),
		  unit_(unit), declarator_memo_generation_(0),
		  declarator_call_depth_(0), memo_child_storage_bytes_(0),
		  active_declarator_stack_bytes_(0) {}

	void Parse()
	{
		while (true)
		{
			if (AtEof())
			{
				if (!scope_stack_.empty())
					ThrowSyntaxError("unterminated namespace");
				return;
			}
			if (Match(OP_RBRACE))
			{
				if (scope_stack_.empty())
					ThrowSyntaxError("unexpected namespace close");
				current_scope_ = scope_stack_.back();
				scope_stack_.pop_back();
				continue;
			}
			ParseDeclaration();
		}
	}

private:
	void RecordParserStack()
	{
		if (!model_.stats) return;
		const std::size_t bytes = active_declarator_stack_bytes_ +
			scope_stack_.capacity() * sizeof(ScopeId);
		model_.stats->peak_parser_scratch_bytes = std::max(
			model_.stats->peak_parser_scratch_bytes, bytes);
	}

	void RecordDeclaratorFrame(std::size_t capacity_bytes,
		std::size_t* accounted_bytes)
	{
		if (!model_.stats) return;
		++model_.stats->declarator_frames;
		if (capacity_bytes > *accounted_bytes)
		{
			active_declarator_stack_bytes_ += capacity_bytes - *accounted_bytes;
			*accounted_bytes = capacity_bytes;
		}
		RecordParserStack();
	}

	void BeginDeclaratorMemoSession()
	{
		if (declarator_call_depth_ != 0) return;
		++declarator_memo_generation_;
		if (declarator_memo_generation_ == 0)
		{
			for (std::size_t i = 0; i < declarator_memo_slots_.size(); ++i)
				declarator_memo_slots_[i].generation = 0;
			++declarator_memo_generation_;
		}
		declarator_memo_entries_.clear();
		memo_child_storage_bytes_ = 0;
	}

	std::size_t HashDeclaratorMemo(std::size_t start, ScopeId scope,
		DeclaratorMode mode) const
	{
		return MixHash(MixHash(MixHash(0, start), scope), mode);
	}

	bool SameDeclaratorMemoKey(const DeclaratorMemoEntry& entry,
		std::size_t start, ScopeId scope, DeclaratorMode mode) const
	{
		return entry.start == start && entry.scope == scope && entry.mode == mode;
	}

	void RehashDeclaratorMemo(std::size_t capacity)
	{
		std::vector<DeclaratorMemoSlot> replacement(capacity);
		const std::size_t mask = capacity - 1;
		for (std::uint32_t i = 0; i < declarator_memo_entries_.size(); ++i)
		{
			const DeclaratorMemoEntry& entry = declarator_memo_entries_[i];
			std::size_t slot = HashDeclaratorMemo(entry.start, entry.scope,
				entry.mode) & mask;
			while (replacement[slot].generation == declarator_memo_generation_)
				slot = (slot + 1) & mask;
			replacement[slot].entry = i;
			replacement[slot].generation = declarator_memo_generation_;
		}
		declarator_memo_slots_.swap(replacement);
	}

	bool FindDeclaratorMemo(std::size_t start, ScopeId scope,
		DeclaratorMode mode, bool* success, Declarator* result)
	{
		if (declarator_memo_slots_.empty()) return false;
		const std::size_t mask = declarator_memo_slots_.size() - 1;
		std::size_t slot = HashDeclaratorMemo(start, scope, mode) & mask;
		while (declarator_memo_slots_[slot].generation ==
			declarator_memo_generation_)
		{
			const DeclaratorMemoEntry& entry = declarator_memo_entries_[
				declarator_memo_slots_[slot].entry];
			if (SameDeclaratorMemoKey(entry, start, scope, mode))
			{
				position_ = entry.end;
				*success = entry.success;
				if (entry.success) *result = entry.declarator;
				return true;
			}
			slot = (slot + 1) & mask;
		}
		return false;
	}

	std::size_t DeclaratorChildStorage(const Declarator& declarator) const
	{
		std::size_t bytes = declarator.name.segments.StorageBytes() +
			declarator.operations.capacity() * sizeof(DeclaratorOperation);
		for (std::size_t i = 0; i < declarator.operations.size(); ++i)
			bytes += declarator.operations[i].parameters.capacity() * sizeof(TypeId);
		return bytes;
	}

	void StoreDeclaratorMemo(std::size_t start, ScopeId scope,
		DeclaratorMode mode, std::size_t end, bool success,
		Declarator&& declarator)
	{
		if (declarator_memo_slots_.empty())
			declarator_memo_slots_.resize(32);
		if ((declarator_memo_entries_.size() + 1) * 10 >
			declarator_memo_slots_.size() * 7)
			RehashDeclaratorMemo(declarator_memo_slots_.size() * 2);
		if (declarator_memo_entries_.size() >=
			std::numeric_limits<std::uint32_t>::max())
			ThrowSemanticResourceLimit("too many declarator memo entries");
		const std::size_t mask = declarator_memo_slots_.size() - 1;
		std::size_t slot = HashDeclaratorMemo(start, scope, mode) & mask;
		while (declarator_memo_slots_[slot].generation ==
			declarator_memo_generation_)
			slot = (slot + 1) & mask;
		const std::uint32_t index =
			static_cast<std::uint32_t>(declarator_memo_entries_.size());
		declarator_memo_entries_.push_back(DeclaratorMemoEntry(start, scope,
			mode, end, success, std::move(declarator)));
		declarator_memo_slots_[slot].entry = index;
		declarator_memo_slots_[slot].generation =
			declarator_memo_generation_;
		memo_child_storage_bytes_ += DeclaratorChildStorage(
			declarator_memo_entries_.back().declarator);
		if (!model_.stats) return;
		model_.stats->declarator_memo_entries = std::max(
			model_.stats->declarator_memo_entries,
			declarator_memo_entries_.size());
		const std::size_t storage = declarator_memo_slots_.capacity() *
				sizeof(DeclaratorMemoSlot) +
			declarator_memo_entries_.capacity() * sizeof(DeclaratorMemoEntry) +
			memo_child_storage_bytes_;
		model_.stats->parser_memo_storage_bytes = std::max(
			model_.stats->parser_memo_storage_bytes, storage);
	}

	bool At(SimpleTokenKind kind) const
	{
		return position_ < input_.tokens.size() &&
			input_.tokens[position_].kind == static_cast<std::uint16_t>(kind);
	}

	bool AtOffset(std::size_t offset, SimpleTokenKind kind) const
	{
		return position_ + offset < input_.tokens.size() &&
			input_.tokens[position_ + offset].kind ==
				static_cast<std::uint16_t>(kind);
	}

	bool AtIdentifier() const
	{
		return position_ < input_.tokens.size() &&
			input_.tokens[position_].kind == kIdentifierToken;
	}

	bool AtLiteral() const
	{
		return position_ < input_.tokens.size() &&
			input_.tokens[position_].kind == kLiteralToken;
	}

	bool AtEof() const
	{
		return position_ < input_.tokens.size() &&
			input_.tokens[position_].kind == kEofToken;
	}

	bool Match(SimpleTokenKind kind)
	{
		if (!At(kind)) return false;
		++position_;
		return true;
	}

	void Expect(SimpleTokenKind kind)
	{
		if (!Match(kind))
			ThrowSyntaxError(std::string("expected ") +
				SimpleTokenKindName(kind));
	}

	NameId ConsumeIdentifier()
	{
		if (!AtIdentifier()) ThrowSyntaxError("expected identifier");
		return input_.tokens[position_++].name;
	}

	bool ParseQualifiedName(QualifiedName* result)
	{
		const std::size_t start = position_;
		QualifiedName parsed;
		parsed.absolute = Match(OP_COLON2);
		if (!AtIdentifier())
		{
			position_ = start;
			return false;
		}
		parsed.segments.push_back(ConsumeIdentifier());
		while (Match(OP_COLON2))
		{
			if (!AtIdentifier())
			{
				position_ = start;
				return false;
			}
			parsed.segments.push_back(ConsumeIdentifier());
		}
		*result = parsed;
		return true;
	}

	bool ConsumeFundamental(FundamentalSpecifiers* value)
	{
		if (Match(KW_CHAR)) ++value->character;
		else if (Match(KW_CHAR16_T)) ++value->character16;
		else if (Match(KW_CHAR32_T)) ++value->character32;
		else if (Match(KW_WCHAR_T)) ++value->wide_character;
		else if (Match(KW_BOOL)) ++value->boolean;
		else if (Match(KW_SHORT)) ++value->short_count;
		else if (Match(KW_INT)) ++value->int_count;
		else if (Match(KW_SIGNED)) ++value->signed_count;
		else if (Match(KW_UNSIGNED)) ++value->unsigned_count;
		else if (Match(KW_FLOAT)) ++value->float_count;
		else if (Match(KW_DOUBLE)) ++value->double_count;
		else if (Match(KW_VOID)) ++value->void_count;
		else if (Match(KW_LONG)) ++value->long_count;
		else return false;
		return true;
	}

	FundamentalType SelectFundamental(const FundamentalSpecifiers& value)
	{
		const unsigned int named = value.character + value.character16 +
			value.character32 + value.wide_character + value.boolean +
			value.float_count + value.double_count + value.void_count;
		if (value.signed_count > 1 || value.unsigned_count > 1 ||
			(value.signed_count && value.unsigned_count) ||
			value.short_count > 1 || value.int_count > 1 ||
			value.long_count > 2 || named > 1)
			ThrowSemanticError("invalid fundamental type specifiers");
		if (value.character)
		{
			if (value.short_count || value.int_count || value.long_count)
				ThrowSemanticError("invalid character type specifiers");
			if (value.unsigned_count) return FT_UNSIGNED_CHAR;
			if (value.signed_count) return FT_SIGNED_CHAR;
			return FT_CHAR;
		}
		if (value.character16 || value.character32 || value.wide_character ||
			value.boolean || value.float_count || value.void_count)
		{
			if (value.signed_count || value.unsigned_count || value.short_count ||
				value.int_count || value.long_count)
				ThrowSemanticError("invalid fundamental type combination");
			if (value.character16) return FT_CHAR16_T;
			if (value.character32) return FT_CHAR32_T;
			if (value.wide_character) return FT_WCHAR_T;
			if (value.boolean) return FT_BOOL;
			if (value.float_count) return FT_FLOAT;
			return FT_VOID;
		}
		if (value.double_count)
		{
			if (value.signed_count || value.unsigned_count || value.short_count ||
				value.int_count || value.long_count > 1)
				ThrowSemanticError("invalid floating type combination");
			return value.long_count == 0 ? FT_DOUBLE : FT_LONG_DOUBLE;
		}
		if (value.short_count && value.long_count)
			ThrowSemanticError("short and long cannot be combined");
		if (value.short_count)
			return value.unsigned_count ? FT_UNSIGNED_SHORT_INT : FT_SHORT_INT;
		if (value.long_count >= 2)
			return value.unsigned_count ? FT_UNSIGNED_LONG_LONG_INT :
				FT_LONG_LONG_INT;
		if (value.long_count == 1)
			return value.unsigned_count ? FT_UNSIGNED_LONG_INT : FT_LONG_INT;
		return value.unsigned_count ? FT_UNSIGNED_INT : FT_INT;
	}

	bool ParseDeclarationSpecifiers(DeclarationSpecifiers* result)
	{
		const std::size_t start = position_;
		FundamentalSpecifiers fundamental;
		TypeId named_type = 0;
		unsigned char cv = CV_NONE;
		bool consumed = false;
		while (true)
		{
			if (Match(KW_STATIC))
			{
				if (result->is_static) ThrowSemanticError("duplicate static");
				result->is_static = true; consumed = true; continue;
			}
			if (Match(KW_EXTERN))
			{
				if (result->is_extern) ThrowSemanticError("duplicate extern");
				result->is_extern = true; consumed = true; continue;
			}
			if (Match(KW_THREAD_LOCAL))
			{
				if (result->is_thread_local)
					ThrowSemanticError("duplicate thread_local");
				result->is_thread_local = true; consumed = true; continue;
			}
			if (Match(KW_TYPEDEF))
			{
				if (result->is_typedef) ThrowSemanticError("duplicate typedef");
				result->is_typedef = true; consumed = true; continue;
			}
			if (Match(KW_CONSTEXPR))
			{
				if (result->is_constexpr)
					ThrowSemanticError("duplicate constexpr");
				result->is_constexpr = true; consumed = true; continue;
			}
			if (Match(KW_INLINE))
			{
				if (result->is_inline) ThrowSemanticError("duplicate inline");
				result->is_inline = true; consumed = true; continue;
			}
			if (Match(KW_CONST))
			{
				if ((cv & CV_CONST) != 0)
					ThrowSemanticError("duplicate const qualifier");
				cv |= CV_CONST; consumed = true; continue;
			}
			if (Match(KW_VOLATILE))
			{
				if ((cv & CV_VOLATILE) != 0)
					ThrowSemanticError("duplicate volatile qualifier");
				cv |= CV_VOLATILE; consumed = true; continue;
			}
			if (named_type == 0 && ConsumeFundamental(&fundamental))
			{
				consumed = true; continue;
			}
			if (named_type == 0 && fundamental.Empty())
			{
				const std::size_t type_start = position_;
				QualifiedName name;
				if (ParseQualifiedName(&name) &&
					model_.ResolveTypeName(current_scope_, name, &named_type))
				{
					consumed = true; continue;
				}
				position_ = type_start;
			}
			break;
		}
		if (!consumed || (named_type == 0 && fundamental.Empty()))
		{
			position_ = start;
			return false;
		}
		if (result->is_static && result->is_extern)
			ThrowSemanticError("static and extern cannot be combined");
		if (result->is_typedef && (result->is_static || result->is_extern ||
			result->is_constexpr || result->is_inline ||
			result->is_thread_local))
			ThrowSemanticError("invalid typedef specifiers");
		TypeId type = named_type == 0 ?
			model_.types.Fundamental(SelectFundamental(fundamental)) : named_type;
		result->type = model_.types.Qualify(type, cv);
		return true;
	}

	bool ParsePointerOperator(DeclaratorOperation* operation)
	{
		if (Match(OP_STAR))
		{
			*operation = DeclaratorOperation(TYPE_POINTER);
			while (true)
			{
				if (Match(KW_CONST))
				{
					if ((operation->cv & CV_CONST) != 0)
						ThrowSemanticError("duplicate pointer const qualifier");
					operation->cv |= CV_CONST;
				}
				else if (Match(KW_VOLATILE))
				{
					if ((operation->cv & CV_VOLATILE) != 0)
						ThrowSemanticError("duplicate pointer volatile qualifier");
					operation->cv |= CV_VOLATILE;
				}
				else break;
			}
			return true;
		}
		if (Match(OP_AMP))
		{
			*operation = DeclaratorOperation(TYPE_LVALUE_REFERENCE);
			return true;
		}
		if (Match(OP_LAND))
		{
			*operation = DeclaratorOperation(TYPE_RVALUE_REFERENCE);
			return true;
		}
		return false;
	}

	std::uint64_t ArrayBound(const Expression& expression)
	{
		bool constant = expression.constant_expression;
		InitialValue value = expression.category == VALUE_LVALUE ?
			model_.LvalueToRvalue(expression, &constant) : expression.value;
		const TypeId type = model_.types.RemoveTopCv(expression.type);
		const TypeRecord& record = model_.types.Get(type);
		if (!constant || record.kind != TYPE_FUNDAMENTAL ||
			!IsIntegralFundamental(record.fundamental) ||
			value.kind != INITIAL_SCALAR)
			ThrowSemanticError("array bound is not an integral constant expression");
		const long double decoded = ReadArithmetic(value);
		if (decoded <= 0 || decoded >
			static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
			ThrowSemanticError("invalid array bound");
		return static_cast<std::uint64_t>(decoded);
	}

	bool ParseArrayOperation(ScopeId expression_scope,
		DeclaratorOperation* operation)
	{
		if (!Match(OP_LSQUARE)) return false;
		*operation = DeclaratorOperation(TYPE_ARRAY);
		if (!At(OP_RSQUARE))
			operation->bound = ArrayBound(ParseExpression(expression_scope));
		Expect(OP_RSQUARE);
		return true;
	}

	bool IsParameterEnd() const
	{
		return At(OP_COMMA) || At(OP_RPAREN) || At(OP_DOTS);
	}

	bool ParseParameter(ScopeId scope, TypeId* result, bool* bare_void)
	{
		const ScopeId saved = current_scope_;
		current_scope_ = scope;
		DeclarationSpecifiers specifiers;
		if (!ParseDeclarationSpecifiers(&specifiers))
		{
			current_scope_ = saved;
			return false;
		}
		if (specifiers.is_typedef || specifiers.is_static ||
			specifiers.is_extern ||
			specifiers.is_thread_local || specifiers.is_constexpr ||
			specifiers.is_inline)
			ThrowSemanticError("invalid parameter specifiers");
		Declarator declarator;
		bool has_declarator = false;
		if (!IsParameterEnd())
		{
			const std::size_t declarator_start = position_;
			if (ParseDeclarator(DECLARATOR_NAMED, &declarator) &&
				IsParameterEnd()) has_declarator = true;
			else
			{
				position_ = declarator_start;
				if (!ParseDeclarator(DECLARATOR_ABSTRACT, &declarator) ||
					!IsParameterEnd())
				{
					current_scope_ = saved;
					return false;
				}
				has_declarator = true;
			}
		}
		TypeId type = ApplyDeclarator(specifiers.type, declarator);
		*bare_void = !has_declarator && model_.types.IsVoid(type);
		if (has_declarator && model_.types.IsVoid(type))
			ThrowSemanticError("named parameter has void type");
		*result = model_.types.AdjustParameter(type);
		current_scope_ = saved;
		return true;
	}

	bool ParseFunctionOperation(ScopeId scope,
		DeclaratorOperation* operation)
	{
		const std::size_t start = position_;
		if (!Match(OP_LPAREN)) return false;
		DeclaratorOperation parsed(TYPE_FUNCTION);
		bool saw_bare_void = false;
		if (Match(OP_RPAREN))
		{
			*operation = parsed;
			return true;
		}
		if (Match(OP_DOTS))
		{
			Expect(OP_RPAREN);
			parsed.variadic = true;
			*operation = parsed;
			return true;
		}
		while (true)
		{
			TypeId parameter;
			bool bare_void = false;
			if (!ParseParameter(scope, &parameter, &bare_void))
			{
				position_ = start;
				return false;
			}
			parsed.parameters.push_back(parameter);
			saw_bare_void = saw_bare_void || bare_void;
			if (Match(OP_DOTS))
			{
				parsed.variadic = true;
				break;
			}
			if (!Match(OP_COMMA)) break;
			if (Match(OP_DOTS))
			{
				parsed.variadic = true;
				break;
			}
		}
		if (!Match(OP_RPAREN))
		{
			position_ = start;
			return false;
		}
		if (saw_bare_void)
		{
			if (parsed.parameters.size() != 1 || parsed.variadic)
				ThrowSemanticError("void parameter in parameter list");
			parsed.parameters.clear();
		}
		*operation = parsed;
		return true;
	}

	bool ParseDeclarator(DeclaratorMode mode, Declarator* result)
	{
		BeginDeclaratorMemoSession();
		if (declarator_call_depth_ >= kMaxDeclaratorCallDepth)
			ThrowSemanticResourceLimit("declarator nesting limit exceeded");
		++declarator_call_depth_;
		const std::size_t start = position_;
		const ScopeId scope = current_scope_;
		if (declarator_call_depth_ == 1)
		{
			const bool success = ParseDeclaratorUncached(mode, result);
			--declarator_call_depth_;
			return success;
		}
		bool success = false;
		if (FindDeclaratorMemo(start, scope, mode, &success, result))
		{
			if (model_.stats) ++model_.stats->declarator_cache_hits;
			--declarator_call_depth_;
			return success;
		}
		if (model_.stats) ++model_.stats->declarator_cache_misses;
		Declarator parsed;
		success = ParseDeclaratorUncached(mode, &parsed);
		StoreDeclaratorMemo(start, scope, mode, position_, success,
			std::move(parsed));
		if (success) *result = declarator_memo_entries_.back().declarator;
		--declarator_call_depth_;
		return success;
	}

	bool ParseDeclaratorUncached(DeclaratorMode mode, Declarator* result)
	{
		enum FrameState { FRAME_START, FRAME_WAIT_GROUP, FRAME_SUFFIXES };
		struct Frame
		{
			std::size_t start;
			std::size_t group_start;
			FrameState state;
			bool direct;
			bool suffix;
			bool failed;
			Declarator parsed;
			std::vector<DeclaratorOperation> prefixes;

			explicit Frame(std::size_t start_value)
				: start(start_value), group_start(start_value), state(FRAME_START),
				  direct(false), suffix(false), failed(false) {}
		};
		std::vector<Frame> frames;
		frames.push_back(Frame(position_));
		std::size_t accounted_frame_bytes = 0;
		RecordDeclaratorFrame(frames.capacity() * sizeof(Frame),
			&accounted_frame_bytes);
		while (true)
		{
			Frame& frame = frames.back();
			if (frame.state == FRAME_START)
			{
				while (true)
				{
					DeclaratorOperation operation(TYPE_POINTER);
					if (!ParsePointerOperator(&operation)) break;
					frame.prefixes.push_back(operation);
				}
				if (mode == DECLARATOR_NAMED &&
					(AtIdentifier() || At(OP_COLON2)))
				{
					if (!ParseQualifiedName(&frame.parsed.name)) frame.failed = true;
					else
					{
						frame.parsed.has_name = true;
						frame.parsed.resolved_owner = model_.ResolveDeclaratorOwner(
							current_scope_, frame.parsed.name);
						frame.direct = true;
					}
					frame.state = FRAME_SUFFIXES;
					continue;
				}
				if (At(OP_LPAREN))
				{
					frame.group_start = position_++;
					frame.state = FRAME_WAIT_GROUP;
					frames.push_back(Frame(position_));
					RecordDeclaratorFrame(frames.capacity() * sizeof(Frame),
						&accounted_frame_bytes);
					continue;
				}
				frame.state = FRAME_SUFFIXES;
			}
			while (!frame.failed)
			{
				DeclaratorOperation operation(TYPE_ARRAY);
				const ScopeId suffix_scope = frame.parsed.has_name ?
					frame.parsed.resolved_owner : current_scope_;
				if (ParseArrayOperation(suffix_scope, &operation) ||
					ParseFunctionOperation(suffix_scope, &operation))
				{
					if (operation.kind == TYPE_FUNCTION)
						frame.parsed.has_function_operation = true;
					frame.parsed.operations.push_back(std::move(operation));
					frame.suffix = true;
					continue;
				}
				break;
			}
			for (std::vector<DeclaratorOperation>::reverse_iterator i =
				frame.prefixes.rbegin(); i != frame.prefixes.rend(); ++i)
				frame.parsed.operations.push_back(std::move(*i));
			const bool success = !frame.failed &&
				(frame.direct || frame.suffix || !frame.prefixes.empty()) &&
				(mode != DECLARATOR_NAMED || frame.parsed.has_name) &&
				(mode != DECLARATOR_ABSTRACT || !frame.parsed.has_name);
			const std::size_t frame_start = frame.start;
			Declarator completed = std::move(frame.parsed);
			frames.pop_back();
			if (frames.empty())
			{
				if (model_.stats)
					active_declarator_stack_bytes_ -= accounted_frame_bytes;
				if (!success)
				{
					position_ = frame_start;
					return false;
				}
				*result = std::move(completed);
				return true;
			}
			Frame& parent = frames.back();
			if (parent.state != FRAME_WAIT_GROUP)
				ThrowSemanticInternal("invalid declarator frame state");
			if (success && Match(OP_RPAREN))
			{
				parent.parsed = std::move(completed);
				parent.direct = true;
			}
			else position_ = parent.group_start;
			parent.state = FRAME_SUFFIXES;
		}
	}

	TypeId ApplyDeclarator(TypeId base, const Declarator& declarator)
	{
		TypeId type = base;
		bool direct_reference_applied = false;
		for (std::vector<DeclaratorOperation>::const_reverse_iterator i =
			declarator.operations.rbegin(); i != declarator.operations.rend(); ++i)
		{
			switch (i->kind)
			{
			case TYPE_POINTER:
				type = model_.types.Qualify(model_.types.Pointer(type), i->cv);
				break;
			case TYPE_LVALUE_REFERENCE: case TYPE_RVALUE_REFERENCE:
			{
				const bool collapse = !direct_reference_applied &&
					model_.types.IsReference(type);
				type = model_.types.Reference(i->kind, type, collapse);
				direct_reference_applied = true;
				break;
			}
			case TYPE_ARRAY:
				type = model_.types.Array(type, i->bound);
				break;
			case TYPE_FUNCTION:
				type = model_.types.Function(type, i->parameters, i->variadic);
				break;
			default: ThrowSemanticInternal("invalid declarator operation");
			}
		}
		return type;
	}

	Expression ParseExpression(ScopeId scope)
	{
		std::size_t parentheses = 0;
		while (Match(OP_LPAREN)) ++parentheses;
		Expression expression;
		expression.translation_unit = unit_;
		if (Match(KW_TRUE) || Match(KW_FALSE))
		{
			const bool value = input_.tokens[position_ - 1].kind ==
				static_cast<std::uint16_t>(KW_TRUE);
			expression.type = model_.types.Fundamental(FT_BOOL);
			expression.value.kind = INITIAL_SCALAR;
			expression.value.scalar_type = FT_BOOL;
			expression.value.bytes[0] = value ? 1 : 0;
			expression.constant_expression = true;
		}
		else if (Match(KW_NULLPTR))
		{
			expression.type = model_.types.Fundamental(FT_NULLPTR_T);
			expression.value.kind = INITIAL_ZERO;
			expression.constant_expression = true;
			expression.null_pointer_constant = true;
		}
		else if (AtLiteral())
		{
			const Token& token = input_.tokens[position_++];
			if (token.literal_array)
			{
				const StringId string = model_.AddString(token.literal_type,
					&input_.bytes[token.byte_offset], token.byte_size);
				const TypeId element = model_.types.Qualify(
					model_.types.Fundamental(token.literal_type), CV_CONST);
				expression.type = model_.types.Array(element, token.elements);
				expression.category = VALUE_LVALUE;
				expression.value.kind = INITIAL_ADDRESS_STRING;
				expression.value.target = string;
				expression.constant_expression = true;
				expression.string_literal = true;
				expression.string_id = string;
			}
			else
			{
				expression.type = model_.types.Fundamental(token.literal_type);
				expression.value.kind = INITIAL_SCALAR;
				expression.value.scalar_type = token.literal_type;
				std::memcpy(expression.value.bytes.data(),
					&input_.bytes[token.byte_offset], token.byte_size);
				expression.constant_expression = true;
				if (token.integer_literal &&
					ReadArithmetic(expression.value) == 0)
					expression.null_pointer_constant = true;
			}
		}
		else
		{
			QualifiedName name;
			if (!ParseQualifiedName(&name))
				ThrowSyntaxError("expected expression");
			LookupResult found;
			const EntityId entity = model_.ResolveExpressionEntity(scope, name,
				&found);
			if (entity == kNoEntity && found.first_function == kNoCandidate)
				ThrowSemanticError("id-expression lookup failed");
			if (entity == kNoEntity)
			{
				expression.first_function = found.first_function;
				expression.last_function = found.last_function;
			}
			else expression = model_.ExpressionForEntity(entity, unit_);
		}
		while (parentheses != 0)
		{
			Expect(OP_RPAREN);
			--parentheses;
		}
		return expression;
	}

	void ParseNamespaceDeclaration()
	{
		const bool is_inline = Match(KW_INLINE);
		Expect(KW_NAMESPACE);
		if (!is_inline && AtIdentifier() && AtOffset(1, OP_ASS))
		{
			const NameId alias = ConsumeIdentifier();
			Expect(OP_ASS);
			QualifiedName target_name;
			if (!ParseQualifiedName(&target_name))
				ThrowSyntaxError("expected namespace alias target");
			ScopeId target;
			if (!model_.ResolveNamespaceName(current_scope_, target_name, &target))
				ThrowSemanticError("namespace alias lookup failed");
			Expect(OP_SEMICOLON);
			model_.AddNamespaceAlias(current_scope_, alias, target);
			return;
		}
		NameId name = 0;
		if (AtIdentifier()) name = ConsumeIdentifier();
		Expect(OP_LBRACE);
		scope_stack_.push_back(current_scope_);
		RecordParserStack();
		current_scope_ = model_.OpenNamespace(current_scope_, name, is_inline);
	}

	void ParseUsingDeclaration()
	{
		Expect(KW_USING);
		if (Match(KW_NAMESPACE))
		{
			QualifiedName target_name;
			if (!ParseQualifiedName(&target_name))
				ThrowSyntaxError("expected using-directive target");
			ScopeId target;
			if (!model_.ResolveNamespaceName(current_scope_, target_name, &target))
				ThrowSemanticError("using-directive lookup failed");
			Expect(OP_SEMICOLON);
			model_.AddUsingDirective(current_scope_, target);
			return;
		}
		if (AtIdentifier() && AtOffset(1, OP_ASS))
		{
			const NameId alias = ConsumeIdentifier();
			Expect(OP_ASS);
			DeclarationSpecifiers specifiers;
			if (!ParseDeclarationSpecifiers(&specifiers) || specifiers.is_typedef ||
				specifiers.is_static || specifiers.is_extern ||
				specifiers.is_thread_local || specifiers.is_constexpr ||
				specifiers.is_inline)
				ThrowSyntaxError("expected alias type-id");
			Declarator declarator;
			if (!At(OP_SEMICOLON) &&
				!ParseDeclarator(DECLARATOR_ABSTRACT, &declarator))
				ThrowSyntaxError("invalid alias declarator");
			Expect(OP_SEMICOLON);
			model_.AddTypeAlias(current_scope_, alias,
				ApplyDeclarator(specifiers.type, declarator));
			return;
		}
		QualifiedName target_name;
		if (!ParseQualifiedName(&target_name))
			ThrowSyntaxError("expected using-declaration target");
		LookupResult target;
		if (!model_.ResolveUsingTarget(current_scope_, target_name, &target))
			ThrowSemanticError("using-declaration lookup failed");
		Expect(OP_SEMICOLON);
		model_.AddUsingDeclaration(current_scope_, target_name.segments.back(),
			target);
	}

	bool IsUsableIntegralConstant(TypeId type, bool initializer_constant) const
	{
		if (!initializer_constant || !model_.types.IsConst(type)) return false;
		type = model_.types.RemoveTopCv(type);
		const TypeRecord& record = model_.types.Get(type);
		return record.kind == TYPE_FUNDAMENTAL &&
			IsIntegralFundamental(record.fundamental);
	}

	bool ParseDeclaredEntity(const DeclarationSpecifiers& specifiers,
		bool first)
	{
		Declarator declarator;
		if (!ParseDeclarator(DECLARATOR_NAMED, &declarator))
			ThrowSyntaxError("expected declarator");
		TypeId type = ApplyDeclarator(specifiers.type, declarator);
		if (specifiers.is_constexpr && !model_.types.IsFunction(type))
			type = model_.types.AddTopConst(type);
		if (specifiers.is_typedef)
		{
			if (declarator.name.absolute || declarator.name.segments.size() != 1 ||
				At(OP_ASS) || At(OP_LBRACE))
				ThrowSemanticError("invalid typedef declaration");
			model_.AddTypeAlias(current_scope_, declarator.name.segments[0], type);
			return false;
		}
		const bool function = model_.types.IsFunction(type);
		const bool function_definition = first && At(OP_LBRACE);
		const bool has_initializer = At(OP_ASS);
		if (function && has_initializer)
			ThrowSemanticError("function cannot have an initializer");
		if (specifiers.is_constexpr && !function && !has_initializer)
			ThrowSemanticError("constexpr variable requires an initializer");
		if (specifiers.is_constexpr && function_definition)
			ThrowSemanticError("empty constexpr function body is invalid");
		const bool definition = function ? function_definition :
			(!specifiers.is_extern || has_initializer);
		const EntityId entity = model_.Declare(current_scope_, declarator, type,
			specifiers, definition, function_definition, unit_);
		if (function_definition)
		{
			Expect(OP_LBRACE);
			Expect(OP_RBRACE);
			InitialValue value;
			model_.Define(entity, type, value, false, false, unit_);
			return true;
		}
		if (function || !definition) return false;
		InitialValue initial;
		bool initializer_constant = true;
		if (Match(OP_ASS))
		{
			const Expression expression = ParseExpression(declarator.resolved_owner);
			initial = model_.ConvertInitializer(&type, expression,
				&initializer_constant);
		}
		else
		{
			if (specifiers.is_constexpr)
				ThrowSemanticError("constexpr variable requires an initializer");
			if (model_.types.IsReference(type))
				ThrowSemanticError("reference requires an initializer");
			if (model_.types.IsConst(type))
				ThrowSemanticError("const object requires an initializer");
			model_.types.SizeOf(type);
		}
		if (specifiers.is_constexpr && !initializer_constant)
			ThrowSemanticError("constexpr initializer is not constant");
		const bool usable = specifiers.is_constexpr ||
			IsUsableIntegralConstant(type, initializer_constant) ||
			(model_.types.IsReference(type) && initializer_constant);
		model_.Define(entity, type, initial, initializer_constant, usable, unit_);
		return false;
	}

	void ParseSimpleOrFunctionDeclaration()
	{
		DeclarationSpecifiers specifiers;
		if (!ParseDeclarationSpecifiers(&specifiers))
			ThrowSyntaxError("expected declaration specifiers");
		bool first = true;
		while (true)
		{
			if (ParseDeclaredEntity(specifiers, first)) return;
			if (!Match(OP_COMMA)) break;
			first = false;
		}
		Expect(OP_SEMICOLON);
	}

	void ParseStaticAssert()
	{
		Expect(KW_STATIC_ASSERT);
		Expect(OP_LPAREN);
		const Expression expression = ParseExpression(current_scope_);
		Expect(OP_COMMA);
		if (!AtLiteral() || !input_.tokens[position_].literal_array)
			ThrowSemanticError("static_assert message is not a string literal");
		++position_;
		Expect(OP_RPAREN);
		Expect(OP_SEMICOLON);
		bool constant = false;
		const bool value = model_.ContextualBool(expression, &constant);
		if (!constant) ThrowSemanticError("static_assert is not constant");
		if (!value) ThrowSemanticError("static assertion failed");
	}

	void ParseDeclaration()
	{
		if (At(KW_NAMESPACE) || (At(KW_INLINE) && AtOffset(1, KW_NAMESPACE)))
		{
			ParseNamespaceDeclaration();
			return;
		}
		if (At(KW_USING))
		{
			ParseUsingDeclaration();
			return;
		}
		if (At(KW_STATIC_ASSERT))
		{
			ParseStaticAssert();
			return;
		}
		if (Match(OP_SEMICOLON)) return;
		ParseSimpleOrFunctionDeclaration();
	}

	const TokenBuffer& input_;
	Model& model_;
	std::size_t position_;
	ScopeId current_scope_;
	std::uint32_t unit_;
	std::vector<ScopeId> scope_stack_;
	std::vector<DeclaratorMemoSlot> declarator_memo_slots_;
	std::vector<DeclaratorMemoEntry> declarator_memo_entries_;
	std::uint32_t declarator_memo_generation_;
	std::size_t declarator_call_depth_;
	std::size_t memo_child_storage_bytes_;
	std::size_t active_declarator_stack_bytes_;
};

}

Token::Token(std::uint16_t kind_value)
	: kind(kind_value), name(0), literal_type(FT_INT), byte_offset(0),
	  byte_size(0), elements(0), literal_array(false), integer_literal(false)
{
}

void ParseTranslationUnit(const TokenBuffer& input, Model& model,
	ScopeId root, std::uint32_t unit)
{
	TranslationUnitParser parser(input, model, root, unit);
	parser.Parse();
}

}
}
