#include "compiler_object/linker.h"
#include "compiler_object/errors.h"
#include "support/exception_types.h"
#include "lowir/io/prepare.h"

#include <chrono>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppgm
{
namespace compiler_object
{
namespace
{

typedef std::unordered_map<std::string, std::string> RenameMap;

void RenameStringId(lowir_model::LowirProgram* program,
	lowir_model::StringId* value, const RenameMap& names, LinkStats* stats)
{
	if (!value->valid()) return;
	const std::string& spelling = program->strings.get(*value);
	if (stats) ++stats->rename_probes;
	const RenameMap::const_iterator found = names.find(spelling);
	if (found != names.end()) *value = program->strings.intern(found->second);
}

void RenameOperand(lowir_model::LowirProgram* program,
	lowir_model::Operand* value, const RenameMap& names, LinkStats* stats)
{
	if (value->kind != lowir_model::Operand::OP_GLOBAL ||
		!value->has_spelling) return;
	const std::string& spelling = program->strings.get(value->literal);
	if (stats) ++stats->rename_probes;
	const RenameMap::const_iterator found = names.find(spelling);
	if (found != names.end())
		value->literal = program->strings.intern(found->second);
}

void RenameProgram(lowir_model::LowirProgram* program,
	const RenameMap& names, LinkStats* stats)
{
	for (std::size_t i = 0; i < program->symbol_names.size(); ++i)
		RenameStringId(program, &program->symbol_names[i], names, stats);
	for (std::size_t i = 0; i < program->globals.size(); ++i)
	{
		lowir_model::GlobalDefinition& item = program->globals[i];
		RenameOperand(program, &item.init_operand, names, stats);
		for (std::size_t j = 0; j < item.data_items.size(); ++j)
		{
			RenameOperand(program, &item.data_items[j].literal_operand,
				names, stats);
		}
	}
	for (std::size_t i = 0; i < program->functions.size(); ++i)
	{
		lowir_model::Function& item = program->functions[i];
		for (std::size_t j = 0; j < item.blocks.size(); ++j)
			for (std::size_t k = 0; k < item.blocks[j].instructions.size(); ++k)
			{
				lowir_model::Instruction& instruction =
					item.blocks[j].instructions[k];
				RenameOperand(program, &instruction.first, names, stats);
				RenameOperand(program, &instruction.second, names, stats);
				RenameOperand(program, &instruction.third, names, stats);
				for (std::size_t a = 0; a < instruction.args.size(); ++a)
					RenameOperand(program, &instruction.args[a], names, stats);
			}
	}
}

lowir_model::Function MakeLifecycleAggregate(lowir_model::LowirProgram& program,
	const std::string& name,
	lowir_model::SymbolRole role,
	const std::vector<lowir_model::SymbolId>& functions,
	bool reverse)
{
	lowir_model::Function result;
	result.symbol = lowir_model::append_lowir_symbol(program, name);
	result.return_type = lowir_model::builtin_lowir_type(lowir_model::LTK_VOID);
	result.metadata.role = role;
	result.metadata.binding = lowir_model::SBM_INTERNAL;
	lowir_model::Block block;
	block.id = lowir_model::allocate_lowir_block_id(
		result, program.presentation_policy ==
			lowir_model::PRESENTATION_SERIALIZABLE ?
			program.strings.intern("entry") : lowir_model::StringId());
	for (std::size_t i = 0; i < functions.size(); ++i)
	{
		const std::size_t index = reverse ? functions.size() - i - 1 : i;
		lowir_model::Instruction call;
		call.kind = lowir_model::Instruction::IK_CALL;
		call.call_returns_void = true;
		call.type = lowir_model::builtin_lowir_type(lowir_model::LTK_VOID);
		call.first.kind = lowir_model::Operand::OP_GLOBAL;
		call.first.symbol = functions[index];
		block.instructions.push_back(call);
	}
	lowir_model::Instruction ret;
	ret.kind = lowir_model::Instruction::IK_RETURN;
	ret.type = lowir_model::builtin_lowir_type(lowir_model::LTK_VOID);
	block.instructions.push_back(ret);
	result.blocks.push_back(block);
	return result;
}

bool IsWeak(lowir_model::SymbolBindingMode binding)
{
	return binding == lowir_model::SBM_WEAK;
}

}

LinkStats::LinkStats()
	: objects(0), symbols(0), symbol_probes(0), rename_probes(0),
	  definitions(0), coalesced_weak_definitions(0), link_nanoseconds(0) {}

lowir_model::LowirProgram Link(
	std::vector<Object> objects, const std::string& target,
	lowir_model::PresentationPolicy presentation_policy,
	LinkStats* stats)
{
	if (objects.empty()) throw InvocationError("no linker inputs");
	if (stats) *stats = LinkStats();
	std::chrono::steady_clock::time_point started;
	if (stats) started = std::chrono::steady_clock::now();
	std::unordered_map<std::string, std::string> external_names;
	std::vector<lowir_model::SymbolId> linked_symbols(1);
	lowir_model::LowirProgram result;
	result.presentation_policy = presentation_policy;
	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		if (objects[i].target != target)
			throw InvocationError("link input target mismatch");
		RenameMap names;
		for (std::size_t j = 0; j < objects[i].lowir.exported_symbols.size(); ++j)
		{
			const lowir_model::ExportedSymbol& symbol =
				objects[i].lowir.exported_symbols[j];
			const std::string& internal_symbol = lowir_model::lowir_symbol_name(
				objects[i].lowir, symbol.internal_symbol);
			const std::string object_symbol = symbol.object_symbol.valid() ?
				objects[i].lowir.strings.get(symbol.object_symbol) : std::string();
			if (stats) { ++stats->symbols; ++stats->symbol_probes; }
			if (symbol.linkage == ir_model::SL_INTERNAL ||
				symbol.prefer_local_object_binding)
				names[internal_symbol] = internal_symbol +
					".__u" + std::to_string(i);
			else
			{
				const std::string key = object_symbol.empty() ?
					internal_symbol : object_symbol;
				const std::pair<std::unordered_map<std::string, std::string>::iterator,
					bool> inserted = external_names.emplace(key,
						internal_symbol);
				names[internal_symbol] = inserted.first->second;
			}
		}
		RenameProgram(&objects[i].lowir, names, stats);
		std::vector<lowir_model::SymbolId> symbol_remap(
			objects[i].lowir.symbol_names.size());
		for (std::size_t j = 0; j < symbol_remap.size(); ++j)
		{
			const std::string& spelling = lowir_model::lowir_symbol_name(
				objects[i].lowir,
				lowir_model::SymbolId(static_cast<std::uint32_t>(j)));
			const lowir_model::StringId name = result.strings.intern(spelling);
			const std::uint32_t name_id = name;
			if (name_id >= linked_symbols.size())
				linked_symbols.resize(name_id + 1);
			if (!linked_symbols[name_id].valid())
				linked_symbols[name_id] =
					lowir_model::append_lowir_symbol(result, name);
			symbol_remap[j] = linked_symbols[name_id];
		}
		lowir_model::remap_lowir_program_symbols(
			objects[i].lowir, symbol_remap);
	}
	// Intern all linked symbol spellings first.  Their StringIds are therefore
	// dense, and the symbol resolver above never needs a string-keyed side map.
	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		lowir_model::remap_lowir_program_strings(
			objects[i].lowir, result.strings);
		std::vector<lowir_model::ExportedSymbol>().swap(
			objects[i].lowir.exported_symbols);
	}

	const std::size_t no_definition = std::numeric_limits<std::size_t>::max();
	std::vector<std::size_t> globals(result.symbol_names.size(), no_definition);
	std::vector<std::size_t> functions(result.symbol_names.size(), no_definition);
	std::vector<lowir_model::SymbolId> initializers;
	std::vector<lowir_model::SymbolId> finalizers;
	for (std::size_t unit = 0; unit < objects.size(); ++unit)
	{
		lowir_model::LowirProgram& program = objects[unit].lowir;
		result.source_bytes += program.source_bytes;
		result.token_count += program.token_count;
		for (std::size_t i = 0; i < program.globals.size(); ++i)
		{
			lowir_model::GlobalDefinition& item = program.globals[i];
			if (stats) { ++stats->definitions; ++stats->symbol_probes; }
			std::size_t& found = globals[item.symbol];
			if (found == no_definition)
			{
				found = result.globals.size();
				result.globals.push_back(std::move(item));
			}
			else if (IsWeak(item.metadata.binding))
			{
				if (stats) ++stats->coalesced_weak_definitions;
			}
			else if (IsWeak(result.globals[found].metadata.binding))
			{
				result.globals[found] = std::move(item);
				if (stats) ++stats->coalesced_weak_definitions;
			}
			else ThrowCompilerObjectInputError("duplicate global definition: " +
				lowir_model::lowir_symbol_name(result, item.symbol));
		}
		for (std::size_t i = 0; i < program.functions.size(); ++i)
		{
			lowir_model::Function& item = program.functions[i];
			if (item.metadata.role == lowir_model::SR_INIT)
			{
				initializers.push_back(item.symbol);
				item.metadata.role = lowir_model::SR_NONE;
			}
			else if (item.metadata.role == lowir_model::SR_FINI)
			{
				finalizers.push_back(item.symbol);
				item.metadata.role = lowir_model::SR_NONE;
			}
			if (stats) { ++stats->definitions; ++stats->symbol_probes; }
			std::size_t& found = functions[item.symbol];
			if (found == no_definition)
			{
				found = result.functions.size();
				result.functions.push_back(std::move(item));
			}
			else if (IsWeak(item.metadata.binding))
			{
				if (stats) ++stats->coalesced_weak_definitions;
			}
			else if (IsWeak(result.functions[found].metadata.binding))
			{
				result.functions[found] = std::move(item);
				if (stats) ++stats->coalesced_weak_definitions;
			}
			else ThrowCompilerObjectInputError("duplicate function definition: " +
				lowir_model::lowir_symbol_name(result, item.symbol));
		}
		result.object_aliases.insert(result.object_aliases.end(),
			std::make_move_iterator(program.object_aliases.begin()),
			std::make_move_iterator(program.object_aliases.end()));
	}

	std::vector<unsigned char> declared_globals(result.symbol_names.size(), 0);
	std::vector<unsigned char> declared_functions(result.symbol_names.size(), 0);
	for (std::size_t unit = 0; unit < objects.size(); ++unit)
	{
		lowir_model::LowirProgram& program = objects[unit].lowir;
		for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
			if (globals[program.global_declarations[i].symbol] == no_definition &&
				!declared_globals[program.global_declarations[i].symbol])
			{
				declared_globals[program.global_declarations[i].symbol] = 1;
				result.global_declarations.push_back(
					std::move(program.global_declarations[i]));
			}
		for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
			if (functions[program.function_declarations[i].symbol] == no_definition &&
				!declared_functions[program.function_declarations[i].symbol])
			{
				declared_functions[program.function_declarations[i].symbol] = 1;
				result.function_declarations.push_back(
					std::move(program.function_declarations[i]));
			}
	}
	if (!initializers.empty())
		result.functions.push_back(MakeLifecycleAggregate(
			result, "__cppgm_link_init", lowir_model::SR_INIT,
			initializers, false));
	if (!finalizers.empty())
		result.functions.push_back(MakeLifecycleAggregate(
			result, "__cppgm_link_fini", lowir_model::SR_FINI,
			finalizers, true));

	std::vector<lowir_model::SymbolId> aliases(result.strings.size() + 1);
	std::vector<lowir_model::ObjectAlias> unique_aliases;
	for (std::size_t i = 0; i < result.object_aliases.size(); ++i)
	{
		const lowir_model::ObjectAlias& alias = result.object_aliases[i];
		const std::uint32_t spelling = alias.object_symbol;
		if (!alias.object_symbol.valid() || spelling >= aliases.size())
			ThrowCompilerObjectInternalError("invalid linked object alias spelling");
		if (!aliases[spelling].valid())
		{
			aliases[spelling] = alias.target_id;
			unique_aliases.push_back(alias);
		}
		else if (aliases[spelling] != alias.target_id)
			ThrowCompilerObjectInputError("conflicting object alias: " +
				result.strings.get(alias.object_symbol));
	}
	result.object_aliases.swap(unique_aliases);
	lowir_model::finalize_lowir_object_model(result);
	if (stats)
	{
		stats->objects = objects.size();
		stats->link_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count());
	}
	return result;
}

}
}
