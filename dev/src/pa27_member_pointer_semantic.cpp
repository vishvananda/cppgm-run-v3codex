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

TypeId SemanticAnalyzer::UnaryAddressContextTarget(
	const std::string& operation, TypeId target, NodeId operand, ScopeId scope)
{
	return operation == "&" && target == kNoType ?
		MemberPointerAddressSyntaxTarget(operand, scope) : target;
}

TypeId SemanticAnalyzer::MemberPointerAddressSyntaxTarget(
	NodeId syntax, ScopeId scope)
{
	while (syntax != kNoNode &&
		arena_->IsTag(syntax, "parenthesized-expression"))
		syntax = FirstSemanticChild(syntax);
	if (syntax == kNoNode || !arena_->IsTag(syntax, "id-expression") ||
		FindChild(syntax, "structured-type-name") == kNoNode)
		return kNoType;
	const LookupResult found = LookupStructuredName(
		syntax, scope, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding) return kNoType;
	const BindingRecord& binding = program_->bindings[found.ordinary];
	if (!binding.non_static_data_member || binding.member_owner == kNoEntity)
		return kNoType;
	return program_->types.MemberPointer(
		program_->entities[binding.member_owner].type, binding.type);
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
	if (binding.non_static_data_member && binding.member_owner != kNoEntity)
		return program_->types.MemberPointer(
			program_->entities[binding.member_owner].type, binding.type);
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
			kNoBinding, static_cast<std::int64_t>(0)));
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
	*selected = member.canonical;
	*scalar = ConstexprScalarValue(*selected, member.non_static_data_member ?
		static_cast<std::int64_t>(member.member_offset + 1) : 0);
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
	ExpressionInfo object_expression = left;
	if (operation == ".*" && object_expression.category == VALUE_PRVALUE &&
		IsClassObjectType(object_expression.type) &&
		dump_.nodes[object_expression.node].kind != DUMP_TEMPORARY_OBJECT)
		object_expression = MaterializeTemporary(object_expression);
	TypeId qualified_object = EffectiveType(object_expression.type);
	std::uint8_t object_cv = CV_NONE;
	const TypeRecord qualified_object_record = program_->types.Get(
		qualified_object);
	if (qualified_object_record.kind == TYPE_QUALIFIED)
		object_cv = qualified_object_record.cv;
	TypeId object_type = program_->types.RemoveTopCv(qualified_object);
	if (operation == "->*")
	{
		const TypeRecord object_pointer = program_->types.Get(Decay(object_type));
		if (object_pointer.kind != TYPE_POINTER)
		{
			*result = CandidateExpressionFailure(
				"arrow-star requires an object pointer");
			return true;
		}
		qualified_object = object_pointer.child;
		const TypeRecord pointee = program_->types.Get(qualified_object);
		object_cv = pointee.kind == TYPE_QUALIFIED ? pointee.cv : CV_NONE;
		object_type = program_->types.RemoveTopCv(qualified_object);
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
	TypeId result_type = pointer.child;
	if (!function_member && object_cv != CV_NONE)
		result_type = program_->types.Qualify(result_type, object_cv);
	const ValueCategory category = function_member ? VALUE_LVALUE :
		operation == "->*" || object_expression.category == VALUE_LVALUE ?
		VALUE_LVALUE : VALUE_XVALUE;
	const std::uint32_t expression = MakeDump(DUMP_BINARY_EXPRESSION,
		result_type, category, program_->names.Intern(display_operation));
	dump_.nodes[expression].operand_type = pointer_type;
	std::uint64_t projection_offset = 0;
	const std::size_t projections = object == owner ? 0 :
		BaseProjectionCount(object_type,
			program_->entities[owner].type, &projection_offset);
	if (projections == std::numeric_limits<std::size_t>::max() ||
		projections > std::numeric_limits<std::uint32_t>::max())
	{
		*result = CandidateExpressionFailure(
			"member pointer object has no unique base path");
		return true;
	}
	dump_.nodes[expression].base_projection_count =
		static_cast<std::uint32_t>(projections);
	dump_.nodes[expression].base_projection_offset = projection_offset;
	dump_.nodes[expression].has_base_projection_offset = true;
	dump_.Add(expression, object_expression.node);
	dump_.Add(expression, right.node);
	result->node = expression;
	result->type = result_type;
	result->category = category;
	result->indirect_constant_designator =
		right.indirect_constant_designator;
	BindingId selected = kNoBinding;
	if (right.constant)
	{
		const ConstexprScalarValue value = ExpressionScalar(right);
		if (value.kind == CONSTEXPR_SCALAR_MEMBER_POINTER)
			selected = value.member_pointer;
	}
	if (selected != kNoBinding && selected < program_->bindings.size())
	{
		selected = program_->bindings[selected].canonical;
		const BindingRecord& member = program_->bindings[selected];
		if (member.member_owner == owner &&
			(member.non_static_data_member || member.kind == BIND_FUNCTION))
		{
			result->binding = selected;
			if (function_member && right.indirect_constant_designator &&
				constant_evaluation_suppressed_depth_ == 0 &&
				constant_expression_required_depth_ == 0 &&
				constexpr_evaluation_depth_ == 0)
				DemandFunction(selected);
			const std::uint32_t object_value =
				ExpressionObject(object_expression);
			const std::uint32_t complete_object =
				ExpressionCompleteObject(object_expression);
			std::uint32_t object_address = operation == "->*" ?
				ExpressionAddress(object_expression) :
				LvalueAddress(&object_expression);
			if (function_member)
			{
				if (object_address != kNoConstexprAddress)
					SetExpressionAddress(result, object_address);
				if (object_value != kNoConstexprObject &&
					complete_object != kNoConstexprObject)
					SetExpressionSubobject(
						result, object_value, complete_object);
			}
			else
			{
				if (projection_offset != 0 &&
					object_address != kNoConstexprAddress &&
					projection_offset <= static_cast<std::uint64_t>(
						std::numeric_limits<std::int64_t>::max()))
					object_address = OffsetConstexprAddress(object_address,
						static_cast<std::int64_t>(projection_offset), false);
				const ConstexprObjectElement* element =
					ConstexprClassMemberAt(object_value, selected);
				if (element) SetExpressionObjectElement(result, *element);
				if (object_address != kNoConstexprAddress &&
					member.member_offset <= static_cast<std::uint64_t>(
						std::numeric_limits<std::int64_t>::max()))
				{
					const std::uint32_t member_address = OffsetConstexprAddress(
						object_address,
						static_cast<std::int64_t>(member.member_offset), true,
						static_cast<std::int64_t>(program_->SizeOf(
							EffectiveType(member.type))));
					if (member_address != kNoConstexprAddress)
						SetExpressionLvalueAddress(result, member_address);
				}
			}
		}
	}
	RecordExpressionFacts(*result);
	++expression_count_;
	return true;
}

}
}
