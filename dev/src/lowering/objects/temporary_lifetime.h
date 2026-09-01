#ifndef CPPGM_LOWERING_LIFETIME_TEMPORARIES_H
#define CPPGM_LOWERING_LIFETIME_TEMPORARIES_H

#include "semantic/model/graph.h"
#include "lowering/ir/model.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/objects/cleanup_continuations.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

template <class Derived>
class TemporaryLifetimeLowering
{
protected:
	static const std::size_t kInlineCleanupActionBudget = 8;
	TemporaryLifetimeLowering()
		: full_expression_cleanup_start_suppressed_(false) {}

	bool SetFullExpressionCleanupStartSuppressed(bool value)
	{
		const bool previous = full_expression_cleanup_start_suppressed_;
		full_expression_cleanup_start_suppressed_ = value;
		return previous;
	}

	SlotId EnsureTemporaryLifetimeSlot(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::uint32_t retained = kNoLowId;
		if (derived.temporary_lifetime_slots_.Find(node, &retained))
			return SlotId(retained);
		const SlotId result = static_cast<SlotId>(
			derived.function_->slots.size());
		Slot slot;
		slot.name = InternLocalName(derived.output_,
			derived.GeneratedSlotName("lifetime"));
		slot.type = LowU8();
		derived.function_->slots.push_back(slot);
		derived.temporary_lifetime_slots_.Insert(node, result);
		if (derived.stats_) ++derived.stats_->conditional_lifetime_slots;
		return result;
	}

	void ResetFullExpressionFunctionState()
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.cleanup_continuations_.Clear();
		derived.pending_cleanup_states_.clear();
		derived.full_expression_cleanup_state_ =
			lowering::cleanup::kNoCleanupState;
		derived.temporary_lifetime_slots_.Clear();
		derived.runtime_lifetime_temporaries_.Clear();
		derived.full_expression_branch_cleanup_heads_.Clear();
		derived.full_expression_branch_cleanup_tails_.Clear();
		full_expression_cleanup_start_suppressed_ = false;
		derived.full_expression_cleanup_ready_ = false;
		derived.full_expression_deferred_cleanup_ = false;
		derived.full_expression_uses_branch_cleanup_ = false;
		derived.conditional_cleanup_resume_ = kNoLowId;
	}

	std::uint32_t InternContinuation(
		const lowering::cleanup::Key& key, const char* label,
		bool* inserted)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.stats_) ++derived.stats_->cleanup_state_probes;
		const std::uint32_t state =
			derived.cleanup_continuations_.Intern(key, inserted);
		if (!*inserted)
		{
			if (derived.stats_) ++derived.stats_->cleanup_state_hits;
			return state;
		}
		const BlockId block = derived.AddBlock(derived.NewLabel(label));
		derived.cleanup_continuations_.BindBlock(state, block);
		derived.pending_cleanup_states_.push_back(state);
		if (derived.stats_)
		{
			++derived.stats_->cleanup_unique_states;
			++derived.stats_->cleanup_blocks_emitted;
		}
		return state;
	}

	BlockId ContinuationBlock(std::uint32_t state) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const lowering::cleanup::State& record =
			derived.cleanup_continuations_.Get(state);
		if (!record.block_bound)
			ThrowLoweringInternal("cleanup continuation has no block");
		return record.block;
	}

	std::uint32_t CleanupActionIdentity(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (node >= derived.arena_.nodes.size())
			ThrowLoweringInternal("invalid cleanup action node");
		const lowering::cleanup::ActionKey key =
			lowering::cleanup::MakeActionKey(derived.arena_.nodes[node]);
		bool inserted = false;
		return derived.cleanup_continuations_.InternAction(key, node, &inserted);
	}

	bool BuildFullExpressionCleanupDispatch(std::uint32_t context)
	{
		Derived& derived = static_cast<Derived&>(*this);
		using namespace lowering::cleanup;
		derived.pending_cleanup_states_.clear();
		bool inserted = false;
		std::uint32_t tail = InternContinuation(Key(kNoCleanupState,
			kNoCleanupState, 0, context, FULL_EXPRESSION_TERMINAL),
			"cleanup_resume", &inserted);
		for (std::size_t i = derived.full_expression_segment_actions_.size();
			i != 0; --i)
			tail = InternContinuation(Key(CleanupActionIdentity(
				derived.full_expression_segment_actions_[i - 1]), tail, 0,
				context, FULL_EXPRESSION_ACTION), "cleanup_action", &inserted);
		if (context != 0)
			tail = InternContinuation(Key(kNoCleanupState, tail, 0, context,
				FULL_EXPRESSION_LANDING), "call_unwind_dispatch", &inserted);
		derived.full_expression_cleanup_state_ = tail;
		derived.full_expression_cleanup_dispatch_ = ContinuationBlock(tail);
		return !inserted;
	}

	void MaterializePendingCleanupStates()
	{
		Derived& derived = static_cast<Derived&>(*this);
		using namespace lowering::cleanup;
		const BlockId original = derived.current_block_;
		for (std::size_t i = 0; i < derived.pending_cleanup_states_.size(); ++i)
		{
			const State state = derived.cleanup_continuations_.Get(
				derived.pending_cleanup_states_[i]);
			derived.SelectBlock(state.block);
			if (state.key.mode == FULL_EXPRESSION_TERMINAL)
			{
				const bool routes_to_try =
					derived.ExceptionCleanupRoutesToTry(state.key.context);
				derived.FinishExceptionCleanupDispatch(routes_to_try);
			}
			else if (state.key.mode == FULL_EXPRESSION_ACTION)
			{
				LowerFullExpressionDestructorAction(
					derived.cleanup_continuations_.GetAction(
						state.key.action).representative_node);
				derived.EmitJump(ContinuationBlock(state.key.tail));
			}
			else if (state.key.mode == FULL_EXPRESSION_LANDING)
			{
				const bool routes_to_try =
					derived.BeginExceptionTryCleanupDispatch();
				if (routes_to_try !=
					derived.ExceptionCleanupRoutesToTry(state.key.context))
					ThrowLoweringInternal("cleanup exception context changed");
				derived.FinishExceptionUnwindCleanupPrefix();
				derived.EmitJump(ContinuationBlock(state.key.tail));
			}
			else ThrowLoweringInternal(
				"invalid full-expression cleanup continuation mode");
		}
		derived.pending_cleanup_states_.clear();
		derived.SelectBlock(original);
	}

	BlockId InternCleanupAction(std::uint32_t action, BlockId tail)
	{
		Derived& derived = static_cast<Derived&>(*this);
		using namespace lowering::cleanup;
		const BlockId original = derived.current_block_;
		const BlockId previous_dispatch =
			derived.full_expression_cleanup_dispatch_;
		if (derived.stats_) ++derived.stats_->cleanup_dispatch_probes;
		std::uint32_t tail_state =
			derived.cleanup_continuations_.StateForBlock(tail);
		if (tail_state == kNoCleanupState)
		{
			bool inserted_tail = false;
			if (derived.stats_) ++derived.stats_->cleanup_state_probes;
			tail_state = derived.cleanup_continuations_.Intern(Key(
				kNoCleanupState, kNoCleanupState, tail,
				derived.ExceptionCleanupContext(), CONDITIONAL_EXTERNAL_TAIL),
				&inserted_tail);
			if (inserted_tail)
			{
				derived.cleanup_continuations_.BindBlock(tail_state, tail);
				if (derived.stats_) ++derived.stats_->cleanup_unique_states;
			}
			else if (derived.stats_) ++derived.stats_->cleanup_state_hits;
		}
		bool inserted = false;
		if (derived.stats_) ++derived.stats_->cleanup_state_probes;
		const std::uint32_t state = derived.cleanup_continuations_.Intern(Key(
			CleanupActionIdentity(action), tail_state, 0,
			derived.ExceptionCleanupContext(), CONDITIONAL_ACTION), &inserted);
		if (!inserted)
		{
			if (derived.stats_)
			{
				++derived.stats_->cleanup_dispatch_cache_hits;
				++derived.stats_->cleanup_state_hits;
				++derived.stats_->cleanup_destructor_actions_avoided;
			}
			return ContinuationBlock(state);
		}
		const BlockId block = derived.AddBlock(
			derived.NewLabel("conditional_cleanup_dispatch"));
		derived.cleanup_continuations_.BindBlock(state, block);
		if (derived.stats_)
		{
			++derived.stats_->cleanup_dispatch_entries;
			++derived.stats_->cleanup_unique_states;
			++derived.stats_->cleanup_blocks_emitted;
		}
		derived.full_expression_cleanup_dispatch_ = tail;
		derived.SelectBlock(block);
		LowerFullExpressionDestructorAction(action);
		derived.EmitJump(tail);
		derived.full_expression_cleanup_dispatch_ = previous_dispatch;
		derived.SelectBlock(original);
		return block;
	}

	BlockId InternConditionalCleanupDispatch()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.ExceptionCleanupContext() != 0)
		{
			const BlockId original = derived.current_block_;
			const BlockId landing = derived.AddBlock(
				derived.NewLabel("conditional_cleanup_landing"));
			const BlockId terminal = derived.AddBlock(
				derived.NewLabel("conditional_cleanup_route"));

			derived.SelectBlock(landing);
			const bool routes_to_try =
				derived.BeginExceptionTryCleanupDispatch();
			derived.FinishExceptionUnwindCleanupPrefix();

			derived.SelectBlock(terminal);
			derived.FinishExceptionCleanupDispatch(routes_to_try);

			BlockId tail = terminal;
			for (std::size_t i = derived.full_expression_segment_actions_.size();
				i != 0; --i)
				tail = InternCleanupAction(
					derived.full_expression_segment_actions_[i - 1], tail);
			derived.SelectBlock(landing);
			derived.EmitJump(tail);
			derived.SelectBlock(original);
			return landing;
		}
		if (derived.conditional_cleanup_resume_ == kNoLowId)
		{
			const BlockId original = derived.current_block_;
			derived.conditional_cleanup_resume_ = derived.AddBlock(
				derived.NewLabel("conditional_cleanup_resume"));
			derived.SelectBlock(derived.conditional_cleanup_resume_);
			derived.EmitExceptionResume();
			derived.SelectBlock(original);
		}
		BlockId tail = derived.conditional_cleanup_resume_;
		for (std::size_t i = derived.full_expression_segment_actions_.size();
			i != 0; --i)
			tail = InternCleanupAction(
				derived.full_expression_segment_actions_[i - 1], tail);
		return tail;
	}

	void StartFullExpressionCleanupSegment()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.full_expression_tracks_lifetime_state_ ||
			derived.full_expression_uses_linked_dispatch_)
		{
			derived.full_expression_cleanup_dispatch_ =
				derived.full_expression_tracks_lifetime_state_ ?
				derived.runtime_lifetime_cleanup_dispatch_ :
				derived.full_expression_linked_cleanup_dispatch_;
			derived.full_expression_cleanup_dispatch_reused_ = true;
			derived.full_expression_cleanup_end_ = kNoLowId;
			derived.EmitEhTarget(Instruction::EH_TRY,
				derived.full_expression_cleanup_dispatch_);
			return;
		}
		derived.full_expression_segment_actions_.clear();
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
		{
			const std::uint32_t action =
				derived.full_expression_cleanup_actions_[i];
			const std::uint32_t temporary =
				derived.arena_.nodes[action].lifetime_object;
			if (derived.arena_.nodes[action].unwind_only ||
				(temporary != kNoDumpEdge &&
				 derived.temporary_initialized_[temporary]))
				derived.full_expression_segment_actions_.push_back(action);
		}
		const std::uint32_t exception_context =
			derived.ExceptionCleanupContext();
		if (derived.stats_) ++derived.stats_->cleanup_dispatch_probes;
		derived.full_expression_cleanup_dispatch_reused_ =
			BuildFullExpressionCleanupDispatch(exception_context);
		if (derived.full_expression_cleanup_dispatch_reused_)
		{
			if (derived.stats_) ++derived.stats_->cleanup_dispatch_cache_hits;
			if (derived.stats_)
			{
				derived.stats_->cleanup_destructor_actions_avoided +=
					derived.full_expression_segment_actions_.size();
				++derived.stats_->cleanup_resume_operations_avoided;
			}
		}
		else if (derived.stats_) ++derived.stats_->cleanup_dispatch_entries;
		derived.full_expression_cleanup_end_ = kNoLowId;
		derived.EmitEhTarget(Instruction::EH_TRY,
			derived.full_expression_cleanup_dispatch_);
	}

	void EnsureFullExpressionCleanupSegment()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (full_expression_cleanup_start_suppressed_) return;
		if (derived.full_expression_cleanup_active_ &&
			derived.full_expression_cleanup_dispatch_ == kNoLowId &&
			(!derived.full_expression_deferred_cleanup_ ||
			 derived.full_expression_cleanup_ready_))
			StartFullExpressionCleanupSegment();
	}

	bool IsConditionalTemporaryAction(std::uint32_t action) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[action];
		return record.lifetime_object != kNoDumpEdge &&
			derived.arena_.nodes[record.lifetime_object].
				conditionally_constructed;
	}

	bool HasBranchCleanupFact(std::uint32_t action) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[action];
		return record.lifetime_object != kNoDumpEdge &&
			record.lifetime_branch_owner != kNoDumpEdge &&
			record.lifetime_branch_child != kNoDumpEdge;
	}

	bool IsBranchCleanupAction(std::uint32_t action) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		return derived.full_expression_uses_branch_cleanup_ &&
			HasBranchCleanupFact(action);
	}

	bool IsRetiredBranchCleanupAction(std::uint32_t action) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (derived.arena_.nodes[action].
			lifetime_branch_statically_unreachable) return true;
		if (!IsBranchCleanupAction(action)) return false;
		const std::uint32_t temporary =
			derived.arena_.nodes[action].lifetime_object;
		return temporary != kNoDumpEdge &&
			!derived.temporary_initialized_[temporary];
	}

	void IndexBranchCleanupActions()
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.full_expression_branch_cleanup_heads_.Clear();
		derived.full_expression_branch_cleanup_tails_.Clear();
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
		{
			const std::uint32_t action =
				derived.full_expression_cleanup_actions_[i];
			if (derived.arena_.nodes[action].
				lifetime_branch_statically_unreachable) continue;
			if (!IsBranchCleanupAction(action)) continue;
			const std::uint32_t child =
				derived.arena_.nodes[action].lifetime_branch_child;
			const std::uint32_t owner =
				derived.arena_.nodes[action].lifetime_branch_owner;
			std::uint32_t tail = kNoDumpEdge;
			if (!derived.full_expression_branch_cleanup_heads_.Find(
				owner, child, &tail))
				derived.full_expression_branch_cleanup_heads_.Insert(
					owner, child, action);
			else
			{
				if (!derived.full_expression_branch_cleanup_tails_.Find(
					owner, child, &tail))
					ThrowLoweringInternal(
						"branch cleanup head has no ordered tail");
				derived.full_expression_branch_cleanup_next_[tail] = action;
			}
			derived.full_expression_branch_cleanup_tails_.Insert(
				owner, child, action);
			derived.full_expression_branch_cleanup_next_[action] = kNoDumpEdge;
		}
	}

	void LowerBranchCleanupActions(std::uint32_t owner,
		std::uint32_t child)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_uses_branch_cleanup_) return;
		std::uint32_t action = kNoDumpEdge;
		if (!derived.full_expression_branch_cleanup_heads_.Find(
			owner, child, &action))
			return;
		SmallSequence<std::uint32_t, kInlineCleanupActionBudget> retired;
		while (action != kNoDumpEdge)
		{
			const DumpNode& record = derived.arena_.nodes[action];
			if (record.lifetime_branch_owner != owner ||
				record.lifetime_branch_child != child ||
				record.lifetime_object == kNoDumpEdge)
				ThrowLoweringInternal("invalid branch-local cleanup identity");
			LowerFullExpressionDestructorAction(action);
			retired.Push(record.lifetime_object);
			if (derived.stats_) ++derived.stats_->branch_cleanup_actions;
			action = derived.full_expression_branch_cleanup_next_[action];
		}
		PauseFullExpressionCleanupSegment();
		for (std::size_t i = 0; i < retired.size(); ++i)
			derived.temporary_initialized_[retired[i]] = 0;
	}

	void ReadyFullExpressionCleanupForTemporary(std::uint32_t temporary)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_cleanup_active_ ||
			!derived.full_expression_uses_branch_cleanup_ ||
			derived.full_expression_cleanup_ready_) return;
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].lifetime_object ==
					temporary)
			{
				derived.full_expression_cleanup_ready_ = true;
				return;
			}
	}

	bool UsesRuntimeLifetimeState(std::uint32_t action) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const std::uint32_t temporary =
			derived.arena_.nodes[action].lifetime_object;
		std::uint32_t ignored = 0;
		return temporary != kNoDumpEdge &&
			derived.runtime_lifetime_temporaries_.Find(temporary, &ignored);
	}

	void PrepareRuntimeLifetimeState()
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.runtime_lifetime_temporaries_.Clear();
		derived.full_expression_branch_cleanup_heads_.Clear();
		derived.full_expression_branch_cleanup_tails_.Clear();
		derived.full_expression_tracks_lifetime_state_ = false;
		bool has_branch_cleanup = false;
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (IsConditionalTemporaryAction(
				derived.full_expression_cleanup_actions_[i]))
			{
				if (HasBranchCleanupFact(
					derived.full_expression_cleanup_actions_[i]))
					has_branch_cleanup = true;
				else derived.full_expression_tracks_lifetime_state_ = true;
			}
		derived.full_expression_uses_branch_cleanup_ = has_branch_cleanup &&
			!derived.full_expression_tracks_lifetime_state_ &&
			derived.full_expression_cleanup_actions_.size() <=
				kInlineCleanupActionBudget;
		if (has_branch_cleanup &&
			!derived.full_expression_uses_branch_cleanup_)
			derived.full_expression_tracks_lifetime_state_ = true;
		if (derived.full_expression_uses_branch_cleanup_)
			IndexBranchCleanupActions();
		if (derived.full_expression_tracks_lifetime_state_)
			derived.full_expression_uses_linked_dispatch_ = false;
		if (!derived.full_expression_tracks_lifetime_state_ &&
			!derived.full_expression_uses_linked_dispatch_)
		{
			derived.runtime_lifetime_cleanup_dispatch_ = kNoLowId;
			return;
		}
		derived.full_expression_linked_action_cursor_ =
			derived.full_expression_cleanup_actions_.size();
		derived.full_expression_segment_actions_.clear();
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
		{
			const std::uint32_t action =
				derived.full_expression_cleanup_actions_[i];
			const std::uint32_t temporary =
				derived.arena_.nodes[action].lifetime_object;
			if (derived.full_expression_uses_linked_dispatch_)
			{
				if (derived.arena_.nodes[action].unwind_only ||
					(temporary != kNoDumpEdge &&
					 derived.temporary_initialized_[temporary]))
					derived.full_expression_segment_actions_.push_back(action);
				continue;
			}
			derived.full_expression_segment_actions_.push_back(action);
			if (temporary == kNoDumpEdge) continue;
			derived.runtime_lifetime_temporaries_.Insert(temporary, 1);
			Instruction reset(Instruction::STORE);
			reset.type = LowU8();
			reset.first = Operand(0, LowU8());
			reset.second = Operand(
				derived.EnsureTemporaryLifetimeSlot(temporary), LowU8());
			derived.Emit(reset);
		}
		if (derived.full_expression_uses_linked_dispatch_)
			derived.full_expression_linked_action_cursor_ -=
				derived.full_expression_segment_actions_.size();
		const BlockId dispatch = InternConditionalCleanupDispatch();
		if (derived.full_expression_uses_linked_dispatch_)
			derived.full_expression_linked_cleanup_dispatch_ = dispatch;
		else derived.runtime_lifetime_cleanup_dispatch_ = dispatch;
	}

	void MarkConditionalTemporaryConstructed(std::uint32_t temporary)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::uint32_t tracked = 0;
		if (!derived.full_expression_cleanup_active_ ||
			!derived.runtime_lifetime_temporaries_.Find(temporary, &tracked))
			return;
		Instruction mark(Instruction::STORE);
		mark.type = LowU8();
		mark.first = Operand(1, LowU8());
		mark.second = Operand(
			derived.EnsureTemporaryLifetimeSlot(temporary), LowU8());
		derived.Emit(mark);
		if (derived.stats_) ++derived.stats_->conditional_lifetime_marks;
	}

	void LowerFullExpressionDestructorAction(std::uint32_t action)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[action];
		if (record.exception_handler_exit)
		{
			derived.FinishExceptionHandlerUnwindBoundary(
				record.exception_cleanup_region_exit);
			return;
		}
		if (!UsesRuntimeLifetimeState(action))
		{
			derived.LowerDestructorAction(record);
			return;
		}
		const std::uint32_t temporary =
			derived.arena_.nodes[action].lifetime_object;
		const Operand state = derived.LoadStorage(Operand(
			derived.EnsureTemporaryLifetimeSlot(temporary), LowU8()), LowU8());
		const BlockId destroy = derived.AddBlock(
			derived.NewLabel("conditional_temporary_destroy"));
		const BlockId done = derived.AddBlock(
			derived.NewLabel("conditional_temporary_done"));
		derived.EmitBranch(state, destroy, done);
		derived.SelectBlock(destroy);
		Instruction clear(Instruction::STORE);
		clear.type = LowU8();
		clear.first = Operand(0, LowU8());
		clear.second = Operand(
			derived.EnsureTemporaryLifetimeSlot(temporary), LowU8());
		derived.Emit(clear);
		const DumpNode& cleanup = record;
		const Operand destination = derived.AddressOfStorage(
			derived.TemporaryObjectStorageSlot(temporary));
		derived.LowerDestructorObject(
			cleanup.operand_type, destination, cleanup.binding);
		derived.EmitJump(done);
		derived.SelectBlock(done);
	}

	void PauseFullExpressionCleanupSegment(
		const char* end_prefix = "call_unwind_end")
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_cleanup_active_ ||
			derived.full_expression_cleanup_dispatch_ == kNoLowId)
			return;
		CloseFullExpressionCleanupSegment(end_prefix);
		derived.full_expression_cleanup_dispatch_ = kNoLowId;
		derived.full_expression_cleanup_end_ = kNoLowId;
	}

	void CloseFullExpressionCleanupSegment(
		const char* end_prefix = "call_unwind_end")
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.Emit(Instruction(Instruction::EH_END));
		if (derived.full_expression_cleanup_dispatch_reused_) return;
		const BlockId end = derived.AddBlock(
			derived.NewLabel(end_prefix));
		derived.full_expression_cleanup_end_ = end;
		derived.EmitJump(end);
		MaterializePendingCleanupStates();
		derived.SelectBlock(end);
	}

	bool RetireFullExpressionNormalActionsBeforeNoreturn()
	{
		Derived& derived = static_cast<Derived&>(*this);
		bool has_normal = false;
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (!derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only)
				has_normal = true;
		if (!has_normal) return false;
		EnsureFullExpressionCleanupSegment();
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (!derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only)
				LowerFullExpressionDestructorAction(
					derived.full_expression_cleanup_actions_[i]);
		PauseFullExpressionCleanupSegment();
		std::size_t retained = 0;
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only)
				derived.full_expression_cleanup_actions_[retained++] =
					derived.full_expression_cleanup_actions_[i];
		derived.full_expression_cleanup_actions_.resize(retained);
		derived.full_expression_segment_actions_.clear();
		derived.full_expression_cleanup_ready_ = retained != 0;
		derived.full_expression_deferred_cleanup_ = false;
		derived.full_expression_tracks_lifetime_state_ = false;
		derived.full_expression_uses_linked_dispatch_ = false;
		derived.full_expression_uses_branch_cleanup_ = false;
		derived.runtime_lifetime_cleanup_dispatch_ = kNoLowId;
		derived.runtime_lifetime_temporaries_.Clear();
		derived.full_expression_branch_cleanup_heads_.Clear();
		derived.full_expression_branch_cleanup_tails_.Clear();
		derived.full_expression_linked_action_cursor_ = 0;
		return true;
	}

	void FinishNoreturnFullExpressionCleanup()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_cleanup_active_)
			ThrowLoweringInternal(
				"noreturn cleanup outside full expression");
		EnsureFullExpressionCleanupSegment();
		derived.Emit(Instruction(Instruction::EH_END));
		if (!derived.full_expression_cleanup_dispatch_reused_)
		{
			MaterializePendingCleanupStates();
		}
		ResetFullExpressionCleanup();
	}

	void BeginFullExpressionCleanup(const NodeChildren& children,
		std::size_t first_cleanup, bool defer_segment = false,
		BlockId preferred_dispatch = kNoLowId)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.full_expression_cleanup_active_)
			ThrowLoweringInternal("nested full-expression cleanup region");
		derived.full_expression_cleanup_actions_.clear();
		for (std::size_t i = first_cleanup; i < children.size(); ++i)
		{
			if (derived.arena_.nodes[children[i]].kind !=
					DUMP_DESTRUCTOR_ACTION ||
				(derived.arena_.nodes[children[i]].lifetime_object == kNoDumpEdge &&
				 !derived.arena_.nodes[children[i]].unwind_only))
				ThrowLoweringInternal("invalid temporary cleanup suffix");
			derived.full_expression_cleanup_actions_.push_back(children[i]);
		}
		derived.full_expression_cleanup_active_ = true;
		derived.full_expression_cleanup_dispatch_ = kNoLowId;
		derived.full_expression_cleanup_end_ = kNoLowId;
		derived.full_expression_cleanup_dispatch_reused_ = false;
		derived.full_expression_linked_cleanup_dispatch_ = kNoLowId;
		derived.full_expression_cleanup_ready_ = false;
		derived.full_expression_deferred_cleanup_ = false;
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
		{
			const DumpNode& action = derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]];
			if (action.managed_full_expression_cleanup)
				derived.full_expression_deferred_cleanup_ = true;
			if (action.unwind_only ||
				(action.lifetime_object != kNoDumpEdge &&
				 (derived.arena_.nodes[action.lifetime_object].
					conditionally_constructed ||
				  derived.temporary_initialized_[action.lifetime_object])))
				derived.full_expression_cleanup_ready_ = true;
		}
		derived.full_expression_uses_linked_dispatch_ =
			derived.full_expression_cleanup_actions_.size() >
				kInlineCleanupActionBudget;
		PrepareRuntimeLifetimeState();
		if (derived.full_expression_uses_branch_cleanup_)
		{
			bool has_preexisting_cleanup = false;
			for (std::size_t i = 0;
				i < derived.full_expression_cleanup_actions_.size(); ++i)
			{
				const DumpNode& action = derived.arena_.nodes[
					derived.full_expression_cleanup_actions_[i]];
				has_preexisting_cleanup = has_preexisting_cleanup ||
					action.unwind_only ||
					(action.lifetime_object != kNoDumpEdge &&
					 derived.temporary_initialized_[action.lifetime_object]);
			}
			if (!has_preexisting_cleanup)
			{
				derived.full_expression_deferred_cleanup_ = true;
				derived.full_expression_cleanup_ready_ = false;
			}
		}
		if (preferred_dispatch != kNoLowId &&
			!derived.full_expression_cleanup_actions_.empty())
			ThrowLoweringInternal(
				"preferred cleanup dispatch has destructor actions");
		if (defer_segment ||
			(derived.full_expression_uses_branch_cleanup_ &&
			 derived.full_expression_deferred_cleanup_ &&
			 !derived.full_expression_cleanup_ready_)) return;
		if (preferred_dispatch == kNoLowId)
			StartFullExpressionCleanupSegment();
		else
		{
			derived.full_expression_cleanup_dispatch_ = preferred_dispatch;
			derived.full_expression_cleanup_dispatch_reused_ = true;
			derived.EmitEhTarget(Instruction::EH_TRY, preferred_dispatch);
		}
	}

	void TransitionFullExpressionCleanup(std::uint32_t temporary)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_cleanup_active_)
			ThrowLoweringInternal(
				"temporary transition outside full expression");
		if (!derived.full_expression_deferred_cleanup_)
		{
			if (derived.full_expression_cleanup_dispatch_ != kNoLowId)
				CloseFullExpressionCleanupSegment();
			if (derived.full_expression_uses_linked_dispatch_ &&
				derived.full_expression_linked_action_cursor_ != 0)
			{
				const std::uint32_t action =
					derived.full_expression_cleanup_actions_[
						derived.full_expression_linked_action_cursor_ - 1];
				if (derived.arena_.nodes[action].lifetime_object == temporary)
				{
					--derived.full_expression_linked_action_cursor_;
					derived.full_expression_linked_cleanup_dispatch_ =
						InternCleanupAction(action,
							derived.full_expression_linked_cleanup_dispatch_);
				}
			}
			derived.full_expression_cleanup_dispatch_ = kNoLowId;
			StartFullExpressionCleanupSegment();
			return;
		}
		bool tracked = false;
		bool eager_transition = false;
		bool unreachable_branch = false;
		if (derived.full_expression_uses_linked_dispatch_)
		{
			if (derived.full_expression_linked_action_cursor_ != 0)
			{
				const std::uint32_t action =
					derived.full_expression_cleanup_actions_[
						derived.full_expression_linked_action_cursor_ - 1];
				tracked = derived.arena_.nodes[action].lifetime_object == temporary;
				unreachable_branch = tracked && derived.arena_.nodes[action].
					lifetime_branch_statically_unreachable;
				eager_transition = tracked &&
					derived.arena_.nodes[action].eager_full_expression_cleanup;
			}
		}
		else
		{
			// The inline path is deliberately bounded by
			// kInlineCleanupActionBudget.  Trivial temporaries have no cleanup
			// action and cannot become a new cleanup boundary.
			for (std::size_t i = 0;
				i < derived.full_expression_cleanup_actions_.size(); ++i)
				if (derived.arena_.nodes[
					derived.full_expression_cleanup_actions_[i]].lifetime_object ==
						temporary)
				{
					tracked = true;
					unreachable_branch = derived.arena_.nodes[
						derived.full_expression_cleanup_actions_[i]].
						lifetime_branch_statically_unreachable;
					eager_transition = derived.arena_.nodes[
						derived.full_expression_cleanup_actions_[i]].
						eager_full_expression_cleanup;
					break;
				}
		}
		if (!tracked)
		{
			if (!derived.full_expression_cleanup_ready_ &&
				derived.full_expression_cleanup_dispatch_ != kNoLowId)
			{
				CloseFullExpressionCleanupSegment();
				derived.full_expression_cleanup_dispatch_ = kNoLowId;
				derived.full_expression_cleanup_end_ = kNoLowId;
			}
			return;
		}
		if (unreachable_branch)
		{
			if (derived.full_expression_cleanup_dispatch_ != kNoLowId)
				CloseFullExpressionCleanupSegment();
			derived.full_expression_cleanup_dispatch_ = kNoLowId;
			derived.full_expression_cleanup_end_ = kNoLowId;
			derived.full_expression_cleanup_ready_ = false;
			return;
		}
		derived.full_expression_cleanup_ready_ = true;
		if (derived.full_expression_cleanup_dispatch_ != kNoLowId)
			CloseFullExpressionCleanupSegment();
		if (derived.full_expression_uses_linked_dispatch_)
		{
			if (derived.full_expression_linked_action_cursor_ != 0)
			{
				const std::uint32_t action =
					derived.full_expression_cleanup_actions_[
						derived.full_expression_linked_action_cursor_ - 1];
				if (derived.arena_.nodes[action].lifetime_object == temporary)
				{
					--derived.full_expression_linked_action_cursor_;
					derived.full_expression_linked_cleanup_dispatch_ =
						InternCleanupAction(action,
							derived.full_expression_linked_cleanup_dispatch_);
				}
			}
		}
		derived.full_expression_cleanup_dispatch_ = kNoLowId;
		if (eager_transition) EnsureFullExpressionCleanupSegment();
	}

	void ResetFullExpressionCleanup()
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.full_expression_cleanup_active_ = false;
		derived.full_expression_cleanup_actions_.clear();
		derived.full_expression_segment_actions_.clear();
		derived.full_expression_cleanup_dispatch_ = kNoLowId;
		derived.full_expression_cleanup_end_ = kNoLowId;
		derived.full_expression_cleanup_dispatch_reused_ = false;
		derived.full_expression_linked_cleanup_dispatch_ = kNoLowId;
		derived.full_expression_cleanup_ready_ = false;
		derived.full_expression_deferred_cleanup_ = false;
		derived.full_expression_tracks_lifetime_state_ = false;
		derived.full_expression_uses_linked_dispatch_ = false;
		derived.full_expression_uses_branch_cleanup_ = false;
		derived.runtime_lifetime_cleanup_dispatch_ = kNoLowId;
		derived.runtime_lifetime_temporaries_.Clear();
		derived.full_expression_branch_cleanup_heads_.Clear();
		derived.full_expression_branch_cleanup_tails_.Clear();
		derived.full_expression_linked_action_cursor_ = 0;
	}

	void CompleteFullExpressionCleanup()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_cleanup_active_)
			ThrowLoweringInternal("missing full-expression cleanup region");
		if (derived.full_expression_uses_linked_dispatch_ &&
			derived.full_expression_linked_action_cursor_ != 0)
			ThrowLoweringInternal("linked cleanup left an unconstructed action");
		bool has_normal_cleanup = false;
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (!derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only &&
				!IsRetiredBranchCleanupAction(
					derived.full_expression_cleanup_actions_[i]))
				has_normal_cleanup = true;
		if (!has_normal_cleanup)
		{
			PauseFullExpressionCleanupSegment();
			ResetFullExpressionCleanup();
			return;
		}
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (!derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only &&
				!IsRetiredBranchCleanupAction(
					derived.full_expression_cleanup_actions_[i]))
				LowerFullExpressionDestructorAction(
					derived.full_expression_cleanup_actions_[i]);
		if (derived.full_expression_cleanup_dispatch_ != kNoLowId)
			CloseFullExpressionCleanupSegment();
		ResetFullExpressionCleanup();
	}

	Operand RetainFullExpressionCallResult(std::uint32_t node,
		const DumpNode& record, const Operand& result)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_cleanup_active_ ||
			!record.full_expression_staging)
			return result;
		const Operand slot(derived.EnsureGeneratedSlot(
			node, "call", result.type), result.type);
		if (result.type.kind == LOW_OBJECT)
		{
			derived.EmitClassObjectCopy(
				record.type, result, derived.AddressOfStorage(slot));
			return slot;
		}
		Instruction store(Instruction::STORE);
		store.type = result.type;
		store.first = result;
		store.second = slot;
		derived.Emit(store);
		return derived.LoadStorage(slot, result.type);
	}

	void EmitFullExpressionConditionBranch(const NodeChildren& children,
		BlockId true_block, BlockId false_block,
		BlockId preferred_dispatch = kNoLowId)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.empty())
			ThrowLoweringInternal("condition cleanup has no value");
		if (children.size() == 1)
		{
			BeginFullExpressionCleanup(children, 1, false, preferred_dispatch);
			derived.EmitConditionBranch(children[0], true_block, false_block);
			ResetFullExpressionCleanup();
			return;
		}
		BeginFullExpressionCleanup(children, 1, true);
		const Operand value = derived.LowerCondition(children[0]);
		PauseFullExpressionCleanupSegment();
		bool has_normal_cleanup = false;
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (!derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only &&
				!IsRetiredBranchCleanupAction(
					derived.full_expression_cleanup_actions_[i]))
				has_normal_cleanup = true;
		if (!has_normal_cleanup)
		{
			derived.EmitBranch(value, true_block, false_block);
			ResetFullExpressionCleanup();
			return;
		}
		const BlockId true_cleanup = derived.AddBlock(
			derived.NewLabel("cond_true_cleanup"));
		const BlockId false_cleanup = derived.AddBlock(
			derived.NewLabel("cond_false_cleanup"));
		derived.EmitBranch(value, true_cleanup, false_cleanup);
		derived.SelectBlock(true_cleanup);
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (!derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only &&
				!IsRetiredBranchCleanupAction(
					derived.full_expression_cleanup_actions_[i]))
				LowerFullExpressionDestructorAction(
					derived.full_expression_cleanup_actions_[i]);
		derived.EmitJump(true_block);
		derived.SelectBlock(false_cleanup);
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (!derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only &&
				!IsRetiredBranchCleanupAction(
					derived.full_expression_cleanup_actions_[i]))
				LowerFullExpressionDestructorAction(
					derived.full_expression_cleanup_actions_[i]);
		derived.EmitJump(false_block);
		ResetFullExpressionCleanup();
	}

	void LowerFullExpressionStatement(const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		bool managed_cleanup = !children.empty() &&
			derived.arena_.nodes[children[0]].full_expression_staging;
		bool lexical_unwind = false;
		for (std::size_t i = 1; i < children.size(); ++i)
			if (derived.arena_.nodes[children[i]].kind ==
					DUMP_DESTRUCTOR_ACTION &&
				(IsConditionalTemporaryAction(children[i]) ||
				 derived.arena_.nodes[children[i]].unwind_only))
			{
				managed_cleanup = true;
				lexical_unwind = lexical_unwind ||
					derived.arena_.nodes[children[i]].unwind_only;
			}
		if (managed_cleanup)
		{
			BeginFullExpressionCleanup(children, 1, lexical_unwind);
			const bool noreturn = !children.empty() &&
				derived.IsDirectNoreturnCall(children[0]);
			if (!children.empty()) derived.LowerDiscardedValue(children[0]);
			if (noreturn)
			{
				FinishNoreturnFullExpressionCleanup();
				derived.TerminateAfterNoreturnCall();
				return;
			}
			CompleteFullExpressionCleanup();
			return;
		}
		const bool noreturn = !children.empty() &&
			derived.IsDirectNoreturnCall(children[0]);
		if (!children.empty()) derived.LowerDiscardedValue(children[0]);
		if (noreturn)
		{
			derived.TerminateAfterNoreturnCall();
			return;
		}
		for (std::size_t i = 1; i < children.size(); ++i)
		{
			if (derived.arena_.nodes[children[i]].kind != DUMP_DESTRUCTOR_ACTION)
				ThrowLoweringInternal(
					"invalid full-expression cleanup action");
			derived.LowerDestructorAction(derived.arena_.nodes[children[i]]);
		}
	}

	bool TryLowerFullExpressionDeclaration(const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::size_t first_cleanup = children.size();
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& node = derived.arena_.nodes[children[i]];
			if (node.kind == DUMP_DESTRUCTOR_ACTION &&
				node.lifetime_object != kNoDumpEdge)
			{
				first_cleanup = i;
				break;
			}
		}
		if (first_cleanup == children.size()) return false;
		for (std::size_t i = first_cleanup; i < children.size(); ++i)
			if (derived.arena_.nodes[children[i]].kind != DUMP_DESTRUCTOR_ACTION ||
				derived.arena_.nodes[children[i]].lifetime_object == kNoDumpEdge)
				return false;
		const DumpNode& last_action =
			derived.arena_.nodes[children[children.size() - 1]];
		const bool enclosing_lifetime_cleanup = !children.empty() &&
			derived.arena_.nodes[children[0]].kind == DUMP_VARIABLE &&
			derived.arena_.nodes[children[0]].enclosing_lifetime_cleanup;
		const bool first_temporary_is_default_argument =
			last_action.lifetime_object != kNoDumpEdge &&
			derived.arena_.nodes[last_action.lifetime_object].default_argument;
		Operand early_destination;
		if (enclosing_lifetime_cleanup &&
			first_temporary_is_default_argument &&
			derived.IsClassObjectType(derived.arena_.nodes[children[0]].type))
			early_destination = derived.AddressOfStorage(derived.StorageFor(
				derived.arena_.nodes[children[0]].binding,
				derived.LowerStorageType(
					derived.arena_.nodes[children[0]].type)));
		BeginFullExpressionCleanup(
			children, first_cleanup,
			first_temporary_is_default_argument &&
				!enclosing_lifetime_cleanup);
		for (std::size_t i = 0; i < first_cleanup; ++i)
		{
			if (i == 0 && early_destination.kind != Operand::NONE)
				LowerFullExpressionVariableInitialization(
					derived.arena_.nodes[children[i]],
					derived.Children(children[i]), early_destination);
			else derived.LowerStatementNode(children[i]);
		}
		CompleteFullExpressionCleanup();
		return true;
	}

	void LowerFullExpressionVariableInitialization(const DumpNode& record,
		const NodeChildren& children,
		const Operand& retained_destination = Operand())
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::size_t first_cleanup = children.size();
		for (std::size_t i = 0; i < children.size(); ++i)
			if (derived.arena_.nodes[children[i]].kind == DUMP_DESTRUCTOR_ACTION)
			{
				first_cleanup = i;
				break;
			}
		if (first_cleanup == children.size())
		{
			derived.LowerVariableInitializationCore(
				record, children, retained_destination);
			return;
		}
		NodeChildren values;
		bool lexical_unwind = false;
		for (std::size_t i = 0; i < first_cleanup; ++i)
			values.Push(children[i]);
		for (std::size_t i = first_cleanup; i < children.size(); ++i)
		{
			if (derived.arena_.nodes[children[i]].kind != DUMP_DESTRUCTOR_ACTION)
				ThrowLoweringInternal(
					"variable cleanup action is not a suffix");
			lexical_unwind = lexical_unwind ||
				derived.arena_.nodes[children[i]].unwind_only;
		}
		bool first_temporary_is_default_argument = false;
		if (first_cleanup < children.size())
		{
			const DumpNode& last_action =
				derived.arena_.nodes[children[children.size() - 1]];
			first_temporary_is_default_argument =
				last_action.lifetime_object != kNoDumpEdge &&
				derived.arena_.nodes[last_action.lifetime_object].default_argument;
		}
		Operand early_destination = retained_destination;
		if (early_destination.kind == Operand::NONE &&
			first_temporary_is_default_argument &&
			record.enclosing_lifetime_cleanup &&
			derived.IsClassObjectType(record.type))
			early_destination = derived.AddressOfStorage(derived.StorageFor(
				record.binding, derived.LowerStorageType(record.type)));
		BeginFullExpressionCleanup(children, first_cleanup,
			(lexical_unwind && !(first_temporary_is_default_argument &&
				record.enclosing_lifetime_cleanup)) ||
			(first_temporary_is_default_argument &&
				!record.enclosing_lifetime_cleanup));
		derived.LowerVariableInitializationCore(
			record, values, early_destination);
		CompleteFullExpressionCleanup();
	}

	Operand LowerFullExpressionCondition(const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.empty())
			ThrowLoweringInternal("empty full-expression condition");
		if (children.size() == 1) return derived.LowerCondition(children[0]);
		BeginFullExpressionCleanup(children, 1);
		const Operand value = derived.LowerCondition(children[0]);
		CompleteFullExpressionCleanup();
		return value;
	}

	void LowerClassDestination(std::uint32_t node, const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[node];
		const NodeChildren children = derived.Children(node);
		if (record.kind == DUMP_STATEMENT_EXPRESSION)
		{
			derived.LowerStatementExpressionDestination(node, destination);
			return;
		}
		if (record.kind == DUMP_CONSTRUCTOR_ACTION)
		{
			derived.LowerConstructorAction(node, destination);
			return;
		}
		if (record.kind == DUMP_CLASS_VALUE_TRANSFER)
		{
			derived.LowerClassValueTransfer(node, destination);
			return;
		}
		if (record.kind == DUMP_CALL_EXPRESSION)
		{
			(void)derived.LowerCall(node, record, children, destination);
			return;
		}
		if (record.kind == DUMP_BRACED_INIT_LIST)
		{
			derived.LowerRuntimeObjectValue(record.type, node, destination);
			return;
		}
		if (record.kind == DUMP_INITIALIZER_LIST)
		{
			derived.LowerInitializerListObject(node, destination);
			return;
		}
		derived.EmitClassObjectCopy(record.type,
			derived.AddressOfStorage(derived.LowerStorage(node)), destination);
	}

	void LowerClassConditionalArm(std::uint32_t node,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const NodeChildren children = derived.Children(node);
		if (derived.arena_.nodes[node].kind != DUMP_CONDITIONAL_ARM ||
			children.empty())
			ThrowLoweringInternal("invalid class conditional arm");
		const bool enclosing_cleanup = derived.full_expression_cleanup_active_;
		if (children.size() != 1)
		{
			bool has_enclosing_unwind = false;
			for (std::size_t i = 1; i < children.size(); ++i)
				if (derived.arena_.nodes[children[i]].unwind_only)
					has_enclosing_unwind = true;
			for (std::size_t i = 1; has_enclosing_unwind &&
				i < children.size(); ++i)
			{
				const DumpNode& action = derived.arena_.nodes[children[i]];
				if (!action.unwind_only &&
					action.lifetime_object != kNoDumpEdge)
					(void)derived.PrepareTemporaryObjectStorage(
						action.lifetime_object);
			}
			if (!enclosing_cleanup) BeginFullExpressionCleanup(children, 1);
		}
		if (derived.arena_.nodes[children[0]].kind == DUMP_THROW_EXPRESSION)
			(void)derived.LowerValue(children[0]);
		else LowerClassDestination(children[0], destination);
		if (!derived.CurrentBlock().terminated &&
			children.size() != 1 && !enclosing_cleanup)
			CompleteFullExpressionCleanup();
	}

	void LowerClassConditionalResult(std::uint32_t node,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const NodeChildren children = derived.Children(node);
		if (derived.arena_.nodes[node].kind != DUMP_CONDITIONAL_EXPRESSION ||
			children.size() != 3 ||
			derived.arena_.nodes[children[1]].kind != DUMP_CONDITIONAL_ARM ||
			derived.arena_.nodes[children[2]].kind != DUMP_CONDITIONAL_ARM)
			ThrowLoweringInternal("invalid class conditional result");
		const BlockId then_block = derived.AddBlock(
			derived.NewLabel("condobj_then"));
		const BlockId else_block = derived.AddBlock(
			derived.NewLabel("condobj_else"));
		const BlockId end_block = derived.AddBlock(
			derived.NewLabel("condobj_end"));
		const Operand condition = derived.LowerCondition(children[0]);
		if (derived.full_expression_cleanup_active_)
			PauseFullExpressionCleanupSegment();
		derived.EmitBranch(condition, then_block, else_block);
		derived.SelectBlock(then_block);
		if (derived.full_expression_cleanup_active_)
			EnsureFullExpressionCleanupSegment();
		LowerClassConditionalArm(children[1], destination);
		if (!derived.CurrentBlock().terminated)
		{
			if (derived.full_expression_cleanup_active_)
				PauseFullExpressionCleanupSegment();
			derived.EmitJump(end_block);
		}
		derived.SelectBlock(else_block);
		if (derived.full_expression_cleanup_active_)
			EnsureFullExpressionCleanupSegment();
		LowerClassConditionalArm(children[2], destination);
		if (!derived.CurrentBlock().terminated)
		{
			if (derived.full_expression_cleanup_active_)
				PauseFullExpressionCleanupSegment();
			derived.EmitJump(end_block);
		}
		derived.SelectBlock(end_block);
	}

	void LowerForComponent(std::uint32_t child)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpKind kind = derived.arena_.nodes[child].kind;
		if (kind == DUMP_SIMPLE_DECLARATION || kind == DUMP_VARIABLE)
			derived.PushStatementNode(child);
		else if (kind == DUMP_DESTRUCTOR_ACTION)
			derived.LowerDestructorAction(derived.arena_.nodes[child]);
		else derived.LowerDiscardedValue(child);
	}

	Operand LowerValueWithUnwind(std::uint32_t node,
		const NodeChildren& actions, bool boolean_condition)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BlockId dispatch = derived.AddBlock(
			derived.NewLabel("call_unwind_dispatch"));
		const BlockId end = derived.AddBlock(
			derived.NewLabel("call_unwind_end"));
		derived.EmitEhTarget(Instruction::EH_TRY, dispatch);
		const Operand value = boolean_condition ? derived.LowerCondition(node) :
			derived.LowerValue(node);
		const Operand slot(derived.EnsureGeneratedSlot(
			node, "call", value.type), value.type);
		Instruction store(Instruction::STORE);
		store.type = value.type;
		store.first = value;
		store.second = slot;
		derived.Emit(store);
		const Operand retained = derived.LoadStorage(slot, value.type);
		derived.Emit(Instruction(Instruction::EH_END));
		derived.EmitJump(end);
		derived.SelectBlock(dispatch);
		for (std::size_t i = 0; i < actions.size(); ++i)
			derived.LowerDestructorAction(derived.arena_.nodes[actions[i]]);
		derived.EmitExceptionResume();
		derived.SelectBlock(end);
		return retained;
	}

	Operand LowerDeclaredCondition(const NodeChildren& condition_children,
		bool boolean_condition)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const std::uint32_t declaration = condition_children[0];
		const NodeChildren declaration_children = derived.Children(declaration);
		if (declaration_children.size() != 1 ||
			derived.arena_.nodes[declaration_children[0]].kind != DUMP_VARIABLE)
			ThrowLoweringInternal("invalid PA17 condition declaration");
		const DumpNode& variable =
			derived.arena_.nodes[declaration_children[0]];
		if (derived.stats_) ++derived.stats_->lowered_nodes;
		derived.LowerStatementNode(declaration_children[0]);
		std::uint32_t value_node = kNoDumpEdge;
		NodeChildren unwind_actions;
		for (std::size_t i = 1; i < condition_children.size(); ++i)
		{
			const DumpNode& candidate =
				derived.arena_.nodes[condition_children[i]];
			if (candidate.kind == DUMP_DESTRUCTOR_ACTION && candidate.unwind_only)
				unwind_actions.Push(condition_children[i]);
			else if (value_node == kNoDumpEdge)
				value_node = condition_children[i];
			else ThrowLoweringInternal("invalid PA17 condition suffix");
		}
		if (value_node != kNoDumpEdge)
		{
			if (!unwind_actions.empty())
				return LowerValueWithUnwind(
					value_node, unwind_actions, boolean_condition);
			return boolean_condition ? derived.LowerCondition(value_node) :
				derived.LowerValue(value_node);
		}
		Operand value = derived.LoadStorage(derived.StorageFor(variable.binding,
			derived.LowerStorageType(variable.type)),
			derived.LowerExpressionType(variable.type));
		if (!boolean_condition || !IsFloating(value.type)) return value;
		const Operand truth = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = truth.id;
		compare.op = LOW_OP_NE;
		compare.type = value.type;
		compare.first = value;
		compare.second = derived.FloatingOperand("0.0", value.type);
		derived.Emit(compare);
		return truth;
	}

private:
	bool full_expression_cleanup_start_suppressed_;
};

}
}

#endif
