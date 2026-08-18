#include "pa30_lowir_adapter.h"

#include "decimal_spelling.h"

#include <cerrno>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace cppgm
{
namespace
{

using namespace pa15_lowir_detail;

struct AdapterTelemetry
{
	AdapterTelemetry(lowir_model::LowirPreparationStats* output_value,
		bool preserve_literal_spellings)
		: output(output_value), preserve_literals(preserve_literal_spellings) {}

	std::string Prefix(char prefix, const std::string& value)
	{
		std::string result;
		result.reserve(value.size() + 1);
		result.push_back(prefix);
		result.append(value);
		if (output)
		{
			++output->adapter_prefix_renders;
			output->adapter_prefix_bytes += result.size();
		}
		return result;
	}

	std::string At(const std::string& value) { return Prefix('@', value); }
	std::string Percent(const std::string& value) { return Prefix('%', value); }
	std::string Dollar(const std::string& value) { return Prefix('$', value); }
	std::string Label(const std::string& value) { return Prefix('^', value); }

	std::string IntegerText(std::int64_t low, std::uint64_t high,
		const LowType& type)
	{
		std::string text;
		if (type.kind != LOW_I128) text = std::to_string(low);
		else
		{
			std::ostringstream result;
			result << "0x" << std::hex << std::setfill('0') << std::setw(16)
				<< high << std::setw(16) << static_cast<std::uint64_t>(low);
			text = result.str();
		}
		if (output)
		{
			++output->adapter_integer_renders;
			output->adapter_integer_bytes += text.size();
		}
		return text;
	}

	lowir_model::StringId Intern(lowir_model::StringPool* strings,
		const std::string& text, bool literal = false)
	{
		if (output && literal) ++output->adapter_literal_materializations;
		return strings->intern(text, output ? &pool : 0);
	}

	lowir_model::StringId Literal(lowir_model::StringPool* strings,
		const std::string& text)
	{
		return preserve_literals ? Intern(strings, text, true) :
			lowir_model::StringId();
	}

	lowir_model::StringId IntegerLiteral(lowir_model::StringPool* strings,
		std::int64_t low, std::uint64_t high, const LowType& type)
	{
		return preserve_literals ? Intern(strings, IntegerText(low, high, type), true) :
			lowir_model::StringId();
	}

	void Finish(const lowir_model::LowirProgram& program)
	{
		if (!output) return;
		output->adapter_string_pool = pool;
		output->lowir_string_entries = program.strings.size();
		output->lowir_spelling_bytes = program.strings.spelling_bytes();
		output->lowir_string_storage_bytes = program.strings.storage_bytes();
		output->lowir_model_storage_bytes =
			lowir_model::lowir_program_storage_bytes(program);
	}

	lowir_model::LowirPreparationStats* output;
	bool preserve_literals;
	lowir_model::StringPoolStats pool;
};

void CountTypedName(const std::string& name, AdapterTelemetry* telemetry)
{
	if (!telemetry->output || name.empty()) return;
	++telemetry->output->typed_name_entries;
	telemetry->output->typed_name_bytes += name.size();
}

void CountTypedNames(const TypedProgram& program, AdapterTelemetry* telemetry)
{
	if (!telemetry->output) return;
	for (std::size_t i = 0; i < program.symbols.size(); ++i)
	{
		CountTypedName(program.symbols[i].name, telemetry);
		CountTypedName(program.symbols[i].object_name, telemetry);
		CountTypedName(program.symbols[i].section_name, telemetry);
	}
	for (std::size_t i = 0; i < program.object_aliases.size(); ++i)
		CountTypedName(program.object_aliases[i].object_name, telemetry);
	for (std::size_t i = 0; i < program.declarations.size(); ++i)
		for (std::size_t p = 0; p < program.declarations[i].parameters.size(); ++p)
			CountTypedName(program.declarations[i].parameters[p].name, telemetry);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
	{
		const Function& function = program.functions[i];
		for (std::size_t p = 0; p < function.parameters.size(); ++p)
			CountTypedName(function.parameters[p].name, telemetry);
		for (std::size_t s = 0; s < function.slots.size(); ++s)
			CountTypedName(function.slots[s].name, telemetry);
		for (std::size_t b = 0; b < function.blocks.size(); ++b)
			CountTypedName(function.blocks[b].label, telemetry);
	}
}

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
		result.storage_size = static_cast<std::size_t>(type.width / 8);
		result.alignment = type.alignment;
		return result;
	}
	case LOW_INVALID: break;
	}
	throw std::logic_error("invalid typed LowIR type at native boundary");
}

struct AdaptedValues
{
	std::vector<lowir_model::ValueId> parameters;
	std::vector<lowir_model::ValueId> temporaries;
};

lowir_model::Operand AdaptOperand(const Operand& operand,
	const TypedProgram& program, const Function& function,
	const AdaptedValues& values, lowir_model::StringPool* literals,
	AdapterTelemetry* telemetry)
{
	lowir_model::Operand result;
	if (operand.type.kind != LOW_INVALID)
		result.literal_type = AdaptType(operand.type);
	switch (operand.kind)
	{
	case Operand::TEMP:
		if (operand.id >= values.temporaries.size() ||
			!values.temporaries[operand.id].valid())
			throw std::logic_error("invalid typed LowIR temporary operand");
		result.kind = lowir_model::Operand::OP_TEMP;
		result.value = values.temporaries[operand.id];
		break;
	case Operand::PARAMETER:
		if (operand.id >= values.parameters.size())
			throw std::logic_error("invalid typed LowIR parameter operand");
		result.kind = lowir_model::Operand::OP_TEMP;
		result.value = values.parameters[operand.id];
		break;
	case Operand::SLOT:
		if (operand.id >= function.slots.size())
			throw std::logic_error("invalid typed LowIR slot operand");
		result.kind = lowir_model::Operand::OP_SLOT;
		result.slot = lowir_model::SlotId(operand.id);
		break;
	case Operand::GLOBAL:
	case Operand::FUNCTION:
		if (operand.id >= program.symbols.size())
			throw std::logic_error("invalid typed LowIR symbol operand");
		result.kind = lowir_model::Operand::OP_GLOBAL;
		{
			const Symbol& symbol = program.symbols[operand.id];
			result.symbol = lowir_model::SymbolId(operand.id);
			result.address_binding = symbol.definition_emitted &&
				!symbol.weak_linkage ?
				lowir_model::Operand::ADDRESS_LOCAL :
				lowir_model::Operand::ADDRESS_PREEMPTIBLE;
		}
		break;
	case Operand::INTEGER:
		result.kind = lowir_model::Operand::OP_INTEGER;
		result.int_value = operand.integer_value;
		result.int_high = operand.integer_high;
		result.has_int_value = true;
		result.literal = telemetry->IntegerLiteral(
			literals, operand.integer_value, operand.integer_high, operand.type);
		result.has_spelling = result.literal.valid();
		break;
	case Operand::FLOATING:
		result.kind = lowir_model::Operand::OP_FLOAT;
		if (result.literal_type.kind == lowir_model::LTK_INVALID)
			result.literal_type = lowir_model::lowir_floating_literal_type(
				program.literals.Get(operand.id));
		result.literal = telemetry->Literal(
			literals, program.literals.Get(operand.id));
		result.has_spelling = result.literal.valid();
		result.has_float_bits = lowir_model::parse_lowir_floating_literal_bits(
			program.literals.Get(operand.id), result.literal_type,
			&result.literal_low, &result.literal_high);
		break;
	case Operand::NULL_POINTER:
		result.kind = lowir_model::Operand::OP_INTEGER;
		result.literal = telemetry->Literal(literals, "nullptr");
		result.has_spelling = result.literal.valid();
		result.int_value = 0;
		result.int_high = 0;
		result.has_int_value = true;
		break;
	case Operand::NONE:
		break;
	}
	return result;
}

void AdaptParameterFacts(const Parameter& source,
	lowir_model::StringPool* strings, lowir_model::Parameter* target,
	AdapterTelemetry* telemetry)
{
	target->name = telemetry->Intern(strings, telemetry->Percent(source.name));
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
	const std::vector<Parameter>& source, lowir_model::StringPool* strings,
	AdapterTelemetry* telemetry)
{
	std::vector<lowir_model::Parameter> result(source.size());
	for (std::size_t i = 0; i < source.size(); ++i)
		AdaptParameterFacts(source[i], strings, &result[i], telemetry);
	return result;
}

lowir_model::LowType AdaptResultType(const Instruction& instruction)
{
	if (instruction.kind == Instruction::ADDR ||
		instruction.kind == Instruction::INDEX)
		return lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
	if (instruction.kind == Instruction::CMP ||
		instruction.kind == Instruction::ATOMIC_COMPARE_EXCHANGE ||
		instruction.kind == Instruction::EXCEPTION_SELECTOR)
		return lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
	return AdaptType(instruction.type);
}

AdaptedValues PrepareValues(const Function& source,
	const lowir_model::StringPool& strings, lowir_model::Function* target)
{
	AdaptedValues result;
	result.parameters.resize(target->params.size());
	for (std::size_t i = 0; i < target->params.size(); ++i)
	{
		result.parameters[i] = lowir_model::append_lowir_value(
			*target, target->params[i].name, target->params[i].type);
		target->params[i].value = result.parameters[i];
	}
	for (std::size_t order = 0; order < source.block_order.size(); ++order)
	{
		const BlockId block = source.block_order[order];
		if (block >= source.blocks.size())
			throw std::logic_error("invalid typed LowIR block order");
		for (std::size_t i = 0; i < source.blocks[block].instructions.size(); ++i)
		{
			const Instruction& instruction = source.blocks[block].instructions[i];
			if (instruction.dest == kNoLowId) continue;
			if (result.temporaries.size() <= instruction.dest)
				result.temporaries.resize(
					static_cast<std::size_t>(instruction.dest) + 1);
			if (result.temporaries[instruction.dest].valid())
				throw std::logic_error("duplicate typed LowIR result identity");
			result.temporaries[instruction.dest] =
				lowir_model::append_lowir_generated_value(
					*target, instruction.dest, AdaptResultType(instruction));
		}
	}
	return result;
}

void AdaptBoundaryFacts(const Symbol& source,
	lowir_model::FunctionBoundaryMetadata* boundary)
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

void AdaptSymbolFacts(const Symbol& source,
	lowir_model::StringPool* strings,
	lowir_model::SymbolMetadata* symbol,
	lowir_model::FunctionBoundaryMetadata* boundary,
	AdapterTelemetry* telemetry)
{
	symbol->linkage = source.c_linkage ? lowir_model::LLM_C :
		lowir_model::LLM_CPP;
	symbol->binding = source.internal_linkage ? lowir_model::SBM_INTERNAL :
		source.weak_linkage ? lowir_model::SBM_WEAK : lowir_model::SBM_STRONG;
	if (!source.object_name.empty())
		symbol->object_symbol = telemetry->Intern(strings, source.object_name);
	if (!source.section_name.empty())
		symbol->section_name = telemetry->Intern(strings, source.section_name);
	symbol->keep_internal_alias = false;
	symbol->prefer_local_object_binding = source.prefer_local_object_binding;
	symbol->object_output_root = source.object_output_root;
	symbol->object_trivial_lifecycle = source.trivial_lifecycle;
	symbol->force_inline = source.force_inline;
	symbol->no_inline = source.no_inline;
	if (boundary) AdaptBoundaryFacts(source, boundary);
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

void AdaptProjection(IndexProjection source, lowir_model::Instruction* target)
{
	switch (source)
	{
	case INDEX_PROJECTION_NONE: break;
	case INDEX_PROJECTION_ARRAY_ELEMENT:
		target->index_projection = lowir_model::IPK_ARRAY_ELEMENT; break;
	case INDEX_PROJECTION_FIELD:
		target->index_projection = lowir_model::IPK_FIELD; break;
	case INDEX_PROJECTION_BASE_SUBOBJECT:
		target->index_projection = lowir_model::IPK_BASE_SUBOBJECT; break;
	}
}

lowir_model::LowOperation AdaptOperation(LowOperation source)
{
	static const lowir_model::LowOperation::Kind operations[] = {
		lowir_model::LowOperation::LOP_NONE,
		lowir_model::LowOperation::LOP_NEG,
		lowir_model::LowOperation::LOP_BITNOT,
		lowir_model::LowOperation::LOP_BSWAP,
		lowir_model::LowOperation::LOP_ADD,
		lowir_model::LowOperation::LOP_SUB,
		lowir_model::LowOperation::LOP_MUL,
		lowir_model::LowOperation::LOP_DIV,
		lowir_model::LowOperation::LOP_UDIV,
		lowir_model::LowOperation::LOP_MOD,
		lowir_model::LowOperation::LOP_UMOD,
		lowir_model::LowOperation::LOP_AND,
		lowir_model::LowOperation::LOP_OR,
		lowir_model::LowOperation::LOP_XOR,
		lowir_model::LowOperation::LOP_SHL,
		lowir_model::LowOperation::LOP_SHR,
		lowir_model::LowOperation::LOP_USHR,
		lowir_model::LowOperation::LOP_EQ,
		lowir_model::LowOperation::LOP_NE,
		lowir_model::LowOperation::LOP_LT,
		lowir_model::LowOperation::LOP_ULT,
		lowir_model::LowOperation::LOP_LE,
		lowir_model::LowOperation::LOP_ULE,
		lowir_model::LowOperation::LOP_GT,
		lowir_model::LowOperation::LOP_UGT,
		lowir_model::LowOperation::LOP_GE,
		lowir_model::LowOperation::LOP_UGE,
		lowir_model::LowOperation::LOP_TRUNC,
		lowir_model::LowOperation::LOP_SEXT,
		lowir_model::LowOperation::LOP_ZEXT,
		lowir_model::LowOperation::LOP_SITOFP,
		lowir_model::LowOperation::LOP_UITOFP,
		lowir_model::LowOperation::LOP_FPTOSI,
		lowir_model::LowOperation::LOP_FPTOUI,
		lowir_model::LowOperation::LOP_FPTRUNC,
		lowir_model::LowOperation::LOP_FPEXT,
		lowir_model::LowOperation::LOP_DECAY
	};
	static_assert(sizeof(operations) / sizeof(operations[0]) ==
		static_cast<std::size_t>(LOW_OP_DECAY) + 1,
		"typed and compact LowIR operation tables must stay synchronized");
	const std::size_t index = static_cast<std::size_t>(source);
	if (index >= sizeof(operations) / sizeof(operations[0]))
		throw std::logic_error("invalid typed LowIR operation");
	return operations[index];
}

lowir_model::Instruction AdaptInstruction(const Instruction& source,
	const TypedProgram& program, const Function& function,
	const AdaptedValues& values, lowir_model::StringPool* literals,
	AdapterTelemetry* telemetry)
{
	lowir_model::Instruction target;
	if (source.dest != kNoLowId)
	{
		if (source.dest >= values.temporaries.size() ||
			!values.temporaries[source.dest].valid())
			throw std::logic_error("invalid typed LowIR result identity");
		target.dest = values.temporaries[source.dest];
	}
	if (source.type.kind != LOW_INVALID) target.type = AdaptType(source.type);
	if (source.source_type.kind != LOW_INVALID)
		target.source_type = AdaptType(source.source_type);
	target.op = AdaptOperation(source.op);
	target.first = AdaptOperand(
		source.first, program, function, values, literals, telemetry);
	target.second = AdaptOperand(
		source.second, program, function, values, literals, telemetry);
	target.third = AdaptOperand(
		source.third, program, function, values, literals, telemetry);
	AdaptProjection(source.projection, &target);
	switch (source.kind)
	{
	case Instruction::CONST: target.kind = lowir_model::Instruction::IK_CONST; break;
	case Instruction::COPY: target.kind = lowir_model::Instruction::IK_COPY; break;
	case Instruction::ADDR: target.kind = lowir_model::Instruction::IK_ADDR; break;
	case Instruction::LOAD: target.kind = lowir_model::Instruction::IK_LOAD; break;
	case Instruction::ATOMIC_LOAD:
		target.kind = lowir_model::Instruction::IK_ATOMIC_LOAD;
		target.args.push_back(AdaptOperand(Operand(source.atomic_order, LowI32()),
			program, function, values, literals, telemetry));
		break;
	case Instruction::STORE: target.kind = lowir_model::Instruction::IK_STORE; break;
	case Instruction::ATOMIC_STORE:
		target.kind = lowir_model::Instruction::IK_ATOMIC_STORE;
		target.args.push_back(AdaptOperand(Operand(source.atomic_order, LowI32()),
			program, function, values, literals, telemetry));
		break;
	case Instruction::ATOMIC_EXCHANGE:
		target.kind = lowir_model::Instruction::IK_ATOMIC_EXCHANGE;
		target.args.push_back(AdaptOperand(Operand(source.atomic_order, LowI32()),
			program, function, values, literals, telemetry));
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
			program, function, values, literals, telemetry));
		break;
	case Instruction::ATOMIC_COMPARE_EXCHANGE:
		target.kind = lowir_model::Instruction::IK_ATOMIC_COMPARE_EXCHANGE;
		target.args.push_back(AdaptOperand(Operand(source.atomic_order, LowI32()),
			program, function, values, literals, telemetry));
		target.args.push_back(AdaptOperand(
			Operand(source.atomic_failure_order, LowI32()), program, function,
			values, literals, telemetry));
		break;
	case Instruction::ATOMIC_THREAD_FENCE:
	case Instruction::ATOMIC_SIGNAL_FENCE:
		target.kind = source.kind == Instruction::ATOMIC_THREAD_FENCE ?
			lowir_model::Instruction::IK_ATOMIC_THREAD_FENCE :
			lowir_model::Instruction::IK_ATOMIC_SIGNAL_FENCE;
		target.first = AdaptOperand(Operand(source.atomic_order, LowI32()),
			program, function, values, literals, telemetry);
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
			AdaptBoundaryFacts(
				program.symbols[source.first.id], &target.call_boundary);
		}
		if (source.extra_count)
		{
			if (source.extra_first == kNoLowId ||
				source.extra_first > program.call_arguments.size() ||
				source.extra_count > program.call_arguments.size() - source.extra_first)
				throw std::logic_error("invalid typed LowIR call arguments");
			for (std::size_t i = 0; i < source.extra_count; ++i)
				target.args.push_back(AdaptOperand(
					program.call_arguments[source.extra_first + i], program, function,
					values, literals, telemetry));
		}
		if (source.indirect)
		{
			target.has_call_signature = true;
			target.call_return_type = AdaptType(source.type);
			target.call_params.resize(source.extra_count);
			for (std::size_t i = 0; i < source.extra_count; ++i)
			{
				lowir_model::Parameter& parameter = target.call_params[i];
				parameter.name = telemetry->Intern(literals,
					i + source.virtual_base_argument_count >=
					source.extra_count ?
					telemetry->Percent("__pvbptr" + std::to_string(i +
						source.virtual_base_argument_count - source.extra_count)) :
					telemetry->Percent("arg" + std::to_string(i)));
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
			type.symbol = lowir_model::SymbolId(symbol);
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
		target.first.block = lowir_model::BlockId(source.target);
		break;
	case Instruction::BRANCH:
		target.kind = lowir_model::Instruction::IK_BRANCH;
		if (source.target >= function.blocks.size() ||
			source.alternate >= function.blocks.size())
			throw std::logic_error("invalid typed LowIR branch target");
		target.second.kind = lowir_model::Operand::OP_LABEL;
		target.second.block = lowir_model::BlockId(source.target);
		target.third.kind = lowir_model::Operand::OP_LABEL;
		target.third.block = lowir_model::BlockId(source.alternate);
		break;
	case Instruction::SWITCH:
		target.kind = lowir_model::Instruction::IK_SWITCH;
		if (source.target >= function.blocks.size())
			throw std::logic_error("invalid typed LowIR switch target");
		target.second.kind = lowir_model::Operand::OP_LABEL;
		target.second.block = lowir_model::BlockId(source.target);
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
			value.int_high = value.int_value < 0 ? ~std::uint64_t(0) : 0;
			value.has_int_value = true;
			value.literal_type = lowir_model::builtin_lowir_type(
				lowir_model::LTK_I64);
			value.literal = telemetry->IntegerLiteral(
				literals, value.int_value, value.int_high, LowI64());
			value.has_spelling = value.literal.valid();
			target.args.push_back(value);
			const BlockId block =
				program.switch_case_targets[source.extra_first + i];
			if (block >= function.blocks.size())
				throw std::logic_error("invalid typed LowIR switch case target");
			lowir_model::Operand label;
			label.kind = lowir_model::Operand::OP_LABEL;
			label.block = lowir_model::BlockId(block);
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
			target.first.block = lowir_model::BlockId(source.target);
		}
	}
	return target;
}

void AppendExport(const Symbol& source, ir_model::ExportedSymbol* target,
	AdapterTelemetry* telemetry)
{
	target->internal_symbol = telemetry->At(source.name);
	target->object_symbol = source.object_name;
	target->keep_internal_alias = source.keep_internal_object_alias;
	target->prefer_local_object_binding = source.prefer_local_object_binding;
	target->linkage = source.internal_linkage ? ir_model::SL_INTERNAL :
		source.weak_linkage ? ir_model::SL_WEAK : ir_model::SL_EXTERNAL;
}

}

lowir_model::LowirProgram AdaptTypedLowIRForNative(
	const TypedProgram& source,
	lowir_model::LowirPreparationStats* preparation_stats,
	bool preserve_literal_spellings)
{
	AdapterTelemetry telemetry(preparation_stats, preserve_literal_spellings);
	CountTypedNames(source, &telemetry);
	lowir_model::LowirProgram target;
	target.symbol_names.reserve(source.symbols.size());
	for (std::size_t i = 0; i < source.symbols.size(); ++i)
		lowir_model::append_lowir_symbol(target,
			telemetry.Intern(&target.strings,
				telemetry.At(source.symbols[i].name)));
	target.global_declarations.reserve(source.global_declarations.size());
	for (std::size_t i = 0; i < source.global_declarations.size(); ++i)
	{
		const GlobalDeclaration& item = source.global_declarations[i];
		const Symbol& symbol = source.symbols[item.symbol];
		lowir_model::GlobalDeclaration result;
		result.symbol = lowir_model::SymbolId(item.symbol);
		result.has_type = item.typed;
		if (item.typed) result.type = AdaptType(item.type);
		if (symbol.thread_local_storage)
			result.storage = lowir_model::GSM_THREAD_LOCAL;
		AdaptSymbolFacts(symbol, &target.strings, &result.metadata, 0,
			&telemetry);
		target.global_declarations.push_back(std::move(result));
	}
	target.function_declarations.reserve(source.declarations.size());
	for (std::size_t i = 0; i < source.declarations.size(); ++i)
	{
		const FunctionDeclaration& item = source.declarations[i];
		const Symbol& symbol = source.symbols[item.symbol];
		lowir_model::FunctionDeclaration result;
		result.symbol = lowir_model::SymbolId(item.symbol);
		result.params = AdaptParameters(
			item.parameters, &target.strings, &telemetry);
		result.return_type = AdaptType(item.result);
		result.boundary.arity = item.variadic ? lowir_model::CAM_VARIADIC :
			lowir_model::CAM_FIXED;
		AdaptSymbolFacts(
			symbol, &target.strings, &result.metadata, &result.boundary,
			&telemetry);
		if (symbol.tls_for_symbol != kNoLowId)
		{
			result.metadata.tls_for_symbol_id =
				lowir_model::SymbolId(symbol.tls_for_symbol);
		}
		target.function_declarations.push_back(std::move(result));
	}
	target.globals.reserve(source.globals.size());
	for (std::size_t i = 0; i < source.globals.size(); ++i)
	{
		const Global& item = source.globals[i];
		const Symbol& symbol = source.symbols[item.symbol];
		lowir_model::GlobalDefinition result;
		result.symbol = lowir_model::SymbolId(item.symbol);
		if (item.type.kind != LOW_INVALID) result.type = AdaptType(item.type);
		if (symbol.thread_local_storage)
			result.storage = lowir_model::GSM_THREAD_LOCAL;
		AdaptSymbolFacts(symbol, &target.strings, &result.metadata, 0,
			&telemetry);
		if (item.initializer_kind == Global::STRUCTURED_VALUE)
		{
			result.structured = true;
			result.data_items.reserve(item.items.size());
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
					data.symbol_id = lowir_model::SymbolId(value.symbol);
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
						const std::string& spelling =
							source.literals.Get(value.floating_spelling);
						data.literal_operand.literal = telemetry.Literal(
							&target.strings, spelling);
						data.literal_operand.has_spelling =
							data.literal_operand.literal.valid();
						data.literal_operand.has_float_bits =
							lowir_model::parse_lowir_floating_literal_bits(
								spelling, data.type,
								&data.literal_operand.literal_low,
								&data.literal_operand.literal_high);
					}
					else
					{
						data.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
						data.literal_operand.int_value = value.integer_value;
						data.literal_operand.int_high = value.integer_high;
						data.literal_operand.has_int_value = true;
						data.literal_operand.literal = telemetry.IntegerLiteral(
							&target.strings, value.integer_value,
							value.integer_high, value.type);
						data.literal_operand.has_spelling =
							data.literal_operand.literal.valid();
					}
				}
				result.data_items.push_back(std::move(data));
			}
		}
		else if (item.initializer_kind == Global::ADDRESS_VALUE)
		{
			result.init_kind = lowir_model::GlobalDefinition::INIT_ADDR;
			result.init_operand.kind = lowir_model::Operand::OP_GLOBAL;
			result.init_operand.symbol =
				lowir_model::SymbolId(item.address_symbol);
			result.addr_addend = item.address_offset;
		}
		else if (item.initializer_kind == Global::ZERO)
			result.init_kind = lowir_model::GlobalDefinition::INIT_ZERO;
		else
		{
			result.init_kind = lowir_model::GlobalDefinition::INIT_INTEGER;
			result.init_operand.literal_type = result.type;
			if (item.initializer_kind == Global::FLOATING_VALUE)
			{
				result.init_operand.kind = lowir_model::Operand::OP_FLOAT;
				result.init_operand.literal_type = result.type;
				const std::string& spelling =
					source.literals.Get(item.floating_initializer);
				result.init_operand.literal = telemetry.Literal(
					&target.strings, spelling);
				result.init_operand.has_spelling =
					result.init_operand.literal.valid();
				result.init_operand.has_float_bits =
					lowir_model::parse_lowir_floating_literal_bits(
						spelling, result.type, &result.init_operand.literal_low,
						&result.init_operand.literal_high);
			}
			else
			{
				result.init_operand.kind = lowir_model::Operand::OP_INTEGER;
				result.init_operand.int_value = item.initializer;
				result.init_operand.int_high = item.initializer_high;
				result.init_operand.has_int_value = true;
				result.init_operand.literal = telemetry.IntegerLiteral(
					&target.strings, item.initializer,
					item.initializer_high, item.type);
				result.init_operand.has_spelling =
					result.init_operand.literal.valid();
			}
		}
		target.globals.push_back(std::move(result));
	}
	target.functions.reserve(source.functions.size());
	for (std::size_t i = 0; i < source.functions.size(); ++i)
	{
		const Function& item = source.functions[i];
		const Symbol& symbol = source.symbols[item.symbol];
		lowir_model::Function result;
		result.symbol = lowir_model::SymbolId(item.symbol);
		result.params = AdaptParameters(
			item.parameters, &target.strings, &telemetry);
		const AdaptedValues values = PrepareValues(item, target.strings, &result);
		result.return_type = AdaptType(item.result);
		result.boundary.arity = item.variadic ? lowir_model::CAM_VARIADIC :
			lowir_model::CAM_FIXED;
		AdaptSymbolFacts(
			symbol, &target.strings, &result.metadata, &result.boundary,
			&telemetry);
		if (item.entry)
		{
			result.metadata.role = lowir_model::SR_ENTRY;
			result.metadata.keep_internal_alias = true;
		}
		else if (item.initializer) result.metadata.role = lowir_model::SR_INIT;
		else if (item.finalizer) result.metadata.role = lowir_model::SR_FINI;
		if (symbol.tls_for_symbol != kNoLowId)
		{
			result.metadata.tls_for_symbol_id =
				lowir_model::SymbolId(symbol.tls_for_symbol);
		}
		for (std::size_t j = 0; j < item.slots.size(); ++j)
		{
			lowir_model::append_lowir_slot(result,
				telemetry.Intern(&target.strings,
					telemetry.Dollar(item.slots[j].name)),
				AdaptType(item.slots[j].type));
			if (item.slots[j].parameter_origin.valid())
			{
				const std::uint32_t parameter =
					item.slots[j].parameter_origin;
				if (parameter >= values.parameters.size())
					throw std::logic_error(
						"typed LowIR slot has invalid parameter origin");
				result.slot_parameter_values[j] = values.parameters[parameter];
			}
		}
		result.blocks.reserve(item.block_order.size());
		result.block_labels.resize(item.blocks.size());
		result.next_block_id = static_cast<std::uint32_t>(item.blocks.size());
		for (std::size_t order = 0; order < item.block_order.size(); ++order)
		{
			const BlockId block_id = item.block_order[order];
			if (block_id >= item.blocks.size())
				throw std::logic_error("invalid typed LowIR block order");
			const Block& block = item.blocks[block_id];
			lowir_model::Block lowered;
			lowered.id = lowir_model::BlockId(block_id);
			result.block_labels[block_id] =
				telemetry.Intern(&target.strings, telemetry.Label(block.label));
			lowered.instructions.reserve(block.instructions.size());
			for (std::size_t j = 0; j < block.instructions.size(); ++j)
				lowered.instructions.push_back(AdaptInstruction(
					block.instructions[j], source, item, values, &target.strings,
					&telemetry));
			result.blocks.push_back(std::move(lowered));
		}
		target.functions.push_back(std::move(result));
	}
	target.object_aliases.reserve(source.object_aliases.size());
	target.exported_symbols.reserve(
		source.object_aliases.size() + source.symbols.size());
	for (std::size_t i = 0; i < source.object_aliases.size(); ++i)
	{
		lowir_model::ObjectAlias alias;
		alias.object_symbol =
			telemetry.Intern(
				&target.strings, source.object_aliases[i].object_name);
		alias.target_id = lowir_model::SymbolId(source.object_aliases[i].target);
		const Symbol& alias_target =
			source.symbols[source.object_aliases[i].target];
		// An ABI alias is an additional exported spelling of the same root, so
		// carry the target's linkage with it across the typed-LowIR boundary.
		if (source.host_object_emission)
		{
			ir_model::ExportedSymbol exported_alias;
			AppendExport(alias_target, &exported_alias, &telemetry);
			exported_alias.object_symbol =
				target.strings.get(alias.object_symbol);
			target.exported_symbols.push_back(std::move(exported_alias));
		}
		target.object_aliases.push_back(std::move(alias));
	}
	for (std::size_t i = 0; i < source.symbols.size(); ++i)
	{
		ir_model::ExportedSymbol symbol;
		AppendExport(source.symbols[i], &symbol, &telemetry);
		target.exported_symbols.push_back(std::move(symbol));
	}
	canonicalize_frontend_lowir(target, preparation_stats);
	finalize_lowir_object_model(target, preparation_stats);
	telemetry.Finish(target);
	return target;
}

}
