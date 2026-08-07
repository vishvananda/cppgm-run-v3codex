#pragma once

#include "pa12_semantic_model.h"
#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"

#include <cstdint>
#include <stdexcept>

namespace cppgm
{
namespace pa17_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowering_support;
using namespace pa15_lowir_detail;

template <typename Derived>
class SpecialMemberLowering
{
protected:
	void LowerAssignmentCall(const DumpNode& step,
		const Operand& destination, const Operand& source)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (step.selected_binding == kNoBinding ||
			step.selected_binding >= derived.function_symbols_.size() ||
			derived.function_symbols_[step.selected_binding] == kNoLowId)
			throw std::logic_error(
				"selected assignment helper has no lowering identity");
		const TypeRecord& function = derived.program_.types.Get(
			derived.program_.bindings[step.selected_binding].type);
		Instruction call(Instruction::CALL);
		call.type = derived.LowerBoundaryResult(function.child);
		call.first = Operand(Operand::FUNCTION,
			derived.function_symbols_[step.selected_binding], LowPtr());
		CallArguments arguments;
		CallArgumentFlags references;
		arguments.Push(destination);
		references.Push(0);
		arguments.Push(source);
		references.Push(1);
		derived.output_.symbols[
			derived.function_symbols_[step.selected_binding]].referenced = true;
		derived.AttachCallArguments(&call, arguments, references);
		if (call.type.kind == LOW_VOID)
			derived.Emit(call);
		else
		{
			const Operand ignored = derived.Temp(call.type);
			call.dest = ignored.id;
			derived.Emit(call);
		}
	}

	Operand LoadAssignmentObject(BindingId binding)
	{
		Derived& derived = static_cast<Derived&>(*this);
		return derived.LoadStorage(
			derived.StorageFor(binding, LowPtr()), LowPtr());
	}

	void LowerAssignmentStep(const DumpNode& assignment,
		const DumpNode& step)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (step.kind != DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION)
			throw std::logic_error("invalid synthesized assignment step");
		if (step.binding == kNoBinding &&
			step.selected_binding == kNoBinding)
		{
			const Operand destination = LoadAssignmentObject(
				derived.current_this_binding_);
			const Operand source =
				LoadAssignmentObject(assignment.object_binding);
			derived.EmitClassObjectCopy(step.type, source, destination);
			return;
		}

		if (step.selected_binding != kNoBinding)
		{
			Operand destination = LoadAssignmentObject(
				derived.current_this_binding_);
			if (step.binding != kNoBinding)
				destination = derived.ProjectAggregateMember(
					destination, step.binding);
			else
				destination = derived.ProjectBaseSubobjects(destination, 1);
			Operand source = LoadAssignmentObject(assignment.object_binding);
			if (step.binding != kNoBinding)
				source = derived.ProjectAggregateMember(source, step.binding);
			else
				source = derived.ProjectBaseSubobjects(source, 1);
			LowerAssignmentCall(step, destination, source);
			return;
		}

		Operand source = LoadAssignmentObject(assignment.object_binding);
		if (step.binding != kNoBinding)
			source = derived.ProjectAggregateMember(source, step.binding);
		if (step.binding != kNoBinding &&
			derived.program_.bindings[step.binding].bit_field)
		{
			const Operand value = derived.LoadBitField(step.binding, source);
			Operand destination = LoadAssignmentObject(
				derived.current_this_binding_);
			destination = derived.ProjectAggregateMember(
				destination, step.binding);
			(void)derived.StoreBitField(
				step.binding, destination, value, true);
			return;
		}
		if (derived.IsClassObjectType(step.type) ||
			derived.IsArrayType(step.type))
		{
			Operand destination = LoadAssignmentObject(
				derived.current_this_binding_);
			if (step.binding != kNoBinding)
				destination = derived.ProjectAggregateMember(
					destination, step.binding);
			derived.EmitClassObjectCopy(step.type, source, destination);
			return;
		}
		const LowType type = derived.LowerExpressionType(step.type);
		const Operand value = derived.LoadStorage(source, type);
		Operand destination = LoadAssignmentObject(
			derived.current_this_binding_);
		if (step.binding != kNoBinding)
			destination = derived.ProjectAggregateMember(
				destination, step.binding);
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = value;
		store.second = destination;
		derived.Emit(store);
	}

	Operand LowerSpecialMemberAssignment(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& assignment = derived.arena_.nodes[node];
		if (assignment.kind != DUMP_SPECIAL_MEMBER_ASSIGNMENT_ACTION ||
			assignment.object_binding == kNoBinding ||
			derived.current_this_binding_ == kNoBinding)
			throw std::logic_error("invalid synthesized assignment action");
		const NodeChildren steps = derived.Children(node);
		for (std::size_t i = 0; i < steps.size(); ++i)
		{
			if (derived.stats_) ++derived.stats_->lowered_nodes;
			LowerAssignmentStep(assignment, derived.arena_.nodes[steps[i]]);
		}
		return derived.LoadStorage(
			derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
	}
};

}
}
