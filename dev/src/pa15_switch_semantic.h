#ifndef CPPGM_PA15_SWITCH_SEMANTIC_H
#define CPPGM_PA15_SWITCH_SEMANTIC_H

#include "pa11_model.h"

#include <cstdint>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

using namespace pa11;

void RegisterSwitchEntryDeclaration(const Program& program, ScopeId scope,
	bool local, bool declaration_only, BindingId object, TypeId type,
	bool has_initializer, std::vector<std::uint32_t>* barriers);
void ValidateSwitchLabelEntry(ScopeId scope,
	const std::vector<ScopeId>& scope_parents,
	const std::vector<std::uint32_t>& barriers,
	const std::vector<ScopeId>& switch_boundaries);

}
}

#endif
