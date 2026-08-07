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
};

}
}

#endif
