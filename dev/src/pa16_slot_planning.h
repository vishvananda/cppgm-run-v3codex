#ifndef CPPGM_PA16_SLOT_PLANNING_H
#define CPPGM_PA16_SLOT_PLANNING_H

#include "pa12_semantic_model.h"
#include "pa15_lowering_support.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa16_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowering_detail;

template <class Derived>
class SlotPlanning
{
protected:
	LowType LowerVariableStorage(const DumpNode& record) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (record.storage_size == 0)
			return derived.LowerStorageType(record.type);
		if (record.kind != DUMP_VARIABLE || record.storage_alignment == 0 ||
			record.storage_size > std::numeric_limits<std::size_t>::max())
			throw std::logic_error("invalid explicit object storage fact");
		const TypeRecord& source = derived.program_.types.Get(
			derived.program_.types.RemoveTopCv(record.type));
		if (source.kind != TYPE_ARRAY || source.bound != 0)
			throw std::logic_error(
				"explicit object storage requires an unbounded array");
		return LowObject(static_cast<std::size_t>(record.storage_size),
			record.storage_alignment);
	}

	bool SetExplicitVariableZero(const DumpNode& record, Global* global) const
	{
		if (record.storage_size == 0) return false;
		global->initializer_kind = Global::STRUCTURED_VALUE;
		Global::DataItem zero;
		zero.kind = Global::DataItem::ZERO_ITEM;
		zero.zero_bytes = static_cast<std::size_t>(record.storage_size);
		global->items.push_back(zero);
		return true;
	}

	void PlanConstructorReferenceArgumentSlots(const DumpNode& action,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (action.kind != DUMP_CONSTRUCTOR_ACTION) return;
		const TypeRecord& function_type = derived.program_.types.Get(action.type);
		if (function_type.kind != TYPE_FUNCTION ||
			function_type.parameter_count == 0)
			throw std::logic_error(
				"constructor slot plan has invalid function type");
		const TypeId* parameters =
			derived.program_.types.Parameters(action.type);
		for (std::size_t argument = 0; argument < children.size(); ++argument)
		{
			const std::size_t parameter = argument + 1;
			if (parameter >= function_type.parameter_count ||
				!derived.IsReferenceType(parameters[parameter])) continue;
			const DumpNode& source = derived.arena_.nodes[children[argument]];
			if (source.kind == DUMP_TEMPORARY_OBJECT ||
				source.category == VALUE_LVALUE || source.category == VALUE_XVALUE ||
				derived.IsClassObjectType(source.type)) continue;
			(void)derived.EnsureGeneratedSlot(children[argument], "refarg",
				derived.LowerExpressionType(parameters[parameter]));
		}
	}

	void CollectSlots(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::vector<std::uint32_t> pending(1, node);
		std::vector<std::uint8_t> under_variable(1, 0);
		std::vector<std::uint8_t> plan_expression_arguments(1, 0);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const bool variable_initializer = under_variable.back() != 0;
			under_variable.pop_back();
			const bool expression_arguments =
				plan_expression_arguments.back() != 0;
			plan_expression_arguments.pop_back();
			const DumpNode& record = derived.arena_.nodes[current];
			const bool persistent_variable = record.kind == DUMP_VARIABLE &&
				record.binding != kNoBinding &&
				derived.program_.bindings[record.binding].storage_class ==
					STORAGE_CLASS_STATIC;
			if ((record.kind == DUMP_PARAMETER || record.kind == DUMP_VARIABLE) &&
				record.binding != kNoBinding && !persistent_variable)
			{
				if (record.kind == DUMP_VARIABLE && record.direct_return_slot &&
					derived.current_indirect_result_)
				{
					derived.binding_indirect_parameters_[record.binding] =
						pa15_lowir_detail::ParameterId(0);
				}
				else if (derived.binding_slots_[record.binding] == kNoLowId)
				{
					std::string requested = record.text == 0 ? std::string() :
						derived.program_.names.Get(record.text);
					if (record.kind == DUMP_PARAMETER && requested.empty())
						requested = derived.parameter_slot_index_ <
							derived.function_->parameters.size() ?
							derived.function_->parameters[
								derived.parameter_slot_index_].name : "__param";
					const std::string name = derived.UniqueSlotName(requested);
					derived.binding_slots_[record.binding] =
						static_cast<SlotId>(derived.function_->slots.size());
					Slot slot;
					slot.name = name;
					slot.type = record.kind == DUMP_VARIABLE ?
						derived.LowerVariableStorage(record) :
						derived.LowerStorageType(record.type);
					derived.function_->slots.push_back(slot);
				}
				if (record.kind == DUMP_PARAMETER)
					++derived.parameter_slot_index_;
			}
			const TypeId temporary_type = record.kind == DUMP_TEMPORARY_OBJECT ?
				derived.program_.types.RemoveTopCv(record.type) : kNoType;
			const EntityId temporary_entity = temporary_type != kNoType &&
				derived.program_.types.Get(temporary_type).kind == TYPE_NAMED ?
				derived.program_.types.Get(temporary_type).entity : kNoEntity;
			const bool union_argument = temporary_entity != kNoEntity &&
				derived.program_.entities[temporary_entity].flavor == NAMED_UNION;
			if (record.kind == DUMP_HANDLER && record.binding != kNoBinding)
				(void)derived.EnsureGeneratedSlot(current, "catch", LowPtr());
			if (record.kind == DUMP_THROW_EXPRESSION &&
				record.full_expression_staging)
				(void)derived.EnsureGeneratedSlot(
					current, "throw_alloc", LowPtr());
			if (record.kind == DUMP_BINARY_EXPRESSION &&
				record.logical_operation != LOGICAL_OPERATION_NONE &&
				record.full_expression_staging)
			{
				const NodeChildren logical_children = derived.Children(current);
				if (logical_children.empty() ||
					!derived.FoldNamedLogicalConstant(logical_children[0]))
					(void)derived.EnsureGeneratedSlot(current,
						record.logical_operation == LOGICAL_OPERATION_AND ?
							"land" : "lor",
						pa15_lowir_detail::LowI64());
			}
			if (record.kind == DUMP_CALL_EXPRESSION &&
				record.full_expression_staging &&
				!derived.UsesIndirectClassResult(record.type, record.binding))
			{
				const LowType result = derived.LowerType(record.type);
				if (result.kind != LOW_VOID)
					(void)derived.EnsureGeneratedSlot(current, "call", result);
			}
			if (record.kind == DUMP_CALL_EXPRESSION &&
				record.reference_call_materialization &&
				record.category == VALUE_PRVALUE &&
				!derived.UsesIndirectClassResult(record.type, record.binding))
				(void)derived.EnsureGeneratedSlot(current, "tmpref",
					derived.LowerStorageType(record.type));
			if (record.kind == DUMP_CONDITIONAL_EXPRESSION &&
				record.full_expression_staging &&
				!derived.IsClassObjectType(record.type))
			{
				const LowType result = derived.LowerExpressionType(record.type);
				if (result.kind != LOW_VOID)
					(void)derived.EnsureGeneratedSlot(current,
						(record.category == VALUE_LVALUE ||
						 record.category == VALUE_XVALUE) ? "condaddr" : "cond",
						(record.category == VALUE_LVALUE ||
						 record.category == VALUE_XVALUE) ? LowPtr() : result);
			}
			const NodeChildren children = derived.Children(current);
			if (variable_initializer)
				PlanConstructorReferenceArgumentSlots(record, children);
			if (record.kind == DUMP_RETURN_STATEMENT && !children.empty() &&
				!derived.current_indirect_result_ &&
				derived.current_result_.kind == LOW_OBJECT)
			{
				const DumpKind result_kind =
					derived.arena_.nodes[children[0]].kind;
				if (result_kind == DUMP_BRACED_INIT_LIST ||
					result_kind == DUMP_INITIALIZER_LIST ||
					result_kind == DUMP_CONDITIONAL_EXPRESSION ||
					result_kind == DUMP_CLASS_VALUE_TRANSFER ||
					result_kind == DUMP_AGGREGATE_CONSTRUCTION_ACTION ||
					result_kind == DUMP_CONSTRUCTOR_ACTION)
					(void)derived.EnsureDirectReturnSlot(children[0]);
			}
			const bool constructed_variable_temporary = variable_initializer &&
				children.size() == 1 && derived.arena_.nodes[children[0]].kind ==
					DUMP_CONSTRUCTOR_ACTION;
			const bool preplan_standalone_discard =
				record.discarded_materialization &&
				(temporary_entity == kNoEntity ||
				 !derived.program_.entities[temporary_entity].empty_class) &&
				children.size() == 1 &&
				derived.arena_.nodes[children[0]].kind == DUMP_CALL_EXPRESSION &&
				!derived.arena_.nodes[children[0]].contains_temporary_object;
			if (record.kind == DUMP_TEMPORARY_OBJECT &&
				record.initializer_list_backing && variable_initializer &&
				derived.generated_slots_[current] == kNoLowId)
				(void)derived.EnsureGeneratedSlot(current, "initlist",
					derived.LowerStorageType(record.type));
			if (record.kind == DUMP_TEMPORARY_OBJECT &&
				!record.elided_temporary_storage &&
				(record.argument_materialization ||
				 preplan_standalone_discard ||
					 (expression_arguments &&
					  record.managed_full_expression_cleanup &&
					  derived.IsClassObjectType(record.type)) ||
					 (record.temporary_implicit_object &&
					  (variable_initializer ||
					   record.managed_full_expression_cleanup)) ||
				 constructed_variable_temporary) &&
				(variable_initializer || expression_arguments || union_argument ||
				 record.discarded_materialization ||
				 (record.full_expression_staging &&
				  !record.managed_full_expression_cleanup)) &&
				derived.generated_slots_[current] == kNoLowId)
			{
				const TypeRecord& temporary = derived.program_.types.Get(
					derived.program_.types.RemoveTopCv(record.type));
				const char* name = record.discarded_materialization ?
					(temporary.kind == TYPE_ARRAY ? "discardarr" : "discard") :
					record.argument_materialization ? "arg" :
					record.temporary_implicit_object ? "tmpobj" :
					constructed_variable_temporary || expression_arguments ?
						"tmpobj" : "arg";
				(void)derived.EnsureGeneratedSlot(current, name,
					derived.LowerStorageType(record.type));
			}
			const bool indirect_result_transfer =
				record.kind == DUMP_CLASS_VALUE_TRANSFER &&
				children.size() == 1 &&
				derived.arena_.nodes[children[0]].kind == DUMP_CALL_EXPRESSION &&
				derived.UsesIndirectClassResult(
					derived.arena_.nodes[children[0]].type,
					derived.arena_.nodes[children[0]].binding);
			const bool empty_call_transfer =
				derived.ElidesEmptyConversionCallTransfer(record, children);
			if (record.class_argument_staging && variable_initializer &&
				!derived.ElidesNestedTemporaryConstruction(record, children) &&
				!(record.kind == DUMP_CALL_EXPRESSION &&
				  derived.UsesIndirectClassResult(record.type, record.binding)) &&
				!indirect_result_transfer &&
				!empty_call_transfer &&
				derived.generated_slots_[current] == kNoLowId)
			{
				const TypeId staging_type =
					record.kind == DUMP_CONSTRUCTOR_ACTION ?
						record.operand_type : record.type;
				(void)derived.EnsureGeneratedSlot(current,
					derived.UsesIndirectClassParameter(staging_type) ?
						"arg" : "argobj",
					derived.LowerStorageType(staging_type));
			}
			if (record.kind == DUMP_NEW_EXPRESSION && record.array_action &&
				!children.empty())
			{
				const NodeChildren call = derived.Children(children[0]);
				const bool retain_size = !record.array_count_constant &&
					(record.array_cookie || record.value_initialization ||
					 children.size() == 2);
				if (retain_size && call.size() > 1)
					(void)derived.EnsureGeneratedSlot(call[1],
						"array_new_size", pa15_lowir_detail::LowI64());
				if (record.value_initialization)
					(void)derived.EnsureGeneratedSlot(children[0],
						"zeroinit_offset", pa15_lowir_detail::LowI64());
				if (children.size() == 2)
				{
					(void)derived.EnsureGeneratedSlot(children[1],
						"array_new_index", pa15_lowir_detail::LowI64());
					if (record.selected_binding != kNoBinding)
						(void)derived.EnsureGeneratedSlot(current,
							"array_dtor_index", pa15_lowir_detail::LowI64());
				}
			}
			for (std::size_t i = children.size(); i != 0; --i)
			{
				const bool range_condition_arguments =
					record.kind == DUMP_CONDITION &&
					record.full_expression_staging;
				pending.push_back(children[i - 1]);
				under_variable.push_back(variable_initializer ||
					record.kind == DUMP_VARIABLE ||
					range_condition_arguments ? 1 : 0);
				const DumpNode& child =
					derived.arena_.nodes[children[i - 1]];
				const bool expression_call =
					record.kind == DUMP_EXPRESSION_STATEMENT &&
					child.kind == DUMP_CALL_EXPRESSION;
				if (expression_call && derived.stats_)
					++derived.stats_->slot_implicit_object_fact_reads;
				const bool plan_child_arguments = expression_arguments ||
					range_condition_arguments ||
					(expression_call && !child.temporary_implicit_object);
				plan_expression_arguments.push_back(
					plan_child_arguments ? 1 : 0);
			}
		}
	}
};

}
}

#endif
