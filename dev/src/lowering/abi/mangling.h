#pragma once

#include "abi/itanium/abi_mangle.h"
#include "semantic/model/graph.h"
#include "semantic/model/program.h"

#include <string>

namespace cppgm
{
namespace lowering
{
namespace abi
{

std::string MangleType(const semantic::Program& program, semantic::TypeId type,
	abi_mangle::AbiMangleStats* stats = 0,
	abi_mangle::AbiMangleContext* context = 0);
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
bool HasWeakLinkage(const semantic::Program& program,
	semantic::BindingId binding, bool function);

}  // namespace abi
}  // namespace lowering
}  // namespace cppgm
