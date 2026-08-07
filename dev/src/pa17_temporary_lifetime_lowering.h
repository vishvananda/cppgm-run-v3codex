#ifndef CPPGM_PA17_TEMPORARY_LIFETIME_LOWERING_H
#define CPPGM_PA17_TEMPORARY_LIFETIME_LOWERING_H

#include "pa12_semantic_model.h"
#include "pa15_lowir_model.h"
#include "pa15_lowering_support.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cppgm
{
namespace pa17_lowering_detail
{

using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

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
			if (temporary != kNoDumpEdge &&
				derived.temporary_initialized_[temporary])
				derived.full_expression_segment_actions_.push_back(action);
		}
		derived.full_expression_cleanup_dispatch_ = derived.AddBlock(
			derived.NewLabel("call_unwind_dispatch"));
		derived.full_expression_cleanup_end_ = derived.AddBlock(
			derived.NewLabel("call_unwind_end"));
		derived.EmitEhTarget(Instruction::EH_TRY,
			derived.full_expression_cleanup_dispatch_);
	}

	void CloseFullExpressionCleanupSegment()
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BlockId dispatch = derived.full_expression_cleanup_dispatch_;
		const BlockId end = derived.full_expression_cleanup_end_;
		derived.Emit(Instruction(Instruction::EH_END));
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
		std::size_t first_cleanup)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.full_expression_cleanup_active_)
			throw std::logic_error("nested full-expression cleanup region");
		derived.full_expression_cleanup_actions_.clear();
		for (std::size_t i = first_cleanup; i < children.size(); ++i)
		{
			if (derived.arena_.nodes[children[i]].kind !=
					DUMP_DESTRUCTOR_ACTION ||
				derived.arena_.nodes[children[i]].lifetime_object == kNoDumpEdge)
				throw std::logic_error("invalid temporary cleanup suffix");
			derived.full_expression_cleanup_actions_.push_back(children[i]);
		}
		derived.full_expression_cleanup_active_ = true;
		StartFullExpressionCleanupSegment();
	}

	void TransitionFullExpressionCleanup()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_cleanup_active_)
			throw std::logic_error(
				"temporary transition outside full expression");
		CloseFullExpressionCleanupSegment();
		StartFullExpressionCleanupSegment();
	}

	void CompleteFullExpressionCleanup()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.full_expression_cleanup_active_)
			throw std::logic_error("missing full-expression cleanup region");
		for (std::size_t i = 0;
			i < derived.full_expression_cleanup_actions_.size(); ++i)
			derived.LowerDestructorAction(derived.arena_.nodes[
				derived.full_expression_cleanup_actions_[i]]);
		CloseFullExpressionCleanupSegment();
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
