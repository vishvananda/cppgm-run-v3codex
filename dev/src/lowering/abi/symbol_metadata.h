#pragma once

#include "lowering/ir/types.h"
#include "semantic/model/program.h"

#include <cstddef>

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

void ApplyBuiltinSymbolMetadata(lowering::ir::Symbol* symbol,
	semantic::BuiltinFunctionKind kind,
	hosted_builtin::MemoryIntrinsicKind memory_kind);
void ApplyNativeRuntimeSymbolMetadata(const lowering::ir::Program& program,
	lowering::ir::Symbol* symbol);
void ApplyBuiltinParameterAliasMetadata(lowering::ir::Parameter* parameter,
	semantic::BuiltinFunctionKind kind,
	hosted_builtin::MemoryIntrinsicKind memory_kind, std::size_t index);

}  // namespace abi
}  // namespace lowering
}  // namespace cppgm
