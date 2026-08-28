#pragma once

#include "semantic/model/program.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace cppgm
{
namespace semantic
{
struct Stats;
namespace presentation
{

std::string RenderLambdaIdentityComponent(const semantic::Program& program,
	semantic::BindingId context, std::size_t token_first,
	std::size_t token_last, std::uint32_t ordinal,
	semantic::Stats* stats = 0);

std::string RenderLambdaEntityTerminal(const semantic::Program& program,
	semantic::EntityId entity, semantic::Stats* stats = 0);
std::string RenderLambdaEntityEmissionName(const semantic::Program& program,
	semantic::EntityId entity, std::size_t* components = 0,
	semantic::Stats* stats = 0);
std::string RenderLambdaMemberTerminal(const semantic::Program& program,
	semantic::EntityId entity, semantic::NameId terminal,
	semantic::Stats* stats = 0);
std::string RenderLambdaInvocationEmissionName(const semantic::Program& program,
	semantic::EntityId entity, semantic::ScopeId owner,
	std::size_t* components = 0, semantic::Stats* stats = 0);
std::string RenderLambdaSourceIdentityName(const semantic::Program& program,
	semantic::EntityId entity, std::size_t* components = 0,
	semantic::Stats* stats = 0);

}
}
}
