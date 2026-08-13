#ifndef CPPGM_LOWIR_NATIVE_ADDRESS_LOWERING_H
#define CPPGM_LOWIR_NATIVE_ADDRESS_LOWERING_H

#include "lowir_model.h"
#include "lowir_native_mir.h"
#include "lowir_native_registers.h"
#include "lowir_native_selection.h"

#include <unordered_map>
#include <vector>

namespace lowir_native
{

template <class Derived>
class AddressLowering
{
protected:
	void emit_address_value(const lowir_model::LowirBlock& block,
		std::size_t instruction_index,
		const lowir_model::Instruction& instruction,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		using namespace selection;
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.facts_.uses.count(instruction.dest)) return;
		mir_model::MirOperand destination;
		if (instruction.first.kind == lowir_model::Operand::OP_SLOT)
		{
			if (derived.address_is_call_argument(instruction.dest) ||
				derived.address_is_next_atomic_expected(
					block, instruction_index, instruction.dest) ||
				derived.address_is_next_va_start(
					block, instruction_index, instruction.dest) ||
				derived.address_is_next_bulk_operand(
					block, instruction_index, instruction.dest) ||
				derived.address_is_immediately_stored(
					block, instruction_index, instruction.dest) ||
				derived.address_is_object_result_destination(
					block, instruction_index, instruction.dest))
			{
				derived.define(instruction.dest,
					lowir_model::builtin_lowir_type(lowir_model::LTK_PTR),
					derived.storage(instruction.first));
				derived.values_[instruction.dest].frame_address = true;
				derived.values_[instruction.dest].has_frame_provenance = true;
				derived.values_[instruction.dest].frame_provenance =
					derived.storage(instruction.first).offset;
				return;
			}
			const X64Register compare_register =
				derived.direct_slot_address_register(
					block, instruction_index, instruction.dest);
			if (compare_register != XR_RSP)
				destination = reg_operand(compare_register);
			else destination =
				(derived.address_is_immediately_loaded(
					block, instruction_index, instruction.dest) ||
				 derived.address_is_immediately_stored(
					block, instruction_index, instruction.dest) ||
				 derived.address_precedes_elided_copy(
					block, instruction_index, instruction.dest) ||
				 derived.skipped_position_ == derived.position_ + 1) ?
					reg_operand(XR_RCX) : reg_operand(
						derived.allocate_result(instruction.dest, out));
			mir_model::MirInstruction lea =
				machine_instruction(mir_model::MirInstruction::MI_LEA);
			append_operand(lea, destination);
			append_operand(lea, derived.storage(instruction.first));
			out.push_back(lea);
		}
		else if (instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
			derived.tls_wrappers_.count(instruction.first.text))
		{
			const std::unordered_map<std::string, std::string>::const_iterator
				wrapper = derived.tls_wrappers_.find(instruction.first.text);
			mir_model::MirOperand target;
			if (derived.constrained_wide_pressure())
			{
				destination = derived.allocate_temp_home(instruction.dest,
					lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
				target = reg_operand(XR_RAX);
			}
			else
			{
				destination = reg_operand(
					derived.allocate_result(instruction.dest, out));
				target = destination;
			}
			mir_model::MirInstruction address =
				machine_instruction(mir_model::MirInstruction::MI_TLS_ADDR);
			append_operand(address, target);
			append_operand(address,
				named_operand(mir_model::MirOperand::OP_SYMBOL, wrapper->second));
			address.tls_storage_symbol = instruction.first.text;
			out.push_back(address);
			if (destination.kind == mir_model::MirOperand::OP_FRAME)
				append_store(out, destination, target, "ptr");
		}
		else if (derived.constrained_wide_pressure())
		{
			destination = derived.allocate_temp_home(instruction.dest,
				lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
			append_move(out, reg_operand(XR_RAX),
				derived.resolve(instruction.first));
			append_store(
				out, destination, reg_operand(XR_RAX), "ptr");
		}
		else
		{
			destination = reg_operand(
				derived.allocate_result(instruction.dest, out));
			append_move(
				out, destination, derived.resolve(instruction.first));
		}
		derived.define(instruction.dest,
			lowir_model::builtin_lowir_type(lowir_model::LTK_PTR), destination);
		if (instruction.first.kind == lowir_model::Operand::OP_SLOT)
		{
			derived.values_[instruction.dest].has_frame_provenance = true;
			derived.values_[instruction.dest].frame_provenance =
				derived.storage(instruction.first).offset;
		}
		if (instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
			derived.pointer_globals_.count(instruction.first.text))
			derived.values_[instruction.dest].pointer_global_cell =
				derived.global_operand(mir_model::MirOperand::OP_GLOBAL,
					instruction.first);
	}
};

}

#endif
