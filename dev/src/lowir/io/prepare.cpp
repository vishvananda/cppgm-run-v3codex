#include "lowir/io/prepare.h"
#include "lowir/model/operand_view.h"

#include <algorithm>
#include <chrono>
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
	std::vector<unsigned char>* referenced,
	LowirPreparationStats* stats)
{
	if (stats) ++stats->reference_operand_visits;
	if (operand.kind == Operand::OP_GLOBAL)
		(*referenced)[operand.symbol] = 1;
}

void canonicalize_frontend_symbol(const Program& program, SymbolId symbol,
	SymbolMetadata* metadata)
{
	if (metadata->binding == SBM_INTERNAL ||
		!metadata->object_symbol.valid()) return;
	if (lowir_symbol_spelling(program, symbol) == metadata->object_symbol)
		metadata->object_symbol = StringId();
}

ir_model::SymbolLinkage exported_linkage(SymbolBindingMode binding)
{
	return binding == SBM_INTERNAL ? ir_model::SL_INTERNAL :
		binding == SBM_WEAK ? ir_model::SL_WEAK : ir_model::SL_EXTERNAL;
}

void append_export(Program& program, SymbolId symbol,
	const SymbolMetadata& metadata)
{
	ExportedSymbol result;
	result.internal_symbol = symbol;
	if (metadata.object_symbol.valid())
		result.object_symbol = metadata.object_symbol;
	// Canonical LowIR omits a redundant external object= spelling whenever it
	// is exactly the LowIR name without its sigil.  Reconstruct that derived
	// spelling at the object boundary; this also covers ordinary namespace-
	// scope data, whose Itanium object name need not carry a _Z prefix.
	if (!result.object_symbol.valid() && metadata.binding != SBM_INTERNAL)
		result.object_symbol = lowir_symbol_spelling(program, symbol);
	result.keep_internal_alias = metadata.keep_internal_alias;
	result.prefer_local_object_binding = metadata.prefer_local_object_binding;
	result.linkage = exported_linkage(metadata.binding);
	program.exported_symbols.push_back(result);
}

void derive_exports(Program& program)
{
	std::vector<ir_model::SymbolLinkage> linkage(
		program.symbol_names.size(), ir_model::SL_EXTERNAL);
	for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
		linkage[program.global_declarations[i].symbol] =
			exported_linkage(program.global_declarations[i].metadata.binding);
	for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
		linkage[program.function_declarations[i].symbol] =
			exported_linkage(program.function_declarations[i].metadata.binding);
	for (std::size_t i = 0; i < program.globals.size(); ++i)
		linkage[program.globals[i].symbol] =
			exported_linkage(program.globals[i].metadata.binding);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		linkage[program.functions[i].symbol] =
			exported_linkage(program.functions[i].metadata.binding);
	for (std::size_t i = 0; i < program.object_aliases.size(); ++i)
	{
		const ObjectAlias& object_alias = program.object_aliases[i];
		ExportedSymbol alias;
		alias.internal_symbol = object_alias.target_id;
		alias.object_symbol = object_alias.object_symbol;
		alias.linkage = linkage[object_alias.target_id];
		program.exported_symbols.push_back(alias);
	}
	for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
		append_export(program, program.global_declarations[i].symbol,
			program.global_declarations[i].metadata);
	for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
		append_export(program, program.function_declarations[i].symbol,
			program.function_declarations[i].metadata);
	for (std::size_t i = 0; i < program.globals.size(); ++i)
		append_export(program, program.globals[i].symbol,
			program.globals[i].metadata);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		append_export(program, program.functions[i].symbol,
			program.functions[i].metadata);
}

void clear_serialized_operand_facts(Operand& operand,
	LowirPreparationStats* stats)
{
	if (stats) ++stats->serialized_operand_visits;
	operand.address_binding = Operand::ADDRESS_LOCAL;
}

void restore_address_binding(Operand& operand,
	const std::vector<unsigned char>& local,
	LowirPreparationStats* stats)
{
	if (stats) ++stats->derived_operand_visits;
	if (operand.kind != Operand::OP_GLOBAL) return;
	operand.address_binding = local[operand.symbol] ?
		Operand::ADDRESS_LOCAL : Operand::ADDRESS_PREEMPTIBLE;
}

}  // namespace

void canonicalize_frontend_lowir(Program& program,
	LowirPreparationStats* stats)
{
	const Clock::time_point started = Clock::now();
	std::vector<unsigned char> referenced(program.symbol_names.size(), 0);
	for (std::size_t i = 0; i < program.globals.size(); ++i)
	{
		const GlobalDefinition& global = program.globals[i];
		note_operand_reference(global.init_operand, &referenced, stats);
		for (std::size_t j = 0; j < global.data_items.size(); ++j)
		{
			if (stats) ++stats->reference_operand_visits;
			if (global.data_items[j].kind ==
				GlobalDefinition::DataItem::ITEM_ADDR)
				referenced[global.data_items[j].symbol_id] = 1;
		}
	}
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		for (std::size_t b = 0; b < program.functions[i].blocks.size(); ++b)
			for (std::size_t j = 0;
				j < program.functions[i].blocks[b].instructions.size(); ++j)
			{
				const Instruction& ins =
					program.functions[i].blocks[b].instructions[j];
				for (std::size_t k = 0;
					k < operand_view::all_operand_count(ins); ++k)
					note_operand_reference(
						operand_view::all_operand_at(ins, k), &referenced, stats);
			}
	// A TLS wrapper's target is a semantic symbol edge even when no ordinary
	// instruction names the variable directly.  It must participate in the
	// same declaration liveness calculation as operand references.
	for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
		if (program.function_declarations[i].metadata.tls_for_symbol_id.valid())
			referenced[program.function_declarations[i].metadata.tls_for_symbol_id] = 1;
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		if (program.functions[i].metadata.tls_for_symbol_id.valid())
			referenced[program.functions[i].metadata.tls_for_symbol_id] = 1;
	if (stats) stats->referenced_symbols +=
		std::count(referenced.begin(), referenced.end(), 1);

	std::vector<GlobalDeclaration> globals;
	globals.reserve(program.global_declarations.size());
	std::vector<unsigned char> retained_global_declarations(
		program.symbol_names.size(), 0);
	for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
	{
		if (stats) ++stats->declaration_visits;
		const SymbolId symbol = program.global_declarations[i].symbol;
		if (referenced[symbol] && !retained_global_declarations[symbol])
		{
			retained_global_declarations[symbol] = 1;
			globals.push_back(std::move(program.global_declarations[i]));
			if (stats) ++stats->retained_declarations;
		}
	}
	program.global_declarations.swap(globals);

	std::vector<FunctionDeclaration> functions;
	functions.reserve(program.function_declarations.size());
	std::vector<unsigned char> retained_function_declarations(
		program.symbol_names.size(), 0);
	for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
	{
		if (stats) ++stats->declaration_visits;
		const SymbolId symbol = program.function_declarations[i].symbol;
		if ((referenced[symbol] ||
			 program.function_declarations[i].metadata.tls_for_symbol_id.valid()) &&
			!retained_function_declarations[symbol])
		{
			retained_function_declarations[symbol] = 1;
			functions.push_back(std::move(program.function_declarations[i]));
			if (stats) ++stats->retained_declarations;
		}
	}
	program.function_declarations.swap(functions);

	std::vector<ObjectAlias> ordered_aliases;
	ordered_aliases.reserve(program.object_aliases.size());
	std::vector<std::vector<std::size_t> > aliases_by_target(
		program.symbol_names.size());
	for (std::size_t i = 0; i < program.object_aliases.size(); ++i)
	{
		aliases_by_target[program.object_aliases[i].target_id].push_back(i);
		if (stats) ++stats->alias_order_visits;
	}
	std::vector<unsigned char> alias_used(program.object_aliases.size(), 0);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
	{
		const std::vector<std::size_t>& found =
			aliases_by_target[program.functions[i].symbol];
		for (std::size_t j = 0; j < found.size(); ++j)
		{
			const std::size_t alias = found[j];
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
		canonicalize_frontend_symbol(program,
			program.global_declarations[i].symbol,
			&program.global_declarations[i].metadata);
	for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
		canonicalize_frontend_symbol(program,
			program.function_declarations[i].symbol,
			&program.function_declarations[i].metadata);
	for (std::size_t i = 0; i < program.globals.size(); ++i)
		canonicalize_frontend_symbol(program, program.globals[i].symbol,
			&program.globals[i].metadata);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		canonicalize_frontend_symbol(program, program.functions[i].symbol,
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
		clear_serialized_operand_facts(program.globals[i].init_operand, stats);
		for (std::size_t j = 0; j < program.globals[i].data_items.size(); ++j)
			clear_serialized_operand_facts(
				program.globals[i].data_items[j].literal_operand, stats);
	}
	for (std::size_t f = 0; f < program.functions.size(); ++f)
		for (std::size_t b = 0; b < program.functions[f].blocks.size(); ++b)
			for (std::size_t i = 0;
				i < program.functions[f].blocks[b].instructions.size(); ++i)
			{
				Instruction& instruction =
					program.functions[f].blocks[b].instructions[i];
				for (std::size_t j = 0;
					j < operand_view::all_operand_count(instruction); ++j)
					clear_serialized_operand_facts(
						operand_view::all_operand_at(instruction, j), stats);
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
	std::vector<FunctionBoundaryMetadata> boundaries(program.symbol_names.size());
	std::vector<unsigned char> known(program.symbol_names.size(), 0);
	for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
	{
		const SymbolId symbol = program.function_declarations[i].symbol;
		boundaries[symbol] = program.function_declarations[i].boundary;
		known[symbol] = 1;
	}
	for (std::size_t i = 0; i < program.functions.size(); ++i) {
		const SymbolId symbol = program.functions[i].symbol;
		boundaries[symbol] = program.functions[i].boundary;
		known[symbol] = 1;
	}
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
				if (known[ins.first.symbol])
					ins.call_boundary = boundaries[ins.first.symbol];
			}
}

void derive_lowir_object_facts(Program& program,
	LowirPreparationStats* stats)
{
	const Clock::time_point started = Clock::now();
	std::vector<unsigned char> local_definitions(program.symbol_names.size(), 0);
	for (std::size_t i = 0; i < program.globals.size(); ++i)
		if (program.globals[i].metadata.binding != SBM_WEAK)
			local_definitions[program.globals[i].symbol] = 1;
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		if (program.functions[i].metadata.binding != SBM_WEAK)
			local_definitions[program.functions[i].symbol] = 1;
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
				for (std::size_t j = 0;
					j < operand_view::all_operand_count(instruction); ++j)
					restore_address_binding(
						operand_view::all_operand_at(instruction, j),
						local_definitions, stats);
			}
	propagate_direct_call_boundaries(program, stats);
	publish_prederived_lowir_object_facts(program, stats);
	if (stats)
		stats->derived_facts_nanoseconds += elapsed_nanoseconds(started);
}

void publish_prederived_lowir_object_facts(Program& program,
	LowirPreparationStats* stats)
{
	program.exported_symbols.clear();
	derive_exports(program);
	if (stats)
	{
		stats->exports += program.exported_symbols.size();
	}
}

void finalize_lowir_object_model(Program& program,
	LowirPreparationStats* stats)
{
	canonicalize_serialized_lowir_facts(program, stats);
	derive_lowir_object_facts(program, stats);
}

}  // namespace lowir_model
