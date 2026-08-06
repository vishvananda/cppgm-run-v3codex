#ifndef CPPGM_PA16_CONSTRUCTOR_LOWERING_H
#define CPPGM_PA16_CONSTRUCTOR_LOWERING_H

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

typedef SmallSequence<BindingId, 8> ConstructorMemberPath;
const std::size_t kConstructorProjectionReplayLimit = 8;

template <class Derived>
class ConstructorActionLowering
{
protected:
	void LowerConstructorAction(std::uint32_t node,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& action = derived.arena_.nodes[node];
		if (action.kind != DUMP_CONSTRUCTOR_ACTION ||
			action.binding == kNoBinding ||
			action.binding >= derived.function_symbols_.size() ||
			derived.function_symbols_[action.binding] == kNoLowId)
			throw std::runtime_error("constructor action has no emitted binding");
		const TypeRecord& function_type =
			derived.program_.types.Get(action.type);
		if (function_type.kind != TYPE_FUNCTION ||
			function_type.parameter_count == 0)
			throw std::logic_error("constructor action has invalid function type");
		const TypeId* parameters = derived.program_.types.Parameters(action.type);
		const NodeChildren children = derived.Children(node);
		Instruction call(Instruction::CALL);
		call.type = LowVoid();
		call.first = Operand(Operand::FUNCTION,
			derived.function_symbols_[action.binding], LowPtr());
		CallArguments arguments;
		CallArgumentFlags references;
		arguments.Push(destination);
		references.Push(0);
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const std::size_t parameter = i + 1;
			const bool reference = parameter < function_type.parameter_count &&
				derived.IsReferenceType(parameters[parameter]);
			references.Push(reference ? 1 : 0);
			if (reference)
			{
				const DumpNode& argument = derived.arena_.nodes[children[i]];
				if (argument.category == VALUE_LVALUE ||
					argument.category == VALUE_XVALUE)
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
				const LowType expected = parameter < function_type.parameter_count ?
					derived.LowerType(parameters[parameter]) :
					derived.LowerExpressionType(
						derived.arena_.nodes[children[i]].type);
				arguments.Push(derived.Convert(
					derived.LowerValue(children[i]), expected));
			}
		}
		derived.output_.symbols[
			derived.function_symbols_[action.binding]].referenced = true;
		derived.AttachCallArguments(&call, arguments, references);
		derived.Emit(call);
	}

	void LowerArrayValues(TypeId type, std::uint32_t list_node,
		const Operand& array_address)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const TypeRecord& array = derived.program_.types.Get(
			derived.ExpressionObjectType(type));
		const NodeChildren values = derived.Children(list_node);
		if (array.kind != TYPE_ARRAY || array.bound == 0 ||
			values.size() > array.bound)
			throw std::runtime_error("invalid bounded array initializer");
		const LowType element = derived.LowerExpressionType(array.child);
		const Operand base = derived.DecayAddress(array_address);
		for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
		{
			const Operand destination = derived.IndexAddress(element, base,
				Operand(static_cast<std::int64_t>(i), LowI64()), true);
			if (derived.IsArrayType(array.child))
			{
				if (i >= values.size() ||
					derived.arena_.nodes[values[i]].kind != DUMP_BRACED_INIT_LIST)
					throw std::runtime_error(
						"nested array requires a braced initializer");
				LowerArrayValues(array.child, values[i], destination);
				continue;
			}
			Instruction store(Instruction::STORE);
			store.type = element;
			if (i < values.size())
				store.first = derived.Convert(
					derived.LowerValue(values[i]), element, false);
			else if (element.kind == LOW_PTR)
				store.first = Operand::NullPointer(element);
			else if (IsFloating(element))
				store.first = derived.FloatingOperand("0.0", element);
			else store.first = Operand(0, element);
			store.second = destination;
			derived.Emit(store);
		}
	}

	void LowerConstructorAggregateLeaf(const DumpNode& action,
		const NodeChildren& values, const ConstructorMemberPath& path,
		const Operand& retained_destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (values.size() > 1)
			throw std::logic_error(
				"constructor aggregate leaf has multiple values");
		if (derived.IsArrayType(action.type))
		{
			if (values.size() != 1 ||
				derived.arena_.nodes[values[0]].kind != DUMP_BRACED_INIT_LIST)
				throw std::runtime_error(
					"constructor array member requires braces");
			if (retained_destination.kind == Operand::NONE)
				derived.LowerConstructorArrayActions(
					action.type, values[0], path);
			else LowerArrayValues(
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
				throw std::logic_error(
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
		derived.Emit(store);
	}

	void LowerConstructorAggregateActions(std::uint32_t list_node,
		ConstructorMemberPath* path, const Operand& retained_address)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const NodeChildren actions = derived.Children(list_node);
		for (std::size_t i = 0; i < actions.size(); ++i)
		{
			const DumpNode& action = derived.arena_.nodes[actions[i]];
			if (action.kind != DUMP_INITIALIZER_ACTION ||
				action.binding == kNoBinding)
				throw std::logic_error("invalid constructor aggregate action");
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
			throw std::logic_error(
				"member initialization is outside a constructor");
		if (children.empty()) return;
		if (children.size() != 1)
			throw std::logic_error("member initialization has multiple values");
		const std::uint32_t value_node = children[0];
		const DumpNode& value = derived.arena_.nodes[value_node];
		if (value.kind == DUMP_CONSTRUCTOR_ACTION)
		{
			if (derived.IsTrivialConstructorAction(action.type, children)) return;
			const Operand object = derived.LoadStorage(
				derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
			const Operand destination =
				derived.ProjectAggregateMember(object, action.binding);
			LowerConstructorAction(value_node, destination);
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
			derived.IsArrayType(action.type))
		{
			const Operand object = derived.LoadStorage(
				derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
			const Operand destination =
				derived.ProjectAggregateMember(object, action.binding);
			derived.LowerArrayValues(action.type, value_node, destination);
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
					throw std::logic_error(
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
					throw std::logic_error(
						"scalar brace initialization has many values");
				if (values.empty())
					store.first = store.type.kind == LOW_PTR ?
						Operand::NullPointer(store.type) :
						IsFloating(store.type) ?
						derived.FloatingOperand("0.0", store.type) :
						Operand(0, store.type);
				else store.first = derived.Convert(
					derived.LowerValue(values[0]), store.type, false);
			}
			else store.first = derived.Convert(
				derived.LowerValue(value_node), store.type, false);
		}
		const Operand object = derived.LoadStorage(
			derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
		store.second = derived.ProjectAggregateMember(object, action.binding);
		derived.Emit(store);
	}

};

}
}

#endif
