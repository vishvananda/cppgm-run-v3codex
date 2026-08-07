#ifndef CPPGM_PA17_TEMPORARY_LIFETIME_LOWERING_H
#define CPPGM_PA17_TEMPORARY_LIFETIME_LOWERING_H

#include "pa12_semantic_model.h"
#include "pa15_lowir_model.h"
#include "pa15_lowering_support.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace cppgm
{
namespace pa17_lowering_detail
{

using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

struct CleanupDispatchKeyHash
{
	std::size_t operator()(const std::vector<std::uint64_t>& key) const
	{
		std::size_t hash = static_cast<std::size_t>(1469598103934665603ULL);
		for (std::size_t i = 0; i < key.size(); ++i)
		{
			hash ^= static_cast<std::size_t>(key[i]);
			hash *= static_cast<std::size_t>(1099511628211ULL);
		}
		return hash;
	}
};

typedef std::unordered_map<std::vector<std::uint64_t>, BlockId,
	CleanupDispatchKeyHash> CleanupDispatchMap;

template <class Derived>
class TemporaryLifetimeLowering
{
protected:
	void StartFullExpressionCleanupSegment()
	{
		Derived& derived = static_cast<Derived&>(*this);
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
				derived.temporary_initialized_[temporary])
				)
				derived.full_expression_segment_actions_.push_back(action);
		}
		std::vector<std::uint64_t> key;
		key.reserve(derived.full_expression_segment_actions_.size() * 5);
		for (std::size_t i = 0;
			i < derived.full_expression_segment_actions_.size(); ++i)
		{
			const DumpNode& action = derived.arena_.nodes[
				derived.full_expression_segment_actions_[i]];
			key.push_back(action.lifetime_object == kNoDumpEdge ? 0 : 1);
			key.push_back(action.lifetime_object);
			key.push_back(action.object_binding);
			key.push_back(action.binding);
			key.push_back(action.operand_type);
		}
		CleanupDispatchMap::const_iterator cached =
			derived.full_expression_cleanup_dispatches_.find(key);
		derived.full_expression_cleanup_dispatch_reused_ =
			cached != derived.full_expression_cleanup_dispatches_.end();
		if (derived.full_expression_cleanup_dispatch_reused_)
			derived.full_expression_cleanup_dispatch_ = cached->second;
		else
		{
			derived.full_expression_cleanup_dispatch_ = derived.AddBlock(
				derived.NewLabel("call_unwind_dispatch"));
			derived.full_expression_cleanup_dispatches_.insert(
				std::make_pair(key,
					derived.full_expression_cleanup_dispatch_));
		}
		derived.full_expression_cleanup_end_ = kNoLowId;
		derived.EmitEhTarget(Instruction::EH_TRY,
			derived.full_expression_cleanup_dispatch_);
	}

	void EnsureFullExpressionCleanupSegment()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.full_expression_cleanup_active_ &&
			derived.full_expression_cleanup_dispatch_ == kNoLowId)
			StartFullExpressionCleanupSegment();
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
		const BlockId dispatch = derived.full_expression_cleanup_dispatch_;
		derived.Emit(Instruction(Instruction::EH_END));
		if (derived.full_expression_cleanup_dispatch_reused_) return;
		const BlockId end = derived.AddBlock(
			derived.NewLabel("call_unwind_end"));
		derived.full_expression_cleanup_end_ = end;
		derived.EmitJump(end);
		derived.SelectBlock(dispatch);
		for (std::size_t i = 0;
			i < derived.full_expression_segment_actions_.size(); ++i)
			derived.LowerDestructorAction(derived.arena_.nodes[
				derived.full_expression_segment_actions_[i]]);
		derived.Emit(Instruction(Instruction::RESUME));
		derived.SelectBlock(end);
	}

	void BeginFullExpressionCleanup(const NodeChildren& children,
		std::size_t first_cleanup, bool defer_segment = false)
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
		if (!defer_segment) StartFullExpressionCleanupSegment();
	}

	void TransitionFullExpressionCleanup()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_cleanup_active_)
			throw std::logic_error(
				"temporary transition outside full expression");
		if (derived.full_expression_cleanup_dispatch_ != kNoLowId)
			CloseFullExpressionCleanupSegment();
		StartFullExpressionCleanupSegment();
	}

	void CompleteFullExpressionCleanup()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_cleanup_active_)
			throw std::logic_error("missing full-expression cleanup region");
		EnsureFullExpressionCleanupSegment();
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (!derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only)
				derived.LowerDestructorAction(derived.arena_.nodes[
					derived.full_expression_cleanup_actions_[i]]);
		CloseFullExpressionCleanupSegment();
		derived.full_expression_cleanup_active_ = false;
		derived.full_expression_cleanup_actions_.clear();
		derived.full_expression_segment_actions_.clear();
		derived.full_expression_cleanup_dispatch_ = kNoLowId;
		derived.full_expression_cleanup_end_ = kNoLowId;
		derived.full_expression_cleanup_dispatch_reused_ = false;
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
		BlockId true_block, BlockId false_block)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() < 2)
			throw std::logic_error("condition cleanup has no actions");
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
				derived.LowerDestructorAction(derived.arena_.nodes[
					derived.full_expression_cleanup_actions_[i]]);
		derived.EmitJump(true_block);
		derived.SelectBlock(false_cleanup);
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			if (!derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]].unwind_only)
				derived.LowerDestructorAction(derived.arena_.nodes[
					derived.full_expression_cleanup_actions_[i]]);
		derived.EmitJump(false_block);
		derived.full_expression_cleanup_active_ = false;
		derived.full_expression_cleanup_actions_.clear();
		derived.full_expression_segment_actions_.clear();
		derived.full_expression_cleanup_dispatch_ = kNoLowId;
		derived.full_expression_cleanup_end_ = kNoLowId;
	}

	void LowerFullExpressionStatement(const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
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
			if (derived.UsesIndirectClassResult(record.type))
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
