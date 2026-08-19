#ifndef CPPGM_PA15_STATIC_MEMBER_SYMBOL_LOWERING_H
#define CPPGM_PA15_STATIC_MEMBER_SYMBOL_LOWERING_H

#include "pa15_lowering_abi.h"
#include "pa15_lowering_support.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa15_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

template <class Derived>
class StaticMemberSymbolLowering
{
protected:
	SymbolId RegisterGlobalVariable(const DumpNode& record,
		const std::string& qualified_name = std::string())
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
			const std::string source_name = qualified_name.empty() ?
				derived.program_.names.Get(binding.qualified_name != 0 ?
					binding.qualified_name : record.text) : qualified_name;
			derived.global_symbols_[canonical] = derived.InternSymbol(record,
				Symbol::GLOBAL_SYMBOL, SanitizeSymbol(source_name),
				pa15_lowering_abi::MangleVariable(
					derived.program_, record, qualified_name,
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
		if (member.qualified_name != 0)
			return derived.program_.names.Get(member.qualified_name);
		if (member.member_owner != kNoEntity)
		{
			const EntityRecord& owner =
				derived.program_.entities[member.member_owner];
			if (owner.template_argument_begin != kNoBinding &&
				expression_name != 0)
				return derived.program_.names.Get(expression_name);
			std::vector<NameId> path;
			derived.program_.BuildEmissionPath(
				owner.owner, owner.identity_name, &path);
			path.push_back(member.name);
			std::string result;
			for (std::size_t i = 0; i < path.size(); ++i)
			{
				if (i != 0) result += "::";
				result += derived.program_.names.Get(path[i]);
			}
			return result;
		}
		return expression_name != 0 ?
			derived.program_.names.Get(expression_name) :
			derived.program_.names.Get(member.name);
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
