#pragma once

#include "abi/itanium/abi_mangle.h"
#include "semantic/semantic.h"
#include "lowering/ir/model.h"
#include "lowering/presentation/local_names.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace cppgm
{
namespace lowering
{

struct Source
{
	std::string path;
	std::string source;

	Source(const std::string& path_value, const std::string& source_value)
		: path(path_value), source(source_value) {}
};

struct Stats
{
	lowering::presentation::LocalPresentationCounters local_presentation;
	std::size_t source_bytes;
	semantic::Stats semantic;
	std::size_t lowered_nodes;
	std::size_t functions;
	std::size_t globals;
	std::size_t blocks;
	std::size_t instructions;
	abi_mangle::AbiMangleStats abi;
	std::size_t binding_index_probes;
	std::size_t slot_implicit_object_fact_reads;
	std::size_t virtual_calls;
	std::size_t vptr_stores;
	std::size_t virtual_base_boundary_scan_nodes;
	std::size_t virtual_base_boundary_facts;
	std::size_t virtual_base_call_arguments;
	std::size_t virtual_base_boundary_binding_steps;
	std::size_t virtual_base_boundary_binding_cache_hits;
	std::size_t virtual_base_boundary_binding_table_growth;
	std::size_t vtable_offset_rows;
	std::size_t vtable_slots;
	std::size_t vtable_thunk_requests;
	std::size_t vtable_thunk_cache_hits;
	std::size_t vtable_thunk_index_probes;
	std::size_t deleting_destructors;
	std::size_t rtti_graph_nodes_visited;
	std::size_t rtti_demand_requests;
	std::size_t rtti_types_demanded;
	std::size_t rtti_symbol_lookups;
	std::size_t rtti_base_dependency_visits;
	std::size_t cleanup_dispatch_probes;
	std::size_t cleanup_dispatch_cache_hits;
	std::size_t cleanup_dispatch_entries;
	std::size_t cleanup_state_probes;
	std::size_t cleanup_state_hits;
	std::size_t cleanup_unique_states;
	std::size_t cleanup_blocks_emitted;
	std::size_t cleanup_destructor_actions_avoided;
	std::size_t cleanup_resume_operations_avoided;
	std::size_t direct_class_call_destination_placements;
	std::size_t direct_class_call_staging_slots_avoided;
	std::size_t bit_field_storage_unit_transfers;
	std::size_t constant_template_bytes;
	std::size_t constant_template_globals;
	std::size_t constant_template_copies;
	std::size_t conditional_lifetime_slots;
	std::size_t conditional_lifetime_marks;
	std::size_t branch_cleanup_actions;
	std::size_t statement_scheduler_entries;
	std::size_t statement_scheduler_nested_entries;
	std::size_t statement_scheduler_tasks;
	std::size_t statement_scheduler_peak_tasks;
	std::size_t exception_selector_resets;
	std::size_t exception_selector_table_growth;
	std::size_t exception_selector_assignments;
	std::size_t terminate_boundaries_explicit;
	std::size_t terminate_boundaries_derived_special_member;
	std::size_t terminate_boundaries_template_specialization;
	std::size_t terminate_boundaries_builtin_runtime;
	std::size_t unexpected_boundaries;
	std::size_t potentially_throwing_explicit_operations;
	std::size_t potentially_throwing_indirect_calls;
	std::size_t potentially_throwing_ordinary_calls;
	std::size_t potentially_throwing_special_member_calls;
	std::size_t potentially_throwing_template_calls;
	std::size_t potentially_throwing_builtin_runtime_calls;
	std::size_t force_inline_candidates;
	std::size_t force_inline_recursive_candidates;
	std::size_t force_inline_call_probes;
	std::size_t force_inline_calls;
	std::size_t force_inline_blocks;
	std::size_t force_inline_cloned_instructions;
	std::size_t post_inline_reachable_functions;
	std::size_t post_inline_unreachable_weak_functions;
	std::size_t post_inline_unreachable_internal_functions;
	std::size_t post_inline_pruned_functions;
	std::size_t post_inline_retained_external_strong;
	std::size_t post_inline_retained_address_or_relocation;
	std::size_t post_inline_retained_direct_call;
	std::size_t post_inline_retained_lifecycle;
	std::size_t post_inline_retained_eh_or_runtime;
	std::size_t post_inline_retained_required_weak;
	std::size_t post_inline_retained_object_output_root;
	std::size_t post_inline_retained_object_output_root_weak;
	std::size_t post_inline_retained_object_output_root_internal;
	std::size_t post_inline_retained_conservative_fallback;
	std::vector<std::string> post_inline_retained_conservative_fallback_names;
	std::vector<std::string> post_inline_unreachable_internal_names;
	std::size_t typed_identity_paths;
	std::size_t typed_identity_types;
	std::size_t typed_identity_bytes;
	std::size_t typed_storage_bytes;
	std::size_t output_bytes;
	std::uint64_t lowering_nanoseconds;
	std::uint64_t render_nanoseconds;

	Stats();
};

// Construct the production typed LowIR result.  Textual LowIR is only a view
// produced by lowering::WriteLowIR and is not required by in-process consumers.
ir::Program BuildProgram(
	const std::vector<Source>& sources,
	const PreprocessingOptions& options,
	Stats* stats = 0,
	bool complete_constructor_unwind = false,
	bool host_object_emission = false,
	bool prune_unreachable_weak_functions = false,
	bool retain_local_names = true);

// Analyze all inputs through PA12, lower directly from the borrowed canonical
// graph into one typed LowIR program, and serialize the PA15 assignment view.
void WriteLowIR(const std::vector<Source>& sources,
	const PreprocessingOptions& options, std::ostream& output,
	Stats* stats = 0);

}  // namespace lowering
}
