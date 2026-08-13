#ifndef CPPGM_PA16_LIFETIME_LOWERING_H
#define CPPGM_PA16_LIFETIME_LOWERING_H

#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"
#include "pa12_semantic_model.h"

#include <cstdint>
#include <stdexcept>

namespace cppgm
{
namespace pa16_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

const std::size_t kDestructorCleanupInlineLimit = 8;

template <class Derived>
class LifetimeActionLowering
{
protected:
	LifetimeActionLowering() : direct_return_slot_(kNoLowId) {}

	void ResetLifetimeFunctionState()
	{
		direct_return_slot_ = kNoLowId;
	}

	SlotId EnsureDirectReturnSlot(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (direct_return_slot_ == kNoLowId)
			direct_return_slot_ = derived.EnsureGeneratedSlot(
				node, "retobj", derived.current_result_);
		else if (derived.generated_slots_[node] == kNoLowId)
			derived.generated_slots_[node] = direct_return_slot_;
		return direct_return_slot_;
	}

	Operand ZeroDirectReturnObject(std::uint32_t node, const LowType& type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand slot(EnsureDirectReturnSlot(node), type);
		Instruction zero(Instruction::ZERO_OBJECT);
		zero.type = type;
		zero.first = slot;
		derived.Emit(zero);
		return slot;
	}

	void LowerConstructorReturn(std::uint32_t node, Operand* result_value)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& action = derived.arena_.nodes[node];
		if (derived.current_indirect_result_)
		{
			const Operand destination(static_cast<ParameterId>(0), LowPtr());
			if (action.value_initialization)
				derived.EmitZeroInitialization(action.operand_type, destination);
			derived.LowerConstructorAction(node, destination);
			return;
		}
		if (derived.current_result_.kind != LOW_OBJECT)
			throw std::logic_error(
				"class construction return has a non-object boundary");
		const Operand slot(EnsureDirectReturnSlot(node), derived.current_result_);
		const Operand destination = derived.AddressOfStorage(slot);
		if (action.value_initialization)
			derived.EmitZeroInitialization(action.operand_type, destination);
		derived.LowerConstructorAction(node, destination);
		*result_value = slot;
	}

	void LowerReturn(const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const bool has_value = !children.empty() &&
			derived.arena_.nodes[children[0]].kind != DUMP_DESTRUCTOR_ACTION;
		const std::size_t first_cleanup = has_value ? 1 : 0;
		std::size_t full_expression_cleanup_end = first_cleanup;
		NodeChildren full_expression_actions;
		bool conditional_full_expression = false;
		bool explicitly_managed_full_expression = false;
		bool lexical_unwind = false;
		while (full_expression_cleanup_end < children.size())
		{
			const DumpNode& action =
				derived.arena_.nodes[children[full_expression_cleanup_end]];
			if (action.kind != DUMP_DESTRUCTOR_ACTION ||
				!action.full_expression_staging)
				break;
			full_expression_actions.Push(children[full_expression_cleanup_end]);
			if (action.lifetime_object != kNoDumpEdge &&
				derived.arena_.nodes[action.lifetime_object].conditionally_constructed)
				conditional_full_expression = true;
			if (action.managed_full_expression_cleanup)
				explicitly_managed_full_expression = true;
			lexical_unwind = lexical_unwind || action.unwind_only;
			++full_expression_cleanup_end;
		}
		const bool managed_full_expression = conditional_full_expression ||
			explicitly_managed_full_expression || lexical_unwind;
		if (managed_full_expression)
			derived.BeginFullExpressionCleanup(full_expression_actions, 0,
				has_value && derived.arena_.nodes[children[0]].kind ==
					DUMP_CONDITIONAL_EXPRESSION);
		Operand result_value;
		if (has_value)
		{
			if (derived.arena_.nodes[children[0]].direct_return_slot)
			{
				if (!derived.current_indirect_result_)
					throw std::logic_error(
						"direct return slot has a direct result boundary");
			}
			else if (derived.arena_.nodes[children[0]].kind ==
				DUMP_BRACED_INIT_LIST &&
				derived.IsClassObjectType(derived.arena_.nodes[children[0]].type))
			{
				if (derived.current_indirect_result_)
				{
					const Operand destination(
						static_cast<ParameterId>(0), LowPtr());
					derived.LowerRuntimeObjectValue(
						derived.arena_.nodes[children[0]].type,
						children[0], destination);
				}
				else if (derived.current_result_.kind == LOW_OBJECT)
				{
					const Operand slot(EnsureDirectReturnSlot(children[0]),
						derived.current_result_);
					derived.LowerRuntimeObjectValue(
						derived.arena_.nodes[children[0]].type,
						children[0], derived.AddressOfStorage(slot));
					result_value = slot;
				}
				else throw std::logic_error(
					"class aggregate return has a non-object boundary");
			}
			else if (derived.arena_.nodes[children[0]].kind ==
				DUMP_CONDITIONAL_EXPRESSION &&
				!derived.current_result_reference_ &&
				derived.IsClassObjectType(derived.arena_.nodes[children[0]].type))
			{
				if (derived.current_indirect_result_)
				{
					const Operand destination(
						static_cast<ParameterId>(0), LowPtr());
					derived.LowerClassConditionalResult(
						children[0], destination);
				}
				else if (derived.current_result_.kind == LOW_OBJECT)
				{
					const Operand slot(EnsureDirectReturnSlot(children[0]),
						derived.current_result_);
					derived.LowerClassConditionalResult(children[0],
						derived.AddressOfStorage(slot));
					result_value = slot;
				}
				else throw std::logic_error(
					"class conditional return has a non-object boundary");
			}
			else if (derived.arena_.nodes[children[0]].kind ==
				DUMP_CLASS_VALUE_TRANSFER)
			{
				if (derived.current_indirect_result_)
				{
					const Operand destination(
						static_cast<ParameterId>(0), LowPtr());
					derived.LowerClassValueTransfer(children[0], destination);
				}
				else if (derived.current_result_.kind != LOW_OBJECT)
					throw std::logic_error(
						"class-value return has a non-object boundary");
				else
				{
					const Operand slot(EnsureDirectReturnSlot(children[0]),
						derived.current_result_);
					derived.LowerClassValueTransfer(children[0],
						derived.AddressOfStorage(slot));
					result_value = slot;
				}
			}
			else if (derived.arena_.nodes[children[0]].kind ==
				DUMP_AGGREGATE_CONSTRUCTION_ACTION)
			{
				const bool protect =
					derived.AggregateConstructionOwnsNontrivialParameters(children[0]);
				const BlockId dispatch = protect ? derived.AddBlock(
					derived.NewLabel("call_unwind_dispatch")) : BlockId(kNoLowId);
				const BlockId end = protect ? derived.AddBlock(
					derived.NewLabel("call_unwind_end")) : BlockId(kNoLowId);
				if (protect)
					derived.EmitEhTarget(Instruction::EH_TRY, dispatch);
				if (derived.current_indirect_result_)
				{
					const Operand destination(
						static_cast<ParameterId>(0), LowPtr());
					derived.LowerAggregateConstructionAction(
						children[0], destination);
				}
				else if (derived.current_result_.kind == LOW_OBJECT)
				{
					const Operand slot(EnsureDirectReturnSlot(children[0]),
						derived.current_result_);
					derived.LowerAggregateConstructionAction(children[0],
						derived.AddressOfStorage(slot));
					result_value = slot;
				}
				else throw std::logic_error(
					"aggregate construction return has a non-object boundary");
				if (protect)
				{
					derived.Emit(Instruction(Instruction::EH_END));
					derived.EmitJump(end);
					derived.SelectBlock(dispatch);
					derived.EmitExceptionResume();
					derived.SelectBlock(end);
				}
			}
			else if (derived.arena_.nodes[children[0]].kind ==
				DUMP_CONSTRUCTOR_ACTION)
				LowerConstructorReturn(children[0], &result_value);
			else if (derived.current_result_.kind == LOW_VOID)
				(void)derived.LowerValue(children[0]);
			else if (derived.current_result_reference_)
			{
				const DumpNode& returned = derived.arena_.nodes[children[0]];
				if (returned.category != VALUE_PRVALUE)
					result_value = derived.AddressOfStorage(
						derived.LowerStorage(children[0]));
				else
				{
					const LowType type =
						derived.LowerExpressionType(returned.type);
					const Operand slot(derived.EnsureGeneratedSlot(
						children[0], "retref", type), type);
					Instruction store(Instruction::STORE);
					store.type = type;
					store.first = derived.LowerValue(children[0], type);
					store.second = slot;
					derived.Emit(store);
					result_value = derived.AddressOfStorage(slot);
				}
			}
			else
			{
				const bool boolean_conversion =
					derived.arena_.nodes[children[0]].boolean_conversion;
				const Operand value = boolean_conversion ?
					derived.LowerConvertedValue(children[0],
						derived.current_result_, false) :
					derived.LowerValue(children[0],
						derived.current_result_.kind == LOW_PTR ?
						derived.current_result_ : LowType());
				const bool preserve_unsigned_conversion =
					value.kind == Operand::INTEGER && IsInteger(value.type) &&
					IsInteger(derived.current_result_) && value.type.is_signed &&
					!derived.current_result_.is_signed &&
					value.type.width < derived.current_result_.width;
				result_value = boolean_conversion ? value : derived.Convert(
					value, derived.current_result_,
					!preserve_unsigned_conversion);
			}
		}
		if (derived.CurrentBlock().terminated) return;
		if (managed_full_expression)
			derived.CompleteFullExpressionCleanup();
		const std::size_t exception_regions =
			derived.ActiveExceptionRegionCount();
		const std::size_t closed_exception_handlers =
			derived.BeginExceptionControlExit(exception_regions);
		const std::size_t remaining_cleanup = managed_full_expression ?
			full_expression_cleanup_end : first_cleanup;
		for (std::size_t i = remaining_cleanup; i < children.size(); ++i)
		{
			if (derived.arena_.nodes[children[i]].kind != DUMP_DESTRUCTOR_ACTION)
				throw std::logic_error("invalid return cleanup action");
			if (has_value &&
				derived.arena_.nodes[children[0]].direct_return_slot &&
				derived.arena_.nodes[children[i]].object_binding ==
					derived.arena_.nodes[children[0]].binding)
				continue;
			derived.LowerDestructorAction(derived.arena_.nodes[children[i]]);
		}
		derived.FinishExceptionControlExit(
			closed_exception_handlers, exception_regions);
		if (derived.destructor_return_routes_to_epilogue_)
		{
			if (derived.destructor_return_target_ == kNoLowId)
				derived.destructor_return_target_ = derived.AddBlock(
					derived.NewLabel("destructor_return_epilogue"));
			derived.Emit(Instruction(Instruction::EH_END));
			derived.EmitJump(derived.destructor_return_target_);
			return;
		}
		derived.FinishFunctionExceptionBoundaryNormalExit();
		if (derived.current_result_.kind == LOW_VOID)
			derived.Emit(Instruction(Instruction::RETURN_VOID));
		else
		{
			if (!has_value)
				throw std::runtime_error("non-void return has no value");
			Instruction instruction(Instruction::RETURN_VALUE);
			instruction.type = derived.current_result_;
			instruction.first = result_value;
			derived.Emit(instruction);
		}
	}

	void EmitDestructorActionRange(const NodeChildren& children,
		std::size_t first)
	{
		Derived& derived = static_cast<Derived&>(*this);
		for (std::size_t i = first; i < children.size(); ++i)
		{
			if (derived.arena_.nodes[children[i]].kind != DUMP_DESTRUCTOR_ACTION)
				throw std::logic_error("invalid destructor suffix action");
			if (i + 1 == children.size())
			{
				derived.LowerDestructorAction(derived.arena_.nodes[children[i]]);
				continue;
			}
			const BlockId cleanup = derived.AddBlock(
				derived.NewLabel("destructor_suffix_cleanup"));
			const BlockId next = derived.AddBlock(
				derived.NewLabel("destructor_suffix_next"));
			derived.EmitEhTarget(Instruction::EH_CLEANUP, cleanup);
			derived.LowerDestructorAction(derived.arena_.nodes[children[i]]);
			derived.Emit(Instruction(Instruction::EH_END));
			derived.EmitJump(next);
			derived.SelectBlock(cleanup);
			for (std::size_t j = i + 1; j < children.size(); ++j)
				derived.LowerDestructorAction(derived.arena_.nodes[children[j]]);
			derived.Emit(Instruction(Instruction::EH_END));
			derived.EmitExceptionResume();
			derived.SelectBlock(next);
		}
	}

	void LowerDestructorBody(std::uint32_t body)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const NodeChildren children = derived.Children(body);
		std::size_t first_action = children.size();
		for (std::size_t i = 0; i < children.size(); ++i)
			if (derived.arena_.nodes[children[i]].kind == DUMP_DESTRUCTOR_ACTION)
			{
				first_action = i;
				break;
			}
		if (first_action == children.size())
		{
			derived.LowerStatement(body);
			return;
		}
		if (children.size() - first_action > kDestructorCleanupInlineLimit)
		{
			LowerCompactDestructorBody(body, children, first_action);
			return;
		}
		const BlockId cleanup = derived.AddBlock(
			derived.NewLabel("destructor_cleanup"));
		const BlockId end = derived.AddBlock(
			derived.NewLabel("destructor_end"));
		derived.EmitEhTarget(Instruction::EH_CLEANUP, cleanup);
		derived.destructor_return_target_ = kNoLowId;
		derived.destructor_return_routes_to_epilogue_ = true;
		for (std::size_t i = 0; i < first_action; ++i)
			derived.LowerStatement(children[i]);
		derived.destructor_return_routes_to_epilogue_ = false;
		if (derived.destructor_return_target_ != kNoLowId)
		{
			if (!derived.CurrentBlock().terminated)
			{
				derived.Emit(Instruction(Instruction::EH_END));
				derived.EmitJump(derived.destructor_return_target_);
			}
			derived.SelectBlock(derived.destructor_return_target_);
		}
		else
		{
			if (derived.CurrentBlock().terminated) return;
			derived.Emit(Instruction(Instruction::EH_END));
		}
		derived.destructor_return_target_ = kNoLowId;
		EmitDestructorActionRange(children, first_action);
		derived.EmitJump(end);
		derived.SelectBlock(cleanup);
		for (std::size_t i = first_action; i < children.size(); ++i)
			derived.LowerDestructorAction(derived.arena_.nodes[children[i]]);
		derived.Emit(Instruction(Instruction::EH_END));
		derived.EmitExceptionResume();
		derived.SelectBlock(end);
	}

	void LowerCompactDestructorBody(std::uint32_t body,
		const NodeChildren& children, std::size_t first_action)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const std::size_t action_count = children.size() - first_action;
		const Operand progress(derived.EnsureGeneratedSlot(
			body, "destructor_progress", LowI64()), LowI64());
		const BlockId cleanup = derived.AddBlock(
			derived.NewLabel("destructor_cleanup"));
		const BlockId end = derived.AddBlock(
			derived.NewLabel("destructor_end"));
		Instruction initial_progress(Instruction::STORE);
		initial_progress.type = LowI64();
		initial_progress.first = Operand(0, LowI64());
		initial_progress.second = progress;
		derived.Emit(initial_progress);
		derived.EmitEhTarget(Instruction::EH_CLEANUP, cleanup);
		derived.destructor_return_target_ = kNoLowId;
		derived.destructor_return_routes_to_epilogue_ = true;
		for (std::size_t i = 0; i < first_action; ++i)
			derived.LowerStatement(children[i]);
		derived.destructor_return_routes_to_epilogue_ = false;
		if (derived.destructor_return_target_ != kNoLowId)
		{
			if (!derived.CurrentBlock().terminated)
			{
				derived.Emit(Instruction(Instruction::EH_END));
				derived.EmitJump(derived.destructor_return_target_);
			}
			derived.SelectBlock(derived.destructor_return_target_);
		}
		else
		{
			if (derived.CurrentBlock().terminated) return;
			derived.Emit(Instruction(Instruction::EH_END));
		}
		derived.destructor_return_target_ = kNoLowId;
		for (std::size_t i = 0; i < action_count; ++i)
		{
			if (i + 1 < action_count)
			{
				Instruction next_progress(Instruction::STORE);
				next_progress.type = LowI64();
				next_progress.first = Operand(
					static_cast<std::int64_t>(i + 1), LowI64());
				next_progress.second = progress;
				derived.Emit(next_progress);
				derived.EmitEhTarget(Instruction::EH_CLEANUP, cleanup);
			}
			derived.LowerDestructorAction(
				derived.arena_.nodes[children[first_action + i]]);
			if (i + 1 < action_count)
				derived.Emit(Instruction(Instruction::EH_END));
		}
		derived.EmitJump(end);

		SmallSequence<BlockId, 8> cleanup_blocks;
		for (std::size_t i = 0; i < action_count; ++i)
			cleanup_blocks.Push(derived.AddBlock(
				derived.NewLabel("destructor_suffix_cleanup")));
		derived.SelectBlock(cleanup);
		const Operand selected = derived.LoadStorage(progress, LowI64());
		Instruction dispatch(Instruction::SWITCH);
		dispatch.first = selected;
		dispatch.target = cleanup_blocks[0];
		SmallSequence<std::int64_t, 8> values;
		for (std::size_t i = 0; i < action_count; ++i)
			values.Push(static_cast<std::int64_t>(i));
		derived.AttachSwitchCases(&dispatch, values, cleanup_blocks);
		derived.Emit(dispatch);
		derived.RecordBlockIncoming(dispatch.target);
		for (std::size_t i = 0; i < cleanup_blocks.size(); ++i)
			derived.RecordBlockIncoming(cleanup_blocks[i]);
		for (std::size_t i = 0; i < action_count; ++i)
		{
			derived.SelectBlock(cleanup_blocks[i]);
			derived.LowerDestructorAction(
				derived.arena_.nodes[children[first_action + i]]);
			if (i + 1 < action_count)
				derived.EmitJump(cleanup_blocks[i + 1]);
			else
			{
				derived.Emit(Instruction(Instruction::EH_END));
				derived.EmitExceptionResume();
			}
		}
		derived.SelectBlock(end);
	}

	SlotId direct_return_slot_;
};

}
}

#endif
