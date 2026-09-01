#include "lowir/analysis/function_reachability.h"

#include "lowir/io/prepare.h"
#include "lowir/optimize/errors.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace lowir_model
{
namespace
{

enum RetentionReason
{
	RETENTION_EXTERNAL_STRONG = 1U << 0,
	RETENTION_ADDRESS_OR_RELOCATION = 1U << 1,
	RETENTION_DIRECT_CALL = 1U << 2,
	RETENTION_LIFECYCLE = 1U << 3,
	RETENTION_OBJECT_OUTPUT_ROOT = 1U << 4
};

class FunctionReachability
{
public:
	explicit FunctionReachability(const Program& program) : program_(program),
		functions_(program.symbol_names.size(), no_function()),
		reachable_(program.functions.size(), 0),
		retention_reasons_(program.functions.size(), 0)
	{
		for (std::size_t i = 0; i < program.functions.size(); ++i)
		{
			const SymbolId symbol = program.functions[i].symbol;
			if (functions_[symbol] != no_function())
				lowir_opt::ThrowOptimizerInternalError(
					"function reachability has duplicate definitions");
			functions_[symbol] = i;
		}
	}

	void Run()
	{
		MarkRoots();
		for (std::size_t cursor = 0; cursor < pending_.size(); ++cursor)
			VisitFunction(pending_[cursor]);
	}

	bool Reachable(std::size_t function) const
	{
		return function < reachable_.size() && reachable_[function] != 0;
	}

	std::size_t ReachableCount() const { return pending_.size(); }

	void AddSummary(FunctionPruningSummary* summary) const
	{
		for (std::size_t i = 0; i < program_.functions.size(); ++i)
		{
			const Function& function = program_.functions[i];
			if (!reachable_[i])
			{
				if (function.metadata.binding == SBM_WEAK)
					++summary->unreachable_weak_functions;
				if (function.metadata.binding == SBM_INTERNAL)
					++summary->unreachable_internal_functions;
				continue;
			}
			const std::uint16_t reasons = retention_reasons_[i];
			if (reasons & RETENTION_EXTERNAL_STRONG)
				++summary->retained_external_strong;
			else if (reasons & RETENTION_LIFECYCLE)
				++summary->retained_lifecycle;
			else if (reasons & RETENTION_OBJECT_OUTPUT_ROOT)
			{
				++summary->retained_object_output_root;
				if (function.metadata.binding == SBM_WEAK)
					++summary->retained_object_output_root_weak;
				if (function.metadata.binding == SBM_INTERNAL)
					++summary->retained_object_output_root_internal;
			}
			else if (reasons & RETENTION_ADDRESS_OR_RELOCATION)
				++summary->retained_address_or_relocation;
			else if (reasons & RETENTION_DIRECT_CALL)
				++summary->retained_direct_call;
		}
	}

private:
	const Program& program_;
	std::vector<std::size_t> functions_;
	std::vector<unsigned char> reachable_;
	std::vector<std::uint16_t> retention_reasons_;
	std::vector<std::size_t> pending_;

	static std::size_t no_function() { return static_cast<std::size_t>(-1); }

	void MarkSymbol(SymbolId symbol, std::uint16_t reason)
	{
		const std::size_t found = functions_[symbol];
		if (found == no_function()) return;
		retention_reasons_[found] |= reason;
		if (reachable_[found]) return;
		reachable_[found] = 1;
		pending_.push_back(found);
	}

	void MarkOperand(const Operand& operand, std::uint16_t reason)
	{
		if (operand.kind == Operand::OP_GLOBAL) MarkSymbol(operand.symbol, reason);
	}

	void MarkRoots()
	{
		for (std::size_t i = 0; i < program_.functions.size(); ++i)
		{
			const Function& function = program_.functions[i];
			if (function.metadata.binding != SBM_WEAK &&
				function.metadata.binding != SBM_INTERNAL)
				MarkSymbol(function.symbol, RETENTION_EXTERNAL_STRONG);
			if (function.metadata.object_output_root)
				MarkSymbol(function.symbol, RETENTION_OBJECT_OUTPUT_ROOT);
			if (function.metadata.role == SR_ENTRY ||
				function.metadata.role == SR_INIT ||
				function.metadata.role == SR_FINI ||
				function.metadata.tls_for_symbol_id.valid())
				MarkSymbol(function.symbol, RETENTION_LIFECYCLE);
		}
		for (std::size_t i = 0; i < program_.globals.size(); ++i)
		{
			const GlobalDefinition& global = program_.globals[i];
			MarkOperand(global.init_operand, RETENTION_ADDRESS_OR_RELOCATION);
			for (std::size_t j = 0; j < global.data_items.size(); ++j)
				if (global.data_items[j].kind ==
					GlobalDefinition::DataItem::ITEM_ADDR)
					MarkSymbol(global.data_items[j].symbol_id,
						RETENTION_ADDRESS_OR_RELOCATION);
		}
	}

	void VisitFunction(std::size_t index)
	{
		const Function& function = program_.functions[index];
		for (std::size_t b = 0; b < function.blocks.size(); ++b)
			for (std::size_t i = 0;
				i < function.blocks[b].instructions.size(); ++i)
			{
				const Instruction& instruction =
					function.blocks[b].instructions[i];
				MarkOperand(instruction.first,
					instruction.kind == Instruction::IK_CALL ?
						RETENTION_DIRECT_CALL : RETENTION_ADDRESS_OR_RELOCATION);
				MarkOperand(instruction.second, RETENTION_ADDRESS_OR_RELOCATION);
				MarkOperand(instruction.third, RETENTION_ADDRESS_OR_RELOCATION);
				for (std::size_t a = 0; a < instruction.args.size(); ++a)
					MarkOperand(instruction.args[a],
						RETENTION_ADDRESS_OR_RELOCATION);
			}
	}
};

}  // namespace

FunctionPruningSummary prune_unreachable_weak_functions(Program& program)
{
	FunctionReachability reachability(program);
	reachability.Run();
	FunctionPruningSummary result;
	result.reachable_functions = reachability.ReachableCount();
	reachability.AddSummary(&result);
	std::vector<Function> retained;
	retained.reserve(result.reachable_functions);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
	{
		if (!reachability.Reachable(i) &&
			(program.functions[i].metadata.binding == SBM_WEAK ||
			 program.functions[i].metadata.binding == SBM_INTERNAL))
		{
			++result.pruned_functions;
		}
		else retained.push_back(std::move(program.functions[i]));
	}
	program.functions.swap(retained);
	std::vector<unsigned char> defined(program.symbol_names.size(), 0);
	for (std::size_t i = 0; i < program.globals.size(); ++i)
		defined[program.globals[i].symbol] = 1;
	for (std::size_t i = 0; i < program.functions.size(); ++i)
		defined[program.functions[i].symbol] = 1;
	std::vector<ObjectAlias> aliases;
	aliases.reserve(program.object_aliases.size());
	for (std::size_t i = 0; i < program.object_aliases.size(); ++i)
	{
		const std::uint32_t target = program.object_aliases[i].target_id;
		if (target < defined.size() && defined[target])
			aliases.push_back(std::move(program.object_aliases[i]));
	}
	program.object_aliases.swap(aliases);
	derive_lowir_object_facts(program);
	return result;
}

}  // namespace lowir_model
