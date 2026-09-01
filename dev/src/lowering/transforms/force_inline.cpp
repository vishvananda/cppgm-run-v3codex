#include "lowering/transforms/force_inline.h"

#include "lowering/core/reachability.h"
#include "lowering/presentation/local_names.h"
#include "lowering/support/errors.h"
#include "lowering/api.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace cppgm
{
namespace lowering
{
namespace inline_policy
{
namespace
{

using namespace lowering::ir;

const std::size_t kNoFunction = std::numeric_limits<std::size_t>::max();

void AppendOrdinalReservation(const std::string& name, const char* prefix,
	lowir_model::GeneratedNameReservationKind kind,
	lowir_model::GeneratedNameReservations* reservations)
{
	std::uint32_t ordinal = 0;
	if (lowir_model::parse_generated_name_ordinal(name, prefix, &ordinal))
		reservations->append(kind, ordinal);
}

void ClassifyPresentationReservations(lowering::ir::Program* program)
{
	if (!program->retain_local_names)
	{
		for (std::size_t f = 0; f < program->functions.size(); ++f)
			program->functions[f].generated_name_reservations.normalize();
		return;
	}
	for (std::size_t f = 0; f < program->functions.size(); ++f)
	{
		Function& function = program->functions[f];
		lowir_model::GeneratedNameReservations& reservations =
			function.generated_name_reservations;
		reservations.clear();
		for (std::size_t i = 0; i < function.parameters.size(); ++i)
		{
			const std::string& name = program->strings.get(
				function.parameters[i].name);
			lowir_model::collect_o1_site_reservations(name, &reservations);
			AppendOrdinalReservation(name, "__force_inline_parameter_",
				lowir_model::GNR_FORCE_PARAMETER, &reservations);
			AppendOrdinalReservation(name, "__force_inline_temporary_",
				lowir_model::GNR_FORCE_TEMPORARY, &reservations);
		}
		for (std::size_t i = 0; i < function.slots.size(); ++i)
		{
			const std::string& name = program->strings.get(
				function.slots[i].name);
			lowir_model::collect_o1_site_reservations(name, &reservations);
			AppendOrdinalReservation(name, "__force_inline_slot_",
				lowir_model::GNR_TYPED_FORCE_SLOT, &reservations);
			AppendOrdinalReservation(name, "__force_inline_local_",
				lowir_model::GNR_FORCE_LOCAL, &reservations);
			AppendOrdinalReservation(name, "__force_inline_result_",
				lowir_model::GNR_FORCE_RESULT, &reservations);
			AppendOrdinalReservation(name, "retmerge__",
				lowir_model::GNR_O1_SCALAR_MERGE_SUFFIX, &reservations);
			AppendOrdinalReservation(name, "retmergeobj__",
				lowir_model::GNR_O1_OBJECT_MERGE_SUFFIX, &reservations);
		}
		for (std::size_t i = 0; i < function.blocks.size(); ++i)
		{
			const std::string& name = program->strings.get(
				function.blocks[i].label);
			lowir_model::collect_o1_site_reservations(name, &reservations);
			AppendOrdinalReservation(name, "__force_inline_block_",
				lowir_model::GNR_TYPED_FORCE_BLOCK, &reservations);
			AppendOrdinalReservation(name, "__force_inline_prologue_",
				lowir_model::GNR_FORCE_PROLOGUE, &reservations);
			AppendOrdinalReservation(name, "__force_inline_continuation_",
				lowir_model::GNR_FORCE_CONTINUATION, &reservations);
		}
		reservations.normalize();
	}
}

class PresentationNames
{
public:
	PresentationNames(lowering::ir::Program* program, Function* function)
		: program_(*program), function_(*function), next_(0) {}

	lowir_model::StringId SlotName()
	{
		return Fresh(lowir_model::GNR_TYPED_FORCE_SLOT, "slot");
	}
	BlockPresentationName BlockName()
	{
		const std::uint32_t ordinal = Reserve(
			lowir_model::GNR_TYPED_FORCE_BLOCK);
		return lowering::presentation::GeneratedBlockPresentation(
			program_, "__force_inline_block", ordinal);
	}

private:
	std::uint32_t Reserve(lowir_model::GeneratedNameReservationKind kind)
	{
		while (function_.generated_name_reservations.contains(kind, next_))
			++next_;
		const std::uint32_t ordinal = next_++;
		function_.generated_name_reservations.reserve(kind, ordinal);
		return ordinal;
	}

	lowir_model::StringId Fresh(lowir_model::GeneratedNameReservationKind kind,
		const char* role)
	{
		const std::uint32_t ordinal = Reserve(kind);
		if (!program_.retain_local_names) return lowir_model::StringId();
		return program_.strings.intern("__force_inline_" +
			std::string(role) + "_" + std::to_string(ordinal));
	}

	lowering::ir::Program& program_;
	Function& function_;
	std::uint32_t next_;
};

class Inliner
{
public:
	Inliner(lowering::ir::Program* program, lowering::Stats* stats)
		: program_(*program), stats_(stats), candidate_count_(0),
		  tarjan_next_(0)
	{
		IndexCandidates();
	}

	bool HasCandidates() const { return candidate_count_ != 0; }

	void Run()
	{
		MarkRecursiveCandidates();
		expansion_state_.assign(program_.functions.size(), 0);
		for (std::size_t i = 0; i < program_.functions.size(); ++i)
			ExpandFunction(i);
		for (std::size_t i = 0; i < program_.symbols.size(); ++i)
			program_.symbols[i].force_inline = false;
		RecountOutput();
	}

private:
	lowering::ir::Program& program_;
	lowering::Stats* stats_;
	std::vector<std::size_t> candidate_by_symbol_;
	std::size_t candidate_count_;
	std::vector<bool> recursive_;
	std::vector<int> tarjan_index_, tarjan_low_;
	std::vector<bool> tarjan_stacked_;
	std::vector<std::size_t> tarjan_stack_;
	int tarjan_next_;
	std::vector<unsigned char> expansion_state_;

	void IndexCandidates()
	{
		candidate_by_symbol_.assign(program_.symbols.size(), kNoFunction);
		for (std::size_t i = 0; i < program_.functions.size(); ++i)
		{
			const Function& function = program_.functions[i];
			if (function.symbol >= program_.symbols.size())
				ThrowLoweringInternal("force-inline function has no symbol");
			if (!program_.symbols[function.symbol].force_inline ||
				function.variadic) continue;
			std::size_t& candidate = candidate_by_symbol_[function.symbol];
			if (candidate != kNoFunction)
				ThrowLoweringInternal(
					"multiple force-inline function definitions");
			candidate = i;
			++candidate_count_;
		}
		if (stats_) stats_->force_inline_candidates = candidate_count_;
	}

	std::size_t Candidate(const Instruction& instruction)
	{
		if (stats_) ++stats_->force_inline_call_probes;
		if (instruction.kind != Instruction::CALL || instruction.indirect ||
			(instruction.first.kind != Operand::FUNCTION &&
			 instruction.first.kind != Operand::GLOBAL) ||
			instruction.first.id >= candidate_by_symbol_.size())
			return kNoFunction;
		return candidate_by_symbol_[instruction.first.id];
	}

	void VisitCandidate(std::size_t function_index)
	{
		tarjan_index_[function_index] = tarjan_next_;
		tarjan_low_[function_index] = tarjan_next_++;
		tarjan_stack_.push_back(function_index);
		tarjan_stacked_[function_index] = true;
		const Function& function = program_.functions[function_index];
		bool self_edge = false;
		for (std::size_t i = 0; i < function.blocks.size(); ++i)
			for (std::size_t j = 0;
				j < function.blocks[i].instructions.size(); ++j)
			{
				const std::size_t callee =
					Candidate(function.blocks[i].instructions[j]);
				if (callee == kNoFunction) continue;
				self_edge = self_edge || callee == function_index;
				if (tarjan_index_[callee] < 0)
				{
					VisitCandidate(callee);
					tarjan_low_[function_index] = std::min(
						tarjan_low_[function_index], tarjan_low_[callee]);
				}
				else if (tarjan_stacked_[callee])
					tarjan_low_[function_index] = std::min(
						tarjan_low_[function_index], tarjan_index_[callee]);
			}
		if (tarjan_low_[function_index] != tarjan_index_[function_index])
			return;
		std::vector<std::size_t> component;
		for (;;)
		{
			const std::size_t member = tarjan_stack_.back();
			tarjan_stack_.pop_back();
			tarjan_stacked_[member] = false;
			component.push_back(member);
			if (member == function_index) break;
		}
		if (component.size() > 1 || self_edge)
			for (std::size_t i = 0; i < component.size(); ++i)
			{
				recursive_[component[i]] = true;
				if (stats_) ++stats_->force_inline_recursive_candidates;
			}
	}

	void MarkRecursiveCandidates()
	{
		const std::size_t count = program_.functions.size();
		recursive_.assign(count, false);
		tarjan_index_.assign(count, -1);
		tarjan_low_.assign(count, -1);
		tarjan_stacked_.assign(count, false);
		for (std::size_t symbol = 0;
			symbol < candidate_by_symbol_.size(); ++symbol)
		{
			const std::size_t function = candidate_by_symbol_[symbol];
			if (function != kNoFunction && tarjan_index_[function] < 0)
				VisitCandidate(function);
		}
	}

	void ExpandFunction(std::size_t function_index)
	{
		if (expansion_state_[function_index] == 2) return;
		if (expansion_state_[function_index] == 1) return;
		expansion_state_[function_index] = 1;
		const Function& source = program_.functions[function_index];
		std::vector<std::size_t> dependencies;
		for (std::size_t i = 0; i < source.blocks.size(); ++i)
			for (std::size_t j = 0;
				j < source.blocks[i].instructions.size(); ++j)
			{
				const std::size_t callee =
					Candidate(source.blocks[i].instructions[j]);
				if (callee != kNoFunction && !recursive_[callee])
					dependencies.push_back(callee);
			}
		for (std::size_t i = 0; i < dependencies.size(); ++i)
			ExpandFunction(dependencies[i]);
		InlineCalls(function_index);
		expansion_state_[function_index] = 2;
	}

	static TempId AllocateTemp(std::uint32_t* next)
	{
		if (*next >= kNoLowId)
			ThrowLoweringResourceLimit("too many force-inline temporaries");
		return static_cast<TempId>((*next)++);
	}

	void BuildTempMaps(const Function& callee, std::uint32_t* next,
		std::vector<TempId>* parameters, std::vector<TempId>* temporaries)
	{
		parameters->resize(callee.parameters.size());
		for (std::size_t i = 0; i < parameters->size(); ++i)
			(*parameters)[i] = AllocateTemp(next);
		temporaries->assign(callee.temporary_limit, TempId(kNoLowId));
		for (std::size_t i = 0; i < callee.blocks.size(); ++i)
			for (std::size_t j = 0;
				j < callee.blocks[i].instructions.size(); ++j)
			{
				const TempId dest = callee.blocks[i].instructions[j].dest;
				if (dest != kNoLowId && dest >= temporaries->size())
					ThrowLoweringInternal(
						"force-inline temporary exceeds its dense limit");
				if (dest != kNoLowId && (*temporaries)[dest] == kNoLowId)
					(*temporaries)[dest] = AllocateTemp(next);
			}
	}

	static Operand RenameOperand(const Operand& source,
		const std::vector<TempId>& parameters,
		const std::vector<TempId>& temporaries, std::size_t slot_base)
	{
		Operand result = source;
		if (source.kind == Operand::PARAMETER)
		{
			if (source.id >= parameters.size())
				ThrowLoweringInternal(
					"force-inline parameter identity is invalid");
			result.kind = Operand::TEMP;
			result.id = parameters[source.id];
		}
		else if (source.kind == Operand::TEMP)
		{
			if (source.id >= temporaries.size() ||
				temporaries[source.id] == kNoLowId)
				ThrowLoweringInternal(
					"force-inline temporary identity is invalid");
			result.id = temporaries[source.id];
		}
		else if (source.kind == Operand::SLOT)
		{
			if (source.id >= kNoLowId || slot_base > kNoLowId - source.id)
				ThrowLoweringResourceLimit("too many force-inline slots");
			result.id = static_cast<std::uint32_t>(slot_base + source.id);
		}
		return result;
	}

	Instruction CloneInstruction(const Instruction& source,
		const std::vector<TempId>& parameters,
		const std::vector<TempId>& temporaries, std::size_t slot_base,
		const std::vector<BlockId>& blocks)
	{
		Instruction result = source;
		if (source.dest != kNoLowId)
		{
			if (source.dest >= temporaries.size() ||
				temporaries[source.dest] == kNoLowId)
				ThrowLoweringInternal(
					"force-inline result identity is invalid");
			result.dest = temporaries[source.dest];
		}
		result.first = RenameOperand(
			source.first, parameters, temporaries, slot_base);
		result.second = RenameOperand(
			source.second, parameters, temporaries, slot_base);
		result.third = RenameOperand(
			source.third, parameters, temporaries, slot_base);
		if (source.target != kNoLowId)
		{
			if (source.target >= blocks.size())
				ThrowLoweringInternal("force-inline block target is invalid");
			result.target = blocks[source.target];
		}
		if (source.alternate != kNoLowId)
		{
			if (source.alternate >= blocks.size())
				ThrowLoweringInternal(
					"force-inline alternate block target is invalid");
			result.alternate = blocks[source.alternate];
		}
		if (source.kind == Instruction::CALL && source.extra_count != 0)
			CloneCallArguments(source, parameters, temporaries,
				slot_base, &result);
		else if (source.kind == Instruction::SWITCH &&
			source.extra_count != 0)
			CloneSwitchCases(source, blocks, &result);
		return result;
	}

	void CloneCallArguments(const Instruction& source,
		const std::vector<TempId>& parameters,
		const std::vector<TempId>& temporaries, std::size_t slot_base,
		Instruction* result)
	{
		if (source.extra_first == kNoLowId ||
			source.extra_first > program_.call_arguments.size() ||
			source.extra_count > program_.call_arguments.size() -
				source.extra_first ||
			source.extra_first > program_.call_argument_references.size() ||
			source.extra_count > program_.call_argument_references.size() -
				source.extra_first ||
			source.extra_first > program_.call_argument_object_bytes.size() ||
			source.extra_count > program_.call_argument_object_bytes.size() -
				source.extra_first)
			ThrowLoweringInternal(
				"force-inline call argument range is invalid");
		result->extra_first = static_cast<std::uint32_t>(
			program_.call_arguments.size());
		for (std::size_t i = 0; i < source.extra_count; ++i)
		{
			program_.call_arguments.push_back(RenameOperand(
				program_.call_arguments[source.extra_first + i],
				parameters, temporaries, slot_base));
			program_.call_argument_references.push_back(
				program_.call_argument_references[source.extra_first + i]);
			program_.call_argument_object_bytes.push_back(
				program_.call_argument_object_bytes[source.extra_first + i]);
		}
	}

	void CloneSwitchCases(const Instruction& source,
		const std::vector<BlockId>& blocks, Instruction* result)
	{
		if (source.extra_first == kNoLowId ||
			source.extra_first > program_.switch_case_values.size() ||
			source.extra_count > program_.switch_case_values.size() -
				source.extra_first ||
			source.extra_first > program_.switch_case_targets.size() ||
			source.extra_count > program_.switch_case_targets.size() -
				source.extra_first)
			ThrowLoweringInternal("force-inline switch range is invalid");
		result->extra_first = static_cast<std::uint32_t>(
			program_.switch_case_values.size());
		for (std::size_t i = 0; i < source.extra_count; ++i)
		{
			program_.switch_case_values.push_back(
				program_.switch_case_values[source.extra_first + i]);
			const BlockId target =
				program_.switch_case_targets[source.extra_first + i];
			if (target >= blocks.size())
				ThrowLoweringInternal(
					"force-inline switch target is invalid");
			program_.switch_case_targets.push_back(blocks[target]);
		}
	}

	static Instruction JumpTo(BlockId target)
	{
		Instruction result(Instruction::JUMP);
		result.target = target;
		return result;
	}

	Block CloneBlock(const Block& source, const Function& callee,
		const std::vector<TempId>& parameters,
		const std::vector<TempId>& temporaries, std::size_t slot_base,
		const std::vector<BlockId>& blocks, BlockId continuation,
		SlotId result_slot, Function* caller,
		const BlockPresentationName& name)
	{
		Block result = lowering::presentation::MakePresentedBlock(
			program_, caller, name);
		result.selected = source.selected;
		for (std::size_t i = 0; i < source.instructions.size(); ++i)
		{
			const Instruction& instruction = source.instructions[i];
			if (stats_) ++stats_->force_inline_cloned_instructions;
			if (instruction.kind != Instruction::RETURN_VALUE &&
				instruction.kind != Instruction::RETURN_VOID)
			{
				result.instructions.push_back(CloneInstruction(instruction,
					parameters, temporaries, slot_base, blocks));
				continue;
			}
			if (callee.result.kind != LOW_VOID)
			{
				if (instruction.kind != Instruction::RETURN_VALUE ||
					result_slot == kNoLowId)
					ThrowLoweringInternal(
						"force-inline value return has no result slot");
				Instruction store(Instruction::STORE);
				store.type = callee.result;
				store.first = RenameOperand(instruction.first,
					parameters, temporaries, slot_base);
				store.second = Operand(result_slot, callee.result);
				result.instructions.push_back(store);
			}
			result.instructions.push_back(JumpTo(continuation));
		}
		result.terminated = !result.instructions.empty() &&
			IsTerminator(result.instructions.back());
		return result;
	}

	static void InsertBlockOrder(Function* caller, BlockId after,
		const std::vector<BlockId>& inserted)
	{
		std::vector<BlockId>::iterator position = std::find(
			caller->block_order.begin(), caller->block_order.end(), after);
		if (position == caller->block_order.end())
			ThrowLoweringInternal("force-inline caller block is unordered");
		caller->block_order.insert(position + 1,
			inserted.begin(), inserted.end());
	}

	void InlineCall(Function* caller, std::size_t block_index,
		std::size_t instruction_index, const Function& callee,
		PresentationNames* names, std::uint32_t* next_temp)
	{
		const Instruction call =
			caller->blocks[block_index].instructions[instruction_index];
		if (call.extra_count != callee.parameters.size() ||
			(call.extra_count != 0 &&
			 (call.extra_first == kNoLowId ||
			  call.extra_first > program_.call_arguments.size() ||
			  call.extra_count > program_.call_arguments.size() -
				call.extra_first)))
			ThrowLoweringInternal(
				"force-inline call argument count mismatch");
		std::vector<Operand> arguments;
		arguments.reserve(call.extra_count);
		for (std::size_t i = 0; i < call.extra_count; ++i)
			arguments.push_back(
				program_.call_arguments[call.extra_first + i]);

		std::vector<TempId> parameters, temporaries;
		BuildTempMaps(callee, next_temp, &parameters, &temporaries);
		SlotId result_slot = static_cast<SlotId>(kNoLowId);
		if (callee.result.kind != LOW_VOID)
		{
			if (caller->slots.size() >= kNoLowId)
				ThrowLoweringResourceLimit("too many force-inline slots");
			result_slot = static_cast<SlotId>(caller->slots.size());
			Slot slot;
			slot.name = names->SlotName();
			slot.type = callee.result;
			caller->slots.push_back(slot);
		}
		const std::size_t callee_slot_base = caller->slots.size();
		for (std::size_t i = 0; i < callee.slots.size(); ++i)
		{
			if (caller->slots.size() >= kNoLowId)
				ThrowLoweringResourceLimit("too many force-inline slots");
			Slot slot = callee.slots[i];
			slot.name = names->SlotName();
			// The cloned home belongs to an inlined local value, not to a
			// boundary parameter of the caller.
			slot.parameter_origin = ParameterId();
			caller->slots.push_back(slot);
		}

		std::vector<Instruction> tail(
			caller->blocks[block_index].instructions.begin() +
				instruction_index + 1,
			caller->blocks[block_index].instructions.end());
		if (tail.empty())
			ThrowLoweringInternal("force-inline continuation is empty");
		const std::size_t cloned_count = callee.block_order.size();
		if (cloned_count == 0)
			ThrowLoweringInternal("force-inline callee has no ordered blocks");
		const std::size_t added_count = cloned_count + 2;
		if (caller->blocks.size() > kNoLowId - added_count)
			ThrowLoweringResourceLimit("too many force-inline blocks");
		const BlockId prologue = static_cast<BlockId>(caller->blocks.size());
		std::vector<BlockId> cloned(callee.blocks.size(), BlockId(kNoLowId));
		for (std::size_t i = 0; i < cloned_count; ++i)
		{
			const BlockId source_block = callee.block_order[i];
			if (source_block >= callee.blocks.size() ||
				cloned[source_block] != kNoLowId)
				ThrowLoweringInternal("invalid force-inline block order");
			cloned[source_block] = static_cast<BlockId>(
				caller->blocks.size() + 1 + i);
		}
		const BlockId continuation = static_cast<BlockId>(
			caller->blocks.size() + 1 + cloned_count);

		Block& call_block = caller->blocks[block_index];
		call_block.instructions.erase(
			call_block.instructions.begin() + instruction_index,
			call_block.instructions.end());
		call_block.instructions.push_back(JumpTo(prologue));
		call_block.terminated = true;

		Block prologue_block = lowering::presentation::MakePresentedBlock(
			program_, caller, names->BlockName());
		for (std::size_t i = 0; i < parameters.size(); ++i)
		{
			Instruction copy(Instruction::COPY);
			copy.dest = parameters[i];
			copy.type = callee.parameters[i].type;
			copy.first = arguments[i];
			prologue_block.instructions.push_back(copy);
		}
		prologue_block.instructions.push_back(
			JumpTo(cloned[callee.block_order[0]]));
		prologue_block.terminated = true;
		caller->blocks.push_back(prologue_block);

		for (std::size_t i = 0; i < cloned_count; ++i)
		{
			const BlockId source_block = callee.block_order[i];
			caller->blocks.push_back(CloneBlock(callee.blocks[source_block], callee,
				parameters, temporaries, callee_slot_base, cloned,
				continuation, result_slot, caller, names->BlockName()));
		}

		Block continuation_block = lowering::presentation::MakePresentedBlock(
			program_, caller, names->BlockName());
		if (callee.result.kind != LOW_VOID)
		{
			if (call.dest == kNoLowId)
				ThrowLoweringInternal(
					"force-inline value call has no result identity");
			Instruction load(Instruction::LOAD);
			load.dest = call.dest;
			load.type = call.type;
			load.first = Operand(result_slot, call.type);
			continuation_block.instructions.push_back(load);
		}
		continuation_block.instructions.insert(
			continuation_block.instructions.end(), tail.begin(), tail.end());
		continuation_block.terminated =
			IsTerminator(continuation_block.instructions.back());
		caller->blocks.push_back(continuation_block);

		std::vector<BlockId> order;
		order.reserve(added_count);
		order.push_back(prologue);
		for (std::size_t i = 0; i < cloned_count; ++i)
			order.push_back(cloned[callee.block_order[i]]);
		order.push_back(continuation);
		InsertBlockOrder(caller, static_cast<BlockId>(block_index), order);
		if (stats_)
		{
			++stats_->force_inline_calls;
			stats_->force_inline_blocks += added_count;
		}
	}

	void InlineCalls(std::size_t function_index)
	{
		Function& caller = program_.functions[function_index];
		PresentationNames names(&program_, &caller);
		std::uint32_t next_temp = caller.temporary_limit;
		for (std::size_t block = 0; block < caller.blocks.size(); ++block)
			for (std::size_t instruction = 0;
				instruction < caller.blocks[block].instructions.size(); ++instruction)
			{
				const std::size_t callee_index =
					Candidate(caller.blocks[block].instructions[instruction]);
				if (callee_index == kNoFunction || recursive_[callee_index])
					continue;
				if (callee_index == function_index)
					ThrowLoweringInternal(
						"nonrecursive force-inline self call");
				const Function& callee = program_.functions[callee_index];
				InlineCall(&caller, block, instruction, callee,
					&names, &next_temp);
				break;
			}
		caller.temporary_limit = next_temp;
	}

	void RecountOutput()
	{
		if (!stats_) return;
		stats_->blocks = 0;
		stats_->instructions = 0;
		for (std::size_t i = 0; i < program_.functions.size(); ++i)
		{
			stats_->blocks += program_.functions[i].block_order.size();
			for (std::size_t j = 0;
				j < program_.functions[i].blocks.size(); ++j)
				stats_->instructions +=
					program_.functions[i].blocks[j].instructions.size();
		}
	}
};

}

void RewriteProgram(lowering::ir::Program* program, lowering::Stats* stats,
	bool prune_unreachable_weak_functions)
{
	if (!program) ThrowLoweringInternal("force-inline program is null");
	ClassifyPresentationReservations(program);
	const lowering::reachability::Summary reachability =
		prune_unreachable_weak_functions ?
		lowering::reachability::PruneUnreachableWeakFunctions(program) :
		lowering::reachability::Analyze(program);
	lowering::presentation::FinalizeBlockPresentation(program,
		stats ? &stats->local_presentation : 0);
	if (stats)
	{
		const lowering::reachability::Summary internal_audit =
			lowering::reachability::AuditWithoutInternalRoots(*program);
		stats->post_inline_reachable_functions =
			reachability.reachable_functions;
		stats->post_inline_unreachable_weak_functions =
			reachability.unreachable_weak_functions;
		stats->post_inline_unreachable_internal_functions =
			internal_audit.unreachable_internal_functions;
		stats->post_inline_pruned_functions = reachability.pruned_functions;
		stats->post_inline_retained_external_strong =
			reachability.retained_external_strong;
		stats->post_inline_retained_address_or_relocation =
			reachability.retained_address_or_relocation;
		stats->post_inline_retained_direct_call =
			reachability.retained_direct_call;
		stats->post_inline_retained_lifecycle =
			reachability.retained_lifecycle;
		stats->post_inline_retained_eh_or_runtime =
			reachability.retained_eh_or_runtime;
		stats->post_inline_retained_required_weak =
			reachability.retained_required_weak;
		stats->post_inline_retained_object_output_root =
			reachability.retained_object_output_root;
		stats->post_inline_retained_object_output_root_weak =
			reachability.retained_object_output_root_weak;
		stats->post_inline_retained_object_output_root_internal =
			reachability.retained_object_output_root_internal;
		stats->post_inline_retained_conservative_fallback =
			reachability.retained_conservative_fallback;
		stats->post_inline_retained_conservative_fallback_names =
			reachability.retained_conservative_fallback_names;
		stats->post_inline_unreachable_internal_names =
			internal_audit.unreachable_internal_names;
		stats->functions = program->functions.size();
		stats->blocks = 0;
		stats->instructions = 0;
		for (std::size_t i = 0; i < program->functions.size(); ++i)
		{
			stats->blocks += program->functions[i].block_order.size();
			for (std::size_t j = 0;
				j < program->functions[i].blocks.size(); ++j)
				stats->instructions +=
					program->functions[i].blocks[j].instructions.size();
		}
	}
}

}  // namespace inline_policy
}  // namespace lowering
}  // namespace cppgm
