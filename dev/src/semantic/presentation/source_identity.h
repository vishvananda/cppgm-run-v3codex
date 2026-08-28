#pragma once

#include "semantic/model/program.h"

#include <string>
#include <vector>

namespace cppgm
{
namespace semantic { namespace presentation
{

struct TemplateBinding
{
	semantic::NameId name;
	semantic::TemplateArgument argument;

	TemplateBinding(semantic::NameId name_value,
		const semantic::TemplateArgument& argument_value)
		: name(name_value), argument(argument_value) {}
};

std::string RenderType(const semantic::Program& program, semantic::TypeId type);
std::string RenderEntity(const semantic::Program& program, semantic::EntityId entity);
std::string RenderFunction(const semantic::Program& program,
	semantic::BindingId binding, semantic::TypeId type,
	const std::vector<TemplateBinding>& substitutions);

} }
}
