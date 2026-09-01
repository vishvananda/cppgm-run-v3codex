#pragma once

#include "abi/itanium/abi_mangle.h"
#include "lowering/ir/types.h"
#include "semantic/model/graph.h"
#include "semantic/model/program.h"

namespace cppgm
{
namespace lowering
{
namespace ir
{
struct Program;
}
namespace abi
{

bool IsCompleteBoundaryObject(const semantic::Program& program,
	semantic::TypeId type);
bool IsFunctionEmissionDemanded(const semantic::Program& program,
	const semantic::DumpNode& node, bool host_object_emission = false);
bool IsFunctionDeclarationBoundaryComplete(const semantic::Program& program,
	const semantic::DumpNode& node);
bool IsVariableDeclarationOnly(const semantic::Program& program,
	const semantic::DumpNode& node, bool has_initializer);
void ApplyLifecycleSymbolMetadata(const semantic::Program& program,
	const semantic::DumpNode& node, lowering::ir::Program* output,
	lowering::ir::SymbolId symbol,
	abi_mangle::AbiMangleContext* context = 0,
	abi_mangle::AbiMangleStats* stats = 0);

}  // namespace abi
}  // namespace lowering
}  // namespace cppgm
