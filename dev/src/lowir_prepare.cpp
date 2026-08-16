#include "lowir_prepare.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lowir_model
{
namespace
{

typedef std::chrono::steady_clock Clock;

std::uint64_t elapsed_nanoseconds(const Clock::time_point& started)
{
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			Clock::now() - started).count());
}

void note_operand_reference(const Operand& operand,
	std::unordered_set<std::string>* referenced,
	LowirPreparationStats* stats)
{
	if (stats) ++stats->reference_operand_visits;
	if (operand.kind == Operand::OP_GLOBAL)
		referenced->insert(operand.text);
}

void canonicalize_frontend_symbol(const std::string& name,
	SymbolMetadata* metadata)
{
	if (metadata->linkage == LLM_CPP) metadata->linkage = LLM_DEFAULT;
	if (metadata->binding != SBM_INTERNAL &&
		!metadata->object_symbol.empty() &&
		name == "@" + metadata->object_symbol)
		metadata->object_symbol.clear();
}

ir_model::SymbolLinkage exported_linkage(SymbolBindingMode binding)
{
	return binding == SBM_INTERNAL ? ir_model::SL_INTERNAL :
		binding == SBM_WEAK ? ir_model::SL_WEAK : ir_model::SL_EXTERNAL;
}

void append_export(Program& program, const std::string& name,
	const SymbolMetadata& metadata)
{
	ir_model::ExportedSymbol result;
	result.internal_symbol = name;
	result.object_symbol = metadata.object_symbol;
	// Canonical LowIR omits a redundant external object= spelling whenever it
	// is exactly the LowIR name without its sigil.  Reconstruct that derived
	// spelling at the object boundary; this also covers ordinary namespace-
	// scope data, whose Itanium object name need not carry a _Z prefix.
	if (result.object_symbol.empty() && metadata.binding != SBM_INTERNAL &&
		!name.empty() && name[0] == '@')
		result.object_symbol = name.substr(1);
	result.keep_internal_alias = metadata.keep_internal_alias;
	result.prefer_local_object_binding = metadata.prefer_local_object_binding;
	result.linkage = exported_linkage(metadata.binding);
	program.exported_symbols.push_back(result);
}

void derive_exports(Program& program)
{
	std::unordered_map<std::string, ir_model::SymbolLinkage> linkage;
	for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
		linkage[program.global_declarations[i].name] =
			exported_linkage(program.global_declarations[i].metadata.binding);
	for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
		linkage[program.function_declarations[i].name] =
			exported_linkage(program.function_declarations[i].metadata.binding);
	for (std::size_t i = 0; i < program.globals.size(); ++i)
		linkage[program.globals[i].name] =
			exported_linkage(program.globals[i].metadata.binding);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		linkage[program.functions[i].name] =
			exported_linkage(program.functions[i].metadata.binding);
	for (std::size_t i = 0; i < program.object_aliases.size(); ++i)
	{
		const ObjectAlias& object_alias = program.object_aliases[i];
		ir_model::ExportedSymbol alias;
		alias.internal_symbol = object_alias.target;
		alias.object_symbol = object_alias.object_symbol;
		const std::unordered_map<std::string,
			ir_model::SymbolLinkage>::const_iterator found =
			linkage.find(alias.internal_symbol);
		if (found != linkage.end()) alias.linkage = found->second;
		program.exported_symbols.push_back(alias);
	}
	for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
		append_export(program, program.global_declarations[i].name,
			program.global_declarations[i].metadata);
	for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
		append_export(program, program.function_declarations[i].name,
			program.function_declarations[i].metadata);
	for (std::size_t i = 0; i < program.globals.size(); ++i)
		append_export(program, program.globals[i].name,
			program.globals[i].metadata);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		append_export(program, program.functions[i].name,
			program.functions[i].metadata);
}

void clear_serialized_operand_type(Operand& operand,
	LowirPreparationStats* stats)
{
	if (stats) ++stats->serialized_operand_visits;
	operand.literal_type = LowType();
	operand.address_binding = Operand::ADDRESS_LOCAL;
	if (operand.kind != Operand::OP_INTEGER)
	{
		operand.has_int_value = false;
		operand.int_value = 0;
	}
}

void restore_address_binding(Operand& operand,
	const std::unordered_set<std::string>& local,
	LowirPreparationStats* stats)
{
	if (stats) ++stats->derived_operand_visits;
	if (operand.kind != Operand::OP_GLOBAL) return;
	operand.address_binding = local.count(operand.text) ?
		Operand::ADDRESS_LOCAL : Operand::ADDRESS_PREEMPTIBLE;
}

}  // namespace

void canonicalize_frontend_lowir(Program& program,
	LowirPreparationStats* stats)
{
	const Clock::time_point started = Clock::now();
	std::unordered_set<std::string> referenced;
	for (std::size_t i = 0; i < program.globals.size(); ++i)
	{
		const GlobalDefinition& global = program.globals[i];
		note_operand_reference(global.init_operand, &referenced, stats);
		for (std::size_t j = 0; j < global.data_items.size(); ++j)
		{
			if (stats) ++stats->reference_operand_visits;
			if (global.data_items[j].kind ==
				GlobalDefinition::DataItem::ITEM_ADDR)
				referenced.insert(global.data_items[j].symbol);
		}
	}
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		for (std::size_t b = 0; b < program.functions[i].blocks.size(); ++b)
			for (std::size_t j = 0;
				j < program.functions[i].blocks[b].instructions.size(); ++j)
			{
				const Instruction& ins =
					program.functions[i].blocks[b].instructions[j];
				note_operand_reference(ins.first, &referenced, stats);
				note_operand_reference(ins.second, &referenced, stats);
				note_operand_reference(ins.third, &referenced, stats);
				for (std::size_t k = 0; k < ins.args.size(); ++k)
					note_operand_reference(ins.args[k], &referenced, stats);
			}
	// A TLS wrapper's target is a semantic symbol edge even when no ordinary
	// instruction names the variable directly.  It must participate in the
	// same declaration liveness calculation as operand references.
	for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
		if (!program.function_declarations[i].metadata.tls_for_symbol.empty())
			referenced.insert(
				program.function_declarations[i].metadata.tls_for_symbol);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		if (!program.functions[i].metadata.tls_for_symbol.empty())
			referenced.insert(program.functions[i].metadata.tls_for_symbol);
	if (stats) stats->referenced_symbols += referenced.size();

	std::vector<GlobalDeclaration> globals;
	globals.reserve(program.global_declarations.size());
	std::unordered_set<std::string> retained_global_declarations;
	for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
	{
		if (stats) ++stats->declaration_visits;
		if (referenced.count(program.global_declarations[i].name) &&
			retained_global_declarations.insert(
				program.global_declarations[i].name).second)
		{
			globals.push_back(std::move(program.global_declarations[i]));
			if (stats) ++stats->retained_declarations;
		}
	}
	program.global_declarations.swap(globals);

	std::vector<FunctionDeclaration> functions;
	functions.reserve(program.function_declarations.size());
	std::unordered_set<std::string> retained_function_declarations;
	for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
	{
		if (stats) ++stats->declaration_visits;
		if ((referenced.count(program.function_declarations[i].name) ||
			 !program.function_declarations[i].metadata.tls_for_symbol.empty()) &&
			retained_function_declarations.insert(
				program.function_declarations[i].name).second)
		{
			functions.push_back(std::move(program.function_declarations[i]));
			if (stats) ++stats->retained_declarations;
		}
	}
	program.function_declarations.swap(functions);

	std::unordered_map<std::string, std::size_t> function_index;
	function_index.reserve(program.functions.size());
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		function_index[program.functions[i].name] = i;
	std::vector<std::size_t> order;
	order.reserve(program.functions.size());
	std::vector<unsigned char> queued(program.functions.size(), 0);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		if (program.functions[i].metadata.binding != SBM_WEAK)
		{
			queued[i] = 1;
			order.push_back(i);
		}
	for (std::size_t cursor = 0; cursor < order.size(); ++cursor)
	{
		const Function& function = program.functions[order[cursor]];
		for (std::size_t b = 0; b < function.blocks.size(); ++b)
			for (std::size_t j = 0;
				j < function.blocks[b].instructions.size(); ++j)
			{
				if (stats) ++stats->function_order_visits;
				const Instruction& ins = function.blocks[b].instructions[j];
				if (ins.kind != Instruction::IK_CALL ||
					ins.first.kind != Operand::OP_GLOBAL) continue;
				const std::unordered_map<std::string, std::size_t>::const_iterator
					found = function_index.find(ins.first.text);
				if (found != function_index.end() && !queued[found->second])
				{
					queued[found->second] = 1;
					order.push_back(found->second);
				}
			}
	}
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		if (!queued[i]) order.push_back(i);
	std::vector<Function> ordered_functions;
	ordered_functions.reserve(program.functions.size());
	for (std::size_t i = 0; i < order.size(); ++i)
	{
		ordered_functions.push_back(std::move(program.functions[order[i]]));
		if (stats) ++stats->function_moves;
	}
	program.functions.swap(ordered_functions);

	std::vector<ObjectAlias> ordered_aliases;
	ordered_aliases.reserve(program.object_aliases.size());
	std::unordered_map<std::string, std::vector<std::size_t> > aliases_by_target;
	for (std::size_t i = 0; i < program.object_aliases.size(); ++i)
	{
		aliases_by_target[program.object_aliases[i].target].push_back(i);
		if (stats) ++stats->alias_order_visits;
	}
	std::vector<unsigned char> alias_used(program.object_aliases.size(), 0);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
	{
		const std::unordered_map<std::string,
			std::vector<std::size_t> >::const_iterator found =
			aliases_by_target.find(program.functions[i].name);
		if (found == aliases_by_target.end()) continue;
		for (std::size_t j = 0; j < found->second.size(); ++j)
		{
			const std::size_t alias = found->second[j];
			alias_used[alias] = 1;
			ordered_aliases.push_back(std::move(program.object_aliases[alias]));
			if (stats) ++stats->alias_moves;
		}
	}
	for (std::size_t i = 0; i < program.object_aliases.size(); ++i)
		if (!alias_used[i])
		{
			ordered_aliases.push_back(std::move(program.object_aliases[i]));
			if (stats) ++stats->alias_moves;
		}
	program.object_aliases.swap(ordered_aliases);

	for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
		canonicalize_frontend_symbol(program.global_declarations[i].name,
			&program.global_declarations[i].metadata);
	for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
		canonicalize_frontend_symbol(program.function_declarations[i].name,
			&program.function_declarations[i].metadata);
	for (std::size_t i = 0; i < program.globals.size(); ++i)
		canonicalize_frontend_symbol(program.globals[i].name,
			&program.globals[i].metadata);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		canonicalize_frontend_symbol(program.functions[i].name,
			&program.functions[i].metadata);
	if (stats)
		stats->frontend_canonical_nanoseconds += elapsed_nanoseconds(started);
}

void canonicalize_serialized_lowir_facts(Program& program,
	LowirPreparationStats* stats)
{
	const Clock::time_point started = Clock::now();
	for (std::size_t i = 0; i < program.globals.size(); ++i)
	{
		if (program.globals[i].structured)
			program.globals[i].type = LowType();
		clear_serialized_operand_type(program.globals[i].init_operand, stats);
		for (std::size_t j = 0; j < program.globals[i].data_items.size(); ++j)
			clear_serialized_operand_type(
				program.globals[i].data_items[j].literal_operand, stats);
	}
	for (std::size_t f = 0; f < program.functions.size(); ++f)
		for (std::size_t b = 0; b < program.functions[f].blocks.size(); ++b)
			for (std::size_t i = 0;
				i < program.functions[f].blocks[b].instructions.size(); ++i)
			{
				Instruction& instruction =
					program.functions[f].blocks[b].instructions[i];
				clear_serialized_operand_type(instruction.first, stats);
				clear_serialized_operand_type(instruction.second, stats);
				clear_serialized_operand_type(instruction.third, stats);
				for (std::size_t j = 0; j < instruction.args.size(); ++j)
					clear_serialized_operand_type(instruction.args[j], stats);
				if (instruction.kind == Instruction::IK_COPYOBJ ||
					instruction.kind == Instruction::IK_ZEROINIT ||
					instruction.kind == Instruction::IK_VA_START)
					instruction.type = LowType();
				if (instruction.kind == Instruction::IK_EH_CLEANUP_CLAUSE)
					instruction.first = Operand();
				else if (instruction.kind == Instruction::IK_EH_CATCH)
					instruction.second = Operand();
				else if (instruction.kind == Instruction::IK_EH_CATCH_ALL)
					instruction.first = Operand();
				else if (instruction.kind == Instruction::IK_EH_FILTER &&
					!instruction.has_eh_selector && !instruction.args.empty() &&
					instruction.args.back().has_int_value)
				{
					// Text LowIR spells the filter selector as the last operand,
					// while typed LowIR carries it in the dedicated selector field.
					// Use the typed representation internally so native lowering
					// cannot mistake a negative selector for an RTTI symbol.
					instruction.has_eh_selector = true;
					instruction.eh_selector = instruction.args.back().int_value;
					instruction.args.pop_back();
				}
			}
	if (stats)
		stats->serialized_canonical_nanoseconds += elapsed_nanoseconds(started);
}

void propagate_direct_call_boundaries(Program& program,
	LowirPreparationStats* stats)
{
	std::unordered_map<std::string, FunctionBoundaryMetadata> boundaries;
	boundaries.reserve(program.function_declarations.size() +
		program.functions.size());
	for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
		boundaries[program.function_declarations[i].name] =
			program.function_declarations[i].boundary;
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		boundaries[program.functions[i].name] = program.functions[i].boundary;
	for (std::size_t f = 0; f < program.functions.size(); ++f)
		for (std::size_t b = 0; b < program.functions[f].blocks.size(); ++b)
			for (std::size_t i = 0;
				i < program.functions[f].blocks[b].instructions.size(); ++i)
			{
				Instruction& ins =
					program.functions[f].blocks[b].instructions[i];
				if (ins.kind != Instruction::IK_CALL ||
					ins.has_call_signature ||
					ins.first.kind != Operand::OP_GLOBAL) continue;
				if (stats) ++stats->boundary_call_visits;
				const std::unordered_map<std::string,
					FunctionBoundaryMetadata>::const_iterator found =
					boundaries.find(ins.first.text);
				if (found != boundaries.end()) ins.call_boundary = found->second;
			}
}

void derive_lowir_object_facts(Program& program,
	LowirPreparationStats* stats)
{
	const Clock::time_point started = Clock::now();
	std::unordered_set<std::string> local_definitions;
	local_definitions.reserve(program.globals.size() + program.functions.size());
	for (std::size_t i = 0; i < program.globals.size(); ++i)
		if (program.globals[i].metadata.binding != SBM_WEAK)
			local_definitions.insert(program.globals[i].name);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		if (program.functions[i].metadata.binding != SBM_WEAK)
			local_definitions.insert(program.functions[i].name);
	for (std::size_t i = 0; i < program.globals.size(); ++i)
	{
		restore_address_binding(
			program.globals[i].init_operand, local_definitions, stats);
		for (std::size_t j = 0; j < program.globals[i].data_items.size(); ++j)
			restore_address_binding(
				program.globals[i].data_items[j].literal_operand,
				local_definitions, stats);
	}
	for (std::size_t f = 0; f < program.functions.size(); ++f)
		for (std::size_t b = 0; b < program.functions[f].blocks.size(); ++b)
			for (std::size_t i = 0;
				i < program.functions[f].blocks[b].instructions.size(); ++i)
			{
				Instruction& instruction =
					program.functions[f].blocks[b].instructions[i];
				restore_address_binding(
					instruction.first, local_definitions, stats);
				restore_address_binding(
					instruction.second, local_definitions, stats);
				restore_address_binding(
					instruction.third, local_definitions, stats);
				for (std::size_t j = 0; j < instruction.args.size(); ++j)
					restore_address_binding(
						instruction.args[j], local_definitions, stats);
			}
	propagate_direct_call_boundaries(program, stats);
	program.exported_symbols.clear();
	derive_exports(program);
	if (stats)
	{
		stats->exports += program.exported_symbols.size();
		stats->derived_facts_nanoseconds += elapsed_nanoseconds(started);
	}
}

void finalize_lowir_object_model(Program& program,
	LowirPreparationStats* stats)
{
	canonicalize_serialized_lowir_facts(program, stats);
	derive_lowir_object_facts(program, stats);
}

}  // namespace lowir_model
