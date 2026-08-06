#include "pa12_semantic_detail.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

NodeId SemanticAnalyzer::FindChild(NodeId node, const char* tag) const
{
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, tag)) return child;
	}
	return kNoNode;
}

NodeId SemanticAnalyzer::FirstSemanticChild(NodeId node) const
{
	const std::uint32_t edge = arena_->FirstEdge(node);
	return edge == kNoEdge ? kNoNode : arena_->EdgeChild(edge);
}

std::string SemanticAnalyzer::PayloadSource(NodeId node) const
{
	return arena_->SemanticPayload(node);
}

NamePath SemanticAnalyzer::ParseNamePath(const std::string& spelling)
{
	NamePath result;
	std::size_t first = 0;
	result.global = spelling.size() >= 2 && spelling[0] == ':' &&
		spelling[1] == ':';
	if (result.global) first = 2;
	std::size_t count = 1;
	for (std::size_t scan = first; (scan = spelling.find("::", scan)) !=
		std::string::npos; scan += 2) ++count;
	result.Reserve(count);
	while (first < spelling.size())
	{
		const std::size_t separator = spelling.find("::", first);
		const std::size_t last = separator == std::string::npos ?
			spelling.size() : separator;
		if (last == first) throw std::runtime_error("invalid qualified name");
		result.Push(program_->names.InternRange(spelling, first, last - first));
		if (separator == std::string::npos) break;
		first = separator + 2;
	}
	return result;
}

LookupResult SemanticAnalyzer::LookupPath(ScopeId scope,
	const NamePath& path, LookupKind kind)
{
	if (!path.global && path.Size() > 1)
	{
		ScopeId carrier = kNoScope;
		for (ScopeId current = scope; current != kNoScope; )
		{
			const LookupResult direct = program_->LookupDirect(current, path[0],
				LOOKUP_SCOPE_CARRIER);
			if (!direct.Empty())
			{
				carrier = direct.name_space != kNoScope ? direct.name_space :
					program_->ScopeForType(direct.type);
				break;
			}
			current = current < scope_parents_.size() ?
				scope_parents_[current] : kNoScope;
		}
		if (carrier != kNoScope)
		{
			NamePath remainder;
			for (std::size_t i = 1; i < path.Size(); ++i)
				remainder.Push(path[i]);
			return program_->LookupQualified(carrier, remainder, kind);
		}
	}
	return program_->Lookup(scope, path, kind);
}

LookupResult SemanticAnalyzer::LookupSpelling(ScopeId scope,
	const std::string& spelling, LookupKind kind)
{
	if (spelling.find("::") == std::string::npos)
		return program_->LookupName(scope, program_->names.Intern(spelling), kind);
	return LookupPath(scope, ParseNamePath(spelling), kind);
}

ScopeId SemanticAnalyzer::ResolveScopeSpelling(ScopeId scope,
	const std::string& spelling)
{
	const LookupResult result =
		LookupSpelling(scope, spelling, LOOKUP_SCOPE_CARRIER);
	return result.name_space != kNoScope ? result.name_space :
		result.type != kNoType ? program_->ScopeForType(result.type) : kNoScope;
}

ScopeId SemanticAnalyzer::ResolveOwner(ScopeId scope, const NamePath& name)
{
	if (!name.global && name.Size() <= 1) return scope;
	NamePath owner = name;
	if (!owner.Empty()) owner.Pop();
	if (owner.Empty()) return owner.global ? program_->GlobalScope() : scope;
	const LookupResult result = LookupPath(scope, owner, LOOKUP_SCOPE_CARRIER);
	return result.name_space != kNoScope ? result.name_space :
		result.type != kNoType ? program_->ScopeForType(result.type) : kNoScope;
}

const std::string& SemanticAnalyzer::ScopePrefix(ScopeId scope)
{
	return program_->names.Get(ScopePrefixId(scope));
}

NameId SemanticAnalyzer::ScopePrefixId(ScopeId scope)
{
	const NameId deferred = std::numeric_limits<NameId>::max();
	if (scope >= scope_prefixes_.size() || scope_prefixes_[scope] != deferred)
		return scope < scope_prefixes_.size() ? scope_prefixes_[scope] : 0;
	scope_prefix_scratch_.clear();
	ScopeId current = scope;
	while (current != kNoScope && current < scope_prefixes_.size() &&
		scope_prefixes_[current] == deferred)
	{
		if (scope_prefix_segments_[current] != 0)
			scope_prefix_scratch_.push_back(scope_prefix_segments_[current]);
		current = scope_parents_[current];
	}
	std::string rendered = current != kNoScope &&
		current < scope_prefixes_.size() ?
		program_->names.Get(scope_prefixes_[current]) : std::string();
	for (std::size_t i = scope_prefix_scratch_.size(); i != 0; --i)
	{
		rendered += program_->names.Get(scope_prefix_scratch_[i - 1]);
		rendered += "::";
	}
	scope_prefixes_[scope] = program_->names.Intern(rendered);
	return scope_prefixes_[scope];
}

NameId SemanticAnalyzer::DisplayName(ScopeId owner, NameId name)
{
	// ScopePrefix may materialize and intern a deferred prefix, invalidating
	// references into the shared string table. Snapshot the terminal first.
	const std::string terminal = program_->names.Get(name);
	const std::string qualified = ScopePrefix(owner) + terminal;
	return program_->names.Intern(qualified);
}

ScopeId SemanticAnalyzer::NewScope(ScopeId parent, ScopeKind kind,
	NameId name, NameId prefix)
{
	const ScopeId scope = program_->NewScope(parent, kind, name);
	if (scope_prefixes_.size() <= scope)
	{
		scope_prefixes_.resize(static_cast<std::size_t>(scope) + 1, 0);
		scope_prefix_segments_.resize(static_cast<std::size_t>(scope) + 1, 0);
		scope_parents_.resize(static_cast<std::size_t>(scope) + 1, kNoScope);
	}
	scope_prefixes_[scope] = prefix;
	scope_parents_[scope] = parent;
	return scope;
}

bool SemanticAnalyzer::IsDeclaration(NodeId node) const
{
	return arena_->IsTag(node, "simple-declaration") ||
		arena_->IsTag(node, "function-definition") ||
		arena_->IsTag(node, "alias-declaration") ||
		arena_->IsTag(node, "using-declaration") ||
		arena_->IsTag(node, "using-directive") ||
		arena_->IsTag(node, "namespace-definition") ||
		arena_->IsTag(node, "namespace-alias-definition") ||
		arena_->IsTag(node, "template-declaration") ||
		arena_->IsTag(node, "class-specifier") ||
		arena_->IsTag(node, "class-forward-declaration") ||
		arena_->IsTag(node, "enum-specifier") ||
		arena_->IsTag(node, "empty-declaration") ||
		arena_->IsTag(node, "linkage-specification");
}

std::uint32_t SemanticAnalyzer::MakeDump(DumpKind kind, TypeId type,
	ValueCategory category, NameId text, BindingId binding)
{
	const std::uint32_t node = dump_.Make(kind);
	DumpNode& record = dump_.nodes[node];
	record.type = type;
	record.category = category;
	record.text = text;
	record.binding = binding;
	return node;
}

std::size_t SemanticAnalyzer::SideStorageBytes() const
{
	std::size_t bytes =
		scope_prefixes_.capacity() * sizeof(NameId) +
		scope_prefix_segments_.capacity() * sizeof(NameId) +
		scope_parents_.capacity() * sizeof(ScopeId) +
		scope_prefix_scratch_.capacity() * sizeof(NameId) +
		function_sets_.StorageBytes() +
		function_declarations_.StorageBytes() +
		function_fact_by_binding_.capacity() * sizeof(std::uint32_t) +
		functions_.capacity() * sizeof(FunctionInfo) +
		entity_data_members_.capacity() * sizeof(std::vector<BindingId>) +
		function_templates_.capacity() * sizeof(FunctionTemplatePattern) +
		template_function_sets_.StorageBytes() +
		template_instantiations_.StorageBytes() +
		injected_fact_by_binding_.capacity() * sizeof(std::uint32_t) +
		injected_members_.capacity() * sizeof(InjectedMemberInfo) +
		demanded_default_constructor_entities_.capacity() * sizeof(EntityId) +
		default_constructor_demand_states_.capacity() * sizeof(std::uint8_t) +
		demanded_functions_.capacity() * sizeof(BindingId);
	for (std::size_t i = 0; i < functions_.size(); ++i)
		bytes += functions_[i].parameters.capacity() * sizeof(ParameterInfo);
	for (std::size_t i = 0; i < entity_data_members_.size(); ++i)
		bytes += entity_data_members_[i].capacity() * sizeof(BindingId);
	for (std::size_t i = 0; i < function_templates_.size(); ++i)
		bytes += function_templates_[i].type_parameters.capacity() *
			sizeof(NameId);
	return bytes;
}

TypeId SemanticAnalyzer::EffectiveType(TypeId type) const
{
	const TypeRecord record = program_->types.Get(type);
	return record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE ? record.child : type;
}

bool SemanticAnalyzer::IsVoid(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	return record.kind == TYPE_FUNDAMENTAL &&
		record.fundamental == FUND_VOID;
}

bool SemanticAnalyzer::IsNullptr(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	return record.kind == TYPE_FUNDAMENTAL &&
		record.fundamental == FUND_NULLPTR_T;
}

bool SemanticAnalyzer::IsConst(TypeId type) const
{
	type = EffectiveType(type);
	const TypeRecord record = program_->types.Get(type);
	return record.kind == TYPE_QUALIFIED && (record.cv & CV_CONST) != 0;
}

FundamentalKind SemanticAnalyzer::FundamentalOf(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	if (record.kind != TYPE_FUNDAMENTAL)
		throw std::logic_error("fundamental kind requested for non-fundamental");
	return record.fundamental;
}

bool SemanticAnalyzer::IsIntegral(TypeId type, bool allow_scoped_enum) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	if (record.kind == TYPE_FUNDAMENTAL)
		return record.fundamental != FUND_VOID &&
			record.fundamental != FUND_NULLPTR_T &&
			record.fundamental != FUND_FLOAT &&
			record.fundamental != FUND_DOUBLE &&
			record.fundamental != FUND_LONG_DOUBLE;
	if (record.kind != TYPE_NAMED) return false;
	const NamedFlavor flavor = program_->entities[record.entity].flavor;
	return flavor == NAMED_ENUM || (allow_scoped_enum &&
		flavor == NAMED_ENUM_CLASS);
}

bool SemanticAnalyzer::IsFloating(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	return record.kind == TYPE_FUNDAMENTAL &&
		(record.fundamental == FUND_FLOAT ||
		 record.fundamental == FUND_DOUBLE ||
		 record.fundamental == FUND_LONG_DOUBLE);
}

bool SemanticAnalyzer::IsArithmetic(TypeId type) const
{
	return IsIntegral(type) || IsFloating(type);
}

bool SemanticAnalyzer::IsPointer(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	return program_->types.Get(type).kind == TYPE_POINTER;
}

int SemanticAnalyzer::IntegralRank(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	if (record.kind == TYPE_NAMED)
	{
		const EntityRecord& entity = program_->entities[record.entity];
		return entity.underlying == kNoType ? 3 : IntegralRank(entity.underlying);
	}
	switch (record.fundamental)
	{
	case FUND_BOOL: return 0;
	case FUND_CHAR: case FUND_SIGNED_CHAR: case FUND_UNSIGNED_CHAR: return 1;
	case FUND_SHORT_INT: case FUND_UNSIGNED_SHORT_INT:
	case FUND_CHAR16_T: return 2;
	case FUND_INT: case FUND_UNSIGNED_INT: case FUND_WCHAR_T:
	case FUND_CHAR32_T: return 3;
	case FUND_LONG_INT: case FUND_UNSIGNED_LONG_INT: return 4;
	case FUND_LONG_LONG_INT: case FUND_UNSIGNED_LONG_LONG_INT: return 5;
	default: return -1;
	}
}

TypeId SemanticAnalyzer::IntegralPromotionType(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	if (record.kind == TYPE_NAMED)
	{
		const EntityRecord& entity = program_->entities[record.entity];
		if (entity.flavor == NAMED_ENUM_CLASS) return type;
		type = entity.underlying;
	}
	if (IntegralRank(type) < 3)
		return program_->types.Fundamental(FUND_INT);
	return type;
}

TypeId SemanticAnalyzer::CommonArithmeticType(TypeId left, TypeId right) const
{
	left = program_->types.RemoveTopCv(EffectiveType(left));
	right = program_->types.RemoveTopCv(EffectiveType(right));
	if (IsFloating(left) || IsFloating(right))
	{
		if ((IsFloating(left) && FundamentalOf(left) == FUND_LONG_DOUBLE) ||
			(IsFloating(right) && FundamentalOf(right) == FUND_LONG_DOUBLE))
			return program_->types.Fundamental(FUND_LONG_DOUBLE);
		if ((IsFloating(left) && FundamentalOf(left) == FUND_DOUBLE) ||
			(IsFloating(right) && FundamentalOf(right) == FUND_DOUBLE))
			return program_->types.Fundamental(FUND_DOUBLE);
		return program_->types.Fundamental(FUND_FLOAT);
	}
	left = IntegralPromotionType(left);
	right = IntegralPromotionType(right);
	const int left_rank = IntegralRank(left);
	const int right_rank = IntegralRank(right);
	if (left_rank > right_rank) return left;
	if (right_rank > left_rank) return right;
	const FundamentalKind lk = FundamentalOf(left);
	const FundamentalKind rk = FundamentalOf(right);
	const bool left_unsigned = lk == FUND_UNSIGNED_INT ||
		lk == FUND_UNSIGNED_LONG_INT || lk == FUND_UNSIGNED_LONG_LONG_INT;
	const bool right_unsigned = rk == FUND_UNSIGNED_INT ||
		rk == FUND_UNSIGNED_LONG_INT || rk == FUND_UNSIGNED_LONG_LONG_INT;
	return right_unsigned && !left_unsigned ? right : left;
}

TypeId SemanticAnalyzer::Decay(TypeId type) const
{
	type = EffectiveType(type);
	const TypeRecord record = program_->types.Get(type);
	if (record.kind == TYPE_ARRAY) return program_->types.Pointer(record.child);
	if (record.kind == TYPE_FUNCTION) return program_->types.Pointer(type);
	return program_->types.RemoveTopCv(type);
}

TypeId SemanticAnalyzer::AdjustParameterType(TypeId type)
{
	const TypeRecord record = program_->types.Get(type);
	if (record.kind == TYPE_ARRAY) return program_->types.Pointer(record.child);
	if (record.kind == TYPE_FUNCTION) return program_->types.Pointer(type);
	return program_->types.RemoveTopCv(type);
}

bool SemanticAnalyzer::SimilarUnqualified(TypeId source, TypeId target) const
{
	source = program_->types.RemoveTopCv(source);
	target = program_->types.RemoveTopCv(target);
	if (source == target) return true;
	const TypeRecord a = program_->types.Get(source);
	const TypeRecord b = program_->types.Get(target);
	if (a.kind != b.kind) return false;
	switch (a.kind)
	{
	case TYPE_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
		return SimilarUnqualified(a.child, b.child);
	case TYPE_ARRAY:
		return a.bound == b.bound && SimilarUnqualified(a.child, b.child);
	case TYPE_FUNCTION:
		if (a.parameter_count != b.parameter_count || a.variadic != b.variadic ||
			!SimilarUnqualified(a.child, b.child)) return false;
		for (std::size_t i = 0; i < a.parameter_count; ++i)
			if (!SimilarUnqualified(program_->types.Parameters(source)[i],
				program_->types.Parameters(target)[i])) return false;
		return true;
	default: return false;
	}
}

bool SemanticAnalyzer::QualificationConversion(TypeId source,
	TypeId target) const
{
	std::vector<std::uint8_t> source_cv;
	std::vector<std::uint8_t> target_cv;
	TypeId a = source;
	TypeId b = target;
	while (true)
	{
		const TypeRecord ar = program_->types.Get(a);
		const TypeRecord br = program_->types.Get(b);
		if (ar.kind != TYPE_POINTER || br.kind != TYPE_POINTER) break;
		a = ar.child;
		b = br.child;
		std::uint8_t acv = CV_NONE;
		std::uint8_t bcv = CV_NONE;
		if (program_->types.Get(a).kind == TYPE_QUALIFIED)
		{
			acv = program_->types.Get(a).cv;
			a = program_->types.Get(a).child;
		}
		if (program_->types.Get(b).kind == TYPE_QUALIFIED)
		{
			bcv = program_->types.Get(b).cv;
			b = program_->types.Get(b).child;
		}
		source_cv.push_back(acv);
		target_cv.push_back(bcv);
	}
	if (!SimilarUnqualified(a, b) || source_cv.size() != target_cv.size())
		return false;
	for (std::size_t i = 0; i < source_cv.size(); ++i)
	{
		if ((source_cv[i] & ~target_cv[i]) != 0) return false;
		if (i > 0 && source_cv[i] != target_cv[i])
			for (std::size_t j = 0; j < i; ++j)
				if ((target_cv[j] & CV_CONST) == 0) return false;
	}
	return true;
}

ConversionRank SemanticAnalyzer::Conversion(TypeId source,
	ValueCategory category, bool integer_zero, TypeId target) const
{
	++conversion_checks_;
	const TypeRecord target_record = program_->types.Get(target);
	if (target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE)
	{
		const bool lvalue_reference =
			target_record.kind == TYPE_LVALUE_REFERENCE;
		if (lvalue_reference && category != VALUE_LVALUE &&
			!IsConst(target_record.child)) return CONVERSION_INVALID;
		TypeId from = program_->types.RemoveTopCv(EffectiveType(source));
		TypeId to = program_->types.RemoveTopCv(target_record.child);
		if (from == to)
		{
			if (!lvalue_reference && category == VALUE_LVALUE)
				return CONVERSION_INVALID;
			const TypeRecord source_top = program_->types.Get(EffectiveType(source));
			const TypeRecord target_top = program_->types.Get(target_record.child);
			const std::uint8_t source_cv = source_top.kind == TYPE_QUALIFIED ?
				source_top.cv : CV_NONE;
			const std::uint8_t target_cv = target_top.kind == TYPE_QUALIFIED ?
				target_top.cv : CV_NONE;
			if ((source_cv & ~target_cv) != 0) return CONVERSION_INVALID;
			return lvalue_reference && category != VALUE_LVALUE ?
				CONVERSION_STANDARD : CONVERSION_EXACT;
		}
		if (QualificationConversion(EffectiveType(source), target_record.child))
			return !lvalue_reference && category == VALUE_LVALUE ?
				CONVERSION_INVALID : CONVERSION_EXACT;
		if (IsArithmetic(from) && IsArithmetic(to) &&
			(IsConst(target_record.child) || !lvalue_reference))
			return lvalue_reference ? CONVERSION_BOOLEAN : CONVERSION_STANDARD;
		return CONVERSION_INVALID;
	}

	TypeId from = Decay(source);
	TypeId to = program_->types.RemoveTopCv(target);
	if (from == to) return CONVERSION_EXACT;
	if (IsNullptr(to) && integer_zero) return CONVERSION_STANDARD;
	if (IsPointer(to) && (IsNullptr(from) || integer_zero))
		return CONVERSION_STANDARD;
	if (IsPointer(from) && IsPointer(to))
	{
		const TypeRecord source_pointer = program_->types.Get(from);
		const TypeRecord target_pointer = program_->types.Get(to);
		TypeId target_pointee = program_->types.RemoveTopCv(target_pointer.child);
		const TypeRecord pointee = program_->types.Get(target_pointee);
		if (pointee.kind == TYPE_FUNDAMENTAL &&
			pointee.fundamental == FUND_VOID)
		{
			const TypeRecord source_cv = program_->types.Get(source_pointer.child);
			const TypeRecord target_cv = program_->types.Get(target_pointer.child);
			const std::uint8_t scv = source_cv.kind == TYPE_QUALIFIED ?
				source_cv.cv : CV_NONE;
			const std::uint8_t tcv = target_cv.kind == TYPE_QUALIFIED ?
				target_cv.cv : CV_NONE;
			return (scv & ~tcv) == 0 ? CONVERSION_STANDARD : CONVERSION_INVALID;
		}
		return QualificationConversion(from, to) ?
			CONVERSION_STANDARD : CONVERSION_INVALID;
	}
	if (IsPointer(from) && to == program_->types.Fundamental(FUND_BOOL))
		return CONVERSION_BOOLEAN;
	if (IsArithmetic(from) && IsArithmetic(to))
	{
		if (IsIntegral(from) && IsIntegral(to) &&
			IntegralPromotionType(from) == to)
			return CONVERSION_PROMOTION;
		return CONVERSION_STANDARD;
	}
	return CONVERSION_INVALID;
}

ConversionRank SemanticAnalyzer::Conversion(const ExpressionInfo& source,
	TypeId target) const
{
	return Conversion(source.type, source.category,
		source.integer_literal_zero, target);
}

ExpressionInfo SemanticAnalyzer::ApplyTarget(ExpressionInfo value,
	TypeId target)
{
	if (target == kNoType)
	{
		RecordExpressionFacts(value);
		return value;
	}
	if (Conversion(value, target) == CONVERSION_INVALID)
		throw std::runtime_error("invalid standard conversion from " +
			program_->RenderType(value.type) + " to " +
			program_->RenderType(target));
	const TypeRecord target_record = program_->types.Get(target);
	const TypeId nonreference = target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE ? target_record.child : target;
	if (value.integer_literal_zero &&
		(IsPointer(nonreference) || IsNullptr(nonreference)))
	{
		value.type = program_->types.RemoveTopCv(nonreference);
		dump_.nodes[value.node].type = value.type;
	}
	if ((target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE) &&
		program_->types.RemoveTopCv(EffectiveType(value.type)) !=
			program_->types.RemoveTopCv(target_record.child) &&
		IsArithmetic(value.type) && IsArithmetic(target_record.child))
	{
		const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION,
			target_record.child, VALUE_PRVALUE);
		dump_.Add(cast, value.node);
		value.node = cast;
		value.type = target_record.child;
		value.category = VALUE_PRVALUE;
		++expression_count_;
	}
	RecordExpressionFacts(value);
	return value;
}

void SemanticAnalyzer::RecordExpressionFacts(const ExpressionInfo& value)
{
	if (value.node == kNoDumpEdge) return;
	DumpNode& node = dump_.nodes[value.node];
	node.constant = value.constant;
	node.constant_value = value.value;
}

bool SemanticAnalyzer::IsNonthrowing(NodeId declarator, ScopeId scope)
{
	const NodeId qualifier = FindChild(declarator, "function-qualifier");
	if (qualifier == kNoNode) return false;
	const std::string spelling = PayloadSource(qualifier);
	if (spelling == "noexcept" || spelling == "throw()") return true;
	if (spelling.compare(0, 8, "noexcept") != 0) return false;
	const NodeId expression_node = FirstSemanticChild(qualifier);
	if (expression_node == kNoNode)
		throw std::logic_error("missing noexcept expression");
	const ExpressionInfo expression = AnalyzeExpression(expression_node, scope);
	if (!expression.constant)
		throw std::runtime_error("nonconstant noexcept expression");
	return expression.value != 0;
}

bool SemanticAnalyzer::IsModifiableLvalue(const ExpressionInfo& value) const
{
	return value.category == VALUE_LVALUE && !IsConst(value.type) &&
		!program_->types.IsFunction(EffectiveType(value.type)) &&
		!IsVoid(value.type);
}

std::int64_t SemanticAnalyzer::ParseInteger(const std::string& spelling) const
{
	const std::size_t quote = spelling.find('\'');
	if (quote != std::string::npos)
	{
		const std::size_t close = spelling.rfind('\'');
		if (close == quote || close + 1 != spelling.size())
			throw std::runtime_error("invalid character literal");
		unsigned long long value = 0;
		std::size_t count = 0;
		for (std::size_t i = quote + 1; i < close; ++i)
		{
			unsigned int character = static_cast<unsigned char>(spelling[i]);
			if (spelling[i] == '\\')
			{
				if (++i >= close)
					throw std::runtime_error("invalid character escape");
				const char escaped = spelling[i];
				if (escaped == 'a') character = 7;
				else if (escaped == 'b') character = 8;
				else if (escaped == 'f') character = 12;
				else if (escaped == 'n') character = 10;
				else if (escaped == 'r') character = 13;
				else if (escaped == 't') character = 9;
				else if (escaped == 'v') character = 11;
				else if (escaped == 'x')
				{
					character = 0;
					std::size_t digits = 0;
					while (i + 1 < close)
					{
						const char digit = spelling[i + 1];
						const int nibble = digit >= '0' && digit <= '9' ? digit - '0' :
							digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 :
							digit >= 'A' && digit <= 'F' ? digit - 'A' + 10 : -1;
						if (nibble < 0) break;
						character = (character << 4) | static_cast<unsigned int>(nibble);
						++i;
						++digits;
					}
					if (digits == 0)
						throw std::runtime_error("empty hexadecimal character escape");
				}
				else if (escaped >= '0' && escaped <= '7')
				{
					character = static_cast<unsigned int>(escaped - '0');
					for (std::size_t digits = 1; digits < 3 && i + 1 < close &&
						spelling[i + 1] >= '0' && spelling[i + 1] <= '7'; ++digits)
					{
						character = (character << 3) |
							static_cast<unsigned int>(spelling[++i] - '0');
					}
				}
				else character = static_cast<unsigned char>(escaped);
			}
			value = (value << 8) | (character & 0xffU);
			++count;
		}
		if (count == 0 || value > static_cast<unsigned long long>(INT64_MAX))
			throw std::runtime_error("character literal outside PA12 range");
		return static_cast<std::int64_t>(value);
	}
	std::size_t last = spelling.size();
	while (last != 0 && (spelling[last - 1] == 'u' ||
		spelling[last - 1] == 'U' || spelling[last - 1] == 'l' ||
		spelling[last - 1] == 'L')) --last;
	const std::string digits = spelling.substr(0, last);
	errno = 0;
	char* end = 0;
	const unsigned long long value = std::strtoull(digits.c_str(), &end, 0);
	if (errno == ERANGE || end == digits.c_str() || *end != '\0' ||
		value > static_cast<unsigned long long>(INT64_MAX))
		throw std::runtime_error("integer literal outside PA12 range");
	return static_cast<std::int64_t>(value);
}

NameId SemanticAnalyzer::InternNumber(std::int64_t value)
{
	return program_->names.Intern(std::to_string(value));
}

std::int64_t SemanticAnalyzer::ApplyConstantBinary(
	const std::string& operation, std::int64_t left, std::int64_t right) const
{
	if (operation == "+") return left + right;
	if (operation == "-") return left - right;
	if (operation == "*") return left * right;
	if (operation == "/")
	{
		if (right == 0) throw std::runtime_error("division by zero");
		return left / right;
	}
	if (operation == "%")
	{
		if (right == 0) throw std::runtime_error("division by zero");
		return left % right;
	}
	if (operation == "<<") return left << right;
	if (operation == ">>") return left >> right;
	if (operation == "&") return left & right;
	if (operation == "|") return left | right;
	if (operation == "^") return left ^ right;
	if (operation == "==") return left == right;
	if (operation == "!=") return left != right;
	if (operation == "<") return left < right;
	if (operation == ">") return left > right;
	if (operation == "<=") return left <= right;
	if (operation == ">=") return left >= right;
	if (operation == "&&") return left && right;
	if (operation == "||") return left || right;
	if (operation == ",") return right;
	throw std::runtime_error("unsupported constant binary operator");
}

ExpressionInfo SemanticAnalyzer::MakeLiteral(TypeId type, NameId text,
	ValueCategory category)
{
	ExpressionInfo result;
	result.type = type;
	result.category = category;
	result.node = MakeDump(DUMP_LITERAL, type, category, text);
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeExpression(NodeId node, ScopeId scope,
	TypeId target)
{
	if (node == kNoNode) throw std::runtime_error("missing expression");
	if (arena_->IsTag(node, "parenthesized-expression"))
		return AnalyzeExpression(FirstSemanticChild(node), scope, target);
	if (arena_->IsTag(node, "literal"))
	{
		const std::string spelling = arena_->Payload(node);
		ExpressionInfo result;
		if (!spelling.empty() && spelling[0] == '"')
		{
			std::size_t count = 1;
			for (std::size_t i = 1; i + 1 < spelling.size(); ++i)
			{
				if (spelling[i] == '\\' && i + 2 < spelling.size())
				{
					++i;
					if (spelling[i] == 'x')
						while (i + 2 < spelling.size() &&
							((spelling[i + 1] >= '0' && spelling[i + 1] <= '9') ||
							 (spelling[i + 1] >= 'a' && spelling[i + 1] <= 'f') ||
							 (spelling[i + 1] >= 'A' && spelling[i + 1] <= 'F'))) ++i;
					else if (spelling[i] >= '0' && spelling[i] <= '7')
						for (int digits = 1; digits < 3 && i + 2 < spelling.size() &&
							spelling[i + 1] >= '0' && spelling[i + 1] <= '7'; ++digits) ++i;
				}
				++count;
			}
			const TypeId element = program_->types.Qualify(
				program_->types.Fundamental(FUND_CHAR), CV_CONST);
			result = MakeLiteral(program_->types.Array(element, count),
				program_->names.Intern(spelling), VALUE_LVALUE);
		}
		else if (spelling.find('.') != std::string::npos ||
			spelling.find('p') != std::string::npos ||
			spelling.find('P') != std::string::npos ||
			((spelling.size() < 2 || spelling[0] != '0' ||
			  (spelling[1] != 'x' && spelling[1] != 'X')) &&
			 (spelling.find('e') != std::string::npos ||
			  spelling.find('E') != std::string::npos)))
		{
			const char suffix = spelling.empty() ? 0 : spelling[spelling.size() - 1];
			const FundamentalKind kind = suffix == 'f' || suffix == 'F' ?
				FUND_FLOAT : suffix == 'l' || suffix == 'L' ?
				FUND_LONG_DOUBLE : FUND_DOUBLE;
			result = MakeLiteral(program_->types.Fundamental(kind),
				program_->names.Intern(spelling));
		}
		else
		{
			const std::int64_t value = ParseInteger(spelling);
			const bool has_u = spelling.find('u') != std::string::npos ||
				spelling.find('U') != std::string::npos;
			std::size_t ls = 0;
			for (std::size_t i = 0; i < spelling.size(); ++i)
				if (spelling[i] == 'l' || spelling[i] == 'L') ++ls;
			const FundamentalKind kind = ls > 1 ?
				(has_u ? FUND_UNSIGNED_LONG_LONG_INT : FUND_LONG_LONG_INT) :
				ls == 1 ? (has_u ? FUND_UNSIGNED_LONG_INT : FUND_LONG_INT) :
				has_u ? FUND_UNSIGNED_INT : FUND_INT;
			result = MakeLiteral(program_->types.Fundamental(kind),
				program_->names.Intern(spelling));
			result.constant = true;
			result.value = value;
			result.integer_literal_zero = value == 0;
		}
		return ApplyTarget(result, target);
	}
	if (arena_->IsTag(node, "keyword-literal"))
	{
		const std::string spelling = PayloadSource(node);
		ExpressionInfo result;
		if (spelling == "nullptr")
		{
			result = MakeLiteral(program_->types.Fundamental(FUND_NULLPTR_T),
				program_->names.Intern(arena_->Payload(node)));
			result.constant = true;
			result.value = 0;
		}
		else if (spelling == "true" || spelling == "false")
		{
			result = MakeLiteral(program_->types.Fundamental(FUND_BOOL),
				program_->names.Intern(arena_->Payload(node)));
			result.constant = true;
			result.value = spelling == "true";
		}
		else throw std::runtime_error("unsupported keyword literal");
		return ApplyTarget(result, target);
	}
	if (arena_->IsTag(node, "id-expression"))
	{
		const std::string spelling = arena_->Payload(node);
		std::vector<BindingId> candidates = FunctionCandidates(scope, spelling);
		if (!candidates.empty())
		{
			BindingId selected = kNoBinding;
			TypeId desired = target;
			if (desired != kNoType)
			{
				desired = program_->types.RemoveTopCv(desired);
				const TypeRecord target_record = program_->types.Get(desired);
				if (target_record.kind == TYPE_LVALUE_REFERENCE ||
					target_record.kind == TYPE_RVALUE_REFERENCE)
					desired = target_record.child;
				if (program_->types.Get(desired).kind == TYPE_POINTER)
					desired = program_->types.Get(desired).child;
				else if (program_->types.Get(desired).kind == TYPE_MEMBER_POINTER)
					desired = program_->types.Get(desired).child;
			}
			for (std::size_t i = 0; i < candidates.size(); ++i)
				if (desired == kNoType || GetFunction(candidates[i]).type == desired)
				{
					if (selected != kNoBinding && desired != kNoType)
						throw std::runtime_error("ambiguous overloaded function id");
					selected = candidates[i];
					if (desired == kNoType && candidates.size() != 1)
					{
						ExpressionInfo unresolved;
						unresolved.binding = candidates[0];
						return unresolved;
					}
				}
			if (selected == kNoBinding)
				throw std::runtime_error("no target-matching overloaded function");
			const FunctionInfo& function = GetFunction(selected);
			ExpressionInfo result;
			result.type = function.type;
			if (function.member_owner != kNoType)
			{
				const TypeRecord member_type = program_->types.Get(function.type);
				TypeId object = function.member_owner;
				if ((member_type.cv & CV_CONST) != 0)
					object = program_->types.Qualify(object, CV_CONST);
				if ((member_type.cv & CV_VOLATILE) != 0)
					object = program_->types.Qualify(object, CV_VOLATILE);
				std::vector<TypeId> parameters;
				parameters.push_back(program_->types.Pointer(object));
				const TypeId* explicit_parameters =
					program_->types.Parameters(function.type);
				for (std::size_t i = 0; i < member_type.parameter_count; ++i)
					parameters.push_back(explicit_parameters[i]);
				result.type = program_->types.Function(member_type.child,
					parameters, member_type.variadic);
			}
			result.category = VALUE_LVALUE;
			result.binding = selected;
			result.node = MakeDump(DUMP_ID_EXPRESSION, result.type,
				result.category, program_->names.Intern(spelling), selected);
			DemandFunction(selected);
			++expression_count_;
			return result;
		}
		const LookupResult found = LookupSpelling(scope, spelling, LOOKUP_ORDINARY);
		if (found.ordinary == kNoBinding)
			throw std::runtime_error("unknown expression name: " + spelling);
		const BindingRecord& binding = program_->bindings[found.ordinary];
		if (binding.kind == BIND_ENUMERATOR)
		{
			ExpressionInfo result = MakeLiteral(binding.type,
				InternNumber(binding.value));
			result.constant = true;
			result.value = binding.value;
			return ApplyTarget(result, target);
		}
		if (binding.kind != BIND_VARIABLE && binding.kind != BIND_PARAMETER)
			throw std::runtime_error("name does not denote a value");
		if (binding.non_static_data_member)
			return AnalyzeImplicitDataMember(found.ordinary, scope, target);
		const std::uint32_t injected_fact =
			found.ordinary < injected_fact_by_binding_.size() ?
			injected_fact_by_binding_[found.ordinary] : kNoDumpEdge;
		if (injected_fact != kNoDumpEdge)
		{
			const InjectedMemberInfo& injected = injected_members_[injected_fact];
			const BindingRecord& storage =
				program_->bindings[injected.storage];
			const std::uint32_t storage_node = MakeDump(DUMP_ID_EXPRESSION,
				storage.type, VALUE_LVALUE, storage.name, injected.storage);
			const std::uint32_t member_node = MakeDump(DUMP_MEMBER_EXPRESSION,
				binding.type, VALUE_LVALUE, injected.member);
			dump_.Add(member_node, storage_node);
			ExpressionInfo result;
			result.node = member_node;
			result.type = binding.type;
			result.category = VALUE_LVALUE;
			expression_count_ += 2;
			return ApplyTarget(result, target);
		}
		ExpressionInfo result;
		result.type = EffectiveType(binding.type);
		result.category = VALUE_LVALUE;
		result.binding = found.ordinary;
		result.node = MakeDump(DUMP_ID_EXPRESSION, result.type,
			result.category, program_->names.Intern(spelling), found.ordinary);
		result.constant = binding.constant;
		result.value = binding.value;
		++expression_count_;
		return ApplyTarget(result, target);
	}
	if (arena_->IsTag(node, "call-expression"))
		return AnalyzeCall(node, scope, target);
	if (arena_->IsTag(node, "unary-expression") ||
		arena_->IsTag(node, "postfix-expression"))
		return ApplyTarget(AnalyzeUnary(node, scope, target), target);
	if (arena_->IsTag(node, "binary-expression"))
		return ApplyTarget(AnalyzeBinary(node, scope), target);
	if (arena_->IsTag(node, "assignment-expression"))
		return ApplyTarget(AnalyzeAssignment(node, scope), target);
	if (arena_->IsTag(node, "cast-expression"))
		return ApplyTarget(AnalyzeCast(node, scope), target);
	if (arena_->IsTag(node, "conditional-expression"))
		return ApplyTarget(AnalyzeConditional(node, scope), target);
	if (arena_->IsTag(node, "subscript-expression"))
		return ApplyTarget(AnalyzeSubscript(node, scope), target);
	if (arena_->IsTag(node, "sizeof-expression"))
		return ApplyTarget(AnalyzeSizeof(node, scope), target);
	if (arena_->IsTag(node, "braced-init-list"))
		return AnalyzeBracedInit(node, scope, target);
	if (arena_->IsTag(node, "member-expression"))
		return ApplyTarget(AnalyzeMember(node, scope), target);
	throw std::runtime_error("unsupported PA12 expression: " + arena_->Tag(node));
}

BindingId SemanticAnalyzer::SelectOverload(ScopeId scope,
	const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>& arguments,
	const std::vector<BindingId>& candidates)
{
	if (!argument_syntax.empty() && candidates.size() >
		std::numeric_limits<std::size_t>::max() / argument_syntax.size())
		throw std::runtime_error("overload conversion table is too large");
	const std::size_t arity = argument_syntax.size();
	std::vector<ConversionRank> ranks(candidates.size() * arity,
		CONVERSION_ELLIPSIS);
	std::vector<bool> viable(candidates.size(), true);
	for (std::size_t c = 0; c < candidates.size(); ++c)
	{
		++overload_candidates_;
		const FunctionInfo& function = GetFunction(candidates[c]);
		const TypeRecord function_type = program_->types.Get(function.type);
		std::size_t required_parameters = function_type.parameter_count;
		while (required_parameters != 0 &&
			required_parameters <= function.parameters.size() &&
			function.parameters[required_parameters - 1].default_argument != kNoNode)
			--required_parameters;
		if (argument_syntax.size() < required_parameters ||
			(!function_type.variadic &&
			 argument_syntax.size() > function_type.parameter_count))
		{
			viable[c] = false;
			continue;
		}
		const TypeId* parameter_data = program_->types.Parameters(function.type);
		std::vector<TypeId> parameters;
		if (function_type.parameter_count != 0)
			parameters.assign(parameter_data,
				parameter_data + function_type.parameter_count);
		for (std::size_t a = 0; a < argument_syntax.size(); ++a)
		{
			ConversionRank rank = CONVERSION_ELLIPSIS;
			if (a < function_type.parameter_count)
			{
				if (arguments[a].type != kNoType)
					rank = Conversion(arguments[a], parameters[a]);
				else
				{
					const std::vector<BindingId> argument_functions =
						FunctionCandidates(scope,
							arena_->Payload(argument_syntax[a]));
					TypeId desired = program_->types.RemoveTopCv(parameters[a]);
					if (program_->types.Get(desired).kind == TYPE_POINTER)
						desired = program_->types.Get(desired).child;
					std::size_t matches = 0;
					for (std::size_t f = 0; f < argument_functions.size(); ++f)
						if (GetFunction(argument_functions[f]).type == desired)
							++matches;
					rank = matches == 1 ?
						CONVERSION_EXACT : CONVERSION_INVALID;
				}
			}
			ranks[c * arity + a] = rank;
			if (rank == CONVERSION_INVALID) viable[c] = false;
		}
	}
	const auto better = [this, &ranks, &candidates, arity](
		std::size_t left, std::size_t right) -> bool
	{
		++overload_order_comparisons_;
		bool no_worse = true;
		bool strictly_better = false;
		for (std::size_t a = 0; a < arity; ++a)
		{
			if (ranks[left * arity + a] > ranks[right * arity + a])
				no_worse = false;
			if (ranks[left * arity + a] < ranks[right * arity + a])
				strictly_better = true;
		}
		if (!no_worse) return false;
		if (strictly_better) return true;
		const FunctionInfo& left_function = GetFunction(candidates[left]);
		const FunctionInfo& right_function = GetFunction(candidates[right]);
		return !left_function.template_specialization &&
			right_function.template_specialization;
	};

	std::size_t viable_count = 0;
	std::size_t champion = candidates.size();
	for (std::size_t c = 0; c < candidates.size(); ++c)
	{
		if (!viable[c]) continue;
		++viable_count;
		if (champion == candidates.size())
		{
			champion = c;
			continue;
		}
		if (better(c, champion)) champion = c;
	}
	if (viable_count == 0) throw std::runtime_error("no viable overload");
	if (viable_count == 1) return candidates[champion];
	for (std::size_t other = 0; other < candidates.size(); ++other)
	{
		if (other == champion || !viable[other]) continue;
		if (!better(champion, other))
			throw std::runtime_error("ambiguous overload");
	}
	return candidates[champion];
}

ExpressionInfo SemanticAnalyzer::AnalyzeCall(NodeId node, ScopeId scope,
	TypeId target)
{
	const NodeId callee_syntax = FirstSemanticChild(node);
	if (callee_syntax == kNoNode) throw std::runtime_error("call without callee");
	NodeId arguments_node = kNoNode;
	std::uint32_t edge = arena_->FirstEdge(node);
	if (edge != kNoEdge) edge = arena_->NextEdge(edge);
	if (edge != kNoEdge) arguments_node = arena_->EdgeChild(edge);
	std::vector<NodeId> argument_syntax;
	if (arguments_node != kNoNode)
		for (std::uint32_t argument = arena_->FirstEdge(arguments_node);
			argument != kNoEdge; argument = arena_->NextEdge(argument))
			argument_syntax.push_back(arena_->EdgeChild(argument));

	if (arena_->IsTag(callee_syntax, "id-expression"))
	{
		const std::string spelling = arena_->Payload(callee_syntax);
		if (spelling == "__builtin_constant_p")
		{
			if (argument_syntax.size() != 1)
				throw std::runtime_error("invalid __builtin_constant_p call");
			const ExpressionInfo operand =
				AnalyzeExpression(argument_syntax[0], scope);
			ExpressionInfo result = MakeLiteral(
				program_->types.Fundamental(FUND_INT),
				program_->names.Intern(operand.constant ? "1" : "0"));
			result.constant = true;
			result.value = operand.constant ? 1 : 0;
			return ApplyTarget(result, target);
		}
		if (spelling == "__builtin_abort")
		{
			if (!argument_syntax.empty())
				throw std::runtime_error("invalid __builtin_abort call");
			const TypeId function_type = program_->types.Function(
				program_->types.Fundamental(FUND_VOID), std::vector<TypeId>(), false);
			const std::uint32_t call = MakeDump(DUMP_CALL_EXPRESSION,
				program_->types.Fundamental(FUND_VOID), VALUE_PRVALUE);
			const std::uint32_t callee = MakeDump(DUMP_CALLEE, function_type,
				VALUE_NONE, program_->names.Intern("__builtin_abort"));
			dump_.Add(call, callee);
			ExpressionInfo result;
			result.node = call;
			result.type = program_->types.Fundamental(FUND_VOID);
			++expression_count_;
			return ApplyTarget(result, target);
		}

		TypeId cast_type = kNoType;
		const LookupResult named = LookupSpelling(scope, spelling, LOOKUP_TYPE);
		if (named.type != kNoType) cast_type = named.type;
		else if (spelling.size() > 10 &&
			spelling.compare(0, 9, "decltype(") == 0 &&
			spelling[spelling.size() - 1] == ')')
		{
			const std::string operand_name =
				spelling.substr(9, spelling.size() - 10);
			const LookupResult operand = LookupSpelling(scope, operand_name,
				LOOKUP_ORDINARY);
			if (operand.ordinary != kNoBinding)
				cast_type = program_->bindings[operand.ordinary].type;
		}
		else
		{
			FundamentalKind kind = FUND_INT;
			bool fundamental = true;
			if (spelling == "bool") kind = FUND_BOOL;
			else if (spelling == "char") kind = FUND_CHAR;
			else if (spelling == "short" || spelling == "short int")
				kind = FUND_SHORT_INT;
			else if (spelling == "int") kind = FUND_INT;
			else if (spelling == "long" || spelling == "long int")
				kind = FUND_LONG_INT;
			else if (spelling == "unsigned" || spelling == "unsigned int")
				kind = FUND_UNSIGNED_INT;
			else if (spelling == "unsigned long") kind = FUND_UNSIGNED_LONG_INT;
			else if (spelling == "float") kind = FUND_FLOAT;
			else if (spelling == "double") kind = FUND_DOUBLE;
			else if (spelling == "long double") kind = FUND_LONG_DOUBLE;
			else fundamental = false;
			if (fundamental) cast_type = program_->types.Fundamental(kind);
		}
		if (cast_type != kNoType)
		{
			if (argument_syntax.size() > 1)
				throw std::runtime_error("too many functional cast arguments");
			if (argument_syntax.empty())
			{
				ExpressionInfo zero = MakeLiteral(cast_type,
					program_->names.Intern("0"));
				zero.constant = true;
				zero.value = 0;
				return ApplyTarget(zero, target);
			}
			ExpressionInfo operand = AnalyzeExpression(argument_syntax[0], scope);
			const TypeRecord cast_record = program_->types.Get(cast_type);
			const ValueCategory cast_category =
				cast_record.kind == TYPE_LVALUE_REFERENCE ? VALUE_LVALUE :
				cast_record.kind == TYPE_RVALUE_REFERENCE ? VALUE_XVALUE :
				VALUE_PRVALUE;
			const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION,
				cast_type, cast_category);
			dump_.Add(cast, operand.node);
			ExpressionInfo result;
			result.node = cast;
			result.type = cast_type;
			result.category = cast_category;
			result.constant = operand.constant;
			result.value = operand.value;
			++expression_count_;
			return ApplyTarget(result, target);
		}

		std::vector<BindingId> candidates = FunctionCandidates(scope, spelling);
		std::vector<ExpressionInfo> arguments;
		bool arguments_analyzed = false;
		if (!FindFunctionTemplates(scope, spelling).empty())
		{
			for (std::size_t i = 0; i < argument_syntax.size(); ++i)
				arguments.push_back(AnalyzeExpression(argument_syntax[i], scope));
			arguments_analyzed = true;
			DeduceFunctionTemplates(scope, spelling, arguments);
			candidates = FunctionCandidates(scope, spelling);
		}
		if (!candidates.empty())
		{
			if (!arguments_analyzed)
				for (std::size_t i = 0; i < argument_syntax.size(); ++i)
					arguments.push_back(
						AnalyzeExpression(argument_syntax[i], scope));
			const BindingId selected = SelectOverload(scope, argument_syntax,
				arguments, candidates);
			const FunctionInfo function = GetFunction(selected);
			const TypeRecord function_type = program_->types.Get(function.type);
			const TypeId* parameter_data = program_->types.Parameters(function.type);
			std::vector<TypeId> parameters;
			if (function_type.parameter_count != 0)
				parameters.assign(parameter_data,
					parameter_data + function_type.parameter_count);
			const TypeId result_type = function_type.child;
			const TypeRecord returned = program_->types.Get(result_type);
			const ValueCategory category = returned.kind == TYPE_LVALUE_REFERENCE ?
				VALUE_LVALUE : returned.kind == TYPE_RVALUE_REFERENCE ?
				VALUE_XVALUE : VALUE_PRVALUE;
			const std::uint32_t call = MakeDump(DUMP_CALL_EXPRESSION,
				result_type, category);
			const std::uint32_t callee = MakeDump(DUMP_CALLEE, function.type,
				VALUE_NONE, function.display_name, selected);
			dump_.Add(call, callee);
			for (std::size_t a = 0; a < arguments.size(); ++a)
			{
				ExpressionInfo argument = arguments[a];
				if (a < function_type.parameter_count)
				{
					if (argument.type == kNoType)
						argument = AnalyzeExpression(argument_syntax[a], scope,
							parameters[a]);
					else argument = ApplyTarget(argument, parameters[a]);
				}
				dump_.Add(call, argument.node);
			}
			for (std::size_t a = arguments.size();
				a < function_type.parameter_count; ++a)
			{
				if (a >= function.parameters.size() ||
					function.parameters[a].default_argument == kNoNode)
					throw std::runtime_error("missing default argument fact");
				const ExpressionInfo argument = AnalyzeExpression(
					function.parameters[a].default_argument,
					function.parameters[a].default_scope, parameters[a]);
				dump_.Add(call, argument.node);
			}
			ExpressionInfo result;
			result.node = call;
			result.type = result_type;
			result.category = category;
			result.binding = selected;
			DemandFunction(selected);
			++expression_count_;
			return ApplyTarget(result, target);
		}
	}

	ExpressionInfo callee = AnalyzeExpression(callee_syntax, scope);
	TypeId function_type = EffectiveType(callee.type);
	TypeRecord callable = program_->types.Get(function_type);
	if (callable.kind == TYPE_POINTER)
	{
		function_type = callable.child;
		callable = program_->types.Get(function_type);
	}
	if (callable.kind != TYPE_FUNCTION)
		throw std::runtime_error("called object is not callable");
	if (argument_syntax.size() < callable.parameter_count ||
		(!callable.variadic && argument_syntax.size() != callable.parameter_count))
		throw std::runtime_error("indirect call arity mismatch");
	const TypeId* parameter_data = program_->types.Parameters(function_type);
	std::vector<TypeId> parameters;
	if (callable.parameter_count != 0)
		parameters.assign(parameter_data,
			parameter_data + callable.parameter_count);
	const TypeId result_type = callable.child;
	const TypeRecord returned = program_->types.Get(result_type);
	const ValueCategory category = returned.kind == TYPE_LVALUE_REFERENCE ?
		VALUE_LVALUE : returned.kind == TYPE_RVALUE_REFERENCE ?
		VALUE_XVALUE : VALUE_PRVALUE;
	const std::uint32_t call = MakeDump(DUMP_CALL_EXPRESSION,
		result_type, category);
	dump_.Add(call, callee.node);
	for (std::size_t a = 0; a < argument_syntax.size(); ++a)
	{
		ExpressionInfo argument = AnalyzeExpression(argument_syntax[a], scope,
			a < callable.parameter_count ? parameters[a] : kNoType);
		dump_.Add(call, argument.node);
	}
	ExpressionInfo result;
	result.node = call;
	result.type = result_type;
	result.category = category;
	++expression_count_;
	return ApplyTarget(result, target);
}

ExpressionInfo SemanticAnalyzer::AnalyzeImplicitDataMember(BindingId member_binding,
	ScopeId scope, TypeId target)
{
	const BindingRecord& binding = program_->bindings[member_binding];
	const NameId this_name = program_->names.Intern("this");
	const LookupResult this_lookup =
		program_->LookupName(scope, this_name, LOOKUP_ORDINARY);
	if (this_lookup.ordinary == kNoBinding)
		throw std::runtime_error("non-static member requires an object");
	const BindingRecord& this_binding =
		program_->bindings[this_lookup.ordinary];
	const std::uint32_t object = MakeDump(DUMP_ID_EXPRESSION,
		this_binding.type, VALUE_LVALUE, this_name, this_lookup.ordinary);
	const std::uint32_t member = MakeDump(DUMP_MEMBER_EXPRESSION,
		binding.type, VALUE_LVALUE, binding.name, member_binding);
	dump_.Add(member, object);
	ExpressionInfo result;
	result.node = member;
	result.type = binding.type;
	result.category = VALUE_LVALUE;
	result.binding = member_binding;
	expression_count_ += 2;
	return ApplyTarget(result, target);
}

ExpressionInfo SemanticAnalyzer::AnalyzeUnary(NodeId node, ScopeId scope,
	TypeId target)
{
	const bool postfix = arena_->IsTag(node, "postfix-expression");
	const std::string operation = PayloadSource(node);
	TypeId operand_target = kNoType;
	if (operation == "&" && target != kNoType)
	{
		TypeId desired = program_->types.RemoveTopCv(target);
		const TypeRecord target_record = program_->types.Get(desired);
		if ((target_record.kind == TYPE_POINTER &&
			 program_->types.IsFunction(target_record.child)) ||
			target_record.kind == TYPE_MEMBER_POINTER)
			operand_target = target_record.child;
	}
	ExpressionInfo operand = AnalyzeExpression(FirstSemanticChild(node), scope,
		operand_target);
	TypeId result_type = EffectiveType(operand.type);
	ValueCategory category = VALUE_PRVALUE;
	bool constant = operand.constant;
	std::int64_t value = operand.value;
	if (operation == "&")
	{
		if (operand.category != VALUE_LVALUE)
			throw std::runtime_error("address-of requires lvalue");
		if (target != kNoType &&
			program_->types.Get(program_->types.RemoveTopCv(target)).kind ==
				TYPE_MEMBER_POINTER)
			result_type = program_->types.RemoveTopCv(target);
		else result_type = program_->types.Pointer(result_type);
		constant = false;
	}
	else if (operation == "*")
	{
		TypeId decayed = Decay(result_type);
		const TypeRecord pointer = program_->types.Get(decayed);
		if (pointer.kind != TYPE_POINTER)
			throw std::runtime_error("dereference requires pointer");
		result_type = pointer.child;
		category = VALUE_LVALUE;
		constant = false;
	}
	else if (operation == "++" || operation == "--")
	{
		if (!IsModifiableLvalue(operand) ||
			(!IsArithmetic(result_type) && !IsPointer(result_type)))
			throw std::runtime_error("invalid increment operand");
		category = postfix ? VALUE_PRVALUE : VALUE_LVALUE;
		constant = false;
	}
	else if (operation == "!")
	{
		if (!IsArithmetic(result_type) && !IsPointer(Decay(result_type)) &&
			!IsNullptr(result_type))
			throw std::runtime_error("invalid logical-not operand");
		result_type = program_->types.Fundamental(FUND_BOOL);
		if (constant) value = !value;
	}
	else if (operation == "+" || operation == "-" || operation == "~")
	{
		if (operation == "+" && IsPointer(Decay(result_type)))
		{
			result_type = Decay(result_type);
			constant = false;
		}
		else if ((operation == "~" && !IsIntegral(result_type)) ||
			(operation != "~" && !IsArithmetic(result_type)))
			throw std::runtime_error("invalid unary arithmetic operand");
		else if (IsIntegral(result_type) &&
			(IntegralRank(result_type) < 3 ||
			 program_->types.Get(program_->types.RemoveTopCv(result_type)).kind ==
				TYPE_NAMED))
			result_type = program_->types.Fundamental(FUND_INT);
		if (constant)
			value = operation == "-" ? -value : operation == "~" ? ~value : value;
	}
	else throw std::runtime_error("unsupported unary operator");
	const std::uint32_t expression = MakeDump(postfix ?
		DUMP_POSTFIX_EXPRESSION : DUMP_UNARY_EXPRESSION,
		result_type, category, program_->names.Intern(arena_->Payload(node)));
	dump_.Add(expression, operand.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = result_type;
	result.category = category;
	result.constant = constant;
	result.value = value;
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeBinary(NodeId node, ScopeId scope)
{
	const std::uint32_t first_edge = arena_->FirstEdge(node);
	if (first_edge == kNoEdge) throw std::runtime_error("empty binary expression");
	const std::uint32_t second_edge = arena_->NextEdge(first_edge);
	if (second_edge == kNoEdge) throw std::runtime_error("unary binary expression");
	ExpressionInfo left = AnalyzeExpression(arena_->EdgeChild(first_edge), scope);
	ExpressionInfo right = AnalyzeExpression(arena_->EdgeChild(second_edge), scope);
	const std::string operation = PayloadSource(node);
	TypeId result_type = kNoType;
	TypeId operand_type = kNoType;
	ValueCategory result_category = VALUE_PRVALUE;
	if (operation == "&&" || operation == "||")
	{
		if ((!IsArithmetic(left.type) && !IsPointer(Decay(left.type)) &&
			 !IsNullptr(left.type)) ||
			(!IsArithmetic(right.type) && !IsPointer(Decay(right.type)) &&
			 !IsNullptr(right.type)))
			throw std::runtime_error("invalid logical operands");
		result_type = program_->types.Fundamental(FUND_BOOL);
	}
	else if (operation == "==" || operation == "!=" || operation == "<" ||
		operation == ">" || operation == "<=" || operation == ">=")
	{
		const bool equality = operation == "==" || operation == "!=";
		const TypeId left_unqualified = program_->types.RemoveTopCv(
			EffectiveType(left.type));
		const TypeId right_unqualified = program_->types.RemoveTopCv(
			EffectiveType(right.type));
		const EntityId comparison_enum = left_unqualified == right_unqualified ?
			EntityOf(left_unqualified) : kNoEntity;
		if (comparison_enum != kNoEntity &&
			(program_->entities[comparison_enum].flavor == NAMED_ENUM ||
			 program_->entities[comparison_enum].flavor == NAMED_ENUM_CLASS))
			operand_type = left_unqualified;
		else if (IsArithmetic(left.type) && IsArithmetic(right.type))
			operand_type = CommonArithmeticType(left.type, right.type);
		else if (IsNullptr(left.type) && IsNullptr(right.type) && equality) {}
		else if (IsPointer(Decay(left.type)) &&
			(IsPointer(Decay(right.type)) || (right.integer_literal_zero && equality) ||
			 IsNullptr(right.type))) {}
		else if (IsPointer(Decay(right.type)) &&
			((left.integer_literal_zero && equality) || IsNullptr(left.type))) {}
		else throw std::runtime_error("invalid comparison operands");
		result_type = program_->types.Fundamental(FUND_BOOL);
	}
	else if (operation == ",")
	{
		result_type = EffectiveType(right.type);
		result_category = right.category;
	}
	else if (operation == "+" || operation == "-")
	{
		if (IsPointer(Decay(left.type)) && IsIntegral(right.type))
			result_type = Decay(left.type);
		else if (operation == "+" && IsIntegral(left.type) &&
			IsPointer(Decay(right.type)))
			result_type = Decay(right.type);
		else if (operation == "-" && IsPointer(Decay(left.type)) &&
			IsPointer(Decay(right.type)))
			result_type = program_->types.Fundamental(FUND_LONG_INT);
		else if (IsArithmetic(left.type) && IsArithmetic(right.type))
			result_type = operand_type = CommonArithmeticType(left.type, right.type);
		else throw std::runtime_error("invalid additive operands");
	}
	else
	{
		const bool integral_only = operation == "%" || operation == "<<" ||
			operation == ">>" || operation == "&" || operation == "|" ||
			operation == "^";
		if ((integral_only && (!IsIntegral(left.type) || !IsIntegral(right.type))) ||
			(!integral_only && (!IsArithmetic(left.type) ||
			 !IsArithmetic(right.type))))
			throw std::runtime_error("invalid binary arithmetic operands");
		result_type = operand_type = CommonArithmeticType(left.type, right.type);
	}
	const std::uint32_t expression = MakeDump(DUMP_BINARY_EXPRESSION,
		result_type, result_category,
		program_->names.Intern(arena_->Payload(node)));
	dump_.nodes[expression].operand_type = operand_type;
	dump_.Add(expression, left.node);
	dump_.Add(expression, right.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = result_type;
	result.category = result_category;
	result.constant = left.constant && right.constant;
	if (result.constant)
		result.value = ApplyConstantBinary(operation, left.value, right.value);
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeAssignment(NodeId node, ScopeId scope)
{
	const std::uint32_t first = arena_->FirstEdge(node);
	const std::uint32_t second = first == kNoEdge ? kNoEdge :
		arena_->NextEdge(first);
	if (second == kNoEdge) throw std::runtime_error("invalid assignment");
	ExpressionInfo left = AnalyzeExpression(arena_->EdgeChild(first), scope);
	const std::string operation = PayloadSource(node);
	ExpressionInfo right = AnalyzeExpression(arena_->EdgeChild(second), scope,
		operation == "=" ? EffectiveType(left.type) : kNoType);
	if (!IsModifiableLvalue(left))
		throw std::runtime_error("assignment requires modifiable lvalue");
	const bool pointer_add = IsPointer(left.type) &&
		(operation == "+=" || operation == "-=") && IsIntegral(right.type);
	if (operation != "=")
	{
		const bool additive = operation == "+=" || operation == "-=";
		const bool arithmetic_operation = additive || operation == "*=" ||
			operation == "/=";
		const bool integral_operation = operation == "%=" ||
			operation == "<<=" || operation == ">>=" || operation == "&=" ||
			operation == "|=" || operation == "^=";
		const bool arithmetic = arithmetic_operation &&
			IsArithmetic(left.type) && IsArithmetic(right.type);
		const bool integral = integral_operation && IsIntegral(left.type) &&
			IsIntegral(right.type);
		if (!pointer_add && !arithmetic && !integral)
			throw std::runtime_error("invalid compound assignment");
	}
	const TypeId result_type = EffectiveType(left.type);
	const std::uint32_t expression = MakeDump(DUMP_ASSIGNMENT_EXPRESSION,
		result_type, VALUE_LVALUE, program_->names.Intern(arena_->Payload(node)));
	if (operation != "=" && !pointer_add)
		dump_.nodes[expression].operand_type =
			CommonArithmeticType(left.type, right.type);
	dump_.Add(expression, left.node);
	dump_.Add(expression, right.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = result_type;
	result.category = VALUE_LVALUE;
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeCast(NodeId node, ScopeId scope)
{
	const NodeId type_id = FindChild(node, "type-id");
	if (type_id == kNoNode) throw std::runtime_error("cast without type-id");
	const TypeId target = BuildTypeId(type_id, scope);
	NodeId operand_node = kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (arena_->EdgeChild(edge) != type_id) operand_node = arena_->EdgeChild(edge);
	// Expression analysis can intern more types and reallocate TypeTable storage.
	// Keep the cast shape by value across the recursive operand analysis.
	const TypeRecord target_record = program_->types.Get(target);
	const TypeId unqualified_target = program_->types.RemoveTopCv(target);
	const TypeRecord unqualified_target_record =
		program_->types.Get(unqualified_target);
	const bool function_pointer_target =
		unqualified_target_record.kind == TYPE_POINTER &&
		program_->types.IsFunction(unqualified_target_record.child);
	ExpressionInfo operand = AnalyzeExpression(operand_node, scope,
		program_->types.IsFunction(EffectiveType(target)) ||
		function_pointer_target ||
		unqualified_target_record.kind == TYPE_MEMBER_POINTER ?
		target : kNoType);
	if (!IsVoid(target) && !IsArithmetic(target) && !IsPointer(target) &&
		!IsNullptr(target) && target_record.kind != TYPE_LVALUE_REFERENCE &&
		target_record.kind != TYPE_RVALUE_REFERENCE &&
		target_record.kind != TYPE_MEMBER_POINTER &&
		program_->types.Get(program_->types.RemoveTopCv(target)).kind != TYPE_NAMED)
		throw std::runtime_error("unsupported cast target");
	const ValueCategory category = target_record.kind == TYPE_LVALUE_REFERENCE ?
		VALUE_LVALUE : target_record.kind == TYPE_RVALUE_REFERENCE ?
		VALUE_XVALUE : VALUE_PRVALUE;
	const std::string cast_kind = arena_->Payload(node);
	if (target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE)
	{
		const bool explicit_rvalue = target_record.kind == TYPE_RVALUE_REFERENCE &&
			SimilarUnqualified(EffectiveType(operand.type), target_record.child);
		const bool explicit_cv_lvalue =
			target_record.kind == TYPE_LVALUE_REFERENCE &&
			operand.category == VALUE_LVALUE &&
			SimilarUnqualified(EffectiveType(operand.type), target_record.child) &&
			(cast_kind.find("CONST") != std::string::npos ||
			 cast_kind.compare(0, 10, "OP_LPAREN:") == 0);
		if (!explicit_rvalue && !explicit_cv_lvalue &&
			Conversion(operand, target) == CONVERSION_INVALID)
			throw std::runtime_error("invalid reference cast");
		operand.type = target;
		operand.category = category;
		dump_.nodes[operand.node].type = target;
		dump_.nodes[operand.node].category = category;
		return operand;
	}
	if (target_record.kind == TYPE_MEMBER_POINTER)
	{
		operand.type = target;
		dump_.nodes[operand.node].type = target;
		return operand;
	}
	const EntityId source_entity = EntityOf(operand.type);
	const EntityId target_entity = EntityOf(target);
	const bool source_enum = source_entity != kNoEntity &&
		(program_->entities[source_entity].flavor == NAMED_ENUM ||
		 program_->entities[source_entity].flavor == NAMED_ENUM_CLASS);
	const bool target_enum = target_entity != kNoEntity &&
		(program_->entities[target_entity].flavor == NAMED_ENUM ||
		 program_->entities[target_entity].flavor == NAMED_ENUM_CLASS);
	const bool permits_reinterpretation =
		cast_kind.compare(0, 10, "OP_LPAREN:") == 0 ||
		cast_kind.find("REINTER") != std::string::npos;
	const bool valid = IsVoid(target) ||
		(IsArithmetic(target) && IsArithmetic(operand.type)) ||
		(target_enum && (IsIntegral(operand.type, true) ||
			IsFloating(operand.type))) ||
		(source_enum && (IsIntegral(target, true) || IsFloating(target))) ||
		(IsPointer(target) && (IsPointer(operand.type) ||
			IsNullptr(operand.type) || operand.integer_literal_zero)) ||
		(target == program_->types.Fundamental(FUND_BOOL) &&
			(IsPointer(operand.type) || IsNullptr(operand.type))) ||
		(IsNullptr(target) && (IsNullptr(operand.type) ||
			operand.integer_literal_zero)) ||
		(permits_reinterpretation &&
			((IsPointer(target) && IsIntegral(operand.type)) ||
			 (IsIntegral(target) && IsPointer(operand.type))));
	if (!valid) throw std::runtime_error("invalid explicit conversion");
	const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION, target,
		VALUE_PRVALUE, program_->names.Intern(arena_->Payload(node)));
	dump_.Add(cast, operand.node);
	ExpressionInfo result;
	result.node = cast;
	result.type = target;
	result.constant = operand.constant;
	result.value = operand.value;
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeConditional(NodeId node, ScopeId scope)
{
	std::vector<NodeId> children;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge)) children.push_back(arena_->EdgeChild(edge));
	if (children.size() != 3) throw std::runtime_error("invalid conditional");
	ExpressionInfo condition = AnalyzeExpression(children[0], scope);
	if (!IsArithmetic(condition.type) && !IsPointer(Decay(condition.type)) &&
		!IsNullptr(condition.type))
		throw std::runtime_error("invalid conditional condition");
	ExpressionInfo yes = AnalyzeExpression(children[1], scope);
	ExpressionInfo no = AnalyzeExpression(children[2], scope);
	TypeId type = kNoType;
	ValueCategory category = VALUE_PRVALUE;
	if (EffectiveType(yes.type) == EffectiveType(no.type))
	{
		type = EffectiveType(yes.type);
		category = yes.category == no.category ? yes.category : VALUE_PRVALUE;
	}
	else if (IsArithmetic(yes.type) && IsArithmetic(no.type))
		type = CommonArithmeticType(yes.type, no.type);
	else if (IsPointer(Decay(yes.type)) &&
		(IsNullptr(no.type) || no.integer_literal_zero)) type = Decay(yes.type);
	else if (IsPointer(Decay(no.type)) &&
		(IsNullptr(yes.type) || yes.integer_literal_zero)) type = Decay(no.type);
	else if (IsPointer(Decay(yes.type)) && IsPointer(Decay(no.type)))
	{
		if (Conversion(yes, Decay(no.type)) != CONVERSION_INVALID)
			type = Decay(no.type);
		else if (Conversion(no, Decay(yes.type)) != CONVERSION_INVALID)
			type = Decay(yes.type);
	}
	if (type == kNoType) throw std::runtime_error("incompatible conditional arms");
	const std::uint32_t expression = MakeDump(DUMP_CONDITIONAL_EXPRESSION,
		type, category);
	dump_.Add(expression, condition.node);
	dump_.Add(expression, yes.node);
	dump_.Add(expression, no.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = type;
	result.category = category;
	result.constant = condition.constant &&
		(condition.value ? yes.constant : no.constant);
	if (result.constant) result.value = condition.value ? yes.value : no.value;
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeSubscript(NodeId node, ScopeId scope)
{
	const std::uint32_t first = arena_->FirstEdge(node);
	const std::uint32_t second = first == kNoEdge ? kNoEdge :
		arena_->NextEdge(first);
	if (second == kNoEdge) throw std::runtime_error("invalid subscript");
	ExpressionInfo left = AnalyzeExpression(arena_->EdgeChild(first), scope);
	ExpressionInfo right = AnalyzeExpression(arena_->EdgeChild(second), scope);
	if (!IsPointer(Decay(left.type)) && IsPointer(Decay(right.type)))
		std::swap(left, right);
	const TypeId pointer_type = Decay(left.type);
	const TypeRecord pointer = program_->types.Get(pointer_type);
	if (pointer.kind != TYPE_POINTER || !IsIntegral(right.type))
		throw std::runtime_error("invalid subscript operands");
	const std::uint32_t expression = MakeDump(DUMP_SUBSCRIPT_EXPRESSION,
		pointer.child, VALUE_LVALUE);
	dump_.Add(expression, left.node);
	dump_.Add(expression, right.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = pointer.child;
	result.category = VALUE_LVALUE;
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeSizeof(NodeId node, ScopeId scope)
{
	const NodeId operand = FirstSemanticChild(node);
	if (operand == kNoNode) throw std::runtime_error("empty sizeof");
	TypeId measured = kNoType;
	if (arena_->IsTag(operand, "type-id")) measured = BuildTypeId(operand, scope);
	else measured = AnalyzeExpression(operand, scope).type;
	(void)program_->SizeOf(measured);
	ExpressionInfo result;
	result.type = program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	result.node = MakeDump(DUMP_SIZEOF_EXPRESSION, result.type, VALUE_PRVALUE);
	result.constant = true;
	result.value = static_cast<std::int64_t>(program_->SizeOf(measured));
	RecordExpressionFacts(result);
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeBracedInit(NodeId node, ScopeId scope,
	TypeId target)
{
	if (target == kNoType) throw std::runtime_error("untyped braced-init-list");
	TypeId type = target;
	const TypeRecord array = program_->types.Get(type);
	TypeId element = type;
	if (array.kind == TYPE_ARRAY) element = array.child;
	std::vector<ExpressionInfo> values;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		values.push_back(AnalyzeExpression(arena_->EdgeChild(edge), scope, element));
	if (array.kind == TYPE_ARRAY && array.bound != 0 && values.size() > array.bound)
		throw std::runtime_error("excess array initializer elements");
	if (array.kind == TYPE_ARRAY && array.bound == 0)
		type = program_->types.Array(array.child, values.size());
	const std::uint32_t list = MakeDump(DUMP_BRACED_INIT_LIST, type,
		VALUE_LVALUE);
	for (std::size_t i = 0; i < values.size(); ++i) dump_.Add(list, values[i].node);
	ExpressionInfo result;
	result.node = list;
	result.type = type;
	result.category = VALUE_LVALUE;
	RecordExpressionFacts(result);
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeMember(NodeId node, ScopeId scope)
{
	const std::uint32_t first = arena_->FirstEdge(node);
	const std::uint32_t second = first == kNoEdge ? kNoEdge :
		arena_->NextEdge(first);
	if (second == kNoEdge) throw std::runtime_error("invalid member expression");
	ExpressionInfo object = AnalyzeExpression(arena_->EdgeChild(first), scope);
	const std::string source_operation = PayloadSource(node);
	TypeId owner_type = EffectiveType(object.type);
	if (source_operation == "->")
	{
		owner_type = program_->types.RemoveTopCv(owner_type);
		const TypeRecord pointer = program_->types.Get(owner_type);
		if (pointer.kind != TYPE_POINTER)
			throw std::runtime_error("arrow operand is not a pointer");
		owner_type = pointer.child;
	}
	const EntityId entity = EntityOf(owner_type);
	if (entity == kNoEntity || program_->entities[entity].member_scope == kNoScope)
		throw std::runtime_error("member access on non-class object");
	const NodeId identifier = arena_->EdgeChild(second);
	const NameId name = program_->names.Intern(arena_->Payload(identifier));
	const LookupResult found = program_->LookupDirect(
		program_->entities[entity].member_scope, name, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding)
		throw std::runtime_error("unknown class member");
	TypeId type = program_->bindings[found.ordinary].type;
	if (IsConst(owner_type)) type = program_->types.Qualify(type, CV_CONST);
	std::string operation = arena_->Payload(node);
	const std::size_t colon = operation.find(':');
	if (colon != std::string::npos) operation.erase(colon + 1);
	operation += program_->names.Get(name);
	const std::uint32_t expression = MakeDump(DUMP_MEMBER_EXPRESSION,
		type, VALUE_LVALUE, program_->names.Intern(operation), found.ordinary);
	dump_.Add(expression, object.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = type;
	result.category = VALUE_LVALUE;
	result.binding = found.ordinary;
	++expression_count_;
	return result;
}

TypeId SemanticAnalyzer::DecltypeType(NodeId node, ScopeId scope)
{
	if (node == kNoNode) throw std::runtime_error("empty decltype");
	bool parenthesized = false;
	if (arena_->IsTag(node, "parenthesized-expression"))
	{
		parenthesized = true;
		node = FirstSemanticChild(node);
	}
	if (arena_->IsTag(node, "id-expression"))
	{
		const LookupResult found = LookupSpelling(scope,
			arena_->Payload(node), LOOKUP_ORDINARY);
		if (found.ordinary == kNoBinding)
			throw std::runtime_error("decltype name not found");
		const BindingRecord& binding = program_->bindings[found.ordinary];
		if (!parenthesized || binding.kind == BIND_ENUMERATOR) return binding.type;
		return program_->types.Reference(TYPE_LVALUE_REFERENCE,
			EffectiveType(binding.type));
	}
	const ExpressionInfo expression = AnalyzeExpression(node, scope);
	if (expression.category == VALUE_LVALUE)
		return program_->types.Reference(TYPE_LVALUE_REFERENCE,
			EffectiveType(expression.type));
	if (expression.category == VALUE_XVALUE)
		return program_->types.Reference(TYPE_RVALUE_REFERENCE,
			EffectiveType(expression.type));
	return expression.type;
}

void SemanticAnalyzer::AnalyzeUsing(NodeId node, ScopeId scope,
	std::uint32_t output_parent, bool local)
{
	if (arena_->IsTag(node, "alias-declaration"))
	{
		const TypeId type = BuildTypeId(FindChild(node, "type-id"), scope);
		const NameId name = program_->names.Intern(arena_->Payload(node));
		program_->AddBinding(scope, BIND_TYPE_ALIAS, name, type);
		const std::uint32_t alias = MakeDump(DUMP_TYPE_ALIAS, type,
			VALUE_NONE, name);
		dump_.Add(output_parent, alias);
		return;
	}
	const NodeId target_node = FindChild(node, "target");
	if (target_node == kNoNode) throw std::runtime_error("missing using target");
	const std::string target = arena_->Payload(target_node);
	if (arena_->IsTag(node, "namespace-alias-definition"))
	{
		const ScopeId target_scope = ResolveScopeSpelling(scope, target);
		if (target_scope == kNoScope)
			throw std::runtime_error("namespace alias target not found");
		program_->AddNamespaceAlias(scope,
			program_->names.Intern(arena_->Payload(node)), target_scope);
		return;
	}
	if (arena_->IsTag(node, "using-directive"))
	{
		const ScopeId target_scope = ResolveScopeSpelling(scope, target);
		if (target_scope == kNoScope)
			throw std::runtime_error("using namespace target not found");
		program_->AddUsingEdge(scope, target_scope);
		return;
	}
	const NamePath path = ParseNamePath(target);
	const NameId name = path.Last();
	const LookupResult type = program_->Lookup(scope, path, LOOKUP_TYPE);
	if (type.type != kNoType)
	{
		program_->AddBinding(scope,
			program_->types.IsNamed(type.type) ? BIND_TYPE : BIND_TYPE_ALIAS,
			name, type.type, false, 0, NAMED_NONE, 0, type.type_declaration);
		return;
	}
	const std::vector<BindingId> functions = FunctionCandidates(scope, target);
	if (!functions.empty())
	{
		const std::uint64_t key = (static_cast<std::uint64_t>(scope) << 32) | name;
		CompactIndexSequence& aliases = function_sets_.Ensure(key);
		for (std::size_t i = 0; i < functions.size(); ++i)
		{
			const FunctionInfo& function = GetFunction(functions[i]);
			program_->AddBinding(scope, BIND_FUNCTION, name, function.type,
				false, 0, NAMED_NONE, 0, function.binding);
			if (!aliases.Contains(function.binding)) aliases.Push(function.binding);
		}
		return;
	}
	const LookupResult value = program_->Lookup(scope, path, LOOKUP_ORDINARY);
	if (value.ordinary == kNoBinding)
		throw std::runtime_error("using-declaration target not found");
	const BindingRecord& source = program_->bindings[value.ordinary];
	program_->AddBinding(scope, source.kind, name, source.type,
		source.constant, source.value, source.display_flavor,
		source.display_type_name, source.canonical);
	(void)local;
}

void SemanticAnalyzer::AnalyzeTemplate(NodeId node, ScopeId scope)
{
	const NodeId clause = FindChild(node, "template-parameter-clause");
	const NodeId list = clause == kNoNode ? kNoNode :
		FindChild(clause, "template-parameter-list");
	std::vector<NameId> parameters;
	if (list != kNoNode)
	{
		for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId parameter = arena_->EdgeChild(edge);
			if (!arena_->IsTag(parameter, "type-parameter"))
				throw std::runtime_error(
					"PA12 function templates require type parameters");
			const NodeId identifier = FindChild(parameter, "identifier");
			if (identifier == kNoNode)
				throw std::runtime_error("unnamed function template parameter");
			parameters.push_back(
				program_->names.Intern(arena_->Payload(identifier)));
		}
	}

	NodeId target = kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (child != clause) target = child;
	}
	if (target == kNoNode || !arena_->IsTag(target, "simple-declaration"))
		throw std::runtime_error("unsupported PA12 templated declaration");
	const NodeId specifiers = FindChild(target, "decl-specifier-seq");
	const NodeId declarators = FindChild(target, "init-declarator-list");
	if (specifiers == kNoNode || declarators == kNoNode)
		throw std::runtime_error("invalid PA12 function template");
	for (std::uint32_t edge = arena_->FirstEdge(declarators); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId item = arena_->EdgeChild(edge);
		const NodeId declarator = FindChild(item, "declarator");
		if (declarator == kNoNode) continue;
		FunctionTemplatePattern pattern;
		pattern.owner = scope;
		pattern.name = DeclaratorName(declarator);
		pattern.specifiers = specifiers;
		pattern.declarator = declarator;
		pattern.type_parameters = parameters;
		const std::size_t index = function_templates_.size();
		function_templates_.push_back(pattern);
		const std::uint64_t key =
			(static_cast<std::uint64_t>(scope) << 32) | pattern.name;
		template_function_sets_.Ensure(key).Push(index);
	}
}

void SemanticAnalyzer::AnalyzeNamespace(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	std::string spelling = arena_->Payload(node);
	const bool unnamed = spelling.empty() || spelling == "<unnamed>";
	if (unnamed) spelling = "<unnamed>";
	const NameId name = program_->names.Intern(spelling);
	const bool is_inline = FindChild(node, "inline") != kNoNode;
	const ScopeId child = program_->OpenNamespace(scope, name, is_inline);
	if (scope_prefixes_.size() <= child)
	{
		scope_prefixes_.resize(static_cast<std::size_t>(child) + 1, 0);
		scope_prefix_segments_.resize(static_cast<std::size_t>(child) + 1, 0);
		scope_parents_.resize(static_cast<std::size_t>(child) + 1, kNoScope);
		scope_prefixes_[child] = unnamed ? ScopePrefixId(scope) :
			std::numeric_limits<NameId>::max();
		scope_prefix_segments_[child] = unnamed ? 0 : name;
		scope_parents_[child] = scope;
	}
	if (unnamed) program_->AddUsingEdge(scope, child);
	const std::uint32_t output_node = MakeDump(DUMP_NAMESPACE, kNoType,
		VALUE_NONE, name);
	dump_.Add(output_parent, output_node);
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId declaration = arena_->EdgeChild(edge);
		if (IsDeclaration(declaration))
			AnalyzeDeclaration(declaration, child, output_node, false);
	}
}

void SemanticAnalyzer::AnalyzeDeclaration(NodeId node, ScopeId scope,
	std::uint32_t output_parent, bool local)
{
	if (arena_->IsTag(node, "empty-declaration")) return;
	if (arena_->IsTag(node, "template-declaration"))
	{
		AnalyzeTemplate(node, scope);
		return;
	}
	if (arena_->IsTag(node, "namespace-definition"))
	{
		AnalyzeNamespace(node, scope, output_parent);
		return;
	}
	if (arena_->IsTag(node, "namespace-alias-definition") ||
		arena_->IsTag(node, "using-directive") ||
		arena_->IsTag(node, "using-declaration") ||
		arena_->IsTag(node, "alias-declaration"))
	{
		AnalyzeUsing(node, scope, output_parent, local);
		return;
	}
	if (arena_->IsTag(node, "simple-declaration"))
	{
		AnalyzeSimple(node, scope, output_parent, local);
		return;
	}
	if (arena_->IsTag(node, "function-definition"))
	{
		AnalyzeFunction(node, scope, output_parent);
		return;
	}
	if (arena_->IsTag(node, "class-specifier") ||
		arena_->IsTag(node, "class-forward-declaration"))
	{
		const TypeId type = AnalyzeClass(node, scope, std::string(), false);
		const EntityId entity = EntityOf(type);
		if (local && arena_->Payload(node).empty() && entity != kNoEntity &&
			program_->entities[entity].flavor == NAMED_UNION)
		{
			std::ostringstream generated;
			generated << "__anonymous_union_storage__" << arena_->TokenFirst(node)
				<< '_' << arena_->TokenLast(node);
			const NameId storage_name = program_->names.Intern(generated.str());
			const BindingId storage = program_->AddBinding(scope, BIND_VARIABLE,
				storage_name, type);
			const std::uint32_t simple = MakeDump(DUMP_SIMPLE_DECLARATION);
			const std::uint32_t variable = MakeDump(DUMP_VARIABLE, type,
				VALUE_NONE, storage_name, storage);
			AddDefaultConstructor(variable, storage, type);
			dump_.Add(simple, variable);
			dump_.Add(output_parent, simple);
			const std::vector<BindingId>& members = entity_data_members_[entity];
			for (std::size_t i = 0; i < members.size(); ++i)
			{
				const BindingRecord source = program_->bindings[members[i]];
				const BindingId injected = program_->AddBinding(scope, BIND_VARIABLE,
					source.name, source.type, source.constant, source.value,
					source.display_flavor, source.display_type_name, source.canonical);
				if (injected_fact_by_binding_.size() <= injected)
					injected_fact_by_binding_.resize(
						static_cast<std::size_t>(injected) + 1, kNoDumpEdge);
				injected_fact_by_binding_[injected] =
					static_cast<std::uint32_t>(injected_members_.size());
				injected_members_.push_back(
					InjectedMemberInfo(storage, source.name));
			}
		}
		return;
	}
	if (arena_->IsTag(node, "enum-specifier"))
	{
		AnalyzeEnum(node, scope, std::string(), false);
		if (local)
		{
			const std::uint32_t simple = MakeDump(DUMP_SIMPLE_DECLARATION);
			dump_.Add(output_parent, simple);
		}
		return;
	}
	if (arena_->IsTag(node, "linkage-specification"))
	{
		const LanguageLinkage previous_linkage = current_language_linkage_;
		current_language_linkage_ = arena_->Payload(node) == "C" ?
			LANGUAGE_LINKAGE_C : LANGUAGE_LINKAGE_CPP;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (IsDeclaration(child))
				AnalyzeDeclaration(child, scope, output_parent, local);
		}
		current_language_linkage_ = previous_linkage;
		return;
	}
	throw std::runtime_error("unsupported PA12 declaration: " + arena_->Tag(node));
}

void SemanticAnalyzer::AnalyzeSimple(NodeId node, ScopeId scope,
	std::uint32_t output_parent, bool local)
{
	const NodeId specifiers = FindChild(node, "decl-specifier-seq");
	const NodeId list = FindChild(node, "init-declarator-list");
	std::string hint;
	if (list != kNoNode)
	{
		const NodeId first = FirstSemanticChild(list);
		const NodeId declarator = first == kNoNode ? kNoNode :
			FindChild(first, "declarator");
		if (declarator != kNoNode && DeclaratorName(declarator) != 0)
			hint = program_->names.Get(DeclaratorName(declarator));
	}
	const SpecInfo spec = BuildSpecifiers(specifiers, scope, hint,
		list != kNoNode);
	if (list == kNoNode)
	{
		if (local)
		{
			const std::uint32_t empty = MakeDump(DUMP_SIMPLE_DECLARATION);
			dump_.Add(output_parent, empty);
		}
		return;
	}
	const std::uint32_t owner = local ? MakeDump(DUMP_SIMPLE_DECLARATION) :
		output_parent;
	if (local) dump_.Add(output_parent, owner);
	for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId item = arena_->EdgeChild(edge);
		const NodeId declarator = FindChild(item, "declarator");
		if (declarator == kNoNode) throw std::runtime_error("missing declarator");
		DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, scope);
		if (parsed.name == 0) throw std::runtime_error("unnamed declaration");
		if (spec.is_typedef)
		{
			program_->AddBinding(scope, BIND_TYPE_ALIAS, parsed.name, parsed.type);
			const std::uint32_t alias = MakeDump(DUMP_TYPE_ALIAS, parsed.type,
				VALUE_NONE, parsed.name);
			dump_.Add(owner, alias);
			continue;
		}
		if (program_->types.IsFunction(parsed.type))
		{
			const BindingId function = DeclareFunction(scope, parsed.name,
				parsed.type, parsed.parameters, false, false, spec.storage_class,
				current_language_linkage_, IsNonthrowing(declarator, scope));
			const NodeId function_initializer = FindChild(item, "initializer");
			const NodeId special = function_initializer == kNoNode ? kNoNode :
				FindChild(function_initializer, "special-initializer");
			if (special != kNoNode && arena_->Payload(special) == "delete")
				continue;
			const std::uint32_t declaration = MakeDump(DUMP_FUNCTION_DECLARATION,
				parsed.type, VALUE_NONE, GetFunction(function).display_name, function);
			dump_.Add(owner, declaration);
			continue;
		}
		if (spec.is_constexpr)
			parsed.type = program_->types.Qualify(parsed.type, CV_CONST);
		const LookupResult occupied =
			program_->LookupDirect(scope, parsed.name, LOOKUP_ORDINARY);
		if (occupied.ordinary != kNoBinding &&
			program_->bindings[occupied.ordinary].kind == BIND_FUNCTION)
			throw std::runtime_error("variable conflicts with function binding");
		const BindingId binding = program_->AddBinding(scope, BIND_VARIABLE,
			parsed.name, parsed.type);
		BindingRecord& binding_record = program_->bindings[binding];
		binding_record.language_linkage = current_language_linkage_;
		binding_record.storage_class = spec.storage_class;
		const TypeRecord top_type = program_->types.Get(parsed.type);
		if (!local && binding_record.storage_class == STORAGE_CLASS_NONE &&
			top_type.kind == TYPE_QUALIFIED && (top_type.cv & CV_CONST) != 0)
			binding_record.storage_class = STORAGE_CLASS_STATIC;
		BindingRecord& canonical_record =
			program_->bindings[binding_record.canonical];
		canonical_record.language_linkage = binding_record.language_linkage;
		if (canonical_record.storage_class == STORAGE_CLASS_NONE ||
			binding_record.storage_class == STORAGE_CLASS_STATIC)
			canonical_record.storage_class = binding_record.storage_class;
		if (!local)
			program_->bindings[binding].qualified_name =
				DisplayName(scope, parsed.name);
		const NodeId initializer_node = FindChild(item, "initializer");
		ExpressionInfo initializer;
		bool has_initializer = false;
		if (initializer_node != kNoNode)
		{
			NodeId expression = FirstSemanticChild(initializer_node);
			if (expression != kNoNode && arena_->IsTag(expression, "paren-initializer"))
				expression = FirstSemanticChild(expression);
			initializer = AnalyzeExpression(expression, scope, parsed.type);
			has_initializer = true;
			if (program_->types.Get(parsed.type).kind == TYPE_ARRAY &&
				program_->types.Get(parsed.type).bound == 0)
			{
				parsed.type = initializer.type;
				program_->bindings[binding].type = parsed.type;
			}
			if (initializer.constant && (spec.is_constexpr ||
				(IsConst(parsed.type) && IsIntegral(parsed.type, true))))
			{
				program_->bindings[binding].constant = true;
				program_->bindings[binding].value = initializer.value;
				if (spec.is_constexpr && !IsPointer(parsed.type))
					dump_.nodes[initializer.node].type = parsed.type;
			}
		}
		const std::uint32_t variable = MakeDump(DUMP_VARIABLE, parsed.type,
			VALUE_NONE, parsed.name, binding);
		if (has_initializer) dump_.Add(variable, initializer.node);
		else if (EntityOf(parsed.type) != kNoEntity)
			AddDefaultConstructor(variable, binding, parsed.type);
		dump_.Add(owner, variable);
	}
}

void SemanticAnalyzer::AddDefaultConstructor(std::uint32_t variable,
	BindingId binding, TypeId type)
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity) return;
	const NamedFlavor flavor = program_->entities[entity].flavor;
	if (flavor != NAMED_STRUCT && flavor != NAMED_CLASS &&
		flavor != NAMED_UNION) return;
	const EntityRecord& class_record = program_->entities[entity];
	if (!class_record.default_constructible)
		throw std::runtime_error("class has no usable default constructor");
	const std::string owner = program_->names.Get(program_->entities[entity].name);
	const std::size_t separator = owner.rfind("::");
	const std::string leaf = separator == std::string::npos ? owner :
		owner.substr(separator + 2);
	const NameId constructor_name = program_->names.Intern(owner + "::" + leaf);
	const TypeId this_type = program_->types.Pointer(type);
	std::vector<TypeId> parameters(1, this_type);
	const TypeId function_type = program_->types.Function(
		program_->types.Fundamental(FUND_VOID), parameters, false);
	const std::uint32_t action = MakeDump(DUMP_CONSTRUCTOR_ACTION, kNoType,
		VALUE_NONE, constructor_name);
	const std::uint32_t call = MakeDump(DUMP_CALL_EXPRESSION,
		program_->types.Fundamental(FUND_VOID), VALUE_PRVALUE);
	const std::uint32_t callee = MakeDump(DUMP_CALLEE, function_type,
		VALUE_NONE, constructor_name);
	const BindingRecord& object = program_->bindings[binding];
	const std::uint32_t identifier = MakeDump(DUMP_ID_EXPRESSION, type,
		VALUE_LVALUE, object.name, binding);
	const std::uint32_t address = MakeDump(DUMP_UNARY_EXPRESSION, this_type,
		VALUE_PRVALUE, program_->names.Intern("OP_AMP:&"));
	dump_.Add(address, identifier);
	dump_.Add(call, callee);
	dump_.Add(call, address);
	dump_.Add(action, call);
	dump_.Add(variable, action);
	expression_count_ += 3;
	if (default_constructor_demand_states_.size() <= entity)
		default_constructor_demand_states_.resize(
			static_cast<std::size_t>(entity) + 1, 0);
	if (default_constructor_demand_states_[entity] == 0)
	{
		default_constructor_demand_states_[entity] = 1;
		demanded_default_constructor_entities_.push_back(entity);
		++demand_worklist_pushes_;
	}
}

void SemanticAnalyzer::EmitDefaultConstructor(EntityId entity)
{
	if (entity >= default_constructor_demand_states_.size() ||
		default_constructor_demand_states_[entity] != 1) return;
	default_constructor_demand_states_[entity] = 2;
	const TypeId type = program_->entities[entity].type;
	const std::string owner = program_->names.Get(program_->entities[entity].name);
	const std::size_t separator = owner.rfind("::");
	const std::string leaf = separator == std::string::npos ? owner :
		owner.substr(separator + 2);
	const NameId name = program_->names.Intern(owner + "::" + leaf);
	const TypeId this_type = program_->types.Pointer(type);
	std::vector<TypeId> parameters(1, this_type);
	const TypeId function_type = program_->types.Function(
		program_->types.Fundamental(FUND_VOID), parameters, false);
	const std::uint32_t function = MakeDump(DUMP_FUNCTION_DEFINITION,
		function_type, VALUE_NONE, name);
	const std::uint32_t parameter = MakeDump(DUMP_PARAMETER, this_type,
		VALUE_NONE, program_->names.Intern("this"));
	const std::uint32_t body = MakeDump(DUMP_COMPOUND_STATEMENT);
	dump_.Add(function, parameter);
	dump_.Add(function, body);
	dump_.Add(root_, function);
	++default_constructor_emissions_;
}

void SemanticAnalyzer::AnalyzeFunction(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	const NodeId declarator = FindChild(node, "declarator");
	const NamePath path = DeclaratorNamePath(declarator);
	const ScopeId owner = ResolveOwner(scope, path);
	if (owner == kNoScope) throw std::runtime_error("function owner not found");
	const SpecInfo spec = BuildSpecifiers(FindChild(node, "decl-specifier-seq"),
		owner, std::string(), true);
	DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, owner);
	parsed.name = path.Last();
	if (!program_->types.IsFunction(parsed.type))
		throw std::runtime_error("function definition has non-function type");
	const BindingId binding = DeclareFunction(owner, parsed.name,
		parsed.type, parsed.parameters, true, false, spec.storage_class,
		current_language_linkage_, IsNonthrowing(declarator, owner));
	const FunctionInfo& function = GetFunction(binding);
	const std::uint32_t output_node = MakeDump(DUMP_FUNCTION_DEFINITION,
		parsed.type, VALUE_NONE, function.display_name, binding);
	dump_.Add(output_parent, output_node);
	const ScopeId function_scope = NewScope(owner, SCOPE_FUNCTION, parsed.name,
		ScopePrefixId(owner));
	for (std::size_t i = 0; i < parsed.parameters.size(); ++i)
	{
		const ParameterInfo& parameter = parsed.parameters[i];
		const BindingId parameter_binding = program_->AddBinding(function_scope,
			BIND_PARAMETER, parameter.name, parameter.declared_type);
		const std::uint32_t parameter_node = MakeDump(DUMP_PARAMETER,
			parameter.function_type, VALUE_NONE, parameter.name, parameter_binding);
		dump_.Add(output_node, parameter_node);
	}
	const TypeId previous_return = current_return_type_;
	current_return_type_ = program_->types.Get(parsed.type).child;
	const NodeId body = FindChild(node, "compound-statement");
	if (body != kNoNode) AnalyzeCompound(body, function_scope, output_node);
	current_return_type_ = previous_return;
}

void SemanticAnalyzer::AnalyzeCompound(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	const ScopeId block = NewScope(scope, SCOPE_BLOCK, 0, ScopePrefixId(scope));
	const std::uint32_t compound = MakeDump(DUMP_COMPOUND_STATEMENT);
	dump_.Add(output_parent, compound);
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (IsDeclaration(child))
			AnalyzeDeclaration(child, block, compound, true);
		else AnalyzeStatement(child, block, compound);
	}
}

void SemanticAnalyzer::AnalyzeSubstatement(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	if (arena_->IsTag(node, "compound-statement"))
		AnalyzeCompound(node, scope, output_parent);
	else
	{
		const ScopeId child = NewScope(scope, SCOPE_BLOCK, 0,
			ScopePrefixId(scope));
		if (IsDeclaration(node))
			AnalyzeDeclaration(node, child, output_parent, true);
		else AnalyzeStatement(node, child, output_parent);
	}
}

void SemanticAnalyzer::AnalyzeCondition(NodeId node, ScopeId scope,
	std::uint32_t output_parent, bool switch_condition)
{
	const std::uint32_t condition = MakeDump(DUMP_CONDITION);
	dump_.Add(output_parent, condition);
	NodeId declaration_node = node;
	const NodeId first_child = FirstSemanticChild(node);
	if (first_child != kNoNode &&
		arena_->IsTag(first_child, "condition-declaration"))
		declaration_node = first_child;
	const NodeId specifiers = FindChild(declaration_node, "decl-specifier-seq");
	if (specifiers != kNoNode)
	{
		const NodeId declarator = FindChild(declaration_node, "declarator");
		const SpecInfo spec = BuildSpecifiers(specifiers, scope, std::string(), true);
		DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, scope);
		const BindingId binding = program_->AddBinding(scope, BIND_VARIABLE,
			parsed.name, parsed.type);
		const NodeId initializer = FindChild(declaration_node, "initializer");
		ExpressionInfo value = AnalyzeExpression(FirstSemanticChild(initializer),
			scope, parsed.type);
		const std::uint32_t declaration = MakeDump(DUMP_CONDITION_DECLARATION);
		const std::uint32_t variable = MakeDump(DUMP_VARIABLE, parsed.type,
			VALUE_NONE, parsed.name, binding);
		dump_.Add(variable, value.node);
		dump_.Add(declaration, variable);
		dump_.Add(condition, declaration);
		if (switch_condition)
		{
			if (!IsIntegral(parsed.type, true))
				throw std::runtime_error("invalid switch condition");
		}
		else if (!IsArithmetic(parsed.type) && !IsPointer(parsed.type))
			throw std::runtime_error("invalid condition type");
		return;
	}
	ExpressionInfo value = AnalyzeExpression(FirstSemanticChild(node), scope);
	if (switch_condition)
	{
		if (!IsIntegral(value.type, true))
			throw std::runtime_error("invalid switch condition");
	}
	else if (!IsArithmetic(value.type) && !IsPointer(value.type) &&
		!IsNullptr(value.type)) throw std::runtime_error("invalid condition type");
	dump_.Add(condition, value.node);
}

void SemanticAnalyzer::AnalyzeStatement(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	if (arena_->IsTag(node, "compound-statement"))
	{
		AnalyzeCompound(node, scope, output_parent);
		return;
	}
	if (arena_->IsTag(node, "return-statement"))
	{
		const std::uint32_t statement = MakeDump(DUMP_RETURN_STATEMENT);
		dump_.Add(output_parent, statement);
		const NodeId expression = FirstSemanticChild(node);
		if (expression == kNoNode)
		{
			if (!IsVoid(current_return_type_))
				throw std::runtime_error("missing return value");
		}
		else
		{
			ExpressionInfo value = AnalyzeExpression(expression, scope,
				IsVoid(current_return_type_) ? kNoType : current_return_type_);
			if (IsVoid(current_return_type_) && !IsVoid(value.type))
				throw std::runtime_error("void function returns a value");
			dump_.Add(statement, value.node);
		}
		return;
	}
	if (arena_->IsTag(node, "expression-statement"))
	{
		const std::uint32_t statement = MakeDump(DUMP_EXPRESSION_STATEMENT);
		dump_.Add(output_parent, statement);
		const NodeId expression = FirstSemanticChild(node);
		if (expression != kNoNode)
			dump_.Add(statement, AnalyzeExpression(expression, scope).node);
		return;
	}
	if (arena_->IsTag(node, "if-statement"))
	{
		const ScopeId control = NewScope(scope, SCOPE_BLOCK, 0,
			ScopePrefixId(scope));
		const std::uint32_t statement = MakeDump(DUMP_IF_STATEMENT);
		dump_.Add(output_parent, statement);
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "condition"))
				AnalyzeCondition(child, control, statement, false);
			else if (arena_->IsTag(child, "then") || arena_->IsTag(child, "else"))
			{
				const std::uint32_t branch = MakeDump(arena_->IsTag(child, "then") ?
					DUMP_THEN : DUMP_ELSE);
				dump_.Add(statement, branch);
				AnalyzeSubstatement(FirstSemanticChild(child), control, branch);
			}
		}
		return;
	}
	if (arena_->IsTag(node, "while-statement") ||
		arena_->IsTag(node, "do-statement"))
	{
		const bool is_do = arena_->IsTag(node, "do-statement");
		const ScopeId control = NewScope(scope, SCOPE_BLOCK, 0,
			ScopePrefixId(scope));
		const std::uint32_t statement = MakeDump(is_do ?
			DUMP_DO_STATEMENT : DUMP_WHILE_STATEMENT);
		dump_.Add(output_parent, statement);
		++loop_depth_;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "condition"))
				AnalyzeCondition(child, control, statement, false);
			else AnalyzeSubstatement(child, control, statement);
		}
		--loop_depth_;
		return;
	}
	if (arena_->IsTag(node, "for-statement"))
	{
		const ScopeId control = NewScope(scope, SCOPE_BLOCK, 0,
			ScopePrefixId(scope));
		const std::uint32_t statement = MakeDump(DUMP_FOR_STATEMENT);
		dump_.Add(output_parent, statement);
		++loop_depth_;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "for-init-statement"))
			{
				const std::uint32_t init = MakeDump(DUMP_FOR_INIT_STATEMENT);
				dump_.Add(statement, init);
				const NodeId value = FirstSemanticChild(child);
				if (value != kNoNode)
				{
					if (IsDeclaration(value))
						AnalyzeDeclaration(value, control, init, true);
					else dump_.Add(init, AnalyzeExpression(value, control).node);
				}
			}
			else if (arena_->IsTag(child, "condition"))
				AnalyzeCondition(child, control, statement, false);
			else if (arena_->IsTag(child, "iteration"))
			{
				const std::uint32_t iteration = MakeDump(DUMP_ITERATION);
				dump_.Add(statement, iteration);
				dump_.Add(iteration,
					AnalyzeExpression(FirstSemanticChild(child), control).node);
			}
			else AnalyzeSubstatement(child, control, statement);
		}
		--loop_depth_;
		return;
	}
	if (arena_->IsTag(node, "switch-statement"))
	{
		const ScopeId control = NewScope(scope, SCOPE_BLOCK, 0,
			ScopePrefixId(scope));
		const std::uint32_t statement = MakeDump(DUMP_SWITCH_STATEMENT);
		dump_.Add(output_parent, statement);
		++switch_depth_;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "condition"))
				AnalyzeCondition(child, control, statement, true);
			else AnalyzeSubstatement(child, control, statement);
		}
		--switch_depth_;
		return;
	}
	if (arena_->IsTag(node, "break-statement"))
	{
		if (loop_depth_ == 0 && switch_depth_ == 0)
			throw std::runtime_error("break outside loop or switch");
		dump_.Add(output_parent, MakeDump(DUMP_BREAK_STATEMENT));
		return;
	}
	if (arena_->IsTag(node, "continue-statement"))
	{
		if (loop_depth_ == 0) throw std::runtime_error("continue outside loop");
		dump_.Add(output_parent, MakeDump(DUMP_CONTINUE_STATEMENT));
		return;
	}
	if (arena_->IsTag(node, "case-statement") ||
		arena_->IsTag(node, "default-statement"))
	{
		if (switch_depth_ == 0)
			throw std::runtime_error("case/default outside switch");
		const bool is_case = arena_->IsTag(node, "case-statement");
		const std::uint32_t statement = MakeDump(is_case ?
			DUMP_CASE_STATEMENT : DUMP_DEFAULT_STATEMENT);
		dump_.Add(output_parent, statement);
		bool first = true;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (is_case && first)
			{
				ExpressionInfo label = AnalyzeExpression(child, scope);
				if (!label.constant) throw std::runtime_error("nonconstant case label");
				dump_.Add(statement, label.node);
				first = false;
			}
			else if (IsDeclaration(child))
				AnalyzeDeclaration(child, scope, statement, true);
			else AnalyzeStatement(child, scope, statement);
		}
		return;
	}
	if (arena_->IsTag(node, "labeled-statement"))
	{
		const std::uint32_t statement = MakeDump(DUMP_LABELED_STATEMENT,
			kNoType, VALUE_NONE, program_->names.Intern(arena_->Payload(node)));
		dump_.Add(output_parent, statement);
		const NodeId child = FirstSemanticChild(node);
		if (child == kNoNode) throw std::runtime_error("label without statement");
		AnalyzeStatement(child, scope, statement);
		return;
	}
	if (arena_->IsTag(node, "goto-statement"))
	{
		dump_.Add(output_parent, MakeDump(DUMP_GOTO_STATEMENT,
			kNoType, VALUE_NONE, program_->names.Intern(arena_->Payload(node))));
		return;
	}
	if (IsDeclaration(node))
	{
		AnalyzeDeclaration(node, scope, output_parent, true);
		return;
	}
	throw std::runtime_error("unsupported PA12 statement: " + arena_->Tag(node));
}

void SemanticAnalyzer::RenderLine(const DumpNode& node, std::size_t depth)
{
	for (std::size_t i = 0; i < depth; ++i) output_ << "  ";
	const char* category = node.category == VALUE_LVALUE ? "lvalue" :
		node.category == VALUE_XVALUE ? "xvalue" : "prvalue";
	switch (node.kind)
	{
	case DUMP_TRANSLATION_UNIT: output_ << "translation-unit"; break;
	case DUMP_NAMESPACE:
		output_ << "namespace-definition " << program_->names.Get(node.text); break;
	case DUMP_TYPE_ALIAS:
		output_ << "type-alias " << program_->names.Get(node.text) << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_VARIABLE:
		output_ << "variable " << program_->names.Get(node.text) << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_FUNCTION_DECLARATION:
		output_ << "function-declaration " << program_->names.Get(node.text)
			<< ' ' << program_->RenderType(node.type); break;
	case DUMP_FUNCTION_DEFINITION:
		output_ << "function-definition " << program_->names.Get(node.text)
			<< ' ' << program_->RenderType(node.type); break;
	case DUMP_PARAMETER:
		output_ << "parameter " << program_->names.Get(node.text) << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_COMPOUND_STATEMENT: output_ << "compound-statement"; break;
	case DUMP_SIMPLE_DECLARATION: output_ << "simple-declaration"; break;
	case DUMP_RETURN_STATEMENT: output_ << "return-statement"; break;
	case DUMP_EXPRESSION_STATEMENT: output_ << "expression-statement"; break;
	case DUMP_IF_STATEMENT: output_ << "if-statement"; break;
	case DUMP_SWITCH_STATEMENT: output_ << "switch-statement"; break;
	case DUMP_WHILE_STATEMENT: output_ << "while-statement"; break;
	case DUMP_DO_STATEMENT: output_ << "do-statement"; break;
	case DUMP_FOR_STATEMENT: output_ << "for-statement"; break;
	case DUMP_BREAK_STATEMENT: output_ << "break-statement"; break;
	case DUMP_CONTINUE_STATEMENT: output_ << "continue-statement"; break;
	case DUMP_CONDITION: output_ << "condition"; break;
	case DUMP_CONDITION_DECLARATION: output_ << "condition-declaration"; break;
	case DUMP_FOR_INIT_STATEMENT: output_ << "for-init-statement"; break;
	case DUMP_ITERATION: output_ << "iteration"; break;
	case DUMP_THEN: output_ << "then"; break;
	case DUMP_ELSE: output_ << "else"; break;
	case DUMP_CASE_STATEMENT: output_ << "case-statement"; break;
	case DUMP_DEFAULT_STATEMENT: output_ << "default-statement"; break;
	case DUMP_LABELED_STATEMENT:
		output_ << "labeled-statement " << program_->names.Get(node.text); break;
	case DUMP_GOTO_STATEMENT:
		output_ << "goto-statement " << program_->names.Get(node.text); break;
	case DUMP_CALL_EXPRESSION:
		output_ << "call-expression " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_CALLEE:
		output_ << "callee " << program_->names.Get(node.text) << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_ID_EXPRESSION:
		output_ << "id-expression " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_LITERAL:
		output_ << "literal " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_UNARY_EXPRESSION:
		output_ << "unary-expression " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_POSTFIX_EXPRESSION:
		output_ << "postfix-expression " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_BINARY_EXPRESSION:
		output_ << "binary-expression " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_SUBSCRIPT_EXPRESSION:
		output_ << "subscript-expression " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_CONDITIONAL_EXPRESSION:
		output_ << "conditional-expression " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_SIZEOF_EXPRESSION:
		output_ << "sizeof-expression " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_ASSIGNMENT_EXPRESSION:
		output_ << "assignment-expression " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_CAST_EXPRESSION:
		output_ << "cast-expression " << category << ' '
			<< program_->RenderType(node.type);
		if (node.text != 0) output_ << ' ' << program_->names.Get(node.text);
		break;
	case DUMP_BRACED_INIT_LIST:
		output_ << "braced-init-list " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_MEMBER_EXPRESSION:
		output_ << "member-expression " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_CONSTRUCTOR_ACTION:
		output_ << "constructor-action " << program_->names.Get(node.text); break;
	}
	output_ << '\n';
}

void SemanticAnalyzer::RenderNode(std::uint32_t node, std::size_t depth)
{
	RenderLine(dump_.nodes[node], depth);
	for (std::uint32_t edge = dump_.nodes[node].first_edge;
		edge != kNoDumpEdge; edge = dump_.edges[edge].next)
		RenderNode(dump_.edges[edge].child, depth + 1);
}

void SemanticAnalyzer::Render()
{
	RenderNode(root_, 0);
}

void SemanticAnalyzer::Consume(const SyntaxArena& arena, NodeId root)
{
	arena_ = &arena;
	Program program(arena.SharedStrings());
	program_ = &program;
	scope_prefixes_.resize(static_cast<std::size_t>(program.GlobalScope()) + 1, 0);
	scope_prefix_segments_.resize(
		static_cast<std::size_t>(program.GlobalScope()) + 1, 0);
	scope_parents_.resize(static_cast<std::size_t>(program.GlobalScope()) + 1,
		kNoScope);
	program.AddBinding(program.GlobalScope(), BIND_TYPE_ALIAS,
		program.names.Intern("nullptr_t"),
		program.types.Fundamental(FUND_NULLPTR_T));
	root_ = MakeDump(DUMP_TRANSLATION_UNIT);
	const std::chrono::steady_clock::time_point analysis_started =
		std::chrono::steady_clock::now();
	for (std::uint32_t edge = arena.FirstEdge(root); edge != kNoEdge;
		edge = arena.NextEdge(edge))
		AnalyzeDeclaration(arena.EdgeChild(edge), program.GlobalScope(), root_, false);
	std::size_t default_demand = 0;
	std::size_t function_demand = 0;
	while (default_demand < demanded_default_constructor_entities_.size() ||
		function_demand < demanded_functions_.size())
	{
		while (default_demand < demanded_default_constructor_entities_.size())
			EmitDefaultConstructor(
				demanded_default_constructor_entities_[default_demand++]);
		while (function_demand < demanded_functions_.size())
			EmitDemandedFunction(demanded_functions_[function_demand++]);
	}
	const std::chrono::steady_clock::time_point render_started =
		std::chrono::steady_clock::now();
	if (graph_consumer_)
		graph_consumer_->Consume(SemanticGraphView(program, dump_, root_));
	if (render_output_) Render();
	if (stats_)
	{
		const std::chrono::steady_clock::time_point finished =
			std::chrono::steady_clock::now();
		stats_->semantic_nodes = dump_.nodes.size();
		stats_->semantic_edges = dump_.edges.size();
		stats_->interned_names = program.names.Size();
		stats_->canonical_types = program.types.Size();
		stats_->scopes = program.ScopeCount();
		stats_->declarations = program.bindings.size() - 1;
		stats_->expressions = expression_count_;
		stats_->class_layouts = class_layouts_;
		stats_->class_layout_member_visits = class_layout_member_visits_;
		stats_->lookup_queries = program.lookup_queries;
		stats_->lookup_scope_visits = program.lookup_scope_visits;
		stats_->lookup_edge_visits = program.lookup_edge_visits;
		stats_->overload_candidates = overload_candidates_;
		stats_->overload_order_comparisons = overload_order_comparisons_;
		stats_->conversion_checks = conversion_checks_;
		stats_->function_signature_lookups = function_signature_lookups_;
		stats_->template_specialization_requests =
			template_specialization_requests_;
		stats_->template_specialization_cache_hits =
			template_specialization_cache_hits_;
		stats_->demand_worklist_pushes = demand_worklist_pushes_;
		stats_->demanded_function_emissions = demanded_function_emissions_;
		stats_->default_constructor_emissions =
			default_constructor_emissions_;
		const std::size_t shared_string_storage =
			arena.SharedStrings().StorageBytes();
		const std::size_t program_storage = program.StorageBytes();
		stats_->semantic_storage_bytes =
			(program_storage >= shared_string_storage ?
			 program_storage - shared_string_storage : program_storage) +
			dump_.StorageBytes() + SideStorageBytes();
		stats_->analysis_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				render_started - analysis_started).count());
		stats_->render_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				finished - render_started).count());
	}
	program_ = 0;
}


}

SemanticAnalysisStats::SemanticAnalysisStats()
	: tokens(0), syntax_nodes(0), semantic_nodes(0), semantic_edges(0),
	  interned_names(0), canonical_types(0), scopes(0), declarations(0),
	  expressions(0), class_layouts(0), class_layout_member_visits(0),
	  lookup_queries(0), lookup_scope_visits(0),
	  lookup_edge_visits(0), overload_candidates(0),
	  overload_order_comparisons(0), conversion_checks(0),
	  function_signature_lookups(0), template_specialization_requests(0),
	  template_specialization_cache_hits(0), demand_worklist_pushes(0),
	  demanded_function_emissions(0), default_constructor_emissions(0),
	  semantic_storage_bytes(0), peak_stage_storage_bytes(0),
	  analysis_nanoseconds(0), render_nanoseconds(0), elapsed_nanoseconds(0)
{
}

void WriteSemanticTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, SemanticAnalysisStats* stats)
{
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	if (stats) *stats = SemanticAnalysisStats();
	SyntaxStats syntax_stats;
	pa12_semantic_detail::SemanticAnalyzer analyzer(output, stats);
	ConsumeSyntaxTranslationUnit(path, source, options, analyzer,
		stats ? &syntax_stats : 0);
	if (stats)
	{
		stats->preprocessing = syntax_stats.preprocessing;
		stats->tokens = syntax_stats.tokens;
		stats->syntax_nodes = syntax_stats.syntax_nodes;
		stats->peak_stage_storage_bytes = source.size() +
			syntax_stats.token_storage_bytes + syntax_stats.syntax_storage_bytes +
			syntax_stats.parser_storage_bytes + stats->semantic_storage_bytes;
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count());
	}
}

void ConsumeSemanticTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	pa12_semantic_detail::SemanticGraphConsumer& consumer,
	SemanticAnalysisStats* stats)
{
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	if (stats) *stats = SemanticAnalysisStats();
	SyntaxStats syntax_stats;
	std::ostringstream unused_output;
	pa12_semantic_detail::SemanticAnalyzer analyzer(unused_output, stats,
		&consumer, false);
	ConsumeSyntaxTranslationUnit(path, source, options, analyzer,
		stats ? &syntax_stats : 0);
	if (stats)
	{
		stats->preprocessing = syntax_stats.preprocessing;
		stats->tokens = syntax_stats.tokens;
		stats->syntax_nodes = syntax_stats.syntax_nodes;
		stats->peak_stage_storage_bytes = source.size() +
			syntax_stats.token_storage_bytes + syntax_stats.syntax_storage_bytes +
			syntax_stats.parser_storage_bytes + stats->semantic_storage_bytes;
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count());
	}
}

}
