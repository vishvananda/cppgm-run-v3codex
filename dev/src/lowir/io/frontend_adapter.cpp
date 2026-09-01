#include "lowir/io/frontend_adapter.h"

#include "support/numeric/decimal_spelling.h"
#include "semantic/lifetime/demand_reason.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <utility>

namespace cppgm
{
namespace lowir_io
{
namespace
{

using namespace cppgm::lowering::ir;

struct AdapterTelemetry
{
	AdapterTelemetry(lowir_model::LowirPreparationStats* output_value,
		lowir_model::PresentationPolicy policy_value)
		: output(output_value), policy(policy_value) {}

	lowir_model::StringId Intern(lowir_model::StringPool* strings,
		const std::string& text, bool literal = false)
	{
		if (output && literal) ++output->adapter_literal_materializations;
		return strings->intern(text, output ? &pool : 0);
	}

	bool serializable() const
	{
		return policy == lowir_model::PRESENTATION_SERIALIZABLE;
	}

	lowir_model::StringId Literal(lowir_model::StringId spelling) const
	{
		return serializable() ? spelling : lowir_model::StringId();
	}

	void CountInstructionStorage(
		const lowir_model::Instruction& instruction)
	{
		if (!output) return;
		instruction_dynamic_storage +=
			instruction.args.capacity() * sizeof(lowir_model::Operand) +
			instruction.call_params.capacity() * sizeof(lowir_model::Parameter);
	}

	void Finish(const lowir_model::LowirProgram& program)
	{
		if (!output) return;
		output->adapter_string_pool = pool;
		output->lowir_string_entries = program.strings.retained_size();
		output->lowir_spelling_bytes = program.strings.spelling_bytes();
		output->lowir_string_storage_bytes = program.strings.storage_bytes();
		std::size_t bytes = program.strings.storage_bytes() +
			program.symbol_names.capacity() * sizeof(lowir_model::StringId) +
			program.global_declarations.capacity() *
				sizeof(lowir_model::GlobalDeclaration) +
			program.globals.capacity() *
				sizeof(lowir_model::GlobalDefinition) +
			program.function_declarations.capacity() *
				sizeof(lowir_model::FunctionDeclaration) +
			program.functions.capacity() * sizeof(lowir_model::Function) +
			program.object_aliases.capacity() * sizeof(lowir_model::ObjectAlias) +
			program.exported_symbols.capacity() *
				sizeof(lowir_model::ExportedSymbol) +
			instruction_dynamic_storage;
		for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
			bytes += program.function_declarations[i].params.capacity() *
				sizeof(lowir_model::Parameter);
		for (std::size_t i = 0; i < program.globals.size(); ++i)
			bytes += program.globals[i].data_items.capacity() *
				sizeof(lowir_model::GlobalDefinition::DataItem);
		for (std::size_t f = 0; f < program.functions.size(); ++f)
		{
			const lowir_model::Function& function = program.functions[f];
			bytes += function.params.capacity() * sizeof(lowir_model::Parameter) +
				function.slots.capacity() * sizeof(lowir_model::SlotId) +
				function.slot_names.capacity() * sizeof(lowir_model::StringId) +
				function.slot_types.capacity() * sizeof(lowir_model::LowType) +
				function.slot_parameter_values.capacity() *
					sizeof(lowir_model::ValueId) +
				function.value_names.capacity() *
					sizeof(lowir_model::PresentationName) +
				function.value_types.capacity() * sizeof(lowir_model::LowType) +
				function.blocks.capacity() * sizeof(lowir_model::Block) +
				function.block_labels.capacity() * sizeof(lowir_model::StringId) +
				function.block_presentation_order.capacity() *
					sizeof(std::uint32_t) +
				function.generated_name_reservations.storage_bytes();
			for (std::size_t b = 0; b < function.blocks.size(); ++b)
				bytes += function.blocks[b].instructions.capacity() *
					sizeof(lowir_model::Instruction);
		}
		output->lowir_model_storage_bytes = bytes;
	}

	lowir_model::LowirPreparationStats* output;
	lowir_model::PresentationPolicy policy;
	lowir_model::StringPoolStats pool;
	std::size_t instruction_dynamic_storage = 0;
};

void CountTypedName(const std::string& name, AdapterTelemetry* telemetry)
{
	if (!telemetry->output || name.empty()) return;
	++telemetry->output->typed_name_entries;
	telemetry->output->typed_name_bytes += name.size();
}

void CountTypedName(lowir_model::StringId name, const cppgm::lowering::ir::Program& program,
	AdapterTelemetry* telemetry)
{
	if (name.valid()) CountTypedName(program.strings.get(name), telemetry);
}

void CountTypedNames(const cppgm::lowering::ir::Program& program, AdapterTelemetry* telemetry)
{
	if (!telemetry->output) return;
	for (std::size_t i = 0; i < program.symbols.size(); ++i)
	{
		CountTypedName(program.symbols[i].name, program, telemetry);
		CountTypedName(program.symbols[i].object_name, program, telemetry);
		CountTypedName(program.symbols[i].section_name, program, telemetry);
	}
	for (std::size_t i = 0; i < program.object_aliases.size(); ++i)
		CountTypedName(
			program.object_aliases[i].object_name, program, telemetry);
	for (std::size_t i = 0; i < program.declarations.size(); ++i)
		for (std::size_t p = 0; p < program.declarations[i].parameters.size(); ++p)
			CountTypedName(
				program.declarations[i].parameters[p].name, program, telemetry);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
	{
		const Function& function = program.functions[i];
		for (std::size_t p = 0; p < function.parameters.size(); ++p)
			CountTypedName(function.parameters[p].name, program, telemetry);
		for (std::size_t s = 0; s < function.slots.size(); ++s)
			CountTypedName(function.slots[s].name, program, telemetry);
		for (std::size_t b = 0; b < function.blocks.size(); ++b)
			CountTypedName(function.blocks[b].label, program, telemetry);
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
	lowir_model::ThrowLowirInternalError("invalid typed LowIR type at native boundary");
}

struct AdaptedValues
{
	std::vector<lowir_model::ValueId> parameters;
	std::vector<lowir_model::ValueId> temporaries;
};

void AdaptSymbolReference(std::uint32_t symbol_id,
	const cppgm::lowering::ir::Program& program, lowir_model::Operand* result)
{
	if (symbol_id >= program.symbols.size())
		lowir_model::ThrowLowirInternalError("invalid typed LowIR symbol operand");
	const Symbol& symbol = program.symbols[symbol_id];
	result->kind = lowir_model::Operand::OP_GLOBAL;
	result->symbol = lowir_model::SymbolId(symbol_id);
	result->address_binding = symbol.definition_emitted &&
		!symbol.weak_linkage ? lowir_model::Operand::ADDRESS_LOCAL :
		lowir_model::Operand::ADDRESS_PREEMPTIBLE;
}

void AdaptOperand(const Operand& operand,
	const cppgm::lowering::ir::Program& program, const Function& function,
	const AdaptedValues& values, lowir_model::StringPool* literals,
	AdapterTelemetry* telemetry, lowir_model::Operand* output)
{
	lowir_model::Operand& result = *output;
	if (operand.type.kind != LOW_INVALID)
		result.literal_type = AdaptType(operand.type);
	switch (operand.kind)
	{
	case Operand::TEMP:
		if (operand.id >= values.temporaries.size() ||
			!values.temporaries[operand.id].valid())
			lowir_model::ThrowLowirInternalError("invalid typed LowIR temporary operand");
		result.kind = lowir_model::Operand::OP_TEMP;
		result.value = values.temporaries[operand.id];
		break;
	case Operand::PARAMETER:
		if (operand.id >= values.parameters.size())
			lowir_model::ThrowLowirInternalError("invalid typed LowIR parameter operand");
		result.kind = lowir_model::Operand::OP_TEMP;
		result.value = values.parameters[operand.id];
		break;
	case Operand::SLOT:
		if (operand.id >= function.slots.size())
			lowir_model::ThrowLowirInternalError("invalid typed LowIR slot operand");
		result.kind = lowir_model::Operand::OP_SLOT;
		result.slot = lowir_model::SlotId(operand.id);
		break;
	case Operand::GLOBAL:
	case Operand::FUNCTION:
		AdaptSymbolReference(operand.id, program, &result);
		break;
	case Operand::INTEGER:
		result.kind = lowir_model::Operand::OP_INTEGER;
		result.int_value = operand.integer_value;
		result.int_high = operand.integer_high;
		result.has_int_value = true;
		// Integer payloads are already canonical typed facts.  The serializer
		// renders them lazily when text is requested.
		break;
	case Operand::FLOATING:
		result.kind = lowir_model::Operand::OP_FLOAT;
		result.literal = telemetry->Literal(lowir_model::StringId(operand.id));
		result.has_spelling = result.literal.valid();
		result.has_float_bits = true;
		result.literal_low = operand.floating_low;
		result.literal_high = operand.integer_high;
		break;
	case Operand::NULL_POINTER:
		result.kind = lowir_model::Operand::OP_INTEGER;
		result.literal = telemetry->serializable() ?
			telemetry->Intern(literals, "nullptr", true) :
			lowir_model::StringId();
		result.has_spelling = result.literal.valid();
		result.int_value = 0;
		result.int_high = 0;
		result.has_int_value = true;
		break;
	case Operand::NONE:
		break;
	}
}

void AppendAdaptedOperand(const Operand& operand,
	const cppgm::lowering::ir::Program& program, const Function& function,
	const AdaptedValues& values, lowir_model::StringPool* literals,
	AdapterTelemetry* telemetry, std::vector<lowir_model::Operand>* output)
{
	output->emplace_back();
	AdaptOperand(operand, program, function, values, literals, telemetry,
		&output->back());
}

void AdaptParameterFacts(const Parameter& source,
	lowir_model::Parameter* target,
	AdapterTelemetry* telemetry)
{
	target->name = telemetry->serializable() ? source.name :
		lowir_model::StringId();
	target->type = AdaptType(source.type);
	if (source.reference)
		target->metadata.passing = lowir_model::PPM_BY_ADDRESS;
	else if (source.indirect_result)
		target->metadata.passing = lowir_model::PPM_INDIRECT_RESULT;
	else if (source.by_address)
		target->metadata.passing = lowir_model::PPM_BY_ADDRESS;
	if (source.alias == Parameter::ALIAS_NOALIAS)
		target->metadata.alias = lowir_model::PALM_NOALIAS;
	target->metadata.object_bytes = source.object_bytes;
}

std::vector<lowir_model::Parameter> AdaptParameters(
	const std::vector<Parameter>& source,
	AdapterTelemetry* telemetry)
{
	std::vector<lowir_model::Parameter> result(source.size());
	for (std::size_t i = 0; i < source.size(); ++i)
		AdaptParameterFacts(source[i], &result[i], telemetry);
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

AdaptedValues PrepareParameterValues(const Function& source,
	lowir_model::PresentationPolicy presentation_policy,
	lowir_model::Function* target)
{
	AdaptedValues result;
	result.parameters.resize(target->params.size());
	result.temporaries.resize(source.temporary_limit);
	target->value_names.reserve(
		target->params.size() + source.temporary_limit);
	target->value_types.reserve(
		target->params.size() + source.temporary_limit);
	for (std::size_t i = 0; i < target->params.size(); ++i)
	{
		result.parameters[i] =
			presentation_policy == lowir_model::PRESENTATION_SERIALIZABLE ?
			lowir_model::append_lowir_value(
				*target, target->params[i].name, target->params[i].type) :
			lowir_model::append_lowir_unnamed_value(
				*target, target->params[i].type);
		target->params[i].value = result.parameters[i];
	}
	return result;
}

void PrepareInstructionValue(const Instruction& instruction,
	lowir_model::PresentationPolicy presentation_policy,
	AdaptedValues* values, lowir_model::Function* target)
{
	if (instruction.dest == kNoLowId) return;
	if (instruction.dest >= values->temporaries.size())
		lowir_model::ThrowLowirInternalError(
			"typed LowIR result exceeds its dense temporary limit");
	if (values->temporaries[instruction.dest].valid())
		lowir_model::ThrowLowirInternalError("duplicate typed LowIR result identity");
	values->temporaries[instruction.dest] =
		presentation_policy == lowir_model::PRESENTATION_SERIALIZABLE ?
		lowir_model::append_lowir_generated_value(
			*target, instruction.dest, AdaptResultType(instruction)) :
		lowir_model::append_lowir_unnamed_value(
			*target, AdaptResultType(instruction));
}

void AdaptBoundaryFacts(const Symbol& source,
	lowir_model::FunctionBoundaryMetadata* boundary)
{
	boundary->effects = source.effects == Symbol::EFFECTS_READNONE ?
		lowir_model::CFXM_READNONE :
		source.effects == Symbol::EFFECTS_READONLY ?
		lowir_model::CFXM_READONLY : lowir_model::CFXM_DEFAULT;
	if (source.nonthrowing) boundary->unwind = lowir_model::CUM_NO;
	if (source.noreturn) boundary->returns = lowir_model::CRM_NORETURN;
	if (source.stable_prefix_query)
		boundary->query = lowir_model::CQM_STABLE_PREFIX;
}

bool HasNonCallDemand(const Symbol& source)
{
	using namespace semantic;
	const std::uint16_t call_mask =
		FunctionDemandReasonMask(FUNCTION_DEMAND_EVALUATED_USE) |
		FunctionDemandReasonMask(FUNCTION_DEMAND_RETAINED_CALL);
	return (source.demand_reason_mask &
		static_cast<std::uint16_t>(~call_mask)) != 0;
}

void AdaptSymbolFacts(const Symbol& source,
	lowir_model::SymbolMetadata* symbol,
	lowir_model::FunctionBoundaryMetadata* boundary)
{
	if (source.c_linkage) symbol->linkage = lowir_model::LLM_C;
	symbol->binding = source.internal_linkage ? lowir_model::SBM_INTERNAL :
		source.weak_linkage ? lowir_model::SBM_WEAK : lowir_model::SBM_STRONG;
	symbol->object_symbol = source.object_name;
	symbol->section_name = source.section_name;
	symbol->keep_internal_alias = false;
	symbol->prefer_local_object_binding = source.prefer_local_object_binding;
	symbol->object_output_root = source.object_output_root ||
		(source.internal_linkage && HasNonCallDemand(source));
	symbol->force_inline = source.force_inline;
	symbol->inline_hint = source.inline_hint;
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
	case Symbol::RUNTIME_ROLE_TERMINATE:
		symbol->role = lowir_model::SR_TERMINATE; break;
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
		lowir_model::LowOperation::LOP_FPEXT
	};
	static_assert(sizeof(operations) / sizeof(operations[0]) ==
		static_cast<std::size_t>(LOW_OP_FPEXT) + 1,
		"typed and compact LowIR operation tables must stay synchronized");
	const std::size_t index = static_cast<std::size_t>(source);
	if (index >= sizeof(operations) / sizeof(operations[0]))
		lowir_model::ThrowLowirInternalError("invalid typed LowIR operation");
	return operations[index];
}

void AdaptInstruction(const Instruction& source,
	const cppgm::lowering::ir::Program& program, const Function& function,
	const AdaptedValues& values, lowir_model::StringPool* literals,
	AdapterTelemetry* telemetry, lowir_model::Instruction* output)
{
	lowir_model::Instruction& target = *output;
	switch (source.kind)
	{
	case Instruction::ATOMIC_LOAD:
	case Instruction::ATOMIC_STORE:
	case Instruction::ATOMIC_EXCHANGE:
	case Instruction::ATOMIC_ADD_FETCH:
		target.args.reserve(1);
		break;
	case Instruction::ATOMIC_COMPARE_EXCHANGE:
		target.args.reserve(2);
		break;
	case Instruction::CALL:
	case Instruction::EH_FILTER:
		target.args.reserve(source.extra_count);
		break;
	case Instruction::SWITCH:
		target.args.reserve(static_cast<std::size_t>(source.extra_count) * 2);
		break;
	default:
		break;
	}
	if (source.dest != kNoLowId)
	{
		if (source.dest >= values.temporaries.size() ||
			!values.temporaries[source.dest].valid())
			lowir_model::ThrowLowirInternalError("invalid typed LowIR result identity");
		target.dest = values.temporaries[source.dest];
	}
	if (source.type.kind != LOW_INVALID) target.type = AdaptType(source.type);
	if (source.source_type.kind != LOW_INVALID)
		target.source_type = AdaptType(source.source_type);
	target.op = AdaptOperation(source.op);
	target.volatile_access = source.volatile_access;
	target.copy_elision_candidate = source.copy_elision_candidate;
	AdaptOperand(source.first, program, function, values, literals, telemetry,
		&target.first);
	AdaptOperand(source.second, program, function, values, literals, telemetry,
		&target.second);
	AdaptOperand(source.third, program, function, values, literals, telemetry,
		&target.third);
	AdaptProjection(source.projection, &target);
	switch (source.kind)
	{
	case Instruction::CONST: target.kind = lowir_model::Instruction::IK_CONST; break;
	case Instruction::COPY: target.kind = lowir_model::Instruction::IK_COPY; break;
	case Instruction::ADDR: target.kind = lowir_model::Instruction::IK_ADDR; break;
	case Instruction::LOAD: target.kind = lowir_model::Instruction::IK_LOAD; break;
	case Instruction::ATOMIC_LOAD:
		target.kind = lowir_model::Instruction::IK_ATOMIC_LOAD;
		AppendAdaptedOperand(Operand(source.atomic_order, LowI32()), program,
			function, values, literals, telemetry, &target.args);
		break;
	case Instruction::STORE: target.kind = lowir_model::Instruction::IK_STORE; break;
	case Instruction::ATOMIC_STORE:
		target.kind = lowir_model::Instruction::IK_ATOMIC_STORE;
		AppendAdaptedOperand(Operand(source.atomic_order, LowI32()), program,
			function, values, literals, telemetry, &target.args);
		break;
	case Instruction::ATOMIC_EXCHANGE:
		target.kind = lowir_model::Instruction::IK_ATOMIC_EXCHANGE;
		AppendAdaptedOperand(Operand(source.atomic_order, LowI32()), program,
			function, values, literals, telemetry, &target.args);
		break;
	case Instruction::COPY_OBJECT:
		target.kind = lowir_model::Instruction::IK_COPYOBJ;
		target.byte_count = static_cast<std::size_t>(source.type.width / 8);
		target.byte_alignment = source.type.alignment;
		target.type = lowir_model::LowType();
		break;
	case Instruction::ZERO_OBJECT:
		target.kind = lowir_model::Instruction::IK_ZEROINIT;
		target.byte_count = static_cast<std::size_t>(source.type.width / 8);
		target.byte_alignment = source.type.alignment;
		target.type = lowir_model::LowType();
		break;
	case Instruction::INDEX: target.kind = lowir_model::Instruction::IK_INDEX; break;
	case Instruction::UNARY: target.kind = lowir_model::Instruction::IK_UNARY; break;
	case Instruction::BINARY: target.kind = lowir_model::Instruction::IK_BINARY; break;
	case Instruction::CMP: target.kind = lowir_model::Instruction::IK_CMP; break;
	case Instruction::CONVERT: target.kind = lowir_model::Instruction::IK_CONVERT; break;
	case Instruction::ATOMIC_ADD_FETCH:
		target.kind = lowir_model::Instruction::IK_ATOMIC_ADD_FETCH;
		AppendAdaptedOperand(Operand(source.atomic_order, LowI32()), program,
			function, values, literals, telemetry, &target.args);
		break;
	case Instruction::ATOMIC_COMPARE_EXCHANGE:
		target.kind = lowir_model::Instruction::IK_ATOMIC_COMPARE_EXCHANGE;
		AppendAdaptedOperand(Operand(source.atomic_order, LowI32()), program,
			function, values, literals, telemetry, &target.args);
		AppendAdaptedOperand(Operand(source.atomic_failure_order, LowI32()),
			program, function, values, literals, telemetry, &target.args);
		break;
	case Instruction::ATOMIC_THREAD_FENCE:
	case Instruction::ATOMIC_SIGNAL_FENCE:
		target.kind = source.kind == Instruction::ATOMIC_THREAD_FENCE ?
			lowir_model::Instruction::IK_ATOMIC_THREAD_FENCE :
			lowir_model::Instruction::IK_ATOMIC_SIGNAL_FENCE;
		AdaptOperand(Operand(source.atomic_order, LowI32()), program, function,
			values, literals, telemetry, &target.first);
		break;
	case Instruction::STACK_ALLOC:
		target.kind = lowir_model::Instruction::IK_STACK_ALLOC; break;
	case Instruction::VA_START:
		target.kind = lowir_model::Instruction::IK_VA_START;
		target.type = lowir_model::LowType();
		break;
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
				lowir_model::ThrowLowirInternalError("invalid typed LowIR call target");
			AdaptBoundaryFacts(
				program.symbols[source.first.id], &target.call_boundary);
		}
		if (source.extra_count)
		{
			if (source.extra_first == kNoLowId ||
				source.extra_first > program.call_arguments.size() ||
				source.extra_count > program.call_arguments.size() - source.extra_first)
				lowir_model::ThrowLowirInternalError("invalid typed LowIR call arguments");
			for (std::size_t i = 0; i < source.extra_count; ++i)
				AppendAdaptedOperand(
					program.call_arguments[source.extra_first + i], program, function,
					values, literals, telemetry, &target.args);
		}
		if (source.indirect)
		{
			target.has_call_signature = true;
			target.call_return_type = AdaptType(source.type);
			target.call_params.resize(source.extra_count);
			for (std::size_t i = 0; i < source.extra_count; ++i)
			{
				lowir_model::Parameter& parameter = target.call_params[i];
				if (telemetry->serializable())
					parameter.name = telemetry->Intern(literals,
						i + source.virtual_base_argument_count >=
						source.extra_count ?
						"__pvbptr" + std::to_string(i +
							source.virtual_base_argument_count - source.extra_count) :
						"arg" + std::to_string(i));
				parameter.type = target.args[i].literal_type;
				if (source.extra_first + i <
					program.call_argument_object_bytes.size())
					parameter.metadata.object_bytes =
						program.call_argument_object_bytes[source.extra_first + i];
				if (source.extra_first + i <
					program.call_argument_references.size())
				{
					const std::uint8_t passing = program.call_argument_references[
						source.extra_first + i];
					if (passing == Instruction::CALL_PASS_REFERENCE)
						parameter.metadata.passing = lowir_model::PPM_BY_ADDRESS;
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
		if (source.target == kNoLowId) target.first = lowir_model::Operand();
		break;
	case Instruction::EH_CATCH:
		target.kind = lowir_model::Instruction::IK_EH_CATCH;
		target.has_eh_selector = true;
		target.eh_selector = source.second.integer_value;
		target.second = lowir_model::Operand();
		break;
	case Instruction::EH_FILTER:
		target.kind = lowir_model::Instruction::IK_EH_FILTER;
		target.has_eh_selector = true;
		target.eh_selector = source.first.integer_value;
		if (source.extra_first == kNoLowId ||
			source.extra_first > program.exception_filter_types.size() ||
			source.extra_count > program.exception_filter_types.size() -
				source.extra_first)
			lowir_model::ThrowLowirInternalError("invalid typed LowIR exception filter types");
		for (std::size_t i = 0; i < source.extra_count; ++i)
		{
			const SymbolId symbol = program.exception_filter_types[
				source.extra_first + i];
			if (symbol >= program.symbols.size())
				lowir_model::ThrowLowirInternalError("invalid exception filter RTTI symbol");
			lowir_model::Operand type;
			AdaptSymbolReference(symbol, program, &type);
			target.args.push_back(type);
		}
		break;
	case Instruction::EH_CATCH_ALL:
		target.kind = lowir_model::Instruction::IK_EH_CATCH_ALL;
		target.has_eh_selector = true;
		target.eh_selector = source.first.integer_value;
		target.first = lowir_model::Operand();
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
			lowir_model::ThrowLowirInternalError("invalid typed LowIR jump target");
		target.first.kind = lowir_model::Operand::OP_LABEL;
		target.first.block = lowir_model::BlockId(source.target);
		break;
	case Instruction::BRANCH:
		target.kind = lowir_model::Instruction::IK_BRANCH;
		if (source.target >= function.blocks.size() ||
			source.alternate >= function.blocks.size())
			lowir_model::ThrowLowirInternalError("invalid typed LowIR branch target");
		target.second.kind = lowir_model::Operand::OP_LABEL;
		target.second.block = lowir_model::BlockId(source.target);
		target.third.kind = lowir_model::Operand::OP_LABEL;
		target.third.block = lowir_model::BlockId(source.alternate);
		break;
	case Instruction::SWITCH:
		target.kind = lowir_model::Instruction::IK_SWITCH;
		if (source.target >= function.blocks.size())
			lowir_model::ThrowLowirInternalError("invalid typed LowIR switch target");
		target.second.kind = lowir_model::Operand::OP_LABEL;
		target.second.block = lowir_model::BlockId(source.target);
		if (source.extra_count && (source.extra_first == kNoLowId ||
			source.extra_first > program.switch_case_values.size() ||
			source.extra_count > program.switch_case_values.size() -
				source.extra_first ||
			source.extra_first > program.switch_case_targets.size() ||
			source.extra_count > program.switch_case_targets.size() -
				source.extra_first))
			lowir_model::ThrowLowirInternalError("invalid typed LowIR switch cases");
		for (std::size_t i = 0; i < source.extra_count; ++i)
		{
			lowir_model::Operand value;
			value.kind = lowir_model::Operand::OP_INTEGER;
			value.int_value = program.switch_case_values[source.extra_first + i];
			value.int_high = value.int_value < 0 ? ~std::uint64_t(0) : 0;
			value.has_int_value = true;
			value.literal_type = lowir_model::builtin_lowir_type(
				lowir_model::LTK_I64);
			target.args.push_back(value);
			const BlockId block =
				program.switch_case_targets[source.extra_first + i];
			if (block >= function.blocks.size())
				lowir_model::ThrowLowirInternalError("invalid typed LowIR switch case target");
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
	case Instruction::UNREACHABLE:
		target.kind = lowir_model::Instruction::IK_UNREACHABLE;
		break;
	}
	if (source.kind == Instruction::EH_TRY ||
		source.kind == Instruction::EH_CLEANUP)
	{
		if (source.target != kNoLowId)
		{
			if (source.target >= function.blocks.size())
				lowir_model::ThrowLowirInternalError("invalid typed LowIR EH target");
			target.first.kind = lowir_model::Operand::OP_LABEL;
			target.first.block = lowir_model::BlockId(source.target);
		}
	}
	telemetry->CountInstructionStorage(target);
}

void DiscardObjectOnlyPresentation(lowir_model::LowirProgram* program)
{
	std::vector<unsigned char> retained(program->strings.size() + 1, 0);
	const auto retain = [&retained](lowir_model::StringId id) {
		if (!id.valid()) return;
		const std::uint32_t index = id;
		if (index >= retained.size())
			lowir_model::ThrowLowirInternalError(
				"invalid object-only LowIR presentation identity");
		retained[index] = 1;
	};
	const auto retain_metadata = [&retain](
		const lowir_model::SymbolMetadata& metadata) {
		retain(metadata.object_symbol);
		retain(metadata.tls_for_spelling);
		retain(metadata.section_name);
	};
	for (std::size_t i = 0; i < program->symbol_names.size(); ++i)
		retain(program->symbol_names[i]);
	for (std::size_t i = 0; i < program->global_declarations.size(); ++i)
		retain_metadata(program->global_declarations[i].metadata);
	for (std::size_t i = 0; i < program->function_declarations.size(); ++i)
		retain_metadata(program->function_declarations[i].metadata);
	for (std::size_t i = 0; i < program->globals.size(); ++i)
		retain_metadata(program->globals[i].metadata);
	for (std::size_t i = 0; i < program->functions.size(); ++i)
	{
		retain_metadata(program->functions[i].metadata);
		retain(program->functions[i].debug_location.file);
	}
	for (std::size_t i = 0; i < program->object_aliases.size(); ++i)
	{
		retain(program->object_aliases[i].object_symbol);
		retain(program->object_aliases[i].target_spelling);
	}
	for (std::size_t i = 0; i < program->exported_symbols.size(); ++i)
	{
		retain(program->exported_symbols[i].object_symbol);
		retain(program->exported_symbols[i].thread_local_wrapper_object_symbol);
	}
	program->strings.retain_only(retained);
}

}

lowir_model::LowirProgram AdaptTypedLowirForBackend(
	cppgm::lowering::ir::Program&& source,
	lowir_model::LowirPreparationStats* preparation_stats,
	lowir_model::PresentationPolicy presentation_policy)
{
	AdapterTelemetry telemetry(preparation_stats, presentation_policy);
	CountTypedNames(source, &telemetry);
	lowir_model::LowirProgram target;
	target.presentation_policy = presentation_policy;
	target.symbol_names.reserve(source.symbols.size());
	for (std::size_t i = 0; i < source.symbols.size(); ++i)
		lowir_model::append_lowir_symbol(target,
			source.symbols[i].name);
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
		AdaptSymbolFacts(symbol, &result.metadata, 0);
		target.global_declarations.push_back(std::move(result));
	}
	target.function_declarations.reserve(source.declarations.size());
	for (std::size_t i = 0; i < source.declarations.size(); ++i)
	{
		const FunctionDeclaration& item = source.declarations[i];
		const Symbol& symbol = source.symbols[item.symbol];
		lowir_model::FunctionDeclaration result;
		result.symbol = lowir_model::SymbolId(item.symbol);
		result.params = AdaptParameters(item.parameters, &telemetry);
		result.return_type = AdaptType(item.result);
		result.boundary.arity = item.variadic ? lowir_model::CAM_VARIADIC :
			lowir_model::CAM_FIXED;
		AdaptSymbolFacts(symbol, &result.metadata, &result.boundary);
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
		if (item.type.kind != LOW_INVALID &&
			item.initializer_kind != Global::STRUCTURED_VALUE)
			result.type = AdaptType(item.type);
		if (symbol.thread_local_storage)
			result.storage = lowir_model::GSM_THREAD_LOCAL;
		else if (item.storage == Global::STORAGE_READONLY)
			result.storage = lowir_model::GSM_READONLY;
		AdaptSymbolFacts(symbol, &result.metadata, 0);
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
						data.literal_operand.literal = telemetry.Literal(
							value.floating_spelling);
						data.literal_operand.has_spelling =
							data.literal_operand.literal.valid();
						data.literal_operand.has_float_bits = true;
						data.literal_operand.literal_low = value.floating_low;
						data.literal_operand.literal_high = value.integer_high;
					}
					else
					{
						data.literal_operand.kind = lowir_model::Operand::OP_INTEGER;
						data.literal_operand.int_value = value.integer_value;
						data.literal_operand.int_high = value.integer_high;
						data.literal_operand.has_int_value = true;
					}
				}
				result.data_items.push_back(std::move(data));
			}
		}
		else if (item.initializer_kind == Global::ADDRESS_VALUE)
		{
			result.init_kind = lowir_model::GlobalDefinition::INIT_ADDR;
			AdaptSymbolReference(
				item.address_symbol, source, &result.init_operand);
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
				result.init_operand.literal = telemetry.Literal(
					item.floating_initializer);
				result.init_operand.has_spelling =
					result.init_operand.literal.valid();
				result.init_operand.has_float_bits = true;
				result.init_operand.literal_low = item.floating_initializer_low;
				result.init_operand.literal_high = item.initializer_high;
			}
			else
			{
				result.init_operand.kind = lowir_model::Operand::OP_INTEGER;
				result.init_operand.int_value = item.initializer;
				result.init_operand.int_high = item.initializer_high;
				result.init_operand.has_int_value = true;
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
		result.generated_name_reservations =
			item.generated_name_reservations;
		result.params = AdaptParameters(item.parameters, &telemetry);
		AdaptedValues values = PrepareParameterValues(
			item, presentation_policy, &result);
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
		{
			result.metadata.tls_for_symbol_id =
				lowir_model::SymbolId(symbol.tls_for_symbol);
		}
		for (std::size_t j = 0; j < item.slots.size(); ++j)
		{
			lowir_model::append_lowir_slot(result,
				presentation_policy ==
					lowir_model::PRESENTATION_SERIALIZABLE ?
					item.slots[j].name : lowir_model::StringId(),
				AdaptType(item.slots[j].type));
			if (item.slots[j].parameter_origin.valid())
			{
				const std::uint32_t parameter =
					item.slots[j].parameter_origin;
				if (parameter >= values.parameters.size())
					lowir_model::ThrowLowirInternalError(
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
				lowir_model::ThrowLowirInternalError("invalid typed LowIR block order");
			const Block& block = item.blocks[block_id];
			lowir_model::Block lowered;
			lowered.id = lowir_model::BlockId(block_id);
			if (presentation_policy ==
				lowir_model::PRESENTATION_SERIALIZABLE)
				result.block_labels[block_id] = block.label;
			lowered.instructions.reserve(block.instructions.size());
			for (std::size_t j = 0; j < block.instructions.size(); ++j)
			{
				PrepareInstructionValue(
					block.instructions[j], presentation_policy, &values, &result);
				lowered.instructions.emplace_back();
				AdaptInstruction(
					block.instructions[j], source, item, values, &source.strings,
					&telemetry, &lowered.instructions.back());
			}
			result.blocks.push_back(std::move(lowered));
		}
		if (presentation_policy == lowir_model::PRESENTATION_SERIALIZABLE)
			lowir_model::compute_lowir_block_presentation_order(
				result, source.strings);
		else
		{
			if (!item.block_presentation_order.empty() &&
				item.block_presentation_order.size() != item.blocks.size())
				lowir_model::ThrowLowirInternalError(
					"typed LowIR has no compact block presentation order");
			result.block_presentation_order = item.block_presentation_order;
		}
		target.functions.push_back(std::move(result));
	}
	target.object_aliases.reserve(source.object_aliases.size());
	for (std::size_t i = 0; i < source.object_aliases.size(); ++i)
	{
		lowir_model::ObjectAlias alias;
		alias.object_symbol = source.object_aliases[i].object_name;
		alias.target_id = lowir_model::SymbolId(source.object_aliases[i].target);
		target.object_aliases.push_back(std::move(alias));
	}
	target.strings = std::move(source.strings);
	canonicalize_frontend_lowir(target, preparation_stats);
	if (presentation_policy == lowir_model::PRESENTATION_OBJECT_ONLY)
	{
		lowir_model::publish_prederived_lowir_object_facts(
			target, preparation_stats);
		DiscardObjectOnlyPresentation(&target);
	}
	else
		finalize_lowir_object_model(target, preparation_stats);
	telemetry.Finish(target);
	return target;
}

}  // namespace lowir_io
}  // namespace cppgm
