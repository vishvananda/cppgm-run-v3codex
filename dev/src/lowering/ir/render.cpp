#include "lowering/ir/render.h"
#include "lowering/ir/model.h"
#include "lowering/support/errors.h"

#include <ostream>
#include <string>

namespace cppgm
{
namespace
{

using namespace lowering::ir;
using lowering::ThrowLoweringInternal;

const char* RuntimeRoleName(Symbol::RuntimeRole role)
{
	switch (role)
	{
	case Symbol::RUNTIME_ROLE_NONE: return "none";
	case Symbol::RUNTIME_ROLE_EH_RESUME: return "eh_resume";
	case Symbol::RUNTIME_ROLE_EH_ALLOCATE_EXCEPTION:
		return "eh_allocate_exception";
	case Symbol::RUNTIME_ROLE_EH_BEGIN_CATCH: return "eh_begin_catch";
	case Symbol::RUNTIME_ROLE_EH_END_CATCH: return "eh_end_catch";
	case Symbol::RUNTIME_ROLE_EH_RETHROW: return "eh_rethrow";
	case Symbol::RUNTIME_ROLE_EH_THROW: return "eh_throw";
	case Symbol::RUNTIME_ROLE_EH_PERSONALITY: return "eh_personality";
	case Symbol::RUNTIME_ROLE_TERMINATE: return "terminate";
	case Symbol::RUNTIME_ROLE_ALLOCATE_MEMORY:
	case Symbol::RUNTIME_ROLE_FREE_MEMORY:
	case Symbol::RUNTIME_ROLE_PURE_VIRTUAL:
	case Symbol::RUNTIME_ROLE_DYNAMIC_CAST:
	case Symbol::RUNTIME_ROLE_BAD_CAST:
	case Symbol::RUNTIME_ROLE_BAD_TYPEID:
	case Symbol::RUNTIME_ROLE_RTTI_CLASS:
	case Symbol::RUNTIME_ROLE_RTTI_SI:
	case Symbol::RUNTIME_ROLE_RTTI_VMI:
	case Symbol::RUNTIME_ROLE_RTTI_DATA: return 0;
	}
	ThrowLoweringInternal("missing PA15 runtime role");
}

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
	case LOW_I128: output << "i128"; return;
	case LOW_F32: output << "f32"; return;
	case LOW_F64: output << "f64"; return;
	case LOW_F80: output << "f80"; return;
	case LOW_PTR: output << "ptr"; return;
	case LOW_OBJECT:
		output << "obj<" << type.width / 8 << 'x' << type.alignment << '>';
		return;
	case LOW_INVALID: break;
	}
	ThrowLoweringInternal("missing PA15 LowIR type");
}

void WriteParameter(std::ostream& output, const lowering::ir::Program& program,
	const Parameter& parameter)
{
	output << '%' << program.strings.get(parameter.name) << " : ";
	WriteType(output, parameter.type);
	if (parameter.reference || parameter.indirect_result ||
		parameter.by_address ||
		parameter.alias != Parameter::ALIAS_DEFAULT)
	{
		output << " [";
		bool separator = false;
		if (parameter.reference)
		{
			output << "pass=by_address";
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
		if (parameter.alias == Parameter::ALIAS_NOALIAS)
		{
			if (separator) output << ", ";
			output << "alias=noalias";
		}
		output << ']';
	}
}

void WriteBoundary(std::ostream& output,
	const lowering::ir::Program& program, const std::vector<Parameter>& parameters,
	const LowType& result,
	bool variadic)
{
	output << '(';
	for (std::size_t i = 0; i < parameters.size(); ++i)
	{
		if (i != 0) output << ", ";
		WriteParameter(output, program, parameters[i]);
	}
	output << ") -> ";
	WriteType(output, result);
	if (variadic) output << " [arity=variadic]";
}

void WriteOperand(std::ostream& output, const Operand& operand,
	const lowering::ir::Program& program, const Function& function)
{
	switch (operand.kind)
	{
	case Operand::TEMP: output << "%t" << operand.id; break;
	case Operand::PARAMETER:
		if (operand.id >= function.parameters.size())
			ThrowLoweringInternal("invalid PA15 parameter reference");
		output << '%' << program.strings.get(
			function.parameters[operand.id].name);
		break;
	case Operand::SLOT:
		if (operand.id >= function.slots.size())
			ThrowLoweringInternal("invalid PA15 slot reference");
		output << '$' << program.strings.get(function.slots[operand.id].name);
		break;
	case Operand::GLOBAL: case Operand::FUNCTION:
		if (operand.id >= program.symbols.size())
			ThrowLoweringInternal("invalid PA15 symbol reference");
		output << '@' << program.strings.get(program.symbols[operand.id].name);
		break;
	case Operand::INTEGER: output << operand.integer_value; break;
	case Operand::FLOATING:
		output << program.strings.get(lowir_model::StringId(operand.id)); break;
	case Operand::NULL_POINTER: output << "nullptr"; break;
	case Operand::NONE: ThrowLoweringInternal("missing PA15 LowIR operand");
	}
}

void ValidateExtraRange(const Instruction& instruction, std::size_t size,
	const char* description)
{
	if (instruction.extra_count == 0)
	{
		if (instruction.extra_first != kNoLowId)
			ThrowLoweringInternal(
				std::string("invalid empty PA15 ") + description);
		return;
	}
	if (instruction.extra_first == kNoLowId || instruction.extra_first > size ||
		instruction.extra_count > size - instruction.extra_first)
		ThrowLoweringInternal(std::string("invalid PA15 ") + description);
}

void WriteInstruction(std::ostream& output, const Instruction& instruction,
	const lowering::ir::Program& program, const Function& function)
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
	case Instruction::ATOMIC_LOAD:
		output << "%t" << instruction.dest << " = atomic_load ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", " << static_cast<unsigned>(instruction.atomic_order);
		break;
	case Instruction::STORE:
		output << "store ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::ATOMIC_STORE:
		output << "atomic_store ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		output << ", " << static_cast<unsigned>(instruction.atomic_order);
		break;
	case Instruction::ATOMIC_EXCHANGE:
		output << "%t" << instruction.dest << " = atomic_exchange ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		output << ", " << static_cast<unsigned>(instruction.atomic_order);
		break;
	case Instruction::COPY_OBJECT:
		if (instruction.type.kind != LOW_OBJECT)
			ThrowLoweringInternal("invalid PA17 copyobj span");
		output << "copyobj " << instruction.type.width / 8 << 'x'
			<< instruction.type.alignment << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::ZERO_OBJECT:
		if (instruction.type.kind != LOW_OBJECT)
			ThrowLoweringInternal("invalid PA26 zeroinit span");
		output << "zeroinit " << instruction.type.width / 8 << 'x'
			<< instruction.type.alignment << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::INDEX:
		output << "%t" << instruction.dest << " = index ";
		WriteType(output, instruction.type);
		if (instruction.projection == INDEX_PROJECTION_ARRAY_ELEMENT)
			output << " [projection=array_element]";
		else if (instruction.projection == INDEX_PROJECTION_FIELD)
			output << " [projection=field]";
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::UNARY:
		output << "%t" << instruction.dest << " = unary "
			<< LowOperationText(instruction.op) << ' ';
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::BINARY:
		output << "%t" << instruction.dest << " = binary "
			<< LowOperationText(instruction.op) << ' ';
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::CMP:
		output << "%t" << instruction.dest << " = cmp "
			<< LowOperationText(instruction.op) << ' ';
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::CONVERT:
		output << "%t" << instruction.dest << " = convert "
			<< LowOperationText(instruction.op) << ' ';
		WriteType(output, instruction.type);
		output << ' ';
		WriteType(output, instruction.source_type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::ATOMIC_ADD_FETCH:
		output << "%t" << instruction.dest << " = atomic_add_fetch ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		output << ", " << static_cast<unsigned>(instruction.atomic_order);
		break;
	case Instruction::ATOMIC_COMPARE_EXCHANGE:
		output << "%t" << instruction.dest << " = atomic_compare_exchange ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		output << ", ";
		WriteOperand(output, instruction.third, program, function);
		output << ", " << static_cast<unsigned>(instruction.atomic_order)
			<< ", " << static_cast<unsigned>(instruction.atomic_failure_order);
		break;
	case Instruction::ATOMIC_THREAD_FENCE:
	case Instruction::ATOMIC_SIGNAL_FENCE:
		output << (instruction.kind == Instruction::ATOMIC_THREAD_FENCE ?
			"atomic_thread_fence " : "atomic_signal_fence ")
			<< static_cast<unsigned>(instruction.atomic_order);
		break;
	case Instruction::STACK_ALLOC:
		output << "%t" << instruction.dest << " = stack_alloc ";
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::VA_START:
		output << "va_start ";
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::VA_ARG:
		output << "%t" << instruction.dest << " = va_arg ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::CALL:
		ValidateExtraRange(instruction, program.call_arguments.size(),
			"call argument range");
		ValidateExtraRange(instruction,
			program.call_argument_references.size(), "call reference range");
		ValidateExtraRange(instruction,
			program.call_argument_object_bytes.size(), "call object range");
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
				if (i + instruction.virtual_base_argument_count >=
					instruction.extra_count)
					output << "%__pvbptr" << i +
						instruction.virtual_base_argument_count -
						instruction.extra_count << " : ";
				else output << "%arg" << i << " : ";
				WriteType(output,
					program.call_arguments[instruction.extra_first + i].type);
				const std::uint8_t passing =
					program.call_argument_references[instruction.extra_first + i];
				const std::size_t object_bytes =
					program.call_argument_object_bytes[
						instruction.extra_first + i];
				if (passing != Instruction::CALL_PASS_VALUE || object_bytes)
				{
					output << " [";
					if (passing == Instruction::CALL_PASS_REFERENCE ||
						passing == Instruction::CALL_PASS_BY_ADDRESS)
						output << "pass=by_address";
					else if (passing == Instruction::CALL_PASS_INDIRECT_RESULT)
						output << "pass=indirect_result";
					if (object_bytes)
					{
						if (passing != Instruction::CALL_PASS_VALUE) output << ", ";
						output << "object_bytes=" << object_bytes;
					}
					output << ']';
				}
			}
			output << ") -> ";
			WriteType(output, instruction.type);
		}
		break;
	case Instruction::EH_TRY:
		if (instruction.target >= function.blocks.size())
			ThrowLoweringInternal("invalid PA16 eh_try target");
		output << "eh_try ^" << program.strings.get(
			function.blocks[instruction.target].label);
		break;
	case Instruction::EH_CLEANUP:
		if (instruction.target == kNoLowId)
		{
			output << "eh_cleanup";
			break;
		}
		if (instruction.target >= function.blocks.size())
			ThrowLoweringInternal("invalid PA16 eh_cleanup target");
		output << "eh_cleanup ^" << program.strings.get(
			function.blocks[instruction.target].label);
		break;
	case Instruction::EH_CATCH:
		output << "eh_catch ";
		WriteOperand(output, instruction.first, program, function);
		output << ", " << instruction.second.integer_value;
		break;
	case Instruction::EH_FILTER:
		ValidateExtraRange(instruction, program.exception_filter_types.size(),
			"exception filter type range");
		output << "eh_filter";
		for (std::size_t i = 0; i < instruction.extra_count; ++i)
		{
			const SymbolId symbol = program.exception_filter_types[
				instruction.extra_first + i];
			if (symbol >= program.symbols.size())
				ThrowLoweringInternal("invalid exception filter RTTI symbol");
			output << (i == 0 ? " " : ", ") << "@" <<
				program.strings.get(program.symbols[symbol].name);
		}
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
			ThrowLoweringInternal("invalid PA15 jump target");
		output << "jump ^" << program.strings.get(
			function.blocks[instruction.target].label);
		break;
	case Instruction::BRANCH:
		if (instruction.target >= function.blocks.size() ||
			instruction.alternate >= function.blocks.size())
			ThrowLoweringInternal("invalid PA15 branch target");
		output << "branch ";
		WriteOperand(output, instruction.first, program, function);
		output << ", ^" << program.strings.get(
			function.blocks[instruction.target].label) << ", ^"
			<< program.strings.get(
				function.blocks[instruction.alternate].label);
		break;
	case Instruction::SWITCH:
		ValidateExtraRange(instruction, program.switch_case_values.size(),
			"switch value range");
		ValidateExtraRange(instruction, program.switch_case_targets.size(),
			"switch target range");
		if (instruction.target >= function.blocks.size())
			ThrowLoweringInternal("invalid PA15 switch default target");
		output << "switch ";
		WriteOperand(output, instruction.first, program, function);
		output << ", ^" << program.strings.get(
			function.blocks[instruction.target].label);
		for (std::size_t i = 0; i < instruction.extra_count; ++i)
		{
			const BlockId case_target =
				program.switch_case_targets[instruction.extra_first + i];
			if (case_target >= function.blocks.size())
				ThrowLoweringInternal("invalid PA15 switch case target");
			output << ", "
				<< program.switch_case_values[instruction.extra_first + i] << ":^"
				<< program.strings.get(function.blocks[case_target].label);
		}
		break;
	case Instruction::RETURN_VALUE:
		output << "return ";
		WriteType(output, instruction.type);
		output << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::RETURN_VOID: output << "return void"; break;
	case Instruction::UNREACHABLE: output << "unreachable"; break;
	}
}

void WriteSymbolMetadata(std::ostream& output, const Symbol& symbol,
	const lowering::ir::Program& program,
	bool entry, bool function, bool initializer = false,
	bool finalizer = false, bool readonly = false)
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
	if (function && symbol.stable_prefix_query)
	{
		if (separator) output << ", ";
		output << "query=stable_prefix";
		separator = true;
	}
	if (function && symbol.runtime_role != Symbol::RUNTIME_ROLE_NONE)
	{
		const char* role = RuntimeRoleName(symbol.runtime_role);
		if (role)
		{
			if (separator) output << ", ";
			output << "role=" << role;
			separator = true;
		}
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
			ThrowLoweringInternal("invalid PA16 TLS wrapper target");
		if (separator) output << ", ";
		output << "tls_for=@" << program.strings.get(
			program.symbols[symbol.tls_for_symbol].name);
		separator = true;
	}
	if (!function && symbol.thread_local_storage)
	{
		if (separator) output << ", ";
		output << "storage=thread_local";
		separator = true;
	}
	else if (!function && readonly)
	{
		if (separator) output << ", ";
		output << "storage=readonly";
		separator = true;
	}
	if (separator) output << ", ";
	output << "binding=" << (symbol.internal_linkage ? "internal" :
		symbol.weak_linkage ? "weak" : "strong");
	if (symbol.object_name.valid())
		output << ", object=" << program.strings.get(symbol.object_name);
	if (symbol.prefer_local_object_binding) output << ", prefer_local=yes";
	if (entry) output << ", keep_alias=yes";
	if (symbol.object_output_root) output << ", object_root=yes";
	if (function && symbol.force_inline) output << ", force_inline=yes";
	if (function && symbol.inline_hint) output << ", inline_hint=yes";
	if (function && symbol.no_inline) output << ", no_inline=yes";
	output << ']';
}

void RenderProgram(const lowering::ir::Program& program, std::ostream& output)
{
	bool wrote = false;
	for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
	{
		const GlobalDeclaration& declaration = program.global_declarations[i];
		const Symbol& symbol = program.symbols[declaration.symbol];
		if (symbol.definition_emitted || !symbol.referenced) continue;
		if (wrote) output << '\n';
		output << "declare global @" << program.strings.get(symbol.name);
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
		output << "declare function @" << program.strings.get(symbol.name);
		WriteBoundary(output, program, declaration.parameters, declaration.result,
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
		output << "global @" << program.strings.get(symbol.name);
		if (global.initializer_kind != Global::STRUCTURED_VALUE)
		{
			output << " : ";
			WriteType(output, global.type);
		}
		WriteSymbolMetadata(output, symbol, program, false, false, false, false,
			global.storage == Global::STORAGE_READONLY);
		output << " = ";
		if (global.initializer_kind == Global::ZERO) output << "zero\n";
		else if (global.initializer_kind == Global::INTEGER_VALUE)
			output << global.initializer << '\n';
		else if (global.initializer_kind == Global::FLOATING_VALUE)
			output << program.strings.get(global.floating_initializer) << '\n';
		else if (global.initializer_kind == Global::ADDRESS_VALUE)
		{
			output << "addr @" << program.strings.get(
				program.symbols[global.address_symbol].name);
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
					output << "ptr addr @" << program.strings.get(
						program.symbols[item.symbol].name);
					if (item.offset > 0) output << " + " << item.offset;
					else if (item.offset < 0) output << " - " << -item.offset;
				}
				else if (item.kind == Global::DataItem::FLOATING_ITEM)
				{
					WriteType(output, item.type);
					output << ' ' << program.strings.get(item.floating_spelling);
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
		output << "function @" << program.strings.get(symbol.name);
		WriteBoundary(output, program, function.parameters, function.result,
			function.variadic);
		WriteSymbolMetadata(output, symbol, program, function.entry, true,
			function.initializer, function.finalizer);
		output << " {\n";
		for (std::size_t s = 0; s < function.slots.size(); ++s)
		{
			output << "  slot $" << program.strings.get(
				function.slots[s].name) << " : ";
			WriteType(output, function.slots[s].type);
			output << '\n';
		}
		if (!function.slots.empty()) output << '\n';
		for (std::size_t order = 0; order < function.block_order.size(); ++order)
		{
			const BlockId b = function.block_order[order];
			if (order != 0) output << '\n';
			output << "  block ^" << program.strings.get(
				function.blocks[b].label) << ":\n";
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
			ThrowLoweringInternal("invalid PA15 object alias target");
		if (wrote) output << '\n';
		output << "alias object " << program.strings.get(alias.object_name) <<
			" = @" << program.strings.get(
				program.symbols[alias.target].name) << '\n';
		wrote = true;
	}
}


}

void lowering::ir::RenderLowIR(const Program& program,
	std::ostream& output)
{
	RenderProgram(program, output);
}

}
