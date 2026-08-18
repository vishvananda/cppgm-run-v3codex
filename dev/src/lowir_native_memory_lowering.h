#pragma once

#include "lowir_native_selection.h"
#include "lowir_native_value.h"
#include "lowir_native_wide.h"

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
	void emit_load_instruction(const lowir_model::Instruction& instruction,
		const lowir_model::LowirBlock& block, std::size_t instruction_index,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		Derived& lowerer = static_cast<Derived&>(*this);
		if (instruction.first.kind == lowir_model::Operand::OP_SLOT &&
			(lowerer.storage_facts_.promoted_parameter_slots.count(
				instruction.first.text) ||
			 lowerer.storage_facts_.forwarded_parameter_slots.count(
				instruction.first.text)))
		{
			const std::string& parameter =
				lowerer.storage_facts_.promoted_parameter_slots.count(
					instruction.first.text) ?
				lowerer.storage_facts_.promoted_parameter_slots.find(
					instruction.first.text)->second :
				lowerer.storage_facts_.forwarded_parameter_slots.find(
					instruction.first.text)->second;
			ValueFact value = lowerer.values_.find(parameter)->second;
			value.type = instruction.type;
			value.parameter = false;
			if (!lowerer.facts_.calls.empty() &&
				lowerer.incoming_parameter_registers_.count(parameter))
			{
				const std::size_t load_position =
					lowerer.facts_.definition[instruction.dest];
				if (load_position < lowerer.facts_.calls.front() &&
					lowerer.facts_.only_call_arguments.count(instruction.dest) &&
					!lowerer.result_crosses_call(instruction.dest) &&
					lowerer.incoming_parameter_register_is_intact(
						parameter,
						lowerer.incoming_parameter_registers_.find(parameter)->second))
				{
					const X64Register incoming =
						lowerer.incoming_parameter_registers_.find(parameter)->second;
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
				else if (load_position > lowerer.facts_.calls.front())
				{
					const bool preserved =
						lowerer.result_crosses_call(instruction.dest);
					const bool direct_call_alias =
						value.fixed_register_home && !preserved &&
						lowerer.facts_.only_call_arguments.count(instruction.dest);
					X64Register forwarded = XR_R9;
					bool allocated = true;
					if (direct_call_alias)
						allocated = false;
					else if (preserved)
						allocated = lowerer.try_allocate_result(
							instruction.dest, out, &forwarded);
					else if (lowerer.nonparameter_value_live_in_register(forwarded))
						allocated = lowerer.registers_.try_allocate(false, forwarded);
					else if (!lowerer.registers_.is_used(forwarded))
						lowerer.registers_.reserve(forwarded);
					// Storage analysis already required a stable cross-call home.
					// If the speculative copy cannot allocate a register, calls can
					// continue to consume that home directly.
					if (!direct_call_alias && allocated)
					{
						append_move(out, reg_operand(forwarded), value.location);
						value.location = reg_operand(forwarded);
					}
					value.forwarded_parameter = parameter;
				}
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
		if (lowerer.facts_.direct_compare_storage_values.count(
				instruction.dest) &&
			!(instruction.first.kind == lowir_model::Operand::OP_GLOBAL &&
			  lowerer.tls_wrappers_.count(instruction.first.text)))
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
		if (pressure_load)
		{
			pressure_home =
				lowerer.allocate_temp_home(instruction.dest, instruction.type);
			destination = reg_operand(XR_RAX);
		}
		else if (lowerer.facts_.direct_compare_rax_values.count(
				instruction.dest) ||
			(selection::result_is_immediately_stored(block, instruction_index,
				instruction.dest, lowerer.facts_) ||
			 lowerer.result_is_immediate_return(block, instruction_index,
				instruction.dest)))
		{
			destination = reg_operand(XR_RAX);
		}
		else
		{
			X64Register result = XR_RSP;
			if (lowerer.try_allocate_result(instruction.dest, out, &result))
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
			mir_model::MirInstruction::MI_LOAD, lowir_model::lowir_type_text(instruction.type));
		append_operand(load, destination);
		append_operand(load,
			lowerer.materialized_storage(instruction.first, out));
		out.push_back(load);
		if (selection::is_integer_or_pointer(instruction.type))
			lowerer.normalize_integer(instruction.type, destination, out);
		lowerer.consume(instruction.first, destination.reg);
		if (pressure_load)
			append_store(out, pressure_home, destination, lowir_model::lowir_type_text(instruction.type));
		lowerer.define(instruction.dest, instruction.type,
			pressure_load ? pressure_home : destination);
	}

	void emit_store_instruction(const lowir_model::Instruction& instruction,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		Derived& lowerer = static_cast<Derived&>(*this);
		if (instruction.second.kind == lowir_model::Operand::OP_SLOT &&
			(lowerer.storage_facts_.promoted_parameter_slots.count(
				instruction.second.text) ||
			 lowerer.storage_facts_.forwarded_parameter_slots.count(
				instruction.second.text)) &&
			instruction.first.kind == lowir_model::Operand::OP_TEMP &&
			lowerer.storage_facts_.promoted_parameters.count(
				instruction.first.text))
			return;
		if (instruction.second.kind == lowir_model::Operand::OP_SLOT &&
			lowerer.discarded_slots_.count(instruction.second.text))
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
			lowerer.tls_wrappers_.count(instruction.second.text))
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
				lowir_model::lowir_type_text(instruction.type));
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
			mir_model::MirInstruction::MI_STORE, lowir_model::lowir_type_text(instruction.type));
		mir_model::MirOperand value = lowerer.resolve(instruction.first);
		if (value.kind != mir_model::MirOperand::OP_REG)
		{
			if (lowerer.is_frame_address(instruction.first))
				lowerer.emit_operand_address(out, XR_RAX, instruction.first);
			else
				lowerer.move_value_to_register(out, XR_RAX, value,
					lowerer.operand_type(instruction.first));
			value = reg_operand(XR_RAX);
		}
		append_operand(store,
			lowerer.materialized_storage(instruction.second, out));
		append_operand(store, value);
		out.push_back(store);
		lowerer.consume(instruction.first);
		lowerer.consume(instruction.second);
	}
};

}
}
