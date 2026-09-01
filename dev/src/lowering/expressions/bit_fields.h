#pragma once

#include "semantic/model/graph.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/ir/model.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppgm
{
namespace lowering
{

template <class Derived>
class BitFieldValueLowering
{
protected:
	void IndexBitFieldStorageTransferOwner(std::uint32_t function)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const semantic::DumpNode& definition =
			derived.arena_.nodes[function];
		if (definition.kind !=
				semantic::DUMP_FUNCTION_DEFINITION ||
			definition.binding == semantic::kNoBinding)
			return;
		const semantic::BindingRecord& binding =
			derived.program_.bindings[definition.binding];
		if (binding.member_owner == semantic::kNoEntity ||
			derived.program_.names.Get(binding.name) != "operator=") return;
		std::vector<std::uint32_t> pending(1, function);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const semantic::DumpNode& record =
				derived.arena_.nodes[current];
			if (record.storage_unit_transfer)
			{
				if (binding.member_owner >=
					derived.bit_field_storage_transfer_owners_.size())
					ThrowLoweringInternal(
						"bit-field transfer owner is not indexed");
				derived.bit_field_storage_transfer_owners_[
					binding.member_owner] = 1;
				return;
			}
			const lowering::support::NodeChildren children =
				derived.Children(current);
			for (std::size_t i = children.size(); i != 0; --i)
				pending.push_back(children[i - 1]);
		}
	}

	lowering::ir::Operand ConstructorBitFieldStorage(
		semantic::BindingId binding)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const lowering::ir::Operand object = derived.LoadStorage(
			derived.StorageFor(derived.current_this_binding_,
				lowering::ir::LowPtr()), lowering::ir::LowPtr());
		return derived.ProjectAggregateMember(object, binding);
	}

	void LowerConstructorBitField(
		semantic::BindingId binding,
		const lowering::ir::Operand& value,
		const lowering::ir::LowType& type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const semantic::BindingRecord& field =
			derived.program_.bindings[binding];
		if (field.member_owner >=
				derived.bit_field_storage_transfer_owners_.size() ||
			!derived.bit_field_storage_transfer_owners_[field.member_owner])
		{
			const lowering::ir::Operand destination =
				ConstructorBitFieldStorage(binding);
			derived.InitializeBitField(binding, value, destination, type);
			return;
		}
		const bool preserve = derived.PreserveInitializedBitField(binding);
		if (!preserve)
		{
			const lowering::ir::Operand positioned =
				derived.PrepareBitFieldValue(binding, value, type);
			derived.EmitBitFieldStore(type, positioned,
				ConstructorBitFieldStorage(binding));
			return;
		}
		const lowering::ir::Operand cleared =
			derived.ClearBitFieldStorage(
				binding, ConstructorBitFieldStorage(binding), type);
		const lowering::ir::Operand positioned =
			derived.PrepareBitFieldValue(binding, value, type);
		const lowering::ir::Operand stored =
			derived.CombineBitFieldValue(cleared, positioned, type);
		derived.EmitBitFieldStore(type, stored,
			ConstructorBitFieldStorage(binding));
	}

	void ResetInitializedBitFieldUnit()
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.initialized_bit_field_unit_valid_ = false;
		derived.initialized_bit_field_owner_ = semantic::kNoEntity;
		derived.initialized_bit_field_offset_ = 0;
	}
};

}
}
