#include "pa15_lowir_render.h"
#include "pa15_lowir_model.h"

#include <ostream>
#include <stdexcept>
#include <string>

namespace cppgm
{
namespace
{

using namespace pa15_lowir_detail;

void WriteType(std::ostream& output, const LowType& type)
{
	switch (type.kind)
	{
	case LOW_VOID: output << "void"; return;
	case LOW_I8: output << "i8"; return;
	case LOW_U8: output << "u8"; return;
	case LOW_I16: output << "i16"; return;
	case LOW_U16: output << "u16"; return;
	case LOW_I32: output << "i32"; return;
	case LOW_U32: output << "u32"; return;
	case LOW_I64: output << "i64"; return;
	case LOW_F32: output << "f32"; return;
	case LOW_F64: output << "f64"; return;
	case LOW_F80: output << "f80"; return;
	case LOW_PTR: output << "ptr"; return;
	case LOW_OBJECT:
		output << "obj<" << type.width / 8 << 'x' << type.alignment << '>';
		return;
	case LOW_INVALID: break;
	}
	throw std::logic_error("missing PA15 LowIR type");
}

void WriteParameter(std::ostream& output, const Parameter& parameter)
{
	output << '%' << parameter.name << " : ";
	WriteType(output, parameter.type);
	if (parameter.reference) output << " [pass=reference]";
}

void WriteBoundary(std::ostream& output,
	const std::vector<Parameter>& parameters, const LowType& result,
	bool variadic)
{
	output << '(';
	for (std::size_t i = 0; i < parameters.size(); ++i)
	{
		if (i != 0) output << ", ";
		WriteParameter(output, parameters[i]);
	}
	output << ") -> ";
	WriteType(output, result);
	if (variadic) output << " [arity=variadic]";
}

void WriteOperand(std::ostream& output, const Operand& operand,
	const TypedProgram& program, const Function& function)
{
	switch (operand.kind)
	{
	case Operand::TEMP: output << "%t" << operand.id; break;
	case Operand::PARAMETER:
		if (operand.id >= function.parameters.size())
			throw std::logic_error("invalid PA15 parameter reference");
		output << '%' << function.parameters[operand.id].name;
		break;
	case Operand::SLOT:
		if (operand.id >= function.slots.size())
			throw std::logic_error("invalid PA15 slot reference");
		output << '$' << function.slots[operand.id].name;
		break;
	case Operand::GLOBAL: case Operand::FUNCTION:
		if (operand.id >= program.symbols.size())
			throw std::logic_error("invalid PA15 symbol reference");
		output << '@' << program.symbols[operand.id].name;
		break;
	case Operand::INTEGER: output << operand.integer_value; break;
	case Operand::FLOATING: output << program.literals.Get(operand.id); break;
	case Operand::NULL_POINTER: output << "nullptr"; break;
	case Operand::NONE: throw std::logic_error("missing PA15 LowIR operand");
	}
}

const char* OperationText(LowOperation operation)
{
	switch (operation)
	{
	case LOW_OP_NEG: return "neg";
	case LOW_OP_BITNOT: return "bitnot";
	case LOW_OP_ADD: return "add";
	case LOW_OP_SUB: return "sub";
	case LOW_OP_MUL: return "mul";
	case LOW_OP_DIV: return "div";
	case LOW_OP_UDIV: return "udiv";
	case LOW_OP_MOD: return "mod";
	case LOW_OP_UMOD: return "umod";
	case LOW_OP_AND: return "and";
	case LOW_OP_OR: return "or";
	case LOW_OP_XOR: return "xor";
	case LOW_OP_SHL: return "shl";
	case LOW_OP_SHR: return "shr";
	case LOW_OP_USHR: return "ushr";
	case LOW_OP_EQ: return "eq";
	case LOW_OP_NE: return "ne";
	case LOW_OP_LT: return "lt";
	case LOW_OP_ULT: return "ult";
	case LOW_OP_LE: return "le";
	case LOW_OP_ULE: return "ule";
	case LOW_OP_GT: return "gt";
	case LOW_OP_UGT: return "ugt";
	case LOW_OP_GE: return "ge";
	case LOW_OP_UGE: return "uge";
	case LOW_OP_TRUNC: return "trunc";
	case LOW_OP_SEXT: return "sext";
	case LOW_OP_ZEXT: return "zext";
	case LOW_OP_SITOFP: return "sitofp";
	case LOW_OP_UITOFP: return "uitofp";
	case LOW_OP_FPTOSI: return "fptosi";
	case LOW_OP_FPTOUI: return "fptoui";
	case LOW_OP_FPTRUNC: return "fptrunc";
	case LOW_OP_FPEXT: return "fpext";
	case LOW_OP_DECAY: return "decay";
	case LOW_OP_NONE: break;
	}
	throw std::logic_error("missing PA15 LowIR operation");
}

void ValidateExtraRange(const Instruction& instruction, std::size_t size,
	const char* description)
{
	if (instruction.extra_count == 0)
	{
		if (instruction.extra_first != kNoLowId)
			throw std::logic_error(std::string("invalid empty PA15 ") + description);
		return;
	}
	if (instruction.extra_first == kNoLowId || instruction.extra_first > size ||
		instruction.extra_count > size - instruction.extra_first)
		throw std::logic_error(std::string("invalid PA15 ") + description);
}

void WriteInstruction(std::ostream& output, const Instruction& instruction,
	const TypedProgram& program, const Function& function)
{
	switch (instruction.kind)
	{
	case Instruction::CONST:
		output << "%t" << instruction.dest << " = const ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::COPY:
		output << "%t" << instruction.dest << " = copy ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::ADDR:
		output << "%t" << instruction.dest << " = addr ";
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::LOAD:
		output << "%t" << instruction.dest << " = load ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::STORE:
		output << "store ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::INDEX:
		output << "%t" << instruction.dest << " = index ";
		WriteType(output, instruction.type);
		if (instruction.indirect) output << " [projection=array_element]";
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::UNARY:
		output << "%t" << instruction.dest << " = unary "
			<< OperationText(instruction.op) << ' ';
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::BINARY:
		output << "%t" << instruction.dest << " = binary "
			<< OperationText(instruction.op) << ' ';
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::CMP:
		output << "%t" << instruction.dest << " = cmp "
			<< OperationText(instruction.op) << ' ';
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::CONVERT:
		output << "%t" << instruction.dest << " = convert "
			<< OperationText(instruction.op) << ' ';
		WriteType(output, instruction.type);
		output << ' ';
		WriteType(output, instruction.source_type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::CALL:
		ValidateExtraRange(instruction, program.call_arguments.size(),
			"call argument range");
		ValidateExtraRange(instruction,
			program.call_argument_references.size(), "call reference range");
		if (instruction.dest != kNoLowId) output << "%t" << instruction.dest << " = ";
		output << "call ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << '(';
		for (std::size_t i = 0; i < instruction.extra_count; ++i)
		{
			if (i != 0) output << ", ";
			WriteOperand(output,
				program.call_arguments[instruction.extra_first + i], program, function);
		}
		output << ')';
		if (instruction.indirect)
		{
			output << " as (";
			for (std::size_t i = 0; i < instruction.extra_count; ++i)
			{
				if (i != 0) output << ", ";
				output << "%arg" << i << " : ";
				WriteType(output,
					program.call_arguments[instruction.extra_first + i].type);
				if (program.call_argument_references[instruction.extra_first + i])
					output << " [pass=reference]";
			}
			output << ") -> ";
			WriteType(output, instruction.type);
		}
		break;
	case Instruction::JUMP:
		if (instruction.target >= function.blocks.size())
			throw std::logic_error("invalid PA15 jump target");
		output << "jump ^" << function.blocks[instruction.target].label;
		break;
	case Instruction::BRANCH:
		if (instruction.target >= function.blocks.size() ||
			instruction.alternate >= function.blocks.size())
			throw std::logic_error("invalid PA15 branch target");
		output << "branch ";
		WriteOperand(output, instruction.first, program, function);
		output << ", ^" << function.blocks[instruction.target].label << ", ^"
			<< function.blocks[instruction.alternate].label;
		break;
	case Instruction::SWITCH:
		ValidateExtraRange(instruction, program.switch_case_values.size(),
			"switch value range");
		ValidateExtraRange(instruction, program.switch_case_targets.size(),
			"switch target range");
		if (instruction.target >= function.blocks.size())
			throw std::logic_error("invalid PA15 switch default target");
		output << "switch ";
		WriteOperand(output, instruction.first, program, function);
		output << ", ^" << function.blocks[instruction.target].label;
		for (std::size_t i = 0; i < instruction.extra_count; ++i)
		{
			const BlockId case_target =
				program.switch_case_targets[instruction.extra_first + i];
			if (case_target >= function.blocks.size())
				throw std::logic_error("invalid PA15 switch case target");
			output << ", "
				<< program.switch_case_values[instruction.extra_first + i] << ":^"
				<< function.blocks[case_target].label;
		}
		break;
	case Instruction::RETURN_VALUE:
		output << "return ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::RETURN_VOID: output << "return void"; break;
	}
}

void WriteSymbolMetadata(std::ostream& output, const Symbol& symbol,
	bool entry, bool function, bool initializer = false)
{
	output << " [";
	bool separator = false;
	if (function && symbol.nonthrowing)
	{
		output << "unwind=no";
		separator = true;
	}
	if (entry)
	{
		if (separator) output << ", ";
		output << "role=entry";
		separator = true;
	}
	if (initializer)
	{
		if (separator) output << ", ";
		output << "role=init";
		separator = true;
	}
	if (symbol.c_linkage)
	{
		if (separator) output << ", ";
		output << "linkage=c";
		separator = true;
	}
	if (separator) output << ", ";
	output << "binding=" << (symbol.internal_linkage ? "internal" : "strong");
	if (!symbol.object_name.empty()) output << ", object=" << symbol.object_name;
	if (entry) output << ", keep_alias=yes";
	output << ']';
}

void RenderProgram(const TypedProgram& program, std::ostream& output)
{
	bool wrote = false;
	for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
	{
		const GlobalDeclaration& declaration = program.global_declarations[i];
		const Symbol& symbol = program.symbols[declaration.symbol];
		if (symbol.definition_emitted || !symbol.referenced) continue;
		if (wrote) output << '\n';
		output << "declare global @" << symbol.name;
		if (declaration.typed)
		{
			output << " : ";
			WriteType(output, declaration.type);
		}
		WriteSymbolMetadata(output, symbol, false, false);
		output << '\n';
		wrote = true;
	}
	for (std::size_t i = 0; i < program.declarations.size(); ++i)
	{
		const FunctionDeclaration& declaration = program.declarations[i];
		const Symbol& symbol = program.symbols[declaration.symbol];
		if (symbol.definition_emitted || !symbol.referenced) continue;
		if (wrote) output << '\n';
		output << "declare function @" << symbol.name;
		WriteBoundary(output, declaration.parameters, declaration.result,
			declaration.variadic);
		WriteSymbolMetadata(output, symbol, false, true);
		output << '\n';
		wrote = true;
	}
	for (std::size_t i = 0; i < program.globals.size(); ++i)
	{
		const Global& global = program.globals[i];
		const Symbol& symbol = program.symbols[global.symbol];
		if (wrote) output << '\n';
		output << "global @" << symbol.name;
		if (global.initializer_kind != Global::STRUCTURED_VALUE)
		{
			output << " : ";
			WriteType(output, global.type);
		}
		WriteSymbolMetadata(output, symbol, false, false);
		output << " = ";
		if (global.initializer_kind == Global::ZERO) output << "zero\n";
		else if (global.initializer_kind == Global::INTEGER_VALUE)
			output << global.initializer << '\n';
		else if (global.initializer_kind == Global::FLOATING_VALUE)
			output << program.literals.Get(global.floating_initializer) << '\n';
		else if (global.initializer_kind == Global::ADDRESS_VALUE)
		{
			output << "addr @" << program.symbols[global.address_symbol].name;
			if (global.address_offset > 0) output << " + " << global.address_offset;
			else if (global.address_offset < 0)
				output << " - " << -global.address_offset;
			output << '\n';
		}
		else
		{
			output << "{\n";
			for (std::size_t item_index = 0;
				item_index < global.items.size(); ++item_index)
			{
				const Global::DataItem& item = global.items[item_index];
				output << "  ";
				if (item.kind == Global::DataItem::ZERO_ITEM)
					output << "zero " << item.zero_bytes;
				else if (item.kind == Global::DataItem::ADDRESS_ITEM)
				{
					output << "ptr addr @" << program.symbols[item.symbol].name;
					if (item.offset > 0) output << " + " << item.offset;
					else if (item.offset < 0) output << " - " << -item.offset;
				}
				else if (item.kind == Global::DataItem::FLOATING_ITEM)
				{
					WriteType(output, item.type);
					output << ' ' << program.literals.Get(item.floating_spelling);
				}
				else
				{
					WriteType(output, item.type);
					output << ' ' << item.integer_value;
				}
				output << '\n';
			}
			output << "}\n";
		}
		wrote = true;
	}
	for (std::size_t i = 0; i < program.functions.size(); ++i)
	{
		const Function& function = program.functions[i];
		const Symbol& symbol = program.symbols[function.symbol];
		if (wrote) output << '\n';
		output << "function @" << symbol.name;
		WriteBoundary(output, function.parameters, function.result, function.variadic);
		WriteSymbolMetadata(output, symbol, function.entry, true,
			function.initializer);
		output << " {\n";
		for (std::size_t s = 0; s < function.slots.size(); ++s)
		{
			output << "  slot $" << function.slots[s].name << " : ";
			WriteType(output, function.slots[s].type);
			output << '\n';
		}
		if (!function.slots.empty()) output << '\n';
		for (std::size_t order = 0; order < function.block_order.size(); ++order)
		{
			const BlockId b = function.block_order[order];
			if (order != 0) output << '\n';
			output << "  block ^" << function.blocks[b].label << ":\n";
			for (std::size_t j = 0; j < function.blocks[b].instructions.size(); ++j)
			{
				output << "    ";
				WriteInstruction(output, function.blocks[b].instructions[j],
					program, function);
				output << '\n';
			}
		}
		output << "}\n";
		wrote = true;
	}
}


}

void RenderLowIRProgram(const pa15_lowir_detail::TypedProgram& program,
	std::ostream& output)
{
	RenderProgram(program, output);
}

}
