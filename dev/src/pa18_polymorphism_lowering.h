#pragma once

#include "pa11_model.h"
#include "pa12_semantic_model.h"
#include "pa15_lowering.h"
#include "pa15_lowir_model.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa18_lowering_detail
{

struct PolymorphismLoweringState
{
	std::vector<pa15_lowir_detail::SymbolId> class_vtable_symbols;
	std::vector<pa15_lowir_detail::SymbolId> class_rtti_symbols;
	std::vector<pa15_lowir_detail::SymbolId> class_type_name_symbols;
	std::vector<std::uint8_t> class_rtti_demanded;
	std::vector<pa15_lowir_detail::SymbolId> type_rtti_symbols;
	std::vector<pa15_lowir_detail::SymbolId> type_name_symbols;
	std::vector<std::uint8_t> type_rtti_demanded;
	std::vector<std::uint8_t> exception_type_demanded;
	std::vector<std::uint8_t> thrown_type_demanded;
	std::vector<pa15_lowir_detail::SymbolId> exception_rtti_symbols;
	std::vector<pa15_lowir_detail::SymbolId> exception_object_symbols;
	std::vector<pa15_lowir_detail::SymbolId> deleting_destructor_symbols;
	std::vector<pa11::BindingId> deallocation_bindings;
	std::vector<pa11::BindingId> complete_destructor_bindings;
	std::vector<pa11::BindingId> base_destructor_bindings;
	std::vector<std::uint8_t> deleting_destructor_calls_complete;
	pa15_lowir_detail::SymbolId pure_virtual_symbol;
	pa15_lowir_detail::SymbolId rtti_class_symbol;
	pa15_lowir_detail::SymbolId rtti_si_symbol;
	pa15_lowir_detail::SymbolId rtti_vmi_symbol;
	pa15_lowir_detail::SymbolId rtti_fundamental_symbol;
	pa15_lowir_detail::SymbolId rtti_pointer_symbol;
	pa15_lowir_detail::SymbolId rtti_enum_symbol;
	pa15_lowir_detail::SymbolId rtti_array_symbol;
	pa15_lowir_detail::SymbolId rtti_function_symbol;
	pa15_lowir_detail::SymbolId rtti_member_pointer_symbol;
	pa15_lowir_detail::SymbolId dynamic_cast_symbol;
	pa15_lowir_detail::SymbolId bad_cast_symbol;
	pa15_lowir_detail::SymbolId bad_typeid_symbol;
	pa15_lowir_detail::SymbolId eh_resume_symbol;
	pa15_lowir_detail::SymbolId eh_allocate_exception_symbol;
	pa15_lowir_detail::SymbolId eh_begin_catch_symbol;
	pa15_lowir_detail::SymbolId eh_end_catch_symbol;
	pa15_lowir_detail::SymbolId eh_rethrow_symbol;
	pa15_lowir_detail::SymbolId eh_throw_symbol;
	pa15_lowir_detail::SymbolId eh_personality_symbol;
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

bool IsFunctionLocalEntity(const pa11::Program& program,
	pa11::EntityId entity);

void PreparePolymorphism(
	const pa12_semantic_detail::SemanticGraphView& graph,
	pa15_lowir_detail::TypedProgram& output, LowIRLoweringStats* stats,
	std::size_t source_ordinal,
	const std::vector<pa15_lowir_detail::SymbolId>& function_symbols,
	PolymorphismLoweringState* state);

void EmitDeletingDestructors(
	const pa12_semantic_detail::SemanticGraphView& graph,
	pa15_lowir_detail::TypedProgram& output, LowIRLoweringStats* stats,
	const std::vector<pa15_lowir_detail::SymbolId>& function_symbols,
	PolymorphismLoweringState* state);

template <class Derived>
class PolymorphismActionLowering
{
protected:
	pa15_lowir_detail::Operand LowerVirtualCallee(
		const pa12_semantic_detail::DumpNode& record,
		const pa15_lowir_detail::Operand& object)
	{
		using namespace pa15_lowir_detail;
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.stats_) ++derived.stats_->virtual_calls;
		const Operand table = derived.LoadStorage(object, LowPtr());
		Operand slot = table;
		if (record.virtual_slot != 0)
			slot = derived.IndexAddress(LowI8(), table, Operand(
				static_cast<std::int64_t>(record.virtual_slot) * 8,
				LowI64()), false);
		return derived.LoadStorage(slot, LowPtr());
	}

	void LowerVptrInitializationAction(
		const pa12_semantic_detail::DumpNode& action)
	{
		using namespace pa11;
		using namespace pa15_lowir_detail;
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.stats_) ++derived.stats_->vptr_stores;
		const EntityId entity = derived.ClassEntity(action.type);
		if (entity == kNoEntity ||
			entity >= derived.polymorphism_.class_vtable_symbols.size() ||
			derived.polymorphism_.class_vtable_symbols[entity] == kNoLowId ||
			derived.current_this_binding_ == kNoBinding)
			throw std::logic_error("vptr action has no class or vtable");
		const Operand object = derived.LoadStorage(derived.StorageFor(
			derived.current_this_binding_, LowPtr()), LowPtr());
		const SymbolId symbol =
			derived.polymorphism_.class_vtable_symbols[entity];
		derived.output_.symbols[symbol].referenced = true;
		const Operand table = derived.Temp(LowPtr());
		Instruction address(Instruction::ADDR);
		address.dest = table.id;
		address.first = Operand(Operand::GLOBAL, symbol, LowPtr());
		derived.Emit(address);
		const Operand address_point = derived.Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = address_point.id;
		index.type = LowI8();
		index.first = table;
		index.second = Operand(16, LowI64());
		derived.Emit(index);
		Instruction store(Instruction::STORE);
		store.type = LowPtr();
		store.first = address_point;
		store.second = object;
		derived.Emit(store);
	}

	pa15_lowir_detail::Operand ProjectBaseSubobject(
		const pa15_lowir_detail::Operand& object, pa11::EntityId entity)
	{
		Derived& derived = static_cast<Derived&>(*this);
		return ProjectBaseSubobjectOffset(object, entity == pa11::kNoEntity ? 0 :
			derived.program_.entities[entity].direct_base_offset);
	}

	pa15_lowir_detail::Operand ProjectBaseSubobjectOffset(
		const pa15_lowir_detail::Operand& object, std::uint64_t offset)
	{
		using namespace pa15_lowir_detail;
		Derived& derived = static_cast<Derived&>(*this);
		const Operand projected = derived.Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = projected.id;
		index.type = LowI8();
		index.first = object;
		index.second = Operand(static_cast<std::int64_t>(offset), LowI64());
		index.projection = INDEX_PROJECTION_BASE_SUBOBJECT;
		derived.Emit(index);
		return projected;
	}
};

}
}
