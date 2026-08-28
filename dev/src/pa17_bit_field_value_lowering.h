#pragma once

#include "semantic/model/graph.h"
#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa17_lowering_detail
{

template <class Derived>
class BitFieldValueLowering
{
protected:
	void IndexBitFieldStorageTransferOwner(std::uint32_t function)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const pa12_semantic_detail::DumpNode& definition =
			derived.arena_.nodes[function];
		if (definition.kind !=
				pa12_semantic_detail::DUMP_FUNCTION_DEFINITION ||
			definition.binding == pa12_semantic_detail::kNoBinding)
			return;
		const pa12_semantic_detail::BindingRecord& binding =
			derived.program_.bindings[definition.binding];
		if (binding.member_owner == pa11::kNoEntity ||
			derived.program_.names.Get(binding.name) != "operator=") return;
		std::vector<std::uint32_t> pending(1, function);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const pa12_semantic_detail::DumpNode& record =
				derived.arena_.nodes[current];
			if (record.storage_unit_transfer)
			{
				if (binding.member_owner >=
					derived.bit_field_storage_transfer_owners_.size())
					throw std::logic_error(
						"bit-field transfer owner is not indexed");
				derived.bit_field_storage_transfer_owners_[
					binding.member_owner] = 1;
				return;
			}
			const pa15_lowering_support::NodeChildren children =
				derived.Children(current);
			for (std::size_t i = children.size(); i != 0; --i)
				pending.push_back(children[i - 1]);
		}
	}

	pa15_lowir_detail::Operand ConstructorBitFieldStorage(
		pa12_semantic_detail::BindingId binding)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const pa15_lowir_detail::Operand object = derived.LoadStorage(
			derived.StorageFor(derived.current_this_binding_,
				pa15_lowir_detail::LowPtr()), pa15_lowir_detail::LowPtr());
		return derived.ProjectAggregateMember(object, binding);
	}

	void LowerConstructorBitField(
		pa12_semantic_detail::BindingId binding,
		const pa15_lowir_detail::Operand& value,
		const pa15_lowir_detail::LowType& type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const pa12_semantic_detail::BindingRecord& field =
			derived.program_.bindings[binding];
		if (field.member_owner >=
				derived.bit_field_storage_transfer_owners_.size() ||
			!derived.bit_field_storage_transfer_owners_[field.member_owner])
		{
			const pa15_lowir_detail::Operand destination =
				ConstructorBitFieldStorage(binding);
			derived.InitializeBitField(binding, value, destination, type);
			return;
		}
		const bool preserve = derived.PreserveInitializedBitField(binding);
		if (!preserve)
		{
			const pa15_lowir_detail::Operand positioned =
				derived.PrepareBitFieldValue(binding, value, type);
			derived.EmitBitFieldStore(type, positioned,
				ConstructorBitFieldStorage(binding));
			return;
		}
		const pa15_lowir_detail::Operand cleared =
			derived.ClearBitFieldStorage(
				binding, ConstructorBitFieldStorage(binding), type);
		const pa15_lowir_detail::Operand positioned =
			derived.PrepareBitFieldValue(binding, value, type);
		const pa15_lowir_detail::Operand stored =
			derived.CombineBitFieldValue(cleared, positioned, type);
		derived.EmitBitFieldStore(type, stored,
			ConstructorBitFieldStorage(binding));
	}
};

}
}
