#include "lowir_function_reachability.h"

#include "lowir_prepare.h"

#include <stdexcept>
#include <utility>
#include <vector>

namespace lowir_model
{
namespace
{

class FunctionReachability
{
public:
	explicit FunctionReachability(const Program& program) : program_(program),
		functions_(program.symbol_names.size(), no_function()),
		reachable_(program.functions.size(), 0)
	{
		for (std::size_t i = 0; i < program.functions.size(); ++i)
		{
			const SymbolId symbol = program.functions[i].symbol;
			if (functions_[symbol] != no_function())
				throw std::logic_error(
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

private:
	const Program& program_;
	std::vector<std::size_t> functions_;
	std::vector<unsigned char> reachable_;
	std::vector<std::size_t> pending_;

	static std::size_t no_function() { return static_cast<std::size_t>(-1); }

	void MarkSymbol(SymbolId symbol)
	{
		const std::size_t found = functions_[symbol];
		if (found == no_function() || reachable_[found]) return;
		reachable_[found] = 1;
		pending_.push_back(found);
	}

	void MarkOperand(const Operand& operand)
	{
		if (operand.kind == Operand::OP_GLOBAL) MarkSymbol(operand.symbol);
	}

	void MarkRoots()
	{
		for (std::size_t i = 0; i < program_.functions.size(); ++i)
		{
			const Function& function = program_.functions[i];
			if (function.metadata.binding != SBM_WEAK ||
				function.metadata.object_output_root ||
				function.metadata.role == SR_ENTRY ||
				function.metadata.role == SR_INIT ||
				function.metadata.role == SR_FINI ||
				!function.metadata.tls_for_symbol.empty())
				MarkSymbol(function.symbol);
		}
		for (std::size_t i = 0; i < program_.globals.size(); ++i)
		{
			const GlobalDefinition& global = program_.globals[i];
			MarkOperand(global.init_operand);
			for (std::size_t j = 0; j < global.data_items.size(); ++j)
				if (global.data_items[j].kind ==
					GlobalDefinition::DataItem::ITEM_ADDR)
					MarkSymbol(global.data_items[j].symbol_id);
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
				MarkOperand(instruction.first);
				MarkOperand(instruction.second);
				MarkOperand(instruction.third);
				for (std::size_t a = 0; a < instruction.args.size(); ++a)
					MarkOperand(instruction.args[a]);
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
	std::vector<unsigned char> removed(program.symbol_names.size(), 0);
	std::vector<Function> retained;
	retained.reserve(result.reachable_functions);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
	{
		if (!reachability.Reachable(i) &&
			program.functions[i].metadata.binding == SBM_WEAK)
		{
			removed[program.functions[i].symbol] = 1;
			++result.pruned_functions;
		}
		else retained.push_back(std::move(program.functions[i]));
	}
	program.functions.swap(retained);
	std::vector<ObjectAlias> aliases;
	aliases.reserve(program.object_aliases.size());
	for (std::size_t i = 0; i < program.object_aliases.size(); ++i)
		if (!removed[program.object_aliases[i].target_id])
			aliases.push_back(std::move(program.object_aliases[i]));
	program.object_aliases.swap(aliases);
	derive_lowir_object_facts(program);
	return result;
}

}  // namespace lowir_model
