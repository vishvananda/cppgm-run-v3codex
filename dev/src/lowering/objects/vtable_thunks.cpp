#include "lowering/objects/polymorphism.h"

#include "lowering/presentation/local_names.h"
#include "lowering/core/source_types.h"
#include "lowering/support/errors.h"


namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;

namespace
{

class VtableThunkEmitter
{
public:
	VtableThunkEmitter(const SemanticGraphView& graph, lowering::ir::Program& output,
		lowering::Stats* stats, const std::vector<SymbolId>& function_symbols,
		PolymorphismLoweringState* state)
		: program_(graph.program), output_(output), stats_(stats),
		  function_symbols_(function_symbols), state_(*state),
		  source_types_(program_) {}

	void EmitAll()
	{
		for (std::size_t i = 0; i < state_.vtable_thunks.size(); ++i)
			EmitOne(state_.vtable_thunks[i]);
	}

private:
	void AddParameter(Function* function, std::uint32_t ordinal,
		const LowType& type, bool reference)
	{
		Parameter parameter;
		parameter.name = lowering::presentation::InternOrdinalName(
			output_, "arg", 3, ordinal);
		parameter.type = type;
		parameter.reference = reference;
		function->parameters.push_back(parameter);
	}

	void AttachArguments(Instruction* call, const TypeRecord& type,
		const TypeId* source_parameters, const Operand& object)
	{
		call->extra_first = static_cast<std::uint32_t>(
			output_.call_arguments.size());
		call->extra_count = static_cast<std::uint32_t>(
			type.parameter_count + 1);
		output_.call_arguments.push_back(object);
		output_.call_argument_references.push_back(
			Instruction::CALL_PASS_VALUE);
		output_.call_argument_object_bytes.push_back(0);
		for (std::size_t i = 0; i < type.parameter_count; ++i)
		{
			output_.call_arguments.push_back(Operand(
				ParameterId(static_cast<std::uint32_t>(i + 1)),
				source_types_.Lower(source_parameters[i])));
			output_.call_argument_references.push_back(
				source_types_.IsReference(source_parameters[i]) ?
					Instruction::CALL_PASS_REFERENCE : Instruction::CALL_PASS_VALUE);
			output_.call_argument_object_bytes.push_back(0);
		}
	}

	void EmitOne(const VtableThunkLoweringFact& thunk)
	{
		if (thunk.function >= function_symbols_.size() ||
			thunk.target == kNoLowId)
			ThrowLoweringInternal("vtable thunk target is not emitted");
		const BindingRecord& binding = program_.bindings[thunk.function];
		const TypeRecord& type = program_.types.Get(binding.type);
		const TypeId* parameters = program_.types.Parameters(binding.type);
		Function function;
		function.symbol = thunk.symbol;
		function.result = source_types_.Lower(type.child);
		function.variadic = type.variadic;
		AddParameter(&function, 0, LowPtr(), false);
		for (std::size_t i = 0; i < type.parameter_count; ++i)
			AddParameter(&function, static_cast<std::uint32_t>(i + 1),
				source_types_.Lower(parameters[i]),
				source_types_.IsReference(parameters[i]));
		function.blocks.push_back(lowering::presentation::MakePresentedBlock(
			output_, &function,
			lowering::presentation::ExactBlockPresentation(output_, "entry")));
		function.blocks[0].selected = true;
		function.block_order.push_back(BlockId(0));
		const Operand adjusted(TempId(1), LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = adjusted.id;
		index.type = LowI8();
		index.first = Operand(ParameterId(0), LowPtr());
		index.second = Operand(thunk.this_adjustment, LowI64());
		function.blocks[0].instructions.push_back(index);
		Instruction call(Instruction::CALL);
		call.type = function.result;
		call.first = Operand(Operand::FUNCTION, thunk.target, LowPtr());
		AttachArguments(&call, type, parameters, adjusted);
		if (function.result.kind == LOW_VOID)
			function.blocks[0].instructions.push_back(call);
		else
		{
			call.dest = 2;
			function.blocks[0].instructions.push_back(call);
		}
		const bool adjust_return = thunk.return_adjustment_virtual ||
			thunk.return_adjustment != 0;
		std::uint32_t next_temp = function.result.kind == LOW_VOID ? 2 : 3;
		if (adjust_return)
		{
			if (function.result.kind != LOW_PTR)
				ThrowLoweringInternal(
					"covariant vtable thunk result is not pointer-shaped");
			const Operand returned(TempId(2), LowPtr());
			const Operand is_null(TempId(3), LowI64());
			Instruction compare(Instruction::CMP);
			compare.dest = is_null.id;
			compare.op = LOW_OP_EQ;
			compare.type = LowPtr();
			compare.first = returned;
			compare.second = Operand(0, LowPtr());
			function.blocks[0].instructions.push_back(compare);
			Instruction branch(Instruction::BRANCH);
			branch.first = is_null;
			branch.target = BlockId(1);
			branch.alternate = BlockId(2);
			function.blocks[0].instructions.push_back(branch);
			function.blocks[0].terminated = true;

			function.blocks.push_back(lowering::presentation::MakePresentedBlock(
				output_, &function,
				lowering::presentation::ExactBlockPresentation(
					output_, "return_null")));
			function.blocks[1].selected = true;
			function.block_order.push_back(BlockId(1));
			Instruction null_result(Instruction::RETURN_VALUE);
			null_result.type = LowPtr();
			null_result.first = returned;
			function.blocks[1].instructions.push_back(null_result);
			function.blocks[1].terminated = true;

			function.blocks.push_back(lowering::presentation::MakePresentedBlock(
				output_, &function,
				lowering::presentation::ExactBlockPresentation(
					output_, "adjust_return")));
			function.blocks[2].selected = true;
			function.block_order.push_back(BlockId(2));
			next_temp = 4;
			Operand adjusted_result = returned;
			if (thunk.return_adjustment != 0)
			{
				adjusted_result = Operand(TempId(next_temp++), LowPtr());
				Instruction fixed(Instruction::INDEX);
				fixed.dest = adjusted_result.id;
				fixed.type = LowI8();
				fixed.first = returned;
				fixed.second = Operand(thunk.return_adjustment, LowI64());
				function.blocks[2].instructions.push_back(fixed);
			}
			if (thunk.return_adjustment_virtual)
			{
				const Operand vtable(TempId(next_temp++), LowPtr());
				Instruction load_vtable(Instruction::LOAD);
				load_vtable.dest = vtable.id;
				load_vtable.type = LowPtr();
				load_vtable.first = adjusted_result;
				function.blocks[2].instructions.push_back(load_vtable);
				const Operand row(TempId(next_temp++), LowPtr());
				Instruction index_row(Instruction::INDEX);
				index_row.dest = row.id;
				index_row.type = LowI8();
				index_row.first = vtable;
				index_row.second = Operand(
					thunk.return_runtime_vtable_offset, LowI64());
				function.blocks[2].instructions.push_back(index_row);
				const Operand offset(TempId(next_temp++), LowI64());
				Instruction load_offset(Instruction::LOAD);
				load_offset.dest = offset.id;
				load_offset.type = LowI64();
				load_offset.first = row;
				function.blocks[2].instructions.push_back(load_offset);
				const Operand virtual_result(
					TempId(next_temp++), LowPtr());
				Instruction index_result(Instruction::INDEX);
				index_result.dest = virtual_result.id;
				index_result.type = LowI8();
				index_result.first = adjusted_result;
				index_result.second = offset;
				function.blocks[2].instructions.push_back(index_result);
				adjusted_result = virtual_result;
			}
			Instruction result(Instruction::RETURN_VALUE);
			result.type = LowPtr();
			result.first = adjusted_result;
			function.blocks[2].instructions.push_back(result);
			function.blocks[2].terminated = true;
		}
		else
		{
			Instruction result(function.result.kind == LOW_VOID ?
				Instruction::RETURN_VOID : Instruction::RETURN_VALUE);
			if (function.result.kind != LOW_VOID)
			{
				result.type = function.result;
				result.first = Operand(TempId(2), function.result);
			}
			function.blocks[0].instructions.push_back(result);
			function.blocks[0].terminated = true;
		}
		function.temporary_limit = next_temp;
		output_.symbols[thunk.target].referenced = true;
		output_.functions.push_back(function);
		if (stats_)
		{
			++stats_->functions;
			stats_->blocks += function.block_order.size();
			for (std::size_t block = 0;
				block < function.blocks.size(); ++block)
				stats_->instructions += function.blocks[block].instructions.size();
		}
	}

	const semantic::Program& program_;
	lowering::ir::Program& output_;
	lowering::Stats* stats_;
	const std::vector<SymbolId>& function_symbols_;
	PolymorphismLoweringState& state_;
	lowering::SourceTypeLowering source_types_;
};

}

void EmitVtableThunks(const SemanticGraphView& graph, lowering::ir::Program& output,
	lowering::Stats* stats, const std::vector<SymbolId>& function_symbols,
	PolymorphismLoweringState* state)
{
	VtableThunkEmitter(graph, output, stats, function_symbols, state).EmitAll();
}

}
}
