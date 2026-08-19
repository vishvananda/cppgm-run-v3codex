#pragma once

#include "pa11_model.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace cppgm
{
struct SemanticAnalysisStats;
namespace pa22_lambda_presentation
{

std::string RenderLambdaIdentityComponent(const pa11::Program& program,
	pa11::BindingId context, std::size_t token_first,
	std::size_t token_last, std::uint32_t ordinal,
	SemanticAnalysisStats* stats = 0);

}
}
