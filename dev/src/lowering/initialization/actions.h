#ifndef CPPGM_PA16_INITIALIZATION_LOWERING_H
#define CPPGM_PA16_INITIALIZATION_LOWERING_H

#include "lowering/support/utilities.h"
#include "lowering/ir/model.h"
#include "semantic/model/graph.h"
#include "lowering/initialization/zero_fill.h"

#include <cstdint>
#include <stdexcept>

namespace cppgm
{
namespace pa16_lowering_detail
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

const std::size_t kAggregateProjectionReplayLimit = 8;
typedef SmallSequence<BindingId, kAggregateProjectionReplayLimit> AggregatePath;

template <class Derived>
class InitializationLowering
{
public:
	bool NeedsClassInitializerStorageAddress(
		const DumpNode& variable, std::uint32_t initializer) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const EntityId entity = derived.ClassEntity(variable.type);
		const bool closure = entity != kNoEntity &&
			derived.program_.entities[entity].lambda_closure;
		return closure || NeedsAggregateStorageAddress(
			derived.lowering_namespace_object_,
			derived.AggregateHasLeaf(initializer),
			derived.program_.bindings[variable.binding]);
	}

	bool ElidesEmptyConversionCallTransfer(
		const DumpNode& action, const NodeChildren& children) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (action.kind != DUMP_CLASS_VALUE_TRANSFER || children.size() != 1)
			return false;
		const DumpNode& source = derived.arena_.nodes[children[0]];
		const EntityId entity = derived.ClassEntity(action.type);
		return source.kind == DUMP_CALL_EXPRESSION &&
			source.user_conversion_call && entity != kNoEntity &&
			derived.program_.entities[entity].empty_class &&
			derived.program_.entities[entity].trivial_default_constructor;
	}
protected:
	Operand MaterializeDirectClassCallStorage(std::uint32_t node,
		const DumpNode& call, const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const bool retained = derived.full_expression_cleanup_active_ &&
			call.full_expression_staging;
		const LowType type = derived.LowerStorageType(call.type);
		const Operand slot(derived.EnsureGeneratedSlot(
			node, retained ? "call" : "callobj", type), type);
		(void)derived.LowerCall(
			node, call, children, derived.AddressOfStorage(slot));
		return slot;
	}

	Operand TemporaryObjectStorageSlot(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const LowType type = derived.LowerStorageType(
			derived.arena_.nodes[node].type);
		const Operand namespace_backing =
			derived.NamespaceInitializerListBackingStorage(node, type);
		if (namespace_backing.kind != Operand::NONE) return namespace_backing;
		const TypeRecord object_type = derived.program_.types.Get(
			derived.program_.types.RemoveTopCv(
				derived.arena_.nodes[node].type));
		const char* discarded_name = object_type.kind == TYPE_ARRAY ?
			"discardarr" : "discard";
		return Operand(derived.EnsureGeneratedSlot(node,
			derived.arena_.nodes[node].reference_call_materialization ? "refcall" :
			derived.arena_.nodes[node].initializer_list_backing ? "initlist" :
			derived.arena_.nodes[node].argument_materialization ?
				(object_type.kind == TYPE_ARRAY ? "argarr" : "arg") :
			derived.arena_.nodes[node].discarded_materialization ?
				discarded_name :
				(object_type.kind == TYPE_ARRAY &&
				 !derived.arena_.nodes[node].range_for_materialization ?
					"arraytmp" : "tmpobj"),
			type), type);
	}

	Operand PrepareTemporaryObjectStorage(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.temporary_addresses_[node].kind != Operand::NONE)
			return derived.temporary_addresses_[node];
		const Operand slot = TemporaryObjectStorageSlot(node);
		derived.temporary_addresses_[node] = derived.AddressOfStorage(slot);
		return derived.temporary_addresses_[node];
	}

	void EmitClassObjectCopy(TypeId type, const Operand& source,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Instruction copy(Instruction::COPY_OBJECT);
		copy.type = derived.LowerStorageType(type);
		copy.first = source;
		copy.second = destination;
		derived.Emit(copy);
	}

	Operand LowerClassTransferSource(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& source = derived.arena_.nodes[node];
		if (source.kind == DUMP_CAST_EXPRESSION &&
			source.category == VALUE_PRVALUE &&
			source.base_projection_count != 0)
			return derived.LowerValue(node, LowPtr());
		if (source.kind == DUMP_BRACED_INIT_LIST &&
			derived.IsClassObjectType(source.type))
		{
			const LowType type = derived.LowerStorageType(source.type);
			const Operand slot(derived.EnsureGeneratedSlot(node, "tmpobj", type),
				type);
			const Operand destination = derived.AddressOfStorage(slot);
			derived.LowerRuntimeObjectValue(source.type, node, destination);
			return destination;
		}
		if (source.category == VALUE_LVALUE || source.category == VALUE_XVALUE ||
			source.kind == DUMP_TEMPORARY_OBJECT)
			return derived.AddressOfStorage(derived.LowerStorage(node));
		return derived.LowerValue(node,
			derived.LowerExpressionType(source.type));
	}

	void LowerClassValueTransfer(std::uint32_t node,
		const Operand& destination,
		bool elide_empty_call_source = false)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& action = derived.arena_.nodes[node];
		const NodeChildren children = derived.Children(node);
		if (action.kind != DUMP_CLASS_VALUE_TRANSFER || children.size() != 1 ||
			!derived.IsClassObjectType(action.type))
			throw std::logic_error("invalid PA17 class-value transfer action");
		if (action.selected_binding == kNoBinding ||
			action.selected_binding >= derived.program_.bindings.size() ||
			!derived.program_.bindings[action.selected_binding].constructor)
			throw std::logic_error(
				"class-value transfer has no selected constructor fact");
		const DumpNode& source = derived.arena_.nodes[children[0]];
		if (elide_empty_call_source &&
			derived.ElidesEmptyConversionCallTransfer(action, children))
			return;
		if (source.kind == DUMP_CONDITIONAL_EXPRESSION)
		{
			derived.LowerClassConditionalResult(children[0], destination);
			return;
		}
		if (source.kind == DUMP_CLASS_VALUE_TRANSFER)
		{
			derived.LowerClassValueTransfer(
				children[0], destination, elide_empty_call_source);
			return;
		}
		if (source.kind == DUMP_CALL_EXPRESSION)
		{
			(void)derived.LowerCall(children[0], source,
				derived.Children(children[0]), destination);
			return;
		}
		if (source.kind == DUMP_CONSTRUCTOR_ACTION)
		{
			if (source.value_initialization)
				EmitZeroInitialization(action.type, destination);
			if (!IsTrivialConstructorAction(action.type, children))
				derived.LowerConstructorAction(children[0], destination);
			return;
		}
		if (source.kind == DUMP_BRACED_INIT_LIST)
		{
			if (source.value_constructor != kNoDumpEdge)
				derived.LowerConstructorAction(
					source.value_constructor, destination);
			else derived.LowerRuntimeObjectValue(
					action.type, children[0], destination);
			return;
		}
		if (source.kind == DUMP_AGGREGATE_CONSTRUCTION_ACTION)
		{
			derived.LowerAggregateConstructionAction(children[0], destination);
			return;
		}
		EmitClassObjectCopy(action.type,
			LowerClassTransferSource(children[0]), destination);
	}

	bool IsTrivialConstructorAction(TypeId type,
		const NodeChildren& children) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (!derived.IsClassObjectType(type) || children.size() != 1 ||
			derived.arena_.nodes[children[0]].kind != DUMP_CONSTRUCTOR_ACTION)
			return false;
		if (derived.arena_.nodes[children[0]].trivial_special_member_action)
			return false;
		const TypeRecord& record = derived.program_.types.Get(
			derived.ExpressionObjectType(type));
		return derived.program_.entities[record.entity].trivial_default_constructor;
	}

	void EmitZeroInitialization(TypeId type, const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.IsClassObjectType(type))
		{
			const TypeRecord& object = derived.program_.types.Get(
				derived.ExpressionObjectType(type));
			if (derived.program_.entities[object.entity].empty_class) return;
		}
		const LowType storage_type = derived.LowerStorageType(type);
		if (storage_type.kind == LOW_OBJECT &&
			pa16_zero_initialization::ContiguousSpanEligible(
				derived.program_, type))
		{
			Instruction zero(Instruction::ZERO_OBJECT);
			zero.type = storage_type;
			zero.first = destination;
			derived.Emit(zero);
			return;
		}
		const std::size_t size = derived.program_.SizeOf(type);
		if (size > 64)
		{
			const LowType zero_type = derived.LowerStorageType(type);
			Instruction store(Instruction::STORE);
			store.type = zero_type;
			store.first = Operand(0, zero_type);
			store.second = destination;
			derived.Emit(store);
			return;
		}
		std::size_t offset = 0;
		while (offset < size)
		{
			const std::size_t remaining = size - offset;
			const LowType zero_type = remaining >= 8 ? LowI64() :
				remaining >= 4 ? LowI32() : remaining >= 2 ? LowI16() : LowI8();
			Operand address = destination;
			if (offset != 0)
				address = derived.IndexAddress(LowI8(), destination,
					Operand(static_cast<std::int64_t>(offset), LowI64()), false);
			Instruction store(Instruction::STORE);
			store.type = zero_type;
			store.first = Operand(0, zero_type);
			store.second = address;
			derived.Emit(store);
			offset += zero_type.width / 8;
		}
	}

	template <class AggregatePath>
	Operand LowerTemporaryObjectStorage(std::uint32_t node,
		const NodeChildren& children, AggregatePath* path)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 1)
			throw std::runtime_error("invalid temporary object action");
		const bool initialize = derived.temporary_initialized_[node] == 0;
		if (!initialize) return derived.temporary_addresses_[node];
		const bool branch_initializer =
			derived.arena_.nodes[children[0]].kind ==
				DUMP_CONDITIONAL_EXPRESSION;
		derived.ReadyFullExpressionCleanupForTemporary(node);
		if (derived.temporary_addresses_[node].kind == Operand::NONE &&
			!branch_initializer &&
			(!derived.full_expression_deferred_cleanup_ ||
			 (derived.full_expression_uses_branch_cleanup_ &&
			  derived.full_expression_cleanup_ready_)))
			derived.EnsureFullExpressionCleanupSegment();
		const Operand destination = PrepareTemporaryObjectStorage(node);
		if (initialize)
		{
			if (!branch_initializer)
				derived.EnsureFullExpressionCleanupSegment();
			if (derived.arena_.nodes[children[0]].kind == DUMP_CONSTRUCTOR_ACTION)
			{
				if (derived.arena_.nodes[children[0]].value_initialization)
					derived.EmitZeroInitialization(
						derived.arena_.nodes[node].type, destination);
				derived.LowerConstructorAction(children[0], destination);
			}
			else if (derived.arena_.nodes[children[0]].kind == DUMP_BRACED_INIT_LIST)
			{
				const DumpNode& initializer = derived.arena_.nodes[children[0]];
				const TypeRecord temporary_type = derived.program_.types.Get(
					derived.program_.types.RemoveTopCv(
						derived.arena_.nodes[node].type));
				if (temporary_type.kind == TYPE_ARRAY)
				{
					const NodeChildren values = derived.Children(children[0]);
					if (temporary_type.bound == 0 ||
						values.size() > temporary_type.bound)
						throw std::runtime_error(
							"invalid temporary array initializer");
					if (derived.arena_.nodes[node].initializer_list_backing &&
						derived.IsClassObjectType(temporary_type.child))
						derived.LowerInitializerListBackingArray(node,
							derived.arena_.nodes[node].type,
							children[0], destination);
					else if (derived.IsClassObjectType(temporary_type.child) ||
						derived.IsArrayType(temporary_type.child))
						derived.LowerRuntimeArrayValues(
							derived.arena_.nodes[node].type,
							children[0], destination,
							derived.arena_.nodes[node].initializer_list_backing);
					else
					{
						const LowType element = derived.LowerExpressionType(
							temporary_type.child);
						const std::size_t element_size =
							derived.program_.SizeOf(temporary_type.child);
						for (std::size_t i = 0;
							i < static_cast<std::size_t>(temporary_type.bound); ++i)
						{
							Operand element_address = destination;
							if (i != 0 &&
								!derived.arena_.nodes[node].initializer_list_backing)
								element_address = derived.IndexAddress(
									LowI8(), destination,
									Operand(i * element_size, LowI64()), false);
							Instruction store(Instruction::STORE);
							store.type = element;
							store.first = i < values.size() ?
								derived.LowerConvertedValue(values[i], element) :
								Operand(0, element);
							if (i != 0 &&
								derived.arena_.nodes[node].initializer_list_backing)
								element_address = derived.IndexAddress(
									LowI8(), destination,
									Operand(i * element_size, LowI64()), false);
							store.second = element_address;
							derived.Emit(store);
						}
					}
				}
				else if (initializer.value_constructor != kNoDumpEdge)
					derived.LowerConstructorAction(
						initializer.value_constructor, destination, true);
				else derived.LowerAggregateActions(children[0], destination, path,
					destination);
			}
			else if (derived.arena_.nodes[children[0]].kind ==
				DUMP_AGGREGATE_CONSTRUCTION_ACTION)
				derived.LowerAggregateConstructionAction(children[0], destination);
			else if (derived.arena_.nodes[children[0]].kind ==
				DUMP_INITIALIZER_LIST)
				derived.LowerInitializerListObject(children[0], destination);
			else if (derived.arena_.nodes[children[0]].kind == DUMP_CALL_EXPRESSION)
			{
				const DumpNode& call = derived.arena_.nodes[children[0]];
				(void)derived.LowerCall(children[0], call,
					derived.Children(children[0]), destination);
			}
			else if (derived.arena_.nodes[children[0]].kind ==
				DUMP_CONDITIONAL_EXPRESSION)
				derived.LowerClassConditionalResult(children[0], destination);
			else throw std::runtime_error(
				"unsupported temporary object initializer");
			derived.temporary_initialized_[node] = 1;
			derived.MarkConditionalTemporaryConstructed(node);
			if (derived.full_expression_cleanup_active_)
				derived.TransitionFullExpressionCleanup(node);
		}
		return destination;
	}

	void LowerNewInitialization(const DumpNode& record,
		std::uint32_t child, const Operand& result)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpKind kind = derived.arena_.nodes[child].kind;
		if (kind == DUMP_CONSTRUCTOR_ACTION)
			derived.LowerConstructorAction(child, result);
		else if (kind == DUMP_CLASS_VALUE_TRANSFER)
			derived.LowerClassValueTransfer(child, result);
		else if (kind == DUMP_CONDITIONAL_EXPRESSION)
			derived.LowerClassConditionalResult(child, result);
		else if (kind == DUMP_AGGREGATE_CONSTRUCTION_ACTION)
			derived.LowerAggregateConstructionAction(child, result);
		else derived.LowerRuntimeObjectValue(record.operand_type, child, result);
	}

	Operand LowerNewExpression(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (record.array_action)
			return derived.LowerArrayNewExpression(node, record, children);
		if (children.empty() || children.size() > 2)
			throw std::runtime_error("invalid scalar new action");
		const Operand result = derived.LowerValue(children[0], LowPtr());
		if (children.size() != 2) return result;
		if (!record.allocation_may_return_null)
		{
			LowerNewInitialization(record, children[1], result);
			return result;
		}
		const BlockId initialize = derived.AddBlock(derived.NewLabel("new_init"));
		const BlockId done = derived.AddBlock(derived.NewLabel("new_end"));
		const Operand nonnull = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = nonnull.id;
		compare.op = LOW_OP_NE;
		compare.type = LowPtr();
		compare.first = result;
		compare.second = Operand(0, LowPtr());
		derived.Emit(compare);
		derived.EmitBranch(nonnull, initialize, done);
		derived.SelectBlock(initialize);
		LowerNewInitialization(record, children[1], result);
		derived.EmitJump(done);
		derived.SelectBlock(done);
		return result;
	}

	void EmitSelectedDeallocation(const DumpNode& record,
		const Operand& pointer)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BindingId binding = record.binding;
		if (binding == kNoBinding || binding >= derived.function_symbols_.size() ||
			derived.function_symbols_[binding] == kNoLowId)
			throw std::runtime_error("delete action has no deallocation symbol");
		const TypeId function_type = derived.program_.bindings[binding].type;
		const TypeRecord& function = derived.program_.types.Get(function_type);
		const TypeId* parameters = derived.program_.types.Parameters(function_type);
		CallArguments arguments;
		CallArgumentFlags references;
		arguments.Push(pointer);
		references.Push(0);
		if (function.parameter_count == 2)
		{
			arguments.Push(Operand(static_cast<std::int64_t>(
				derived.program_.SizeOf(record.operand_type)),
				derived.LowerType(parameters[1])));
			references.Push(0);
		}
		Instruction call = derived.DirectCallInstruction(
			derived.function_symbols_[binding], LowVoid());
		derived.AttachCallArguments(&call, arguments, references);
		derived.Emit(call);
	}

	Operand LowerDeleteExpression(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (record.array_action)
			return derived.LowerArrayDeleteExpression(node, record, children);
		if (children.size() != 1)
			throw std::runtime_error("invalid scalar delete action");
		Operand pointer;
		const DumpNode& operand = derived.arena_.nodes[children[0]];
		const NodeChildren operand_children = derived.Children(children[0]);
		if (operand.kind == DUMP_CAST_EXPRESSION &&
			operand_children.size() == 1 &&
			derived.arena_.nodes[operand_children[0]].kind == DUMP_LITERAL &&
			derived.arena_.nodes[operand_children[0]].constant &&
			derived.arena_.nodes[operand_children[0]].constant_value == 0)
			pointer = Operand(0, LowPtr());
		else pointer = derived.LowerValue(children[0], LowPtr());
		if (!derived.IsClassObjectType(record.operand_type))
		{
			EmitSelectedDeallocation(record, pointer);
			return Operand(0, LowVoid());
		}
		const BlockId nonnull = derived.AddBlock(
			derived.NewLabel("delete_nonnull"));
		const BlockId done = derived.AddBlock(derived.NewLabel("delete_end"));
		const Operand condition = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = condition.id;
		compare.op = LOW_OP_NE;
		compare.type = LowPtr();
		compare.first = pointer;
		compare.second = Operand(0, LowPtr());
		derived.Emit(compare);
		derived.EmitBranch(condition, nonnull, done);
		derived.SelectBlock(nonnull);
		if (record.virtual_call)
		{
			const Operand table = derived.LoadStorage(pointer, LowPtr());
			Operand entry = table;
			if (record.virtual_slot != 0)
				entry = derived.IndexAddress(LowI8(), table,
					Operand(static_cast<std::int64_t>(record.virtual_slot) * 8,
						LowI64()), false);
			const Operand callee = derived.LoadStorage(entry, LowPtr());
			Instruction call(Instruction::CALL);
			call.type = LowVoid();
			call.indirect = true;
			call.first = callee;
			CallArguments arguments;
			CallArgumentFlags references;
			arguments.Push(pointer);
			references.Push(0);
			derived.AttachCallArguments(&call, arguments, references);
			derived.Emit(call);
		}
		else if (record.selected_binding != kNoBinding)
			derived.EmitDestructorCall(record.selected_binding, pointer);
		if (!record.virtual_call) EmitSelectedDeallocation(record, pointer);
		derived.EmitJump(done);
		derived.SelectBlock(done);
		return Operand(0, LowVoid());
	}

	void LowerLocalClassArrayInitializer(const DumpNode& record,
		const NodeChildren& values)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const TypeRecord& array = derived.program_.types.Get(
			derived.ExpressionObjectType(record.type));
		const Operand base = derived.AddressOfStorage(derived.StorageFor(
			record.binding, derived.LowerStorageType(record.type)));
		const std::size_t element_size = derived.program_.SizeOf(array.child);
		for (std::size_t i = 0; i < values.size(); ++i)
		{
			Operand destination = base;
			if (i != 0)
				destination = derived.IndexAddress(LowI8(), base,
					Operand(static_cast<std::int64_t>(i * element_size),
						LowI64()), false);
			const DumpKind kind = derived.arena_.nodes[values[i]].kind;
			if (kind == DUMP_CONSTRUCTOR_ACTION)
				derived.LowerConstructorAction(values[i], destination);
			else if (kind == DUMP_AGGREGATE_CONSTRUCTION_ACTION)
				derived.LowerAggregateConstructionAction(values[i], destination);
			else derived.LowerRuntimeObjectValue(
				array.child, values[i], destination);
		}
	}

	template <class MemberPath>
	void LowerConstructorArrayActions(TypeId type,
		std::uint32_t list_node, const MemberPath& path)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const TypeRecord& array = derived.program_.types.Get(
			derived.ExpressionObjectType(type));
		const NodeChildren values = derived.Children(list_node);
		if (array.kind != TYPE_ARRAY || array.IsIncompleteArray() ||
			values.size() > array.bound)
			throw std::runtime_error("invalid constructor array initializer");
		if (derived.IsClassObjectType(array.child) ||
			derived.IsArrayType(array.child))
		{
			const Operand base = derived.DecayAddress(
				derived.ProjectConstructorMemberPath(path));
			const std::size_t element_size =
				derived.program_.SizeOf(array.child);
			for (std::size_t i = 0;
				i < static_cast<std::size_t>(array.bound); ++i)
			{
				const Operand destination = derived.IndexAddress(LowI8(), base,
					Operand(static_cast<std::int64_t>(i * element_size),
						LowI64()), true);
				if (i < values.size()) derived.LowerRuntimeObjectValue(
					array.child, values[i], destination);
				else derived.LowerRuntimeZeroValue(array.child, destination);
			}
			return;
		}
		const LowType element = derived.LowerExpressionType(array.child);
		for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
		{
			Operand value;
			if (i < values.size())
				value = derived.LowerConvertedValue(values[i], element);
			else if (element.kind == LOW_PTR)
				value = Operand::NullPointer(element);
			else if (IsFloating(element))
				value = derived.FloatingOperand("0.0", element);
			else value = Operand(0, element);
			const Operand base = derived.DecayAddress(
				derived.ProjectConstructorMemberPath(path));
			const Operand destination = derived.IndexAddress(element, base,
				Operand(static_cast<std::int64_t>(i), LowI64()), true);
			Instruction store(Instruction::STORE);
			store.type = element;
			store.first = value;
			store.second = destination;
			derived.Emit(store);
		}
	}

	void LowerNamespaceClassArrayConstructor(const DumpNode& record,
		TypeId element_type, std::size_t element_index, std::uint32_t value)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.arena_.nodes[value].kind != DUMP_CONSTRUCTOR_ACTION)
			throw std::runtime_error(
				"class array element has no construction recipe");
		Operand destination = derived.AddressOfStorage(derived.StorageFor(
			record.binding, derived.LowerStorageType(record.type)));
		destination = derived.DecayAddress(destination);
		Operand displacement(
			static_cast<std::int64_t>(element_index), LowI64());
		const std::size_t element_size = derived.program_.SizeOf(element_type);
		if (element_size != 1)
		{
			const Operand scaled = derived.Temp(LowI64());
			Instruction multiply(Instruction::BINARY);
			multiply.dest = scaled.id;
			multiply.op = LOW_OP_MUL;
			multiply.type = LowI64();
			multiply.first = displacement;
			multiply.second = Operand(
				static_cast<std::int64_t>(element_size), LowI64());
			derived.Emit(multiply);
			displacement = scaled;
		}
		destination = derived.IndexAddress(
			LowI8(), destination, displacement, true);
		derived.LowerConstructorAction(value, destination);
	}

	template <class AggregatePath>
	void LowerNamespaceClassArrayInitializer(const DumpNode& record,
		const TypeRecord& array, const NodeChildren& values,
		AggregatePath* path)
	{
		Derived& derived = static_cast<Derived&>(*this);
		for (std::size_t i = 0; i < values.size(); ++i)
		{
			if (derived.arena_.nodes[values[i]].kind == DUMP_BRACED_INIT_LIST)
				derived.LowerBoundAggregateArrayActions(record.binding, record.type,
					i, values[i], path);
			else LowerNamespaceClassArrayConstructor(
				record, array.child, i, values[i]);
		}
	}

	bool LowerVariableConstructor(const DumpNode& variable,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.IsClassObjectType(variable.type) || children.size() != 1 ||
			derived.arena_.nodes[children[0]].kind != DUMP_CONSTRUCTOR_ACTION)
			return false;
		const LowType type = derived.LowerStorageType(variable.type);
		const Operand destination = derived.AddressOfStorage(
			derived.StorageFor(variable.binding, type));
		if (derived.arena_.nodes[children[0]].value_initialization)
			EmitZeroInitialization(variable.type, destination);
		if (!IsTrivialConstructorAction(variable.type, children))
			derived.LowerConstructorAction(
				children[0], destination, false, false, true);
		return true;
	}

	bool LowerRuntimeConstructorValue(TypeId type, std::uint32_t node,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpKind kind = derived.arena_.nodes[node].kind;
		if (kind == DUMP_CLASS_VALUE_TRANSFER)
		{
			derived.LowerClassValueTransfer(node, destination);
			return true;
		}
		if (kind == DUMP_AGGREGATE_CONSTRUCTION_ACTION)
		{
			derived.LowerAggregateConstructionAction(node, destination);
			return true;
		}
		if (kind != DUMP_CONSTRUCTOR_ACTION) return false;
		if (derived.arena_.nodes[node].value_initialization)
			EmitZeroInitialization(type, destination);
		NodeChildren action;
		action.Push(node);
		if (!IsTrivialConstructorAction(type, action))
			derived.LowerConstructorAction(node, destination);
		return true;
	}

	bool LowerClassValueInitialization(const DumpNode& variable,
		std::uint32_t initializer, const Operand& storage)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.arena_.nodes[initializer].value_initialization) return false;
		EmitZeroInitialization(variable.type, derived.AddressOfStorage(storage));
		const TypeRecord& object = derived.program_.types.Get(
			derived.ExpressionObjectType(variable.type));
		return derived.program_.entities[object.entity].trivial_default_constructor;
	}

	template <class AggregatePath>
	void LowerAggregateArrayLeaf(const DumpNode& action,
		const NodeChildren& values, const Operand& root,
		const AggregatePath& path, const Operand& retained_destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (values.size() != 1 ||
			derived.arena_.nodes[values[0]].kind != DUMP_BRACED_INIT_LIST)
			throw std::runtime_error("array member requires a braced initializer");
		if (retained_destination.kind != Operand::NONE)
		{
			derived.LowerArrayValues(action.type, values[0], retained_destination);
			return;
		}
		const TypeRecord& array = derived.program_.types.Get(
			derived.ExpressionObjectType(action.type));
		const NodeChildren elements = derived.Children(values[0]);
		if (array.kind != TYPE_ARRAY || array.bound == 0 ||
			elements.size() > array.bound)
			throw std::runtime_error("invalid aggregate array initializer");
		const LowType element = derived.LowerExpressionType(array.child);
		for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
		{
			const Operand base = derived.DecayAddress(
				derived.ProjectAggregatePath(root, path));
			const Operand destination = derived.IndexAddress(element, base,
				Operand(static_cast<std::int64_t>(i), LowI64()), true);
			Instruction store(Instruction::STORE);
			store.type = element;
			if (i < elements.size())
				store.first = derived.LowerConvertedValue(elements[i], element);
			else if (element.kind == LOW_PTR)
				store.first = Operand::NullPointer(element);
			else if (IsFloating(element))
				store.first = derived.FloatingOperand("0.0", element);
			else store.first = Operand(0, element);
			store.second = destination;
			derived.Emit(store);
		}
	}

	template <class AggregatePath>
	bool LowerAggregateConstructorLeaf(const DumpNode& action,
		const NodeChildren& values, const Operand& root,
		const AggregatePath& path, const Operand& retained_destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (values.size() != 1) return false;
		const DumpKind kind = derived.arena_.nodes[values[0]].kind;
		if (kind != DUMP_CONSTRUCTOR_ACTION &&
			kind != DUMP_CLASS_VALUE_TRANSFER) return false;
		const BindingId selected_constructor = kind == DUMP_CONSTRUCTOR_ACTION ?
			derived.arena_.nodes[values[0]].binding :
			derived.arena_.nodes[values[0]].selected_binding;
		const bool cleanup_segment =
			derived.full_expression_cleanup_active_ &&
			selected_constructor != kNoBinding &&
			!derived.program_.bindings[selected_constructor].nonthrowing;
		if (cleanup_segment)
			derived.EnsureFullExpressionCleanupSegment();
		const Operand destination = retained_destination.kind == Operand::NONE ?
			derived.ProjectAggregatePath(root, path) : retained_destination;
		if (kind == DUMP_CLASS_VALUE_TRANSFER)
		{
			derived.LowerClassValueTransfer(values[0], destination);
			if (cleanup_segment)
				derived.PauseFullExpressionCleanupSegment();
			return true;
		}
		if (derived.arena_.nodes[values[0]].elide_empty_constructor)
		{
			if (cleanup_segment)
				derived.PauseFullExpressionCleanupSegment();
			return true;
		}
		if (derived.arena_.nodes[values[0]].value_initialization)
			EmitZeroInitialization(action.type, destination);
		NodeChildren constructor;
		constructor.Push(values[0]);
		if (!IsTrivialConstructorAction(action.type, constructor))
			derived.LowerConstructorAction(
				values[0], destination, false, true);
		if (cleanup_segment)
			derived.PauseFullExpressionCleanupSegment();
		return true;
	}
	void LowerVariableInitializationCore(const DumpNode& record,
		const NodeChildren& children,
		const Operand& retained_destination = Operand())
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (retained_destination.kind != Operand::NONE &&
			!derived.IsReferenceType(record.type) &&
			derived.IsClassObjectType(record.type) && children.size() == 1 &&
			derived.arena_.nodes[children[0]].kind == DUMP_CALL_EXPRESSION)
		{
			const DumpNode& call = derived.arena_.nodes[children[0]];
			(void)derived.LowerCall(children[0], call, derived.Children(children[0]),
				retained_destination);
			return;
		}
		if (derived.LowerScalarCallReferenceInitialization(record, children,
			retained_destination)) return;
		if (derived.TryLowerComplexVariableInitialization(
			record, children, retained_destination)) return;
		if (!derived.IsReferenceType(record.type) &&
			derived.IsClassObjectType(record.type) && children.size() == 1 &&
			derived.arena_.nodes[children[0]].kind == DUMP_CONDITIONAL_EXPRESSION)
		{
			const LowType type = derived.LowerStorageType(record.type);
			const Operand destination = retained_destination.kind == Operand::NONE ?
				derived.AddressOfStorage(derived.StorageFor(record.binding, type)) :
				retained_destination;
			derived.LowerClassConditionalResult(children[0], destination);
			return;
		}
		if (derived.IsClassObjectType(record.type) && children.size() == 1 &&
			derived.arena_.nodes[children[0]].kind == DUMP_CLASS_VALUE_TRANSFER)
		{
			const LowType type = derived.LowerStorageType(record.type);
			const Operand destination = retained_destination.kind == Operand::NONE ?
				derived.AddressOfStorage(derived.StorageFor(record.binding, type)) :
				retained_destination;
			derived.LowerClassValueTransfer(children[0], destination, true);
			return;
		}
		if (derived.LowerInitializerListVariable(record, children)) return;
		if (children.size() == 1 && derived.arena_.nodes[children[0]].kind ==
			DUMP_CONSTRUCTOR_ARRAY_ACTION)
		{
			derived.LowerBoundConstructorArray(children[0], record.binding);
			return;
		}
		if (derived.IsClassObjectType(record.type) && children.size() == 1 &&
			derived.arena_.nodes[children[0]].kind == DUMP_BRACED_INIT_LIST)
		{
			derived.LowerClassInitializer(record, children[0]);
			return;
		}
		if (derived.LowerVariableConstructor(record, children)) return;
		if (derived.TryLowerConstantArrayTemplate(record, children)) return;
		if (!children.empty())
		{
			if (!derived.IsReferenceType(record.type) && derived.IsArrayType(record.type))
			{
				derived.LowerArrayInitializer(record, children);
				return;
			}
			const LowType type = derived.LowerStorageType(record.type);
			Instruction store(Instruction::STORE);
			store.type = type;
			store.volatile_access = !derived.IsReferenceType(record.type) &&
				derived.TypeIsVolatile(record.type);
			const Operand value = derived.IsReferenceType(record.type) ?
				derived.AddressOfStorage(derived.LowerStorage(children[0])) :
				derived.LowerInitializerConvertedValue(children[0], type);
			if (derived.CurrentBlock().terminated) return;
			store.first = value;
			store.second = retained_destination.kind == Operand::NONE ?
				derived.StorageFor(record.binding, type) : retained_destination;
			derived.Emit(store);
		}
		else if (derived.IsClassObjectType(record.type))
		{
			const LowType type = derived.LowerStorageType(record.type);
			(void)derived.AddressOfStorage(derived.StorageFor(record.binding, type));
		}
	}

	void LowerArrayInitializer(const DumpNode& record,
		const NodeChildren& variable_children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (variable_children.size() != 1)
			throw std::runtime_error("invalid PA15 array initializer");
		const NodeChildren values = derived.Children(variable_children[0]);
		const TypeRecord& array = derived.program_.types.Get(
			derived.ExpressionObjectType(record.type));
		if (array.kind != TYPE_ARRAY ||
			(array.IsIncompleteArray() &&
			 (record.storage_size == 0 || !values.empty())) ||
			values.size() > array.bound)
			throw std::runtime_error("invalid PA15 bounded array initializer");
		if (array.IsIncompleteArray())
		{
			(void)derived.AddressOfStorage(derived.StorageFor(
				record.binding, derived.LowerVariableStorage(record)));
			return;
		}
		if (!derived.lowering_namespace_object_ &&
			!derived.IsClassObjectType(array.child) && !derived.IsArrayType(array.child))
		{
			const Operand base = derived.AddressOfStorage(
				derived.StorageFor(record.binding, derived.LowerVariableStorage(record)));
			const LowType element = derived.LowerExpressionType(array.child);
			const std::size_t element_size = derived.program_.SizeOf(array.child);
			for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
			{
				Operand destination = base;
				if (i != 0)
					destination = derived.IndexAddress(LowI8(), base,
						Operand(i * element_size, LowI64()), false);
				Instruction store(Instruction::STORE);
				store.type = element;
				store.first = i < values.size() ?
					derived.LowerConvertedValue(values[i], element) : Operand(0, element);
				store.second = destination;
				derived.Emit(store);
			}
			return;
		}
		if (derived.IsClassObjectType(array.child))
		{
			if (!derived.lowering_namespace_object_)
			{
				derived.LowerLocalClassArrayInitializer(record, values);
				return;
			}
			AggregatePath path;
			derived.LowerNamespaceClassArrayInitializer(record, array, values, &path);
			return;
		}
		const Operand storage = derived.StorageFor(
			record.binding, derived.LowerStorageType(record.type));
		derived.LowerRuntimeArrayValues(record.type, variable_children[0],
			derived.AddressOfStorage(storage), true);
	}
	void LowerBoundAggregateArrayActions(BindingId object, TypeId array_type,
		std::size_t element_index, std::uint32_t list_node,
		AggregatePath* path)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const NodeChildren actions = derived.Children(list_node);
		for (std::size_t i = 0; i < actions.size(); ++i)
		{
			const DumpNode& action = derived.arena_.nodes[actions[i]];
			if (action.kind != DUMP_INITIALIZER_ACTION ||
				action.binding == kNoBinding)
				throw std::logic_error("invalid bound aggregate array action");
			const NodeChildren values = derived.Children(actions[i]);
			const bool nested = values.size() == 1 &&
				derived.arena_.nodes[values[0]].kind == DUMP_BRACED_INIT_LIST &&
				derived.IsClassObjectType(action.type);
			path->Push(action.binding);
			if (nested)
				derived.LowerBoundAggregateArrayActions(object, array_type, element_index,
					values[0], path);
			else
			{
				if (values.size() > 1 || derived.IsArrayType(action.type))
					throw std::runtime_error(
						"complex bound aggregate leaf is outside the checkpoint");
				Instruction store(Instruction::STORE);
				if (derived.IsReferenceType(action.type))
				{
					if (values.empty())
						throw std::logic_error(
							"aggregate reference action has no value");
					store.type = LowPtr();
					store.first = derived.AddressOfStorage(derived.LowerStorage(values[0]));
				}
				else
				{
					store.type = derived.LowerExpressionType(action.type);
					store.first = values.empty() ?
						(store.type.kind == LOW_PTR ?
							Operand::NullPointer(store.type) :
						 IsFloating(store.type) ?
							derived.FloatingOperand("0.0", store.type) :
							Operand(0, store.type)) :
						derived.LowerConvertedValue(values[0], store.type, false);
				}
				Operand destination = derived.AddressOfStorage(derived.StorageFor(object,
					derived.LowerStorageType(array_type)));
				destination = derived.DecayAddress(destination);
				const TypeRecord& array = derived.program_.types.Get(
					derived.ExpressionObjectType(array_type));
				Operand displacement(static_cast<std::int64_t>(element_index),
					LowI64());
				const std::size_t element_size = derived.program_.SizeOf(array.child);
				if (element_size != 1)
				{
					const Operand scaled = derived.Temp(LowI64());
					Instruction multiply(Instruction::BINARY);
					multiply.dest = scaled.id;
					multiply.op = LOW_OP_MUL;
					multiply.type = LowI64();
					multiply.first = displacement;
					multiply.second = Operand(
						static_cast<std::int64_t>(element_size), LowI64());
					derived.Emit(multiply);
					displacement = scaled;
				}
				destination = derived.IndexAddress(LowI8(), destination,
					displacement, true);
				for (std::size_t member = 0; member < path->size(); ++member)
					destination = derived.ProjectAggregateMember(destination,
						(*path)[member]);
				store.second = destination;
				derived.Emit(store);
			}
			path->Pop();
		}
	}
	void LowerRuntimeArrayValues(TypeId type, std::uint32_t list_node,
		const Operand& array_address, bool compact_addressing = false)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const TypeRecord& array = derived.program_.types.Get(
			derived.ExpressionObjectType(type));
		const NodeChildren values = derived.Children(list_node);
		if (array.kind != TYPE_ARRAY || array.IsIncompleteArray() ||
			values.size() > array.bound)
			throw std::runtime_error("invalid runtime array initializer");
		const Operand base = compact_addressing ?
			array_address : derived.DecayAddress(array_address);
		const std::size_t element_size = derived.program_.SizeOf(array.child);
		for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
		{
			const Operand displacement(static_cast<std::int64_t>(
				i * element_size), LowI64());
			const Operand destination = compact_addressing && i == 0 ? base :
				derived.IndexAddress(LowI8(), base, displacement, true);
			if (i < values.size())
				derived.LowerRuntimeObjectValue(array.child, values[i], destination);
			else derived.LowerRuntimeZeroValue(array.child, destination);
		}
	}
	void LowerRuntimeObjectValue(TypeId type, std::uint32_t node,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.LowerInitializerListRuntimeValue(node, destination)) return;
		const TypeRecord& record = derived.program_.types.Get(derived.ExpressionObjectType(type));
		if (record.kind == TYPE_ARRAY)
		{
			if (derived.arena_.nodes[node].kind != DUMP_BRACED_INIT_LIST)
				throw std::runtime_error("nested runtime array requires braces");
			derived.LowerRuntimeArrayValues(type, node, destination, true);
			return;
		}
		if (derived.IsClassObjectType(type))
		{
			if (derived.LowerRuntimeConstructorValue(type, node, destination)) return;
			if (derived.arena_.nodes[node].kind != DUMP_BRACED_INIT_LIST)
				throw std::runtime_error("runtime aggregate element requires braces");
			AggregatePath path;
			derived.LowerAggregateActions(node, destination, &path, destination);
			return;
		}
		Instruction store(Instruction::STORE);
		store.type = derived.LowerExpressionType(type);
		store.first = derived.LowerConvertedValue(node, store.type, false);
		store.second = destination;
		derived.Emit(store);
	}
	void LowerClassInitializer(const DumpNode& variable,
		std::uint32_t initializer)
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.ResetInitializedBitFieldUnit();
		const Operand storage = derived.StorageFor(variable.binding,
			derived.LowerStorageType(variable.type));
		if (derived.LowerClassValueInitialization(variable, initializer, storage)) return;
		Operand retained_address; if (derived.NeedsClassInitializerStorageAddress(variable, initializer)) retained_address = derived.AddressOfStorage(storage);
		AggregatePath path; derived.LowerAggregateActions(initializer, storage, &path,
			LambdaClosureEntity(derived.program_, variable.type) == kNoEntity ? Operand() : retained_address);
	}
	bool AggregateHasLeaf(std::uint32_t list_node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const NodeChildren actions = derived.Children(list_node);
		for (std::size_t i = 0; i < actions.size(); ++i)
		{
			const NodeChildren values = derived.Children(actions[i]);
			if (values.size() == 1 &&
				derived.arena_.nodes[values[0]].kind == DUMP_BRACED_INIT_LIST &&
				derived.IsClassObjectType(derived.arena_.nodes[actions[i]].type))
			{
				if (AggregateHasLeaf(values[0])) return true;
			}
			else return true;
		}
		return false;
	}
	void LowerAggregateActions(std::uint32_t list_node,
		const Operand& root, AggregatePath* path,
		const Operand& retained_address)
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.ResetInitializedBitFieldUnit();
		if (derived.stats_) ++derived.stats_->lowered_nodes;
		const DumpNode& list = derived.arena_.nodes[list_node];
		if (list.kind != DUMP_BRACED_INIT_LIST)
			throw std::logic_error("class initializer is not an action list");
		const NodeChildren actions = derived.Children(list_node);
		for (std::size_t i = 0; i < actions.size(); ++i)
		{
			const DumpNode& action = derived.arena_.nodes[actions[i]];
			if (action.kind != DUMP_INITIALIZER_ACTION ||
				action.binding == kNoBinding ||
				action.binding >= derived.program_.bindings.size())
				throw std::logic_error("invalid aggregate initializer action");
			if (derived.stats_) ++derived.stats_->lowered_nodes;
			const NodeChildren values = derived.Children(actions[i]);
			const bool nested = values.size() == 1 &&
				derived.arena_.nodes[values[0]].kind == DUMP_BRACED_INIT_LIST &&
				derived.IsClassObjectType(action.type);
			if (retained_address.kind != Operand::NONE)
			{
				const Operand destination = derived.ProjectAggregateMember(
					retained_address, action.binding);
				if (nested)
					derived.LowerAggregateActions(values[0], root, path, destination);
				else
					derived.LowerAggregateLeaf(action, values, root, *path, destination);
				continue;
			}
			path->Push(action.binding);
			if (nested && path->size() == kAggregateProjectionReplayLimit)
			{
				const Operand destination = derived.ProjectAggregatePath(root, *path);
				derived.LowerAggregateActions(values[0], root, path, destination);
			}
			else if (nested)
				derived.LowerAggregateActions(values[0], root, path, Operand());
			else
				derived.LowerAggregateLeaf(action, values, root, *path, Operand());
			path->Pop();
		}
	}
	Operand ProjectAggregateMember(const Operand& base, BindingId binding)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BindingRecord& member = derived.program_.bindings[binding];
		if (IsLambdaCaptureMember(derived.program_, binding)) return base;
		const Operand projected = derived.Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = projected.id;
		index.type = LowI8();
		index.first = base;
		index.second = Operand(
			static_cast<std::int64_t>(
				derived.program_.BindingLayout(member).member_offset), LowI64());
		index.projection = INDEX_PROJECTION_FIELD;
		derived.Emit(index);
		return projected;
	}
	Operand ProjectConstructorMemberPath(
		const pa16_lowering_detail::ConstructorMemberPath& path)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Operand destination = derived.LoadStorage(
			derived.StorageFor(derived.current_this_binding_, LowPtr()), LowPtr());
		for (std::size_t i = 0; i < path.size(); ++i)
			destination = derived.ProjectAggregateMember(destination, path[i]);
		return destination;
	}
	Operand ProjectAggregatePath(const Operand& root,
		const AggregatePath& path)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Operand destination = derived.AddressOfStorage(root);
		for (std::size_t i = 0; i < path.size(); ++i)
			destination = derived.ProjectAggregateMember(destination, path[i]);
		return destination;
	}

	void LowerAggregateLeaf(const DumpNode& action,
		const NodeChildren& values, const Operand& root,
		const AggregatePath& path, const Operand& retained_destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (values.size() > 1)
			throw std::logic_error("aggregate leaf has multiple values");
		if (derived.IsArrayType(action.type))
		{
			derived.LowerAggregateArrayLeaf(
				action, values, root, path, retained_destination);
			return;
		}
		if (derived.LowerAggregateConstructorLeaf(
			action, values, root, path, retained_destination)) return;
		Instruction store(Instruction::STORE);
		if (derived.IsReferenceType(action.type))
		{
			if (values.empty())
				throw std::logic_error("aggregate reference action has no value");
			store.type = LowPtr();
			store.first = derived.AddressOfStorage(derived.LowerStorage(values[0]));
		}
		else
		{
			store.type = derived.LowerExpressionType(action.type);
			if (!values.empty())
			{
				const LowType source = derived.LowerExpressionType(derived.arena_.nodes[values[0]].type);
				store.first = derived.LowerConvertedValue(values[0], store.type, IsInteger(source) &&
					IsInteger(store.type) && source.is_signed == store.type.is_signed);
			}
			else if (store.type.kind == LOW_PTR)
				store.first = Operand::NullPointer(store.type);
			else if (IsFloating(store.type))
				store.first = derived.FloatingOperand("0.0", store.type);
			else if (IsInteger(store.type))
				store.first = Operand(0, store.type);
			else throw std::runtime_error(
				"aggregate leaf requires unsupported construction");
		}
		const Operand destination =
			retained_destination.kind == Operand::NONE ?
				derived.ProjectAggregatePath(root, path) : retained_destination;
		if (action.binding != kNoBinding &&
			derived.program_.bindings[action.binding].bit_field)
		{
			const LowType field_type = derived.LowerExpressionType(action.type);
			store.second = destination;
			derived.InitializeBitField(
				action.binding, store.first, store.second, field_type);
		}
		else
		{
			store.second = destination;
			derived.Emit(store);
		}
	}
};

}
}

#endif
