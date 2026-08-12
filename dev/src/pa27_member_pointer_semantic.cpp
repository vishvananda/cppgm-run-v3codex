#include "pa12_semantic_detail.h"

#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::IsMemberPointer(TypeId type) const
{
	return program_->types.Get(program_->types.RemoveTopCv(
		EffectiveType(type))).kind == TYPE_MEMBER_POINTER;
}

std::size_t SemanticAnalyzer::TemplatePartialMemberPointerPackParameter(
	const TypeRecord& type, const std::vector<TemplateParameter>& parameters,
	std::size_t depth) const
{
	const std::size_t owner = TemplatePartialPackParameter(
		static_cast<TypeId>(type.bound), parameters, depth + 1);
	return owner != parameters.size() ? owner :
		TemplatePartialPackParameter(type.child, parameters, depth + 1);
}

bool SemanticAnalyzer::DeduceTemplatePartialMemberPointerType(
	const TypeRecord& pattern, const TypeRecord& argument,
	const std::vector<TemplateParameter>& parameters,
	FunctionTemplateDeduction* deduced) const
{
	return DeduceTemplatePartialType(static_cast<TypeId>(pattern.bound),
		static_cast<TypeId>(argument.bound), parameters, deduced) &&
		DeduceTemplatePartialType(
			pattern.child, argument.child, parameters, deduced);
}

TypeId SemanticAnalyzer::UnaryAddressOperandTarget(
	const std::string& operation, TypeId target) const
{
	if (operation != "&" || target == kNoType) return kNoType;
	const TypeId desired = program_->types.RemoveTopCv(target);
	const TypeRecord shape = program_->types.Get(desired);
	if (shape.kind == TYPE_MEMBER_POINTER) return desired;
	return shape.kind == TYPE_POINTER && program_->types.IsFunction(shape.child) ?
		shape.child : kNoType;
}

TypeId SemanticAnalyzer::MemberPointerAddressTarget(
	const ExpressionInfo& operand, NodeId syntax, TypeId target) const
{
	if (target != kNoType) return target;
	if (operand.binding == kNoBinding ||
		operand.binding >= program_->bindings.size() || syntax == kNoNode ||
		FindChild(syntax, "structured-type-name") == kNoNode)
		return kNoType;
	const BindingRecord& binding = program_->bindings[operand.binding];
	if (binding.kind != BIND_FUNCTION || binding.static_member_function)
		return kNoType;
	const FunctionInfo& function = GetFunction(operand.binding);
	return function.member_owner == kNoType ? kNoType :
		program_->types.MemberPointer(function.member_owner, function.type);
}

bool SemanticAnalyzer::IsBuiltinLogicalOperand(
	const ExpressionInfo& operand) const
{
	return IsArithmetic(operand.type) || IsPointer(Decay(operand.type)) ||
		IsNullptr(operand.type) || IsMemberPointer(operand.type);
}

ConversionRank SemanticAnalyzer::MemberPointerConversion(
	TypeId source, bool integer_zero, TypeId target) const
{
	const TypeRecord from = program_->types.Get(source);
	const TypeRecord to = program_->types.Get(target);
	if (to.kind == TYPE_MEMBER_POINTER &&
		(IsNullptr(source) || integer_zero)) return CONVERSION_STANDARD;
	if (from.kind == TYPE_MEMBER_POINTER &&
		target == program_->types.Fundamental(FUND_BOOL))
		return CONVERSION_BOOLEAN;
	if (from.kind == TYPE_MEMBER_POINTER && to.kind == TYPE_MEMBER_POINTER &&
		SimilarUnqualified(from.child, to.child) &&
		BaseConversionAllowed(EntityOf(static_cast<TypeId>(to.bound)),
			EntityOf(static_cast<TypeId>(from.bound))))
		return CONVERSION_STANDARD;
	return CONVERSION_INVALID;
}

bool SemanticAnalyzer::ApplyMemberPointerTarget(
	ExpressionInfo* value, TypeId source, TypeId target)
{
	if (!value) throw std::logic_error("missing member pointer target value");
	const TypeRecord from = program_->types.Get(source);
	const TypeRecord to = program_->types.Get(target);
	if (from.kind == TYPE_MEMBER_POINTER &&
		target == program_->types.Fundamental(FUND_BOOL) && value->constant)
		SetExpressionScalar(value, ConstexprScalarValue(
			static_cast<std::int64_t>(
				value->binding != kNoBinding || value->value != 0)));
	if (to.kind != TYPE_MEMBER_POINTER) return false;
	if (value->integer_literal_zero || IsNullptr(source))
	{
		value->type = target;
		value->category = VALUE_PRVALUE;
		value->binding = kNoBinding;
		dump_.nodes[value->node].type = target;
		dump_.nodes[value->node].category = VALUE_PRVALUE;
		dump_.nodes[value->node].null_member_pointer_constant =
			IsNullptr(source);
		SetExpressionScalar(value, ConstexprScalarValue(
			static_cast<std::int64_t>(0)));
		return true;
	}
	if (from.kind != TYPE_MEMBER_POINTER) return false;
	value->type = target;
	dump_.nodes[value->node].type = target;
	return true;
}

bool SemanticAnalyzer::FormMemberPointerAddress(
	const ExpressionInfo& operand, TypeId target, TypeId* result_type,
	bool* constant, ConstexprScalarValue* scalar, BindingId* selected) const
{
	if (!result_type || !constant || !scalar || !selected || target == kNoType)
		return false;
	const TypeId shape = program_->types.RemoveTopCv(target);
	if (program_->types.Get(shape).kind != TYPE_MEMBER_POINTER) return false;
	if (operand.binding == kNoBinding ||
		operand.binding >= program_->bindings.size())
		throw std::runtime_error(
			"member pointer address has no selected member");
	const BindingRecord& member = program_->bindings[operand.binding];
	if (member.member_owner == kNoEntity)
		throw std::runtime_error(
			"member pointer address does not name a member");
	if (member.non_static_data_member &&
		member.member_offset >= static_cast<std::uint64_t>(
			std::numeric_limits<std::int64_t>::max()))
		throw std::runtime_error("data member pointer offset is too large");
	*result_type = shape;
	*constant = true;
	*scalar = ConstexprScalarValue(member.non_static_data_member ?
		static_cast<std::int64_t>(member.member_offset + 1) : 0);
	*selected = member.canonical;
	return true;
}

bool SemanticAnalyzer::TryAnalyzeMemberPointerApplication(
	const std::string& operation, const std::string& display_operation,
	const ExpressionInfo& left, const ExpressionInfo& right,
	ExpressionInfo* result)
{
	if (operation != ".*" && operation != "->*") return false;
	if (!result) throw std::logic_error("missing member pointer result");
	const TypeId pointer_type = program_->types.RemoveTopCv(
		EffectiveType(right.type));
	const TypeRecord pointer = program_->types.Get(pointer_type);
	if (pointer.kind != TYPE_MEMBER_POINTER)
	{
		*result = CandidateExpressionFailure(
			"right operand is not a member pointer");
		return true;
	}
	TypeId object_type = program_->types.RemoveTopCv(EffectiveType(left.type));
	if (operation == "->*")
	{
		const TypeRecord object_pointer = program_->types.Get(Decay(object_type));
		if (object_pointer.kind != TYPE_POINTER)
		{
			*result = CandidateExpressionFailure(
				"arrow-star requires an object pointer");
			return true;
		}
		object_type = program_->types.RemoveTopCv(object_pointer.child);
	}
	const EntityId object = EntityOf(object_type);
	const EntityId owner = EntityOf(static_cast<TypeId>(pointer.bound));
	if (object == kNoEntity || owner == kNoEntity ||
		(object != owner && !program_->IsBaseOf(owner, object)))
	{
		*result = CandidateExpressionFailure(
			"member pointer owner does not match object");
		return true;
	}
	const bool function_member = program_->types.IsFunction(pointer.child);
	const ValueCategory category = function_member ? VALUE_LVALUE :
		left.category == VALUE_LVALUE ? VALUE_LVALUE : VALUE_XVALUE;
	const std::uint32_t expression = MakeDump(DUMP_BINARY_EXPRESSION,
		pointer.child, category, program_->names.Intern(display_operation));
	dump_.nodes[expression].operand_type = pointer_type;
	dump_.Add(expression, left.node);
	dump_.Add(expression, right.node);
	result->node = expression;
	result->type = pointer.child;
	result->category = category;
	++expression_count_;
	return true;
}

}
}
