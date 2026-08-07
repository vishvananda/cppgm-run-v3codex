#include "pa12_semantic_detail.h"

#include <limits>
#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::AccessIsBaseOf(EntityId base, EntityId derived) const
{
	if (base == kNoEntity || derived == kNoEntity ||
		base >= program_->entities.size() || derived >= program_->entities.size())
		return false;
	for (EntityId current = derived; current != kNoEntity;
		current = program_->entities[current].direct_base)
	{
		++access_path_visits_;
		if (current == base) return true;
	}
	return false;
}

bool SemanticAnalyzer::HasClassPrivilege(EntityId owner) const
{
	if (owner == kNoEntity) return false;
	for (EntityId context = current_class_context_; context != kNoEntity;
		context = program_->entities[context].enclosing_class)
	{
		if (context == owner) return true;
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owner) << 32) | context;
		++access_grant_probes_;
		if (friend_class_grants_.Find(key)) return true;
	}
	if (current_function_context_ != kNoBinding)
	{
		const BindingId function = program_->bindings[
			current_function_context_].canonical;
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owner) << 32) | function;
		++access_grant_probes_;
		if (friend_function_grants_.Find(key)) return true;
	}
	return false;
}

bool SemanticAnalyzer::HasDerivedClassPrivilege(EntityId base) const
{
	for (EntityId context = current_class_context_; context != kNoEntity;
		context = program_->entities[context].enclosing_class)
		if (AccessIsBaseOf(base, context)) return true;
	return false;
}

bool SemanticAnalyzer::HasProtectedObjectPrivilege(EntityId owner,
	EntityId object_class) const
{
	if (owner == kNoEntity || object_class == kNoEntity) return false;
	bool privileged = false;
	for (EntityId current = object_class; current != kNoEntity;
		current = program_->entities[current].direct_base)
	{
		++access_path_visits_;
		if (HasClassPrivilege(current)) privileged = true;
		if (current == owner) return privileged;
	}
	return false;
}

bool SemanticAnalyzer::CanAccessMember(BindingId member,
	EntityId naming_class, EntityId object_class) const
{
	++access_checks_;
	if (member == kNoBinding || member >= program_->bindings.size()) return false;
	const BindingRecord& binding = program_->bindings[member];
	const BindingRecord& declaration =
		program_->bindings[binding.canonical];
	const bool object_member = declaration.non_static_data_member ||
		(declaration.kind == BIND_FUNCTION &&
		 declaration.member_owner != kNoEntity &&
		 !declaration.static_member_function);
	if (binding.access_owner != kNoEntity)
	{
		if (binding.access == ACCESS_PUBLIC) return true;
		if (binding.access == ACCESS_PRIVATE)
			return HasClassPrivilege(binding.access_owner);
		const bool permitted = HasClassPrivilege(binding.access_owner) ||
			HasDerivedClassPrivilege(binding.access_owner);
		return permitted && (!object_member || object_class == kNoEntity ||
			HasProtectedObjectPrivilege(binding.access_owner, object_class));
	}
	const EntityId owner = binding.member_owner;
	if (owner == kNoEntity) return true;
	if (naming_class == kNoEntity) naming_class = owner;
	EntityId privilege_anchor = kNoEntity;
	for (EntityId context = current_class_context_; context != kNoEntity;
		context = program_->entities[context].enclosing_class)
		if (AccessIsBaseOf(naming_class, context))
		{
			privilege_anchor = context;
			break;
		}
	for (EntityId current = naming_class; current != owner;
		current = program_->entities[current].direct_base)
	{
		if (current == kNoEntity) return false;
		++access_path_visits_;
		if (HasClassPrivilege(current)) privilege_anchor = current;
		const EntityRecord& derived = program_->entities[current];
		if (derived.direct_base == kNoEntity) return false;
		if (derived.base_access == ACCESS_PUBLIC) continue;
		if (HasClassPrivilege(current)) continue;
		if (derived.base_access == ACCESS_PROTECTED &&
			privilege_anchor != kNoEntity &&
			AccessIsBaseOf(current, privilege_anchor)) continue;
		return false;
	}
	if (HasClassPrivilege(owner)) privilege_anchor = owner;
	if (binding.access == ACCESS_PUBLIC) return true;
	if (binding.access == ACCESS_PRIVATE)
		return HasClassPrivilege(owner);
	const bool permitted = HasClassPrivilege(owner) ||
		(privilege_anchor != kNoEntity &&
		 AccessIsBaseOf(owner, privilege_anchor)) ||
		HasDerivedClassPrivilege(owner);
	return permitted && (!object_member || object_class == kNoEntity ||
		HasProtectedObjectPrivilege(owner, object_class));
}

bool SemanticAnalyzer::BaseConversionAllowed(EntityId derived,
	EntityId base) const
{
	++access_checks_;
	if (base == derived) return false;
	EntityId privilege_anchor = kNoEntity;
	for (EntityId context = current_class_context_; context != kNoEntity;
		context = program_->entities[context].enclosing_class)
		if (AccessIsBaseOf(derived, context))
		{
			privilege_anchor = context;
			break;
		}
	for (EntityId current = derived; current != base;
		current = program_->entities[current].direct_base)
	{
		if (current == kNoEntity) return false;
		++access_path_visits_;
		if (HasClassPrivilege(current)) privilege_anchor = current;
		const AccessKind access = program_->entities[current].base_access;
		if (access == ACCESS_PUBLIC) continue;
		if (HasClassPrivilege(current)) continue;
		if (access == ACCESS_PROTECTED && privilege_anchor != kNoEntity &&
			AccessIsBaseOf(current, privilege_anchor)) continue;
		return false;
	}
	return true;
}

std::size_t SemanticAnalyzer::BaseConversionDistance(TypeId source,
	TypeId target) const
{
	const TypeRecord source_top = program_->types.Get(source);
	const TypeRecord target_top = program_->types.Get(target);
	if (source_top.kind == TYPE_LVALUE_REFERENCE ||
		source_top.kind == TYPE_RVALUE_REFERENCE) source = source_top.child;
	if (target_top.kind == TYPE_LVALUE_REFERENCE ||
		target_top.kind == TYPE_RVALUE_REFERENCE) target = target_top.child;
	source = program_->types.RemoveTopCv(source);
	target = program_->types.RemoveTopCv(target);
	const TypeRecord source_core = program_->types.Get(source);
	const TypeRecord target_core = program_->types.Get(target);
	if (source_core.kind == TYPE_POINTER && target_core.kind == TYPE_POINTER)
	{
		source = program_->types.RemoveTopCv(source_core.child);
		target = program_->types.RemoveTopCv(target_core.child);
	}
	const EntityId derived = EntityOf(source);
	const EntityId base = EntityOf(target);
	if (derived == kNoEntity || base == kNoEntity)
		return std::numeric_limits<std::size_t>::max();
	std::size_t distance = 0;
	for (EntityId current = derived; current != kNoEntity;
		current = program_->entities[current].direct_base, ++distance)
	{
		++access_path_visits_;
		if (current == base) return distance;
	}
	return std::numeric_limits<std::size_t>::max();
}

ConversionRank SemanticAnalyzer::MemberObjectConversion(
	const ExpressionInfo& source, TypeId target, BindingId member) const
{
	const ConversionRank ordinary = Conversion(source, target);
	if (ordinary != CONVERSION_INVALID || member == kNoBinding ||
		member >= program_->bindings.size() ||
		program_->bindings[member].access_owner == kNoEntity)
		return ordinary;
	const TypeId from = Decay(source.type);
	const TypeId to = program_->types.RemoveTopCv(target);
	const TypeRecord source_pointer = program_->types.Get(from);
	const TypeRecord target_pointer = program_->types.Get(to);
	if (source_pointer.kind != TYPE_POINTER ||
		target_pointer.kind != TYPE_POINTER) return CONVERSION_INVALID;
	const EntityId derived = EntityOf(source_pointer.child);
	const EntityId base = EntityOf(target_pointer.child);
	if (derived == kNoEntity || base == kNoEntity || derived == base ||
		!AccessIsBaseOf(base, derived)) return CONVERSION_INVALID;
	const TypeRecord source_cv = program_->types.Get(source_pointer.child);
	const TypeRecord target_cv = program_->types.Get(target_pointer.child);
	const std::uint8_t scv = source_cv.kind == TYPE_QUALIFIED ?
		source_cv.cv : CV_NONE;
	const std::uint8_t tcv = target_cv.kind == TYPE_QUALIFIED ?
		target_cv.cv : CV_NONE;
	return (scv & ~tcv) == 0 ?
		CONVERSION_DERIVED_TO_BASE : CONVERSION_INVALID;
}

ExpressionInfo SemanticAnalyzer::ApplyMemberObjectTarget(
	ExpressionInfo value, TypeId target, BindingId member,
	const ObjectConversionFact* conversion_fact)
{
	const ConversionRank conversion = conversion_fact ? conversion_fact->rank :
		MemberObjectConversion(value, target, member);
	if (conversion != CONVERSION_DERIVED_TO_BASE)
		return ApplyTarget(value, target);
	const std::size_t projections = conversion_fact ?
		conversion_fact->base_projection_count :
		BaseConversionDistance(value.type, target);
	if (projections == std::numeric_limits<std::size_t>::max() ||
		projections > std::numeric_limits<std::uint32_t>::max())
		throw std::logic_error("using member has no bounded base path");
	const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION,
		target, VALUE_PRVALUE);
	dump_.nodes[cast].base_projection_count =
		static_cast<std::uint32_t>(projections);
	dump_.Add(cast, value.node);
	value.node = cast;
	value.type = target;
	value.category = VALUE_PRVALUE;
	value.binding = kNoBinding;
	value.constant = false;
	++expression_count_;
	RecordExpressionFacts(value);
	return value;
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
		const ConversionRank reference_conversion = Conversion(operand, target);
		const bool explicit_rvalue = target_record.kind == TYPE_RVALUE_REFERENCE &&
			SimilarUnqualified(EffectiveType(operand.type), target_record.child);
		const bool explicit_cv_lvalue =
			target_record.kind == TYPE_LVALUE_REFERENCE &&
			operand.category == VALUE_LVALUE &&
			SimilarUnqualified(EffectiveType(operand.type), target_record.child) &&
			(cast_kind.find("CONST") != std::string::npos ||
			 cast_kind.compare(0, 10, "OP_LPAREN:") == 0);
		if (!explicit_rvalue && !explicit_cv_lvalue &&
			reference_conversion == CONVERSION_INVALID)
			throw std::runtime_error("invalid reference cast");
		if (reference_conversion == CONVERSION_DERIVED_TO_BASE)
			return ApplyTarget(operand, target);
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
	const TypeId decayed_operand_type = Decay(operand.type);
	if (cast_kind.find("STATIC") != std::string::npos && IsPointer(target) &&
		IsPointer(decayed_operand_type))
	{
		const TypeRecord source_pointer = program_->types.Get(Decay(operand.type));
		const TypeRecord target_pointer = program_->types.Get(
			program_->types.RemoveTopCv(target));
		const EntityId derived = EntityOf(source_pointer.child);
		const EntityId base = EntityOf(target_pointer.child);
		if (derived != kNoEntity && base != kNoEntity &&
			program_->IsBaseOf(base, derived) &&
			!BaseConversionAllowed(derived, base))
			throw std::runtime_error("inaccessible base conversion");
	}
	const bool valid = IsVoid(target) ||
		(IsArithmetic(target) && IsArithmetic(operand.type)) ||
		(target_enum && (IsIntegral(operand.type, true) ||
			IsFloating(operand.type))) ||
		(source_enum && (IsIntegral(target, true) || IsFloating(target))) ||
		(IsPointer(target) && (IsPointer(decayed_operand_type) ||
			IsNullptr(operand.type) || operand.integer_literal_zero)) ||
		(target == program_->types.Fundamental(FUND_BOOL) &&
			(IsPointer(decayed_operand_type) || IsNullptr(operand.type))) ||
		(IsNullptr(target) && (IsNullptr(operand.type) ||
			operand.integer_literal_zero)) ||
		(permits_reinterpretation &&
			((IsPointer(target) && IsIntegral(operand.type)) ||
			 (IsIntegral(target) && IsPointer(decayed_operand_type))));
	if (!valid) throw std::runtime_error("invalid explicit conversion");
	const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION, target,
		VALUE_PRVALUE, program_->names.Intern(arena_->Payload(node)));
	if (cast_kind.find("REINTER") == std::string::npos && IsPointer(target) &&
		IsPointer(decayed_operand_type))
	{
		const TypeRecord source_pointer = program_->types.Get(Decay(operand.type));
		const TypeRecord target_pointer = program_->types.Get(
			program_->types.RemoveTopCv(target));
		const EntityId derived = EntityOf(source_pointer.child);
		const EntityId base = EntityOf(target_pointer.child);
		if (derived != kNoEntity && base != kNoEntity && derived != base &&
			program_->IsBaseOf(base, derived))
		{
			const std::size_t projections =
				BaseConversionDistance(operand.type, target);
			if (projections == std::numeric_limits<std::size_t>::max() ||
				projections > std::numeric_limits<std::uint32_t>::max())
				throw std::logic_error("cast has no bounded base path");
			dump_.nodes[cast].base_projection_count =
				static_cast<std::uint32_t>(projections);
		}
	}
	dump_.Add(cast, operand.node);
	ExpressionInfo result;
	result.node = cast;
	result.type = target;
	result.constant = operand.constant;
	result.value = operand.value;
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeImplicitDataMember(
	BindingId member_binding, ScopeId scope, TypeId target,
	EntityId naming_class)
{
	const BindingRecord& binding = program_->bindings[member_binding];
	const NameId this_name = program_->names.Intern("this");
	const LookupResult this_lookup =
		program_->LookupName(scope, this_name, LOOKUP_ORDINARY);
	if (this_lookup.ordinary == kNoBinding)
		throw std::runtime_error("non-static member requires an object");
	const BindingRecord& this_binding =
		program_->bindings[this_lookup.ordinary];
	TypeId member_type = binding.type;
	TypeId object_type = EffectiveType(this_binding.type);
	const TypeRecord object_pointer = program_->types.Get(
		program_->types.RemoveTopCv(object_type));
	const EntityId object_class = object_pointer.kind == TYPE_POINTER ?
		EntityOf(object_pointer.child) : kNoEntity;
	if (!CanAccessMember(member_binding, naming_class, object_class))
		throw std::runtime_error("inaccessible implicit data member");
	if (object_pointer.kind == TYPE_POINTER)
	{
		const TypeRecord pointee = program_->types.Get(object_pointer.child);
		if (pointee.kind == TYPE_QUALIFIED)
			member_type = program_->types.Qualify(member_type, pointee.cv);
	}
	const std::uint32_t object = MakeDump(DUMP_ID_EXPRESSION,
		this_binding.type, VALUE_LVALUE, this_name, this_lookup.ordinary);
	const std::uint32_t member = MakeDump(DUMP_MEMBER_EXPRESSION,
		member_type, VALUE_LVALUE, binding.name, member_binding);
	if (current_class_context_ == kNoEntity)
		throw std::logic_error("implicit member has no class context");
	const std::size_t projections = BaseConversionDistance(
		program_->entities[current_class_context_].type,
		program_->entities[binding.member_owner].type);
	if (projections == std::numeric_limits<std::size_t>::max() ||
		projections > std::numeric_limits<std::uint32_t>::max())
		throw std::logic_error("implicit member has no bounded base path");
	dump_.nodes[member].base_projection_count =
		static_cast<std::uint32_t>(projections);
	dump_.Add(member, object);
	ExpressionInfo result;
	result.node = member;
	result.type = member_type;
	result.category = VALUE_LVALUE;
	result.binding = member_binding;
	expression_count_ += 2;
	return ApplyTarget(result, target);
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
	else if (EntityOf(yes.type) != kNoEntity &&
		EntityOf(no.type) != kNoEntity && yes.category == no.category &&
		(yes.category == VALUE_LVALUE || yes.category == VALUE_XVALUE))
	{
		const EntityId yes_entity = EntityOf(yes.type);
		const EntityId no_entity = EntityOf(no.type);
		if (yes_entity != kNoEntity && no_entity != kNoEntity &&
			BaseConversionAllowed(yes_entity, no_entity))
		{
			type = no.type;
			category = yes.category;
			yes = ApplyTarget(yes, program_->types.Reference(
				category == VALUE_LVALUE ? TYPE_LVALUE_REFERENCE :
				TYPE_RVALUE_REFERENCE, type));
		}
		else if (yes_entity != kNoEntity && no_entity != kNoEntity &&
			BaseConversionAllowed(no_entity, yes_entity))
		{
			type = yes.type;
			category = yes.category;
			no = ApplyTarget(no, program_->types.Reference(
				category == VALUE_LVALUE ? TYPE_LVALUE_REFERENCE :
				TYPE_RVALUE_REFERENCE, type));
		}
	}
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

}
}
