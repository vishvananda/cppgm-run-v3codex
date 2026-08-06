#include "pa12_semantic_detail.h"

#include <limits>
#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::CanAccessMember(BindingId member) const
{
	if (member == kNoBinding || member >= program_->bindings.size()) return false;
	const BindingRecord& binding = program_->bindings[member];
	return binding.member_owner == kNoEntity ||
		binding.access == ACCESS_PUBLIC ||
		binding.member_owner == current_class_context_ ||
		(binding.access == ACCESS_PROTECTED &&
		 current_class_context_ != kNoEntity &&
		 program_->IsBaseOf(binding.member_owner, current_class_context_));
}

bool SemanticAnalyzer::BaseConversionAllowed(EntityId derived,
	EntityId base) const
{
	if (!program_->IsBaseOf(base, derived) || base == derived) return false;
	for (EntityId current = derived; current != base;
		current = program_->entities[current].direct_base)
	{
		const AccessKind access = program_->entities[current].base_access;
		if (access == ACCESS_PUBLIC) continue;
		if (current_class_context_ == current) continue;
		if (access == ACCESS_PROTECTED && current_class_context_ != kNoEntity &&
			program_->IsBaseOf(current, current_class_context_)) continue;
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
		if (current == base) return distance;
	return std::numeric_limits<std::size_t>::max();
}

ExpressionInfo SemanticAnalyzer::AnalyzeImplicitDataMember(
	BindingId member_binding, ScopeId scope, TypeId target)
{
	const BindingRecord& binding = program_->bindings[member_binding];
	if (!CanAccessMember(member_binding) ||
		(binding.member_owner != kNoEntity &&
		 binding.member_owner != current_class_context_ &&
		 !BaseConversionAllowed(current_class_context_, binding.member_owner)))
		throw std::runtime_error("inaccessible implicit data member");
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
