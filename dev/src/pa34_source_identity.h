#pragma once

#include "pa11_model.h"

#include <string>
#include <vector>

namespace cppgm
{
namespace pa34_source_identity
{

struct TemplateBinding
{
	pa11::NameId name;
	pa11::TemplateArgument argument;

	TemplateBinding(pa11::NameId name_value,
		const pa11::TemplateArgument& argument_value)
		: name(name_value), argument(argument_value) {}
};

std::string RenderType(const pa11::Program& program, pa11::TypeId type);
std::string RenderEntity(const pa11::Program& program, pa11::EntityId entity);
std::string RenderFunction(const pa11::Program& program,
	pa11::BindingId binding, pa11::TypeId type,
	const std::vector<TemplateBinding>& substitutions);

}
}
