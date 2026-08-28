#pragma once

#include "semantic/model/graph.h"

namespace cppgm
{
namespace lowering
{
namespace zero_initialization
{

bool ContiguousSpanEligible(const semantic::Program& program, semantic::TypeId type);

}  // namespace zero_initialization
}  // namespace lowering
}  // namespace cppgm
