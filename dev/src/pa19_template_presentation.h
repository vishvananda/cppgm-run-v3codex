#pragma once

#include "pa11_model.h"

#include <cstddef>
#include <string>

namespace cppgm
{
struct SemanticAnalysisStats;
namespace pa19_template_presentation
{

std::string RenderClassTemplateSpecializationName(
	const pa11::Program& program, pa11::NameId primary,
	const pa11::TemplateArgument* arguments, std::size_t argument_count,
	SemanticAnalysisStats* stats);

}
}
