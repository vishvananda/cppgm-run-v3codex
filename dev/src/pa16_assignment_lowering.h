#ifndef CPPGM_PA16_ASSIGNMENT_LOWERING_H
#define CPPGM_PA16_ASSIGNMENT_LOWERING_H

#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"
#include "pa12_semantic_model.h"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa16_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

template <class Derived>
class AssignmentLowering
{
public:
	BindingId BitFieldBinding(std::uint32_t node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[node];
		if (record.kind != DUMP_MEMBER_EXPRESSION ||
			record.binding == kNoBinding ||
			record.binding >= derived.program_.bindings.size() ||
			!derived.program_.bindings[record.binding].bit_field)
			return kNoBinding;
		return record.binding;
	}

	std::uint64_t BitFieldMask(const BindingRecord& field) const
	{
		if (field.bit_width == 0 || field.bit_width > 64)
			throw std::logic_error("invalid canonical bit-field width");
		return field.bit_width == 64 ? ~std::uint64_t(0) :
			(std::uint64_t(1) << field.bit_width) - 1;
	}

	LowType BitFieldAccessType(const BindingRecord& field) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const LowType declared = derived.LowerExpressionType(field.type);
		if (declared.width < 32 ||
			(!declared.is_signed && declared.width == 32 &&
			 field.bit_width < declared.width))
			return LowI32();
		return declared;
	}

	LowType BitFieldMemoryType(const BindingRecord& field) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const LowType storage = derived.LowerExpressionType(field.type);
		const LowType access = BitFieldAccessType(field);
		return storage.width == access.width ? access : storage;
	}

	Operand FinishBitFieldValue(BindingId binding, Operand value,
		const LowType& type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BindingRecord& field = derived.program_.bindings[binding];
		Operand normalized = value;
		const LowType declared = derived.LowerExpressionType(field.type);
		if (declared.is_signed && field.bit_width < type.width)
		{
			const std::size_t shift_count = type.width - field.bit_width;
			const Operand shifted = derived.Temp(type);
			Instruction shift_left(Instruction::BINARY);
			shift_left.dest = shifted.id;
			shift_left.op = LOW_OP_SHL;
			shift_left.type = type;
			shift_left.first = normalized;
			shift_left.second = Operand(shift_count, type);
			derived.Emit(shift_left);
			normalized = derived.Temp(type);
			Instruction shift_right(Instruction::BINARY);
			shift_right.dest = normalized.id;
			shift_right.op = LOW_OP_SHR;
			shift_right.type = type;
			shift_right.first = shifted;
			shift_right.second = Operand(shift_count, type);
			derived.Emit(shift_right);
		}
		normalized.type = declared;
		return normalized;
	}

	Operand NormalizeBitFieldValue(BindingId binding, Operand value,
		const LowType& type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BindingRecord& field = derived.program_.bindings[binding];
		const Operand masked = derived.Temp(type);
		Instruction mask(Instruction::BINARY);
		mask.dest = masked.id;
		mask.op = LOW_OP_AND;
		mask.type = type;
		mask.first = value;
		mask.second = Operand(
			static_cast<std::int64_t>(BitFieldMask(field)), type);
		derived.Emit(mask);
		return FinishBitFieldValue(binding, masked, type);
	}

	Operand LoadBitField(BindingId binding, const Operand& storage)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BindingRecord& field = derived.program_.bindings[binding];
		const LowType type = BitFieldMemoryType(field);
		Operand value = derived.LoadStorage(storage, type);
		if (field.bit_offset != 0)
		{
			const Operand shifted = derived.Temp(type);
			Instruction shift(Instruction::BINARY);
			shift.dest = shifted.id;
			shift.op = type.is_signed ? LOW_OP_SHR : LOW_OP_USHR;
			shift.type = type;
			shift.first = value;
			shift.second = Operand(field.bit_offset, type);
			derived.Emit(shift);
			value = shifted;
		}
		return NormalizeBitFieldValue(binding, value, type);
	}

	Operand PrepareBitFieldValue(BindingId binding, Operand value,
		const LowType& type, bool mask_first = true,
		Operand* normalized_value = 0)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BindingRecord& field = derived.program_.bindings[binding];
		value = derived.Convert(value, type, false);
		const Operand masked = derived.Temp(type);
		Instruction mask(Instruction::BINARY);
		mask.dest = masked.id;
		mask.op = LOW_OP_AND;
		mask.type = type;
		mask.first = mask_first ? Operand(
			static_cast<std::int64_t>(BitFieldMask(field)), type) : value;
		mask.second = mask_first ? value : Operand(
			static_cast<std::int64_t>(BitFieldMask(field)), type);
		derived.Emit(mask);
		if (normalized_value)
			*normalized_value = FinishBitFieldValue(binding, masked, type);
		Operand positioned = masked;
		if (field.bit_offset != 0)
		{
			positioned = derived.Temp(type);
			Instruction shift(Instruction::BINARY);
			shift.dest = positioned.id;
			shift.op = LOW_OP_SHL;
			shift.type = type;
			shift.first = masked;
			shift.second = Operand(field.bit_offset, type);
			derived.Emit(shift);
		}
		return positioned;
	}

	Operand ClearBitFieldStorage(BindingId binding, const Operand& storage,
		const LowType& type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BindingRecord& field = derived.program_.bindings[binding];
		const Operand old = derived.LoadStorage(storage, type);
		const std::uint64_t unit_mask = field.bit_storage_bits == 64 ?
			~std::uint64_t(0) :
			(std::uint64_t(1) << field.bit_storage_bits) - 1;
		const std::uint64_t clear_mask = unit_mask &
			~(BitFieldMask(field) << field.bit_offset);
		const Operand cleared = derived.Temp(type);
		Instruction clear(Instruction::BINARY);
		clear.dest = cleared.id;
		clear.op = LOW_OP_AND;
		clear.type = type;
		clear.first = old;
		clear.second = Operand(static_cast<std::int64_t>(clear_mask), type);
		derived.Emit(clear);
		return cleared;
	}

	Operand CombineBitFieldValue(const Operand& cleared,
		const Operand& positioned, const LowType& type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand stored = derived.Temp(type);
		Instruction combine(Instruction::BINARY);
		combine.dest = stored.id;
		combine.op = LOW_OP_OR;
		combine.type = type;
		combine.first = cleared;
		combine.second = positioned;
		derived.Emit(combine);
		return stored;
	}

	Operand MergeBitFieldStore(BindingId binding, const Operand& storage,
		const Operand& positioned, const LowType& type, bool preserve)
	{
		if (!preserve) return positioned;
		const Operand cleared = ClearBitFieldStorage(binding, storage, type);
		return CombineBitFieldValue(cleared, positioned, type);
	}

	void EmitBitFieldStore(const LowType& type, const Operand& value,
		const Operand& storage)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = value;
		store.second = storage;
		derived.Emit(store);
	}

	Operand CommitBitFieldStore(BindingId binding, const Operand& storage,
		const Operand& positioned, const LowType& type, bool preserve)
	{
		const Operand stored = MergeBitFieldStore(
			binding, storage, positioned, type, preserve);
		EmitBitFieldStore(type, stored, storage);
		return positioned;
	}

	Operand StoreBitField(BindingId binding, const Operand& storage,
		Operand value, bool preserve)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const LowType type = BitFieldMemoryType(
			derived.program_.bindings[binding]);
		Operand normalized;
		const Operand positioned = PrepareBitFieldValue(
			binding, value, type, false, &normalized);
		CommitBitFieldStore(binding, storage, positioned, type, preserve);
		return normalized;
	}

	bool PreserveInitializedBitField(BindingId binding)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BindingRecord& member = derived.program_.bindings[binding];
		const bool preserve = derived.initialized_bit_field_unit_valid_ &&
			derived.initialized_bit_field_owner_ == member.member_owner &&
			derived.initialized_bit_field_offset_ == member.member_offset;
		derived.initialized_bit_field_unit_valid_ = true;
		derived.initialized_bit_field_owner_ = member.member_owner;
		derived.initialized_bit_field_offset_ = member.member_offset;
		return preserve;
	}

	void InitializeBitField(BindingId binding, const Operand& value,
		const Operand& destination, const LowType& type)
	{
		const bool preserve = PreserveInitializedBitField(binding);
		const Operand positioned = PrepareBitFieldValue(binding, value, type);
		const Operand stored = preserve ? CombineBitFieldValue(
			ClearBitFieldStorage(binding, destination, type),
			positioned, type) : positioned;
		EmitBitFieldStore(type, stored, destination);
	}

	Operand LowerAssignment(const DumpNode& record,
		const NodeChildren& children)
	{
		return LowerAssignmentCore(record, children, false);
	}

	Operand LowerAssignmentCore(const DumpNode& record,
		const NodeChildren& children, bool return_storage)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 2)
			throw std::runtime_error("invalid semantic assignment");
		const std::string op = StripOperationPrefix(
			derived.program_.names.Get(record.text));
		const LowType type = derived.LowerExpressionType(record.type);
		const BindingId bit_field = BitFieldBinding(children[0]);
		if (bit_field != kNoBinding)
		{
			const Operand storage = derived.LowerStorage(children[0]);
			Operand value;
			if (op == "=")
				value = derived.LowerConvertedValue(children[1], type, false);
			else
			{
				Operand left = LoadBitField(bit_field, storage);
				if (record.operand_type == kNoType)
					throw std::runtime_error(
						"bit-field compound assignment has no operand type");
				const LowType operation_type = derived.LowerType(record.operand_type);
				left = derived.Convert(left, operation_type, false);
				const Operand right = derived.LowerConvertedValue(
					children[1], operation_type, false);
				value = derived.Temp(operation_type);
				Instruction binary(Instruction::BINARY);
				binary.dest = value.id;
				binary.type = operation_type;
				binary.first = left;
				binary.second = right;
				binary.op = op == "+=" ? LOW_OP_ADD : op == "-=" ? LOW_OP_SUB :
					op == "*=" ? LOW_OP_MUL : op == "/=" ?
						(operation_type.is_signed || IsFloating(operation_type) ?
							LOW_OP_DIV : LOW_OP_UDIV) :
					op == "%=" ? (operation_type.is_signed ? LOW_OP_MOD : LOW_OP_UMOD) :
					op == "&=" ? LOW_OP_AND : op == "|=" ? LOW_OP_OR :
					op == "^=" ? LOW_OP_XOR : op == "<<=" ? LOW_OP_SHL : op == ">>=" ?
						(operation_type.is_signed ? LOW_OP_SHR : LOW_OP_USHR) : LOW_OP_NONE;
				if (binary.op == LOW_OP_NONE)
					throw std::runtime_error(
						"unsupported bit-field compound assignment");
				derived.Emit(binary);
				value = derived.Convert(value, type, false);
			}
			const Operand stored = StoreBitField(
				bit_field, storage, value, true);
			return return_storage ? storage : stored;
		}

		Operand storage;
		Operand value;
		if (op == "=")
		{
			bool union_member = false;
			const DumpNode& destination = derived.arena_.nodes[children[0]];
			if (destination.kind == DUMP_MEMBER_EXPRESSION &&
				destination.binding != kNoBinding)
			{
				const EntityId owner = derived.program_.bindings[
					destination.binding].member_owner;
				union_member = owner != kNoEntity &&
					derived.program_.entities[owner].flavor == NAMED_UNION;
			}
			value = derived.LowerConvertedValue(children[1], type,
				union_member ||
				record.target_typed_scalar_immediate ||
				derived.CanonicalizeImmediateConversion(children[1]));
			storage = derived.LowerStorage(children[0]);
		}
		else if ((op == "+=" || op == "-=") &&
			derived.IsPointerLikeType(derived.arena_.nodes[children[0]].type))
		{
			storage = derived.LowerStorage(children[0]);
			const Operand left = derived.LoadStorage(storage, LowPtr());
			value = derived.ApplyPointerOffset(left,
				derived.LowerValue(children[1]),
				derived.PointeeType(derived.arena_.nodes[children[0]].type),
				op == "-=");
		}
		else
		{
			storage = derived.LowerStorage(children[0]);
			Operand left = derived.Temp(type);
			Instruction load(Instruction::LOAD);
			load.dest = left.id;
			load.type = type;
			load.first = storage;
			derived.Emit(load);
			if (record.operand_type == kNoType)
				throw std::runtime_error(
					"compound assignment is missing its PA12 operand type");
			const LowType operation_type = derived.LowerType(record.operand_type);
			left = derived.Convert(left, operation_type, false);
			const Operand right = derived.LowerConvertedValue(
				children[1], operation_type, false);
			value = derived.Temp(operation_type);
			Instruction binary(Instruction::BINARY);
			binary.dest = value.id;
			binary.type = operation_type;
			binary.first = left;
			binary.second = right;
			binary.op = op == "+=" ? LOW_OP_ADD : op == "-=" ? LOW_OP_SUB :
				op == "*=" ? LOW_OP_MUL : op == "/=" ?
					(operation_type.is_signed || IsFloating(operation_type) ?
						LOW_OP_DIV : LOW_OP_UDIV) :
				op == "%=" ? (operation_type.is_signed ? LOW_OP_MOD : LOW_OP_UMOD) :
				op == "&=" ? LOW_OP_AND : op == "|=" ? LOW_OP_OR :
				op == "^=" ? LOW_OP_XOR : op == "<<=" ? LOW_OP_SHL : op == ">>=" ?
					(operation_type.is_signed ? LOW_OP_SHR : LOW_OP_USHR) : LOW_OP_NONE;
			if (binary.op == LOW_OP_NONE)
				throw std::runtime_error("unsupported PA15 compound assignment");
			derived.Emit(binary);
			value = derived.Convert(value, type, false);
		}
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = value;
		store.second = storage;
		derived.Emit(store);
		return return_storage ? storage : value;
	}
};

}
}

#endif
