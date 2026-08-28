#include "semantic/analysis/analyzer.h"

namespace cppgm
{
namespace semantic
{

void DumpArena::ReserveNodes(std::size_t count)
{
	if (count < kNoDumpEdge) nodes.reserve(count);
}

void Analyzer::ReserveSemanticCapacity(const SyntaxArena& arena)
{
	// Scope, name, and declaration records are normally no more numerous than
	// syntax nodes. Use that known scale to avoid repeatedly moving them;
	// expansion-heavy translation units retain ordinary vector growth.
	const std::size_t syntax_nodes = arena.Nodes();
	if (syntax_nodes >= static_cast<std::size_t>(kNoBinding)) return;
	program_->ReserveSemanticStorage(syntax_nodes);

	// Grammar scaffolding accounts for roughly half the syntax arena and does
	// not become semantic dump nodes. Start at that scale because DumpNode is a
	// comparatively large retained record; denser units still grow normally.
	dump_.ReserveNodes((syntax_nodes + 1) / 2);
}

void Analyzer::PublishBindingPopulationStats()
{
	for (std::size_t i = 1; i < program_->bindings.size(); ++i)
	{
		const BindingRecord& binding = program_->bindings[i];
		if (!binding.lambda_invocation &&
			binding.layout_fact != kNoBindingLayoutFact)
			++stats_->binding_layout_fact_records;
		if (binding.template_argument_list != kNoTemplateArgumentList ||
			binding.template_argument_count != 0 || binding.exception_type_count != 0 ||
			binding.function_template_abi_recipe != kNoFunctionTemplateAbiRecipe ||
			binding.exception_boundary != FUNCTION_EXCEPTION_BOUNDARY_NONE)
			++stats_->binding_template_fact_records;
		if (binding.presentation_name_override != 0 ||
			binding.object_section_name != 0 ||
			binding.assembly_name != 0 || binding.abi_tag_count != 0 ||
			binding.display_flavor != NAMED_NONE || binding.display_type_name != 0 ||
			binding.lifecycle_base_entry != kNoBinding)
			++stats_->binding_output_fact_records;
		if (binding.conversion_target != kNoType ||
			binding.operator_kind != OPERATOR_NONE ||
			binding.builtin_function != BUILTIN_FUNCTION_NONE ||
			binding.hosted_integer_intrinsic != hosted_builtin::INTEGER_INTRINSIC_NONE ||
			binding.hosted_floating_intrinsic != hosted_builtin::FLOATING_INTRINSIC_NONE ||
			binding.hosted_memory_intrinsic != hosted_builtin::MEMORY_INTRINSIC_NONE ||
			binding.operator_literal_suffix != 0)
			++stats_->binding_operator_fact_records;
		if (binding.value != 0) ++stats_->binding_value_records;
	}
}

void Analyzer::PublishPresentationPopulationStats()
{
	stats_->binding_record_size = sizeof(BindingRecord);
	stats_->entity_record_size = sizeof(EntityRecord);
	stats_->function_info_size = sizeof(FunctionInfo);
	stats_->dump_node_size = sizeof(DumpNode);

	program_->AccumulateScopeEmissionNames(
		&stats_->presentation_retained_values[
			SEMANTIC_PRESENTATION_READ_SCOPE_EMISSION],
		&stats_->presentation_retained_bytes[
			SEMANTIC_PRESENTATION_READ_SCOPE_EMISSION]);
}

}
}
