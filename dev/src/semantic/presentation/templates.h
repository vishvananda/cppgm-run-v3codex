#pragma once

#include "semantic/model/program.h"

#include <cstddef>
#include <string>

namespace cppgm
{
namespace semantic
{
struct Stats;
namespace presentation
{

std::string RenderClassTemplateSpecializationName(
	const semantic::Program& program, semantic::NameId primary,
	const semantic::TemplateArgument* arguments, std::size_t argument_count,
	semantic::Stats* stats);

}
}
}
