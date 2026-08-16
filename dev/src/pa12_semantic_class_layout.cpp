#include "pa12_semantic_detail.h"

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::ClassBasesAreEmpty(EntityId entity) const
{
	const EntityRecord& owner = program_->entities[entity];
	for (std::size_t base_index = 0;
		base_index < owner.direct_base_count; ++base_index)
		if (!program_->entities[
			program_->DirectBase(entity, base_index).entity].empty_class)
			return false;
	return true;
}

void SemanticAnalyzer::SetBindingRequestedAlignment(
	BindingRecord& binding, std::size_t alignment)
{
	if (alignment != 0)
		program_->MutableBindingLayout(binding).requested_alignment = alignment;
}

}
}
