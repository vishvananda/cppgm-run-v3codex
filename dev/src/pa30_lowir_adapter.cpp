#include "pa30_lowir_adapter.h"

#include <cerrno>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace cppgm
{
namespace
{

using namespace pa15_lowir_detail;

std::string At(const std::string& value) { return "@" + value; }
std::string Percent(const std::string& value) { return "%" + value; }
std::string Dollar(const std::string& value) { return "$" + value; }
std::string Temp(std::uint32_t value) { return "%t" + std::to_string(value); }
std::string Label(const std::string& value) { return "^" + value; }

lowir_model::LowType AdaptType(const LowType& type)
{
	using namespace lowir_model;
	switch (type.kind)
	{
	case LOW_VOID: return builtin_lowir_type(LTK_VOID);
	case LOW_I8: return builtin_lowir_type(LTK_I8);
	case LOW_U8: return builtin_lowir_type(LTK_U8);
	case LOW_I16: return builtin_lowir_type(LTK_I16);
	case LOW_U16: return builtin_lowir_type(LTK_U16);
	case LOW_I32: return builtin_lowir_type(LTK_I32);
	case LOW_U32: return builtin_lowir_type(LTK_U32);
	case LOW_I64: return builtin_lowir_type(LTK_I64);
	case LOW_I128: return builtin_lowir_type(LTK_I128);
	case LOW_F32: return builtin_lowir_type(LTK_F32);
	case LOW_F64: return builtin_lowir_type(LTK_F64);
	case LOW_F80: return builtin_lowir_type(LTK_F80);
	case LOW_PTR: return builtin_lowir_type(LTK_PTR);
	case LOW_OBJECT:
	{
		lowir_model::LowType result;
		result.kind = lowir_model::LTK_OBJECT;
		result.bit_width = static_cast<std::size_t>(type.width);
		result.storage_size = static_cast<std::size_t>(type.width / 8);
		result.alignment = type.alignment;
		result.text = "obj<" + std::to_string(result.storage_size) + "x" +
			std::to_string(result.alignment) + ">";
		return result;
	}
	case LOW_INVALID: break;
	}
	throw std::logic_error("invalid typed LowIR type at native boundary");
}

lowir_model::Operand AdaptOperand(const Operand& operand,
	const TypedProgram& program, const Function& function)
{
	lowir_model::Operand result;
	if (operand.type.kind != LOW_INVALID)
		result.literal_type = AdaptType(operand.type);
	switch (operand.kind)
	{
	case Operand::TEMP:
		result.kind = lowir_model::Operand::OP_TEMP;
		result.text = Temp(operand.id);
		break;
	case Operand::PARAMETER:
		if (operand.id >= function.parameters.size())
			throw std::logic_error("invalid typed LowIR parameter operand");
		result.kind = lowir_model::Operand::OP_TEMP;
		result.text = Percent(function.parameters[operand.id].name);
		break;
	case Operand::SLOT:
		if (operand.id >= function.slots.size())
			throw std::logic_error("invalid typed LowIR slot operand");
		result.kind = lowir_model::Operand::OP_SLOT;
		result.text = Dollar(function.slots[operand.id].name);
		break;
	case Operand::GLOBAL:
	case Operand::FUNCTION:
		if (operand.id >= program.symbols.size())
			throw std::logic_error("invalid typed LowIR symbol operand");
		result.kind = lowir_model::Operand::OP_GLOBAL;
		{
			const Symbol& symbol = program.symbols[operand.id];
			result.text = At(symbol.name);
			result.address_binding = symbol.definition_emitted &&
				!symbol.weak_linkage ?
				lowir_model::Operand::ADDRESS_LOCAL :
				lowir_model::Operand::ADDRESS_PREEMPTIBLE;
		}
		break;
	case Operand::INTEGER:
		result.kind = lowir_model::Operand::OP_INTEGER;
		result.int_value = operand.integer_value;
		result.has_int_value = true;
		result.text = std::to_string(operand.integer_value);
		break;
	case Operand::FLOATING:
		result.kind = lowir_model::Operand::OP_FLOAT;
		result.text = program.literals.Get(operand.id);
		errno = 0;
		result.float_value = std::strtold(result.text.c_str(), 0);
		break;
	case Operand::NULL_POINTER:
		result.kind = lowir_model::Operand::OP_INTEGER;
		result.text = "nullptr";
		result.int_value = 0;
		result.has_int_value = true;
		break;
	case Operand::NONE:
		break;
	}
	return result;
}

void AdaptParameterFacts(const Parameter& source,
	lowir_model::Parameter* target)
{
	target->name = Percent(source.name);
	target->type = AdaptType(source.type);
	if (source.reference)
		target->metadata.passing = lowir_model::PPM_REFERENCE;
	else if (source.decay)
		target->metadata.passing = lowir_model::PPM_DECAY;
	else if (source.indirect_result)
		target->metadata.passing = lowir_model::PPM_INDIRECT_RESULT;
	else if (source.by_address)
		target->metadata.passing = lowir_model::PPM_BY_ADDRESS;
	if (source.capture == Parameter::CAPTURE_NOCAPTURE)
		target->metadata.capture = lowir_model::PCM_NOCAPTURE;
	if (source.access == Parameter::ACCESS_READ)
		target->metadata.access = lowir_model::PAM_READ;
	else if (source.access == Parameter::ACCESS_WRITE)
		target->metadata.access = lowir_model::PAM_WRITE;
	else if (source.access == Parameter::ACCESS_READWRITE)
		target->metadata.access = lowir_model::PAM_READWRITE;
	if (source.alias == Parameter::ALIAS_NOALIAS)
		target->metadata.alias = lowir_model::PALM_NOALIAS;
}

std::vector<lowir_model::Parameter> AdaptParameters(
	const std::vector<Parameter>& source)
{
	std::vector<lowir_model::Parameter> result(source.size());
	for (std::size_t i = 0; i < source.size(); ++i)
		AdaptParameterFacts(source[i], &result[i]);
	return result;
}

void AdaptSymbolFacts(const Symbol& source,
	lowir_model::SymbolMetadata* symbol,
	lowir_model::FunctionBoundaryMetadata* boundary)
{
	symbol->linkage = source.c_linkage ? lowir_model::LLM_C :
		lowir_model::LLM_CPP;
	symbol->binding = source.internal_linkage ? lowir_model::SBM_INTERNAL :
		source.weak_linkage ? lowir_model::SBM_WEAK : lowir_model::SBM_STRONG;
	symbol->object_symbol = source.object_name;
	symbol->section_name = source.section_name;
	symbol->object_output_root = source.object_output_root;
	symbol->object_trivial_lifecycle = source.trivial_lifecycle;
	if (boundary)
	{
		boundary->effects = source.effects == Symbol::EFFECTS_READNONE ?
			lowir_model::CFXM_READNONE :
			source.effects == Symbol::EFFECTS_READONLY ?
			lowir_model::CFXM_READONLY :
			source.effects == Symbol::EFFECTS_READWRITE ?
			lowir_model::CFXM_READWRITE : lowir_model::CFXM_DEFAULT;
		if (source.nonthrowing) boundary->unwind = lowir_model::CUM_NO;
		if (source.noreturn) boundary->returns = lowir_model::CRM_NORETURN;
	}
	switch (source.runtime_role)
	{
	case Symbol::RUNTIME_ROLE_NONE: break;
	case Symbol::RUNTIME_ROLE_EH_RESUME:
		symbol->role = lowir_model::SR_EH_RESUME; break;
	case Symbol::RUNTIME_ROLE_EH_ALLOCATE_EXCEPTION:
		symbol->role = lowir_model::SR_EH_ALLOCATE_EXCEPTION; break;
	case Symbol::RUNTIME_ROLE_EH_BEGIN_CATCH:
		symbol->role = lowir_model::SR_EH_BEGIN_CATCH; break;
	case Symbol::RUNTIME_ROLE_EH_END_CATCH:
		symbol->role = lowir_model::SR_EH_END_CATCH; break;
	case Symbol::RUNTIME_ROLE_EH_RETHROW:
		symbol->role = lowir_model::SR_EH_RETHROW; break;
	case Symbol::RUNTIME_ROLE_EH_THROW:
		symbol->role = lowir_model::SR_EH_THROW; break;
	case Symbol::RUNTIME_ROLE_EH_PERSONALITY:
		symbol->role = lowir_model::SR_EH_PERSONALITY; break;
	case Symbol::RUNTIME_ROLE_ALLOCATE_MEMORY:
		symbol->role = lowir_model::SR_ALLOCATE_MEMORY; break;
	case Symbol::RUNTIME_ROLE_FREE_MEMORY:
		symbol->role = lowir_model::SR_FREE_MEMORY; break;
	case Symbol::RUNTIME_ROLE_PURE_VIRTUAL:
		symbol->role = lowir_model::SR_PURE_VIRTUAL; break;
	case Symbol::RUNTIME_ROLE_DYNAMIC_CAST:
		symbol->role = lowir_model::SR_DYNAMIC_CAST; break;
	case Symbol::RUNTIME_ROLE_BAD_CAST:
		symbol->role = lowir_model::SR_BAD_CAST; break;
	case Symbol::RUNTIME_ROLE_BAD_TYPEID:
		symbol->role = lowir_model::SR_BAD_TYPEID; break;
	case Symbol::RUNTIME_ROLE_RTTI_CLASS:
		symbol->role = lowir_model::SR_RTTI_CLASS; break;
	case Symbol::RUNTIME_ROLE_RTTI_SI:
		symbol->role = lowir_model::SR_RTTI_SI; break;
	case Symbol::RUNTIME_ROLE_RTTI_VMI:
		symbol->role = lowir_model::SR_RTTI_VMI; break;
	case Symbol::RUNTIME_ROLE_RTTI_DATA:
		symbol->role = lowir_model::SR_RTTI_DATA; break;
	}
}

lowir_model::Instruction AdaptInstruction(const Instruction& source,
	const TypedProgram& program, const Function& function)
{
	lowir_model::Instruction target;
	if (source.dest != kNoLowId) target.dest = Temp(source.dest);
	if (source.type.kind != LOW_INVALID) target.type = AdaptType(source.type);
	if (source.source_type.kind != LOW_INVALID)
		target.source_type = AdaptType(source.source_type);
	if (source.op != LOW_OP_NONE) target.op = LowOperationText(source.op);
	target.first = AdaptOperand(source.first, program, function);
	target.second = AdaptOperand(source.second, program, function);
	target.third = AdaptOperand(source.third, program, function);
	switch (source.projection)
	{
	case INDEX_PROJECTION_NONE: break;
	case INDEX_PROJECTION_ARRAY_ELEMENT:
		target.index_projection = lowir_model::IPK_ARRAY_ELEMENT; break;
	case INDEX_PROJECTION_FIELD:
		target.index_projection = lowir_model::IPK_FIELD; break;
	case INDEX_PROJECTION_BASE_SUBOBJECT:
		target.index_projection = lowir_model::IPK_BASE_SUBOBJECT; break;
	}
	switch (source.kind)
	{
	case Instruction::CONST: target.kind = lowir_model::Instruction::IK_CONST; break;
	case Instruction::COPY: target.kind = lowir_model::Instruction::IK_COPY; break;
	case Instruction::ADDR: target.kind = lowir_model::Instruction::IK_ADDR; break;
	case Instruction::LOAD: target.kind = lowir_model::Instruction::IK_LOAD; break;
	case Instruction::ATOMIC_LOAD:
		target.kind = lowir_model::Instruction::IK_ATOMIC_LOAD;
		target.args.push_back(AdaptOperand(Operand(source.atomic_order, LowI32()),
			program, function));
		break;
	case Instruction::STORE: target.kind = lowir_model::Instruction::IK_STORE; break;
	case Instruction::ATOMIC_STORE:
		target.kind = lowir_model::Instruction::IK_ATOMIC_STORE;
		target.args.push_back(AdaptOperand(Operand(source.atomic_order, LowI32()),
			program, function));
		break;
	case Instruction::ATOMIC_EXCHANGE:
		target.kind = lowir_model::Instruction::IK_ATOMIC_EXCHANGE;
		target.args.push_back(AdaptOperand(Operand(source.atomic_order, LowI32()),
			program, function));
		break;
	case Instruction::COPY_OBJECT:
		target.kind = lowir_model::Instruction::IK_COPYOBJ;
		target.byte_count = static_cast<std::size_t>(source.type.width / 8);
		target.byte_alignment = source.type.alignment;
		break;
	case Instruction::ZERO_OBJECT:
		target.kind = lowir_model::Instruction::IK_ZEROINIT;
		target.byte_count = static_cast<std::size_t>(source.type.width / 8);
		target.byte_alignment = source.type.alignment;
		break;
	case Instruction::INDEX: target.kind = lowir_model::Instruction::IK_INDEX; break;
	case Instruction::UNARY: target.kind = lowir_model::Instruction::IK_UNARY; break;
	case Instruction::BINARY: target.kind = lowir_model::Instruction::IK_BINARY; break;
	case Instruction::CMP: target.kind = lowir_model::Instruction::IK_CMP; break;
	case Instruction::CONVERT: target.kind = lowir_model::Instruction::IK_CONVERT; break;
	case Instruction::ATOMIC_ADD_FETCH:
		target.kind = lowir_model::Instruction::IK_ATOMIC_ADD_FETCH;
		target.args.push_back(AdaptOperand(Operand(source.atomic_order, LowI32()),
			program, function));
		break;
	case Instruction::ATOMIC_COMPARE_EXCHANGE:
		target.kind = lowir_model::Instruction::IK_ATOMIC_COMPARE_EXCHANGE;
		target.args.push_back(AdaptOperand(Operand(source.atomic_order, LowI32()),
			program, function));
		target.args.push_back(AdaptOperand(
			Operand(source.atomic_failure_order, LowI32()), program, function));
		break;
	case Instruction::ATOMIC_THREAD_FENCE:
	case Instruction::ATOMIC_SIGNAL_FENCE:
		target.kind = source.kind == Instruction::ATOMIC_THREAD_FENCE ?
			lowir_model::Instruction::IK_ATOMIC_THREAD_FENCE :
			lowir_model::Instruction::IK_ATOMIC_SIGNAL_FENCE;
		target.first = AdaptOperand(Operand(source.atomic_order, LowI32()),
			program, function);
		break;
	case Instruction::STACK_ALLOC:
		target.kind = lowir_model::Instruction::IK_STACK_ALLOC; break;
	case Instruction::VA_START:
		target.kind = lowir_model::Instruction::IK_VA_START; break;
	case Instruction::VA_ARG:
		target.kind = lowir_model::Instruction::IK_VA_ARG; break;
	case Instruction::CALL:
		target.kind = lowir_model::Instruction::IK_CALL;
		target.call_returns_void = source.dest == kNoLowId;
		if (!source.indirect &&
			(source.first.kind == Operand::FUNCTION ||
			 source.first.kind == Operand::GLOBAL))
		{
			if (source.first.id >= program.symbols.size())
				throw std::logic_error("invalid typed LowIR call target");
			lowir_model::SymbolMetadata ignored_symbol;
			AdaptSymbolFacts(program.symbols[source.first.id], &ignored_symbol,
				&target.call_boundary);
		}
		if (source.extra_count)
		{
			if (source.extra_first == kNoLowId ||
				source.extra_first > program.call_arguments.size() ||
				source.extra_count > program.call_arguments.size() - source.extra_first)
				throw std::logic_error("invalid typed LowIR call arguments");
			for (std::size_t i = 0; i < source.extra_count; ++i)
				target.args.push_back(AdaptOperand(
					program.call_arguments[source.extra_first + i], program, function));
		}
		if (source.indirect)
		{
			target.has_call_signature = true;
			target.call_return_type = AdaptType(source.type);
			target.call_params.resize(source.extra_count);
			for (std::size_t i = 0; i < source.extra_count; ++i)
			{
				lowir_model::Parameter& parameter = target.call_params[i];
				parameter.name = i + source.virtual_base_argument_count >=
					source.extra_count ?
					Percent("__pvbptr" + std::to_string(i +
						source.virtual_base_argument_count - source.extra_count)) :
					Percent("arg" + std::to_string(i));
				parameter.type = target.args[i].literal_type;
				if (source.extra_first + i <
					program.call_argument_references.size())
				{
					const std::uint8_t passing = program.call_argument_references[
						source.extra_first + i];
					if (passing == Instruction::CALL_PASS_REFERENCE)
						parameter.metadata.passing = lowir_model::PPM_REFERENCE;
					else if (passing == Instruction::CALL_PASS_BY_ADDRESS)
						parameter.metadata.passing = lowir_model::PPM_BY_ADDRESS;
					else if (passing == Instruction::CALL_PASS_INDIRECT_RESULT)
						parameter.metadata.passing = lowir_model::PPM_INDIRECT_RESULT;
				}
			}
		}
		break;
	case Instruction::EH_TRY:
		target.kind = lowir_model::Instruction::IK_EH_TRY; break;
	case Instruction::EH_CLEANUP:
		target.kind = source.target == kNoLowId ?
			lowir_model::Instruction::IK_EH_CLEANUP_CLAUSE :
			lowir_model::Instruction::IK_EH_CLEANUP;
		break;
	case Instruction::EH_CATCH:
		target.kind = lowir_model::Instruction::IK_EH_CATCH;
		target.has_eh_selector = true;
		target.eh_selector = source.second.integer_value;
		break;
	case Instruction::EH_FILTER:
		target.kind = lowir_model::Instruction::IK_EH_FILTER;
		target.has_eh_selector = true;
		target.eh_selector = source.first.integer_value;
		if (source.extra_first == kNoLowId ||
			source.extra_first > program.exception_filter_types.size() ||
			source.extra_count > program.exception_filter_types.size() -
				source.extra_first)
			throw std::logic_error("invalid typed LowIR exception filter types");
		for (std::size_t i = 0; i < source.extra_count; ++i)
		{
			const SymbolId symbol = program.exception_filter_types[
				source.extra_first + i];
			if (symbol >= program.symbols.size())
				throw std::logic_error("invalid exception filter RTTI symbol");
			lowir_model::Operand type;
			type.kind = lowir_model::Operand::OP_GLOBAL;
			type.text = At(program.symbols[symbol].name);
			target.args.push_back(type);
		}
		break;
	case Instruction::EH_CATCH_ALL:
		target.kind = lowir_model::Instruction::IK_EH_CATCH_ALL;
		target.has_eh_selector = true;
		target.eh_selector = source.first.integer_value;
		break;
	case Instruction::EH_END: target.kind = lowir_model::Instruction::IK_EH_END; break;
	case Instruction::EXCEPTION:
		target.kind = lowir_model::Instruction::IK_EXCEPTION; break;
	case Instruction::EXCEPTION_SELECTOR:
		target.kind = lowir_model::Instruction::IK_EXCEPTION_SELECTOR; break;
	case Instruction::RESUME: target.kind = lowir_model::Instruction::IK_RESUME; break;
	case Instruction::JUMP:
		target.kind = lowir_model::Instruction::IK_JUMP;
		if (source.target >= function.blocks.size())
			throw std::logic_error("invalid typed LowIR jump target");
		target.first.kind = lowir_model::Operand::OP_LABEL;
		target.first.text = Label(function.blocks[source.target].label);
		break;
	case Instruction::BRANCH:
		target.kind = lowir_model::Instruction::IK_BRANCH;
		if (source.target >= function.blocks.size() ||
			source.alternate >= function.blocks.size())
			throw std::logic_error("invalid typed LowIR branch target");
		target.second.kind = lowir_model::Operand::OP_LABEL;
		target.second.text = Label(function.blocks[source.target].label);
		target.third.kind = lowir_model::Operand::OP_LABEL;
		target.third.text = Label(function.blocks[source.alternate].label);
		break;
	case Instruction::SWITCH:
		target.kind = lowir_model::Instruction::IK_SWITCH;
		if (source.target >= function.blocks.size())
			throw std::logic_error("invalid typed LowIR switch target");
		target.second.kind = lowir_model::Operand::OP_LABEL;
		target.second.text = Label(function.blocks[source.target].label);
		if (source.extra_count && (source.extra_first == kNoLowId ||
			source.extra_first > program.switch_case_values.size() ||
			source.extra_count > program.switch_case_values.size() -
				source.extra_first ||
			source.extra_first > program.switch_case_targets.size() ||
			source.extra_count > program.switch_case_targets.size() -
				source.extra_first))
			throw std::logic_error("invalid typed LowIR switch cases");
		for (std::size_t i = 0; i < source.extra_count; ++i)
		{
			lowir_model::Operand value;
			value.kind = lowir_model::Operand::OP_INTEGER;
			value.int_value = program.switch_case_values[source.extra_first + i];
			value.has_int_value = true;
			value.text = std::to_string(value.int_value);
			target.args.push_back(value);
			const BlockId block =
				program.switch_case_targets[source.extra_first + i];
			if (block >= function.blocks.size())
				throw std::logic_error("invalid typed LowIR switch case target");
			lowir_model::Operand label;
			label.kind = lowir_model::Operand::OP_LABEL;
			label.text = Label(function.blocks[block].label);
			target.args.push_back(label);
		}
		break;
	case Instruction::RETURN_VALUE:
		target.kind = lowir_model::Instruction::IK_RETURN; break;
	case Instruction::RETURN_VOID:
		target.kind = lowir_model::Instruction::IK_RETURN;
		target.type = lowir_model::builtin_lowir_type(lowir_model::LTK_VOID);
		break;
	}
	if (source.kind == Instruction::EH_TRY ||
		source.kind == Instruction::EH_CLEANUP)
	{
		if (source.target != kNoLowId)
		{
			if (source.target >= function.blocks.size())
				throw std::logic_error("invalid typed LowIR EH target");
			target.first.kind = lowir_model::Operand::OP_LABEL;
			target.first.text = Label(function.blocks[source.target].label);
		}
	}
	return target;
}

void AppendExport(const Symbol& source, ir_model::ExportedSymbol* target)
{
	target->internal_symbol = At(source.name);
	target->object_symbol = source.object_name;
	target->keep_internal_alias = false;
	target->prefer_local_object_binding = source.prefer_local_object_binding;
	target->linkage = source.internal_linkage ? ir_model::SL_INTERNAL :
		source.weak_linkage ? ir_model::SL_WEAK : ir_model::SL_EXTERNAL;
}

}

lowir_model::LowirProgram AdaptTypedLowIRForNative(
	const TypedProgram& source)
{
	lowir_model::LowirProgram target;
	target.global_declarations.reserve(source.global_declarations.size());
	for (std::size_t i = 0; i < source.global_declarations.size(); ++i)
	{
		const GlobalDeclaration& item = source.global_declarations[i];
		const Symbol& symbol = source.symbols[item.symbol];
		lowir_model::GlobalDeclaration result;
		result.name = At(symbol.name);
		result.has_type = item.typed;
		if (item.typed) result.type = AdaptType(item.type);
		if (symbol.thread_local_storage)
			result.storage = lowir_model::GSM_THREAD_LOCAL;
		AdaptSymbolFacts(symbol, &result.metadata, 0);
		target.global_declarations.push_back(result);
	}
	target.function_declarations.reserve(source.declarations.size());
	for (std::size_t i = 0; i < source.declarations.size(); ++i)
	{
		const FunctionDeclaration& item = source.declarations[i];
		const Symbol& symbol = source.symbols[item.symbol];
		lowir_model::FunctionDeclaration result;
		result.name = At(symbol.name);
		result.params = AdaptParameters(item.parameters);
		result.return_type = AdaptType(item.result);
		result.boundary.arity = item.variadic ? lowir_model::CAM_VARIADIC :
			lowir_model::CAM_FIXED;
		AdaptSymbolFacts(symbol, &result.metadata, &result.boundary);
		if (symbol.tls_for_symbol != kNoLowId)
			result.metadata.tls_for_symbol =
				At(source.symbols[symbol.tls_for_symbol].name);
		target.function_declarations.push_back(result);
	}
	target.globals.reserve(source.globals.size());
	for (std::size_t i = 0; i < source.globals.size(); ++i)
	{
		const Global& item = source.globals[i];
		const Symbol& symbol = source.symbols[item.symbol];
		lowir_model::GlobalDefinition result;
		result.name = At(symbol.name);
		if (item.type.kind != LOW_INVALID) result.type = AdaptType(item.type);
		if (symbol.thread_local_storage)
			result.storage = lowir_model::GSM_THREAD_LOCAL;
		AdaptSymbolFacts(symbol, &result.metadata, 0);
		if (item.initializer_kind == Global::STRUCTURED_VALUE)
		{
			result.structured = true;
			for (std::size_t j = 0; j < item.items.size(); ++j)
			{
				const Global::DataItem& value = item.items[j];
				lowir_model::GlobalDefinition::DataItem data;
				if (value.kind == Global::DataItem::ZERO_ITEM)
				{
					data.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ZERO;
					data.zero_bytes = value.zero_bytes;
				}
				else if (value.kind == Global::DataItem::ADDRESS_ITEM)
				{
					data.kind = lowir_model::GlobalDefinition::DataItem::ITEM_ADDR;
					data.type = lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
					data.symbol = At(source.symbols[value.symbol].name);
					data.addr_addend = value.offset;
				}
				else
				{
					data.kind = lowir_model::GlobalDefinition::DataItem::ITEM_INTEGER;
					data.type = AdaptType(value.type);
					data.literal_operand.literal_type = data.type;
					if (value.kind == Global::DataItem::FLOATING_ITEM)
					{
						data.literal_operand.kind = lowir_model::Operand::OP_FLOAT;
						data.literal_operand.text =
							source.literals.Get(value.floating_spelling);
					}
					else
					{
						data.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
						data.literal_operand.int_value = value.integer_value;
						data.literal_operand.has_int_value = true;
						data.literal_operand.text = std::to_string(value.integer_value);
					}
				}
				result.data_items.push_back(data);
			}
		}
		else if (item.initializer_kind == Global::ADDRESS_VALUE)
		{
			result.init_kind = lowir_model::GlobalDefinition::INIT_ADDR;
			result.init_operand.kind = lowir_model::Operand::OP_GLOBAL;
			result.init_operand.text = At(source.symbols[item.address_symbol].name);
			result.addr_addend = item.address_offset;
		}
		else if (item.initializer_kind == Global::ZERO)
			result.init_kind = lowir_model::GlobalDefinition::INIT_ZERO;
		else
		{
			result.init_kind = lowir_model::GlobalDefinition::INIT_INTEGER;
			if (item.initializer_kind == Global::FLOATING_VALUE)
			{
				result.init_operand.kind = lowir_model::Operand::OP_FLOAT;
				result.init_operand.text = source.literals.Get(item.floating_initializer);
			}
			else
			{
				result.init_operand.kind = lowir_model::Operand::OP_INTEGER;
				result.init_operand.int_value = item.initializer;
				result.init_operand.has_int_value = true;
				result.init_operand.text = std::to_string(item.initializer);
			}
		}
		target.globals.push_back(result);
	}
	target.functions.reserve(source.functions.size());
	for (std::size_t i = 0; i < source.functions.size(); ++i)
	{
		const Function& item = source.functions[i];
		const Symbol& symbol = source.symbols[item.symbol];
		lowir_model::Function result;
		result.name = At(symbol.name);
		result.params = AdaptParameters(item.parameters);
		result.return_type = AdaptType(item.result);
		result.boundary.arity = item.variadic ? lowir_model::CAM_VARIADIC :
			lowir_model::CAM_FIXED;
		AdaptSymbolFacts(symbol, &result.metadata, &result.boundary);
		if (item.entry)
		{
			result.metadata.role = lowir_model::SR_ENTRY;
			result.metadata.keep_internal_alias = true;
		}
		else if (item.initializer) result.metadata.role = lowir_model::SR_INIT;
		else if (item.finalizer) result.metadata.role = lowir_model::SR_FINI;
		if (symbol.tls_for_symbol != kNoLowId)
			result.metadata.tls_for_symbol =
				At(source.symbols[symbol.tls_for_symbol].name);
		for (std::size_t j = 0; j < item.slots.size(); ++j)
			result.slots.push_back(std::make_pair(
				Dollar(item.slots[j].name), AdaptType(item.slots[j].type)));
		result.blocks.reserve(item.block_order.size());
		for (std::size_t order = 0; order < item.block_order.size(); ++order)
		{
			const BlockId block_id = item.block_order[order];
			if (block_id >= item.blocks.size())
				throw std::logic_error("invalid typed LowIR block order");
			const Block& block = item.blocks[block_id];
			lowir_model::Block lowered;
			lowered.label = Label(block.label);
			lowered.instructions.reserve(block.instructions.size());
			for (std::size_t j = 0; j < block.instructions.size(); ++j)
				lowered.instructions.push_back(AdaptInstruction(
					block.instructions[j], source, item));
			result.blocks.push_back(lowered);
		}
		target.functions.push_back(result);
	}
	for (std::size_t i = 0; i < source.object_aliases.size(); ++i)
	{
		lowir_model::ObjectAlias alias;
		alias.object_symbol = source.object_aliases[i].object_name;
		const Symbol& alias_target =
			source.symbols[source.object_aliases[i].target];
		alias.target = At(alias_target.name);
		target.object_aliases.push_back(alias);
		// An ABI alias is an additional exported spelling of the same root, so
		// carry the target's linkage with it across the typed-LowIR boundary.
		if (source.host_object_emission)
		{
			ir_model::ExportedSymbol exported_alias;
			AppendExport(alias_target, &exported_alias);
			exported_alias.object_symbol = alias.object_symbol;
			target.exported_symbols.push_back(exported_alias);
		}
	}
	for (std::size_t i = 0; i < source.symbols.size(); ++i)
	{
		ir_model::ExportedSymbol symbol;
		AppendExport(source.symbols[i], &symbol);
		target.exported_symbols.push_back(symbol);
	}
	return target;
}

}
