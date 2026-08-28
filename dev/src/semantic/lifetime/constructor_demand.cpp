#include "semantic/analysis/analyzer.h"

namespace cppgm
{
namespace semantic
{

void Analyzer::DemandDefaultConstructor(EntityId entity)
{
	if (entity == kNoEntity) return;
	if (default_constructor_demand_states_.size() <= entity)
		default_constructor_demand_states_.resize(
			static_cast<std::size_t>(entity) + 1, 0);
	if (default_constructor_demand_states_[entity] != 0) return;
	default_constructor_demand_states_[entity] = 1;
	demanded_default_constructor_entities_.push_back(entity);
	++demand_worklist_pushes_;
}

void Analyzer::DemandConstructorDefinition(BindingId binding)
{
	DemandFunction(binding);
}

}
}
