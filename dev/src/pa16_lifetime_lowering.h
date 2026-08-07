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
	void LowerReturn(const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::size_t first_cleanup = 0;
		const bool has_value = !children.empty() &&
			derived.arena_.nodes[children[0]].kind != DUMP_DESTRUCTOR_ACTION;
		Operand result_value;
		if (has_value)
		{
			first_cleanup = 1;
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
					const Operand slot(derived.EnsureGeneratedSlot(
						children[0], "retobj", derived.current_result_),
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
					const Operand slot(derived.EnsureGeneratedSlot(
						children[0], "retobj", derived.current_result_),
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
					const Operand slot(derived.EnsureGeneratedSlot(
						children[0], "retobj", derived.current_result_),
						derived.current_result_);
					derived.LowerClassValueTransfer(children[0],
						derived.AddressOfStorage(slot));
					result_value = slot;
				}
			}
			else if (derived.arena_.nodes[children[0]].kind ==
				DUMP_CONSTRUCTOR_ACTION)
			{
				if (derived.current_indirect_result_)
				{
					const Operand destination(
						static_cast<ParameterId>(0), LowPtr());
					derived.LowerConstructorAction(
						children[0], destination);
				}
				else if (derived.current_result_.kind == LOW_OBJECT)
				{
					const Operand slot(derived.EnsureGeneratedSlot(
						children[0], "retobj", derived.current_result_),
						derived.current_result_);
					derived.LowerConstructorAction(children[0],
						derived.AddressOfStorage(slot));
					result_value = slot;
				}
				else throw std::logic_error(
					"class construction return has a non-object boundary");
			}
			else if (derived.current_result_.kind == LOW_VOID)
				(void)derived.LowerValue(children[0]);
			else if (derived.current_result_reference_)
				result_value = derived.AddressOfStorage(
					derived.LowerStorage(children[0]));
			else
			{
				const Operand value = derived.LowerValue(children[0],
					derived.current_result_.kind == LOW_PTR ?
					derived.current_result_ : LowType());
				const bool preserve_unsigned_conversion =
					value.kind == Operand::INTEGER && IsInteger(value.type) &&
					IsInteger(derived.current_result_) && value.type.is_signed &&
					!derived.current_result_.is_signed &&
					value.type.width < derived.current_result_.width;
				result_value = derived.Convert(value, derived.current_result_,
					!preserve_unsigned_conversion);
			}
		}
		for (std::size_t i = first_cleanup; i < children.size(); ++i)
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
		if (derived.destructor_return_routes_to_epilogue_)
		{
			if (derived.destructor_return_target_ == kNoLowId)
				derived.destructor_return_target_ = derived.AddBlock(
					derived.NewLabel("destructor_return_epilogue"));
			derived.Emit(Instruction(Instruction::EH_END));
			derived.EmitJump(derived.destructor_return_target_);
			return;
		}
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
			derived.Emit(Instruction(Instruction::RESUME));
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
		derived.Emit(Instruction(Instruction::RESUME));
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
				derived.Emit(Instruction(Instruction::RESUME));
			}
		}
		derived.SelectBlock(end);
	}

};

}
}

#endif
