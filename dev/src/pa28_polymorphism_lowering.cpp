#include "pa18_polymorphism_lowering.h"

#include "pa15_source_type_lowering.h"

#include <stdexcept>

namespace cppgm
{
namespace pa18_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;

namespace
{

class VtableThunkEmitter
{
public:
	VtableThunkEmitter(const SemanticGraphView& graph, TypedProgram& output,
		LowIRLoweringStats* stats, const std::vector<SymbolId>& function_symbols,
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
	void AddParameter(Function* function, const std::string& name,
		const LowType& type, bool reference)
	{
		Parameter parameter;
		parameter.name = name;
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
		for (std::size_t i = 0; i < type.parameter_count; ++i)
		{
			output_.call_arguments.push_back(Operand(
				ParameterId(static_cast<std::uint32_t>(i + 1)),
				source_types_.Lower(source_parameters[i])));
			output_.call_argument_references.push_back(
				source_types_.IsReference(source_parameters[i]) ?
				Instruction::CALL_PASS_REFERENCE : Instruction::CALL_PASS_VALUE);
		}
	}

	void EmitOne(const VtableThunkLoweringFact& thunk)
	{
		if (thunk.function >= function_symbols_.size() ||
			thunk.target == kNoLowId)
			throw std::logic_error("vtable thunk target is not emitted");
		const BindingRecord& binding = program_.bindings[thunk.function];
		const TypeRecord& type = program_.types.Get(binding.type);
		const TypeId* parameters = program_.types.Parameters(binding.type);
		Function function;
		function.symbol = thunk.symbol;
		function.result = source_types_.Lower(type.child);
		function.variadic = type.variadic;
		AddParameter(&function, "arg0", LowPtr(), false);
		for (std::size_t i = 0; i < type.parameter_count; ++i)
			AddParameter(&function, "arg" + std::to_string(i + 1),
				source_types_.Lower(parameters[i]),
				source_types_.IsReference(parameters[i]));
		function.blocks.push_back(Block("entry"));
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
		Instruction result(function.result.kind == LOW_VOID ?
			Instruction::RETURN_VOID : Instruction::RETURN_VALUE);
		if (function.result.kind != LOW_VOID)
		{
			result.type = function.result;
			result.first = Operand(TempId(2), function.result);
		}
		function.blocks[0].instructions.push_back(result);
		function.blocks[0].terminated = true;
		output_.symbols[thunk.target].referenced = true;
		output_.functions.push_back(function);
		if (stats_)
		{
			++stats_->functions;
			++stats_->blocks;
			stats_->instructions += function.result.kind == LOW_VOID ? 3 : 3;
		}
	}

	const Program& program_;
	TypedProgram& output_;
	LowIRLoweringStats* stats_;
	const std::vector<SymbolId>& function_symbols_;
	PolymorphismLoweringState& state_;
	pa15_lowering_detail::SourceTypeLowering source_types_;
};

}

void EmitVtableThunks(const SemanticGraphView& graph, TypedProgram& output,
	LowIRLoweringStats* stats, const std::vector<SymbolId>& function_symbols,
	PolymorphismLoweringState* state)
{
	VtableThunkEmitter(graph, output, stats, function_symbols, state).EmitAll();
}

}
}
