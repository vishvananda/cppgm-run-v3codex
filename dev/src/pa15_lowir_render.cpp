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
	if (parameter.reference || parameter.decay || parameter.indirect_result ||
		parameter.by_address ||
		parameter.capture != Parameter::CAPTURE_DEFAULT ||
		parameter.access != Parameter::ACCESS_DEFAULT ||
		parameter.alias != Parameter::ALIAS_DEFAULT)
	{
		output << " [";
		bool separator = false;
		if (parameter.reference)
		{
			output << "pass=reference";
			separator = true;
		}
		else if (parameter.decay)
		{
			output << "pass=decay";
			separator = true;
		}
		else if (parameter.indirect_result)
		{
			output << "pass=indirect_result";
			separator = true;
		}
		else if (parameter.by_address)
		{
			output << "pass=by_address";
			separator = true;
		}
		if (parameter.capture == Parameter::CAPTURE_NOCAPTURE)
		{
			if (separator) output << ", ";
			output << "capture=nocapture";
			separator = true;
		}
		if (parameter.access != Parameter::ACCESS_DEFAULT)
		{
			if (separator) output << ", ";
			output << "access=" << (parameter.access == Parameter::ACCESS_READ ?
				"read" : parameter.access == Parameter::ACCESS_WRITE ?
				"write" : "readwrite");
			separator = true;
		}
		if (parameter.alias == Parameter::ALIAS_NOALIAS)
		{
			if (separator) output << ", ";
			output << "alias=noalias";
		}
		output << ']';
	}
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
	case Instruction::COPY_OBJECT:
		if (instruction.type.kind != LOW_OBJECT)
			throw std::logic_error("invalid PA17 copyobj span");
		output << "copyobj " << instruction.type.width / 8 << 'x'
			<< instruction.type.alignment << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::INDEX:
		output << "%t" << instruction.dest << " = index ";
		WriteType(output, instruction.type);
		if (instruction.projection == INDEX_PROJECTION_ARRAY_ELEMENT)
			output << " [projection=array_element]";
		else if (instruction.projection == INDEX_PROJECTION_FIELD)
			output << " [projection=field]";
		else if (instruction.projection == INDEX_PROJECTION_BASE_SUBOBJECT)
			output << " [projection=base_subobject]";
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
				const std::uint8_t passing =
					program.call_argument_references[instruction.extra_first + i];
				if (passing == Instruction::CALL_PASS_REFERENCE)
					output << " [pass=reference]";
				else if (passing == Instruction::CALL_PASS_BY_ADDRESS)
					output << " [pass=by_address]";
				else if (passing == Instruction::CALL_PASS_INDIRECT_RESULT)
					output << " [pass=indirect_result]";
			}
			output << ") -> ";
			WriteType(output, instruction.type);
		}
		break;
	case Instruction::EH_TRY:
		if (instruction.target >= function.blocks.size())
			throw std::logic_error("invalid PA16 eh_try target");
		output << "eh_try ^" << function.blocks[instruction.target].label;
		break;
	case Instruction::EH_CLEANUP:
		if (instruction.target >= function.blocks.size())
			throw std::logic_error("invalid PA16 eh_cleanup target");
		output << "eh_cleanup ^" << function.blocks[instruction.target].label;
		break;
	case Instruction::EH_CATCH:
		output << "eh_catch ";
		WriteOperand(output, instruction.first, program, function);
		output << ", " << instruction.second.integer_value;
		break;
	case Instruction::EH_CATCH_ALL:
		output << "eh_catch_all, " << instruction.first.integer_value;
		break;
	case Instruction::EH_END: output << "eh_end"; break;
	case Instruction::EXCEPTION:
		output << "%t" << instruction.dest << " = exception ";
		WriteType(output, instruction.type);
		break;
	case Instruction::EXCEPTION_SELECTOR:
		output << "%t" << instruction.dest << " = exception_selector ";
		WriteType(output, instruction.type);
		break;
	case Instruction::RESUME: output << "resume"; break;
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
	const TypedProgram& program,
	bool entry, bool function, bool initializer = false,
	bool finalizer = false)
{
	output << " [";
	bool separator = false;
	if (function && symbol.effects != Symbol::EFFECTS_DEFAULT)
	{
		output << "effects=" << (symbol.effects == Symbol::EFFECTS_READNONE ?
			"readnone" : symbol.effects == Symbol::EFFECTS_READONLY ?
			"readonly" : "readwrite");
		separator = true;
	}
	if (function && symbol.nonthrowing)
	{
		if (separator) output << ", ";
		output << "unwind=no";
		separator = true;
	}
	if (function && symbol.noreturn)
	{
		if (separator) output << ", ";
		output << "return=noreturn";
		separator = true;
	}
	if (function && symbol.runtime_role != Symbol::RUNTIME_ROLE_NONE)
	{
		if (separator) output << ", ";
		const char* role = symbol.runtime_role == Symbol::RUNTIME_ROLE_EH_RESUME ?
			"eh_resume" :
			symbol.runtime_role == Symbol::RUNTIME_ROLE_EH_ALLOCATE_EXCEPTION ?
			"eh_allocate_exception" :
			symbol.runtime_role == Symbol::RUNTIME_ROLE_EH_BEGIN_CATCH ?
			"eh_begin_catch" :
			symbol.runtime_role == Symbol::RUNTIME_ROLE_EH_END_CATCH ?
			"eh_end_catch" :
			symbol.runtime_role == Symbol::RUNTIME_ROLE_EH_RETHROW ?
			"eh_rethrow" :
			symbol.runtime_role == Symbol::RUNTIME_ROLE_EH_THROW ?
			"eh_throw" : "eh_personality";
		output << "role=" << role;
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
	if (finalizer)
	{
		if (separator) output << ", ";
		output << "role=fini";
		separator = true;
	}
	if (symbol.c_linkage)
	{
		if (separator) output << ", ";
		output << "linkage=c";
		separator = true;
	}
	if (function && symbol.tls_for_symbol != kNoLowId)
	{
		if (symbol.tls_for_symbol >= program.symbols.size())
			throw std::logic_error("invalid PA16 TLS wrapper target");
		if (separator) output << ", ";
		output << "tls_for=@" << program.symbols[symbol.tls_for_symbol].name;
		separator = true;
	}
	if (!function && symbol.thread_local_storage)
	{
		if (separator) output << ", ";
		output << "storage=thread_local";
		separator = true;
	}
	if (separator) output << ", ";
	output << "binding=" << (symbol.internal_linkage ? "internal" :
		symbol.weak_linkage ? "weak" : "strong");
	if (!symbol.object_name.empty()) output << ", object=" << symbol.object_name;
	if (entry) output << ", keep_alias=yes";
	if (symbol.object_output_root) output << ", object_root=yes";
	if (function && symbol.trivial_lifecycle)
		output << ", trivial_lifecycle=yes";
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
		WriteSymbolMetadata(output, symbol, program, false, false);
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
		WriteSymbolMetadata(output, symbol, program, false, true);
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
		WriteSymbolMetadata(output, symbol, program, false, false);
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
		WriteSymbolMetadata(output, symbol, program, function.entry, true,
			function.initializer, function.finalizer);
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
	for (std::size_t i = 0; i < program.object_aliases.size(); ++i)
	{
		const ObjectAlias& alias = program.object_aliases[i];
		if (alias.target >= program.symbols.size())
			throw std::logic_error("invalid PA15 object alias target");
		if (wrote) output << '\n';
		output << "alias object " << alias.object_name << " = @" <<
			program.symbols[alias.target].name << '\n';
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
