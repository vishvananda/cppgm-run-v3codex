#ifndef CPPGM_PA15_STATIC_MEMBER_SYMBOL_LOWERING_H
#define CPPGM_PA15_STATIC_MEMBER_SYMBOL_LOWERING_H

#include "lowering/abi/itanium.h"
#include "lowering/support/utilities.h"

#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa15_lowering_detail
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;
using namespace pa15_lowering_support;

template <class Derived>
class StaticMemberSymbolLowering
{
protected:
	SymbolId RegisterGlobalVariable(const DumpNode& record,
		const std::string& presentation_name = std::string())
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (record.binding == kNoBinding ||
			record.binding >= derived.program_.bindings.size())
			throw std::logic_error("invalid PA15 global variable binding");
		const BindingId canonical =
			derived.program_.bindings[record.binding].canonical;
		if (derived.global_symbols_[canonical] == kNoLowId)
		{
			const BindingRecord& binding =
				derived.program_.bindings[record.binding];
			const std::string source_name = presentation_name.empty() ?
				RenderBindingPresentation(derived.program_, binding,
					derived.stats_ ? &derived.stats_->semantic : 0) :
				presentation_name;
			derived.global_symbols_[canonical] = derived.InternSymbol(record,
				Symbol::GLOBAL_SYMBOL, SanitizeSymbol(source_name),
				pa15_lowering_abi::MangleVariable(
					derived.program_, record,
					derived.stats_ ? &derived.stats_->abi : 0,
					&derived.abi_context_));
		}
		derived.global_symbols_[record.binding] =
			derived.global_symbols_[canonical];
		derived.output_.symbols[derived.global_symbols_[canonical]].
			thread_local_storage =
			derived.program_.bindings[record.binding].thread_local_storage;
		return derived.global_symbols_[canonical];
	}

	std::string StaticDataMemberEmissionName(
		BindingId binding, NameId expression_name) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const BindingRecord& member = derived.program_.bindings[binding];
		(void)expression_name;
		return RenderBindingPresentation(derived.program_, member,
			derived.stats_ ? &derived.stats_->semantic : 0);
	}

	void RegisterAddressableStaticDataMember(
		BindingId binding, NameId expression_name)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.program_.IsStaticDataMember(binding)) return;
		const BindingId canonical =
			derived.program_.bindings[binding].canonical;
		if (derived.global_symbols_[canonical] != kNoLowId)
		{
			derived.global_symbols_[binding] =
				derived.global_symbols_[canonical];
			return;
		}
		DumpNode declaration(DUMP_VARIABLE);
		declaration.binding = binding;
		declaration.type = derived.program_.bindings[binding].type;
		declaration.text = derived.program_.bindings[binding].name;
		(void)RegisterGlobalVariable(declaration,
			StaticDataMemberEmissionName(binding, expression_name));
	}
};

}
}

#endif
