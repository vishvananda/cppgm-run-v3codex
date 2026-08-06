#pragma once

#include "pa12_semantic_model.h"
#include "pa15_lowering.h"
#include "pa15_lowering_support.h"
#include "pa15_source_type_lowering.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cppgm
{
namespace pa16_lowering_detail
{

class StaticInitializerLowering
{
public:
	StaticInitializerLowering(
		const pa12_semantic_detail::Program& program,
		const pa12_semantic_detail::DumpArena& arena,
		pa15_lowir_detail::TypedProgram& output,
		LowIRLoweringStats* stats,
		const std::vector<pa15_lowir_detail::SymbolId>& function_symbols,
		const std::vector<pa15_lowir_detail::SymbolId>& global_symbols,
		std::vector<pa15_lowir_detail::SymbolId>& literal_symbols,
		std::unordered_map<std::string, pa15_lowir_detail::SymbolId>&
			string_literal_symbols,
		const std::vector<std::uint32_t>& function_definitions);

	bool Lower(const pa12_semantic_detail::NamespaceObjectAction& action,
		bool thread_local_object, pa15_lowir_detail::Global* global,
		bool* needs_global_class_initializer);
	void SetZero(pa11::TypeId type, pa15_lowir_detail::Global* global);
	pa15_lowir_detail::SymbolId EnsureStringLiteral(std::uint32_t node);

private:
	pa15_lowering_support::NodeChildren Children(std::uint32_t node) const;
	bool IsTrivialConstructorAction(pa11::TypeId type,
		const pa15_lowering_support::NodeChildren& children) const;
	bool SymbolForBinding(pa11::BindingId binding,
		pa15_lowir_detail::SymbolId* symbol);
	bool ResolveConstantAddress(std::uint32_t node,
		pa15_lowir_detail::SymbolId* symbol, std::int64_t* offset);
	bool RequiresDynamicAddress(std::uint32_t node) const;
	void AppendZero(std::size_t bytes,
		std::vector<pa15_lowir_detail::Global::DataItem>* items);
	bool AppendValue(pa11::TypeId type, std::uint32_t node,
		std::vector<pa15_lowir_detail::Global::DataItem>* items,
		const std::unordered_map<pa11::BindingId, std::uint32_t>*
			substitutions = 0);
	bool AppendConstructorValue(pa11::TypeId type, std::uint32_t action_node,
		std::vector<pa15_lowir_detail::Global::DataItem>* items);

	const pa12_semantic_detail::Program& program_;
	const pa12_semantic_detail::DumpArena& arena_;
	pa15_lowir_detail::TypedProgram& output_;
	LowIRLoweringStats* stats_;
	const std::vector<pa15_lowir_detail::SymbolId>& function_symbols_;
	const std::vector<pa15_lowir_detail::SymbolId>& global_symbols_;
	std::vector<pa15_lowir_detail::SymbolId>& literal_symbols_;
	std::unordered_map<std::string, pa15_lowir_detail::SymbolId>&
		string_literal_symbols_;
	const std::vector<std::uint32_t>& function_definitions_;
	pa15_lowering_detail::SourceTypeLowering types_;
};

}
}
