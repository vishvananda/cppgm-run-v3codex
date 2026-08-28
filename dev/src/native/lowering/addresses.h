#ifndef CPPGM_LOWIR_NATIVE_ADDRESS_LOWERING_H
#define CPPGM_LOWIR_NATIVE_ADDRESS_LOWERING_H

#include "lowir/model/program.h"
#include "native/mir/construction.h"
#include "native/allocation/registers.h"
#include "native/lowering/selection.h"

#include <unordered_map>
#include <vector>

namespace lowir_native
{

template <class Derived>
class AddressLowering
{
protected:
	bool address_is_immediately_loaded(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, lowir_model::ValueId destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (instruction_index + 1 >= block.instructions.size()) return false;
		const lowir_model::Instruction& next =
			block.instructions[instruction_index + 1];
		return next.kind == lowir_model::Instruction::IK_LOAD &&
			next.first.kind == lowir_model::Operand::OP_TEMP &&
			next.first.value == destination &&
			derived.facts_.uses[destination] == 1;
	}

	bool address_is_immediately_stored(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, lowir_model::ValueId destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (instruction_index + 1 >= block.instructions.size()) return false;
		const lowir_model::Instruction& next =
			block.instructions[instruction_index + 1];
		return next.kind == lowir_model::Instruction::IK_STORE &&
			next.second.kind == lowir_model::Operand::OP_TEMP &&
			next.second.value == destination &&
			derived.facts_.uses[destination] == 1;
	}

	bool address_is_immediately_indexed(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, lowir_model::ValueId destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (instruction_index + 1 >= block.instructions.size()) return false;
		const lowir_model::Instruction& next =
			block.instructions[instruction_index + 1];
		return next.kind == lowir_model::Instruction::IK_INDEX &&
			next.first.kind == lowir_model::Operand::OP_TEMP &&
			next.first.value == destination &&
			derived.facts_.uses[destination] == 1;
	}

	bool address_is_next_bulk_operand(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, lowir_model::ValueId destination) const
	{
		if (instruction_index + 1 >= block.instructions.size()) return false;
		const lowir_model::Instruction& next =
			block.instructions[instruction_index + 1];
		if (next.kind == lowir_model::Instruction::IK_ZEROINIT)
			return next.first.kind == lowir_model::Operand::OP_TEMP &&
				next.first.value == destination;
		if (next.kind != lowir_model::Instruction::IK_COPYOBJ) return false;
		return (next.first.kind == lowir_model::Operand::OP_TEMP &&
			next.first.value == destination) ||
			(next.second.kind == lowir_model::Operand::OP_TEMP &&
			 next.second.value == destination);
	}

	bool address_precedes_elided_copy(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, lowir_model::ValueId destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (instruction_index + 1 >= block.instructions.size()) return false;
		const lowir_model::Instruction& next =
			block.instructions[instruction_index + 1];
		if (next.kind != lowir_model::Instruction::IK_COPYOBJ ||
			next.second.kind != lowir_model::Operand::OP_TEMP ||
			next.second.value != destination) return false;
		long long source_offset = 0;
		long long destination_offset = 0;
		return derived.frame_provenance(next.first, source_offset) &&
			derived.frame_provenance(
				block.instructions[instruction_index].first, destination_offset) &&
			source_offset == destination_offset;
	}

	bool address_is_call_argument(lowir_model::ValueId destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		return derived.facts_.uses[destination] == 1 &&
			derived.facts_.has(destination,
				analysis::FunctionFacts::VF_ONLY_CALL_ARGUMENT);
	}

	bool address_is_next_atomic_expected(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, lowir_model::ValueId destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (derived.facts_.uses[destination] != 1) return false;
		for (std::size_t i = instruction_index + 1;
			i < block.instructions.size() && i <= instruction_index + 2; ++i)
		{
			const lowir_model::Instruction& next = block.instructions[i];
			if (next.kind == lowir_model::Instruction::IK_ATOMIC_COMPARE_EXCHANGE)
				return next.second.kind == lowir_model::Operand::OP_TEMP &&
					next.second.value == destination;
		}
		return false;
	}

	bool address_is_next_va_start(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, lowir_model::ValueId destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (instruction_index + 1 >= block.instructions.size() ||
			derived.facts_.uses[destination] != 1) return false;
		const lowir_model::Instruction& next =
			block.instructions[instruction_index + 1];
		return next.kind == lowir_model::Instruction::IK_VA_START &&
			next.first.kind == lowir_model::Operand::OP_TEMP &&
			next.first.value == destination;
	}

	bool address_is_va_list_operand(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, lowir_model::ValueId destination) const
	{
		for (std::size_t i = instruction_index + 1;
			i < block.instructions.size(); ++i)
		{
			const lowir_model::Instruction& instruction = block.instructions[i];
			if ((instruction.kind == lowir_model::Instruction::IK_VA_START ||
				 instruction.kind == lowir_model::Instruction::IK_VA_ARG) &&
				instruction.first.kind == lowir_model::Operand::OP_TEMP &&
				instruction.first.value == destination)
				return true;
		}
		return false;
	}

	bool address_is_object_result_destination(
		const lowir_model::LowirBlock& block, std::size_t instruction_index,
		lowir_model::ValueId destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (derived.facts_.uses[destination] != 1) return false;
		const std::size_t copy_position = derived.facts_.last_use[destination];
		if (copy_position == analysis::FunctionFacts::missing_position() ||
			copy_position < derived.position_) return false;
		const std::size_t copy_index = instruction_index +
			(copy_position - derived.position_);
		if (copy_index >= block.instructions.size()) return false;
		const lowir_model::Instruction& copy = block.instructions[copy_index];
		if (copy.kind != lowir_model::Instruction::IK_COPYOBJ ||
			copy.first.kind != lowir_model::Operand::OP_TEMP ||
			copy.second.kind != lowir_model::Operand::OP_TEMP ||
			copy.second.value != destination) return false;
		const std::size_t call_position =
			derived.facts_.definition[copy.first.value];
		if (call_position == analysis::FunctionFacts::missing_position() ||
			call_position < derived.position_) return false;
		const std::size_t call_index = instruction_index +
			(call_position - derived.position_);
		if (call_index >= copy_index) return false;
		const lowir_model::Instruction& call = block.instructions[call_index];
		return call.kind == lowir_model::Instruction::IK_CALL &&
			!call.call_returns_void && call.type.kind == lowir_model::LTK_OBJECT &&
			copy.first.value == call.dest &&
			call.dest.valid();
	}

	X64Register direct_slot_address_register(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, lowir_model::ValueId destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		for (std::size_t i = instruction_index + 1;
			i < block.instructions.size() && i <= instruction_index + 2; ++i)
		{
			const lowir_model::Instruction& comparison = block.instructions[i];
			if (comparison.kind != lowir_model::Instruction::IK_CMP) continue;
			if (!derived.comparison_feeds_branch(block, i, comparison)) break;
			if (comparison.first.kind == lowir_model::Operand::OP_TEMP &&
				comparison.first.value == destination) return XR_RAX;
			if (comparison.second.kind == lowir_model::Operand::OP_TEMP &&
				comparison.second.value == destination) return XR_RDX;
		}
		return XR_RSP;
	}

	void emit_address_value(const lowir_model::LowirBlock& block,
		std::size_t instruction_index,
		const lowir_model::Instruction& instruction,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		using namespace selection;
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.facts_.uses[instruction.dest] == 0) return;
		const lowir_model::LowType pointer_type =
			lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
		mir_model::MirOperand destination;
		if (instruction.first.kind == lowir_model::Operand::OP_SLOT)
		{
			if (derived.result_is_immediate_return(
					block, instruction_index, instruction.dest))
			{
				destination = reg_operand(XR_RAX);
				mir_model::MirInstruction lea =
					machine_instruction(mir_model::MirInstruction::MI_LEA);
				append_operand(lea, destination);
				append_operand(lea, derived.storage(instruction.first));
				out.push_back(lea);
			}
			else if (derived.address_is_call_argument(instruction.dest) ||
				derived.facts_.has(
					instruction.dest,
					analysis::FunctionFacts::VF_ONLY_STORAGE_ADDRESS) ||
				derived.facts_.has(
					instruction.dest,
					analysis::FunctionFacts::VF_ADDRESS_UNION_SAFE) ||
				derived.rematerialize_address(instruction.dest) ||
				derived.address_is_next_atomic_expected(
					block, instruction_index, instruction.dest) ||
				derived.address_is_next_va_start(
					block, instruction_index, instruction.dest) ||
				derived.address_is_va_list_operand(
					block, instruction_index, instruction.dest) ||
				derived.address_is_next_bulk_operand(
					block, instruction_index, instruction.dest) ||
				derived.address_is_immediately_loaded(
					block, instruction_index, instruction.dest) ||
				derived.address_is_immediately_stored(
					block, instruction_index, instruction.dest) ||
				derived.address_is_immediately_indexed(
					block, instruction_index, instruction.dest) ||
				derived.address_is_object_result_destination(
					block, instruction_index, instruction.dest) ||
				selection::address_only_feeds_dead_index(
					block, instruction_index, instruction.dest, derived.facts_))
			{
				derived.define(instruction.dest,
					lowir_model::builtin_lowir_type(lowir_model::LTK_PTR),
					derived.storage(instruction.first));
				derived.values_[instruction.dest].frame_address = true;
				derived.values_[instruction.dest].deferred_address_stable = true;
				derived.values_[instruction.dest].has_frame_provenance = true;
				derived.values_[instruction.dest].frame_provenance =
					derived.storage(instruction.first).offset;
				return;
			}
			else
			{
				const X64Register compare_register =
					derived.direct_slot_address_register(
						block, instruction_index, instruction.dest);
				mir_model::MirOperand target;
				if (compare_register != XR_RSP) {
					destination = reg_operand(compare_register);
					target = destination;
				} else if (derived.address_is_immediately_loaded(
						block, instruction_index, instruction.dest) ||
					 derived.address_is_immediately_stored(
						block, instruction_index, instruction.dest) ||
					 derived.address_precedes_elided_copy(
						block, instruction_index, instruction.dest) ||
					 derived.skipped_position_ == derived.position_ + 1) {
					destination = reg_operand(XR_RCX);
					target = destination;
				} else {
					X64Register result = XR_RSP;
					if (derived.try_allocate_result(instruction.dest, out, &result)) {
						destination = reg_operand(result);
						target = destination;
					} else {
						destination = derived.allocate_temp_home(
							instruction.dest, pointer_type);
						target = reg_operand(XR_RAX);
					}
				}
				mir_model::MirInstruction lea =
					machine_instruction(mir_model::MirInstruction::MI_LEA);
				append_operand(lea, target);
				append_operand(lea, derived.storage(instruction.first));
				out.push_back(lea);
				if (destination.kind == mir_model::MirOperand::OP_FRAME)
					append_store(out, destination, target, pointer_type);
			}
		}
		else if (instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
			derived.tls_wrappers_[instruction.first.symbol].valid())
		{
			const lowir_model::SymbolId wrapper =
				derived.tls_wrappers_[instruction.first.symbol];
			mir_model::MirOperand target;
			X64Register result = XR_RSP;
			if (derived.result_is_immediate_return(
					block, instruction_index, instruction.dest))
			{
				destination = reg_operand(XR_RAX);
				target = destination;
			}
			else if (derived.constrained_wide_pressure() ||
				!derived.try_allocate_result(instruction.dest, out, &result))
			{
				destination = derived.allocate_temp_home(instruction.dest,
					pointer_type);
				target = reg_operand(XR_RAX);
			}
			else
			{
				destination = reg_operand(result);
				target = destination;
			}
			mir_model::MirInstruction address =
				machine_instruction(mir_model::MirInstruction::MI_TLS_ADDR);
			append_operand(address, target);
			append_operand(address,
				symbol_operand(mir_model::MirOperand::OP_SYMBOL, wrapper));
			address.tls_storage_symbol = instruction.first.symbol;
			out.push_back(address);
			if (destination.kind == mir_model::MirOperand::OP_FRAME)
				append_store(out, destination, target, machine_type(lowir_model::LTK_PTR));
		}
		else if (instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
			derived.optimization_level_ >= 1 &&
			derived.facts_.has(instruction.dest,
				analysis::FunctionFacts::VF_ADDRESS_REMATERIALIZE_SAFE))
		{
			if (derived.stats_)
				++derived.stats_->planned_rematerialized_global_addresses;
			derived.define(instruction.dest, pointer_type,
				derived.global_operand(mir_model::MirOperand::OP_SYMBOL,
					instruction.first));
			if (derived.pointer_globals_[instruction.first.symbol])
				derived.values_[instruction.dest].pointer_global_cell =
					derived.global_operand(mir_model::MirOperand::OP_GLOBAL,
						instruction.first);
			return;
		}
		else if (instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
			derived.facts_.has(instruction.dest,
				analysis::FunctionFacts::VF_ONLY_STORAGE_ADDRESS))
		{
			// Every use is a load or store address, so the symbol itself is
			// the complete RIP-relative operand; no register is needed.
			derived.define(instruction.dest, pointer_type,
				derived.global_operand(mir_model::MirOperand::OP_SYMBOL,
					instruction.first));
			return;
		}
		else if (instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
			derived.address_is_call_argument(instruction.dest))
		{
			// A symbol address is already a complete target operand.  Retain it
			// until call setup selects the ABI register instead of assigning an
			// unrelated persistent register and immediately copying from it.
			destination = derived.global_operand(
				mir_model::MirOperand::OP_SYMBOL, instruction.first);
		}
		else if (instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
			derived.result_is_immediate_return(
				block, instruction_index, instruction.dest))
		{
			destination = reg_operand(XR_RAX);
			append_move(out, destination, derived.resolve(instruction.first));
		}
		else
		{
			X64Register result = XR_RSP;
			if (!derived.constrained_wide_pressure() &&
				derived.try_allocate_result(instruction.dest, out, &result)) {
				destination = reg_operand(result);
				append_move(out, destination, derived.resolve(instruction.first));
			} else {
				destination = derived.allocate_temp_home(
					instruction.dest, pointer_type);
				append_move(out, reg_operand(XR_RAX),
					derived.resolve(instruction.first));
				append_store(out, destination, reg_operand(XR_RAX), pointer_type);
			}
		}
		derived.define(instruction.dest, pointer_type, destination);
		if (instruction.first.kind == lowir_model::Operand::OP_SLOT)
		{
			derived.values_[instruction.dest].has_frame_provenance = true;
			derived.values_[instruction.dest].frame_provenance =
				derived.storage(instruction.first).offset;
		}
		if (instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
			derived.pointer_globals_[instruction.first.symbol])
			derived.values_[instruction.dest].pointer_global_cell =
				derived.global_operand(mir_model::MirOperand::OP_GLOBAL,
					instruction.first);
	}
};

}

#endif
