#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <limits>

namespace cppgm
{
namespace semantic
{

bool Analyzer::IsMemberPointer(TypeId type) const
{
	return program_->types.Get(program_->types.RemoveTopCv(
		EffectiveType(type))).kind == TYPE_MEMBER_POINTER;
}

std::size_t Analyzer::TemplatePartialMemberPointerPackParameter(
	const TypeRecord& type, const std::vector<TemplateParameter>& parameters,
	std::size_t depth) const
{
	const std::size_t owner = TemplatePartialPackParameter(
		static_cast<TypeId>(type.bound), parameters, depth + 1);
	return owner != parameters.size() ? owner :
		TemplatePartialPackParameter(type.child, parameters, depth + 1);
}

bool Analyzer::DeduceTemplatePartialMemberPointerType(
	const TypeRecord& pattern, const TypeRecord& argument,
	const std::vector<TemplateParameter>& parameters,
	FunctionTemplateDeduction* deduced) const
{
	return DeduceTemplatePartialType(static_cast<TypeId>(pattern.bound),
		static_cast<TypeId>(argument.bound), parameters, deduced) &&
		DeduceTemplatePartialType(
			pattern.child, argument.child, parameters, deduced);
}

TypeId Analyzer::UnaryAddressOperandTarget(
	const std::string& operation, TypeId target) const
{
	if (operation != "&" || target == kNoType) return kNoType;
	const TypeId desired = program_->types.RemoveTopCv(target);
	const TypeRecord shape = program_->types.Get(desired);
	if (shape.kind == TYPE_MEMBER_POINTER) return desired;
	return shape.kind == TYPE_POINTER && program_->types.IsFunction(shape.child) ?
		shape.child : kNoType;
}

TypeId Analyzer::UnaryAddressContextTarget(
	const std::string& operation, TypeId target, NodeId operand, ScopeId scope)
{
	return ClassifyOperationSpelling(operation) == OP_AMP &&
		target == kNoType ?
		MemberPointerAddressSyntaxTarget(operand, scope) : target;
}

TypeId Analyzer::MemberPointerAddressSyntaxTarget(
	NodeId syntax, ScopeId scope)
{
	while (syntax != kNoNode &&
		arena_->IsTag(syntax, ::cppgm::syntax::STAG_PARENTHESIZED_EXPRESSION))
		syntax = FirstSemanticChild(syntax);
	if (syntax == kNoNode || !arena_->IsTag(syntax, ::cppgm::syntax::STAG_ID_EXPRESSION) ||
		FindChild(syntax, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME) == kNoNode)
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

TypeId Analyzer::MemberPointerAddressTarget(
	const ExpressionInfo& operand, NodeId syntax, TypeId target) const
{
	if (target != kNoType) return target;
	if (operand.binding == kNoBinding ||
		operand.binding >= program_->bindings.size() || syntax == kNoNode ||
		FindChild(syntax, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME) == kNoNode)
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

bool Analyzer::IsBuiltinLogicalOperand(
	const ExpressionInfo& operand) const
{
	return IsArithmetic(operand.type) || IsPointer(Decay(operand.type)) ||
		IsNullptr(operand.type) || IsMemberPointer(operand.type);
}

ConversionRank Analyzer::MemberPointerConversion(
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

bool Analyzer::MemberPointerBaseAdjustment(
	TypeId source, TypeId target, std::uint64_t* adjustment) const
{
	source = program_->types.RemoveTopCv(EffectiveType(source));
	target = program_->types.RemoveTopCv(EffectiveType(target));
	const TypeRecord& from = program_->types.Get(source);
	const TypeRecord& to = program_->types.Get(target);
	if (from.kind != TYPE_MEMBER_POINTER || to.kind != TYPE_MEMBER_POINTER ||
		!SimilarUnqualified(from.child, to.child)) return false;
	const EntityId base = EntityOf(static_cast<TypeId>(from.bound));
	const EntityId derived = EntityOf(static_cast<TypeId>(to.bound));
	if (base == kNoEntity || derived == kNoEntity) return false;
	if (base == derived)
	{
		if (adjustment) *adjustment = 0;
		return true;
	}
	std::size_t distance = 0;
	std::uint64_t offset = 0;
	bool ambiguous = false;
	++access_path_visits_;
	if (!program_->QueryBasePath(derived, base, &distance, 0, &offset,
		&ambiguous) || distance == 0 || ambiguous ||
		!BaseConversionAllowed(derived, base)) return false;
	if (adjustment) *adjustment = offset;
	return true;
}

bool Analyzer::ApplyMemberPointerTarget(
	ExpressionInfo* value, TypeId source, TypeId target)
{
	if (!value) ThrowInternalCompilerError("missing member pointer target value");
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
	std::uint64_t adjustment = 0;
	if (!MemberPointerBaseAdjustment(source, target, &adjustment))
		return false;
	if (adjustment > static_cast<std::uint64_t>(
		std::numeric_limits<std::int64_t>::max()))
		ThrowSemanticResourceLimit("member pointer adjustment is too large");
	if (adjustment != 0)
	{
		if (value->constant)
		{
			ConstexprScalarValue scalar = ExpressionScalar(*value);
			if (ScalarTruth(scalar))
			{
				if (scalar.integral > std::numeric_limits<std::int64_t>::max() -
					static_cast<std::int64_t>(adjustment))
					ThrowSemanticResourceLimit(
						"member pointer adjustment is too large");
				scalar.integral += static_cast<std::int64_t>(adjustment);
			}
			SetExpressionScalar(value, scalar);
		}
		const std::uint32_t cast = MakeDump(
			DUMP_CAST_EXPRESSION, target, VALUE_PRVALUE);
		dump_.nodes[cast].member_pointer_conversion = true;
		dump_.nodes[cast].base_projection_offset = adjustment;
		dump_.nodes[cast].has_base_projection_offset = true;
		dump_.Add(cast, value->node);
		value->node = cast;
		value->category = VALUE_PRVALUE;
		if (!value->constant) value->binding = kNoBinding;
		++expression_count_;
	}
	value->type = target;
	dump_.nodes[value->node].type = target;
	return true;
}

bool Analyzer::FormMemberPointerAddress(
	const ExpressionInfo& operand, TypeId target, TypeId* result_type,
	bool* constant, ConstexprScalarValue* scalar, BindingId* selected) const
{
	if (!result_type || !constant || !scalar || !selected || target == kNoType)
		return false;
	const TypeId shape = program_->types.RemoveTopCv(target);
	if (program_->types.Get(shape).kind != TYPE_MEMBER_POINTER) return false;
	if (operand.binding == kNoBinding ||
		operand.binding >= program_->bindings.size())
		ThrowSemanticError(
			"member pointer address has no selected member");
	const BindingRecord& member = program_->bindings[operand.binding];
	if (member.member_owner == kNoEntity)
		ThrowSemanticError(
			"member pointer address does not name a member");
	const TypeId source = program_->types.MemberPointer(
		program_->entities[member.member_owner].type, member.type);
	std::uint64_t adjustment = 0;
	if (!MemberPointerBaseAdjustment(source, shape, &adjustment)) return false;
	const std::uint64_t member_offset = member.non_static_data_member ?
		program_->BindingLayout(member).member_offset : 0;
	if (adjustment > static_cast<std::uint64_t>(
			std::numeric_limits<std::int64_t>::max()) ||
		member_offset > static_cast<std::uint64_t>(
			std::numeric_limits<std::int64_t>::max()) - adjustment -
			(member.non_static_data_member ? 1 : 0))
		ThrowSemanticResourceLimit("member pointer offset is too large");
	*result_type = shape;
	*constant = true;
	*selected = member.canonical;
	*scalar = ConstexprScalarValue(*selected, member.non_static_data_member ?
		static_cast<std::int64_t>(adjustment + member_offset + 1) :
		static_cast<std::int64_t>(adjustment));
	return true;
}

void Analyzer::RecordMemberPointerAddressFacts(
	NodeId expression, BindingId selected)
{
	if (expression == kNoNode || expression >= dump_.nodes.size() ||
		selected == kNoBinding || selected >= program_->bindings.size())
		ThrowInternalCompilerError("invalid member pointer address fact");
	dump_.nodes[expression].binding = selected;
	const BindingRecord& member = program_->bindings[selected];
	if (host_object_emission_ && member.kind == BIND_FUNCTION)
		DemandFunction(selected, FUNCTION_DEMAND_ADDRESS);
	if (!member.virtual_function) return;
	const std::uint32_t slot = VirtualSlotFor(selected);
	if (slot == kNoDumpEdge)
		ThrowInternalCompilerError("virtual member pointer has no canonical slot");
	dump_.nodes[expression].virtual_slot = slot;
}

bool Analyzer::TryAnalyzeMemberPointerApplication(
	const std::string& operation, const std::string& display_operation,
	const ExpressionInfo& left, const ExpressionInfo& right,
	ExpressionInfo* result)
{
	const int op = ClassifyOperationSpelling(operation);
	if (operation != ".*" && operation != "->*") return false;
	if (!result) ThrowInternalCompilerError("missing member pointer result");
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
	if (op == OP_DOTSTAR && object_expression.category == VALUE_PRVALUE &&
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
	if (op == OP_ARROWSTAR)
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
	if (function_member)
	{
		ExpressionInfo invocation_object = object_expression;
		if (op == OP_ARROWSTAR) invocation_object.category = VALUE_LVALUE;
		const TypeRecord& function_type = program_->types.Get(pointer.child);
		const bool ref_qualifier_viable =
			function_type.ref_qualifier == FUNCTION_REF_NONE ||
			(function_type.ref_qualifier == FUNCTION_REF_LVALUE ?
				invocation_object.category == VALUE_LVALUE :
				invocation_object.category != VALUE_LVALUE);
		if (!ref_qualifier_viable ||
			(object_cv & ~function_type.cv) != 0)
		{
			*result = CandidateExpressionFailure(
				"member function pointer object is not viable");
			return true;
		}
	}
	if (!function_member && object_cv != CV_NONE)
		result_type = program_->types.Qualify(result_type, object_cv);
	const ValueCategory category = function_member ? VALUE_LVALUE :
		op == OP_ARROWSTAR || object_expression.category == VALUE_LVALUE ?
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
	ConstexprScalarValue pointer_value;
	bool has_pointer_value = false;
	if (right.constant)
	{
		pointer_value = ExpressionScalar(right);
		has_pointer_value =
			pointer_value.kind == CONSTEXPR_SCALAR_MEMBER_POINTER;
		if (has_pointer_value) selected = pointer_value.member_pointer;
	}
	if (selected != kNoBinding && selected < program_->bindings.size())
	{
		selected = program_->bindings[selected].canonical;
		const BindingRecord& member = program_->bindings[selected];
		std::size_t member_distance = 0;
		bool member_path_ambiguous = false;
		const bool compatible_owner = member.member_owner == owner ||
			program_->QueryBasePath(owner, member.member_owner,
				&member_distance, 0, 0, &member_path_ambiguous);
		if (compatible_owner && !member_path_ambiguous &&
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
			std::uint32_t object_address = op == OP_ARROWSTAR ?
				ExpressionAddress(object_expression) :
				LvalueAddress(&object_expression);
			if (projection_offset != 0 &&
				object_address != kNoConstexprAddress &&
				projection_offset <= static_cast<std::uint64_t>(
					std::numeric_limits<std::int64_t>::max()))
				object_address = OffsetConstexprAddress(object_address,
					static_cast<std::int64_t>(projection_offset), false);
			if (function_member)
			{
				if (has_pointer_value && pointer_value.integral != 0 &&
					object_address != kNoConstexprAddress)
					object_address = OffsetConstexprAddress(object_address,
						pointer_value.integral, false);
				if (object_address != kNoConstexprAddress)
					SetExpressionAddress(result, object_address);
				const std::uint32_t member_object = ProjectConstexprObject(
					object_value, program_->entities[member.member_owner].type);
				if (member_object != kNoConstexprObject &&
					complete_object != kNoConstexprObject)
					SetExpressionSubobject(
						result, member_object, complete_object);
			}
			else
			{
				const ConstexprObjectElement* element =
					ConstexprClassMemberAt(object_value, selected);
				if (element) SetExpressionObjectElement(result, *element);
				if (has_pointer_value && pointer_value.integral > 0 &&
					object_address != kNoConstexprAddress)
				{
					const std::uint32_t member_address = OffsetConstexprAddress(
						object_address,
						pointer_value.integral - 1, true,
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
