#pragma once

#include "semantic/model/graph.h"
#include "lowering/api.h"
#include "lowering/support/sequences.h"
#include "lowering/core/source_types.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cppgm
{
namespace lowering
{

class StaticInitializerLowering
{
public:
	StaticInitializerLowering(
		const semantic::Program& program,
		const semantic::DumpArena& arena,
		lowering::ir::Program& output,
		lowering::Stats* stats,
		const std::vector<lowering::ir::SymbolId>& function_symbols,
		const std::vector<lowering::ir::SymbolId>& global_symbols,
		std::vector<lowering::ir::SymbolId>& literal_symbols,
		const std::vector<std::uint32_t>& function_definitions,
		const std::vector<lowering::ir::SymbolId>& class_vtable_symbols);

	bool Lower(const semantic::NamespaceObjectAction& action,
		bool thread_local_object, lowering::ir::Global* global,
		bool* needs_global_class_initializer,
		bool* keep_global_class_address = 0);
	bool LowerConstantObject(semantic::TypeId type, std::uint32_t initializer,
		lowering::ir::Global* global);
	void SetZero(semantic::TypeId type, lowering::ir::Global* global);
	bool HasConstantAddress(std::uint32_t node);
	lowering::ir::SymbolId EnsureStringLiteral(std::uint32_t node);
	lowering::ir::SymbolId EnsureStringLiteralSpelling(
		const std::string& spelling);

private:
	lowering::support::NodeChildren Children(std::uint32_t node) const;
	bool IsTrivialConstructorAction(semantic::TypeId type,
		const lowering::support::NodeChildren& children) const;
	bool IsEmptyConstructionTransferRecipe(std::uint32_t node) const;
	bool SymbolForBinding(semantic::BindingId binding,
		lowering::ir::SymbolId* symbol);
	bool ResolveConstantAddress(std::uint32_t node,
		lowering::ir::SymbolId* symbol, std::int64_t* offset);
	bool RequiresDynamicAddress(std::uint32_t node) const;
	void AppendZero(std::size_t bytes,
		std::vector<lowering::ir::Global::DataItem>* items);
	bool AppendValue(semantic::TypeId type, std::uint32_t node,
		std::vector<lowering::ir::Global::DataItem>* items,
		const std::vector<std::pair<semantic::BindingId, std::uint32_t> >*
			substitutions = 0, bool allow_constructor = true);
	bool AppendConstructorValue(semantic::TypeId type, std::uint32_t action_node,
		std::vector<lowering::ir::Global::DataItem>* items,
		bool require_vptr = false);

	const semantic::Program& program_;
	const semantic::DumpArena& arena_;
	lowering::ir::Program& output_;
	lowering::Stats* stats_;
	const std::vector<lowering::ir::SymbolId>& function_symbols_;
	const std::vector<lowering::ir::SymbolId>& global_symbols_;
	std::vector<lowering::ir::SymbolId>& literal_symbols_;
	const std::vector<std::uint32_t>& function_definitions_;
	const std::vector<lowering::ir::SymbolId>& class_vtable_symbols_;
	lowering::SourceTypeLowering types_;
};

}
}
