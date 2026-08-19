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

std::string RenderLambdaEntityTerminal(const pa11::Program& program,
	pa11::EntityId entity, SemanticAnalysisStats* stats = 0);
std::string RenderLambdaEntityEmissionName(const pa11::Program& program,
	pa11::EntityId entity, std::size_t* components = 0,
	SemanticAnalysisStats* stats = 0);
std::string RenderLambdaMemberTerminal(const pa11::Program& program,
	pa11::EntityId entity, pa11::NameId terminal,
	SemanticAnalysisStats* stats = 0);
std::string RenderLambdaInvocationEmissionName(const pa11::Program& program,
	pa11::EntityId entity, pa11::ScopeId owner,
	std::size_t* components = 0, SemanticAnalysisStats* stats = 0);
std::string RenderLambdaSourceIdentityName(const pa11::Program& program,
	pa11::EntityId entity, std::size_t* components = 0,
	SemanticAnalysisStats* stats = 0);

}
}
