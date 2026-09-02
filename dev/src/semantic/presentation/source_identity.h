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
std::string RenderName(const semantic::Program& program,
	semantic::ScopeId owner, semantic::NameId name,
	bool show_anonymous_namespace = false);
std::string RenderTemplateArgument(const semantic::Program& program,
	const semantic::TemplateArgument& argument);
std::string RenderEntity(const semantic::Program& program,
	semantic::EntityId entity, bool show_anonymous_namespace = false);
std::string RenderFunction(const semantic::Program& program,
	semantic::BindingId binding, semantic::TypeId type,
	const std::vector<TemplateBinding>& substitutions);

} }
}
