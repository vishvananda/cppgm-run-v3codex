#include "pa12_semantic_detail.h"

#include <climits>
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
	(void)ApplyBuiltinUnaryConversion(operation, &operand);
	if (operation == "&" && operand.binding != kNoBinding)
		EnsureStaticMemberStorage(operand.binding, true);
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
		{
			if (operation == "-")
			{
				const std::size_t width = IntegralWidth(result_type);
				const std::int64_t minimum = width == 64 ? INT64_MIN :
					- static_cast<std::int64_t>(
						std::uint64_t(1) << (width - 1));
				if (!IsUnsignedIntegral(result_type) && value == minimum)
					throw std::runtime_error(
						"signed constant unary negation overflow");
				value = static_cast<std::int64_t>(
					- static_cast<std::uint64_t>(value));
			}
			else if (operation == "~") value = ~value;
			if (IsIntegral(result_type, true))
				value = NormalizeIntegralConstant(result_type, value);
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
	const std::string operation = PayloadSource(node);
	const bool short_circuit = left.constant &&
		((operation == "&&" && left.value == 0) ||
		 (operation == "||" && left.value != 0));
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
		((operation == "&&" && left.value == 0) ||
		 (operation == "||" && left.value != 0));
	result.constant = constant_evaluation_suppressed_depth_ == 0 &&
		(short_circuit || (left.constant && right.constant));
	if (result.constant)
		result.value = short_circuit ? left.value != 0 :
			ApplyConstantBinary(operation, left.value, right.value,
				operand_type != kNoType ? operand_type : result_type);
	++expression_count_;
	return result;
}

}
}
