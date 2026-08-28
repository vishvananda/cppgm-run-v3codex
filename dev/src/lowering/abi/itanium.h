#pragma once

#include "abi/itanium/abi_mangle.h"
#include "semantic/model/program.h"
#include "semantic/model/graph.h"
#include "lowering/ir/types.h"

#include <string>

namespace cppgm
{
namespace lowering
{
namespace ir
{
struct TypedProgram;
}
}
namespace pa15_lowering_abi
{

std::string MangleType(const semantic::Program& program, semantic::TypeId type,
	abi_mangle::AbiMangleStats* stats = 0,
	abi_mangle::AbiMangleContext* context = 0);
bool IsFunctionEmissionDemanded(const semantic::Program& program,
	const semantic::DumpNode& node,
	bool host_object_emission = false);
bool IsFunctionDeclarationBoundaryComplete(const semantic::Program& program,
	const semantic::DumpNode& node);
bool IsVariableDeclarationOnly(const semantic::Program& program,
	const semantic::DumpNode& node, bool has_initializer);
bool HasWeakLinkage(
	const semantic::Program& program, semantic::BindingId binding, bool function);
std::string MangleFunction(const semantic::Program& program,
	const semantic::DumpNode& node,
	bool force_lifecycle_base_entry = false,
	abi_mangle::AbiMangleStats* stats = 0,
	abi_mangle::AbiMangleContext* context = 0);
std::string MangleVariable(const semantic::Program& program,
	const semantic::DumpNode& node,
	abi_mangle::AbiMangleStats* stats = 0,
	abi_mangle::AbiMangleContext* context = 0);
std::string MangleThreadLocalWrapper(const semantic::Program& program,
	semantic::BindingId binding, semantic::NameId fallback_name,
	abi_mangle::AbiMangleStats* stats = 0,
	abi_mangle::AbiMangleContext* context = 0);
void ApplyBuiltinSymbolMetadata(lowering::ir::Symbol* symbol,
	semantic::BuiltinFunctionKind kind,
	hosted_builtin::MemoryIntrinsicKind memory_kind);
void ApplyNativeRuntimeSymbolMetadata(
	const lowering::ir::TypedProgram& program,
	lowering::ir::Symbol* symbol);
void ApplyBuiltinParameterAliasMetadata(lowering::ir::Parameter* parameter,
	semantic::BuiltinFunctionKind kind,
	hosted_builtin::MemoryIntrinsicKind memory_kind, std::size_t index);
void ApplyLifecycleSymbolMetadata(const semantic::Program& program,
	const semantic::DumpNode& node,
	lowering::ir::TypedProgram* output,
	lowering::ir::SymbolId symbol,
	abi_mangle::AbiMangleContext* context = 0,
	abi_mangle::AbiMangleStats* stats = 0);

}
}
