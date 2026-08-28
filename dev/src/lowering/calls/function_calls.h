#pragma once

#include "lowering/ir/model.h"
#include "lowering/objects/member_pointers.h"
#include "lowering/support/sequences.h"
#include "semantic/model/graph.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace lowering
{

template <class Derived>
class FunctionCallLowering
{
protected:
	lowering::ir::Instruction DirectCallInstruction(
		lowering::ir::SymbolId symbol,
		const lowering::ir::LowType& result_type)
	{
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		derived.output_.symbols[symbol].referenced = true;
		Instruction call(Instruction::CALL);
		call.type = result_type;
		call.first = Operand(Operand::FUNCTION, symbol, LowPtr());
		return call;
	}

	lowering::ir::Operand LowerCall(std::uint32_t node,
		const semantic::DumpNode& record,
		const lowering::support::NodeChildren& children,
		const lowering::ir::Operand& supplied_result = lowering::ir::Operand())
	{
		using namespace semantic;
		using namespace lowering::ir;
		using namespace lowering::support;
		Derived& derived = static_cast<Derived&>(*this);
		if (children.empty())
			throw std::runtime_error("semantic call has no callee");
		const DumpNode& callee = derived.arena_.nodes[children[0]];
		Operand builtin_result;
		if (derived.TryLowerCompilerBuiltinCall(
			record, children, &builtin_result))
			return builtin_result;
		if (derived.TryLowerNumericBuiltinCall(
			record, children, &builtin_result))
			return builtin_result;
		if (derived.stats_) ++derived.stats_->binding_index_probes;
		const bool direct = !record.virtual_call &&
			callee.kind == DUMP_CALLEE && callee.binding != kNoBinding &&
			callee.binding < derived.function_symbols_.size() &&
			derived.function_symbols_[callee.binding] != kNoLowId;
		if (derived.full_expression_cleanup_active_ &&
			((!direct ||
			  !derived.program_.bindings[callee.binding].nonthrowing) ||
			 (derived.full_expression_deferred_cleanup_ &&
			  derived.full_expression_cleanup_ready_ &&
			  record.eager_full_expression_cleanup)))
			derived.EnsureFullExpressionCleanupSegment();
		TypeId function_type_id = callee.type;
		bool block_pointer_call = false;
		if (!direct)
		{
			function_type_id = derived.ExpressionObjectType(function_type_id);
			const TypeRecord& callable =
				derived.program_.types.Get(function_type_id);
			block_pointer_call = callable.kind == TYPE_BLOCK_POINTER;
			if (callable.kind == TYPE_POINTER || block_pointer_call)
				function_type_id = callable.child;
		}
		const TypeRecord& function_type =
			derived.program_.types.Get(function_type_id);
		if (function_type.kind != TYPE_FUNCTION)
			throw std::runtime_error("invalid PA15 indirect callee type");
		const TypeId* parameters =
			derived.program_.types.Parameters(function_type_id);
		CallArguments arguments;
		CallArgumentFlags argument_references;
		const bool indirect_result = derived.UsesIndirectClassResult(
			function_type.child, callee.binding);
		const LowType call_type = indirect_result ?
			LowVoid() : derived.LowerType(record.type);
		Instruction call = direct ? DirectCallInstruction(
			derived.function_symbols_[callee.binding], call_type) :
			Instruction(Instruction::CALL);
		if (!direct)
		{
			call.type = call_type;
			call.indirect = true;
		}
		Operand result_storage;
		Operand virtual_object;
		if (indirect_result)
		{
			if (supplied_result.kind != Operand::NONE)
				result_storage = supplied_result;
			else
			{
				const LowType type =
					derived.LowerStorageType(function_type.child);
				const char* purpose = record.reference_call_materialization ?
					"refcall" : "call";
				const Operand slot(derived.EnsureGeneratedSlot(
					node, purpose, type), type);
				result_storage = derived.AddressOfStorage(slot);
			}
			arguments.Push(result_storage);
			argument_references.Push(
				Instruction::CALL_PASS_INDIRECT_RESULT);
		}
		Operand block_object;
		if (block_pointer_call)
		{
			block_object = derived.LowerValue(children[0], LowPtr());
			arguments.Push(block_object);
			argument_references.Push(Instruction::CALL_PASS_VALUE);
		}
		const bool member_pointer_call =
			derived.IsMemberPointerApplication(callee);
		Operand member_pointer_callee;
		std::size_t member_pointer_argument =
			std::numeric_limits<std::size_t>::max();
		if (member_pointer_call)
		{
			const NodeChildren application_children =
				derived.Children(children[0]);
			member_pointer_argument = arguments.size();
			arguments.Push(derived.MemberPointerObject(
				callee, application_children));
			argument_references.Push(Instruction::CALL_PASS_VALUE);
		}
		const std::size_t lowered_argument_begin = arguments.size();
		for (std::size_t i = 1; i < children.size(); ++i)
		{
			const bool reference = i - 1 < function_type.parameter_count &&
				derived.IsReferenceType(parameters[i - 1]);
			argument_references.Push(i - 1 < function_type.parameter_count ?
				derived.BoundaryCallPassing(parameters[i - 1]) :
				Instruction::CALL_PASS_VALUE);
			if (derived.arena_.nodes[children[i]].variadic_class_argument)
				arguments.Push(derived.LowerStorage(children[i]));
			else if (!reference &&
				i - 1 < function_type.parameter_count &&
				derived.IsComplexObjectType(parameters[i - 1]) &&
				derived.UsesIndirectClassParameter(parameters[i - 1]))
				arguments.Push(derived.AddressOfStorage(
					derived.LowerStorage(children[i])));
			else if (!reference &&
				derived.arena_.nodes[children[i]].class_argument_staging)
				arguments.Push(derived.LowerClassArgumentStaging(
					children[i], parameters[i - 1]));
			else if (reference)
				arguments.Push(derived.LowerReferenceCallArgument(
					children[i], parameters[i - 1]));
			else
			{
				LowType expected = i - 1 < function_type.parameter_count ?
					derived.LowerType(parameters[i - 1]) :
					derived.LowerExpressionType(
						derived.arena_.nodes[children[i]].type);
				if (i - 1 >= function_type.parameter_count)
				{
					if (expected.kind == LOW_F32) expected = LowF64();
					else if (IsInteger(expected) && expected.width < 32)
						expected = LowI32();
				}
				arguments.Push(derived.LowerConvertedValue(
					children[i], expected,
					derived.CanonicalizeInitializerImmediate(
						children[i], expected) ||
					derived.CanonicalizeImmediateConversion(children[i]) ||
					derived.CanonicalizeOperatorLiteral(
						children[i], callee)));
			}
			if (record.virtual_call && i == 1)
				virtual_object = arguments[arguments.size() - 1];
		}
		CallArguments lowered_boundary_arguments;
		for (std::size_t i = lowered_argument_begin;
			i < arguments.size(); ++i)
			lowered_boundary_arguments.Push(arguments[i]);
		const std::size_t boundary_argument_begin = arguments.size();
		derived.AppendCallVirtualBaseArguments(callee, function_type_id,
			children, lowered_boundary_arguments, &arguments,
			&argument_references);
		call.virtual_base_argument_count = static_cast<std::uint32_t>(
			arguments.size() - boundary_argument_begin);
		if (member_pointer_call)
		{
			const typename Derived::MemberPointerCallOperands lowered =
				derived.LowerMemberPointerCall(children[0], callee,
					derived.Children(children[0]),
					arguments[member_pointer_argument]);
			arguments[member_pointer_argument] = lowered.object;
			member_pointer_callee = lowered.callee;
		}
		if (derived.full_expression_cleanup_active_ &&
			derived.full_expression_deferred_cleanup_)
			derived.EnsureFullExpressionCleanupSegment();
		if (record.virtual_call)
		{
			if (virtual_object.kind == Operand::NONE ||
				record.virtual_slot == kNoDumpEdge)
				throw std::logic_error(
					"virtual call has no object or slot");
			call.first = derived.LowerVirtualCallee(record, virtual_object,
				ResolveHostVirtualSlot(derived.program_,
					derived.output_.host_object_emission,
					derived.polymorphism_, record,
					derived.BaseEntityForType(
						derived.arena_.nodes[children[1]].type)));
		}
		else if (!direct)
			call.first = member_pointer_call ? member_pointer_callee :
				block_pointer_call ? derived.LoadBlockInvoke(block_object) :
				derived.LowerValue(children[0], LowPtr());
		derived.AttachCallArguments(&call, arguments, argument_references);
		if (call.type.kind == LOW_VOID)
		{
			derived.Emit(call);
			return indirect_result ?
				result_storage : Operand(0, LowVoid());
		}
		const Operand result = derived.Temp(call.type);
		call.dest = result.id;
		derived.Emit(call);
		if (supplied_result.kind != Operand::NONE)
		{
			if (call.type.kind != LOW_OBJECT)
				throw std::logic_error(
					"direct class call destination has a scalar result");
			derived.EmitClassObjectCopy(
				record.type, result, supplied_result);
			if (derived.stats_)
				++derived.stats_->direct_class_call_destination_placements;
			return supplied_result;
		}
		return derived.RetainFullExpressionCallResult(node, record, result);
	}
};

}  // namespace lowering
}  // namespace cppgm
