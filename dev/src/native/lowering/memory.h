#pragma once

#include "native/lowering/selection.h"
#include "native/lowering/integers.h"
#include "native/lowering/values.h"
#include "native/lowering/wide.h"

#include <cstddef>
#include <string>
#include <vector>

namespace lowir_native
{
namespace memory_detail
{

template <class Derived>
class MemoryLowering
{
protected:
	bool load_has_direct_memory_rhs(
		const lowir_model::Instruction& instruction,
		const lowir_model::LowirBlock& block,
		std::size_t instruction_index) const
	{
		const Derived& lowerer = static_cast<const Derived&>(*this);
		if (instruction.volatile_access ||
			lowerer.facts_.uses[instruction.dest] != 1 ||
			lowerer.facts_.has(instruction.dest,
				analysis::FunctionFacts::VF_EDGE_LIVE) ||
			instruction_index + 1 >= block.instructions.size())
			return false;
		const lowir_model::Instruction& consumer =
			block.instructions[instruction_index + 1];
		if (consumer.second.kind != lowir_model::Operand::OP_TEMP ||
			consumer.second.value != instruction.dest ||
			!lowir_model::same_lowir_type(consumer.type, instruction.type))
			return false;
		if (instruction.first.kind == lowir_model::Operand::OP_TEMP &&
			lowerer.value_known_[instruction.first.value] &&
			lowerer.values_[instruction.first.value].deferred_address &&
			(lowerer.facts_.uses[instruction.first.value] != 1 ||
			 lowerer.facts_.has(instruction.first.value,
				 analysis::FunctionFacts::VF_EDGE_LIVE)))
			return false;
		// A deferred branch comparison replays at the branch, not adjacent
		// to this load, so its cached carrier cannot be kept live here.
		if (consumer.kind == lowir_model::Instruction::IK_CMP)
			return selection::is_integer_or_pointer(instruction.type) &&
				!(consumer.dest.valid() &&
				  lowerer.facts_.deferred_branch_comparisons[consumer.dest]);
		return consumer.kind == lowir_model::Instruction::IK_BINARY &&
			integer_detail::memory_rhs_operation(consumer.op, consumer.type);
	}

	void emit_load_instruction(const lowir_model::Instruction& instruction,
		const lowir_model::LowirBlock& block, std::size_t instruction_index,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		Derived& lowerer = static_cast<Derived&>(*this);
		if (instruction.first.kind == lowir_model::Operand::OP_SLOT &&
			(lowerer.storage_facts_.promoted_parameter_slots[
				instruction.first.slot].valid() ||
			 lowerer.storage_facts_.forwarded_parameter_slots[
				instruction.first.slot].valid()))
		{
			const lowir_model::ValueId promoted =
				lowerer.storage_facts_.promoted_parameter_slots[
					instruction.first.slot];
			const lowir_model::ValueId parameter = promoted.valid() ? promoted :
				lowerer.storage_facts_.forwarded_parameter_slots[
					instruction.first.slot];
			ValueFact value = lowerer.values_[parameter];
			value.type = instruction.type;
			value.parameter = false;
			value.forwarded_parameter = parameter;
			if (!lowerer.facts_.calls.empty() &&
				lowerer.incoming_parameter_register_known_[parameter])
			{
				const std::size_t load_position =
					lowerer.facts_.definition[instruction.dest];
				if (load_position < lowerer.facts_.calls.front() &&
					lowerer.facts_.has(instruction.dest,
						analysis::FunctionFacts::VF_ONLY_CALL_ARGUMENT) &&
					!lowerer.result_crosses_call(instruction.dest) &&
					lowerer.incoming_parameter_register_is_intact(
						parameter,
						lowerer.incoming_parameter_registers_[parameter]))
				{
					const X64Register incoming =
						lowerer.incoming_parameter_registers_[parameter];
					// A promoted load may deliberately reuse the untouched ABI
					// register all the way to its first call.  Make that reuse a
					// real allocator lifetime: otherwise a later indirect-call
					// target calculation can allocate r8/r9 and silently replace a
					// pending argument before the parallel call moves run.
					if (lowerer.managed_register(incoming) &&
						!lowerer.registers_.is_used(incoming))
						lowerer.registers_.reserve(incoming);
					value.location = reg_operand(incoming);
				}
			}
			lowerer.set_value(instruction.dest, value);
			return;
		}
		if (load_has_direct_memory_rhs(instruction, block, instruction_index) &&
			!(instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
			  lowerer.tls_wrappers_[instruction.first.symbol].valid()))
		{
			ValueFact value;
			value.location =
				lowerer.materialized_storage(instruction.first, out);
			value.type = instruction.type;
			if (instruction.first.kind == lowir_model::Operand::OP_TEMP)
			{
				value.deferred_address = true;
				const ValueFact& address =
					lowerer.values_[instruction.first.value];
				if (address.deferred_address)
				{
					// Transfer the address inputs to the loaded value instead
					// of nesting dependency records.  The immediate consumer
					// then keeps each carrier live with one O(1) fact lookup.
					value.deferred_address_base =
						address.deferred_address_base;
					value.deferred_address_index =
						address.deferred_address_index;
					--lowerer.facts_.uses[instruction.first.value];
				}
				else value.deferred_address_base = instruction.first;
			}
			lowerer.set_value(instruction.dest, value);
			return;
		}
		if (wide::is_integer(instruction.type))
		{
			const mir_model::MirOperand destination =
				lowerer.allocate_temp_home(instruction.dest, instruction.type);
			wide::append_copy(destination, wide::storage_value(
				lowerer.materialized_storage(instruction.first, out)), out);
			lowerer.consume(instruction.first);
			lowerer.define(instruction.dest, instruction.type, destination);
			return;
		}
		if (selection::is_floating(instruction.type))
		{
			lowerer.emit_float_load(instruction, out);
			return;
		}
		if (lowerer.facts_.has(instruction.dest,
				analysis::FunctionFacts::VF_DIRECT_COMPARE_STORAGE) &&
			!instruction.volatile_access &&
			!(instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
			  lowerer.tls_wrappers_[instruction.first.symbol].valid()))
		{
			lowerer.define(instruction.dest, instruction.type,
				lowerer.storage(instruction.first));
			return;
		}
		if (instruction.type.kind == lowir_model::LTK_OBJECT)
		{
			lowerer.emit_object_load(instruction, out);
			return;
		}

		mir_model::MirOperand destination;
		bool pressure_load = lowerer.constrained_wide_pressure() &&
			lowerer.facts_.definition[instruction.dest] >
			lowerer.facts_.calls.front();
		mir_model::MirOperand pressure_home;
		mir_model::MirOperand takeover_storage;
		bool address_register_takeover = false;
		if (pressure_load)
		{
			pressure_home =
				lowerer.allocate_temp_home(instruction.dest, instruction.type);
			destination = reg_operand(XR_RAX);
		}
		else if (lowerer.facts_.has(instruction.dest,
				analysis::FunctionFacts::VF_DIRECT_COMPARE_RAX) ||
			(selection::result_is_immediately_stored(block, instruction_index,
				instruction.dest, lowerer.facts_) ||
			 lowerer.result_is_immediate_return(block, instruction_index,
				instruction.dest)))
		{
			destination = reg_operand(XR_RAX);
		}
		else
		{
			// A final-use pointer may load into its own register: x86 reads the
			// effective address before replacing the destination.  This also
			// retains the cursor-advance takeover through a claimed phi home.
			if (instruction.first.kind == lowir_model::Operand::OP_TEMP &&
				lowerer.value_known_[instruction.first.value] &&
				lowerer.values_[instruction.first.value].location.kind ==
					mir_model::MirOperand::OP_REG &&
				lowerer.can_reuse(instruction.first))
			{
				const X64Register home =
					lowerer.values_[instruction.first.value].location.reg;
				const bool phi_takeover = lowerer.is_phi_home_register(home);
				const bool staged_takeover = lowerer.optimization_level_ >= 1 &&
					lowerer.facts_.has_eh &&
					lowerer.facts_.has(instruction.dest,
						analysis::FunctionFacts::VF_EDGE_LIVE) &&
					(lowerer.facts_.has(instruction.dest,
						 analysis::FunctionFacts::VF_LOOP_INVARIANT) ||
					 lowerer.result_crosses_call(instruction.dest)) &&
					!lowerer.facts_.has(instruction.dest,
						analysis::FunctionFacts::VF_EXACT_FORWARD_EDGE) &&
					lowerer.planned_register_entry(instruction.dest) == 0 &&
					!lowerer.facts_.has(instruction.first.value,
						analysis::FunctionFacts::VF_EDGE_LIVE);
				if (phi_takeover || staged_takeover)
					takeover_storage =
						lowerer.materialized_storage(instruction.first, out);
				if ((phi_takeover || staged_takeover) &&
					takeover_storage.kind == mir_model::MirOperand::OP_DEREF &&
					takeover_storage.reg == home)
				{
					destination = reg_operand(home);
					address_register_takeover = true;
					if (staged_takeover && !phi_takeover && lowerer.stats_)
						++lowerer.stats_->load_address_register_takeovers;
				}
			}
			X64Register result = XR_RSP;
			if (address_register_takeover) {}
			else if (lowerer.try_allocate_result(instruction.dest, out, &result))
				destination = reg_operand(result);
			else
			{
				pressure_load = true;
				pressure_home =
					lowerer.allocate_temp_home(instruction.dest, instruction.type);
				destination = reg_operand(XR_RAX);
			}
		}
		mir_model::MirInstruction load = machine_instruction(
			mir_model::MirInstruction::MI_LOAD, instruction.type);
		load.volatile_access = instruction.volatile_access;
		append_operand(load, destination);
		append_operand(load, address_register_takeover ? takeover_storage :
			lowerer.materialized_storage(instruction.first, out));
		out.push_back(load);
		if (selection::is_integer_or_pointer(instruction.type))
			append_integer_normalization(out, instruction.type, destination);
		lowerer.consume(instruction.first, destination.reg);
		if (pressure_load)
			append_store(out, pressure_home, destination, instruction.type);
		lowerer.define(instruction.dest, instruction.type,
			pressure_load ? pressure_home : destination);
	}

	void emit_store_instruction(const lowir_model::Instruction& instruction,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		Derived& lowerer = static_cast<Derived&>(*this);
		if (instruction.second.kind == lowir_model::Operand::OP_SLOT &&
			(lowerer.storage_facts_.promoted_parameter_slots[
				instruction.second.slot].valid() ||
			 lowerer.storage_facts_.forwarded_parameter_slots[
				instruction.second.slot].valid()) &&
			instruction.first.kind == lowir_model::Operand::OP_TEMP &&
			lowerer.storage_facts_.has(
				instruction.first.value,
				analysis::StorageFacts::VF_PROMOTED_PARAMETER))
			return;
		if (instruction.second.kind == lowir_model::Operand::OP_SLOT &&
			lowerer.discarded_slots_[instruction.second.slot])
		{
			lowerer.consume(instruction.first);
			lowerer.consume(instruction.second);
			return;
		}
		if (selection::is_floating(instruction.type))
		{
			lowerer.emit_float_store(instruction, out);
			return;
		}
		if (lowerer.emit_object_store(instruction, out)) return;
		if (instruction.second.kind == lowir_model::Operand::OP_GLOBAL &&
			lowerer.tls_wrappers_[instruction.second.symbol].valid())
		{
			mir_model::MirOperand value = lowerer.resolve(instruction.first);
			X64Register stable = XR_RSP;
			if (lowerer.registers_.try_allocate(true, stable))
			{
				lowerer.move_value_to_register(out, stable, value,
					lowerer.operand_type(instruction.first));
				value = reg_operand(stable);
			}
			else if (value.kind != mir_model::MirOperand::OP_REG)
			{
				lowerer.move_value_to_register(out, XR_RAX, value,
					lowerer.operand_type(instruction.first));
				value = reg_operand(XR_RAX);
			}
			append_store(out,
				lowerer.materialized_storage(instruction.second, out), value,
				instruction.type);
			lowerer.consume(instruction.first);
			lowerer.consume(instruction.second);
			if (stable != XR_RSP) lowerer.registers_.release(stable);
			return;
		}
		if (wide::is_integer(instruction.type))
		{
			wide::append_copy(
				lowerer.materialized_storage(instruction.second, out),
				lowerer.wide_value(instruction.first), out);
			lowerer.consume(instruction.first);
			lowerer.consume(instruction.second);
			return;
		}
		mir_model::MirInstruction store = machine_instruction(
			mir_model::MirInstruction::MI_STORE, instruction.type);
		store.volatile_access = instruction.volatile_access;
		mir_model::MirOperand value = lowerer.resolve(instruction.first);
		if (value.kind != mir_model::MirOperand::OP_REG &&
			value.kind != mir_model::MirOperand::OP_IMM)
		{
			X64Register scratch = XR_RAX;
			if (instruction.second.kind == lowir_model::Operand::OP_TEMP &&
				lowerer.value_known_[instruction.second.value] &&
				operand_uses_register(
					lowerer.values_[instruction.second.value].location, XR_RAX))
				scratch = XR_R11;
			if (lowerer.is_frame_address(instruction.first))
				lowerer.emit_operand_address(out, scratch, instruction.first);
			else
				lowerer.move_value_to_register(out, scratch, value,
					lowerer.operand_type(instruction.first));
			value = reg_operand(scratch);
		}
		append_operand(store,
			lowerer.materialized_storage(instruction.second, out,
				value.kind == mir_model::MirOperand::OP_REG && value.reg == XR_RCX ?
				XR_RAX : XR_RCX));
		append_operand(store, value);
		out.push_back(store);
		if (value.kind == mir_model::MirOperand::OP_IMM && lowerer.stats_)
			++lowerer.stats_->immediate_stores_selected;
		lowerer.consume(instruction.first);
		lowerer.consume(instruction.second);
	}
};

}
}
