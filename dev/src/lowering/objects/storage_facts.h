#pragma once

#include "semantic/model/program.h"

#include <string>

namespace cppgm
{
namespace semantic
{
struct DumpNode;
}
namespace lowering
{

bool NeedsAggregateStorageAddress(bool namespace_object, bool has_leaf,
	const semantic::BindingRecord& binding);
semantic::EntityId LambdaClosureEntity(
	const semantic::Program& program, semantic::TypeId type);
bool IsLambdaCaptureMember(
	const semantic::Program& program, semantic::BindingId binding);
std::string MissingStorageBindingDetail(
	const semantic::Program& program, semantic::BindingId binding);

}  // namespace lowering
}  // namespace cppgm
