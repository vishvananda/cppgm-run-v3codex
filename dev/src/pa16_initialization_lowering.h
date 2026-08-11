#ifndef CPPGM_PA16_INITIALIZATION_LOWERING_H
#define CPPGM_PA16_INITIALIZATION_LOWERING_H

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
	Operand TemporaryObjectStorageSlot(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const LowType type = derived.LowerStorageType(
			derived.arena_.nodes[node].type);
		const TypeRecord object_type = derived.program_.types.Get(
			derived.program_.types.RemoveTopCv(
				derived.arena_.nodes[node].type));
		const char* discarded_name = object_type.kind == TYPE_ARRAY ?
			"discardarr" : "discard";
		return Operand(derived.EnsureGeneratedSlot(node,
			derived.arena_.nodes[node].reference_call_materialization ? "refcall" :
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
			if (derived.UsesIndirectClassResult(source.type, source.binding))
				(void)derived.LowerCall(children[0], source,
					derived.Children(children[0]), destination);
			else EmitClassObjectCopy(action.type,
				derived.LowerCall(children[0], source,
					derived.Children(children[0])), destination);
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
		if (derived.temporary_addresses_[node].kind == Operand::NONE &&
			!derived.full_expression_deferred_cleanup_)
			derived.EnsureFullExpressionCleanupSegment();
		const Operand destination = PrepareTemporaryObjectStorage(node);
		if (initialize)
		{
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
					if (derived.IsClassObjectType(temporary_type.child) ||
						derived.IsArrayType(temporary_type.child))
						derived.LowerRuntimeArrayValues(
							derived.arena_.nodes[node].type,
							children[0], destination);
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
							if (i != 0)
								element_address = derived.IndexAddress(
									LowI8(), destination,
									Operand(i * element_size, LowI64()), false);
							Instruction store(Instruction::STORE);
							store.type = element;
							store.first = i < values.size() ?
								derived.LowerConvertedValue(values[i], element) :
								Operand(0, element);
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
			else if (derived.arena_.nodes[children[0]].kind == DUMP_CALL_EXPRESSION)
			{
				const DumpNode& call = derived.arena_.nodes[children[0]];
				if (derived.UsesIndirectClassResult(call.type, call.binding))
					(void)derived.LowerCall(children[0], call,
						derived.Children(children[0]), destination);
				else EmitClassObjectCopy(call.type,
					derived.LowerValue(children[0],
						derived.LowerExpressionType(call.type)), destination);
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
		const Operand nonnull = derived.Temp(LowU8());
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
		Instruction call(Instruction::CALL);
		call.type = LowVoid();
		call.first = Operand(Operand::FUNCTION,
			derived.function_symbols_[binding], LowPtr());
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
		derived.output_.symbols[
			derived.function_symbols_[binding]].referenced = true;
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
		const Operand condition = derived.Temp(LowU8());
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
		if (derived.arena_.nodes[node].kind != DUMP_CONSTRUCTOR_ACTION)
			return false;
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
		const Operand destination = retained_destination.kind == Operand::NONE ?
			derived.ProjectAggregatePath(root, path) : retained_destination;
		if (kind == DUMP_CLASS_VALUE_TRANSFER)
		{
			derived.LowerClassValueTransfer(values[0], destination);
			return true;
		}
		if (derived.arena_.nodes[values[0]].elide_empty_constructor) return true;
		if (derived.arena_.nodes[values[0]].value_initialization)
			EmitZeroInitialization(action.type, destination);
		NodeChildren constructor;
		constructor.Push(values[0]);
		if (!IsTrivialConstructorAction(action.type, constructor))
			derived.LowerConstructorAction(
				values[0], destination, false, true);
		return true;
	}
};

}
}

#endif
