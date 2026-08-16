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
	bool address_is_immediately_loaded(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, const std::string& destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (instruction_index + 1 >= block.instructions.size()) return false;
		const lowir_model::Instruction& next =
			block.instructions[instruction_index + 1];
		return next.kind == lowir_model::Instruction::IK_LOAD &&
			next.first.text == destination &&
			derived.facts_.uses.find(destination) != derived.facts_.uses.end() &&
			derived.facts_.uses.find(destination)->second == 1;
	}

	bool address_is_immediately_stored(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, const std::string& destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (instruction_index + 1 >= block.instructions.size()) return false;
		const lowir_model::Instruction& next =
			block.instructions[instruction_index + 1];
		return next.kind == lowir_model::Instruction::IK_STORE &&
			next.second.text == destination &&
			derived.facts_.uses.find(destination) != derived.facts_.uses.end() &&
			derived.facts_.uses.find(destination)->second == 1;
	}

	bool address_is_next_bulk_operand(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, const std::string& destination) const
	{
		if (instruction_index + 1 >= block.instructions.size()) return false;
		const lowir_model::Instruction& next =
			block.instructions[instruction_index + 1];
		if (next.kind == lowir_model::Instruction::IK_ZEROINIT)
			return next.first.kind == lowir_model::Operand::OP_TEMP &&
				next.first.text == destination;
		if (next.kind != lowir_model::Instruction::IK_COPYOBJ) return false;
		return (next.first.kind == lowir_model::Operand::OP_TEMP &&
			next.first.text == destination) ||
			(next.second.kind == lowir_model::Operand::OP_TEMP &&
			 next.second.text == destination);
	}

	bool address_precedes_elided_copy(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, const std::string& destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (instruction_index + 1 >= block.instructions.size()) return false;
		const lowir_model::Instruction& next =
			block.instructions[instruction_index + 1];
		if (next.kind != lowir_model::Instruction::IK_COPYOBJ ||
			next.second.kind != lowir_model::Operand::OP_TEMP ||
			next.second.text != destination) return false;
		long long source_offset = 0;
		long long destination_offset = 0;
		return derived.frame_provenance(next.first, source_offset) &&
			derived.frame_provenance(
				block.instructions[instruction_index].first, destination_offset) &&
			source_offset == destination_offset;
	}

	bool address_is_call_argument(const std::string& destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		return derived.facts_.uses.find(destination) != derived.facts_.uses.end() &&
			derived.facts_.uses.find(destination)->second == 1 &&
			derived.facts_.only_call_arguments.count(destination);
	}

	bool address_is_next_atomic_expected(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, const std::string& destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (derived.facts_.uses.find(destination) == derived.facts_.uses.end() ||
			derived.facts_.uses.find(destination)->second != 1) return false;
		for (std::size_t i = instruction_index + 1;
			i < block.instructions.size() && i <= instruction_index + 2; ++i)
		{
			const lowir_model::Instruction& next = block.instructions[i];
			if (next.kind == lowir_model::Instruction::IK_ATOMIC_COMPARE_EXCHANGE)
				return next.second.kind == lowir_model::Operand::OP_TEMP &&
					next.second.text == destination;
		}
		return false;
	}

	bool address_is_next_va_start(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, const std::string& destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (instruction_index + 1 >= block.instructions.size() ||
			derived.facts_.uses.find(destination) == derived.facts_.uses.end() ||
			derived.facts_.uses.find(destination)->second != 1) return false;
		const lowir_model::Instruction& next =
			block.instructions[instruction_index + 1];
		return next.kind == lowir_model::Instruction::IK_VA_START &&
			next.first.kind == lowir_model::Operand::OP_TEMP &&
			next.first.text == destination;
	}

	bool address_is_va_list_operand(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, const std::string& destination) const
	{
		for (std::size_t i = instruction_index + 1;
			i < block.instructions.size(); ++i)
		{
			const lowir_model::Instruction& instruction = block.instructions[i];
			if ((instruction.kind == lowir_model::Instruction::IK_VA_START ||
				 instruction.kind == lowir_model::Instruction::IK_VA_ARG) &&
				instruction.first.kind == lowir_model::Operand::OP_TEMP &&
				instruction.first.text == destination)
				return true;
		}
		return false;
	}

	bool address_is_object_result_destination(
		const lowir_model::LowirBlock& block, std::size_t instruction_index,
		const std::string& destination) const
	{
		if (instruction_index + 2 >= block.instructions.size()) return false;
		const lowir_model::Instruction& call =
			block.instructions[instruction_index + 1];
		const lowir_model::Instruction& copy =
			block.instructions[instruction_index + 2];
		return call.kind == lowir_model::Instruction::IK_CALL &&
			!call.call_returns_void && call.type.kind == lowir_model::LTK_OBJECT &&
			copy.kind == lowir_model::Instruction::IK_COPYOBJ &&
			copy.first.kind == lowir_model::Operand::OP_TEMP &&
			copy.first.text == call.dest &&
			copy.second.kind == lowir_model::Operand::OP_TEMP &&
			copy.second.text == destination;
	}

	X64Register direct_slot_address_register(const lowir_model::LowirBlock& block,
		std::size_t instruction_index, const std::string& destination) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		for (std::size_t i = instruction_index + 1;
			i < block.instructions.size() && i <= instruction_index + 2; ++i)
		{
			const lowir_model::Instruction& comparison = block.instructions[i];
			if (comparison.kind != lowir_model::Instruction::IK_CMP) continue;
			if (!derived.comparison_feeds_branch(block, i, comparison)) break;
			if (comparison.first.text == destination) return XR_RAX;
			if (comparison.second.text == destination) return XR_RDX;
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
		if (!derived.facts_.uses.count(instruction.dest)) return;
		const lowir_model::LowType pointer_type =
			lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
		mir_model::MirOperand destination;
		if (instruction.first.kind == lowir_model::Operand::OP_SLOT)
		{
			if (derived.address_is_call_argument(instruction.dest) ||
				derived.address_is_next_atomic_expected(
					block, instruction_index, instruction.dest) ||
				derived.address_is_next_va_start(
					block, instruction_index, instruction.dest) ||
				derived.address_is_va_list_operand(
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
				append_store(out, destination, target, pointer_type.text);
		}
		else if (instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
			derived.tls_wrappers_.count(instruction.first.text))
		{
			const std::unordered_map<std::string, std::string>::const_iterator
				wrapper = derived.tls_wrappers_.find(instruction.first.text);
			mir_model::MirOperand target;
			X64Register result = XR_RSP;
			if (derived.constrained_wide_pressure() ||
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
				named_operand(mir_model::MirOperand::OP_SYMBOL, wrapper->second));
			address.tls_storage_symbol = instruction.first.text;
			out.push_back(address);
			if (destination.kind == mir_model::MirOperand::OP_FRAME)
				append_store(out, destination, target, "ptr");
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
				append_store(out, destination, reg_operand(XR_RAX), pointer_type.text);
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
			derived.pointer_globals_.count(instruction.first.text))
			derived.values_[instruction.dest].pointer_global_cell =
				derived.global_operand(mir_model::MirOperand::OP_GLOBAL,
					instruction.first);
	}
};

}

#endif
