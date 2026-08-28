#ifndef CPPGM_PA16_CALL_ARGUMENT_LOWERING_H
#define CPPGM_PA16_CALL_ARGUMENT_LOWERING_H

#include "lowering/support/utilities.h"
#include "lowering/ir/model.h"
#include "semantic/model/graph.h"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa16_lowering_detail
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;
using namespace pa15_lowering_support;

template <class Derived>
class CallArgumentLowering
{
protected:
	Operand EmitIntegerIntrinsicUnary(LowOperation operation,
		const Operand& value, const LowType& type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand result = derived.Temp(type);
		Instruction instruction(Instruction::UNARY);
		instruction.dest = result.id;
		instruction.op = operation;
		instruction.type = type;
		instruction.first = value;
		derived.Emit(instruction);
		return result;
	}

	Operand EmitIntegerIntrinsicBinary(LowOperation operation,
		const Operand& left, const Operand& right, const LowType& type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand result = derived.Temp(type);
		Instruction instruction(Instruction::BINARY);
		instruction.dest = result.id;
		instruction.op = operation;
		instruction.type = type;
		instruction.first = left;
		instruction.second = right;
		derived.Emit(instruction);
		return result;
	}

	Operand LowerIntegerIntrinsicPopcount(
		Operand value, const LowType& type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand one(1, type);
		Operand shifted = EmitIntegerIntrinsicBinary(
			LOW_OP_USHR, value, one, type);
		shifted = EmitIntegerIntrinsicBinary(LOW_OP_AND, shifted,
			Operand(static_cast<std::int64_t>(UINT64_C(0x5555555555555555)),
				type), type);
		value = EmitIntegerIntrinsicBinary(LOW_OP_SUB, value, shifted, type);
		Operand left = EmitIntegerIntrinsicBinary(LOW_OP_AND, value,
			Operand(static_cast<std::int64_t>(UINT64_C(0x3333333333333333)),
				type), type);
		shifted = EmitIntegerIntrinsicBinary(
			LOW_OP_USHR, value, Operand(2, type), type);
		Operand right = EmitIntegerIntrinsicBinary(LOW_OP_AND, shifted,
			Operand(static_cast<std::int64_t>(UINT64_C(0x3333333333333333)),
				type), type);
		value = EmitIntegerIntrinsicBinary(LOW_OP_ADD, left, right, type);
		shifted = EmitIntegerIntrinsicBinary(
			LOW_OP_USHR, value, Operand(4, type), type);
		value = EmitIntegerIntrinsicBinary(LOW_OP_ADD, value, shifted, type);
		value = EmitIntegerIntrinsicBinary(LOW_OP_AND, value,
			Operand(static_cast<std::int64_t>(UINT64_C(0x0f0f0f0f0f0f0f0f)),
				type), type);
		for (std::uint16_t shift = 8; shift < type.width; shift *= 2)
		{
			shifted = EmitIntegerIntrinsicBinary(
				LOW_OP_USHR, value, Operand(shift, type), type);
			value = EmitIntegerIntrinsicBinary(
				LOW_OP_ADD, value, shifted, type);
		}
		value = EmitIntegerIntrinsicBinary(LOW_OP_AND, value,
			Operand(0x7f, type), type);
		return derived.Convert(value, LowI32());
	}

	Operand LowerIntegerIntrinsicCount(
		hosted_builtin::IntegerIntrinsicOperation operation,
		Operand value, const LowType& type)
	{
		if (operation == hosted_builtin::INTEGER_OPERATION_POPCOUNT)
			return LowerIntegerIntrinsicPopcount(value, type);
		if (operation == hosted_builtin::INTEGER_OPERATION_CTZ)
		{
			const Operand negated = EmitIntegerIntrinsicUnary(
				LOW_OP_NEG, value, type);
			value = EmitIntegerIntrinsicBinary(
				LOW_OP_AND, value, negated, type);
			value = EmitIntegerIntrinsicBinary(
				LOW_OP_SUB, value, Operand(1, type), type);
			return LowerIntegerIntrinsicPopcount(value, type);
		}
		for (std::uint16_t shift = 1; shift < type.width; shift *= 2)
		{
			const Operand shifted = EmitIntegerIntrinsicBinary(
				LOW_OP_USHR, value, Operand(shift, type), type);
			value = EmitIntegerIntrinsicBinary(
				LOW_OP_OR, value, shifted, type);
		}
		value = EmitIntegerIntrinsicUnary(LOW_OP_BITNOT, value, type);
		return LowerIntegerIntrinsicPopcount(value, type);
	}

	bool TryLowerIntegerIntrinsicCall(const DumpNode& record,
		const NodeChildren& children, const BindingRecord& binding,
		Operand* result)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (binding.builtin_function !=
			BUILTIN_FUNCTION_HOSTED_INTEGER_INTRINSIC) return false;
		const hosted_builtin::IntegerIntrinsic& intrinsic =
			hosted_builtin::GetIntegerIntrinsic(
				binding.hosted_integer_intrinsic);
		if (children.size() != intrinsic.arity + 1)
			throw std::logic_error("invalid integer intrinsic call");
		TypeId argument_type = derived.arena_.nodes[children[1]].type;
		if (intrinsic.argument_rule !=
			hosted_builtin::INTEGER_ARGUMENT_GENERIC_UNSIGNED)
			argument_type = derived.program_.types.Parameters(binding.type)[0];
		const LowType type = derived.LowerType(argument_type);
		if (!IsInteger(type) || type.width > 64 || type.is_signed)
			throw std::logic_error("invalid lowered integer intrinsic type");
		Operand value = derived.LowerValue(children[1], type);
		if (intrinsic.operation == hosted_builtin::INTEGER_OPERATION_BSWAP)
		{
			if (type.width == 16)
			{
				const Operand low = EmitIntegerIntrinsicBinary(LOW_OP_SHL,
					EmitIntegerIntrinsicBinary(LOW_OP_AND, value,
						Operand(0xff, type), type), Operand(8, type), type);
				const Operand high = EmitIntegerIntrinsicBinary(LOW_OP_USHR,
					value, Operand(8, type), type);
				*result = EmitIntegerIntrinsicBinary(
					LOW_OP_OR, low, high, type);
			}
			else if (type.width == 32 || type.width == 64)
				*result = EmitIntegerIntrinsicUnary(LOW_OP_BSWAP, value, type);
			else throw std::logic_error("invalid byte-swap width");
			return true;
		}
		Operand count = LowerIntegerIntrinsicCount(
			intrinsic.operation, value, type);
		if (intrinsic.kind == hosted_builtin::INTEGER_INTRINSIC_CLZG)
		{
			const Operand nonzero = derived.Temp(LowI64());
			Instruction compare(Instruction::CMP);
			compare.dest = nonzero.id;
			compare.op = LOW_OP_NE;
			compare.type = type;
			compare.first = value;
			compare.second = Operand(0, type);
			derived.Emit(compare);
			const Operand selected = derived.Convert(nonzero, LowI32());
			const Operand unselected = EmitIntegerIntrinsicBinary(LOW_OP_SUB,
				Operand(1, LowI32()), selected, LowI32());
			count = EmitIntegerIntrinsicBinary(LOW_OP_MUL,
				count, selected, LowI32());
			const Operand fallback = derived.LowerValue(
				children[2], LowI32());
			const Operand zero = EmitIntegerIntrinsicBinary(LOW_OP_MUL,
				fallback, unselected, LowI32());
			count = EmitIntegerIntrinsicBinary(
				LOW_OP_ADD, count, zero, LowI32());
		}
		*result = derived.Convert(count, derived.LowerType(record.type));
		return true;
	}

	Operand EmitFloatingIntrinsicCompare(LowOperation operation,
		const Operand& left, const Operand& right)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand compared = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = compared.id;
		compare.op = operation;
		compare.type = left.type;
		compare.first = left;
		compare.second = right;
		derived.Emit(compare);
		return derived.Convert(compared, LowI32());
	}

	Operand FloatingIntrinsicNot(const Operand& value)
	{
		return EmitIntegerIntrinsicBinary(
			LOW_OP_SUB, Operand(1, LowI32()), value, LowI32());
	}

	Operand FloatingIntrinsicAnd(const Operand& left, const Operand& right)
	{
		return EmitIntegerIntrinsicBinary(
			LOW_OP_AND, left, right, LowI32());
	}

	Operand FloatingIntrinsicOr(const Operand& left, const Operand& right)
	{
		return EmitIntegerIntrinsicBinary(
			LOW_OP_OR, left, right, LowI32());
	}

	std::string FloatingIntrinsicLiteral(
		const char* spelling, const LowType& type) const
	{
		return std::string(spelling) +
			(type.width == 32 ? "f" : type.width == 80 ? "L" : "");
	}

	Operand FloatingIntrinsicIsNan(const Operand& value)
	{
		return EmitFloatingIntrinsicCompare(LOW_OP_NE, value, value);
	}

	Operand FloatingIntrinsicIsInf(const Operand& value)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand infinity = derived.FloatingOperand(
			FloatingIntrinsicLiteral("INFINITY", value.type), value.type);
		const Operand negative_infinity = EmitIntegerIntrinsicUnary(
			LOW_OP_NEG, infinity, value.type);
		const Operand positive =
			EmitFloatingIntrinsicCompare(LOW_OP_EQ, value, infinity);
		const Operand negative =
			EmitFloatingIntrinsicCompare(LOW_OP_EQ, value, negative_infinity);
		return FloatingIntrinsicOr(positive, negative);
	}

	Operand FloatingIntrinsicIsFinite(const Operand& value)
	{
		const Operand nan = FloatingIntrinsicIsNan(value);
		const Operand not_nan = FloatingIntrinsicNot(nan);
		const Operand inf = FloatingIntrinsicIsInf(value);
		const Operand not_inf = FloatingIntrinsicNot(inf);
		return FloatingIntrinsicAnd(not_nan, not_inf);
	}

	Operand FloatingIntrinsicIsNormal(const Operand& value)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const char* minimum = value.type.width == 32 ?
			"1.17549435082228750796873653722224568e-38f" :
			value.type.width == 64 ?
			"2.22507385850720138309023271733240406e-308" :
			"3.36210314311209350626267781732175260e-4932L";
		const Operand positive = derived.FloatingOperand(minimum, value.type);
		const Operand negative = EmitIntegerIntrinsicUnary(
			LOW_OP_NEG, positive, value.type);
		const Operand positive_magnitude =
			EmitFloatingIntrinsicCompare(LOW_OP_GE, value, positive);
		const Operand negative_magnitude =
			EmitFloatingIntrinsicCompare(LOW_OP_LE, value, negative);
		const Operand magnitude =
			FloatingIntrinsicOr(positive_magnitude, negative_magnitude);
		const Operand finite = FloatingIntrinsicIsFinite(value);
		return FloatingIntrinsicAnd(finite, magnitude);
	}

	Operand FloatingIntrinsicSignbit(const Operand& value)
	{
		Derived& derived = static_cast<Derived&>(*this);
		LowType word_type;
		std::int64_t word_offset = 0;
		if (value.type.width == 32) word_type = LowU32();
		else if (value.type.width == 64) word_type = LowU64();
		else if (value.type.width == 80)
		{
			word_type = LowU16();
			word_offset = 8;
		}
		else throw std::logic_error("invalid floating signbit width");

		const Operand slot(derived.CreateGeneratedSlot(
			"floating_signbit", value.type), value.type);
		const Operand address = derived.AddressOfStorage(slot);
		Instruction store(Instruction::STORE);
		store.type = value.type;
		store.first = value;
		store.second = address;
		derived.Emit(store);
		const Operand word_address = word_offset == 0 ? address :
			derived.IndexAddress(LowI8(), address,
				Operand(word_offset, LowI64()), false);
		const Operand word = derived.LoadStorage(word_address, word_type);
		const Operand sign = EmitIntegerIntrinsicBinary(LOW_OP_USHR, word,
			Operand(word_type.width - 1, word_type), word_type);
		return derived.Convert(sign, LowI32());
	}

	bool TryLowerFloatingIntrinsicCall(const DumpNode& record,
		const NodeChildren& children, const BindingRecord& binding,
		Operand* result)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (binding.builtin_function !=
			BUILTIN_FUNCTION_HOSTED_FLOATING_INTRINSIC) return false;
		const hosted_builtin::FloatingIntrinsic& intrinsic =
			hosted_builtin::GetFloatingIntrinsic(
				binding.hosted_floating_intrinsic);
		if (children.size() != intrinsic.arity + 1)
			throw std::logic_error("invalid floating intrinsic call");
		if (intrinsic.operation ==
			hosted_builtin::FLOATING_OPERATION_EXTERNAL_CEIL)
			return false;
		if (intrinsic.operation ==
			hosted_builtin::FLOATING_OPERATION_ROUNDING_MODE)
		{
			*result = derived.Convert(
				Operand(1, LowI32()), derived.LowerType(record.type));
			return true;
		}
		if (intrinsic.operation ==
			hosted_builtin::FLOATING_OPERATION_INFINITY)
		{
			const LowType type = derived.LowerType(record.type);
			*result = derived.FloatingOperand(
				FloatingIntrinsicLiteral("INFINITY", type), type);
			return true;
		}
		if (intrinsic.operation == hosted_builtin::FLOATING_OPERATION_NAN ||
			intrinsic.operation == hosted_builtin::FLOATING_OPERATION_SNAN)
		{
			(void)derived.LowerValue(children[1]);
			const LowType type = derived.LowerType(record.type);
			const bool signaling = intrinsic.operation ==
				hosted_builtin::FLOATING_OPERATION_SNAN;
			*result = derived.FloatingOperand(FloatingIntrinsicLiteral(
				signaling ? "snan" : "nan", type), type);
			return true;
		}
		const std::size_t value_index = intrinsic.operation ==
			hosted_builtin::FLOATING_OPERATION_CLASSIFY ? 6 : 1;
		const LowType value_type = derived.LowerExpressionType(
			derived.arena_.nodes[children[value_index]].type);
		if (!IsFloating(value_type))
			throw std::logic_error("floating intrinsic lost operand type");
		const Operand value =
			derived.LowerValue(children[value_index], value_type);
		Operand classified;
		switch (intrinsic.operation)
		{
		case hosted_builtin::FLOATING_OPERATION_ISFINITE:
			classified = FloatingIntrinsicIsFinite(value); break;
		case hosted_builtin::FLOATING_OPERATION_ISINF:
			classified = FloatingIntrinsicIsInf(value); break;
		case hosted_builtin::FLOATING_OPERATION_ISNAN:
			classified = FloatingIntrinsicIsNan(value); break;
		case hosted_builtin::FLOATING_OPERATION_ISNORMAL:
			classified = FloatingIntrinsicIsNormal(value); break;
		case hosted_builtin::FLOATING_OPERATION_SIGNBIT:
			classified = FloatingIntrinsicSignbit(value); break;
		case hosted_builtin::FLOATING_OPERATION_CLASSIFY:
		{
			const Operand nan = FloatingIntrinsicIsNan(value);
			const Operand inf = FloatingIntrinsicIsInf(value);
			const Operand not_nan = FloatingIntrinsicNot(nan);
			const Operand not_inf = FloatingIntrinsicNot(inf);
			const Operand finite = FloatingIntrinsicAnd(not_nan, not_inf);
			const Operand zero_value = derived.FloatingOperand(
				FloatingIntrinsicLiteral("0.0", value.type), value.type);
			const Operand zero = EmitFloatingIntrinsicCompare(LOW_OP_EQ,
				value, zero_value);
			const Operand normal = FloatingIntrinsicIsNormal(value);
			const Operand not_zero = FloatingIntrinsicNot(zero);
			const Operand not_normal = FloatingIntrinsicNot(normal);
			const Operand nonzero_abnormal =
				FloatingIntrinsicAnd(not_zero, not_normal);
			const Operand subnormal =
				FloatingIntrinsicAnd(finite, nonzero_abnormal);
			const Operand predicates[] = {nan, inf, normal, subnormal, zero};
			classified = Operand(0, LowI32());
			for (std::size_t i = 0; i < 5; ++i)
			{
				const Operand control =
					derived.LowerValue(children[i + 1], LowI32());
				const Operand selected = EmitIntegerIntrinsicBinary(
					LOW_OP_MUL, control, predicates[i], LowI32());
				classified = EmitIntegerIntrinsicBinary(
					LOW_OP_ADD, classified, selected, LowI32());
			}
			break;
		}
		case hosted_builtin::FLOATING_OPERATION_ROUNDING_MODE:
		case hosted_builtin::FLOATING_OPERATION_INFINITY:
		case hosted_builtin::FLOATING_OPERATION_NAN:
		case hosted_builtin::FLOATING_OPERATION_SNAN:
		case hosted_builtin::FLOATING_OPERATION_EXTERNAL_CEIL:
			throw std::logic_error("invalid floating intrinsic dispatch");
		}
		*result = derived.Convert(classified, derived.LowerType(record.type));
		return true;
	}

	bool TryLowerNumericBuiltinCall(const DumpNode& record,
		const NodeChildren& children, Operand* result)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.empty()) return false;
		const DumpNode& callee = derived.arena_.nodes[children[0]];
		if (callee.kind != DUMP_CALLEE || callee.binding == kNoBinding ||
			callee.binding >= derived.program_.bindings.size()) return false;
		const BuiltinFunctionKind kind =
			derived.program_.bindings[callee.binding].builtin_function;
		if (TryLowerIntegerIntrinsicCall(record, children,
			derived.program_.bindings[callee.binding], result)) return true;
		if (TryLowerFloatingIntrinsicCall(record, children,
			derived.program_.bindings[callee.binding], result)) return true;
		if (kind == BUILTIN_FUNCTION_NANL)
		{
			if (children.size() != 2)
				throw std::logic_error("invalid nanl builtin call");
			(void)derived.LowerValue(children[1]);
			*result = derived.FloatingOperand("nanL", LowF80());
			return true;
		}
		if (kind != BUILTIN_FUNCTION_ISNAN) return false;
		if (children.size() != 2)
			throw std::logic_error("invalid isnan builtin call");
		const Operand value = derived.LowerConvertedValue(
			children[1], LowF80(), false);
		const Operand compared = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = compared.id;
		compare.op = LOW_OP_NE;
		compare.type = LowF80();
		compare.first = value;
		compare.second = value;
		derived.Emit(compare);
		*result = derived.Convert(compared, derived.LowerType(record.type));
		return true;
	}

	LowType AtomicTransferType(const DumpNode& record) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const LowType value = derived.LowerExpressionType(record.operand_type);
		if (value.kind != LOW_OBJECT) return value;
		switch (value.width)
		{
		case 8: return LowU8();
		case 16: return LowU16();
		case 32: return LowU32();
		case 64: return LowU64();
		case 128: return LowU128();
		default:
			throw std::runtime_error(
				"atomic object width has no native LowIR representation");
		}
	}

	std::uint8_t AtomicOrderAt(const NodeChildren& children,
		std::size_t index, std::uint8_t fallback)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (index >= children.size()) return fallback;
		const DumpNode& order = derived.arena_.nodes[children[index]];
		if (order.runtime_atomic_order)
		{
			(void)derived.LowerValue(children[index], LowI32());
			return 5;
		}
		if (!order.constant || order.constant_value < 0 ||
			order.constant_value > 5)
			throw std::logic_error("atomic order lost its semantic constant fact");
		return static_cast<std::uint8_t>(order.constant_value);
	}

	std::uint8_t AtomicFailureOrder(std::uint8_t success) const
	{
		return success == 3 ? 0 : success == 4 ? 2 : success;
	}

	Operand LowerAtomicValue(std::uint32_t node, const LowType& transfer)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& source_node = derived.arena_.nodes[node];
		const TypeId source_type = source_node.kind == DUMP_CONSTRUCTOR_ACTION ?
			source_node.operand_type : source_node.type;
		if (!derived.IsClassObjectType(source_type))
			return derived.LowerConvertedValue(node, transfer, false);
		if (source_node.kind == DUMP_CLASS_VALUE_TRANSFER ||
			source_node.kind == DUMP_CONSTRUCTOR_ACTION ||
			source_node.kind == DUMP_AGGREGATE_CONSTRUCTION_ACTION)
		{
			const LowType object = derived.LowerStorageType(source_type);
			const Operand slot(derived.CreateGeneratedSlot("atomic_arg", object),
				object);
			const Operand address = derived.AddressOfStorage(slot);
			if (source_node.kind == DUMP_CLASS_VALUE_TRANSFER)
				derived.LowerClassValueTransfer(node, address);
			else if (source_node.kind == DUMP_AGGREGATE_CONSTRUCTION_ACTION)
				derived.LowerAggregateConstructionAction(node, address);
			else
			{
				if (source_node.value_initialization)
					derived.EmitZeroInitialization(source_type, address);
				derived.LowerConstructorAction(node, address);
			}
			return derived.LoadStorage(address, transfer);
		}
		const Operand source = derived.LowerClassTransferSource(node);
		if (source.type.kind == LOW_PTR)
			return derived.LoadStorage(source, transfer);
		if (source.type.kind != LOW_OBJECT)
			throw std::logic_error("atomic class value has invalid storage form");
		const Operand slot(derived.CreateGeneratedSlot(
			"atomic_arg", source.type), source.type);
		const Operand address = derived.AddressOfStorage(slot);
		derived.EmitClassObjectCopy(source_type, source, address);
		return derived.LoadStorage(address, transfer);
	}

	Operand RepackAtomicResult(std::uint32_t node, const DumpNode& record,
		const Operand& value, const LowType& transfer)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const LowType result_type = derived.LowerExpressionType(record.type);
		if (result_type.kind != LOW_OBJECT)
			return derived.Convert(value, result_type);
		const Operand slot(derived.CreateGeneratedSlot(
			"atomic_result", result_type), result_type);
		Instruction store(Instruction::STORE);
		store.type = transfer;
		store.first = value;
		store.second = slot;
		derived.Emit(store);
		(void)node;
		return derived.AddressOfStorage(slot);
	}

	Operand EmitAtomicLoad(const Operand& pointer, const LowType& type,
		std::uint8_t order)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand result = derived.Temp(type);
		Instruction load(Instruction::ATOMIC_LOAD);
		load.dest = result.id;
		load.type = type;
		load.first = pointer;
		load.atomic_order = order;
		derived.Emit(load);
		return result;
	}

	void EmitAtomicStore(const Operand& pointer, const Operand& value,
		const LowType& type, std::uint8_t order)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Instruction store(Instruction::ATOMIC_STORE);
		store.type = type;
		store.first = value;
		store.second = pointer;
		store.atomic_order = order;
		derived.Emit(store);
	}

	Operand EmitAtomicExchange(const Operand& pointer, const Operand& value,
		const LowType& type, std::uint8_t order)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand result = derived.Temp(type);
		Instruction exchange(Instruction::ATOMIC_EXCHANGE);
		exchange.dest = result.id;
		exchange.type = type;
		exchange.first = pointer;
		exchange.second = value;
		exchange.atomic_order = order;
		derived.Emit(exchange);
		return result;
	}

	Operand EmitAtomicAddFetch(const Operand& pointer, const Operand& delta,
		const LowType& type, std::uint8_t order)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand result = derived.Temp(type);
		Instruction update(Instruction::ATOMIC_ADD_FETCH);
		update.dest = result.id;
		update.type = type;
		update.first = pointer;
		update.second = delta;
		update.atomic_order = order;
		derived.Emit(update);
		return result;
	}

	Operand EmitAtomicCompareExchange(const Operand& pointer,
		const Operand& expected, const Operand& desired, const LowType& type,
		std::uint8_t success_order, std::uint8_t failure_order)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand result = derived.Temp(LowI64());
		Instruction compare(Instruction::ATOMIC_COMPARE_EXCHANGE);
		compare.dest = result.id;
		compare.type = type;
		compare.first = pointer;
		compare.second = expected;
		compare.third = desired;
		compare.atomic_order = success_order;
		compare.atomic_failure_order = failure_order;
		derived.Emit(compare);
		return result;
	}

	Operand AtomicTruthValue(const Operand& value, const LowType& type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand result = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = result.id;
		compare.op = LOW_OP_NE;
		compare.type = type;
		compare.first = value;
		compare.second = Operand(0, type);
		derived.Emit(compare);
		return result;
	}

	Operand LowerAtomicCasFetch(std::uint32_t node, const Operand& pointer,
		const Operand& argument, const LowType& type, LowOperation operation,
		std::uint8_t order, bool result_is_new)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand expected_slot(derived.CreateGeneratedSlot(
			"atomic_expected", type), type);
		const Operand initial = EmitAtomicLoad(pointer, type, 0);
		Instruction initialize(Instruction::STORE);
		initialize.type = type;
		initialize.first = initial;
		initialize.second = expected_slot;
		derived.Emit(initialize);
		const BlockId loop = derived.AddBlock(derived.NewLabel("atomic_retry"));
		const BlockId done = derived.AddBlock(derived.NewLabel("atomic_done"));
		derived.EmitJump(loop);
		derived.SelectBlock(loop);
		const Operand expected = derived.LoadStorage(expected_slot, type);
		const Operand desired = EmitIntegerIntrinsicBinary(
			operation, expected, argument, type);
		const Operand exchanged = EmitAtomicCompareExchange(pointer,
			derived.AddressOfStorage(expected_slot), desired, type,
			order, AtomicFailureOrder(order));
		derived.EmitBranch(exchanged, done, loop);
		derived.SelectBlock(done);
		const Operand old = derived.LoadStorage(expected_slot, type);
		(void)node;
		return result_is_new ? desired : old;
	}

	bool TryLowerAtomicIntrinsicCall(const DumpNode& record,
		const NodeChildren& children, Operand* result)
	{
		using namespace hosted_builtin;
		Derived& derived = static_cast<Derived&>(*this);
		if (record.hosted_atomic_intrinsic == ATOMIC_INTRINSIC_NONE)
			return false;
		const AtomicIntrinsic& intrinsic =
			GetAtomicIntrinsic(record.hosted_atomic_intrinsic);
		if (children.size() != intrinsic.arity + 1)
			throw std::logic_error("invalid lowered atomic intrinsic arity");
		if (intrinsic.shape == ATOMIC_SHAPE_FENCE)
		{
			Instruction fence(
				record.hosted_atomic_intrinsic == ATOMIC_INTRINSIC_SIGNAL_FENCE ||
				record.hosted_atomic_intrinsic == ATOMIC_INTRINSIC_C11_SIGNAL_FENCE ?
					Instruction::ATOMIC_SIGNAL_FENCE :
					Instruction::ATOMIC_THREAD_FENCE);
			fence.atomic_order = AtomicOrderAt(children, 1, 5);
			derived.Emit(fence);
			*result = Operand(0, LowVoid());
			return true;
		}
		if (children.size() < 2)
			throw std::logic_error("atomic intrinsic has no object argument");
		const LowType type = AtomicTransferType(record);
		const Operand pointer = derived.LowerValue(children[1], LowPtr());
		const std::uint8_t default_order =
			record.hosted_atomic_intrinsic == ATOMIC_INTRINSIC_SYNC_LOCK_RELEASE ?
				3 : record.hosted_atomic_intrinsic ==
					ATOMIC_INTRINSIC_SYNC_LOCK_TEST_AND_SET ? 2 : 5;
		switch (intrinsic.shape)
		{
		case ATOMIC_SHAPE_LOAD:
		{
			const Operand loaded = EmitAtomicLoad(pointer, type,
				AtomicOrderAt(children, 2, default_order));
			*result = RepackAtomicResult(children[0], record, loaded, type);
			return true;
		}
		case ATOMIC_SHAPE_LOAD_OUT:
		{
			const Operand output = derived.LowerValue(children[2], LowPtr());
			const Operand loaded = EmitAtomicLoad(pointer, type,
				AtomicOrderAt(children, 3, default_order));
			Instruction store(Instruction::STORE);
			store.type = type;
			store.first = loaded;
			store.second = output;
			derived.Emit(store);
			*result = Operand(0, LowVoid());
			return true;
		}
		case ATOMIC_SHAPE_STORE:
		{
			const Operand value = LowerAtomicValue(children[2], type);
			EmitAtomicStore(pointer, value, type,
				AtomicOrderAt(children, 3, 0));
			*result = Operand(0, LowVoid());
			return true;
		}
		case ATOMIC_SHAPE_STORE_FROM:
		{
			const Operand input = derived.LowerValue(children[2], LowPtr());
			const Operand value = derived.LoadStorage(input, type);
			EmitAtomicStore(pointer, value, type,
				AtomicOrderAt(children, 3, default_order));
			*result = Operand(0, LowVoid());
			return true;
		}
		case ATOMIC_SHAPE_EXCHANGE:
		{
			const Operand value = LowerAtomicValue(children[2], type);
			const Operand old = EmitAtomicExchange(pointer, value, type,
				AtomicOrderAt(children, 3, default_order));
			*result = RepackAtomicResult(children[0], record, old, type);
			return true;
		}
		case ATOMIC_SHAPE_COMPARE_EXCHANGE:
		{
			const Operand expected = derived.LowerValue(children[2], LowPtr());
			const Operand desired = LowerAtomicValue(children[3], type);
			std::size_t success_index = 4;
			if (record.hosted_atomic_intrinsic ==
				ATOMIC_INTRINSIC_COMPARE_EXCHANGE_N)
			{
				(void)derived.LowerValue(children[4], LowU8());
				success_index = 5;
			}
			const Operand exchanged = EmitAtomicCompareExchange(pointer,
				expected, desired, type,
				AtomicOrderAt(children, success_index, default_order),
				AtomicOrderAt(children, success_index + 1, 0));
			*result = derived.Convert(exchanged,
				derived.LowerExpressionType(record.type));
			return true;
		}
		case ATOMIC_SHAPE_SYNC_COMPARE_EXCHANGE:
		{
			const Operand expected_value = LowerAtomicValue(children[2], type);
			const Operand desired = LowerAtomicValue(children[3], type);
			const Operand expected(derived.CreateGeneratedSlot(
				"atomic_expected", type), type);
			Instruction initialize(Instruction::STORE);
			initialize.type = type;
			initialize.first = expected_value;
			initialize.second = expected;
			derived.Emit(initialize);
			(void)EmitAtomicCompareExchange(pointer,
				derived.AddressOfStorage(expected), desired, type, 5, 5);
			*result = RepackAtomicResult(children[0], record,
				derived.LoadStorage(expected, type), type);
			return true;
		}
		case ATOMIC_SHAPE_FETCH_UPDATE:
		{
			Operand argument = LowerAtomicValue(children[2], type);
			const std::uint8_t order = AtomicOrderAt(children, 3, default_order);
			Operand value;
			if (intrinsic.update == ATOMIC_UPDATE_ADD ||
				intrinsic.update == ATOMIC_UPDATE_SUB)
			{
				if (intrinsic.update == ATOMIC_UPDATE_SUB)
					argument = EmitIntegerIntrinsicUnary(LOW_OP_NEG, argument, type);
				const Operand updated = EmitAtomicAddFetch(
					pointer, argument, type, order);
				value = intrinsic.result_is_new ? updated :
					EmitIntegerIntrinsicBinary(
						LOW_OP_SUB, updated, argument, type);
			}
			else
			{
				const LowOperation operation =
					intrinsic.update == ATOMIC_UPDATE_AND ? LOW_OP_AND :
					intrinsic.update == ATOMIC_UPDATE_OR ? LOW_OP_OR : LOW_OP_XOR;
				value = LowerAtomicCasFetch(children[0], pointer, argument, type,
					operation, order, intrinsic.result_is_new);
			}
			*result = RepackAtomicResult(children[0], record, value, type);
			return true;
		}
		case ATOMIC_SHAPE_TEST_AND_SET:
		{
			const Operand old = EmitAtomicExchange(pointer, Operand(1, type), type,
				AtomicOrderAt(children, 2, default_order));
			*result = derived.Convert(AtomicTruthValue(old, type),
				derived.LowerExpressionType(record.type));
			return true;
		}
		case ATOMIC_SHAPE_CLEAR:
			EmitAtomicStore(pointer, Operand(0, type), type,
				AtomicOrderAt(children, 2, default_order));
			*result = Operand(0, LowVoid());
			return true;
		case ATOMIC_SHAPE_LOCK_FREE:
		case ATOMIC_SHAPE_FENCE: break;
		}
		throw std::logic_error("unhandled atomic intrinsic lowering shape");
	}

	bool TryLowerCompilerBuiltinCall(const DumpNode& record,
		const NodeChildren& children, Operand* result)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.empty()) return false;
		const DumpNode& callee = derived.arena_.nodes[children[0]];
		if (callee.kind != DUMP_CALLEE) return false;
		if (record.hosted_vector_intrinsic !=
			hosted_builtin::VECTOR_INTRINSIC_NONE)
		{
			const hosted_builtin::VectorIntrinsic& intrinsic =
				hosted_builtin::GetVectorIntrinsic(
					record.hosted_vector_intrinsic);
			const std::size_t arity = intrinsic.operation ==
				hosted_builtin::VECTOR_OPERATION_INIT ? intrinsic.lane_count : 2;
			if (children.size() != arity + 1)
				throw std::logic_error("invalid vector intrinsic call");
			const LowType element =
				intrinsic.element == hosted_builtin::VECTOR_ELEMENT_I8 ? LowI8() :
				intrinsic.element == hosted_builtin::VECTOR_ELEMENT_I16 ? LowI16() :
				LowI32();
			const LowType vector = LowObject(8, 8);
			if (intrinsic.operation == hosted_builtin::VECTOR_OPERATION_EXTRACT)
			{
				const Operand source = derived.LowerValue(children[1], vector);
				const DumpNode& lane = derived.arena_.nodes[children[2]];
				if (!lane.constant_value && !lane.constant)
					throw std::logic_error("vector extraction lost its lane");
				const Operand address = derived.IndexAddress(LowI8(), source,
					Operand(static_cast<std::int64_t>(lane.constant_value *
						(element.width / 8)), LowI64()), false);
				*result = derived.LoadStorage(address, element);
				return true;
			}
			const LowType type = derived.LowerType(record.type);
			if (!SameType(type, vector))
				throw std::logic_error("invalid vector intrinsic result type");
			const Operand storage(derived.EnsureGeneratedSlot(
				children[0], "vector", type), type);
			const Operand base = derived.AddressOfStorage(storage);
			for (std::size_t lane = 0; lane < intrinsic.lane_count; ++lane)
			{
				Instruction store(Instruction::STORE);
				store.type = element;
				store.first = derived.LowerValue(children[lane + 1], element);
				store.second = derived.IndexAddress(LowI8(), base,
					Operand(static_cast<std::int64_t>(lane *
						(element.width / 8)), LowI64()), false);
				derived.Emit(store);
			}
			*result = storage;
			return true;
		}
		if (record.compiler_intrinsic != COMPILER_INTRINSIC_NONE)
		{
			if (children.size() != 4)
				throw std::logic_error("invalid overflow intrinsic call");
			const LowType narrow = derived.LowerType(record.operand_type);
			if (!IsInteger(narrow) || narrow.width > 64)
				throw std::logic_error("invalid overflow intrinsic operand type");
			const LowType wide = narrow.width < 64 ?
				(narrow.is_signed ? LowI64() : LowU64()) :
				(narrow.is_signed ? LowI128() : LowU128());
			const Operand left = derived.Convert(
				derived.LowerValue(children[1], narrow), wide);
			const Operand right = derived.Convert(
				derived.LowerValue(children[2], narrow), wide);
			const LowOperation operation =
				record.compiler_intrinsic == COMPILER_INTRINSIC_ADD_OVERFLOW ?
					LOW_OP_ADD :
				record.compiler_intrinsic == COMPILER_INTRINSIC_SUB_OVERFLOW ?
					LOW_OP_SUB : LOW_OP_MUL;
			const Operand exact = EmitIntegerIntrinsicBinary(
				operation, left, right, wide);
			const Operand narrowed = derived.Convert(exact, narrow);
			Instruction store(Instruction::STORE);
			store.type = narrow;
			store.first = narrowed;
			store.second = derived.LowerValue(children[3], LowPtr());
			derived.Emit(store);
			const Operand round_trip = derived.Convert(narrowed, wide);
			const Operand overflowed = derived.Temp(LowI64());
			Instruction compare(Instruction::CMP);
			compare.dest = overflowed.id;
			compare.op = LOW_OP_NE;
			compare.type = wide;
			compare.first = exact;
			compare.second = round_trip;
			derived.Emit(compare);
			*result = derived.Convert(
				overflowed, derived.LowerType(record.type));
			return true;
		}
		if (TryLowerAtomicIntrinsicCall(record, children, result)) return true;
		if (callee.binding == kNoBinding ||
			callee.binding >= derived.program_.bindings.size()) return false;
		const BuiltinFunctionKind kind =
			derived.program_.bindings[callee.binding].builtin_function;
		if (kind == BUILTIN_FUNCTION_UNREACHABLE)
		{
			if (children.size() != 1)
				throw std::logic_error("invalid unreachable intrinsic call");
			derived.Emit(Instruction(Instruction::UNREACHABLE));
			*result = Operand(0, LowVoid());
			return true;
		}
		if (kind == BUILTIN_FUNCTION_HOSTED_MEMORY_INTRINSIC)
		{
			const hosted_builtin::MemoryIntrinsic& intrinsic =
				hosted_builtin::GetMemoryIntrinsic(
					derived.program_.bindings[callee.binding].
						hosted_memory_intrinsic);
			if (children.size() < intrinsic.minimum_arity + 1 ||
				children.size() > intrinsic.maximum_arity + 1)
				throw std::logic_error("invalid memory intrinsic call");
			if (intrinsic.lowering == hosted_builtin::MEMORY_LOWER_EXTERNAL)
				return false;
			Operand first;
			for (std::size_t i = 1; i < children.size(); ++i)
			{
				const Operand value = derived.LowerValue(children[i],
					i == 1 ? LowPtr() : LowType());
				if (i == 1) first = value;
			}
			if (intrinsic.lowering == hosted_builtin::MEMORY_LOWER_IDENTITY)
			{
				*result = derived.Convert(
					first, derived.LowerType(record.type));
				return true;
			}
			*result = Operand(0, LowVoid());
			return true;
		}
		if (kind != BUILTIN_FUNCTION_ALLOCA &&
			kind != BUILTIN_FUNCTION_VA_START &&
			kind != BUILTIN_FUNCTION_VA_END &&
			kind != BUILTIN_FUNCTION_VA_ARG) return false;
		if (children.size() != 2)
			throw std::logic_error("invalid compiler intrinsic call");
		if (kind == BUILTIN_FUNCTION_VA_END)
		{
			(void)derived.LowerValue(children[1], LowPtr());
			*result = Operand(0, LowVoid());
			return true;
		}
		Instruction instruction(kind == BUILTIN_FUNCTION_ALLOCA ?
			Instruction::STACK_ALLOC : kind == BUILTIN_FUNCTION_VA_START ?
				Instruction::VA_START : Instruction::VA_ARG);
		instruction.first = derived.LowerValue(children[1],
			kind == BUILTIN_FUNCTION_ALLOCA ? LowU64() : LowPtr());
		instruction.type = kind == BUILTIN_FUNCTION_ALLOCA ? LowPtr() :
			kind == BUILTIN_FUNCTION_VA_START ? LowVoid() :
				derived.LowerType(record.type);
		if (kind == BUILTIN_FUNCTION_VA_START)
		{
			derived.Emit(instruction);
			*result = Operand(0, LowVoid());
			return true;
		}
		*result = derived.Temp(instruction.type);
		instruction.dest = result->id;
		derived.Emit(instruction);
		return true;
	}

	void AttachCallArguments(Instruction* call,
		const CallArguments& arguments, const CallArgumentFlags& references)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (arguments.size() != references.size())
			throw std::logic_error("PA15 call argument fact mismatch");
		if (arguments.empty()) return;
		if (arguments.size() >= kNoLowId ||
			derived.output_.call_arguments.size() >
				kNoLowId - arguments.size() ||
			derived.output_.call_arguments.size() !=
				derived.output_.call_argument_references.size())
			throw std::runtime_error("too many PA15 call arguments");
		call->extra_first = static_cast<std::uint32_t>(
			derived.output_.call_arguments.size());
		call->extra_count = static_cast<std::uint32_t>(arguments.size());
		for (std::size_t i = 0; i < arguments.size(); ++i)
		{
			derived.output_.call_arguments.push_back(arguments[i]);
			derived.output_.call_argument_references.push_back(references[i]);
		}
	}

	Operand LowerBooleanConversion(std::uint32_t node, const LowType& target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand value = derived.LowerValue(node);
		Operand truth_value = value;
		const TypeId source_type = derived.program_.types.RemoveTopCv(
			derived.arena_.nodes[node].type);
		const TypeRecord& source = derived.program_.types.Get(source_type);
		if (source.kind == TYPE_MEMBER_POINTER &&
			derived.program_.types.IsFunction(source.child))
			truth_value = derived.Convert(value, LowU64(), false);
		const Operand boolean = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = boolean.id;
		compare.op = LOW_OP_NE;
		compare.type = truth_value.type;
		compare.first = truth_value;
		compare.second = IsFloating(truth_value.type) ?
			derived.FloatingOperand("0.0", truth_value.type) :
			Operand(0, truth_value.type);
		derived.Emit(compare);
		return derived.Convert(boolean, target, false);
	}

	bool IsClassValueType(TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		type = derived.program_.types.RemoveTopCv(type);
		const TypeRecord& record = derived.program_.types.Get(type);
		if (record.kind != TYPE_NAMED) return false;
			return IsClassNamedFlavor(
				derived.program_.entities[record.entity].flavor);
	}

	bool CanonicalizeOperatorLiteral(
		std::uint32_t node, const DumpNode& callee) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (derived.arena_.nodes[node].kind != DUMP_LITERAL ||
			callee.kind != DUMP_CALLEE || callee.binding == kNoBinding ||
			callee.binding >= derived.program_.bindings.size()) return false;
		const OperatorKind kind =
			derived.program_.bindings[callee.binding].operator_kind;
		return kind > OPERATOR_NONE && kind < OPERATOR_NEW;
	}

	bool CanonicalizeInitializerImmediate(std::uint32_t node,
		const LowType& target) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& source_node = derived.arena_.nodes[node];
		if (CanonicalizeNullPointerImmediate(node, target)) return true;
		if (source_node.integer_narrowing_conversion) return true;
		if (source_node.kind != DUMP_LITERAL ||
			source_node.enum_arithmetic_conversion) return false;
		const LowType source = derived.LowerExpressionType(source_node.type);
		return IsInteger(source) && IsInteger(target) &&
			source.is_signed == target.is_signed &&
			(source.is_signed || source.width >= target.width);
	}

	bool CanonicalizeNullPointerImmediate(std::uint32_t node,
		const LowType& target) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& cast = derived.arena_.nodes[node];
		if (target.kind != LOW_PTR || cast.kind != DUMP_CAST_EXPRESSION)
			return false;
		const NodeChildren children = derived.Children(node);
		return children.size() == 1 && IsNullPointerLiteralCast(
			derived.program_, derived.arena_.nodes[children[0]], cast.type);
	}

	bool CanonicalizeBinaryImmediate(std::uint32_t node,
		const LowType& target, bool selected, bool comparison,
		bool constant_expression, bool template_layout_constant) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& source_node = derived.arena_.nodes[node];
		if (source_node.template_parameter_constant) return false;
		if (!selected || source_node.kind != DUMP_LITERAL) return selected;
		const LowType source = derived.LowerExpressionType(source_node.type);
		if (!IsInteger(source) || !IsInteger(target)) return selected;
		const TypeId source_type =
			derived.program_.types.RemoveTopCv(source_node.type);
		const TypeRecord& source_record =
			derived.program_.types.Get(source_type);
		if (source_record.kind == TYPE_FUNDAMENTAL &&
			source_record.fundamental == FUND_BOOL) return true;
		if (source.is_signed != target.is_signed)
			return !comparison && constant_expression &&
				!template_layout_constant;
		return !comparison || source.is_signed || source.width >= target.width;
	}

	Operand LowerInitializerConvertedValue(std::uint32_t node,
		const LowType& target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		return derived.LowerConvertedValue(node, target,
			CanonicalizeInitializerImmediate(node, target));
	}

	Operand LowerClassArgumentStaging(std::uint32_t node, TypeId target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const LowType type = derived.LowerType(target);
		const Operand slot(derived.EnsureGeneratedSlot(
			node, derived.UsesIndirectClassParameter(target) ?
				"arg" : "argobj", type), type);
		if (derived.arena_.nodes[node].kind == DUMP_TEMPORARY_OBJECT)
		{
			const Operand temporary = derived.LowerStorage(node);
			return derived.UsesIndirectClassParameter(target) ? temporary : slot;
		}
		const Operand destination = derived.AddressOfStorage(slot);
		if (derived.arena_.nodes[node].kind == DUMP_INITIALIZER_LIST)
			derived.LowerInitializerListObject(node, destination);
		else if (derived.arena_.nodes[node].kind == DUMP_CLASS_VALUE_TRANSFER)
			derived.LowerClassValueTransfer(node, destination);
		else if (derived.arena_.nodes[node].kind == DUMP_CONSTRUCTOR_ACTION)
		{
			const EntityId entity = derived.ClassEntity(target);
			if (derived.arena_.nodes[node].value_initialization &&
				entity != kNoEntity && entity < derived.program_.entities.size() &&
				!derived.program_.entities[entity].empty_class)
				derived.EmitZeroInitialization(target, destination);
			derived.LowerConstructorAction(node, destination);
		}
		else (void)derived.AddressOfStorage(derived.LowerStorage(node));
		return derived.UsesIndirectClassParameter(target) ? destination : slot;
	}

	bool IsProjectedClassReference(std::uint32_t node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& argument = derived.arena_.nodes[node];
		return argument.kind == DUMP_CAST_EXPRESSION &&
			argument.base_projection_count != 0 &&
			derived.IsClassObjectType(argument.type);
	}

	Operand LowerProjectedClassPointer(std::uint32_t child,
		std::uint32_t projection_count, std::uint64_t projection_offset,
		bool has_projection_offset, EntityId target_entity = kNoEntity,
		bool inverse_projection = false)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Operand inherited;
		bool adjusted_boundary = false;
		if (target_entity != kNoEntity &&
			derived.CurrentVirtualBasePathAddressForExpression(
				child, target_entity, &inherited, &adjusted_boundary))
			return adjusted_boundary &&
				derived.BoundaryBindingForExpression(child) ==
					derived.current_this_binding_ ? inherited :
				derived.ProjectBaseSubobjectOffset(inherited, 0);
		const DumpNode& source = derived.arena_.nodes[child];
		const Operand address = derived.IsClassObjectType(source.type) ?
			derived.AddressOfStorage(derived.LowerStorage(child)) :
			derived.LowerValue(child, LowPtr());
		if (target_entity != kNoEntity &&
			derived.RuntimeVirtualBaseAddressForExpression(
				child, address, target_entity, &inherited)) return inherited;
		TypeId source_shape = derived.program_.types.RemoveTopCv(source.type);
		const bool pointer_source =
			derived.program_.types.Get(source_shape).kind == TYPE_POINTER;
		const bool nonnull_this = source.kind == DUMP_ID_EXPRESSION &&
			source.binding != kNoBinding &&
			source.binding == derived.current_this_binding_;
		const bool known_nonnull_address =
			source.kind == DUMP_UNARY_EXPRESSION &&
			source.OperationIs(OP_AMP);
		EntityId entity = derived.BaseEntityForType(source.type);
		bool adjusted = has_projection_offset && projection_offset != 0;
		for (std::uint32_t i = 0; !has_projection_offset &&
			i < projection_count && entity != kNoEntity; ++i)
		{
			adjusted = adjusted ||
				derived.program_.entities[entity].direct_base_offset != 0;
			entity = derived.program_.entities[entity].direct_base;
		}
		if (!pointer_source || !adjusted || nonnull_this || known_nonnull_address)
		{
			if (inverse_projection)
				return derived.ProjectBaseSubobjectAdjustment(address,
					-static_cast<std::int64_t>(projection_offset));
			return derived.ProjectBaseSubobjects(address, projection_count,
				source.type, projection_offset, has_projection_offset);
		}

		const Operand result(derived.EnsureGeneratedSlot(
			child, "basecast", LowPtr()), LowPtr());
		const BlockId null_block = derived.AddBlock(
			derived.NewLabel("basecast_null"));
		const BlockId adjust_block = derived.AddBlock(
			derived.NewLabel("basecast_adjust"));
		const BlockId end_block = derived.AddBlock(
			derived.NewLabel("basecast_end"));
		const Operand is_null = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = is_null.id;
		compare.op = LOW_OP_EQ;
		compare.type = LowPtr();
		compare.first = address;
		compare.second = Operand(0, LowPtr());
		derived.Emit(compare);
		derived.EmitBranch(is_null, null_block, adjust_block);
		derived.SelectBlock(null_block);
		Instruction store_null(Instruction::STORE);
		store_null.type = LowPtr();
		store_null.first = Operand(0, LowPtr());
		store_null.second = result;
		derived.Emit(store_null);
		derived.EmitJump(end_block);
		derived.SelectBlock(adjust_block);
		const Operand projected = inverse_projection ?
			derived.ProjectBaseSubobjectAdjustment(address,
				-static_cast<std::int64_t>(projection_offset)) :
			derived.ProjectBaseSubobjects(address, projection_count, source.type,
				projection_offset, has_projection_offset);
		Instruction store_adjusted(Instruction::STORE);
		store_adjusted.type = LowPtr();
		store_adjusted.first = projected;
		store_adjusted.second = result;
		derived.Emit(store_adjusted);
		derived.EmitJump(end_block);
		derived.SelectBlock(end_block);
		return derived.LoadStorage(result, LowPtr());
	}

	Operand LowerReferenceCallArgument(std::uint32_t node, TypeId target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& argument = derived.arena_.nodes[node];
		if (argument.category == VALUE_LVALUE ||
			argument.category == VALUE_XVALUE)
		{
			if (argument.kind == DUMP_TEMPORARY_OBJECT)
				return derived.LowerStorage(node);
			return derived.AddressOfStorage(derived.LowerStorage(node));
		}
		if (IsProjectedClassReference(node))
			return derived.LowerValue(node, LowPtr());
		if (argument.kind == DUMP_CALL_EXPRESSION &&
			derived.IsClassObjectType(argument.type) &&
			derived.UsesIndirectClassResult(argument.type, argument.binding))
		{
			const LowType type = derived.LowerStorageType(argument.type);
			const Operand slot(derived.EnsureGeneratedSlot(
				node, "arg", type), type);
			const Operand destination = derived.AddressOfStorage(slot);
			return derived.LowerCall(node, argument,
				derived.Children(node), destination);
		}
		const LowType type = derived.LowerExpressionType(target);
		const Operand slot(derived.EnsureGeneratedSlot(
			node, "refarg", type), type);
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = derived.LowerConvertedValue(node, type, false);
		store.second = slot;
		derived.Emit(store);
		return derived.AddressOfStorage(slot);
	}

	Operand LowerMemberValue(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BindingId binding = record.binding;
		if (binding != kNoBinding &&
			derived.program_.bindings[binding].kind == BIND_FUNCTION)
		{
			if (children.size() != 1 ||
				binding >= derived.function_symbols_.size() ||
				derived.function_symbols_[binding] == kNoLowId)
				throw std::runtime_error(
					"invalid static member function expression");
			const std::string spelling =
				derived.program_.names.Get(record.text);
			if (spelling.compare(0, 8, "OP_ARROW") == 0)
				(void)derived.LowerValue(children[0], LowPtr());
			else derived.LowerDiscardedValue(children[0]);
			return derived.DecayAddress(derived.AddressOfStorage(Operand(
				Operand::FUNCTION, derived.function_symbols_[binding], LowPtr())));
		}
		const LowType type = derived.LowerExpressionType(record.type);
		if (record.constant) return Operand(
			record.constant_value, record.constant_high, type);
		if (binding != kNoBinding &&
			derived.program_.bindings[binding].bit_field)
			return derived.LoadBitField(binding, derived.LowerStorage(node));
		return derived.LoadStorage(derived.LowerStorage(node), type,
			derived.TypeIsVolatile(record.type));
	}
};

}
}

#endif
