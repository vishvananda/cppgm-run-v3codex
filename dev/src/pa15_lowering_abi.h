#pragma once

#include "abi_mangle.h"
#include "pa11_model.h"
#include "pa12_semantic_model.h"
#include "pa15_lowir_types.h"

#include <string>

namespace cppgm
{
namespace pa15_lowir_detail
{
struct TypedProgram;
}
namespace pa15_lowering_abi
{

std::string MangleType(const pa11::Program& program, pa11::TypeId type,
	abi_mangle::AbiMangleStats* stats = 0,
	abi_mangle::AbiMangleContext* context = 0);
bool IsFunctionEmissionDemanded(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node,
	bool host_object_emission = false);
bool IsFunctionDeclarationBoundaryComplete(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node);
bool IsVariableDeclarationOnly(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node, bool has_initializer);
bool HasWeakLinkage(
	const pa11::Program& program, pa11::BindingId binding, bool function);
std::string MangleFunction(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node,
	bool force_lifecycle_base_entry = false,
	abi_mangle::AbiMangleStats* stats = 0,
	abi_mangle::AbiMangleContext* context = 0);
std::string MangleVariable(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node,
	abi_mangle::AbiMangleStats* stats = 0,
	abi_mangle::AbiMangleContext* context = 0);
std::string MangleThreadLocalWrapper(const pa11::Program& program,
	pa11::BindingId binding, pa11::NameId fallback_name,
	abi_mangle::AbiMangleStats* stats = 0,
	abi_mangle::AbiMangleContext* context = 0);
void ApplyBuiltinSymbolMetadata(pa15_lowir_detail::Symbol* symbol,
	pa11::BuiltinFunctionKind kind,
	hosted_builtin::MemoryIntrinsicKind memory_kind);
void ApplyNativeRuntimeSymbolMetadata(
	const pa15_lowir_detail::TypedProgram& program,
	pa15_lowir_detail::Symbol* symbol);
void ApplyBuiltinParameterMetadata(pa15_lowir_detail::Parameter* parameter,
	pa11::BuiltinFunctionKind kind,
	hosted_builtin::MemoryIntrinsicKind memory_kind, std::size_t index);
void ApplyLifecycleSymbolMetadata(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node,
	pa15_lowir_detail::TypedProgram* output,
	pa15_lowir_detail::SymbolId symbol,
	abi_mangle::AbiMangleContext* context = 0,
	abi_mangle::AbiMangleStats* stats = 0);

}
}
