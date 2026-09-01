#pragma once

#include "semantic/model/program.h"
#include "semantic/model/graph.h"
#include "lowering/api.h"
#include "lowering/ir/model.h"
#include "lowering/support/errors.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppgm
{
namespace lowering
{

struct VtableThunkLoweringFact
{
	lowering::ir::SymbolId symbol;
	lowering::ir::SymbolId target;
	semantic::BindingId function;
	std::int64_t this_adjustment;
	std::int64_t return_adjustment;
	std::int64_t return_vtable_offset;
	std::int64_t return_runtime_vtable_offset;
	bool return_adjustment_virtual;

	VtableThunkLoweringFact(lowering::ir::SymbolId symbol_value,
		lowering::ir::SymbolId target_value, semantic::BindingId function_value,
		std::int64_t this_adjustment_value,
		std::int64_t return_adjustment_value,
		std::int64_t return_vtable_offset_value,
		std::int64_t return_runtime_vtable_offset_value,
		bool return_adjustment_virtual_value)
		: symbol(symbol_value), target(target_value), function(function_value),
		  this_adjustment(this_adjustment_value),
		  return_adjustment(return_adjustment_value),
		  return_vtable_offset(return_vtable_offset_value),
		  return_runtime_vtable_offset(return_runtime_vtable_offset_value),
		  return_adjustment_virtual(return_adjustment_virtual_value) {}
};

struct PolymorphismLoweringState
{
	std::vector<lowering::ir::SymbolId> class_vtable_symbols;
	std::vector<std::uint8_t> class_vtable_external;
	std::vector<lowering::ir::SymbolId> class_vtt_symbols;
	std::vector<std::uint64_t> class_vtable_address_points;
	std::vector<std::vector<lowering::ir::SymbolId> >
		class_view_vtable_symbols;
	std::vector<std::vector<std::uint64_t> > class_view_address_points;
	std::vector<std::vector<std::vector<lowering::ir::SymbolId> > >
		class_construction_vtable_symbols;
	std::vector<std::vector<std::uint64_t> >
		class_construction_vtt_offsets;
	std::vector<std::vector<std::vector<lowering::ir::SymbolId> > >
		class_view_slot_symbols;
	std::vector<std::vector<std::vector<lowering::ir::SymbolId> > >
		class_view_deleting_slot_symbols;
	std::vector<std::vector<semantic::VirtualSlotFact> >
		class_host_primary_slots;
	std::vector<std::uint32_t> host_primary_slot_by_binding;
	std::vector<VtableThunkLoweringFact> vtable_thunks;
	std::vector<lowering::ir::SymbolId> class_rtti_symbols;
	std::vector<lowering::ir::SymbolId> class_type_name_symbols;
	std::vector<std::uint8_t> class_rtti_demanded;
	std::vector<lowering::ir::SymbolId> type_rtti_symbols;
	std::vector<lowering::ir::SymbolId> type_name_symbols;
	std::vector<std::uint8_t> type_rtti_demanded;
	std::vector<std::uint8_t> exception_type_demanded;
	std::vector<std::uint8_t> thrown_type_demanded;
	std::vector<lowering::ir::SymbolId> exception_rtti_symbols;
	std::vector<lowering::ir::SymbolId> exception_object_symbols;
	std::vector<lowering::ir::SymbolId> deleting_destructor_symbols;
	std::vector<std::uint8_t> deleting_destructor_external;
	std::vector<semantic::BindingId> deallocation_bindings;
	std::vector<semantic::BindingId> complete_destructor_bindings;
	std::vector<semantic::BindingId> base_destructor_bindings;
	std::vector<std::uint8_t> deleting_destructor_calls_complete;
	lowering::ir::SymbolId pure_virtual_symbol;
	lowering::ir::SymbolId rtti_class_symbol;
	lowering::ir::SymbolId rtti_si_symbol;
	lowering::ir::SymbolId rtti_vmi_symbol;
	lowering::ir::SymbolId rtti_fundamental_symbol;
	lowering::ir::SymbolId rtti_pointer_symbol;
	lowering::ir::SymbolId rtti_enum_symbol;
	lowering::ir::SymbolId rtti_array_symbol;
	lowering::ir::SymbolId rtti_function_symbol;
	lowering::ir::SymbolId rtti_member_pointer_symbol;
	lowering::ir::SymbolId dynamic_cast_symbol;
	lowering::ir::SymbolId bad_cast_symbol;
	lowering::ir::SymbolId bad_typeid_symbol;
	lowering::ir::SymbolId eh_resume_symbol;
	lowering::ir::SymbolId eh_allocate_exception_symbol;
	lowering::ir::SymbolId eh_begin_catch_symbol;
	lowering::ir::SymbolId eh_end_catch_symbol;
	lowering::ir::SymbolId eh_rethrow_symbol;
	lowering::ir::SymbolId eh_throw_symbol;
	lowering::ir::SymbolId eh_personality_symbol;
	bool need_dynamic_cast;
	bool need_bad_cast;
	bool need_bad_typeid;
	bool need_exceptions;
	bool need_throw;
	bool need_exception_handlers;
	bool need_rethrow;
	std::size_t source_function_first;

	PolymorphismLoweringState();
};

bool IsFunctionLocalEntity(const semantic::Program& program,
	semantic::EntityId entity);
bool PreferLocalObjectBinding(const semantic::Program& program,
	semantic::EntityId entity);

void PreparePolymorphism(
	const semantic::SemanticGraphView& graph,
	lowering::ir::Program& output, lowering::Stats* stats,
	std::size_t source_ordinal,
	const std::vector<lowering::ir::SymbolId>& function_symbols,
	PolymorphismLoweringState* state);

void EmitDeletingDestructors(
	const semantic::SemanticGraphView& graph,
	lowering::ir::Program& output, lowering::Stats* stats,
	const std::vector<lowering::ir::SymbolId>& function_symbols,
	PolymorphismLoweringState* state);

void EmitVtableThunks(
	const semantic::SemanticGraphView& graph,
	lowering::ir::Program& output, lowering::Stats* stats,
	const std::vector<lowering::ir::SymbolId>& function_symbols,
	PolymorphismLoweringState* state);

std::uint32_t ResolveHostVirtualSlot(const semantic::Program& program,
	bool host_object_emission, const PolymorphismLoweringState& state,
	const semantic::DumpNode& record,
	semantic::EntityId object_entity);

template <class Derived>
class PolymorphismActionLowering
{
protected:
	lowering::ir::Operand LowerVirtualCallee(
		const semantic::DumpNode& record,
		const lowering::ir::Operand& object, std::uint32_t virtual_slot)
	{
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.stats_) ++derived.stats_->virtual_calls;
		const Operand table = derived.LoadStorage(object, LowPtr());
		Operand slot = table;
		if (virtual_slot != 0)
			slot = derived.IndexAddress(LowI8(), table, Operand(
				static_cast<std::int64_t>(virtual_slot) * 8,
				LowI64()), false);
		return derived.LoadStorage(slot, LowPtr());
	}

	void LowerVptrInitializationAction(
		const semantic::DumpNode& action)
	{
		using namespace semantic;
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.stats_) ++derived.stats_->vptr_stores;
		const EntityId entity = derived.ClassEntity(action.type);
		if (entity == kNoEntity ||
			entity >= derived.polymorphism_.class_vtable_symbols.size() ||
			derived.polymorphism_.class_vtable_symbols[entity] == kNoLowId ||
			derived.current_this_binding_ == kNoBinding)
			ThrowLoweringInternal("vptr action has no class or vtable");
		const Operand object = derived.LoadStorage(derived.StorageFor(
			derived.current_this_binding_, LowPtr()), LowPtr());
		Operand address_point;
		if (derived.HasCurrentConstructionVtt())
			address_point = derived.LoadStorage(
				derived.CurrentConstructionVtt(), LowPtr());
		else
		{
			const SymbolId symbol =
				derived.polymorphism_.class_vtable_symbols[entity];
			derived.output_.symbols[symbol].referenced = true;
			const Operand table = derived.Temp(LowPtr());
			Instruction address(Instruction::ADDR);
			address.dest = table.id;
			address.first = Operand(Operand::GLOBAL, symbol, LowPtr());
			derived.Emit(address);
			const std::uint64_t primary_address_point = entity <
				derived.polymorphism_.class_vtable_address_points.size() ?
				derived.polymorphism_.class_vtable_address_points[entity] : 16;
			address_point = derived.Temp(LowPtr());
			Instruction index(Instruction::INDEX);
			index.dest = address_point.id;
			index.type = LowI8();
			index.first = table;
			index.second = Operand(static_cast<std::int64_t>(
				primary_address_point), LowI64());
			derived.Emit(index);
		}
		Instruction store(Instruction::STORE);
		store.type = LowPtr();
		store.first = address_point;
		store.second = object;
		derived.Emit(store);
		const semantic::ClassPolymorphismFacts& facts =
			derived.graph_.class_polymorphism[entity];
		std::size_t physical_view = 1;
		if (derived.HasCurrentConstructionVtt() &&
			entity < derived.polymorphism_.class_construction_vtable_symbols.size())
			for (std::size_t base = 0; base < derived.polymorphism_.
				class_construction_vtable_symbols[entity].size(); ++base)
				physical_view += derived.polymorphism_.
					class_construction_vtable_symbols[entity][base].size();
		for (std::size_t view = 0; view < facts.views.size(); ++view)
		{
			if (!facts.views[view].stores_vptr ||
				entity >= derived.polymorphism_.class_view_vtable_symbols.size() ||
				view >= derived.polymorphism_.class_view_vtable_symbols[entity].size())
				continue;
			const SymbolId view_symbol =
				derived.polymorphism_.class_view_vtable_symbols[entity][view];
			if (view_symbol == kNoLowId) continue;
			if (derived.stats_) ++derived.stats_->vptr_stores;
			Operand inherited;
			Operand subobject;
			if (facts.views[view].virtual_base &&
				derived.CurrentVirtualBaseAddress(
					derived.current_this_binding_, facts.views[view].entity,
					&inherited))
				subobject = ProjectBaseSubobjectOffset(inherited, 0);
			else
			{
				const Operand view_object = derived.LoadStorage(derived.StorageFor(
					derived.current_this_binding_, LowPtr()), LowPtr());
				subobject = facts.views[view].virtual_base ?
					ProjectBaseSubobjectOffset(derived.RuntimeVirtualBaseAddress(
						view_object, entity,
						facts.views[view].virtual_base_ordinal), 0) :
					ProjectBaseSubobjectOffset(
						view_object, facts.views[view].offset);
			}
			Operand view_address_point;
			if (derived.HasCurrentConstructionVtt())
			{
				const Operand entry = derived.IndexAddress(LowI8(),
					derived.CurrentConstructionVtt(),
					Operand(static_cast<std::int64_t>(physical_view * 8),
						LowI64()), false);
				view_address_point = derived.LoadStorage(entry, LowPtr());
			}
			else
			{
				derived.output_.symbols[view_symbol].referenced = true;
				const Operand view_table = derived.Temp(LowPtr());
				Instruction view_address(Instruction::ADDR);
				view_address.dest = view_table.id;
				view_address.first = Operand(
					Operand::GLOBAL, view_symbol, LowPtr());
				derived.Emit(view_address);
				view_address_point = derived.Temp(LowPtr());
				Instruction view_index(Instruction::INDEX);
				view_index.dest = view_address_point.id;
				view_index.type = LowI8();
				view_index.first = view_table;
				const std::uint64_t view_point = entity <
					derived.polymorphism_.class_view_address_points.size() &&
					view < derived.polymorphism_.class_view_address_points[entity].size() ?
					derived.polymorphism_.class_view_address_points[entity][view] :
					facts.views[view].address_point;
				view_index.second = Operand(static_cast<std::int64_t>(
					view_point), LowI64());
				derived.Emit(view_index);
			}
			Instruction view_store(Instruction::STORE);
			view_store.type = LowPtr();
			view_store.first = view_address_point;
			view_store.second = subobject;
			derived.Emit(view_store);
			++physical_view;
		}
	}

	lowering::ir::Operand ProjectBaseSubobject(
		const lowering::ir::Operand& object, semantic::EntityId entity)
	{
		Derived& derived = static_cast<Derived&>(*this);
		return ProjectBaseSubobjectOffset(object, entity == semantic::kNoEntity ? 0 :
			derived.program_.entities[entity].direct_base_offset);
	}

	lowering::ir::Operand ProjectBaseSubobjectOffset(
		const lowering::ir::Operand& object, std::uint64_t offset)
	{
		return ProjectBaseSubobjectAdjustment(object,
			static_cast<std::int64_t>(offset));
	}

	lowering::ir::Operand ProjectBaseSubobjectAdjustment(
		const lowering::ir::Operand& object, std::int64_t offset)
	{
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		const Operand projected = derived.Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = projected.id;
		index.type = LowI8();
		index.first = object;
		index.second = Operand(offset, LowI64());
		derived.Emit(index);
		return projected;
	}
};

}
}
