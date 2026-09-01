#ifndef CPPGM_LOWERING_EXTENSIONS_INITIALIZER_LISTS_H
#define CPPGM_LOWERING_EXTENSIONS_INITIALIZER_LISTS_H

#include "lowering/support/identity_maps.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/ir/model.h"
#include "semantic/model/graph.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

const std::size_t kInitializerListInlineCleanupLimit = 8;
typedef SmallSequence<Operand, kInitializerListInlineCleanupLimit>
	InitializerListElementAddresses;

struct InitializerListLoweringState
{
	std::vector<SymbolId> backing_symbols;
	FlatIdMap element_address_index;
	std::vector<InitializerListElementAddresses> element_addresses;
	std::size_t backing_ordinal;
	BlockId lifetime_observation_dispatch;

	InitializerListLoweringState()
		: backing_ordinal(0), lifetime_observation_dispatch(kNoLowId) {}
};

template <class Derived>
class InitializerListLowering
{
protected:
	void ResetInitializerListFunctionState()
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.initializer_lists_.element_address_index.Clear();
		derived.initializer_lists_.element_addresses.clear();
		derived.initializer_lists_.lifetime_observation_dispatch = kNoLowId;
	}

	void RecordInitializerListElementAddress(
		std::uint32_t backing, const Operand& address)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::uint32_t index = 0;
		if (!derived.initializer_lists_.element_address_index.Find(
			backing, &index))
		{
			index = static_cast<std::uint32_t>(
				derived.initializer_lists_.element_addresses.size());
			derived.initializer_lists_.element_address_index.Insert(backing, index);
			derived.initializer_lists_.element_addresses.push_back(
				InitializerListElementAddresses());
		}
		derived.initializer_lists_.element_addresses[index].Push(address);
	}

	const InitializerListElementAddresses* InitializerListElementAddressPlan(
		std::uint32_t backing) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		std::uint32_t index = 0;
		if (!derived.initializer_lists_.element_address_index.Find(
			backing, &index) ||
			index >= derived.initializer_lists_.element_addresses.size())
			return 0;
		return &derived.initializer_lists_.element_addresses[index];
	}

	void RegisterNamespaceInitializerListBacking(
		const NamespaceObjectAction& action)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.initializer_lists_.backing_symbols.empty())
			derived.initializer_lists_.backing_symbols.resize(
				derived.arena_.nodes.size(), kNoLowId);
		const std::uint32_t node = action.initializer_list_backing;
		if (node == kNoDumpEdge) return;
		if (node >= derived.initializer_lists_.backing_symbols.size() ||
			derived.arena_.nodes[node].kind != DUMP_TEMPORARY_OBJECT)
			ThrowLoweringInternal(
				"invalid namespace initializer-list backing fact");
		if (derived.initializer_lists_.backing_symbols[node] != kNoLowId) return;
		const SymbolId symbol = derived.AddSyntheticSymbol(
			Symbol::GLOBAL_SYMBOL, "__cppgm_initlist_backing_" +
				std::to_string(++derived.initializer_lists_.backing_ordinal),
			std::string(), true);
		derived.initializer_lists_.backing_symbols[node] = symbol;
		Symbol& symbol_record = derived.output_.symbols[symbol];
		symbol_record.definition_emitted = true;
		symbol_record.referenced = true;
		symbol_record.thread_local_storage =
			derived.program_.bindings[action.object].thread_local_storage;
		Global global;
		global.symbol = symbol;
		global.type = derived.LowerStorageType(derived.arena_.nodes[node].type);
		derived.static_initializers_.SetZero(
			derived.arena_.nodes[node].type, &global);
		derived.output_.globals.push_back(global);
		if (derived.stats_) ++derived.stats_->globals;
	}

	BlockId InitializerListLifetimeObservationDispatch(
		std::uint32_t condition) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		return derived.arena_.nodes[condition].
			initializer_list_lifetime_observation ?
			derived.initializer_lists_.lifetime_observation_dispatch :
			BlockId(kNoLowId);
	}

	Operand NamespaceInitializerListBackingStorage(
		std::uint32_t node, const LowType& type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (node >= derived.initializer_lists_.backing_symbols.size())
			return Operand();
		const SymbolId symbol = derived.initializer_lists_.backing_symbols[node];
		return symbol == kNoLowId ? Operand() :
			Operand(Operand::GLOBAL, symbol, type);
	}

	bool LowerInitializerListBackingArray(std::uint32_t backing,
		TypeId type, std::uint32_t list, const Operand& base)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& backing_record = derived.arena_.nodes[backing];
		if (!backing_record.initializer_list_backing) return false;
		const TypeRecord& array = derived.program_.types.Get(
			derived.ExpressionObjectType(type));
		const NodeChildren values = derived.Children(list);
		if (array.kind != TYPE_ARRAY || array.bound == 0 ||
			values.size() > array.bound ||
			!derived.IsClassObjectType(array.child))
			ThrowLoweringInternal(
				"invalid class initializer-list backing recipe");
		const BindingId destructor = backing_record.selected_binding;
		const bool staged_full_expression =
			derived.full_expression_cleanup_active_;
		const std::size_t count = static_cast<std::size_t>(array.bound);
		const std::size_t element_size = derived.program_.SizeOf(array.child);
		if (count <= kInitializerListInlineCleanupLimit)
		{
			for (std::size_t i = 0; i < count; ++i)
			{
				BlockId dispatch = kNoLowId;
				BlockId end = kNoLowId;
				if (destructor != kNoBinding && i != 0)
				{
					dispatch = derived.AddBlock(
						derived.NewLabel("call_unwind_dispatch"));
					end = derived.AddBlock(derived.NewLabel("call_unwind_end"));
					if (staged_full_expression)
						derived.EmitEhTarget(Instruction::EH_TRY, dispatch);
				}
				const Operand destination = i == 0 ? base : derived.IndexAddress(
					LowI8(), base, Operand(i * element_size, LowI64()), true);
				if (dispatch != kNoLowId && !staged_full_expression)
					derived.EmitEhTarget(Instruction::EH_TRY, dispatch);
				const bool previous_suppression =
					derived.SetFullExpressionCleanupStartSuppressed(
						dispatch != kNoLowId && staged_full_expression);
				if (i < values.size())
					derived.LowerRuntimeObjectValue(
						array.child, values[i], destination);
				else derived.LowerRuntimeZeroValue(array.child, destination);
				(void)derived.SetFullExpressionCleanupStartSuppressed(
					previous_suppression);
				RecordInitializerListElementAddress(backing, destination);
				if (i == 0 && derived.full_expression_cleanup_active_ &&
					derived.full_expression_segment_actions_.empty())
					derived.PauseFullExpressionCleanupSegment();
				if (dispatch == kNoLowId) continue;
				derived.Emit(Instruction(Instruction::EH_END));
				derived.EmitJump(end);
				derived.SelectBlock(dispatch);
				const InitializerListElementAddresses* addresses =
					InitializerListElementAddressPlan(backing);
				for (std::size_t built = i; built != 0; --built)
					derived.EmitDestructorCall(
						destructor, (*addresses)[built - 1]);
				derived.EmitExceptionResume();
				derived.SelectBlock(end);
			}
			return true;
		}

		const SlotId progress_id = static_cast<SlotId>(
			derived.function_->slots.size());
		Slot progress_slot;
		progress_slot.name = InternLocalName(derived.output_,
			derived.GeneratedSlotName("initlist_constructor_index"));
		progress_slot.type = LowI64();
		derived.function_->slots.push_back(progress_slot);
		const Operand progress(progress_id, LowI64());
		Instruction initialize(Instruction::STORE);
		initialize.type = LowI64();
		initialize.first = Operand(0, LowI64());
		initialize.second = progress;
		derived.Emit(initialize);
		const BlockId cleanup = destructor == kNoBinding ? BlockId(kNoLowId) :
			derived.AddBlock(derived.NewLabel("initlist_constructor_cleanup"));
		const BlockId continuation = destructor == kNoBinding ?
			BlockId(kNoLowId) :
			derived.AddBlock(derived.NewLabel("initlist_constructor_end"));
		for (std::size_t i = 0; i < count; ++i)
		{
			const Operand destination = i == 0 ? base : derived.IndexAddress(
				LowI8(), base, Operand(i * element_size, LowI64()), true);
			if (destructor != kNoBinding && i != 0)
				derived.EmitEhTarget(Instruction::EH_TRY, cleanup);
			const bool previous_suppression =
				derived.SetFullExpressionCleanupStartSuppressed(
					destructor != kNoBinding && i != 0);
			if (i < values.size())
				derived.LowerRuntimeObjectValue(array.child, values[i], destination);
			else derived.LowerRuntimeZeroValue(array.child, destination);
			(void)derived.SetFullExpressionCleanupStartSuppressed(
				previous_suppression);
			if (i == 0 && derived.full_expression_cleanup_active_ &&
				derived.full_expression_segment_actions_.empty())
				derived.PauseFullExpressionCleanupSegment();
			if (destructor != kNoBinding && i != 0)
				derived.Emit(Instruction(Instruction::EH_END));
			Instruction save(Instruction::STORE);
			save.type = LowI64();
			save.first = Operand(static_cast<std::int64_t>(i + 1), LowI64());
			save.second = progress;
			derived.Emit(save);
		}
		if (destructor == kNoBinding) return true;
		derived.EmitJump(continuation);
		derived.SelectBlock(cleanup);
		const BlockId cleanup_body = derived.AddBlock(
			derived.NewLabel("initlist_constructor_cleanup_body"));
		const BlockId resume = derived.AddBlock(
			derived.NewLabel("initlist_constructor_resume"));
		const Operand remaining = derived.LoadStorage(progress, LowI64());
		const Operand any = derived.Temp(LowI64());
		Instruction nonzero(Instruction::CMP);
		nonzero.dest = any.id;
		nonzero.op = LOW_OP_NE;
		nonzero.type = LowI64();
		nonzero.first = remaining;
		nonzero.second = Operand(0, LowI64());
		derived.Emit(nonzero);
		derived.EmitBranch(any, cleanup_body, resume);
		derived.SelectBlock(cleanup_body);
		const Operand previous = derived.Temp(LowI64());
		Instruction decrement(Instruction::BINARY);
		decrement.dest = previous.id;
		decrement.op = LOW_OP_SUB;
		decrement.type = LowI64();
		decrement.first = remaining;
		decrement.second = Operand(1, LowI64());
		derived.Emit(decrement);
		Instruction save_previous(Instruction::STORE);
		save_previous.type = LowI64();
		save_previous.first = previous;
		save_previous.second = progress;
		derived.Emit(save_previous);
		Operand displacement = previous;
		if (element_size != 1)
		{
			const Operand scaled = derived.Temp(LowI64());
			Instruction multiply(Instruction::BINARY);
			multiply.dest = scaled.id;
			multiply.op = LOW_OP_MUL;
			multiply.type = LowI64();
			multiply.first = previous;
			multiply.second = Operand(
				static_cast<std::int64_t>(element_size), LowI64());
			derived.Emit(multiply);
			displacement = scaled;
		}
		derived.EmitDestructorCall(destructor,
			derived.IndexAddress(LowI8(), base, displacement, true));
		derived.EmitJump(cleanup);
		derived.SelectBlock(resume);
		derived.EmitExceptionResume();
		derived.SelectBlock(continuation);
		return true;
	}

	bool LowerInitializerListTemporaryDestructor(const DumpNode& action)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (action.lifetime_object == kNoDumpEdge ||
			action.lifetime_object >= derived.arena_.nodes.size() ||
			!derived.arena_.nodes[action.lifetime_object].initializer_list_backing)
			return false;
		const InitializerListElementAddresses* addresses =
			InitializerListElementAddressPlan(action.lifetime_object);
		if (!addresses) return false;
		for (std::size_t i = addresses->size(); i != 0; --i)
			derived.EmitDestructorCall(action.binding, (*addresses)[i - 1]);
		return true;
	}

	void LowerNamespaceInitializerListBackingDestructor(
		const NamespaceObjectAction& action)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& destructor = derived.arena_.nodes[action.destructor];
		const SymbolId symbol = derived.initializer_lists_.backing_symbols[
			action.initializer_list_backing];
		if (symbol == kNoLowId)
			ThrowLoweringInternal(
				"namespace initializer-list backing has no global");
		const Operand storage(Operand::GLOBAL, symbol,
			derived.LowerStorageType(destructor.operand_type));
		const TypeRecord& array = derived.program_.types.Get(
			derived.ExpressionObjectType(destructor.operand_type));
		if (array.kind != TYPE_ARRAY || array.bound == 0)
			ThrowLoweringInternal(
				"namespace initializer-list backing is not an array");
		const std::size_t element_size = derived.program_.SizeOf(array.child);
		for (std::size_t i = static_cast<std::size_t>(array.bound);
			i != 0; --i)
		{
			const Operand base = derived.DecayAddress(
				derived.AddressOfStorage(storage));
			Operand displacement(
				static_cast<std::int64_t>(i - 1), LowI64());
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
			derived.EmitDestructorCall(destructor.binding,
				derived.IndexAddress(LowI8(), base, displacement, true));
		}
	}
	Operand LowerInitializerListValue(std::uint32_t node,
		const DumpNode& record, const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (record.kind == DUMP_INITIALIZER_LIST)
			return derived.LowerClassArgumentStaging(node, record.type);
		if (children.size() != 1)
			ThrowLoweringInternal("initializer-list projection has no object");
		const Operand base = derived.AddressOfStorage(
			derived.LowerStorage(children[0]));
		const Operand field = derived.Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = field.id;
		index.type = LowI8();
		index.first = base;
		index.second = Operand(
			record.kind == DUMP_INITIALIZER_LIST_BEGIN ? 0 : 8, LowI64());
		index.projection = INDEX_PROJECTION_FIELD;
		derived.Emit(index);
		return derived.LoadStorage(field,
			record.kind == DUMP_INITIALIZER_LIST_BEGIN ? LowPtr() : LowI64());
	}

	void LowerInitializerListObject(std::uint32_t node,
		const Operand& destination, bool protect_object = false)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[node];
		const NodeChildren children = derived.Children(node);
		if (record.kind != DUMP_INITIALIZER_LIST || children.size() > 1)
			ThrowLoweringInternal("invalid initializer-list object recipe");
		const Operand backing = children.empty() ?
			Operand::NullPointer(LowPtr()) : derived.LowerStorage(children[0]);
		const BlockId dispatch = protect_object ? derived.AddBlock(
			derived.NewLabel("call_unwind_dispatch")) : BlockId(kNoLowId);
		const BlockId end = protect_object ? derived.AddBlock(
			derived.NewLabel("call_unwind_end")) : BlockId(kNoLowId);
		if (protect_object)
			derived.EmitEhTarget(Instruction::EH_TRY, dispatch);
		Instruction store_begin(Instruction::STORE);
		store_begin.type = LowPtr();
		store_begin.first = backing;
		store_begin.second = destination;
		derived.Emit(store_begin);
		const Operand size_field = derived.IndexAddress(LowI8(), destination,
			Operand(8, LowI64()), false);
		Instruction store_size(Instruction::STORE);
		store_size.type = LowI64();
		store_size.first = Operand(
			static_cast<std::int64_t>(record.array_count), LowI64());
		store_size.second = size_field;
		derived.Emit(store_size);
		if (protect_object)
		{
			derived.initializer_lists_.lifetime_observation_dispatch = dispatch;
			derived.Emit(Instruction(Instruction::EH_END));
			derived.EmitJump(end);
			derived.SelectBlock(dispatch);
			derived.EmitExceptionResume();
			derived.SelectBlock(end);
		}
	}

	bool LowerInitializerListVariable(const DumpNode& record,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.IsClassObjectType(record.type) || children.size() != 1 ||
			derived.arena_.nodes[children[0]].kind != DUMP_INITIALIZER_LIST)
			return false;
		const NodeChildren list_children = derived.Children(children[0]);
		const bool protect = !derived.full_expression_cleanup_active_ &&
			list_children.size() == 1 &&
			derived.arena_.nodes[list_children[0]].selected_binding != kNoBinding;
		LowerInitializerListObject(children[0], derived.AddressOfStorage(
			derived.StorageFor(record.binding,
				derived.LowerStorageType(record.type))), protect);
		return true;
	}

	bool LowerInitializerListRuntimeValue(std::uint32_t node,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.arena_.nodes[node].kind != DUMP_INITIALIZER_LIST)
			return false;
		LowerInitializerListObject(node, destination);
		return true;
	}

	void LowerRuntimeZeroValue(TypeId type, const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const TypeRecord& record = derived.program_.types.Get(
			derived.ExpressionObjectType(type));
		if (record.kind == TYPE_ARRAY || derived.IsClassObjectType(type))
			ThrowLoweringSource(
				"omitted runtime aggregate element is outside the checkpoint");
		Instruction store(Instruction::STORE);
		store.type = derived.LowerExpressionType(type);
		store.first = store.type.kind == LOW_PTR ?
			Operand::NullPointer(store.type) : IsFloating(store.type) ?
			derived.FloatingOperand("0.0", store.type) : Operand(0, store.type);
		store.second = destination;
		derived.Emit(store);
	}
};

}
}

#endif
