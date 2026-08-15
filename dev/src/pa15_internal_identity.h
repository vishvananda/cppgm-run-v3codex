#pragma once

#include "pa11_model.h"

#include <cstdint>
#include <vector>

namespace cppgm
{
namespace pa15_lowering_detail
{

class InternalIdentityClassifier
{
public:
	explicit InternalIdentityClassifier(const pa11::Program& program);
	bool BindingHasInternalIdentity(pa11::BindingId binding);

private:
	bool BindingDeclaresInternalIdentity(pa11::BindingId binding) const;
	bool ScopeHasInternalIdentity(pa11::ScopeId scope) const;
	bool TemplateArgumentsHaveInternalIdentity(
		std::uint32_t begin, std::uint32_t count);
	bool EntityHasInternalIdentity(pa11::EntityId entity);
	bool TypeHasInternalIdentity(pa11::TypeId type);

	const pa11::Program& program_;
	std::vector<std::uint8_t> type_states_;
	std::vector<std::uint8_t> entity_states_;
};

}
}
