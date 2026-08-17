#include "pa15_function_reachability.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa15_function_reachability
{
namespace
{

using namespace pa15_lowir_detail;

const std::size_t kNoFunction = std::numeric_limits<std::size_t>::max();

class Analyzer
{
public:
	explicit Analyzer(const TypedProgram& program)
		: program_(program), function_by_symbol_(program.symbols.size(),
			kNoFunction), reachable_(program.functions.size(), 0)
	{
		for (std::size_t i = 0; i < program_.functions.size(); ++i)
		{
			const std::uint32_t symbol = program_.functions[i].symbol;
			if (symbol >= function_by_symbol_.size())
				throw std::logic_error(
					"function reachability symbol is out of bounds");
			if (function_by_symbol_[symbol] != kNoFunction)
				throw std::logic_error(
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
		}
		return result;
	}

private:
	const TypedProgram& program_;
	std::vector<std::size_t> function_by_symbol_;
	std::vector<unsigned char> reachable_;
	std::vector<std::size_t> pending_;

	void MarkSymbol(SymbolId symbol)
	{
		const std::uint32_t id = symbol;
		if (id == kNoLowId || id >= function_by_symbol_.size()) return;
		const std::size_t function = function_by_symbol_[id];
		if (function == kNoFunction || reachable_[function]) return;
		reachable_[function] = 1;
		pending_.push_back(function);
	}

	void MarkOperand(const Operand& operand)
	{
		if (operand.kind == Operand::FUNCTION || operand.kind == Operand::GLOBAL)
			MarkSymbol(SymbolId(operand.id));
	}

	void MarkGlobalReferences()
	{
		for (std::size_t i = 0; i < program_.globals.size(); ++i)
		{
			const Global& global = program_.globals[i];
			if (global.initializer_kind == Global::ADDRESS_VALUE)
				MarkSymbol(global.address_symbol);
			for (std::size_t j = 0; j < global.items.size(); ++j)
				if (global.items[j].kind == Global::DataItem::ADDRESS_ITEM)
					MarkSymbol(global.items[j].symbol);
		}
		for (std::size_t i = 0; i < program_.exception_filter_types.size(); ++i)
			MarkSymbol(program_.exception_filter_types[i]);
	}

	void MarkRoots()
	{
		for (std::size_t i = 0; i < program_.functions.size(); ++i)
		{
			const Function& function = program_.functions[i];
			const Symbol& symbol = program_.symbols[function.symbol];
			if (!symbol.weak_linkage || symbol.object_output_root ||
				function.entry || function.initializer || function.finalizer ||
				symbol.tls_for_symbol != kNoLowId)
				MarkSymbol(function.symbol);
		}
		MarkGlobalReferences();
		MarkSymbol(program_.terminate_runtime_symbol);
		MarkSymbol(program_.terminate_helper_symbol);
		MarkSymbol(program_.call_unexpected_symbol);
	}

	void MarkCallArguments(const Instruction& instruction)
	{
		if (instruction.kind != Instruction::CALL ||
			instruction.extra_count == 0) return;
		if (instruction.extra_first == kNoLowId ||
			instruction.extra_first > program_.call_arguments.size() ||
			instruction.extra_count > program_.call_arguments.size() -
				instruction.extra_first)
			throw std::logic_error(
				"function reachability call arguments are out of bounds");
		for (std::size_t i = 0; i < instruction.extra_count; ++i)
			MarkOperand(program_.call_arguments[
				instruction.extra_first + i]);
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
				MarkOperand(instruction.first);
				MarkOperand(instruction.second);
				MarkOperand(instruction.third);
				MarkCallArguments(instruction);
			}
	}
};

}

Summary Analyze(const TypedProgram& program)
{
	return Analyzer(program).Run();
}

}
}
