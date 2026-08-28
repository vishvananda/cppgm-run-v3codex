#pragma once

#include "native/lowering/selection.h"
#include "native/lowering/values.h"
#include "native/lowering/wide.h"

#include <cstddef>
#include <vector>

namespace lowir_native
{

template <class Derived>
class AtomicLowering
{
protected:
	GprMove atomic_move(X64Register destination,
		const lowir_model::Operand& source) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		GprMove move;
		move.destination = destination;
		move.source = derived.resolve(source);
		move.type = derived.operand_type(source);
		move.source_is_address = derived.is_frame_address(source);
		return move;
	}

	void append_atomic(mir_model::MirInstruction::Opcode opcode,
		const lowir_model::LowType& type,
		const mir_model::MirOperand& address, X64Register source,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		mir_model::MirInstruction atomic = machine_instruction(opcode, type);
		append_operand(atomic, address);
		append_operand(atomic, reg_operand(source));
		out.push_back(atomic);
	}

	void emit_atomic_load(const lowir_model::Instruction& instruction,
		const lowir_model::LowirBlock& block, std::size_t instruction_index,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		Derived& derived = static_cast<Derived&>(*this);
		selection::atomic_order(instruction.args.at(0));
		if (wide::is_integer(instruction.type))
		{
			const mir_model::MirOperand destination =
				derived.allocate_temp_home(instruction.dest, instruction.type);
			wide::append_atomic_load(
				derived.materialized_storage(instruction.first, out), destination, out);
			derived.consume(instruction.first);
			derived.consume(instruction.args[0]);
			derived.define(instruction.dest, instruction.type, destination);
			return;
		}
		mir_model::MirOperand destination = reg_operand(XR_RAX);
		mir_model::MirOperand pressure_home;
		if (!derived.result_is_immediate_return(
			block, instruction_index, instruction.dest))
		{
			X64Register result = XR_RSP;
			if (derived.try_allocate_result(instruction.dest, out, &result))
				destination = reg_operand(result);
			else pressure_home =
				derived.allocate_temp_home(instruction.dest, instruction.type);
		}
		append_load(out, destination,
			derived.materialized_storage(instruction.first, out), instruction.type);
		append_integer_normalization(out, instruction.type, destination);
		derived.consume(instruction.first, destination.reg);
		derived.consume(instruction.args[0]);
		derived.finalize_integer_result(instruction.dest, instruction.type,
			destination, pressure_home, out);
	}

	void emit_atomic_store(const lowir_model::Instruction& instruction,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		Derived& derived = static_cast<Derived&>(*this);
		const bool sequential = selection::atomic_order(instruction.args.at(0)) == 5;
		mir_model::MirOperand value = derived.resolve(instruction.first);
		if (sequential || value.kind != mir_model::MirOperand::OP_REG)
		{
			derived.move_value_to_register(
				out, XR_RAX, value, derived.operand_type(instruction.first));
			value = reg_operand(XR_RAX);
		}
		const mir_model::MirOperand address =
			derived.materialized_storage(instruction.second, out,
				value.kind == mir_model::MirOperand::OP_REG && value.reg == XR_RCX ?
				XR_RAX : XR_RCX);
		if (sequential)
			append_atomic(mir_model::MirInstruction::MI_XCHG,
				instruction.type, address, XR_RAX, out);
		else append_store(out, address, value, instruction.type);
		derived.consume(instruction.first);
		derived.consume(instruction.second);
		derived.consume(instruction.args[0]);
	}

	void emit_atomic_exchange(const lowir_model::Instruction& instruction,
		const lowir_model::LowirBlock& block, std::size_t instruction_index,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		Derived& derived = static_cast<Derived&>(*this);
		selection::atomic_order(instruction.args.at(0));
		const mir_model::MirOperand address =
			derived.materialized_storage(instruction.first, out);
		derived.move_value_to_register(
			out, XR_RAX, derived.resolve(instruction.second), instruction.type);
		append_atomic(mir_model::MirInstruction::MI_XCHG,
			instruction.type, address, XR_RAX, out);
		mir_model::MirOperand destination = reg_operand(XR_RAX);
		mir_model::MirOperand pressure_home;
		if (!derived.result_is_immediate_return(
			block, instruction_index, instruction.dest))
		{
			X64Register result = XR_RSP;
			if (derived.try_allocate_result(instruction.dest, out, &result))
			{
				destination = reg_operand(result);
				append_move(out, destination, reg_operand(XR_RAX));
			}
			else pressure_home =
				derived.allocate_temp_home(instruction.dest, instruction.type);
		}
		append_integer_normalization(out, instruction.type, destination);
		derived.consume(instruction.first);
		derived.consume(instruction.second);
		derived.consume(instruction.args[0]);
		derived.finalize_integer_result(instruction.dest, instruction.type,
			destination, pressure_home, out);
	}

	void emit_atomic_add_fetch(const lowir_model::Instruction& instruction,
		const lowir_model::LowirBlock& block, std::size_t instruction_index,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		Derived& derived = static_cast<Derived&>(*this);
		selection::atomic_order(instruction.args.at(0));
		std::vector<GprMove> moves;
		moves.push_back(atomic_move(XR_RCX, instruction.first));
		moves.push_back(atomic_move(XR_RDX, instruction.second));
		moves.push_back(atomic_move(XR_RAX, instruction.second));
		derived.emit_parallel_gpr_moves(moves, out);
		append_atomic(mir_model::MirInstruction::MI_LOCK_XADD,
			instruction.type, dereference(XR_RCX), XR_RAX, out);
		mir_model::MirInstruction add =
			machine_instruction(mir_model::MirInstruction::MI_ADD);
		append_operand(add, reg_operand(XR_RAX));
		append_operand(add, reg_operand(XR_RDX));
		out.push_back(add);
		mir_model::MirOperand destination = reg_operand(XR_RAX);
		mir_model::MirOperand pressure_home;
		if (!derived.result_is_immediate_return(
			block, instruction_index, instruction.dest))
		{
			X64Register result = XR_RSP;
			if (derived.try_allocate_result(instruction.dest, out, &result))
			{
				destination = reg_operand(result);
				append_move(out, destination, reg_operand(XR_RAX));
			}
			else pressure_home =
				derived.allocate_temp_home(instruction.dest, instruction.type);
		}
		append_integer_normalization(out, instruction.type, destination);
		derived.consume(instruction.first);
		derived.consume(instruction.second);
		derived.consume(instruction.args[0]);
		derived.finalize_integer_result(instruction.dest, instruction.type,
			destination, pressure_home, out);
	}

	void emit_atomic_compare_exchange(
		const lowir_model::Instruction& instruction,
		const lowir_model::LowirBlock& block, std::size_t instruction_index,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		Derived& derived = static_cast<Derived&>(*this);
		selection::atomic_order(instruction.args.at(0));
		selection::atomic_order(instruction.args.at(1));
		if (wide::is_integer(instruction.type))
		{
			derived.emit_operand_address(out, XR_R10, instruction.second);
			wide::append_atomic_compare_exchange(
				derived.materialized_storage(instruction.first, out),
				dereference(XR_R10), derived.wide_value(instruction.third), out);
			const mir_model::MirOperand destination =
				reg_operand(derived.allocate_result(instruction.dest, out));
			append_move(out, destination, reg_operand(XR_RAX));
			derived.consume(instruction.first);
			derived.consume(instruction.second);
			derived.consume(instruction.third);
			derived.consume(instruction.args[0]);
			derived.consume(instruction.args[1]);
			derived.define(instruction.dest,
				lowir_model::builtin_lowir_type(lowir_model::LTK_I64), destination);
			return;
		}
		std::vector<GprMove> moves;
		moves.push_back(atomic_move(XR_RCX, instruction.first));
		moves.push_back(atomic_move(XR_RDX, instruction.second));
		GprMove desired = atomic_move(XR_RSI, instruction.third);
		if (desired.source.kind == mir_model::MirOperand::OP_REG &&
			(desired.source.reg == XR_RCX || desired.source.reg == XR_RDX))
		{
			append_move(out, reg_operand(XR_R10), desired.source);
			desired.source = reg_operand(XR_R10);
		}
		derived.emit_parallel_gpr_moves(moves, out);
		append_load(out, reg_operand(XR_RAX), dereference(XR_RDX),
			instruction.type);
		derived.emit_gpr_move(desired, out);
		append_atomic(mir_model::MirInstruction::MI_LOCK_CMPXCHG,
			instruction.type, dereference(XR_RCX), XR_RSI, out);
		append_store(out, dereference(XR_RDX), reg_operand(XR_RAX),
			instruction.type);
		mir_model::MirInstruction equal =
			machine_instruction(mir_model::MirInstruction::MI_SETCC);
		equal.condition = XC_E;
		append_operand(equal, reg_operand(XR_RAX));
		out.push_back(equal);
		mir_model::MirInstruction widen =
			machine_instruction(mir_model::MirInstruction::MI_MOVZX);
		append_operand(widen, reg_operand(XR_RAX));
		append_operand(widen, reg_operand(XR_RAX));
		out.push_back(widen);
		mir_model::MirOperand destination = reg_operand(XR_RAX);
		mir_model::MirOperand pressure_home;
		if (!derived.result_is_immediate_return(
			block, instruction_index, instruction.dest))
		{
			X64Register result = XR_RSP;
			if (derived.try_allocate_result(instruction.dest, out, &result))
			{
				destination = reg_operand(result);
				append_move(out, destination, reg_operand(XR_RAX));
			}
			else pressure_home =
				derived.allocate_temp_home(instruction.dest,
					lowir_model::builtin_lowir_type(lowir_model::LTK_I64));
		}
		derived.consume(instruction.first);
		derived.consume(instruction.second);
		derived.consume(instruction.third);
		derived.consume(instruction.args[0]);
		derived.consume(instruction.args[1]);
		if (pressure_home.kind == mir_model::MirOperand::OP_FRAME)
			append_store(out, pressure_home, reg_operand(XR_RAX), machine_type(lowir_model::LTK_I64));
		derived.define(instruction.dest,
			lowir_model::builtin_lowir_type(lowir_model::LTK_I64),
			pressure_home.kind == mir_model::MirOperand::OP_FRAME ?
			pressure_home : destination);
	}

	void emit_atomic_fence(const lowir_model::Instruction& instruction,
		std::vector<mir_model::MirInstruction>& out)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const long long order = selection::atomic_order(instruction.first);
		if (instruction.kind == lowir_model::Instruction::IK_ATOMIC_THREAD_FENCE &&
			order == 5)
			out.push_back(build::machine_instruction(
				mir_model::MirInstruction::MI_MFENCE));
		derived.consume(instruction.first);
	}
};

}
