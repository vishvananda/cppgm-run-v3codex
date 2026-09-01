#include "lowering/core/reachability.h"

#include "lowering/support/errors.h"
#include "semantic/lifetime/demand_reason.h"

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace cppgm
{
namespace lowering
{
namespace reachability
{
namespace
{

using namespace lowering::ir;

const std::size_t kNoFunction = std::numeric_limits<std::size_t>::max();

enum RetentionReason
{
	RETENTION_EXTERNAL_STRONG = 1U << 0,
	RETENTION_ADDRESS_OR_RELOCATION = 1U << 1,
	RETENTION_DIRECT_CALL = 1U << 2,
	RETENTION_LIFECYCLE = 1U << 3,
	RETENTION_EH_OR_RUNTIME = 1U << 4,
	RETENTION_REQUIRED_WEAK = 1U << 5,
	RETENTION_CONSERVATIVE_FALLBACK = 1U << 6,
	RETENTION_OBJECT_OUTPUT_ROOT = 1U << 7
};

std::uint16_t semantic_retention_reasons(const Symbol& symbol)
{
	using namespace semantic;
	const std::uint16_t demand = symbol.demand_reason_mask;
	std::uint16_t result = 0;
	if (demand & (FunctionDemandReasonMask(FUNCTION_DEMAND_LIFECYCLE) |
		FunctionDemandReasonMask(FUNCTION_DEMAND_VTABLE) |
		FunctionDemandReasonMask(FUNCTION_DEMAND_STATIC_LIFECYCLE)))
		result |= RETENTION_LIFECYCLE;
	if (demand & (FunctionDemandReasonMask(FUNCTION_DEMAND_EXCEPTION_CLEANUP) |
		FunctionDemandReasonMask(FUNCTION_DEMAND_ABI_SUPPORT)))
		result |= RETENTION_EH_OR_RUNTIME;
	if (demand & FunctionDemandReasonMask(FUNCTION_DEMAND_ADDRESS))
		result |= RETENTION_ADDRESS_OR_RELOCATION;
	if (demand & (FunctionDemandReasonMask(FUNCTION_DEMAND_EVALUATED_USE) |
		FunctionDemandReasonMask(FUNCTION_DEMAND_RETAINED_CALL)))
		result |= RETENTION_DIRECT_CALL;
	if (demand & FunctionDemandReasonMask(
		FUNCTION_DEMAND_EXPLICIT_INSTANTIATION))
		result |= RETENTION_REQUIRED_WEAK;
	return result;
}

class Analyzer
{
public:
	Analyzer(const lowering::ir::Program& program, bool retain_internal_roots)
		: program_(program), function_by_symbol_(program.symbols.size(),
			kNoFunction), reachable_(program.functions.size(), 0),
			retention_reasons_(program.functions.size(), 0),
			retain_internal_roots_(retain_internal_roots)
	{
		for (std::size_t i = 0; i < program_.functions.size(); ++i)
		{
			const std::uint32_t symbol = program_.functions[i].symbol;
			if (symbol >= function_by_symbol_.size())
				ThrowLoweringInternal(
					"function reachability symbol is out of bounds");
			if (function_by_symbol_[symbol] != kNoFunction)
				ThrowLoweringInternal(
					"function reachability has duplicate definitions");
			function_by_symbol_[symbol] = i;
		}
	}

	Summary Run()
	{
		MarkRoots();
		for (std::size_t cursor = 0; cursor < pending_.size(); ++cursor)
			VisitFunction(pending_[cursor]);
		Summary result;
		result.reachable_functions = pending_.size();
		for (std::size_t i = 0; i < program_.functions.size(); ++i)
		{
			const std::uint32_t symbol = program_.functions[i].symbol;
			if (!reachable_[i] && program_.symbols[symbol].weak_linkage)
				++result.unreachable_weak_functions;
			if (!reachable_[i] && program_.symbols[symbol].internal_linkage)
			{
				++result.unreachable_internal_functions;
				const Symbol& internal = program_.symbols[symbol];
				const lowir_model::StringId presentation =
					internal.object_name.valid() ? internal.object_name :
						internal.name;
				result.unreachable_internal_names.push_back(
					program_.strings.get(presentation));
			}
		}
		ClassifyRetainedFunctions(&result);
		return result;
	}

	bool Reachable(std::size_t function) const
	{
		return function < reachable_.size() && reachable_[function] != 0;
	}

private:
	const lowering::ir::Program& program_;
	std::vector<std::size_t> function_by_symbol_;
	std::vector<unsigned char> reachable_;
	std::vector<std::uint16_t> retention_reasons_;
	std::vector<std::size_t> pending_;
	bool retain_internal_roots_;

	void MarkSymbol(SymbolId symbol, std::uint16_t reason)
	{
		const std::uint32_t id = symbol;
		if (id == kNoLowId || id >= function_by_symbol_.size()) return;
		const std::size_t function = function_by_symbol_[id];
		if (function == kNoFunction) return;
		retention_reasons_[function] |= reason;
		if (reachable_[function]) return;
		reachable_[function] = 1;
		pending_.push_back(function);
	}

	void MarkOperand(const Operand& operand, std::uint16_t reason)
	{
		if (operand.kind == Operand::FUNCTION || operand.kind == Operand::GLOBAL)
			MarkSymbol(SymbolId(operand.id), reason);
	}

	void MarkGlobalReferences()
	{
		for (std::size_t i = 0; i < program_.globals.size(); ++i)
		{
			const Global& global = program_.globals[i];
			if (global.initializer_kind == Global::ADDRESS_VALUE)
				MarkSymbol(global.address_symbol,
					RETENTION_ADDRESS_OR_RELOCATION);
			for (std::size_t j = 0; j < global.items.size(); ++j)
				if (global.items[j].kind == Global::DataItem::ADDRESS_ITEM)
					MarkSymbol(global.items[j].symbol,
						RETENTION_ADDRESS_OR_RELOCATION);
		}
		for (std::size_t i = 0; i < program_.exception_filter_types.size(); ++i)
			MarkSymbol(program_.exception_filter_types[i],
				RETENTION_EH_OR_RUNTIME);
	}

	void MarkRoots()
	{
		for (std::size_t i = 0; i < program_.functions.size(); ++i)
		{
			const Function& function = program_.functions[i];
			const Symbol& symbol = program_.symbols[function.symbol];
			if (!symbol.weak_linkage &&
				(!symbol.internal_linkage || retain_internal_roots_))
				MarkSymbol(function.symbol, symbol.internal_linkage ?
					RETENTION_CONSERVATIVE_FALLBACK :
					RETENTION_EXTERNAL_STRONG);
			const std::uint16_t semantic_reasons =
				semantic_retention_reasons(symbol);
			if (symbol.object_output_root)
				MarkSymbol(function.symbol,
					RETENTION_OBJECT_OUTPUT_ROOT | semantic_reasons);
			if (symbol.internal_linkage && semantic_reasons != 0)
				MarkSymbol(function.symbol, semantic_reasons);
			if (function.entry || function.initializer || function.finalizer ||
				symbol.tls_for_symbol != kNoLowId)
				MarkSymbol(function.symbol, RETENTION_LIFECYCLE);
		}
		MarkGlobalReferences();
		MarkSymbol(program_.terminate_runtime_symbol, RETENTION_EH_OR_RUNTIME);
		MarkSymbol(program_.terminate_helper_symbol, RETENTION_EH_OR_RUNTIME);
		MarkSymbol(program_.call_unexpected_symbol, RETENTION_EH_OR_RUNTIME);
	}

	void MarkCallArguments(const Instruction& instruction)
	{
		if (instruction.kind != Instruction::CALL ||
			instruction.extra_count == 0) return;
		if (instruction.extra_first == kNoLowId ||
			instruction.extra_first > program_.call_arguments.size() ||
			instruction.extra_count > program_.call_arguments.size() -
				instruction.extra_first)
			ThrowLoweringInternal(
				"function reachability call arguments are out of bounds");
		for (std::size_t i = 0; i < instruction.extra_count; ++i)
			MarkOperand(program_.call_arguments[
				instruction.extra_first + i], RETENTION_ADDRESS_OR_RELOCATION);
	}

	void VisitFunction(std::size_t index)
	{
		const Function& function = program_.functions[index];
		for (std::size_t i = 0; i < function.blocks.size(); ++i)
			for (std::size_t j = 0;
				j < function.blocks[i].instructions.size(); ++j)
			{
				const Instruction& instruction =
					function.blocks[i].instructions[j];
				MarkOperand(instruction.first,
					instruction.kind == Instruction::CALL ?
						RETENTION_DIRECT_CALL : RETENTION_ADDRESS_OR_RELOCATION);
				MarkOperand(instruction.second, RETENTION_ADDRESS_OR_RELOCATION);
				MarkOperand(instruction.third, RETENTION_ADDRESS_OR_RELOCATION);
				MarkCallArguments(instruction);
			}
	}

	void ClassifyRetainedFunctions(Summary* result) const
	{
		for (std::size_t i = 0; i < reachable_.size(); ++i)
		{
			if (!reachable_[i]) continue;
			const std::uint16_t reasons = retention_reasons_[i];
			if (reasons & RETENTION_EXTERNAL_STRONG)
				++result->retained_external_strong;
			else if (reasons & RETENTION_LIFECYCLE)
				++result->retained_lifecycle;
			else if (reasons & RETENTION_EH_OR_RUNTIME)
				++result->retained_eh_or_runtime;
			else if (reasons & RETENTION_REQUIRED_WEAK)
				++result->retained_required_weak;
			else if (reasons & RETENTION_OBJECT_OUTPUT_ROOT)
			{
				++result->retained_object_output_root;
				const Symbol& symbol = program_.symbols[
					program_.functions[i].symbol];
				if (symbol.weak_linkage)
					++result->retained_object_output_root_weak;
				if (symbol.internal_linkage)
					++result->retained_object_output_root_internal;
			}
			else if (reasons & RETENTION_ADDRESS_OR_RELOCATION)
				++result->retained_address_or_relocation;
			else if (reasons & RETENTION_DIRECT_CALL)
				++result->retained_direct_call;
			else {
				++result->retained_conservative_fallback;
				const Symbol& symbol = program_.symbols[
					program_.functions[i].symbol];
				const lowir_model::StringId presentation =
					symbol.object_name.valid() ? symbol.object_name : symbol.name;
				result->retained_conservative_fallback_names.push_back(
					program_.strings.get(presentation));
			}
		}
	}
};

}

namespace
{

void PreserveReachableLifecycleBaseEntries(lowering::ir::Program* program,
	const Analyzer& analyzer)
{
	for (std::size_t i = 0; i < program->functions.size(); ++i)
	{
		const std::uint32_t symbol = program->functions[i].symbol;
		if (analyzer.Reachable(i) &&
			program->symbols[symbol].lifecycle_base_entry)
			program->symbols[symbol].object_output_root = true;
	}
}

}

Summary Analyze(lowering::ir::Program* program)
{
	if (!program) ThrowLoweringInternal(
		"cannot analyze a null typed LowIR program");
	Analyzer object_reachability(*program, false);
	(void)object_reachability.Run();
	PreserveReachableLifecycleBaseEntries(program, object_reachability);
	return Analyzer(*program, true).Run();
}

Summary AuditWithoutInternalRoots(const lowering::ir::Program& program)
{
	return Analyzer(program, false).Run();
}

Summary PruneUnreachableWeakFunctions(lowering::ir::Program* program)
{
	if (!program) ThrowLoweringInternal(
		"cannot prune a null typed LowIR program");
	Analyzer analyzer(*program, false);
	Summary result = analyzer.Run();
	PreserveReachableLifecycleBaseEntries(program, analyzer);
	std::vector<Function> retained;
	retained.reserve(result.reachable_functions);
	for (std::size_t i = 0; i < program->functions.size(); ++i)
	{
		Function& function = program->functions[i];
		const std::uint32_t symbol = function.symbol;
		if (!analyzer.Reachable(i) &&
			(program->symbols[symbol].weak_linkage ||
			 program->symbols[symbol].internal_linkage))
		{
			program->symbols[symbol].definition_emitted = false;
			++result.pruned_functions;
		}
		else retained.push_back(std::move(function));
	}
	program->functions.swap(retained);
	std::vector<unsigned char> defined_symbols(program->symbols.size(), 0);
	for (std::size_t i = 0; i < program->globals.size(); ++i)
		defined_symbols[program->globals[i].symbol] = 1;
	for (std::size_t i = 0; i < program->functions.size(); ++i)
		defined_symbols[program->functions[i].symbol] = 1;
	std::vector<ObjectAlias> aliases;
	aliases.reserve(program->object_aliases.size());
	for (std::size_t i = 0; i < program->object_aliases.size(); ++i)
	{
		const std::uint32_t target = program->object_aliases[i].target;
		if (target < defined_symbols.size() && defined_symbols[target])
			aliases.push_back(std::move(program->object_aliases[i]));
	}
	program->object_aliases.swap(aliases);
	return result;
}

}  // namespace reachability
}  // namespace lowering
}  // namespace cppgm
