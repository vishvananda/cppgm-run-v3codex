#include "pa12_semantic_detail.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <limits>
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
	std::size_t conversion_terminal = std::string::npos;
	if (spelling.compare(first, 9, "operator ") == 0)
		conversion_terminal = first;
	else
	{
		const std::size_t separator = spelling.find("::operator ", first);
		if (separator != std::string::npos)
			conversion_terminal = separator + 2;
	}
	std::size_t count = 1;
	for (std::size_t scan = first; scan != conversion_terminal &&
		(scan = spelling.find("::", scan)) != std::string::npos;
		scan += 2) ++count;
	result.Reserve(count);
	while (first < spelling.size())
	{
		const std::size_t separator = first == conversion_terminal ?
			std::string::npos : spelling.find("::", first);
		const std::size_t last = separator == std::string::npos ?
			spelling.size() : separator;
		if (last == first) throw std::runtime_error("invalid qualified name");
		if (first == conversion_terminal)
		{
			std::string terminal;
			terminal.reserve(last - first);
			for (std::size_t i = first; i < last; ++i)
				if (!std::isspace(static_cast<unsigned char>(spelling[i])))
					terminal += spelling[i];
			result.Push(program_->names.Intern(terminal));
		}
		else result.Push(
			program_->names.InternRange(spelling, first, last - first));
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
		arena_->IsTag(node, "special-member-declaration") ||
		arena_->IsTag(node, "special-member-definition") ||
		arena_->IsTag(node, "class-specifier") ||
		arena_->IsTag(node, "class-forward-declaration") ||
		arena_->IsTag(node, "enum-specifier") ||
		arena_->IsTag(node, "empty-declaration") ||
		arena_->IsTag(node, "layout-pack-push") ||
		arena_->IsTag(node, "layout-pack-pop") ||
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
	if (kind == DUMP_VARIABLE && binding != kNoBinding)
	{
		if (variable_node_by_binding_.size() <= binding)
			variable_node_by_binding_.resize(
				static_cast<std::size_t>(binding) + 1, kNoDumpEdge);
		variable_node_by_binding_[binding] = node;
	}
	return node;
}

std::size_t SemanticAnalyzer::SideStorageBytes() const {
	std::size_t bytes =
		scope_prefixes_.capacity() * sizeof(NameId) +
		scope_prefix_segments_.capacity() * sizeof(NameId) +
		scope_parents_.capacity() * sizeof(ScopeId) +
		scope_prefix_scratch_.capacity() * sizeof(NameId) +
		function_sets_.StorageBytes() +
		ordinary_function_sets_.StorageBytes() +
		hidden_friend_sets_.StorageBytes() +
		friend_class_grants_.StorageBytes() +
		friend_function_grants_.StorageBytes() +
		function_declarations_.StorageBytes() +
		using_function_declarations_.StorageBytes() +
		member_ref_qualifier_shapes_.StorageBytes() +
		function_fact_by_binding_.capacity() * sizeof(std::uint32_t) +
		functions_.capacity() * sizeof(FunctionInfo) +
		variable_node_by_binding_.capacity() * sizeof(std::uint32_t) +
		builtin_functions_.capacity() * sizeof(BindingId) +
		entity_data_members_.capacity() * sizeof(std::vector<BindingId>) +
		entity_layout_members_.capacity() *
			sizeof(std::vector<ClassLayoutMember>) +
		zero_offset_subobject_marks_.capacity() * sizeof(std::uint32_t) +
		zero_offset_subobject_scratch_.capacity() * sizeof(EntityId) +
		entity_constructors_.capacity() * sizeof(std::vector<BindingId>) +
		entity_conversion_functions_.capacity() *
			sizeof(std::vector<BindingId>) +
		class_special_members_.capacity() * sizeof(ClassSpecialMemberFacts) +
		implicit_constructor_by_entity_.capacity() * sizeof(BindingId) +
		constructor_base_entry_by_binding_.capacity() * sizeof(BindingId) +
		destructor_base_entry_by_binding_.capacity() * sizeof(BindingId) +
		static_member_storage_by_binding_.capacity() * sizeof(std::uint32_t) +
		entity_destructor_by_entity_.capacity() * sizeof(BindingId) +
		hidden_friend_anchor_by_entity_.capacity() * sizeof(BindingId) +
		member_initializer_by_binding_.capacity() * sizeof(NodeId) +
		constructor_initializer_scratch_.capacity() * sizeof(NodeId) +
		constructor_initializer_touched_.capacity() * sizeof(BindingId) +
		function_templates_.capacity() * sizeof(FunctionTemplatePattern) +
		template_function_sets_.StorageBytes() +
		template_instantiations_.StorageBytes() +
		injected_fact_by_binding_.capacity() * sizeof(std::uint32_t) +
		injected_members_.capacity() * sizeof(InjectedMemberInfo) +
		scope_lifetimes_.capacity() *
			sizeof(std::vector<LifetimeObligation>) +
		nearest_lifetime_scopes_.capacity() * sizeof(ScopeId) +
		namespace_objects_.capacity() * sizeof(NamespaceObjectAction) +
		aggregate_helpers_.capacity() * sizeof(AggregateHelperInfo) + aggregate_helper_index_.StorageBytes() +
		break_cleanup_stops_.capacity() * sizeof(ScopeId) +
		continue_cleanup_stops_.capacity() * sizeof(ScopeId) +
		demanded_default_constructor_entities_.capacity() * sizeof(EntityId) +
		default_constructor_demand_states_.capacity() * sizeof(std::uint8_t) +
		demanded_functions_.capacity() * sizeof(BindingId) +
		associated_entities_.capacity() * sizeof(EntityId) +
		associated_scopes_.capacity() * sizeof(ScopeId) +
		associated_type_scratch_.capacity() * sizeof(TypeId) +
		associated_entity_marks_.capacity() * sizeof(std::uint32_t) +
		associated_scope_marks_.capacity() * sizeof(std::uint32_t) +
		associated_type_marks_.capacity() * sizeof(std::uint32_t) +
		candidate_marks_.capacity() * sizeof(std::uint32_t) +
		pack_alignment_stack_.capacity() * sizeof(std::size_t);
	for (std::size_t i = 0; i < functions_.size(); ++i)
		bytes += functions_[i].parameters.capacity() * sizeof(ParameterInfo);
	for (std::size_t i = 0; i < entity_data_members_.size(); ++i)
		bytes += entity_data_members_[i].capacity() * sizeof(BindingId);
	for (std::size_t i = 0; i < entity_layout_members_.size(); ++i)
		bytes += entity_layout_members_[i].capacity() *
			sizeof(ClassLayoutMember);
	for (std::size_t i = 0; i < entity_constructors_.size(); ++i)
		bytes += entity_constructors_[i].capacity() * sizeof(BindingId);
	for (std::size_t i = 0; i < entity_conversion_functions_.size(); ++i)
		bytes += entity_conversion_functions_[i].capacity() * sizeof(BindingId);
	for (std::size_t i = 0; i < scope_lifetimes_.size(); ++i)
		bytes += scope_lifetimes_[i].capacity() * sizeof(LifetimeObligation);
	for (std::size_t i = 0; i < aggregate_helpers_.size(); ++i)
		bytes += aggregate_helpers_[i].members.capacity() * sizeof(BindingId) +
			aggregate_helpers_[i].member_constructors.capacity() *
				sizeof(BindingId) +
			aggregate_helpers_[i].trivial_member_constructors.capacity() *
				sizeof(std::uint8_t);
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
		const EntityId source_entity = EntityOf(from);
		const EntityId target_entity = EntityOf(to);
		const bool derived_to_base = source_entity != kNoEntity &&
			target_entity != kNoEntity &&
			BaseConversionAllowed(source_entity, target_entity);
		const bool reference_related =
			SimilarUnqualified(EffectiveType(source), target_record.child) ||
			derived_to_base;
		if (!reference_related &&
			(!lvalue_reference || IsConst(target_record.child)))
		{
			const ConversionRank temporary = Conversion(source, category,
				integer_zero, target_record.child);
			if (temporary != CONVERSION_INVALID) return temporary;
		}
		if (derived_to_base)
		{
			const TypeRecord source_top = program_->types.Get(EffectiveType(source));
			const TypeRecord target_top = program_->types.Get(target_record.child);
			const std::uint8_t source_cv = source_top.kind == TYPE_QUALIFIED ?
				source_top.cv : CV_NONE;
			const std::uint8_t target_cv = target_top.kind == TYPE_QUALIFIED ?
				target_top.cv : CV_NONE;
			if ((source_cv & ~target_cv) == 0)
				return CONVERSION_DERIVED_TO_BASE;
		}
		if (IsArithmetic(from) && IsArithmetic(to) &&
			(IsConst(target_record.child) || !lvalue_reference))
			return lvalue_reference ? CONVERSION_BOOLEAN : CONVERSION_STANDARD;
		return CONVERSION_INVALID;
	}

	TypeId from = Decay(source);
	TypeId to = program_->types.RemoveTopCv(target);
	if (from == to) return CONVERSION_EXACT;
	const EntityId derived_object = EntityOf(from);
	const EntityId base_object = EntityOf(to);
	if (derived_object != kNoEntity && base_object != kNoEntity &&
		BaseConversionAllowed(derived_object, base_object))
		return CONVERSION_DERIVED_TO_BASE;
	if (IsNullptr(to) && integer_zero) return CONVERSION_STANDARD;
	if (IsPointer(to) && (IsNullptr(from) || integer_zero))
		return CONVERSION_STANDARD;
	if (IsPointer(from) && IsPointer(to))
	{
		const TypeRecord source_pointer = program_->types.Get(from);
		const TypeRecord target_pointer = program_->types.Get(to);
		const EntityId derived = EntityOf(source_pointer.child);
		const EntityId base = EntityOf(target_pointer.child);
		if (derived != kNoEntity && base != kNoEntity &&
			BaseConversionAllowed(derived, base))
		{
			const TypeRecord source_cv = program_->types.Get(source_pointer.child);
			const TypeRecord target_cv = program_->types.Get(target_pointer.child);
			const std::uint8_t scv = source_cv.kind == TYPE_QUALIFIED ?
				source_cv.cv : CV_NONE;
			const std::uint8_t tcv = target_cv.kind == TYPE_QUALIFIED ?
				target_cv.cv : CV_NONE;
			if ((scv & ~tcv) == 0) return CONVERSION_DERIVED_TO_BASE;
		}
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
	TypeId target, ConversionRank known_conversion)
{
	if (target == kNoType)
	{
		RecordExpressionFacts(value);
		return value;
	}
	const ConversionRank conversion = known_conversion == CONVERSION_INVALID ?
		Conversion(value, target) : known_conversion;
	if (conversion == CONVERSION_INVALID)
		throw std::runtime_error("invalid standard conversion from " +
			program_->RenderType(value.type) + " to " +
			program_->RenderType(target));
	const TypeRecord target_record = program_->types.Get(target);
	const TypeId nonreference = target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE ? target_record.child : target;
	const TypeId conversion_target =
		program_->types.RemoveTopCv(nonreference);
	const TypeId conversion_source =
		program_->types.RemoveTopCv(EffectiveType(value.type));
	const TypeRecord& conversion_target_record =
		program_->types.Get(conversion_target);
	const bool target_is_bool =
		conversion_target_record.kind == TYPE_FUNDAMENTAL &&
		conversion_target_record.fundamental == FUND_BOOL;
	const TypeRecord& conversion_source_record =
		program_->types.Get(conversion_source);
	const bool source_is_bool =
		conversion_source_record.kind == TYPE_FUNDAMENTAL &&
		conversion_source_record.fundamental == FUND_BOOL;
	if (target_is_bool && !source_is_bool)
		dump_.nodes[value.node].boolean_conversion = true;
	if (!target_is_bool && IsIntegral(conversion_source, true) &&
		IsIntegral(conversion_target, true) &&
		program_->SizeOf(conversion_target) < program_->SizeOf(conversion_source))
		dump_.nodes[value.node].integer_narrowing_conversion = true;
	if (conversion == CONVERSION_DERIVED_TO_BASE)
	{
		const bool binds_temporary =
			target_record.kind == TYPE_LVALUE_REFERENCE &&
			value.category != VALUE_LVALUE;
		if (binds_temporary && EntityOf(value.type) != kNoEntity &&
			dump_.nodes[value.node].kind != DUMP_TEMPORARY_OBJECT)
			value = MaterializeTemporary(value);
		const std::size_t projections =
			BaseProjectionCount(value.type, nonreference);
		if (projections == std::numeric_limits<std::size_t>::max() ||
			projections > std::numeric_limits<std::uint32_t>::max())
			throw std::logic_error(
				"derived conversion has no bounded base path");
		const ValueCategory category = binds_temporary ? VALUE_PRVALUE :
			target_record.kind == TYPE_LVALUE_REFERENCE ? VALUE_LVALUE :
			target_record.kind == TYPE_RVALUE_REFERENCE ?
			VALUE_XVALUE : VALUE_PRVALUE;
		const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION,
			nonreference, category);
		dump_.nodes[cast].base_projection_count =
			static_cast<std::uint32_t>(projections);
		dump_.Add(cast, value.node);
		value.node = cast;
		value.type = nonreference;
		value.category = category;
		value.binding = kNoBinding;
		value.constant = false;
		++expression_count_;
	}
	const bool reference_target = target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE;
	if (reference_target &&
		!SimilarUnqualified(EffectiveType(value.type), target_record.child) &&
		conversion != CONVERSION_DERIVED_TO_BASE)
	{
		const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION,
			nonreference, VALUE_PRVALUE);
		dump_.Add(cast, value.node);
		value.node = cast;
		value.type = nonreference;
		value.category = VALUE_PRVALUE;
		value.binding = kNoBinding;
		value.constant = false;
		++expression_count_;
	}
	if (value.integer_literal_zero &&
		(IsPointer(nonreference) || IsNullptr(nonreference)))
	{
		value.type = program_->types.RemoveTopCv(nonreference);
		dump_.nodes[value.node].type = value.type;
	}
	if (reference_target &&
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
ExpressionInfo SemanticAnalyzer::AnalyzeExpression(NodeId node, ScopeId scope,
	TypeId target)
{
	if (node == kNoNode) throw std::runtime_error("missing expression");
	ExpressionInfo prepared;
	if (ReusePreparedBracedExpression(node, target, &prepared)) return prepared;
	if (arena_->IsTag(node, "parenthesized-expression"))
		return AnalyzeExpression(FirstSemanticChild(node), scope, target);
	if (arena_->IsTag(node, "literal"))
	{
		const std::string spelling = arena_->Payload(node);
		ExpressionInfo result;
		if (!spelling.empty() && spelling[0] == '"')
		{
			if (TryAnalyzeUserDefinedStringLiteral(
				spelling, scope, target, &result)) return result;
			result = MakeStringLiteral(spelling);
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
		if (spelling == "this")
			result = AnalyzeThisExpression(scope);
		else if (spelling == "nullptr")
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
		EntityId function_naming_class = kNoEntity;
		std::vector<BindingId> candidates = FunctionCandidates(scope, spelling,
			&function_naming_class);
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
			if (!CanAccessMember(selected, function_naming_class))
				throw std::runtime_error("inaccessible member function");
			const FunctionInfo& function = GetFunction(selected);
			const BindingId emission_binding =
				program_->bindings[selected].canonical;
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
			result.binding = emission_binding;
			result.node = MakeDump(DUMP_ID_EXPRESSION, result.type,
				result.category, program_->names.Intern(spelling),
				emission_binding);
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
		if (!CanAccessMember(found.ordinary, found.naming_class))
			throw std::runtime_error("inaccessible member object");
		if (binding.non_static_data_member)
			return AnalyzeImplicitDataMember(found.ordinary, scope, target,
				found.naming_class);
		if (binding.member_owner != kNoEntity)
			EnsureStaticMemberStorage(found.ordinary);
		const std::uint32_t injected_fact =
			found.ordinary < injected_fact_by_binding_.size() ?
			injected_fact_by_binding_[found.ordinary] : kNoDumpEdge;
		if (injected_fact != kNoDumpEdge)
		{
			const InjectedMemberInfo& injected = injected_members_[injected_fact];
			const BindingRecord& storage =
				program_->bindings[injected.storage];
			const BindingRecord& member =
				program_->bindings[injected.member];
			const std::uint32_t storage_node = MakeDump(DUMP_ID_EXPRESSION,
				storage.type, VALUE_LVALUE, storage.name, injected.storage);
			const std::uint32_t member_node = MakeDump(DUMP_MEMBER_EXPRESSION,
				binding.type, VALUE_LVALUE, member.name, injected.member);
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
	if (arena_->IsTag(node, "type-trait-expression") &&
		PayloadSource(node) == "alignof")
		return ApplyTarget(AnalyzeSizeof(node, scope), target);
	if (arena_->IsTag(node, "braced-init-list"))
		return AnalyzeBracedInit(node, scope, target);
	if (arena_->IsTag(node, "new-expression")) return AnalyzeNewExpression(node, scope, target);
	if (arena_->IsTag(node, "delete-expression"))
		return AnalyzeDeleteExpression(node, scope, target);
	if (arena_->IsTag(node, "member-expression"))
		return ApplyTarget(AnalyzeMember(node, scope), target);
	throw std::runtime_error("unsupported PA12 expression: " + arena_->Tag(node));
}
BindingId SemanticAnalyzer::SelectOverload(ScopeId scope,
	const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>& arguments,
	const std::vector<BindingId>& candidates,
	const ExpressionInfo* object, ObjectConversionFact* object_conversion,
	std::vector<CallConversionFact>* argument_conversions)
{
	const std::size_t explicit_arity = argument_syntax.size();
	const std::size_t arity = explicit_arity + (object ? 1 : 0);
	if (arity != 0 && candidates.size() >
		std::numeric_limits<std::size_t>::max() / arity)
		throw std::runtime_error("overload conversion table is too large");
	std::vector<ConversionRank> ranks(candidates.size() * arity,
		CONVERSION_ELLIPSIS);
	std::vector<std::size_t> base_distances(candidates.size() * arity,
		std::numeric_limits<std::size_t>::max());
	std::vector<ConversionRank> actual_object_ranks(candidates.size(),
		CONVERSION_INVALID);
	std::vector<std::size_t> actual_object_distances(candidates.size(),
		std::numeric_limits<std::size_t>::max());
	std::vector<CallConversionFact> conversions(
		candidates.size() * explicit_arity);
	CallConversionTable conversion_cache;
	std::vector<bool> viable(candidates.size(), true);
	for (std::size_t c = 0; c < candidates.size(); ++c)
	{
		++overload_candidates_;
		const FunctionInfo& function = GetFunction(candidates[c]);
		const TypeRecord function_type = program_->types.Get(function.type);
		const std::size_t explicit_offset = object ? 1 : 0;
		if (function.member_owner != kNoType)
		{
			if (!object)
			{
				viable[c] = false;
				continue;
			}
			if (!RefQualifierViable(*object, function_type))
			{
				viable[c] = false;
				continue;
			}
			TypeId object_type = function.member_owner;
			if ((function_type.cv & CV_CONST) != 0)
				object_type = program_->types.Qualify(object_type, CV_CONST);
			if ((function_type.cv & CV_VOLATILE) != 0)
				object_type = program_->types.Qualify(object_type, CV_VOLATILE);
			const ConversionRank object_rank = MemberObjectConversion(*object,
				program_->types.Pointer(object_type), candidates[c]);
			actual_object_ranks[c] = object_rank;
			if (object_rank == CONVERSION_DERIVED_TO_BASE)
				actual_object_distances[c] = BaseConversionDistance(
					object->type, program_->types.Pointer(object_type));
			std::size_t selection_distance = actual_object_distances[c];
			const ConversionRank selection_rank =
				MemberCandidateSelectionRank(
					*object, candidates[c], object_rank, &selection_distance);
			ranks[c * arity] = selection_rank;
			if (selection_rank == CONVERSION_DERIVED_TO_BASE)
				base_distances[c * arity] = selection_distance;
			if (object_rank == CONVERSION_INVALID)
			{
				viable[c] = false;
				continue;
			}
		}
		else if (object)
			ranks[c * arity] = CONVERSION_EXACT;
		std::size_t required_parameters = function_type.parameter_count;
		while (required_parameters != 0 &&
			required_parameters <= function.parameters.size() &&
			function.parameters[required_parameters - 1].default_argument != kNoNode)
			--required_parameters;
		if (explicit_arity < required_parameters ||
			(!function_type.variadic &&
			 explicit_arity > function_type.parameter_count))
		{
			viable[c] = false;
			continue;
		}
		const TypeId* parameter_data = program_->types.Parameters(function.type);
		std::vector<TypeId> parameters;
		if (function_type.parameter_count != 0)
			parameters.assign(parameter_data,
				parameter_data + function_type.parameter_count);
		for (std::size_t a = 0; a < explicit_arity; ++a)
		{
			ConversionRank rank = CONVERSION_ELLIPSIS;
			if (a < function_type.parameter_count)
			{
				if (arguments[a].type != kNoType)
				{
					const CallConversionFact conversion = CallConversion(
						arguments[a], parameters[a], &conversion_cache, a);
					conversions[c * explicit_arity + a] = conversion;
					rank = conversion.rank;
				}
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
			ranks[c * arity + explicit_offset + a] = rank;
			if (rank == CONVERSION_DERIVED_TO_BASE)
				base_distances[c * arity + explicit_offset + a] =
					BaseConversionDistance(arguments[a].type, parameters[a]);
			if (rank == CONVERSION_INVALID) viable[c] = false;
		}
	}
	const auto better = [this, &ranks, &base_distances, &conversions,
		&candidates, &arguments, arity, explicit_arity, object](
		std::size_t left, std::size_t right) -> bool
	{
		++overload_order_comparisons_;
		bool no_worse = true;
		bool strictly_better = false;
		for (std::size_t a = 0; a < arity; ++a)
		{
			const ConversionRank left_rank = ranks[left * arity + a];
			const ConversionRank right_rank = ranks[right * arity + a];
			const std::size_t left_distance =
				base_distances[left * arity + a];
			const std::size_t right_distance =
				base_distances[right * arity + a];
			if (left_rank > right_rank ||
				(left_rank == right_rank &&
				 left_rank == CONVERSION_DERIVED_TO_BASE &&
				 left_distance > right_distance))
				no_worse = false;
			if (left_rank < right_rank ||
				(left_rank == right_rank &&
				 left_rank == CONVERSION_DERIVED_TO_BASE &&
				 left_distance < right_distance))
				strictly_better = true;
			if (left_rank == CONVERSION_USER_DEFINED &&
				right_rank == CONVERSION_USER_DEFINED &&
				a >= (object ? 1u : 0u))
			{
				const std::size_t argument = a - (object ? 1u : 0u);
				const int preference = CompareCallConversions(
					conversions[left * explicit_arity + argument],
					conversions[right * explicit_arity + argument]);
				if (preference < 0) no_worse = false;
				if (preference > 0) strictly_better = true;
			}
		}
		if (!no_worse) return false;
		if (strictly_better) return true;
		const FunctionInfo& left_function = GetFunction(candidates[left]);
		const FunctionInfo& right_function = GetFunction(candidates[right]);
		const TypeRecord& left_type =
			program_->types.Get(left_function.type);
		const TypeRecord& right_type =
			program_->types.Get(right_function.type);
		const TypeId* left_parameters =
			program_->types.Parameters(left_function.type);
		const TypeId* right_parameters =
			program_->types.Parameters(right_function.type);
		for (std::size_t a = 0; a < explicit_arity; ++a)
		{
			if (a >= left_type.parameter_count ||
				a >= right_type.parameter_count)
				continue;
			const int preference = CompareReferenceBindings(
				arguments[a], left_parameters[a], right_parameters[a]);
			if (preference != 0) return preference > 0;
		}
		if (object && left_function.member_owner != kNoType &&
			right_function.member_owner != kNoType)
		{
			const int preference = CompareImplicitObjectBindings(
				object->category, program_->types.Get(left_function.type),
				program_->types.Get(right_function.type));
			if (preference != 0) return preference > 0;
		}
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
	if (viable_count != 1)
		for (std::size_t other = 0; other < candidates.size(); ++other)
		{
			if (other == champion || !viable[other]) continue;
			if (!better(champion, other))
				throw std::runtime_error("ambiguous overload");
		}
	if (object_conversion && object)
	{
		object_conversion->rank = actual_object_ranks[champion];
		if (object_conversion->rank == CONVERSION_DERIVED_TO_BASE)
		{
			const std::size_t projections = actual_object_distances[champion];
			if (projections == std::numeric_limits<std::size_t>::max() ||
				projections > std::numeric_limits<std::uint32_t>::max())
				throw std::logic_error(
					"selected object conversion has no bounded base path");
			object_conversion->base_projection_count = projections == 0 ? 0 : 1;
		}
	}
	if (argument_conversions)
	{
		argument_conversions->clear();
		argument_conversions->reserve(explicit_arity);
		for (std::size_t a = 0; a < explicit_arity; ++a)
			argument_conversions->push_back(
				conversions[champion * explicit_arity + a]);
	}
	return candidates[champion];
}

ExpressionInfo SemanticAnalyzer::BuildResolvedCall(BindingId selected,
	ScopeId scope, const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>& arguments,
	const ExpressionInfo* object, TypeId target, EntityId naming_class,
	const ObjectConversionFact* object_conversion,
	const std::vector<CallConversionFact>* argument_conversions)
{
	if (GetFunction(selected).deleted_special_member)
		throw std::runtime_error("selected special member is deleted");
	EntityId object_class = kNoEntity;
	if (object)
	{
		TypeId object_type = program_->types.RemoveTopCv(
			EffectiveType(object->type));
		const TypeRecord object_shape = program_->types.Get(object_type);
		if (object_shape.kind == TYPE_POINTER) object_type = object_shape.child;
		object_class = EntityOf(object_type);
	}
	if (!CanAccessMember(selected, naming_class, object_class))
		throw std::runtime_error("inaccessible member function");
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
	const TypeId callable_type = function.member_owner == kNoType ?
		function.type : AdaptMemberFunctionType(selected);
	const BindingId emission_binding = program_->bindings[selected].canonical;
	const std::uint32_t call = MakeDump(DUMP_CALL_EXPRESSION,
		result_type, category, 0, emission_binding);
	dump_.nodes[call].user_conversion_call = function.conversion_function;
	dump_.nodes[call].explicit_user_conversion_call =
		function.conversion_function && function.explicit_conversion;
	const std::uint32_t callee = MakeDump(DUMP_CALLEE, callable_type,
		VALUE_NONE, function.display_name, emission_binding);
	dump_.Add(call, callee);
	if (function.member_owner != kNoType)
	{
		if (!object) throw std::logic_error("selected member call has no object");
		const TypeId object_parameter =
			program_->types.Parameters(callable_type)[0];
		const ExpressionInfo converted = ApplyMemberObjectTarget(
			*object, object_parameter, selected, object_conversion);
		dump_.Add(call, converted.node);
	}
	for (std::size_t a = 0; a < arguments.size(); ++a)
	{
		ExpressionInfo argument = arguments[a];
		if (a < function_type.parameter_count)
		{
			if (argument.type == kNoType)
				argument = AnalyzeExpression(argument_syntax[a], scope,
					parameters[a]);
			else argument = ApplyCallArgument(argument, parameters[a],
				argument_conversions && a < argument_conversions->size() ?
				&(*argument_conversions)[a] : 0);
		}
		dump_.Add(call, argument.node);
	}
	for (std::size_t a = arguments.size();
		a < function_type.parameter_count; ++a)
	{
		if (a >= function.parameters.size() ||
			function.parameters[a].default_argument == kNoNode)
			throw std::runtime_error("missing default argument fact");
		ExpressionInfo argument = AnalyzeExpression(
			function.parameters[a].default_argument,
			function.parameters[a].default_scope, parameters[a]);
		argument = ApplyCallArgument(argument, parameters[a]);
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

ExpressionInfo SemanticAnalyzer::AnalyzeCall(NodeId node, ScopeId scope,
	TypeId target)
{
	const NodeId callee_syntax = FirstSemanticChild(node);
	if (callee_syntax == kNoNode) throw std::runtime_error("call without callee");
	NodeId direct_callee_syntax = callee_syntax;
	bool parenthesized_callee = false;
	while (arena_->IsTag(direct_callee_syntax, "parenthesized-expression"))
	{
		parenthesized_callee = true;
		direct_callee_syntax = FirstSemanticChild(direct_callee_syntax);
		if (direct_callee_syntax == kNoNode)
			throw std::runtime_error("empty parenthesized callee");
	}
	NodeId arguments_node = FindChild(node, "argument-list");
	if (arguments_node == kNoNode)
		arguments_node = FindChild(node, "braced-init-list");
	if (arguments_node == kNoNode)
	{
		std::uint32_t edge = arena_->FirstEdge(node);
		if (edge != kNoEdge) edge = arena_->NextEdge(edge);
		if (edge != kNoEdge) arguments_node = arena_->EdgeChild(edge);
	}
	std::vector<NodeId> argument_syntax;
	if (arguments_node != kNoNode)
		for (std::uint32_t argument = arena_->FirstEdge(arguments_node);
			argument != kNoEdge; argument = arena_->NextEdge(argument))
			argument_syntax.push_back(arena_->EdgeChild(argument));
	std::vector<ExpressionInfo> analyzed_arguments;
	bool arguments_analyzed = false;

	ExpressionInfo member_call;
	if (AnalyzeExplicitDestructorCall(callee_syntax, scope, argument_syntax,
		target, &member_call)) return member_call;
	if (AnalyzeDirectMemberCall(callee_syntax, scope, argument_syntax,
		target, &member_call)) return member_call;

	if (arena_->IsTag(direct_callee_syntax, "id-expression"))
	{
		const std::string spelling = arena_->Payload(direct_callee_syntax);
		ExpressionInfo builtin;
		if (AnalyzeBuiltinCall(spelling, scope, argument_syntax, target, &builtin))
			return builtin;
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

		EntityId function_naming_class = kNoEntity;
		std::vector<BindingId> candidates = FunctionCandidates(scope, spelling,
			&function_naming_class);
		for (std::size_t i = 0; i < argument_syntax.size(); ++i)
			analyzed_arguments.push_back(
				AnalyzeExpression(argument_syntax[i], scope));
		arguments_analyzed = true;
		if (!FindFunctionTemplates(scope, spelling).empty())
		{
			DeduceFunctionTemplates(scope, spelling, analyzed_arguments);
			candidates = FunctionCandidates(scope, spelling,
				&function_naming_class);
		}
		if (!parenthesized_callee &&
			spelling.find("::") == std::string::npos)
		{
			BeginCandidateCollection();
			std::vector<BindingId> combined;
			bool suppress_adl = false;
			for (std::size_t i = 0; i < candidates.size(); ++i)
			{
				AddCandidate(candidates[i], &combined);
				if (GetFunction(candidates[i]).member_owner != kNoType)
					suppress_adl = true;
			}
			if (!suppress_adl)
				AppendArgumentDependentCandidates(program_->names.Intern(spelling),
					analyzed_arguments, &combined);
			candidates.swap(combined);
		}
		if (!candidates.empty())
		{
			bool has_member_candidate = false;
			for (std::size_t i = 0; i < candidates.size(); ++i)
				if (GetFunction(candidates[i]).member_owner != kNoType)
					has_member_candidate = true;
			ExpressionInfo implicit_object;
			const ExpressionInfo* object = 0;
			if (has_member_candidate)
			{
				const NameId this_name = program_->names.Intern("this");
				const LookupResult found_this = program_->LookupName(
					scope, this_name, LOOKUP_ORDINARY);
				if (found_this.ordinary != kNoBinding)
				{
					const BindingRecord& this_binding =
						program_->bindings[found_this.ordinary];
					implicit_object.type = EffectiveType(this_binding.type);
					implicit_object.category = VALUE_LVALUE;
					implicit_object.binding = found_this.ordinary;
					implicit_object.node = MakeDump(DUMP_ID_EXPRESSION,
						implicit_object.type, VALUE_LVALUE, this_name,
						found_this.ordinary);
					object = &implicit_object;
					++expression_count_;
				}
			}
			ObjectConversionFact object_conversion;
			std::vector<CallConversionFact> argument_conversions;
			const BindingId selected = SelectOverload(scope, argument_syntax,
				analyzed_arguments, candidates, object,
				object ? &object_conversion : 0, &argument_conversions);
			return BuildResolvedCall(selected, scope, argument_syntax,
				analyzed_arguments, object, target, function_naming_class,
				object ? &object_conversion : 0, &argument_conversions);
		}

		const TypeId cast_type = ResolveFunctionalCastType(scope, spelling);
		if (cast_type != kNoType)
		{
			if (DestructedEntity(cast_type) != kNoEntity)
				return AnalyzeClassFunctionalCast(cast_type, scope,
					argument_syntax, arguments_node, target);
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
			ExpressionInfo operand = analyzed_arguments[0];
			if (EntityOf(operand.type) != kNoEntity &&
				ConvertingFunction(operand, cast_type, true).rank !=
					CONVERSION_INVALID)
				return ApplyTarget(
					ApplyExplicitConversion(operand, cast_type), target);
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

	}

	ExpressionInfo callee = AnalyzeExpression(callee_syntax, scope);
	if (!arguments_analyzed)
	{
		for (std::size_t i = 0; i < argument_syntax.size(); ++i)
			analyzed_arguments.push_back(
				AnalyzeExpression(argument_syntax[i], scope));
		arguments_analyzed = true;
	}
	ExpressionInfo call_operator;
	if (TryAnalyzeCallOperator(scope, callee, argument_syntax,
		&analyzed_arguments, target, &call_operator))
		return call_operator;
	if (TryAnalyzeCallSurrogate(scope, callee, analyzed_arguments,
		target, &call_operator))
		return call_operator;
	TypeId function_type = program_->types.RemoveTopCv(
		EffectiveType(callee.type));
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
		ExpressionInfo argument = arguments_analyzed ? analyzed_arguments[a] :
			AnalyzeExpression(argument_syntax[a], scope);
		if (a < callable.parameter_count)
			argument = ApplyTarget(argument, parameters[a]);
		dump_.Add(call, argument.node);
	}
	ExpressionInfo result;
	result.node = call;
	result.type = result_type;
	result.category = category;
	++expression_count_;
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
	const NodeId operand_syntax = FirstSemanticChild(node);
	ExpressionInfo operand = AnalyzeExpression(operand_syntax, scope,
		operand_target);
	std::vector<NodeId> overloaded_syntax(1, operand_syntax);
	std::vector<ExpressionInfo> overloaded_operands(1, operand);
	if (postfix && (operation == "++" || operation == "--"))
	{
		ExpressionInfo dummy = MakeLiteral(
			program_->types.Fundamental(FUND_INT), program_->names.Intern("0"));
		dummy.constant = true;
		dummy.value = 0;
		dummy.integer_literal_zero = true;
		overloaded_syntax.push_back(kNoNode);
		overloaded_operands.push_back(dummy);
	}
	ExpressionInfo overloaded;
	if (TryAnalyzeOverloadedOperator(operation, scope, overloaded_syntax,
		overloaded_operands, false, target, &overloaded)) return overloaded;
	(void)ApplyBuiltinUnaryConversion(operation, &operand);
	if (operation == "&" && operand.binding != kNoBinding &&
		program_->bindings[operand.binding].bit_field)
		throw std::runtime_error("address-of bit-field unsupported");
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
	const NodeId left_syntax = arena_->EdgeChild(first_edge);
	const NodeId right_syntax = arena_->EdgeChild(second_edge);
	ExpressionInfo left = AnalyzeExpression(left_syntax, scope);
	ExpressionInfo right = AnalyzeExpression(right_syntax, scope);
	const std::string operation = PayloadSource(node);
	std::vector<NodeId> overloaded_syntax;
	overloaded_syntax.push_back(left_syntax);
	overloaded_syntax.push_back(right_syntax);
	std::vector<ExpressionInfo> overloaded_operands;
	overloaded_operands.push_back(left);
	overloaded_operands.push_back(right);
	std::vector<ConversionRank> builtin_ranks;
	const bool builtin_viable = ApplyBuiltinBinaryConversions(operation,
		&left, &right, &builtin_ranks, false);
	ExpressionInfo overloaded;
	if (TryAnalyzeOverloadedOperator(operation, scope, overloaded_syntax,
		overloaded_operands, false, kNoType, &overloaded,
		builtin_viable ? &builtin_ranks : 0)) return overloaded;
	(void)ApplyBuiltinBinaryConversions(operation, &left, &right);
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
		else if (IsNullptr(left.type) && IsNullptr(right.type) && equality)
			operand_type = left_unqualified;
		else if (IsPointer(Decay(left.type)) && IsPointer(Decay(right.type)))
		{
			const TypeId left_pointer = Decay(left.type);
			const TypeId right_pointer = Decay(right.type);
			const ConversionRank right_to_left = Conversion(right, left_pointer);
			const ConversionRank left_to_right = Conversion(left, right_pointer);
			if (right_to_left != CONVERSION_INVALID &&
				(left_to_right == CONVERSION_INVALID ||
				 right_to_left <= left_to_right))
			{
				operand_type = left_pointer;
				right = ApplyTarget(right, left_pointer);
			}
			else if (left_to_right != CONVERSION_INVALID)
			{
				operand_type = right_pointer;
				left = ApplyTarget(left, right_pointer);
			}
			else throw std::runtime_error("invalid pointer comparison operands");
		}
		else if (IsPointer(Decay(left.type)) &&
			((right.integer_literal_zero && equality) || IsNullptr(right.type))) {}
		else if (IsPointer(Decay(right.type)) &&
			((left.integer_literal_zero && equality) || IsNullptr(left.type))) {}
		else throw std::runtime_error("invalid comparison operands");
		result_type = program_->types.Fundamental(FUND_BOOL);
	}
	else if (operation == ",")
	{
		left = MaterializeDiscardedClassResult(left);
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
	const NodeId left_syntax = arena_->EdgeChild(first);
	const NodeId right_syntax = arena_->EdgeChild(second);
	ExpressionInfo left = AnalyzeExpression(left_syntax, scope);
	const std::string operation = PayloadSource(node);
	ExpressionInfo right = AnalyzeExpression(right_syntax, scope,
		operation == "=" && arena_->IsTag(right_syntax, "braced-init-list") ?
			EffectiveType(left.type) : kNoType);
	std::vector<NodeId> overloaded_syntax;
	overloaded_syntax.push_back(left_syntax);
	overloaded_syntax.push_back(right_syntax);
	std::vector<ExpressionInfo> overloaded_operands;
	overloaded_operands.push_back(left);
	overloaded_operands.push_back(right);
	ExpressionInfo overloaded;
	if (TryAnalyzeOverloadedOperator(operation, scope, overloaded_syntax,
		overloaded_operands, operation == "=", kNoType, &overloaded))
		return overloaded;
	(void)ApplyBuiltinAssignmentConversion(operation, left, &right);
	if (operation == "=") right = ApplyTarget(right, EffectiveType(left.type));
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

ExpressionInfo SemanticAnalyzer::AnalyzeSubscript(NodeId node, ScopeId scope)
{
	const std::uint32_t first = arena_->FirstEdge(node);
	const std::uint32_t second = first == kNoEdge ? kNoEdge :
		arena_->NextEdge(first);
	if (second == kNoEdge) throw std::runtime_error("invalid subscript");
	const NodeId left_syntax = arena_->EdgeChild(first);
	const NodeId right_syntax = arena_->EdgeChild(second);
	ExpressionInfo left = AnalyzeExpression(left_syntax, scope);
	ExpressionInfo right = AnalyzeExpression(right_syntax, scope);
	std::vector<NodeId> overloaded_syntax;
	overloaded_syntax.push_back(left_syntax);
	overloaded_syntax.push_back(right_syntax);
	std::vector<ExpressionInfo> overloaded_operands;
	overloaded_operands.push_back(left);
	overloaded_operands.push_back(right);
	ExpressionInfo overloaded;
	if (TryAnalyzeOverloadedOperator("[]", scope, overloaded_syntax,
		overloaded_operands, true, kNoType, &overloaded)) return overloaded;
	(void)ApplyBuiltinBinaryConversions("[]", &left, &right);
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
	else
	{
		++unevaluated_depth_;
		try
		{
			measured = AnalyzeExpression(operand, scope).type;
		}
		catch (...)
		{
			--unevaluated_depth_;
			throw;
		}
		--unevaluated_depth_;
	}
	const bool alignment_query = arena_->IsTag(node, "type-trait-expression");
	const std::size_t value = alignment_query ? program_->AlignOf(measured) :
		program_->SizeOf(measured);
	ExpressionInfo result;
	result.type = program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	result.node = MakeDump(DUMP_SIZEOF_EXPRESSION, result.type, VALUE_PRVALUE);
	result.constant = true;
	result.value = static_cast<std::int64_t>(value);
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
	if (source_operation == "." && object.category == VALUE_PRVALUE &&
		EntityOf(object.type) != kNoEntity &&
		dump_.nodes[object.node].kind != DUMP_TEMPORARY_OBJECT)
		object = MaterializeTemporary(object);
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
	const NameId name = ParseNamePath(arena_->Payload(identifier)).Last();
	const LookupResult found = program_->LookupMember(
		entity, name, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding)
		throw std::runtime_error("unknown class member");
	if (!CanAccessMember(found.ordinary, found.naming_class, entity))
		throw std::runtime_error("inaccessible class member");
	const EntityId member_owner =
		program_->bindings[found.ordinary].member_owner;
	const BindingRecord& member_binding =
		program_->bindings[found.ordinary];
	TypeId type = member_binding.type;
	if (IsConst(owner_type) && !member_binding.mutable_member)
		type = program_->types.Qualify(type, CV_CONST);
	if (!member_binding.non_static_data_member)
		EnsureStaticMemberStorage(found.ordinary);
	std::string operation = arena_->Payload(node);
	const std::size_t colon = operation.find(':');
	if (colon != std::string::npos) operation.erase(colon + 1);
	operation += program_->names.Get(name);
	ValueCategory member_category = VALUE_LVALUE;
	if (source_operation != "->" && member_binding.non_static_data_member &&
		object.category != VALUE_LVALUE &&
		!program_->types.IsReference(member_binding.type))
		member_category = VALUE_XVALUE;
	const std::uint32_t expression = MakeDump(DUMP_MEMBER_EXPRESSION,
		type, member_category, program_->names.Intern(operation), found.ordinary);
	if (member_owner != kNoEntity)
	{
		const std::size_t projections = BaseProjectionCount(owner_type,
			program_->entities[member_owner].type);
		if (projections == std::numeric_limits<std::size_t>::max() ||
			projections > std::numeric_limits<std::uint32_t>::max())
			throw std::logic_error("member has no bounded base path");
		dump_.nodes[expression].base_projection_count =
			static_cast<std::uint32_t>(projections);
	}
	dump_.Add(expression, object.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = type;
	result.category = member_category;
	result.binding = found.ordinary;
	const BindingRecord& canonical = program_->bindings[
		program_->bindings[found.ordinary].canonical];
	result.constant = canonical.constant;
	result.value = canonical.value;
	dump_.nodes[expression].constant = result.constant;
	dump_.nodes[expression].constant_value = result.value;
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
	program_->SetScopeEmissionName(child, unnamed ?
		program_->names.Intern("_GLOBAL__N_1") : name);
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
	if (arena_->IsTag(node, "layout-pack-push"))
	{
		const std::int64_t parsed = ParseInteger(arena_->Payload(node));
		if (parsed <= 0 ||
			(static_cast<std::uint64_t>(parsed) &
			 (static_cast<std::uint64_t>(parsed) - 1)) != 0)
			throw std::runtime_error("invalid layout packing alignment");
		pack_alignment_stack_.push_back(current_pack_alignment_);
		current_pack_alignment_ = static_cast<std::size_t>(parsed);
		return;
	}
	if (arena_->IsTag(node, "layout-pack-pop"))
	{
		if (pack_alignment_stack_.empty()) current_pack_alignment_ = 0;
		else
		{
			current_pack_alignment_ = pack_alignment_stack_.back();
			pack_alignment_stack_.pop_back();
		}
		return;
	}
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
	if (arena_->IsTag(node, "special-member-definition") ||
		arena_->IsTag(node, "special-member-declaration"))
	{
		AnalyzeOutOfClassSpecialMember(node, scope);
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
				if (program_->LookupDirect(scope, source.name,
					LOOKUP_ORDINARY).ordinary != kNoBinding)
					throw std::runtime_error(
						"anonymous union member conflicts in block scope");
				const BindingId injected = program_->AddBinding(scope, BIND_VARIABLE,
					source.name, source.type, source.constant, source.value,
					source.display_flavor, source.display_type_name);
				if (injected_fact_by_binding_.size() <= injected)
					injected_fact_by_binding_.resize(
						static_cast<std::size_t>(injected) + 1, kNoDumpEdge);
				injected_fact_by_binding_[injected] =
					static_cast<std::uint32_t>(injected_members_.size());
				injected_members_.push_back(
					InjectedMemberInfo(storage, members[i]));
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
	if (local && AnalyzeQualifiedAssignmentStatement(
		node, scope, output_parent))
		return;
	const NodeId specifiers = FindChild(node, "decl-specifier-seq");
	const NodeId list = FindChild(node, "init-declarator-list");
	std::string hint;
	EntityId declaration_class_context = kNoEntity;
	if (list != kNoNode)
	{
		const NodeId first = FirstSemanticChild(list);
		const NodeId declarator = first == kNoNode ? kNoNode :
			FindChild(first, "declarator");
		if (declarator != kNoNode && DeclaratorName(declarator) != 0)
			hint = program_->names.Get(DeclaratorName(declarator));
		if (declarator != kNoNode)
		{
			NamePath owner_path = DeclaratorNamePath(declarator);
			if (owner_path.global || owner_path.Size() > 1)
			{
				owner_path.Pop();
				const LookupResult owner_type = LookupPath(scope, owner_path,
					LOOKUP_TYPE);
				if (owner_type.type != kNoType)
					declaration_class_context = EntityOf(owner_type.type);
			}
		}
	}
	const EntityId previous_class_context = current_class_context_;
	if (declaration_class_context != kNoEntity)
		current_class_context_ = declaration_class_context;
	const SpecInfo spec = BuildSpecifiers(specifiers, scope, hint,
		list != kNoNode);
	if (list == kNoNode)
	{
		if (local)
		{
			const std::uint32_t empty = MakeDump(DUMP_SIMPLE_DECLARATION);
			dump_.Add(output_parent, empty);
		}
		current_class_context_ = previous_class_context;
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
		const NamePath declared_path = DeclaratorNamePath(declarator);
		const ScopeId declaration_scope =
			declared_path.global || declared_path.Size() > 1 ?
				ResolveOwner(scope, declared_path) : scope;
		if (declaration_scope == kNoScope)
			throw std::runtime_error("variable owner not found");
		DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type,
			declaration_scope);
		parsed.name = declared_path.Last();
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
			if (spec.thread_local_storage)
				throw std::runtime_error("thread_local function");
			const BindingId function = DeclareFunction(declaration_scope, parsed.name,
				parsed.type, parsed.parameters, false, false, spec.storage_class,
				current_language_linkage_, IsNonthrowing(declarator, scope));
			ValidateFunctionRefQualifier(function);
			ValidateNonmemberOperator(function);
			const NodeId function_initializer = FindChild(item, "initializer");
			ConfigureAssignmentSpecialMember(function, function_initializer,
				!declared_path.global && declared_path.Size() <= 1);
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
			program_->LookupDirect(declaration_scope, parsed.name, LOOKUP_ORDINARY);
		if (occupied.ordinary != kNoBinding &&
			program_->bindings[occupied.ordinary].kind == BIND_FUNCTION)
			throw std::runtime_error("variable conflicts with function binding");
		const BindingId binding = program_->AddBinding(declaration_scope,
			BIND_VARIABLE,
			parsed.name, parsed.type);
		PublishVariableDeclarationFacts(binding, declaration_scope,
			parsed.name, parsed.type, spec, local);
		const NodeId initializer_node = FindChild(item, "initializer");
		if (spec.is_constexpr && initializer_node == kNoNode)
			throw std::runtime_error("constexpr variable requires initializer");
		ExpressionInfo initializer;
		const bool has_initializer = initializer_node != kNoNode;
		if (initializer_node != kNoNode)
		{
			initializer = AnalyzeVariableInitializer(initializer_node,
				declaration_scope, parsed.type, local);
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
		if (program_->bindings[binding].constant)
		{
			BindingRecord& canonical = program_->bindings[
				program_->bindings[binding].canonical];
			canonical.constant = true;
			canonical.value = program_->bindings[binding].value;
		}
		const bool declaration_only = !local && !has_initializer &&
			program_->bindings[binding].storage_class == STORAGE_CLASS_EXTERN;
		const std::uint32_t variable = MakeDump(DUMP_VARIABLE, parsed.type,
			VALUE_NONE, parsed.name, binding);
		if (has_initializer) dump_.Add(variable, initializer.node);
		else if (!declaration_only && DestructedEntity(parsed.type) != kNoEntity)
			AddDefaultConstructor(variable, binding, parsed.type);
		dump_.Add(owner, variable);
		if (local && program_->bindings[binding].storage_class ==
			STORAGE_CLASS_NONE)
			AddLifetimeObligation(scope, binding, parsed.type);
		else if (!local && !declaration_only)
		{
			const std::uint32_t initializer_action =
				dump_.nodes[variable].first_edge == kNoDumpEdge ? kNoDumpEdge :
				dump_.edges[dump_.nodes[variable].first_edge].child;
			AddNamespaceObjectAction(variable, binding, parsed.type,
				initializer_action);
		}
		if (local && has_initializer)
		{
			const TypeKind declared_kind = program_->types.Get(parsed.type).kind;
			const bool control_dependent =
				HasControlDependentTemporary(initializer.node);
			if ((declared_kind == TYPE_LVALUE_REFERENCE ||
					 declared_kind == TYPE_RVALUE_REFERENCE) &&
				!control_dependent)
			{
				std::vector<std::uint32_t> temporaries;
				CollectTemporaryObjects(initializer.node, &temporaries);
				if (!temporaries.empty())
				{
					AddTemporaryLifetimeObligation(scope, temporaries.back());
					for (std::size_t i = temporaries.size() - 1; i != 0; --i)
					{
						const std::uint32_t action =
							MakeTemporaryDestructorAction(temporaries[i - 1]);
						if (action != kNoDumpEdge) dump_.Add(owner, action);
					}
				}
			}
			else
			{
				const std::size_t edge_count = dump_.edges.size();
				AppendFullExpressionDestructionActions(initializer.node, owner);
				if (control_dependent && dump_.edges.size() != edge_count &&
					!InitializationActionsAreNonthrowing(initializer.node))
				{
					dump_.nodes[owner].full_expression_staging = true;
					MarkFullExpressionCalls(initializer.node);
				}
			}
		}
	}
	current_class_context_ = previous_class_context;
}


void SemanticAnalyzer::AnalyzeFunction(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	const NodeId declarator = FindChild(node, "declarator");
	const NamePath path = DeclaratorNamePath(declarator);
	const ScopeId owner = ResolveOwner(scope, path);
	if (owner == kNoScope) throw std::runtime_error("function owner not found");
	const EntityId previous_class = current_class_context_;
	const EntityId declaration_class = program_->EntityForScope(owner);
	if (declaration_class != kNoEntity)
		current_class_context_ = declaration_class;
	const SpecInfo spec = BuildSpecifiers(FindChild(node, "decl-specifier-seq"),
		owner, std::string(), true);
	DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, owner);
	parsed.name = path.Last();
	if (!program_->types.IsFunction(parsed.type))
		throw std::runtime_error("function definition has non-function type");
	const BindingId binding = DeclareFunction(owner, parsed.name,
		parsed.type, parsed.parameters, true, false, spec.storage_class,
		current_language_linkage_, IsNonthrowing(declarator, owner));
	ValidateFunctionRefQualifier(binding);
	ValidateNonmemberOperator(binding);
	FunctionInfo& function = GetMutableFunction(binding);
	function.deferred = false;
	const bool member = function.member_owner != kNoType;
	const TypeId output_type = member ?
		AdaptMemberFunctionType(binding) : parsed.type;
	const std::uint32_t output_node = MakeDump(DUMP_FUNCTION_DEFINITION,
		output_type, VALUE_NONE, function.display_name, binding);
	dump_.Add(output_parent, output_node);
	const ScopeId function_scope = NewScope(owner, SCOPE_FUNCTION, parsed.name,
		ScopePrefixId(owner));
	if (member)
	{
		const TypeId this_type = program_->types.Parameters(output_type)[0];
		const NameId this_name = program_->names.Intern("this");
		const BindingId this_binding = program_->AddBinding(function_scope,
			BIND_PARAMETER, this_name, this_type);
		dump_.Add(output_node, MakeDump(DUMP_PARAMETER, this_type,
			VALUE_NONE, this_name, this_binding));
	}
	for (std::size_t i = 0; i < parsed.parameters.size(); ++i)
	{
		ParameterInfo parameter = parsed.parameters[i];
		if (parameter.name == 0 && i < function.parameters.size())
			parameter.name = function.parameters[i].name;
		const BindingId parameter_binding = program_->AddBinding(function_scope,
			BIND_PARAMETER, parameter.name, parameter.declared_type);
		const std::uint32_t parameter_node = MakeDump(DUMP_PARAMETER,
			parameter.function_type, VALUE_NONE, parameter.name, parameter_binding);
		dump_.Add(output_node, parameter_node);
	}
	const TypeId previous_return = current_return_type_;
	const BindingId previous_function = current_function_context_;
	current_return_type_ = program_->types.Get(parsed.type).child;
	current_class_context_ = function.friend_of != kNoEntity ?
		function.friend_of : program_->bindings[binding].member_owner;
	current_function_context_ = program_->bindings[binding].canonical;
	const NodeId body = FindChild(node, "compound-statement");
	if (body != kNoNode)
	{
		AnalyzeCompound(body, function_scope, output_node);
		FinalizeNamedReturnSlot(output_node);
	}
	current_return_type_ = previous_return;
	current_class_context_ = previous_class;
	current_function_context_ = previous_function;
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
	AppendScopeDestructionActions(block, compound, scope);
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
		AppendScopeDestructionActions(child, output_parent, scope);
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
		ExpressionInfo value = AnalyzeVariableInitializer(initializer,
			scope, parsed.type, true);
		const std::uint32_t declaration = MakeDump(DUMP_CONDITION_DECLARATION);
		const std::uint32_t variable = MakeDump(DUMP_VARIABLE, parsed.type,
			VALUE_NONE, parsed.name, binding);
		dump_.Add(variable, value.node);
		dump_.Add(declaration, variable);
		dump_.Add(condition, declaration);
		if (switch_condition)
		{
			if (!IsIntegral(parsed.type, true) &&
				EntityOf(parsed.type) == kNoEntity)
				throw std::runtime_error("invalid switch condition");
			if (!IsIntegral(parsed.type, true))
			{
				ExpressionInfo declared;
				declared.node = MakeDump(DUMP_ID_EXPRESSION, parsed.type,
					VALUE_LVALUE, parsed.name, binding);
				declared.type = parsed.type;
				declared.category = VALUE_LVALUE;
				declared.binding = binding;
				++expression_count_;
				const ExpressionInfo converted = ApplyExplicitConversion(declared,
					program_->types.Fundamental(FUND_INT));
				dump_.Add(condition, converted.node);
			}
		}
		else if (!IsArithmetic(parsed.type) && !IsPointer(parsed.type))
		{
			ExpressionInfo declared;
			declared.node = MakeDump(DUMP_ID_EXPRESSION, parsed.type,
				VALUE_LVALUE, parsed.name, binding);
			declared.type = parsed.type;
			declared.category = VALUE_LVALUE;
			declared.binding = binding;
			++expression_count_;
			const ExpressionInfo converted = ApplyExplicitConversion(declared,
				program_->types.Fundamental(FUND_BOOL));
			dump_.Add(condition, converted.node);
		}
		RegisterConditionLifetime(scope, binding, parsed.type, value, condition);
		return;
	}
	ExpressionInfo value = AnalyzeExpression(FirstSemanticChild(node), scope);
	if (switch_condition)
	{
		if (!IsIntegral(value.type, true))
			throw std::runtime_error("invalid switch condition");
	}
	else if (!IsArithmetic(value.type) && !IsPointer(value.type) &&
		!IsNullptr(value.type))
		value = ApplyExplicitConversion(value,
			program_->types.Fundamental(FUND_BOOL));
	dump_.Add(condition, value.node);
	AppendFullExpressionDestructionActions(value.node, condition);
	const std::uint32_t first = dump_.nodes[condition].first_edge;
	if (first != kNoDumpEdge && dump_.edges[first].next != kNoDumpEdge)
	{
		MarkFullExpressionCalls(value.node);
		AppendUnwindDestructionActions(scope, condition);
	}
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
		AnalyzeReturnStatement(node, scope, output_parent);
		return;
	}
	if (arena_->IsTag(node, "expression-statement"))
	{
		const std::uint32_t statement = MakeDump(DUMP_EXPRESSION_STATEMENT);
		dump_.Add(output_parent, statement);
		const NodeId expression = FirstSemanticChild(node);
		if (expression != kNoNode)
		{
			ExpressionInfo value = MaterializeDiscardedClassResult(
				AnalyzeExpression(expression, scope));
			dump_.Add(statement, value.node);
			AppendFullExpressionDestructionActions(value.node, statement);
		}
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
		AppendScopeDestructionActions(control, output_parent, scope);
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
		break_cleanup_stops_.push_back(control);
		continue_cleanup_stops_.push_back(control);
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "condition"))
				AnalyzeCondition(child, control, statement, false);
			else AnalyzeSubstatement(child, control, statement);
		}
		continue_cleanup_stops_.pop_back();
		break_cleanup_stops_.pop_back();
		--loop_depth_;
		AppendScopeDestructionActions(control, output_parent, scope);
		return;
	}
	if (arena_->IsTag(node, "for-statement"))
	{
		const ScopeId control = NewScope(scope, SCOPE_BLOCK, 0,
			ScopePrefixId(scope));
		const std::uint32_t statement = MakeDump(DUMP_FOR_STATEMENT);
		dump_.Add(output_parent, statement);
		++loop_depth_;
		break_cleanup_stops_.push_back(control);
		continue_cleanup_stops_.push_back(control);
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
					else dump_.Add(init, MaterializeDiscardedClassResult(
						AnalyzeExpression(value, control)).node);
					if (!IsDeclaration(value) &&
						dump_.nodes[init].first_edge != kNoDumpEdge)
					{
						const std::uint32_t expression =
							dump_.edges[dump_.nodes[init].first_edge].child;
						const std::size_t edge_count = dump_.edges.size();
						AppendFullExpressionDestructionActions(
							expression, init);
						if (dump_.edges.size() != edge_count)
						{
							MarkFullExpressionCalls(expression);
							AppendUnwindDestructionActions(control, init);
						}
					}
				}
			}
			else if (arena_->IsTag(child, "condition"))
				AnalyzeCondition(child, control, statement, false);
			else if (arena_->IsTag(child, "iteration"))
			{
				const std::uint32_t iteration = MakeDump(DUMP_ITERATION);
				dump_.Add(statement, iteration);
				const ExpressionInfo value = MaterializeDiscardedClassResult(
					AnalyzeExpression(FirstSemanticChild(child), control));
				dump_.Add(iteration, value.node);
				const std::size_t edge_count = dump_.edges.size();
				AppendFullExpressionDestructionActions(value.node, iteration);
				if (dump_.edges.size() != edge_count)
				{
					MarkFullExpressionCalls(value.node);
					AppendUnwindDestructionActions(control, iteration);
				}
			}
			else AnalyzeSubstatement(child, control, statement);
		}
		continue_cleanup_stops_.pop_back();
		break_cleanup_stops_.pop_back();
		--loop_depth_;
		AppendScopeDestructionActions(control, output_parent, scope);
		return;
	}
	if (arena_->IsTag(node, "switch-statement"))
	{
		const ScopeId control = NewScope(scope, SCOPE_BLOCK, 0,
			ScopePrefixId(scope));
		const std::uint32_t statement = MakeDump(DUMP_SWITCH_STATEMENT);
		dump_.Add(output_parent, statement);
		++switch_depth_;
		break_cleanup_stops_.push_back(control);
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "condition"))
				AnalyzeCondition(child, control, statement, true);
			else AnalyzeSubstatement(child, control, statement);
		}
		break_cleanup_stops_.pop_back();
		--switch_depth_;
		AppendScopeDestructionActions(control, output_parent, scope);
		return;
	}
	if (arena_->IsTag(node, "break-statement"))
	{
		if (loop_depth_ == 0 && switch_depth_ == 0)
			throw std::runtime_error("break outside loop or switch");
		const std::uint32_t statement = MakeDump(DUMP_BREAK_STATEMENT);
		dump_.Add(output_parent, statement);
		if (break_cleanup_stops_.empty())
			throw std::logic_error("break has no cleanup boundary");
		AppendScopeDestructionActions(scope, statement,
			break_cleanup_stops_.back());
		return;
	}
	if (arena_->IsTag(node, "continue-statement"))
	{
		if (loop_depth_ == 0) throw std::runtime_error("continue outside loop");
		const std::uint32_t statement = MakeDump(DUMP_CONTINUE_STATEMENT);
		dump_.Add(output_parent, statement);
		if (continue_cleanup_stops_.empty())
			throw std::logic_error("continue has no cleanup boundary");
		AppendScopeDestructionActions(scope, statement,
			continue_cleanup_stops_.back());
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
	(void)EnsureBuiltinFunction(BUILTIN_FUNCTION_OPERATOR_NEW);
	(void)EnsureBuiltinFunction(BUILTIN_FUNCTION_OPERATOR_DELETE);
	(void)EnsureBuiltinFunction(BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY);
	(void)EnsureBuiltinFunction(BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY);
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
	if (graph_consumer_) graph_consumer_->Consume(SemanticGraphView(program,
		dump_, namespace_objects_, aggregate_helpers_, root_));
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
		stats_->class_zero_offset_subobject_visits =
			class_zero_offset_subobject_visits_;
		stats_->special_member_fact_lookups =
			special_member_fact_lookups_;
		stats_->special_member_subobject_visits =
			special_member_subobject_visits_;
		stats_->constructor_member_action_visits =
			constructor_member_action_visits_;
		stats_->constructor_base_action_visits =
			constructor_base_action_visits_;
		stats_->constructor_delegation_action_visits =
			constructor_delegation_action_visits_;
		stats_->destructor_subobject_action_visits =
			destructor_subobject_action_visits_;
		stats_->lexical_cleanup_action_visits =
			lexical_cleanup_action_visits_;
		stats_->unwind_cleanup_scope_visits =
			unwind_cleanup_scope_visits_;
		stats_->unwind_cleanup_action_visits =
			unwind_cleanup_action_visits_;
		stats_->namespace_object_actions = namespace_objects_.size();
		stats_->lookup_queries = program.lookup_queries;
		stats_->lookup_scope_visits = program.lookup_scope_visits;
		stats_->lookup_edge_visits = program.lookup_edge_visits;
		stats_->lookup_cache_hits = program.lookup_cache_hits;
		stats_->lookup_cache_misses = program.lookup_cache_misses;
		stats_->lookup_cache_invalidations =
			program.lookup_cache_invalidations;
		stats_->lookup_cache_dependency_edges =
			program.lookup_cache_dependency_edges;
		stats_->lookup_cache_invalidation_pushes =
			program.lookup_cache_invalidation_pushes;
		stats_->associated_scope_visits = associated_scope_visits_;
		stats_->associated_declaration_visits =
			associated_declaration_visits_;
		stats_->overload_candidates = overload_candidates_;
		stats_->overload_order_comparisons = overload_order_comparisons_;
		stats_->conversion_checks = conversion_checks_;
		stats_->call_conversion_cache_hits = call_conversion_cache_hits_;
		stats_->call_conversion_cache_misses = call_conversion_cache_misses_;
		stats_->braced_fact_cache_hits = braced_fact_cache_hits_;
		stats_->braced_fact_cache_misses = braced_fact_cache_misses_;
		stats_->function_signature_lookups = function_signature_lookups_;
		stats_->access_checks = access_checks_;
		stats_->access_path_visits = access_path_visits_;
		stats_->access_grant_probes = access_grant_probes_;
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
}
