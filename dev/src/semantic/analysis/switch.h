#ifndef CPPGM_SEMANTIC_ANALYSIS_SWITCH_H
#define CPPGM_SEMANTIC_ANALYSIS_SWITCH_H

#include "semantic/model/program.h"

#include <cstdint>
#include <vector>

namespace cppgm
{
namespace semantic
{


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
