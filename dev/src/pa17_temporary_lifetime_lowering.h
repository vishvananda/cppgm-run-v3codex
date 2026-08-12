#ifndef CPPGM_PA17_TEMPORARY_LIFETIME_LOWERING_H
#define CPPGM_PA17_TEMPORARY_LIFETIME_LOWERING_H

#include "pa12_semantic_model.h"
#include "pa15_lowir_model.h"
#include "pa15_lowering_support.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa17_lowering_detail
{

using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

class CleanupDispatchCache
{
public:
	CleanupDispatchCache() : slots_(16), size_(0) {}

	bool Find(const DumpArena& arena,
		const std::vector<std::uint32_t>& actions, BlockId* block) const
	{
		const std::uint64_t fingerprint = Fingerprint(arena, actions);
		std::size_t index = static_cast<std::size_t>(fingerprint) &
			(slots_.size() - 1);
		while (slots_[index].occupied)
		{
			const Entry& entry = slots_[index];
			if (entry.fingerprint == fingerprint &&
				Matches(entry, arena, actions))
			{
				*block = entry.block;
				return true;
			}
			index = (index + 1) & (slots_.size() - 1);
		}
		return false;
	}

	void Insert(const DumpArena& arena,
		const std::vector<std::uint32_t>& actions, BlockId block)
	{
		if ((size_ + 1) * 2 >= slots_.size()) Rehash(slots_.size() * 2);
		Entry entry;
		entry.fingerprint = Fingerprint(arena, actions);
		entry.offset = words_.size();
		entry.count = actions.size() * kWordsPerAction;
		entry.block = block;
		entry.occupied = true;
		for (std::size_t i = 0; i < actions.size(); ++i)
			for (std::size_t word = 0; word < kWordsPerAction; ++word)
				words_.push_back(IdentityWord(arena.nodes[actions[i]], word));
		std::size_t index = static_cast<std::size_t>(entry.fingerprint) &
			(slots_.size() - 1);
		while (slots_[index].occupied)
			index = (index + 1) & (slots_.size() - 1);
		slots_[index] = entry;
		++size_;
	}

	void Clear()
	{
		for (std::size_t i = 0; i < slots_.size(); ++i)
			slots_[i].occupied = false;
		words_.clear();
		size_ = 0;
	}

private:
	static const std::size_t kWordsPerAction = 5;
	struct Entry
	{
		std::uint64_t fingerprint;
		std::size_t offset, count;
		BlockId block;
		bool occupied;
		Entry() : fingerprint(0), offset(0), count(0), block(kNoLowId),
			occupied(false) {}
	};

	static std::uint64_t IdentityWord(const DumpNode& action,
		std::size_t word)
	{
		switch (word)
		{
		case 0: return action.lifetime_object == kNoDumpEdge ? 0 : 1;
		case 1: return action.lifetime_object;
		case 2: return action.object_binding;
		case 3: return action.binding;
		default: return action.operand_type;
		}
	}

	static std::uint64_t Fingerprint(const DumpArena& arena,
		const std::vector<std::uint32_t>& actions)
	{
		std::uint64_t hash = 1469598103934665603ULL;
		for (std::size_t i = 0; i < actions.size(); ++i)
			for (std::size_t word = 0; word < kWordsPerAction; ++word)
			{
				hash ^= IdentityWord(arena.nodes[actions[i]], word);
				hash *= 1099511628211ULL;
			}
		return hash;
	}

	bool Matches(const Entry& entry, const DumpArena& arena,
		const std::vector<std::uint32_t>& actions) const
	{
		if (entry.count != actions.size() * kWordsPerAction) return false;
		std::size_t current = entry.offset;
		for (std::size_t i = 0; i < actions.size(); ++i)
			for (std::size_t word = 0; word < kWordsPerAction; ++word)
				if (words_[current++] !=
					IdentityWord(arena.nodes[actions[i]], word))
					return false;
		return true;
	}

	void Rehash(std::size_t capacity)
	{
		std::vector<Entry> replacement(capacity);
		for (std::size_t i = 0; i < slots_.size(); ++i)
		{
			if (!slots_[i].occupied) continue;
			std::size_t index = static_cast<std::size_t>(
				slots_[i].fingerprint) & (capacity - 1);
			while (replacement[index].occupied)
				index = (index + 1) & (capacity - 1);
			replacement[index] = slots_[i];
		}
		slots_.swap(replacement);
	}

	std::vector<Entry> slots_;
	std::vector<std::uint64_t> words_;
	std::size_t size_;
};

template <class Derived>
class TemporaryLifetimeLowering
{
protected:
	static const std::size_t kInlineCleanupActionBudget = 8;

	SlotId EnsureTemporaryLifetimeSlot(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::uint32_t retained = kNoLowId;
		if (derived.temporary_lifetime_slots_.Find(node, &retained))
			return SlotId(retained);
		const SlotId result = static_cast<SlotId>(
			derived.function_->slots.size());
		Slot slot;
		slot.name = derived.GeneratedSlotName("lifetime");
		slot.type = LowU8();
		derived.function_->slots.push_back(slot);
		derived.temporary_lifetime_slots_.Insert(node, result);
		if (derived.stats_) ++derived.stats_->conditional_lifetime_slots;
		return result;
	}

	void ResetFullExpressionFunctionState()
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.full_expression_cleanup_dispatches_.Clear();
		derived.temporary_lifetime_slots_.Clear();
		derived.conditional_cleanup_dispatches_.Clear();
		derived.conditional_cleanup_tails_.Clear();
		derived.runtime_lifetime_temporaries_.Clear();
		derived.full_expression_cleanup_ready_ = false;
		derived.full_expression_deferred_cleanup_ = false;
		derived.conditional_cleanup_resume_ = kNoLowId;
	}

	BlockId InternCleanupAction(std::uint32_t action, BlockId tail)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BlockId original = derived.current_block_;
		const BlockId previous_dispatch =
			derived.full_expression_cleanup_dispatch_;
		std::uint32_t cached_value = kNoLowId;
		if (derived.stats_) ++derived.stats_->cleanup_dispatch_probes;
		if (derived.conditional_cleanup_dispatches_.Find(action, &cached_value))
		{
			if (derived.stats_) ++derived.stats_->cleanup_dispatch_cache_hits;
			std::uint32_t cached_tail = kNoLowId;
			if (!derived.conditional_cleanup_tails_.Find(action, &cached_tail) ||
				cached_tail != tail)
				throw std::logic_error(
					"cleanup action acquired a second ordered suffix");
			return BlockId(cached_value);
		}
		const BlockId block = derived.AddBlock(
			derived.NewLabel("conditional_cleanup_dispatch"));
		if (derived.stats_) ++derived.stats_->cleanup_dispatch_entries;
		derived.conditional_cleanup_dispatches_.Insert(action, block);
		derived.conditional_cleanup_tails_.Insert(action, tail);
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
		if (derived.conditional_cleanup_resume_ == kNoLowId)
		{
			const BlockId original = derived.current_block_;
			derived.conditional_cleanup_resume_ = derived.AddBlock(
				derived.NewLabel("conditional_cleanup_resume"));
			derived.SelectBlock(derived.conditional_cleanup_resume_);
			derived.Emit(Instruction(Instruction::RESUME));
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
		BlockId cached;
		if (derived.stats_) ++derived.stats_->cleanup_dispatch_probes;
		derived.full_expression_cleanup_dispatch_reused_ =
			derived.full_expression_cleanup_dispatches_.Find(derived.arena_,
				derived.full_expression_segment_actions_, &cached);
		if (derived.full_expression_cleanup_dispatch_reused_)
		{
			if (derived.stats_) ++derived.stats_->cleanup_dispatch_cache_hits;
			derived.full_expression_cleanup_dispatch_ = cached;
		}
		else
		{
			if (derived.stats_) ++derived.stats_->cleanup_dispatch_entries;
			derived.full_expression_cleanup_dispatch_ = derived.AddBlock(
				derived.NewLabel("call_unwind_dispatch"));
			derived.full_expression_cleanup_dispatches_.Insert(derived.arena_,
				derived.full_expression_segment_actions_,
				derived.full_expression_cleanup_dispatch_);
		}
		derived.full_expression_cleanup_end_ = kNoLowId;
		derived.EmitEhTarget(Instruction::EH_TRY,
			derived.full_expression_cleanup_dispatch_);
	}

	void EnsureFullExpressionCleanupSegment()
	{
		Derived& derived = static_cast<Derived&>(*this);
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
		derived.full_expression_tracks_lifetime_state_ = false;
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (IsConditionalTemporaryAction(
				derived.full_expression_cleanup_actions_[i]))
				derived.full_expression_tracks_lifetime_state_ = true;
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
		if (!UsesRuntimeLifetimeState(action))
		{
			derived.LowerDestructorAction(derived.arena_.nodes[action]);
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
		const DumpNode& cleanup = derived.arena_.nodes[action];
		const Operand destination = derived.AddressOfStorage(
			derived.TemporaryObjectStorageSlot(temporary));
		derived.LowerDestructorObject(
			cleanup.operand_type, destination, cleanup.binding);
		derived.EmitJump(done);
		derived.SelectBlock(done);
	}

	void PauseFullExpressionCleanupSegment()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_cleanup_active_ ||
			derived.full_expression_cleanup_dispatch_ == kNoLowId)
			return;
		CloseFullExpressionCleanupSegment();
		derived.full_expression_cleanup_dispatch_ = kNoLowId;
		derived.full_expression_cleanup_end_ = kNoLowId;
	}

	void CloseFullExpressionCleanupSegment()
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.Emit(Instruction(Instruction::EH_END));
		if (derived.full_expression_cleanup_dispatch_reused_) return;
		const BlockId dispatch = derived.full_expression_cleanup_dispatch_;
		const BlockId end = derived.AddBlock(
			derived.NewLabel("call_unwind_end"));
		derived.full_expression_cleanup_end_ = end;
		derived.EmitJump(end);
		derived.SelectBlock(dispatch);
		for (std::size_t i = 0;
			i < derived.full_expression_segment_actions_.size(); ++i)
			LowerFullExpressionDestructorAction(
				derived.full_expression_segment_actions_[i]);
		derived.Emit(Instruction(Instruction::RESUME));
		derived.SelectBlock(end);
	}

	void BeginFullExpressionCleanup(const NodeChildren& children,
		std::size_t first_cleanup, bool defer_segment = false,
		BlockId preferred_dispatch = kNoLowId)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.full_expression_cleanup_active_)
			throw std::logic_error("nested full-expression cleanup region");
		derived.full_expression_cleanup_actions_.clear();
		for (std::size_t i = first_cleanup; i < children.size(); ++i)
		{
			if (derived.arena_.nodes[children[i]].kind !=
					DUMP_DESTRUCTOR_ACTION ||
				(derived.arena_.nodes[children[i]].lifetime_object == kNoDumpEdge &&
				 !derived.arena_.nodes[children[i]].unwind_only))
				throw std::logic_error("invalid temporary cleanup suffix");
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
		if (preferred_dispatch != kNoLowId &&
			!derived.full_expression_cleanup_actions_.empty())
			throw std::logic_error(
				"preferred cleanup dispatch has destructor actions");
		if (defer_segment) return;
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
			throw std::logic_error(
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
		if (derived.full_expression_uses_linked_dispatch_)
		{
			if (derived.full_expression_linked_action_cursor_ != 0)
			{
				const std::uint32_t action =
					derived.full_expression_cleanup_actions_[
						derived.full_expression_linked_action_cursor_ - 1];
				tracked = derived.arena_.nodes[action].lifetime_object == temporary;
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
		derived.runtime_lifetime_cleanup_dispatch_ = kNoLowId;
		derived.runtime_lifetime_temporaries_.Clear();
		derived.full_expression_linked_action_cursor_ = 0;
	}

	void CompleteFullExpressionCleanup()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_cleanup_active_)
			throw std::logic_error("missing full-expression cleanup region");
		if (derived.full_expression_uses_linked_dispatch_ &&
			derived.full_expression_linked_action_cursor_ != 0)
			throw std::logic_error("linked cleanup left an unconstructed action");
		EnsureFullExpressionCleanupSegment();
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (!derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only)
				LowerFullExpressionDestructorAction(
					derived.full_expression_cleanup_actions_[i]);
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
			throw std::logic_error("condition cleanup has no value");
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
		const BlockId true_cleanup = derived.AddBlock(
			derived.NewLabel("cond_true_cleanup"));
		const BlockId false_cleanup = derived.AddBlock(
			derived.NewLabel("cond_false_cleanup"));
		derived.EmitBranch(value, true_cleanup, false_cleanup);
		derived.SelectBlock(true_cleanup);
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (!derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only)
				LowerFullExpressionDestructorAction(
					derived.full_expression_cleanup_actions_[i]);
		derived.EmitJump(true_block);
		derived.SelectBlock(false_cleanup);
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (!derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only)
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
		for (std::size_t i = 1; i < children.size(); ++i)
			if (derived.arena_.nodes[children[i]].kind ==
					DUMP_DESTRUCTOR_ACTION &&
				(IsConditionalTemporaryAction(children[i]) ||
				 derived.arena_.nodes[children[i]].unwind_only))
				managed_cleanup = true;
		if (managed_cleanup)
		{
			BeginFullExpressionCleanup(children, 1);
			if (!children.empty()) derived.LowerDiscardedValue(children[0]);
			CompleteFullExpressionCleanup();
			return;
		}
		if (!children.empty()) derived.LowerDiscardedValue(children[0]);
		for (std::size_t i = 1; i < children.size(); ++i)
		{
			if (derived.arena_.nodes[children[i]].kind != DUMP_DESTRUCTOR_ACTION)
				throw std::logic_error(
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
		BeginFullExpressionCleanup(children, first_cleanup);
		for (std::size_t i = 0; i < first_cleanup; ++i)
			derived.LowerStatementNode(children[i]);
		CompleteFullExpressionCleanup();
		return true;
	}

	Operand LowerFullExpressionCondition(const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.empty())
			throw std::logic_error("empty full-expression condition");
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
			if (derived.UsesIndirectClassResult(record.type, record.binding))
				(void)derived.LowerCall(node, record, children, destination);
			else derived.EmitClassObjectCopy(record.type,
				derived.LowerValue(node, derived.LowerExpressionType(record.type)),
				destination);
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
			throw std::logic_error("invalid class conditional arm");
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
			BeginFullExpressionCleanup(children, 1);
		}
		LowerClassDestination(children[0], destination);
		if (children.size() != 1) CompleteFullExpressionCleanup();
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
			throw std::logic_error("invalid class conditional result");
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
		if (derived.full_expression_cleanup_active_)
			PauseFullExpressionCleanupSegment();
		derived.EmitJump(end_block);
		derived.SelectBlock(else_block);
		if (derived.full_expression_cleanup_active_)
			EnsureFullExpressionCleanupSegment();
		LowerClassConditionalArm(children[2], destination);
		if (derived.full_expression_cleanup_active_)
			PauseFullExpressionCleanupSegment();
		derived.EmitJump(end_block);
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
		derived.Emit(Instruction(Instruction::RESUME));
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
			throw std::runtime_error("invalid PA17 condition declaration");
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
			else throw std::runtime_error("invalid PA17 condition suffix");
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
		const Operand truth = derived.Temp(LowU8());
		Instruction compare(Instruction::CMP);
		compare.dest = truth.id;
		compare.op = LOW_OP_NE;
		compare.type = value.type;
		compare.first = value;
		compare.second = derived.FloatingOperand("0.0", value.type);
		derived.Emit(compare);
		return truth;
	}
};

}
}

#endif
