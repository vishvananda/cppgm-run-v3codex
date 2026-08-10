#include "pa12_semantic_detail.h"

#include <climits>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

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
	// Preserve an unresolved overload set until a surrounding call or
	// constructor supplies the function-pointer target.  The target-directed
	// replay consumes the retained syntax and publishes one selected binding.
	if (operand.type == kNoType)
	{
		if (operation == "&" && target == kNoType) return operand;
		throw std::runtime_error("unresolved unary operand");
	}
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
	const std::uint32_t operand_object = ExpressionObject(operand);
	const std::uint32_t operand_complete_object =
		ExpressionCompleteObject(operand);
	(void)ApplyBuiltinUnaryConversion(operation, &operand);
	if (operation == "&" && operand.binding != kNoBinding)
		EnsureStaticMemberStorage(operand.binding, true);
	if (operation == "&" && operand.binding != kNoBinding &&
		program_->bindings[operand.binding].bit_field)
		throw std::runtime_error("address-of bit-field unsupported");
	TypeId result_type = EffectiveType(operand.type);
	ValueCategory category = VALUE_PRVALUE;
	bool constant = operand.constant;
	ConstexprScalarValue scalar;
	std::uint32_t address = ExpressionAddress(operand);
	if (address == kNoConstexprAddress &&
		(operation == "*" || operation == "+") &&
		IsPointer(Decay(operand.type)))
		address = LvalueAddress(&operand);
	std::uint32_t lvalue_address = kNoConstexprAddress;
	if (constant && address == kNoConstexprAddress)
		scalar = ExpressionScalar(operand);
	if (operation == "&")
	{
		if (operand.category != VALUE_LVALUE)
			throw std::runtime_error("address-of requires lvalue");
		if (target != kNoType &&
			program_->types.Get(program_->types.RemoveTopCv(target)).kind ==
				TYPE_MEMBER_POINTER)
			result_type = program_->types.RemoveTopCv(target);
		else result_type = program_->types.Pointer(result_type);
		address = LvalueAddress(&operand);
		constant = address != kNoConstexprAddress;
	}
	else if (operation == "*")
	{
		TypeId decayed = Decay(result_type);
		const TypeRecord pointer = program_->types.Get(decayed);
		if (pointer.kind != TYPE_POINTER)
			throw std::runtime_error("dereference requires pointer");
		result_type = pointer.child;
		category = VALUE_LVALUE;
		lvalue_address = address;
		constant = false;
	}
	else if (operation == "++" || operation == "--")
	{
		if (!IsModifiableLvalue(operand) ||
			(!IsArithmetic(result_type) && !IsPointer(result_type)) ||
			(IsPointer(result_type) &&
			 IsVoid(program_->types.Get(
				 program_->types.RemoveTopCv(result_type)).child)))
		{
			if (CandidateSubstitutionActive())
				return CandidateSubstitutionFailure();
			throw std::runtime_error("invalid increment operand");
		}
		category = postfix ? VALUE_PRVALUE : VALUE_LVALUE;
		constant = false;
		if (constexpr_evaluation_depth_ != 0 &&
			constant_evaluation_suppressed_depth_ == 0 &&
			operand.constexpr_local < constexpr_locals_.size() &&
			operand.constant)
		{
			ConstexprLocalValue& local =
				constexpr_locals_[operand.constexpr_local];
			if (IsPointer(result_type) &&
				local.address != kNoConstexprAddress)
			{
				const TypeRecord pointer = program_->types.Get(
					program_->types.RemoveTopCv(result_type));
				const std::int64_t step = static_cast<std::int64_t>(
					program_->SizeOf(pointer.child));
				const std::uint32_t previous = local.address;
				const std::uint32_t updated = OffsetConstexprAddress(previous,
					operation == "++" ? step : -step, false);
				if (updated != kNoConstexprAddress)
				{
					local.address = updated;
					address = postfix ? previous : updated;
					constant = true;
				}
			}
			else if (IsIntegral(result_type, true) || IsFloating(result_type))
			{
				const ConstexprScalarValue previous = local.value;
				const ConstexprScalarValue one = IsFloating(result_type) ?
					ConstexprScalarValue(1.0L) :
					ConstexprScalarValue(static_cast<std::int64_t>(1));
				const ConstexprScalarValue updated = ApplyConstantScalarBinary(
					operation == "++" ? "+" : "-", previous, one, result_type);
				local.value = updated;
				constant = true;
				scalar = postfix ? previous : updated;
			}
		}
	}
	else if (operation == "!")
	{
		if (!IsArithmetic(result_type) && !IsPointer(Decay(result_type)) &&
			!IsNullptr(result_type))
			throw std::runtime_error("invalid logical-not operand");
		result_type = program_->types.Fundamental(FUND_BOOL);
		if (constant) scalar = ConstexprScalarValue(
			static_cast<std::int64_t>(!ExpressionTruth(operand)));
		address = kNoConstexprAddress;
	}
	else if (operation == "+" || operation == "-" || operation == "~")
	{
		if (operation == "+" && IsPointer(Decay(result_type)))
		{
			result_type = Decay(result_type);
			constant = address != kNoConstexprAddress;
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
		{
			if (IsFloating(result_type))
			{
				if (operation == "-") scalar.floating = -scalar.floating;
				scalar = NormalizeScalarConstant(result_type, scalar);
			}
			else if (operation == "-")
			{
				const std::size_t width = IntegralWidth(result_type);
				const std::int64_t minimum = width == 64 ? INT64_MIN :
					- static_cast<std::int64_t>(
						std::uint64_t(1) << (width - 1));
				if (!IsUnsignedIntegral(result_type) && scalar.integral == minimum)
					throw std::runtime_error(
						"signed constant unary negation overflow");
				scalar.integral = static_cast<std::int64_t>(
					- static_cast<std::uint64_t>(scalar.integral));
			}
			else if (operation == "~") scalar.integral = ~scalar.integral;
			if (IsIntegral(result_type, true))
				scalar = NormalizeScalarConstant(result_type, scalar);
		}
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
	if (constant && address != kNoConstexprAddress)
		SetExpressionAddress(&result, address);
	else if (constant) SetExpressionScalar(&result, scalar);
	if (lvalue_address != kNoConstexprAddress)
		SetExpressionLvalueAddress(&result, lvalue_address);
	if (operation == "&" && operand_object != kNoConstexprObject)
		SetExpressionSubobject(
			&result, operand_object, operand_complete_object);
	if (operation == "*" && operand_object != kNoConstexprObject)
		SetExpressionSubobject(
			&result, operand_object, operand_complete_object);
	if (operation == "*" && lvalue_address != kNoConstexprAddress)
	{
		const ConstexprAddressValue* pointed =
			ConstexprAddressAt(lvalue_address);
		if (pointed && pointed->kind == CONSTEXPR_ADDRESS_BINDING &&
			pointed->identity < program_->bindings.size() &&
			program_->bindings[static_cast<BindingId>(pointed->identity)].constant &&
			program_->types.Get(program_->types.RemoveTopCv(EffectiveType(
				program_->bindings[static_cast<BindingId>(
					pointed->identity)].type))).kind == TYPE_ARRAY &&
			pointed->offset >= 0)
		{
			const std::uint32_t object = BindingObject(
				static_cast<BindingId>(pointed->identity));
			const std::int64_t step = static_cast<std::int64_t>(
				program_->SizeOf(result_type));
			if (object != kNoConstexprObject && step > 0 &&
				pointed->offset < pointed->upper_bound &&
				pointed->offset % step == 0)
			{
				const ConstexprObjectElement* element =
					ConstexprObjectElementAt(object,
						static_cast<std::size_t>(pointed->offset / step));
				if (element) SetExpressionObjectElement(&result, *element);
			}
		}
	}
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
	if (CandidateSubstitutionFailed()) return left;
	const std::string operation = PayloadSource(node);
	const bool short_circuit = left.constant &&
		((operation == "&&" && !ExpressionTruth(left)) ||
		 (operation == "||" && ExpressionTruth(left)));
	if (short_circuit) ++constant_evaluation_suppressed_depth_;
	ExpressionInfo right;
	try
	{
			right = AnalyzeExpression(right_syntax, scope);
	}
	catch (...)
	{
		if (short_circuit) --constant_evaluation_suppressed_depth_;
		throw;
	}
	if (short_circuit) --constant_evaluation_suppressed_depth_;
	if (CandidateSubstitutionFailed()) return right;
	return BuildBinaryExpression(operation, arena_->Payload(node),
		left_syntax, right_syntax, left, right, scope);
}

ExpressionInfo SemanticAnalyzer::BuildBinaryExpression(
	const std::string& operation, const std::string& display_operation,
	NodeId left_syntax, NodeId right_syntax, ExpressionInfo left,
	ExpressionInfo right, ScopeId scope)
{
	std::vector<NodeId> overloaded_syntax;
	overloaded_syntax.push_back(left_syntax);
	overloaded_syntax.push_back(right_syntax);
	std::vector<ExpressionInfo> overloaded_operands;
	overloaded_operands.push_back(left);
	overloaded_operands.push_back(right);
	std::vector<ConversionRank> builtin_ranks;
	const bool builtin_viable = ApplyBuiltinBinaryConversions(operation,
		&left, &right, &builtin_ranks, false);
	const bool builtin_competes = builtin_viable && operation != ",";
	ExpressionInfo overloaded;
	if (TryAnalyzeOverloadedOperator(operation, scope, overloaded_syntax,
		overloaded_operands, false, kNoType, &overloaded,
		builtin_competes ? &builtin_ranks : 0)) return overloaded;
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
			((right.integer_literal_zero && equality) || IsNullptr(right.type)))
			SetExpressionAddress(&right, NullConstexprAddress());
		else if (IsPointer(Decay(right.type)) &&
			((left.integer_literal_zero && equality) || IsNullptr(left.type)))
			SetExpressionAddress(&left, NullConstexprAddress());
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
		{
			if (IsVoid(program_->types.Get(Decay(left.type)).child))
			{
				if (CandidateSubstitutionActive())
					return CandidateSubstitutionFailure();
				throw std::runtime_error("arithmetic on pointer to void");
			}
			result_type = Decay(left.type);
		}
		else if (operation == "+" && IsIntegral(left.type) &&
			IsPointer(Decay(right.type)))
		{
			if (IsVoid(program_->types.Get(Decay(right.type)).child))
			{
				if (CandidateSubstitutionActive())
					return CandidateSubstitutionFailure();
				throw std::runtime_error("arithmetic on pointer to void");
			}
			result_type = Decay(right.type);
		}
		else if (operation == "-" && IsPointer(Decay(left.type)) &&
			IsPointer(Decay(right.type)))
		{
			if (IsVoid(program_->types.Get(Decay(left.type)).child) ||
				IsVoid(program_->types.Get(Decay(right.type)).child))
			{
				if (CandidateSubstitutionActive())
					return CandidateSubstitutionFailure();
				throw std::runtime_error("subtraction on pointer to void");
			}
			result_type = program_->types.Fundamental(FUND_LONG_INT);
		}
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
		if (operation == "<<" || operation == ">>") result_type =
			operand_type = IntegralPromotionType(left.type);
		else result_type = operand_type =
			CommonArithmeticType(left.type, right.type);
	}
	const std::uint32_t expression = MakeDump(DUMP_BINARY_EXPRESSION,
		result_type, result_category,
		program_->names.Intern(display_operation));
	dump_.nodes[expression].operand_type = operand_type;
	dump_.Add(expression, left.node);
	dump_.Add(expression, right.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = result_type;
	result.category = result_category;
	const bool short_circuit = left.constant &&
		((operation == "&&" && !ExpressionTruth(left)) ||
		 (operation == "||" && ExpressionTruth(left)));
	result.constant = constant_evaluation_suppressed_depth_ == 0 &&
		(short_circuit || (left.constant && right.constant));
	if (result.constant)
	{
		if (operation == ",")
		{
			result = right;
			result.node = expression;
			result.type = result_type;
			result.category = result_category;
			RecordExpressionFacts(result);
			++expression_count_;
			return result;
		}
		std::uint32_t left_address = ExpressionAddress(left);
		std::uint32_t right_address = ExpressionAddress(right);
		if (left_address == kNoConstexprAddress &&
			IsPointer(Decay(left.type)))
			left_address = LvalueAddress(&left);
		if (right_address == kNoConstexprAddress &&
			IsPointer(Decay(right.type)))
			right_address = LvalueAddress(&right);
		if (operation == "&&" || operation == "||")
		{
			SetExpressionScalar(&result, ConstexprScalarValue(
				static_cast<std::int64_t>(short_circuit ?
					ExpressionTruth(left) :
					(operation == "&&" ?
					 ExpressionTruth(left) && ExpressionTruth(right) :
					 ExpressionTruth(left) || ExpressionTruth(right)))));
			++expression_count_;
			return result;
		}
		if ((operation == "==" || operation == "!=" || operation == "<" ||
			operation == ">" || operation == "<=" || operation == ">=") &&
			(left_address != kNoConstexprAddress ||
			 right_address != kNoConstexprAddress))
		{
			const ConstexprAddressValue* a = ConstexprAddressAt(left_address);
			const ConstexprAddressValue* b = ConstexprAddressAt(right_address);
			if (!a || !b)
				result.constant = false;
			else
			{
				const bool same_base = a->kind == b->kind &&
					a->identity == b->identity;
				bool compared = false;
				if (operation == "==")
					compared = same_base && a->offset == b->offset;
				else if (operation == "!=")
					compared = !(same_base && a->offset == b->offset);
				else if (!same_base) result.constant = false;
				else if (operation == "<") compared = a->offset < b->offset;
				else if (operation == ">") compared = a->offset > b->offset;
				else if (operation == "<=") compared = a->offset <= b->offset;
				else compared = a->offset >= b->offset;
				if (result.constant) SetExpressionScalar(&result,
					ConstexprScalarValue(static_cast<std::int64_t>(compared)));
			}
			++expression_count_;
			return result;
		}
		if ((operation == "+" || operation == "-") &&
			(left_address != kNoConstexprAddress ||
			 right_address != kNoConstexprAddress))
		{
			if (left_address != kNoConstexprAddress &&
				right_address != kNoConstexprAddress)
			{
				const ConstexprAddressValue* a =
					ConstexprAddressAt(left_address);
				const ConstexprAddressValue* b =
					ConstexprAddressAt(right_address);
				if (operation != "-" || !a || !b || a->kind != b->kind ||
					a->identity != b->identity)
					result.constant = false;
				else
				{
					const TypeRecord pointer = program_->types.Get(
						program_->types.RemoveTopCv(Decay(left.type)));
					const std::int64_t step = static_cast<std::int64_t>(
						program_->SizeOf(pointer.child));
					if (step == 0 || (a->offset - b->offset) % step != 0)
						result.constant = false;
					else SetExpressionScalar(&result, ConstexprScalarValue(
						(a->offset - b->offset) / step));
				}
			}
			else
			{
				const bool pointer_left = left_address != kNoConstexprAddress;
				const ExpressionInfo& index = pointer_left ? right : left;
				const TypeId pointer_type = Decay(
					(pointer_left ? left : right).type);
				const TypeRecord pointer = program_->types.Get(
					program_->types.RemoveTopCv(pointer_type));
				const std::int64_t count = ExpressionScalar(index).integral;
				const std::int64_t step = static_cast<std::int64_t>(
					program_->SizeOf(pointer.child));
				if (step != 0 && (count >
					std::numeric_limits<std::int64_t>::max() / step ||
					count < std::numeric_limits<std::int64_t>::min() / step))
					result.constant = false;
				else
				{
					std::int64_t delta = count * step;
					if (operation == "-" && pointer_left) delta = -delta;
					const std::uint32_t advanced = OffsetConstexprAddress(
						pointer_left ? left_address : right_address,
						delta, false);
					if (advanced == kNoConstexprAddress)
						result.constant = false;
					else SetExpressionAddress(&result, advanced);
				}
			}
			++expression_count_;
			return result;
		}
		ConstexprScalarValue left_value = ExpressionScalar(left);
		ConstexprScalarValue right_value = ExpressionScalar(right);
		if (operand_type != kNoType &&
			(IsIntegral(operand_type, true) || IsFloating(operand_type)))
		{
			left_value = ConvertScalarConstant(
				left.type, operand_type, left_value);
			right_value = ConvertScalarConstant(
				right.type, operand_type, right_value);
		}
		SetExpressionScalar(&result, short_circuit ?
			ConstexprScalarValue(static_cast<std::int64_t>(
				ScalarTruth(left_value))) :
			ApplyConstantScalarBinary(operation, left_value, right_value,
				operand_type != kNoType ? operand_type : result_type));
	}
	++expression_count_;
	return result;
}

}
}
