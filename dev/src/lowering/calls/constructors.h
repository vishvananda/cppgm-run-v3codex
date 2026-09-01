#ifndef CPPGM_LOWERING_CALLS_CONSTRUCTORS_H
#define CPPGM_LOWERING_CALLS_CONSTRUCTORS_H

#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/ir/model.h"
#include "semantic/model/graph.h"

#include <cstdint>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

typedef SmallSequence<BindingId, 8> ConstructorMemberPath;
const std::size_t kConstructorProjectionReplayLimit = 8;

template <class Derived>
class ConstructorActionLowering
{
protected:
	bool BuildConstructorCleanup(const DumpNode& action,
		DumpNode* cleanup) const
	{
		if ((action.kind != DUMP_INITIALIZER_ACTION &&
			action.kind != DUMP_BASE_INITIALIZER_ACTION &&
			action.kind != DUMP_DELEGATING_INITIALIZER_ACTION) ||
			action.selected_binding == kNoBinding)
			return false;
		*cleanup = DumpNode(DUMP_DESTRUCTOR_ACTION);
		cleanup->binding = action.selected_binding;
		cleanup->operand_type = action.type;
		if (action.kind == DUMP_INITIALIZER_ACTION)
			cleanup->object_binding = action.binding;
		else if (action.kind == DUMP_DELEGATING_INITIALIZER_ACTION)
			cleanup->complete_object_destruction = true;
		else
		{
			cleanup->base_projection_count = 1;
			cleanup->base_projection_offset = action.direct_base_offset;
			cleanup->has_base_projection_offset = true;
		}
		return true;
	}

	void InstallConstructorCleanup(
		const DumpNode& action, BlockId* active)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (*active != kNoLowId)
			derived.Emit(Instruction(Instruction::EH_END));
		const BlockId source = derived.current_block_;
		const BlockId cleanup = derived.AddBlock(
			derived.NewLabel("constructor_cleanup"));
		derived.SelectBlock(cleanup);
		derived.LowerDestructorAction(action);
		// Each newly constructed subobject owns one cleanup block.  Reuse the
		// preceding block as the remaining destructor suffix instead of copying
		// every earlier action into this block.
		if (*active == kNoLowId)
		{
			derived.Emit(Instruction(Instruction::EH_END));
			derived.EmitExceptionResume();
		}
		else derived.EmitJump(*active);
		derived.SelectBlock(source);
		derived.EmitEhTarget(Instruction::EH_CLEANUP, cleanup);
		*active = cleanup;
	}

	void LowerConstructorBody(std::uint32_t body)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const NodeChildren children = derived.Children(body);
		BlockId active = kNoLowId;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			derived.LowerStatement(children[i]);
			if (derived.CurrentBlock().terminated) continue;
			DumpNode cleanup(DUMP_DESTRUCTOR_ACTION);
			if (!BuildConstructorCleanup(
				derived.arena_.nodes[children[i]], &cleanup)) continue;
			InstallConstructorCleanup(cleanup, &active);
			// Body statements from here on run inside the member cleanup
			// region; an early return must pop it before leaving.
			derived.constructor_body_cleanup_active_ = true;
		}
		if (active != kNoLowId && !derived.CurrentBlock().terminated)
			derived.Emit(Instruction(Instruction::EH_END));
		derived.constructor_body_cleanup_active_ = false;
	}

	bool ElidesNestedTemporaryConstruction(
		const DumpNode& action, const NodeChildren& children) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (!action.trivial_special_member_action || children.size() != 1)
			return false;
		const DumpNode& source = derived.arena_.nodes[children[0]];
		const NodeChildren source_children = derived.Children(children[0]);
		return source.kind == DUMP_TEMPORARY_OBJECT &&
			source.elided_temporary_storage &&
			source_children.size() == 1 &&
			derived.arena_.nodes[source_children[0]].kind ==
				DUMP_CONSTRUCTOR_ACTION &&
			derived.arena_.nodes[source_children[0]].operand_type ==
				action.operand_type;
	}

	void LowerConstructorAction(std::uint32_t node,
		const Operand& destination, bool force_empty = false,
		bool elide_direct_empty_source = false,
		bool elide_empty_call_source = false,
		EntityId complete_entity = kNoEntity)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& action = derived.arena_.nodes[node];
		if (action.elide_empty_constructor && !force_empty) return;
		if (action.kind != DUMP_CONSTRUCTOR_ACTION ||
			action.binding == kNoBinding)
			ThrowLoweringInternal("invalid constructor action");
		const BindingRecord& selected =
			derived.program_.bindings[action.binding];
		if (derived.output_.host_object_emission && selected.constructor &&
			!selected.constructor_base_entry &&
			selected.member_owner != kNoEntity &&
			derived.program_.types.Get(selected.type).parameter_count == 0 &&
			derived.program_.entities[selected.member_owner].
				trivial_default_constructor)
			return;
		const NodeChildren children = derived.Children(node);
		bool retained_empty_special_member = false;
		if (action.trivial_special_member_action &&
			derived.IsClassObjectType(action.operand_type))
		{
			const TypeRecord& object = derived.program_.types.Get(
				derived.ExpressionObjectType(action.operand_type));
			retained_empty_special_member =
				derived.program_.entities[object.entity].empty_class &&
				!derived.program_.entities[object.entity].trivial_destructor;
		}
		if (action.trivial_special_member_action &&
			!retained_empty_special_member)
		{
			if (children.size() != 1 ||
				!derived.IsClassObjectType(action.operand_type))
				ThrowLoweringInternal(
					"invalid trivial special-member construction");
			const DumpNode& source_record = derived.arena_.nodes[children[0]];
			const NodeChildren source_children = derived.Children(children[0]);
			if (ElidesNestedTemporaryConstruction(action, children))
			{
				derived.LowerConstructorAction(source_children[0], destination);
				return;
			}
			const TypeRecord& object = derived.program_.types.Get(
				derived.ExpressionObjectType(action.operand_type));
			if (derived.program_.entities[object.entity].empty_class)
			{
				const bool direct_id = source_record.kind == DUMP_ID_EXPRESSION;
				const bool projected_id = source_record.kind == DUMP_CAST_EXPRESSION &&
					source_record.base_projection_count != 0 && source_children.size() == 1 &&
					derived.arena_.nodes[source_children[0]].kind == DUMP_ID_EXPRESSION;
				const bool elided_reference_call = elide_empty_call_source &&
					source_record.kind == DUMP_CALL_EXPRESSION &&
					(source_record.category == VALUE_LVALUE ||
					 source_record.category == VALUE_XVALUE);
				if (!elided_reference_call &&
					((!direct_id || !elide_direct_empty_source) && !projected_id))
					(void)derived.LowerClassTransferSource(children[0]);
				return;
			}
			const Operand source =
				derived.LowerClassTransferSource(children[0]);
			derived.EmitClassObjectCopy(action.operand_type,
				source, destination);
			return;
		}
		if (action.binding >= derived.function_symbols_.size() ||
			derived.function_symbols_[action.binding] == kNoLowId)
			ThrowLoweringInternal("constructor action has no emitted binding: " +
				derived.program_.names.Get(action.text));
		const TypeRecord& function_type =
			derived.program_.types.Get(action.type);
		if (function_type.kind != TYPE_FUNCTION ||
			function_type.parameter_count == 0)
			ThrowLoweringInternal("constructor action has invalid function type");
		const TypeId* parameters = derived.program_.types.Parameters(action.type);
		CallArguments arguments;
		CallArgumentFlags references;
		arguments.Push(destination);
		references.Push(0);
		const BindingRecord& constructor_binding =
			derived.program_.bindings[action.binding];
		if (derived.IncludesConstructionVtt(action.binding))
		{
			arguments.Push(derived.ConstructionVttArgument(
				complete_entity, constructor_binding.member_owner));
			references.Push(Instruction::CALL_PASS_VALUE);
		}
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const std::size_t parameter = i + 1;
			const bool reference = parameter < function_type.parameter_count &&
				derived.IsReferenceType(parameters[parameter]);
			references.Push(reference ? 1 : 0);
			if (reference)
			{
				const DumpNode& argument = derived.arena_.nodes[children[i]];
				if (argument.kind == DUMP_TEMPORARY_OBJECT)
					arguments.Push(derived.LowerStorage(children[i]));
				else if (argument.category == VALUE_LVALUE ||
					argument.category == VALUE_XVALUE ||
					derived.IsClassObjectType(argument.type))
					arguments.Push(derived.AddressOfStorage(
						derived.LowerStorage(children[i])));
				else
				{
					const LowType type =
						derived.LowerExpressionType(parameters[parameter]);
					const Operand slot(
						derived.EnsureGeneratedSlot(children[i], "refarg", type), type);
					Instruction store(Instruction::STORE);
					store.type = type;
					store.first = derived.Convert(
						derived.LowerValue(children[i]), type);
					store.second = slot;
					derived.Emit(store);
					arguments.Push(derived.AddressOfStorage(slot));
				}
			}
			else
			{
				if (parameter < function_type.parameter_count &&
					derived.arena_.nodes[children[i]].class_argument_staging)
				{
					arguments.Push(derived.LowerClassArgumentStaging(
						children[i], parameters[parameter]));
					continue;
				}
				const LowType expected = parameter < function_type.parameter_count ?
					derived.LowerType(parameters[parameter]) :
					derived.LowerExpressionType(
						derived.arena_.nodes[children[i]].type);
				const DumpNode& child = derived.arena_.nodes[children[i]];
				if (derived.source_types_.IsNullptr(child.type) &&
					!child.default_argument)
					arguments.Push(Operand::NullPointer(expected));
				else arguments.Push(derived.LowerConvertedValue(
					children[i], expected,
					derived.CanonicalizeInitializerImmediate(
						children[i], expected)));
			}
		}
		std::size_t hidden_remaining =
			derived.VirtualBaseParameterCount(action.binding, action.type);
		if (constructor_binding.constructor_base_entry &&
			constructor_binding.member_owner != kNoEntity)
		{
			const EntityRecord& base_owner = derived.program_.entities[
				constructor_binding.member_owner];
			for (std::size_t i = 0; i < base_owner.virtual_base_count &&
				hidden_remaining != 0; ++i)
			{
				if (!derived.CarriesVirtualBase(action.binding, 0, i)) continue;
				const VirtualBaseLayout& needed = derived.program_.VirtualBase(
					constructor_binding.member_owner, i);
				Operand address;
				if (complete_entity != kNoEntity)
				{
					std::uint64_t offset = 0;
					if (!derived.program_.FindVirtualBase(
						complete_entity, needed.entity, &offset))
						ThrowLoweringInternal(
							"complete constructor has no virtual base address");
					const Operand complete_object = derived.LoadStorage(
						derived.StorageFor(
							derived.current_this_binding_, LowPtr()), LowPtr());
					address = derived.ProjectBaseSubobjectOffset(
						complete_object, offset);
				}
				else
				{
					if (!derived.CurrentVirtualBaseAddress(
						derived.current_this_binding_, needed.entity, &address))
						ThrowLoweringInternal(
							"base constructor has no forwarded virtual base address");
				}
				arguments.Push(address);
				references.Push(0);
				if (derived.stats_)
					++derived.stats_->virtual_base_call_arguments;
				--hidden_remaining;
			}
		}
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const std::size_t parameter = i + 1;
			if (parameter >= function_type.parameter_count) break;
			const EntityId owner =
				derived.VirtualBoundaryEntity(parameters[parameter]);
			if (owner == kNoEntity) continue;
			for (std::size_t base = 0;
				base < derived.program_.entities[owner].virtual_base_count &&
				hidden_remaining != 0; ++base)
			{
				if (!derived.CarriesVirtualBase(
					action.binding, parameter, base)) continue;
				const std::size_t lowered_parameter = parameter +
					(derived.IncludesConstructionVtt(action.binding) ? 1 : 0);
				arguments.Push(derived.VirtualBaseCallAddress(
					children[i], arguments[lowered_parameter], owner, base));
				references.Push(Instruction::CALL_PASS_VALUE);
				if (derived.stats_)
					++derived.stats_->virtual_base_call_arguments;
				--hidden_remaining;
			}
		}
		Instruction call = derived.DirectCallInstruction(
			derived.function_symbols_[action.binding], LowVoid());
		call.copy_elision_candidate = action.copy_elision_candidate;
		derived.AttachCallArguments(&call, arguments, references);
		if (derived.full_expression_cleanup_active_ &&
			!derived.program_.bindings[action.binding].nonthrowing)
			derived.EnsureFullExpressionCleanupSegment();
		derived.Emit(call);
	}

	void LowerBaseInitializationAction(const DumpNode& action,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (action.kind != DUMP_BASE_INITIALIZER_ACTION ||
			derived.current_this_binding_ == kNoBinding || children.empty() ||
			derived.arena_.nodes[children[0]].kind != DUMP_CONSTRUCTOR_ACTION)
			ThrowLoweringInternal(
				"base initialization is outside a constructor");
		NodeChildren constructor;
		constructor.Push(children[0]);
		const bool trivial =
			derived.IsTrivialConstructorAction(action.type, constructor);
		const bool value_initialization =
			derived.arena_.nodes[children[0]].value_initialization;
		if (derived.arena_.nodes[children[0]].elide_empty_constructor)
			return;
		const EntityId base = derived.ClassEntity(action.type);
		const bool empty_base = base != kNoEntity &&
			derived.program_.entities[base].empty_class;
		if (trivial && (!value_initialization || empty_base)) return;
		if (children.size() > 1)
			derived.BeginFullExpressionCleanup(children, 1);
		const Operand object = derived.LoadStorage(
			derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
		const Operand destination = action.has_direct_base_offset ?
			derived.ProjectBaseSubobjectOffset(
				object, action.direct_base_offset) :
			derived.ProjectBaseSubobjects(object,
				action.base_projection_count);
		if (value_initialization && !empty_base)
			derived.EmitZeroInitialization(action.type, destination);
		if (!trivial)
			derived.LowerConstructorAction(
				children[0], destination, false, true, false,
				derived.HasCurrentImplicitVirtualBases() ?
					kNoEntity : derived.current_member_owner_);
		if (children.size() > 1)
			derived.CompleteFullExpressionCleanup();
	}

	void LowerDelegatingInitializationAction(const DumpNode& action,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (action.kind != DUMP_DELEGATING_INITIALIZER_ACTION ||
			derived.current_this_binding_ == kNoBinding || children.empty() ||
			derived.arena_.nodes[children[0]].kind != DUMP_CONSTRUCTOR_ACTION)
			ThrowLoweringInternal(
				"delegating initialization is outside a constructor");
		if (children.size() > 1)
			derived.BeginFullExpressionCleanup(children, 1);
		const Operand destination = derived.LoadStorage(
			derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
		derived.LowerConstructorAction(children[0], destination);
		if (children.size() > 1)
			derived.CompleteFullExpressionCleanup();
	}

	bool TryLowerConstructorInitializationAction(const DumpNode& action,
		const NodeChildren& children)
	{
		if (action.kind == DUMP_BASE_INITIALIZER_ACTION)
			LowerBaseInitializationAction(action, children);
		else if (action.kind == DUMP_DELEGATING_INITIALIZER_ACTION)
			LowerDelegatingInitializationAction(action, children);
		else return false;
		return true;
	}

	void LowerConstructorAggregateLeaf(const DumpNode& action,
		const NodeChildren& values, const ConstructorMemberPath& path,
		const Operand& retained_destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (values.size() > 1)
			ThrowLoweringInternal(
				"constructor aggregate leaf has multiple values");
		if (derived.IsArrayType(action.type))
		{
			if (values.size() != 1 ||
				derived.arena_.nodes[values[0]].kind != DUMP_BRACED_INIT_LIST)
				ThrowLoweringSource(
					"constructor array member requires braces");
			if (retained_destination.kind == Operand::NONE)
				derived.LowerConstructorArrayActions(
					action.type, values[0], path);
			else derived.LowerArrayValues(
				action.type, values[0], retained_destination);
			return;
		}
		const Operand destination = retained_destination.kind == Operand::NONE ?
			derived.ProjectConstructorMemberPath(path) : retained_destination;
		if (values.size() == 1 &&
			derived.arena_.nodes[values[0]].kind == DUMP_CONSTRUCTOR_ACTION)
		{
			LowerConstructorAction(values[0], destination);
			return;
		}
		Instruction store(Instruction::STORE);
		if (derived.IsReferenceType(action.type))
		{
			if (values.empty())
				ThrowLoweringInternal(
					"constructor aggregate reference has no value");
			store.type = LowPtr();
			store.first = derived.AddressOfStorage(
				derived.LowerStorage(values[0]));
		}
		else
		{
			store.type = derived.LowerExpressionType(action.type);
			if (!values.empty())
				store.first = derived.Convert(
					derived.LowerValue(values[0]), store.type);
			else if (store.type.kind == LOW_PTR)
				store.first = Operand::NullPointer(store.type);
			else if (IsFloating(store.type))
				store.first = derived.FloatingOperand("0.0", store.type);
			else store.first = Operand(0, store.type);
		}
		store.second = destination;
		if (derived.program_.bindings[action.binding].bit_field)
			derived.InitializeBitField(
				action.binding, store.first, destination, store.type);
		else derived.Emit(store);
	}

	void LowerConstructorAggregateActions(std::uint32_t list_node,
		ConstructorMemberPath* path, const Operand& retained_address)
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.ResetInitializedBitFieldUnit();
		const NodeChildren actions = derived.Children(list_node);
		for (std::size_t i = 0; i < actions.size(); ++i)
		{
			const DumpNode& action = derived.arena_.nodes[actions[i]];
			if (action.kind != DUMP_INITIALIZER_ACTION ||
				action.binding == kNoBinding)
				ThrowLoweringInternal("invalid constructor aggregate action");
			const NodeChildren values = derived.Children(actions[i]);
			const bool nested = values.size() == 1 &&
				derived.arena_.nodes[values[0]].kind == DUMP_BRACED_INIT_LIST &&
				derived.IsClassObjectType(action.type);
			if (retained_address.kind != Operand::NONE)
			{
				const Operand destination = derived.ProjectAggregateMember(
					retained_address, action.binding);
				if (nested)
					LowerConstructorAggregateActions(values[0], path, destination);
				else LowerConstructorAggregateLeaf(
					action, values, *path, destination);
				continue;
			}
			path->Push(action.binding);
			if (nested &&
				path->size() == kConstructorProjectionReplayLimit)
			{
				const Operand destination =
					derived.ProjectConstructorMemberPath(*path);
				LowerConstructorAggregateActions(
					values[0], path, destination);
			}
			else if (nested)
				LowerConstructorAggregateActions(
					values[0], path, Operand());
			else LowerConstructorAggregateLeaf(
				action, values, *path, Operand());
			path->Pop();
		}
	}

	void LowerMemberInitializationAction(const DumpNode& action,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (action.binding == kNoBinding ||
			derived.current_this_binding_ == kNoBinding)
			ThrowLoweringInternal(
				"member initialization is outside a constructor");
		if (children.empty()) return;
		if (children.size() != 1)
			ThrowLoweringInternal("member initialization has multiple values");
		const std::uint32_t value_node = children[0];
		const DumpNode& value = derived.arena_.nodes[value_node];
		if (value.kind == DUMP_CONSTRUCTOR_ARRAY_ACTION)
		{
			derived.LowerBoundConstructorArray(value_node, action.binding);
			return;
		}
		if (value.kind == DUMP_CONSTRUCTOR_ACTION)
		{
			if (!value.value_initialization &&
				derived.IsTrivialConstructorAction(action.type, children)) return;
			const Operand object = derived.LoadStorage(
				derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
			const Operand destination =
				derived.ProjectAggregateMember(object, action.binding);
			if (value.value_initialization)
				derived.EmitZeroInitialization(action.type, destination);
			if (derived.IsTrivialConstructorAction(action.type, children)) return;
			if (value.trivial_special_member_action)
			{
				const TypeRecord& object = derived.program_.types.Get(
					derived.ExpressionObjectType(action.type));
				if (derived.program_.entities[object.entity].empty_class &&
					derived.program_.entities[object.entity].trivial_destructor)
					return;
			}
			LowerConstructorAction(value_node, destination);
			return;
		}
		if (value.kind == DUMP_CLASS_VALUE_TRANSFER)
		{
			const Operand object = derived.LoadStorage(
				derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
			const Operand destination =
				derived.ProjectAggregateMember(object, action.binding);
			derived.LowerClassValueTransfer(value_node, destination);
			return;
		}
		if (value.kind == DUMP_BRACED_INIT_LIST &&
			!derived.IsReferenceType(action.type) &&
			derived.IsClassObjectType(action.type))
		{
			ConstructorMemberPath path;
			path.Push(action.binding);
			LowerConstructorAggregateActions(value_node, &path, Operand());
			return;
		}
		if (value.kind == DUMP_BRACED_INIT_LIST &&
			!derived.IsReferenceType(action.type) &&
			derived.IsArrayType(action.type))
		{
			ConstructorMemberPath path;
			path.Push(action.binding);
			derived.LowerConstructorArrayActions(action.type, value_node, path);
			return;
		}
		Instruction store(Instruction::STORE);
		if (derived.IsReferenceType(action.type))
		{
			store.type = LowPtr();
			if (value.kind == DUMP_BRACED_INIT_LIST)
			{
				const NodeChildren values = derived.Children(value_node);
				if (values.size() != 1)
					ThrowLoweringInternal(
						"reference member requires one initializer");
				store.first = derived.AddressOfStorage(
					derived.LowerStorage(values[0]));
			}
			else store.first = derived.AddressOfStorage(
				derived.LowerStorage(value_node));
		}
		else
		{
			store.type = derived.LowerExpressionType(action.type);
			if (value.kind == DUMP_BRACED_INIT_LIST)
			{
				const NodeChildren values = derived.Children(value_node);
				if (values.size() > 1)
					ThrowLoweringInternal(
						"scalar brace initialization has many values");
				if (values.empty())
					store.first = store.type.kind == LOW_PTR ?
						Operand::NullPointer(store.type) :
						IsFloating(store.type) ?
						derived.FloatingOperand("0.0", store.type) :
						Operand(0, store.type);
				else
				{
					const Operand initial = derived.LowerValue(values[0]);
					const bool fold_immediate =
						initial.kind == Operand::INTEGER &&
						IsInteger(initial.type) && IsInteger(store.type) &&
						initial.type.is_signed == store.type.is_signed;
					store.first = derived.Convert(
						initial, store.type, fold_immediate);
				}
			}
			else
			{
				const Operand initial = derived.LowerValue(value_node);
				const bool fold_immediate = initial.kind == Operand::INTEGER &&
					IsInteger(initial.type) && IsInteger(store.type) &&
					initial.type.is_signed == store.type.is_signed;
				store.first = derived.Convert(
					initial, store.type, fold_immediate);
			}
		}
		if (derived.program_.bindings[action.binding].bit_field)
		{
			derived.LowerConstructorBitField(action.binding, store.first,
				derived.LowerExpressionType(action.type));
		}
		else
		{
			const Operand object = derived.LoadStorage(
				derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
			store.second = derived.ProjectAggregateMember(object, action.binding);
			derived.Emit(store);
		}
	}

};

}
}

#endif
