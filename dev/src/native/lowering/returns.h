#pragma once

#include "native/errors.h"
#include "native/lowering/abi.h"
#include "native/mir/construction.h"
#include "native/lowering/selection.h"
#include "native/lowering/wide.h"

#include <vector>

namespace lowir_native
{
namespace return_detail
{

template <class Derived>
class ReturnLowering
{
protected:
	void emit_return(const lowir_model::Instruction& instruction,
		std::vector<mir_model::MirInstruction>& out)
	{
		using namespace build;
		Derived& lowerer = static_cast<Derived&>(*this);
		const lowir_model::LowType& source_type =
			instruction.type.kind == lowir_model::LTK_VOID ?
			instruction.type : lowerer.operand_type(instruction.first);
		mir_model::MirOperand scalar_return = reg_operand(XR_RAX);
		if (wide::is_integer(instruction.type))
		{
			const wide::Value value = lowerer.wide_value(instruction.first);
			wide::append_word_to_register(value, 0, XR_RAX, XR_R11, out);
			wide::append_word_to_register(value, 1, XR_RDX, XR_R11, out);
		}
		else if (instruction.type.kind == lowir_model::LTK_OBJECT)
		{
			if (instruction.type.storage_size > 16)
				native_errors::ThrowSource(
					"direct object return exceeds two SysV eightbytes");
			const std::size_t chunks =
				(instruction.type.storage_size + 7) / 8;
			if (instruction.first.kind == lowir_model::Operand::OP_INTEGER &&
				selection::integer_value(instruction.first) == 0)
			{
				for (std::size_t chunk = 0; chunk < chunks; ++chunk)
					append_move(out, reg_operand(chunk ? XR_RDX : XR_RAX),
						immediate(0));
			}
			else
			{
				mir_model::MirOperand direct_storage;
				const bool direct = lowerer.direct_object_chunk_storage(
					instruction.first, 0, &direct_storage);
				if (!direct)
					lowerer.emit_operand_address(out, XR_R11, instruction.first);
				for (std::size_t chunk = 0; chunk < chunks; ++chunk)
				{
					const lowir_model::LowType& chunk_type =
						abi::object_chunk_type(
							instruction.type.storage_size - chunk * 8);
					mir_model::MirOperand storage = direct ? direct_storage :
						dereference(XR_R11);
					storage.offset += static_cast<long long>(chunk * 8);
					append_load(out, reg_operand(chunk ? XR_RDX : XR_RAX),
						storage, chunk_type);
				}
			}
		}
		else if (selection::is_extended_float(instruction.type))
		{
			lowerer.uses_scalar_float_ = true;
			mir_model::MirOperand source = lowerer.resolve(instruction.first);
			if (selection::is_floating(source_type) &&
				!lowir_model::same_lowir_type(source_type, instruction.type))
			{
				const mir_model::MirOperand converted =
					lowerer.allocate_named_temp_home(
						lowir_model::FPN_F80_RETURN, instruction.type);
				lowerer.append_float_width_conversion(out, converted, source,
					source_type, instruction.type);
				source = converted;
			}
			mir_model::MirInstruction result = machine_instruction(
				mir_model::MirInstruction::MI_FRET, instruction.type);
			append_operand(result, source);
			out.push_back(result);
			lowerer.consume(instruction.first);
			return;
		}
		else if (selection::is_scalar_float(instruction.type))
		{
			lowerer.uses_scalar_float_ = true;
			if (selection::is_extended_float(source_type))
				lowerer.append_float_width_conversion(out, xmm_operand(XMM_0),
					lowerer.resolve(instruction.first), source_type,
					instruction.type);
			else
				append_float_move(out, xmm_operand(XMM_0),
					lowerer.resolve(instruction.first), instruction.type);
		}
		else if (instruction.type.kind != lowir_model::LTK_VOID)
		{
			if (lowerer.is_frame_address(instruction.first)) {
				lowerer.emit_operand_address(out, XR_RAX, instruction.first);
				scalar_return = reg_operand(XR_RAX);
			}
			else
			{
				scalar_return = lowerer.resolve(instruction.first);
				if (scalar_return.kind != mir_model::MirOperand::OP_REG)
				{
					lowerer.move_value_to_register(out, XR_RAX, scalar_return,
						instruction.type);
					scalar_return = reg_operand(XR_RAX);
				}
			}
		}
		mir_model::MirInstruction ret =
			machine_instruction(mir_model::MirInstruction::MI_RET);
		if (instruction.type.kind != lowir_model::LTK_VOID &&
			!selection::is_scalar_float(instruction.type) &&
			!selection::is_extended_float(instruction.type) &&
			instruction.type.kind != lowir_model::LTK_OBJECT)
			append_operand(ret, scalar_return);
		out.push_back(ret);
		lowerer.consume(instruction.first);
	}
};

}
}
