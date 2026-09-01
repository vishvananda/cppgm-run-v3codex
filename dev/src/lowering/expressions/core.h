#pragma once

#include "lowering/core/source_types.h"
#include "lowering/ir/model.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "semantic/model/graph.h"

#include <cstdint>
#include <string>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

template <class Derived>
class ExpressionLowering
{
protected:
	Operand Convert(Operand value, const LowType& target,
		bool canonicalize_immediate = true)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (SameType(value.type, target) && (!IsInteger(value.type) || value.type.is_signed == target.is_signed))
		{
			value.type = target;
			return value;
		}
		if (IsInteger(value.type) && IsInteger(target) &&
			value.type.width == target.width)
		{
			if (value.kind == Operand::INTEGER)
			{
				value.type = target;
				return value;
			}
			const Operand result = derived.Temp(target);
			Instruction copy(Instruction::COPY);
			copy.dest = result.id;
			copy.type = target;
			copy.first = value;
			derived.Emit(copy);
			return result;
		}
		if (canonicalize_immediate && value.kind == Operand::INTEGER &&
			IsInteger(value.type) && IsInteger(target))
		{
			value.integer_value = CanonicalIntegerImmediate(
				value.integer_value, target.width, target.is_signed);
			value.integer_high = target.is_signed && value.integer_value < 0 ? ~std::uint64_t(0) : 0; value.type = target;
			return value;
		}
		if ((IsInteger(value.type) && target.kind == LOW_PTR) ||
			(value.type.kind == LOW_PTR && IsInteger(target)))
		{
			const Operand result = derived.Temp(target);
			Instruction copy(Instruction::COPY);
			copy.dest = result.id;
			copy.type = target;
			copy.first = value;
			derived.Emit(copy);
			return result;
		}
		Instruction instruction(Instruction::CONVERT);
		instruction.type = target;
		instruction.source_type = value.type;
		if (IsInteger(value.type) && IsInteger(target))
			instruction.op = target.width < value.type.width ? LOW_OP_TRUNC :
				value.type.is_signed ? LOW_OP_SEXT : LOW_OP_ZEXT;
		else if (IsInteger(value.type) && IsFloating(target))
			instruction.op = value.type.is_signed ? LOW_OP_SITOFP : LOW_OP_UITOFP;
		else if (IsFloating(value.type) && IsInteger(target))
			instruction.op = target.is_signed ? LOW_OP_FPTOSI : LOW_OP_FPTOUI;
		else if (IsFloating(value.type) && IsFloating(target))
			instruction.op = target.width < value.type.width ?
				LOW_OP_FPTRUNC : LOW_OP_FPEXT;
		else ThrowLoweringSource("unsupported PA15 scalar conversion");
		const Operand result = derived.Temp(target);
		instruction.dest = result.id;
		instruction.first = value;
		derived.Emit(instruction);
		return result;
	}

	bool IsBooleanType(TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const TypeRecord* record = &derived.program_.types.Get(type);
		while (record->kind == TYPE_QUALIFIED ||
			record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE)
		{
			type = record->child;
			record = &derived.program_.types.Get(type);
		}
		return record->kind == TYPE_FUNDAMENTAL &&
			record->fundamental == FUND_BOOL;
	}

	Operand LowerCondition(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[node];
		if (record.boolean_conversion)
		{
			const TypeId source_type = derived.program_.types.RemoveTopCv(record.type);
			const TypeRecord& source = derived.program_.types.Get(source_type);
			if (source.kind == TYPE_MEMBER_POINTER)
				return derived.LowerValue(node);
			return derived.LowerBooleanConversion(node, LowU8());
		}
		Operand value = derived.LowerValue(node);
		if (derived.IsBooleanType(record.type) || !IsFloating(value.type))
			return value;
		const Operand result = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = result.id;
		compare.op = LOW_OP_NE;
		compare.type = value.type;
		compare.first = value;
		compare.second = IsFloating(value.type) ? derived.FloatingOperand("0.0", value.type) :
			Operand(0, value.type);
		derived.Emit(compare);
		return result;
	}

	Operand LowerValue(std::uint32_t node, const LowType& expected = LowType())
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.stats_) ++derived.stats_->lowered_nodes;
		const DumpNode& record = derived.arena_.nodes[node];
		const NodeChildren children = derived.Children(node);
		Operand result;
		if (derived.TryLowerComplexValue(node, record, children, &result)) {}
		else if (record.kind == DUMP_TYPEID_EXPRESSION)
			result = derived.LowerTypeid(record, children);
		else if (record.kind == DUMP_DYNAMIC_CAST_EXPRESSION)
			result = derived.LowerDynamicCast(node, record, children);
		else if (record.kind == DUMP_THROW_EXPRESSION) result =
			derived.LowerThrowExpression(node, record, children);
		else if (record.kind == DUMP_STATEMENT_EXPRESSION)
			result = derived.IsClassObjectType(record.type) ?
				derived.AddressOfStorage(derived.LowerStatementExpressionStorage(node, record)) :
				derived.LowerStatementExpressionValue(node, record, children);
		else if ((record.category == VALUE_LVALUE || record.category == VALUE_XVALUE) &&
			derived.IsArrayType(record.type))
			result = record.kind == DUMP_LITERAL ?
				derived.AddressOfStorage(derived.LowerStorage(node)) :
				(record.kind == DUMP_CONDITIONAL_EXPRESSION ||
				 record.kind == DUMP_CALL_EXPRESSION) ?
				derived.LowerStorage(node) : derived.AddressOfStorage(derived.LowerStorage(node));
		else if (record.kind == DUMP_LITERAL)
		{
			const LowType type = derived.LowerType(record.type);
			if (record.null_member_pointer_constant ||
				(type.kind == LOW_PTR && record.constant &&
				 record.constant_value == 0 && record.value_initialization))
				result = Operand::NullPointer(type);
			else if (expected.kind == LOW_PTR &&
				derived.source_types_.IsNullptr(record.type))
			{
				result = derived.Temp(expected);
				Instruction copy(Instruction::COPY);
				copy.dest = result.id;
				copy.type = expected;
				copy.first = Operand::NullPointer(expected);
				derived.Emit(copy);
			}
			else if (IsFloating(type))
				result = derived.FloatingOperand(record.value_initialization ?
					"0.0" : derived.program_.names.Get(record.text), type);
			else
			{
				if (!record.constant)
					ThrowLoweringInternal("literal is missing its PA12 constant fact");
				result = Operand(record.constant_value, record.constant_high, type);
			}
		}
		else if (record.kind == DUMP_ID_EXPRESSION)
		{
			if (record.constant && record.binding != kNoBinding &&
				(derived.program_.IsStaticDataMember(record.binding) ||
				 (derived.program_.bindings[record.binding].kind == BIND_VARIABLE &&
				  !derived.HasLoweredStorage(record.binding)) ||
				 derived.program_.bindings[record.binding].kind == BIND_PARAMETER))
				result = Operand(record.constant_value, record.constant_high, derived.LowerExpressionType(record.type));
			else if (record.binding != kNoBinding && record.binding < derived.function_symbols_.size() &&
				derived.function_symbols_[record.binding] != kNoLowId)
			{
				result = derived.AddressOfStorage(Operand(Operand::FUNCTION,
					derived.function_symbols_[record.binding], LowPtr()));
			}
			else if (derived.IsFunctionType(record.type))
			{
				result = derived.LowerStorage(node);
			}
			else
			{
				const LowType type = derived.LowerExpressionType(record.type);
				const Operand storage = derived.LowerStorage(node);
				result = derived.LoadStorage(storage, type,
					derived.TypeIsVolatile(record.type));
			}
		}
		else if (record.kind == DUMP_SUBSCRIPT_EXPRESSION)
			result = derived.LoadStorage(derived.LowerStorage(node),
				derived.LowerExpressionType(record.type),
				derived.TypeIsVolatile(record.type));
		else if (record.kind == DUMP_MEMBER_EXPRESSION)
			result = derived.LowerMemberValue(node, record, children);
		else if (record.kind == DUMP_INITIALIZER_LIST_BEGIN ||
			record.kind == DUMP_INITIALIZER_LIST_SIZE ||
			record.kind == DUMP_INITIALIZER_LIST)
			result = derived.LowerInitializerListValue(node, record, children);
		else if (record.kind == DUMP_SIZEOF_EXPRESSION)
		{
			if (!record.constant)
				ThrowLoweringInternal("sizeof is missing its PA12 constant fact");
			const LowType type = derived.LowerExpressionType(record.type);
			result = derived.Temp(type);
			Instruction constant(Instruction::CONST);
			constant.dest = result.id;
			constant.type = type;
			constant.first = Operand(record.constant_value, record.constant_high, type);
			derived.Emit(constant);
		}
		else if (record.kind == DUMP_BINARY_EXPRESSION)
			result = derived.LowerBinary(node, record, children);
		else if (record.kind == DUMP_ASSIGNMENT_EXPRESSION)
			result = derived.LowerAssignment(record, children);
		else if (record.kind == DUMP_UNARY_EXPRESSION ||
			record.kind == DUMP_POSTFIX_EXPRESSION)
			result = derived.LowerUnary(record, children);
		else if (record.kind == DUMP_CALL_EXPRESSION)
		{
			result = derived.LowerCall(node, record, children);
			if (derived.IsReferenceType(record.type) &&
				!derived.IsFunctionType(derived.RemoveReference(record.type)))
				result = derived.LoadStorage(result,
					derived.LowerExpressionType(derived.RemoveReference(record.type)),
					derived.TypeIsVolatile(derived.RemoveReference(record.type)));
		}
		else if (record.kind == DUMP_NEW_EXPRESSION) result = derived.LowerNewExpression(node, record, children);
		else if (record.kind == DUMP_DELETE_EXPRESSION) result = derived.LowerDeleteExpression(node, record, children);
		else if (record.kind == DUMP_SPECIAL_MEMBER_CONSTRUCTION_ACTION)
			result = derived.LowerSpecialMemberConstruction(node);
		else if (record.kind == DUMP_CAST_EXPRESSION) {
			if (children.size() != 1) ThrowLoweringInternal("invalid semantic cast");
			if (derived.IsBooleanType(record.type)) result = derived.LowerBooleanConversion(children[0], derived.LowerExpressionType(record.type));
			else if (record.member_pointer_conversion)
				result = derived.LowerMemberPointerConversion(record, children);
			else if (derived.LowerExpressionType(record.type).kind == LOW_VOID)
			{
				const DumpNode& source = derived.arena_.nodes[children[0]];
				if ((source.category == VALUE_LVALUE ||
					 source.category == VALUE_XVALUE) &&
					(derived.IsClassObjectType(source.type) || derived.IsArrayType(source.type)))
					(void)derived.AddressOfStorage(derived.LowerStorage(children[0]));
				else (void)derived.LowerValue(children[0]);
				result = Operand(0, LowVoid());
			}
			else if (record.category == VALUE_LVALUE || record.category == VALUE_XVALUE)
				result = derived.LoadStorage(derived.LowerStorage(node),
					derived.LowerExpressionType(record.type),
					derived.TypeIsVolatile(record.type));
			else if (record.base_projection_count != 0)
				result = derived.LowerProjectedClassPointer(
					children[0], record.base_projection_count,
					record.base_projection_offset,
					record.has_base_projection_offset,
					derived.BaseEntityForType(record.type),
					record.inverse_base_projection);
			else
			{
				const DumpNode& source = derived.arena_.nodes[children[0]];
				const LowType type = derived.LowerExpressionType(record.type);
				result = IsIntNullPointerLiteralCast(derived.program_, source,
					record.type) ? Operand(0, type) :
					derived.LowerInitializerConvertedValue(children[0], type);
			}
		}
		else if (record.kind == DUMP_CONDITIONAL_EXPRESSION)
			result = derived.LowerConditional(node, record, children);
		else if (record.kind == DUMP_BRACED_INIT_LIST)
		{
			if (children.empty()) result = Operand(0, derived.LowerType(record.type));
			else if (children.size() == 1) result = derived.LowerValue(children[0],
				derived.LowerExpressionType(record.type));
			else ThrowLoweringSource("scalar initializer has excess elements");
		}
		else ThrowLoweringSource("semantic expression kind " +
			std::to_string(static_cast<unsigned>(record.kind)) +
			" is outside the active PA15 checkpoint");
		return expected.kind == LOW_INVALID ? result : derived.Convert(result, expected);
	}

	Operand LowerConvertedValue(std::uint32_t node, const LowType& target,
		bool canonicalize_immediate = true) {
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.arena_.nodes[node].boolean_conversion)
			return derived.LowerBooleanConversion(node, target);
		if (canonicalize_immediate && derived.CanonicalizeNullPointerImmediate(node, target)) return Operand(0, target);
		return derived.Convert(derived.LowerValue(node, target.kind == LOW_PTR ?
			target : LowType()), target, canonicalize_immediate);
	}

	bool CanonicalizeImmediateConversion(std::uint32_t node) const {
		const Derived& derived = static_cast<const Derived&>(*this); return derived.arena_.nodes[node].integer_narrowing_conversion; }

	void LowerDiscardedValue(std::uint32_t node) {
		Derived& derived = static_cast<Derived&>(*this); const DumpNode& record = derived.arena_.nodes[node];
		if (record.kind == DUMP_BINARY_EXPRESSION) { (void)derived.LowerBinary(node, record, derived.Children(node), true); return; }
		if ((record.category == VALUE_LVALUE || record.category == VALUE_XVALUE) && !derived.IsFunctionType(derived.RemoveReference(record.type))) (void)derived.LowerStorage(node);
		else (void)derived.LowerValue(node); }

	bool IsDirectNoreturnCall(std::uint32_t node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (node >= derived.arena_.nodes.size() ||
			derived.arena_.nodes[node].kind != DUMP_CALL_EXPRESSION) return false;
		const NodeChildren children = derived.Children(node);
		if (children.empty()) return false;
		const DumpNode& callee = derived.arena_.nodes[children[0]];
		if (callee.kind != DUMP_CALLEE || callee.binding == kNoBinding ||
			callee.binding >= derived.program_.bindings.size()) return false;
		const BindingRecord& binding = derived.program_.bindings[callee.binding];
		return binding.noreturn_function ||
			(binding.canonical < derived.program_.bindings.size() &&
			 derived.program_.bindings[binding.canonical].noreturn_function);
	}

	void TerminateAfterNoreturnCall()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.CurrentBlock().terminated)
			ThrowLoweringInternal("noreturn call follows a terminator");
		derived.CurrentBlock().terminated = true;
	}

	Operand LowerBinary(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children, bool discarded = false)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 2) ThrowLoweringInternal("invalid semantic binary");
		if (derived.IsMemberPointerApplication(record))
			return derived.LoadStorage(derived.LowerMemberPointerStorage(record, children),
				derived.LowerExpressionType(record.type));
		if (record.logical_operation != LOGICAL_OPERATION_NONE)
			return derived.LowerLogical(node, children,
				record.logical_operation == LOGICAL_OPERATION_AND);
		const int op = static_cast<int>(record.operation_kind) - 1;
		if (op == OP_COMMA)
		{
			derived.LowerDiscardedValue(children[0]);
			if (discarded) { derived.LowerDiscardedValue(children[1]); return Operand(0, LowVoid()); }
			return derived.LowerValue(children[1]);
		}
		const bool comparison = op == OP_EQ || op == OP_NE || op == OP_LT ||
			op == OP_LE || op == OP_GT || op == OP_GE;
		const bool left_pointer = derived.IsPointerLikeType(derived.arena_.nodes[children[0]].type);
		const bool right_pointer = derived.IsPointerLikeType(derived.arena_.nodes[children[1]].type);
		if ((op == OP_PLUS || op == OP_MINUS) && left_pointer && !right_pointer)
			return derived.LowerPointerOffset(children[0], children[1], op == OP_MINUS);
		if (op == OP_PLUS && !left_pointer && right_pointer)
		{
			const Operand offset = derived.LowerValue(children[0]), base = derived.LowerArrayPointer(children[1]);
			return derived.ApplyPointerOffset(base, offset, derived.PointeeType(derived.arena_.nodes[children[1]].type), false);
		}
		if (op == OP_MINUS && left_pointer && right_pointer)
			return derived.LowerPointerDifference(children[0], children[1]);
		if (record.operand_type == kNoType &&
			!(comparison && (left_pointer || right_pointer)))
			ThrowLoweringInternal("binary expression is missing its PA12 operand type");
		const LowType operand_type = record.operand_type == kNoType ?
			LowPtr() : derived.LowerExpressionType(record.operand_type);
		Operand left = derived.LowerValue(children[0], comparison ?
			derived.NullPointerExpectation(children[0], operand_type) : LowType());
		Operand right = derived.LowerValue(children[1], comparison ?
			derived.NullPointerExpectation(children[1], operand_type) : LowType());
		const bool canonical_pointer_difference_compare = comparison &&
			derived.arena_.nodes[children[0]].kind == DUMP_BINARY_EXPRESSION &&
			derived.arena_.nodes[children[0]].operand_type == kNoType &&
			derived.LowerExpressionType(derived.arena_.nodes[children[0]].type).kind == LOW_I64;
		const bool preserves_enum_conversion =
			(derived.arena_.nodes[children[0]].enum_arithmetic_conversion && !SameType(left.type, operand_type)) ||
			(derived.arena_.nodes[children[1]].enum_arithmetic_conversion && !SameType(right.type, operand_type));
		const bool canonicalize_immediates =
			derived.CanonicalizeAdditiveImmediates(children[0], op, comparison, preserves_enum_conversion) ||
			(comparison &&
			 ((left.kind == Operand::INTEGER && IsInteger(left.type) &&
			   left.type.width < operand_type.width) ||
			  (right.kind == Operand::INTEGER && IsInteger(right.type) &&
			   right.type.width < operand_type.width))) ||
			canonical_pointer_difference_compare ||
			(comparison && (derived.current_class_value_boundary_ ||
				derived.CallHasClassValueBoundary(children[0]) ||
				derived.CallHasClassValueBoundary(children[1]))) ||
			(comparison &&
			 (derived.arena_.nodes[children[0]].kind == DUMP_MEMBER_EXPRESSION ||
			  derived.arena_.nodes[children[1]].kind == DUMP_MEMBER_EXPRESSION ||
			  derived.arena_.nodes[children[0]].user_conversion_call || derived.arena_.nodes[children[1]].user_conversion_call));
		if (comparison && operand_type.kind == LOW_PTR &&
			left.kind == Operand::INTEGER && left.integer_value == 0)
			left.type = operand_type;
		else left = derived.Convert(left, operand_type, derived.CanonicalizeBinaryImmediate(
			children[0], operand_type, canonicalize_immediates, comparison,
			record.constant, record.template_layout_constant));
		if (comparison && operand_type.kind == LOW_PTR &&
			right.kind == Operand::INTEGER && right.integer_value == 0)
			right.type = operand_type;
		else right = derived.Convert(right, operand_type, derived.CanonicalizeBinaryImmediate(
			children[1], operand_type, canonicalize_immediates, comparison,
			record.constant, record.template_layout_constant));
		const LowType result_type = comparison ? LowI64() : derived.LowerType(record.type);
		const Operand result = derived.Temp(result_type);
		Instruction instruction(comparison ? Instruction::CMP : Instruction::BINARY);
		instruction.dest = result.id;
		instruction.type = operand_type;
		instruction.first = left;
		instruction.second = right;
		if (comparison)
		{
			instruction.op = op == OP_EQ ? LOW_OP_EQ : op == OP_NE ? LOW_OP_NE :
				op == OP_LT ? (operand_type.is_signed ? LOW_OP_LT : LOW_OP_ULT) :
				op == OP_LE ? (operand_type.is_signed ? LOW_OP_LE : LOW_OP_ULE) :
				op == OP_GT ? (operand_type.is_signed ? LOW_OP_GT : LOW_OP_UGT) :
				(operand_type.is_signed ? LOW_OP_GE : LOW_OP_UGE);
		}
		else
		{
			instruction.op = op == OP_PLUS ? LOW_OP_ADD :
				op == OP_MINUS ? LOW_OP_SUB :
				op == OP_STAR ? LOW_OP_MUL : op == OP_DIV ?
					(operand_type.is_signed || IsFloating(operand_type) ?
						LOW_OP_DIV : LOW_OP_UDIV) :
				op == OP_MOD ? (operand_type.is_signed ?
					LOW_OP_MOD : LOW_OP_UMOD) :
				op == OP_AMP ? LOW_OP_AND : op == OP_BOR ? LOW_OP_OR :
				op == OP_XOR ? LOW_OP_XOR : op == OP_LSHIFT ? LOW_OP_SHL :
				op == OP_RSHIFT ?
					(operand_type.is_signed ? LOW_OP_SHR : LOW_OP_USHR) :
				LOW_OP_NONE;
			if (instruction.op == LOW_OP_NONE)
				ThrowLoweringSource("unsupported binary operator");
		}
		derived.Emit(instruction);
		return result;
	}

	Operand LowerPointerOffset(std::uint32_t base_node,
		std::uint32_t offset_node, bool subtract)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand base = derived.LowerArrayPointer(base_node), offset = derived.LowerValue(offset_node);
		return derived.ApplyPointerOffset(base, offset, derived.PointeeType(derived.arena_.nodes[base_node].type), subtract);
	}

	Operand LowerPointerDifference(std::uint32_t left_node,
		std::uint32_t right_node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand left = derived.LowerArrayPointer(left_node);
		const Operand right = derived.LowerArrayPointer(right_node);
		const Operand bytes = derived.Temp(LowI64());
		Instruction subtract(Instruction::BINARY);
		subtract.dest = bytes.id;
		subtract.op = LOW_OP_SUB;
		subtract.type = LowPtr();
		subtract.first = left;
		subtract.second = right;
		derived.Emit(subtract);
		const std::size_t element_size = derived.program_.SizeOf(derived.PointeeType(derived.arena_.nodes[left_node].type));
		if (element_size == 1) return bytes;
		const Operand result = derived.Temp(LowI64());
		Instruction divide(Instruction::BINARY);
		divide.dest = result.id;
		divide.type = LowI64();
		divide.first = bytes;
		if ((element_size & (element_size - 1)) == 0)
		{
			// The difference of two pointers into one array is a multiple of
			// the element size, so the exact division is an arithmetic shift.
			std::size_t shift = 0;
			while ((element_size >> shift) != 1) ++shift;
			divide.op = LOW_OP_SHR;
			divide.second = Operand(static_cast<std::int64_t>(shift), LowI64());
		}
		else
		{
			divide.op = LOW_OP_DIV;
			divide.second =
				Operand(static_cast<std::int64_t>(element_size), LowI64());
		}
		derived.Emit(divide);
		return result;
	}

	Operand ApplyPointerOffset(const Operand& base, const Operand& raw_offset,
		TypeId element_type, bool subtract)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand offset = derived.Convert(raw_offset, LowI64());
		const std::size_t element_size = derived.program_.SizeOf(element_type);
		if (element_size == 1 && !subtract)
			return derived.IndexAddress(LowI8(), base, offset, false);
		const Operand scaled = element_size == 1 ? offset : derived.Temp(LowI64());
		if (element_size != 1) {
			Instruction multiply(Instruction::BINARY);
			multiply.dest = scaled.id;
			multiply.op = LOW_OP_MUL;
			multiply.type = LowI64();
			multiply.first = offset;
			multiply.second = Operand(static_cast<std::int64_t>(element_size), LowI64());
			derived.Emit(multiply);
		}
		Operand displacement = scaled;
		if (subtract)
		{
			displacement = derived.Temp(LowI64());
			Instruction negate(Instruction::BINARY);
			negate.dest = displacement.id;
			negate.op = LOW_OP_SUB;
			negate.type = LowI64();
			negate.first = Operand(0, LowI64());
			negate.second = scaled;
			derived.Emit(negate);
		}
		return derived.IndexAddress(LowI8(), base, displacement, false);
	}

	Operand LowerIncrement(const DumpNode& record, std::uint32_t operand_node,
		bool return_storage)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const int op = static_cast<int>(record.operation_kind) - 1;
		const Operand storage = derived.LowerStorage(operand_node);
		const BindingId bit_field = derived.BitFieldBinding(operand_node);
		const LowType type = bit_field == kNoBinding ?
			derived.LowerExpressionType(derived.arena_.nodes[operand_node].type) :
			derived.BitFieldAccessType(derived.program_.bindings[bit_field]);
		const bool volatile_access = bit_field == kNoBinding &&
			derived.TypeIsVolatile(derived.arena_.nodes[operand_node].type);
		Operand old_value = bit_field == kNoBinding ?
			derived.LoadStorage(storage, type, volatile_access) :
			derived.LoadBitField(bit_field, storage);
		if (bit_field != kNoBinding) old_value.type = type;
		Operand new_value;
		if (derived.IsPointerLikeType(derived.arena_.nodes[operand_node].type))
			new_value = derived.ApplyPointerOffset(old_value, Operand(1, LowI32()),
				derived.PointeeType(derived.arena_.nodes[operand_node].type), op == OP_DEC);
		else
		{
			new_value = derived.Temp(type);
			Instruction binary(Instruction::BINARY);
			binary.dest = new_value.id;
			binary.op = op == OP_INC ? LOW_OP_ADD : LOW_OP_SUB;
			binary.type = type;
			binary.first = old_value;
			binary.second = Operand(1, type);
			derived.Emit(binary);
		}
		if (bit_field == kNoBinding)
		{
			Instruction store(Instruction::STORE);
			store.type = type;
			store.first = new_value;
			store.second = storage;
			store.volatile_access = volatile_access;
			derived.Emit(store);
		}
		else
		{
			const Operand write_storage = return_storage ?
				derived.LowerStorage(operand_node) : storage;
			new_value = derived.StoreBitField(
				bit_field, write_storage, new_value, true);
		}
		if (return_storage) return storage;
		return record.kind == DUMP_POSTFIX_EXPRESSION ? old_value : new_value;
	}
};

}  // namespace lowering
}  // namespace cppgm
