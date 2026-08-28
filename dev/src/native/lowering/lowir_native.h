#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "lowir/model/program.h"
#include "native/mir/mir_model.h"

namespace lowir_native {

enum NativeMovementReason
{
  NMR_PARAMETER_HOME,
  NMR_SOURCE_SLOT,
  NMR_SCALAR_TEMPORARY,
  NMR_OBJECT_TEMPORARY,
  NMR_CALL_BOUNDARY,
  NMR_CLEANUP_EH,
  NMR_WIDTH_NORMALIZATION,
  NMR_ADDRESS_MATERIALIZATION,
  NMR_ENCODER_FALLBACK,
  NMR_COUNT
};

enum TemporaryHomeReason
{
  THR_SCALAR_VALUE,
  THR_OBJECT_VALUE,
  THR_LIVE_ACROSS_CALL,
  THR_EDGE_LIVE,
  THR_REGISTER_PRESSURE,
  THR_ADDRESS_ESCAPE,
  THR_CALL_RESULT,
  THR_EXTENDED_REPRESENTATION,
  THR_COUNT
};

struct Stats
{
	// Disabled-by-default presentation transit telemetry.  Every update is
	// guarded by the optional Stats pointer already used by --stats.
	std::size_t presentation_map_calls = 0;
	std::size_t presentation_map_hits = 0;
	std::size_t presentation_map_misses = 0;
	std::size_t presentation_mapped_bytes = 0;
	std::size_t presentation_map_storage_bytes = 0;
	std::size_t mir_string_entries = 0;
	std::size_t mir_spelling_bytes = 0;
	std::size_t mir_string_storage_bytes = 0;
	std::size_t mir_model_peak_live_bytes = 0;
	std::size_t native_semantic_string_reads = 0;
	std::size_t native_literal_text_parses = 0;
	std::size_t code_buffer_typed_labels = 0;
	std::size_t code_buffer_object_labels = 0;
	std::size_t code_buffer_named_labels = 0;
	std::size_t code_buffer_typed_fixups = 0;
	std::size_t code_buffer_named_fixups = 0;
	std::size_t elf_internal_string_entries = 0;
	std::size_t elf_imported_string_entries = 0;
	std::size_t elf_string_map_probes = 0;
	std::size_t final_strtab_entries = 0;
	std::size_t final_strtab_bytes = 0;
	std::size_t final_shstrtab_entries = 0;
	std::size_t final_shstrtab_bytes = 0;
	std::size_t final_shared_string_entries = 0;
	std::size_t final_section_string_reuses = 0;
	std::size_t final_symbol_string_reuses = 0;
	std::size_t final_string_suffix_aliases = 0;
	std::size_t encoded_section_bytes = 0;
	std::size_t final_elf_live_bytes = 0;
	std::uint64_t presentation_bridge_nanoseconds = 0;
	std::uint64_t native_literal_parse_nanoseconds = 0;
	std::uint64_t elf_string_table_nanoseconds = 0;
  std::size_t functions = 0;
  std::size_t blocks = 0;
  std::size_t lowir_instructions = 0;
  std::size_t mir_instructions = 0;
  std::size_t machine_opt_functions = 0;
  std::size_t machine_opt_input_instructions = 0;
  std::size_t machine_opt_output_instructions = 0;
  std::size_t machine_opt_instruction_visits = 0;
  std::size_t machine_opt_cfg_edge_visits = 0;
  std::size_t machine_opt_worklist_pushes = 0;
  std::size_t machine_opt_rewrites = 0;
  std::size_t machine_opt_identity_moves = 0;
  std::size_t machine_opt_frameless_functions = 0;
  std::size_t machine_opt_frameless_call_functions = 0;
  std::size_t machine_opt_frameless_saved_registers = 0;
  std::size_t machine_opt_peak_analysis_bytes = 0;
  std::size_t live_location_scans = 0;
  std::size_t live_location_value_visits = 0;
  std::size_t live_location_alias_queries = 0;
  std::size_t live_location_updates = 0;
  std::size_t spill_attempts = 0;
  std::size_t spill_value_visits = 0;
  std::size_t spill_candidates = 0;
  std::size_t spill_full_scan_fallbacks = 0;
  std::size_t spills = 0;
  std::size_t temporary_frame_homes_created = 0;
  std::size_t temporary_frame_homes_reused = 0;
  std::size_t exact_forward_edge_values = 0;
  std::size_t exact_forward_edge_register_retains = 0;
  std::size_t planned_edge_register_retains = 0;
  std::size_t planned_value_registers = 0;
  std::size_t planned_register_grants = 0;
  std::size_t planned_edge_residencies = 0;
  std::size_t planned_interval_releases = 0;
  std::size_t span_free_edge_releases = 0;
  std::size_t planned_phi_registers = 0;
  std::size_t phi_register_homes = 0;
  std::size_t planned_invariant_registers = 0;
  std::size_t planner_cyclic_region_candidates = 0;
  std::size_t planner_cyclic_region_assignments = 0;
  std::size_t planned_cyclic_region_grants = 0;
  std::size_t planned_cyclic_region_residencies = 0;
  std::size_t planned_cyclic_region_busy_fails = 0;
  std::size_t planned_rematerialized_addresses = 0;
  std::size_t planned_rematerialized_global_addresses = 0;
  std::size_t planned_rematerialized_constant_indexes = 0;
  std::size_t planned_direct_copy_edge_homes = 0;
  std::size_t planned_direct_call_edge_homes = 0;
  std::size_t edge_staging_total = 0;
  std::array<std::size_t, lowir_model::Instruction::IK_UNREACHABLE + 1>
    edge_staging_by_kind = {{0}};
  std::size_t edge_staging_gpr = 0;
  std::size_t edge_staging_xmm = 0;
  std::size_t edge_staging_eh = 0;
  std::size_t edge_staging_loop_invariant = 0;
  std::size_t edge_staging_crosses_call = 0;
  std::size_t edge_staging_narrow_alias = 0;
  std::size_t edge_staging_fixed_clobber = 0;
  std::size_t edge_staging_single_use = 0;
  std::size_t edge_staging_multi_use = 0;
  std::size_t edge_staging_addr_slot = 0;
  std::size_t edge_staging_addr_global = 0;
  std::size_t edge_staging_addr_other = 0;
  std::size_t edge_staging_index_constant = 0;
  std::size_t edge_staging_index_variable = 0;
  std::size_t planned_grant_clobber_fails = 0;
  std::size_t planned_grant_busy_fails = 0;
  std::size_t planned_grant_busy_fails_callee = 0;
  std::size_t planned_grant_busy_planned_holder = 0;
  std::size_t planned_defined_in_plan = 0;
  std::size_t planned_defined_in_plan_callee = 0;
  std::size_t planned_defined_frame = 0;
  std::size_t planned_defined_frame_callee = 0;
  std::size_t planned_defined_other_register = 0;
  std::size_t planned_defined_other_register_callee = 0;
  std::size_t planned_defined_elsewhere = 0;
  std::array<std::size_t, THR_COUNT + 1> planned_frame_homes_by_reason =
    {{0}};
  std::size_t call_arg_frame_loads = 0;
  std::size_t call_arg_frame_loads_slot = 0;
  std::size_t call_arg_frame_loads_parameter = 0;
  std::size_t call_arg_frame_loads_only_call_argument = 0;
  std::size_t call_arg_frame_loads_storage_address = 0;
  std::size_t call_arg_frame_loads_planned = 0;
  std::size_t call_arg_frame_loads_crossing_unplanned = 0;
  std::size_t call_arg_frame_loads_other = 0;
  std::size_t call_arg_frame_loads_oca_call_result = 0;
  std::size_t call_arg_frame_loads_oca_crossing = 0;
  std::size_t call_arg_frame_loads_oca_edge_live = 0;
  std::size_t call_arg_frame_loads_oca_multi_use = 0;
  std::size_t planner_candidate_call_arguments = 0;
  std::size_t planner_assigned_call_arguments = 0;
  std::size_t planner_assign_failures = 0;
  std::size_t planner_assign_failures_call_argument = 0;
  std::size_t planned_grant_busy_parameter_holder = 0;
  std::size_t planned_grant_busy_value_holder = 0;
  std::size_t planned_grant_busy_no_holder = 0;
  std::size_t narrow_call_result_normalizations_omitted = 0;
  std::size_t redundant_integer_normalizations_omitted = 0;
  std::size_t fused_integer_normalization_moves = 0;
  std::size_t scratch_carried_reloads = 0;
  // Per-function movement census, filled only when the driver requests it
  // with --stats-functions; one formatted line per lowered function.
  bool function_census = false;
  std::vector<std::string> function_census_lines;
  std::array<std::size_t, NMR_COUNT> movement_instructions_by_reason = {{0}};
  std::array<std::size_t, NMR_COUNT> movement_loads_by_reason = {{0}};
  std::array<std::size_t, NMR_COUNT> movement_stores_by_reason = {{0}};
  std::array<std::size_t, NMR_COUNT> movement_register_copies_by_reason = {{0}};
  std::array<std::size_t, NMR_COUNT> movement_addresses_by_reason = {{0}};
  std::array<std::size_t, NMR_COUNT> movement_normalizations_by_reason = {{0}};
  std::array<std::size_t, THR_COUNT> temporary_home_requests_by_reason = {{0}};
  std::array<std::size_t, THR_COUNT> temporary_home_creations_by_reason = {{0}};
  std::array<std::size_t, THR_COUNT> temporary_home_reuses_by_reason = {{0}};
  std::size_t shared_storage_lifetime_extensions = 0;
  std::size_t reclaim_attempts = 0;
  std::size_t reclaim_parameter_visits = 0;
  std::size_t reclaims = 0;
  std::size_t eh_region_states = 0;
  std::size_t eh_region_edges = 0;
  std::size_t eh_call_sites = 0;
  std::size_t eh_lsda_call_sites = 0;
  std::size_t eh_coalesced_call_sites = 0;
  std::size_t eh_lsda_unprotected_call_sites = 0;
  std::size_t eh_lsda_uncovered_code_bytes = 0;
  std::size_t eh_lsda_call_site_table_bytes = 0;
  std::size_t eh_lsda_unprotected_call_site_bytes = 0;
  std::size_t semantic_resume_instructions = 0;
  std::size_t physical_resume_terminals = 0;
  std::size_t shared_resume_branches = 0;
  std::size_t immediate_stores_selected = 0;
  std::size_t memory_rhs_operations_selected = 0;
  std::size_t three_operand_adds_selected = 0;
  std::size_t load_address_register_takeovers = 0;
  std::size_t direct_zero_operations_selected = 0;
  std::size_t direct_zero_stores_emitted = 0;
  std::size_t direct_zero_bytes = 0;
  std::size_t native_returns = 0;
  std::size_t physical_epilogues = 0;
  std::size_t fixups = 0;
  std::size_t output_bytes = 0;
  std::uint64_t lower_nanoseconds = 0;
  std::uint64_t machine_opt_nanoseconds = 0;
  std::uint64_t encode_nanoseconds = 0;
  std::uint64_t write_nanoseconds = 0;
  std::size_t planner_use_tail_candidates = 0;
  std::size_t planner_use_tail_assignments = 0;
  std::size_t planned_use_tail_promotions = 0;
  std::size_t planned_use_tail_busy_fails = 0;
  std::size_t planner_local_phi_candidates = 0;
  std::size_t planner_local_phi_assignments = 0;
  std::size_t planned_local_phi_promotions = 0;
  std::size_t planned_local_phi_busy_fails = 0;
};

struct RelocatableLabel
{
  std::string name;
  std::size_t offset = 0;
};

struct RelocatableRelocation
{
  enum Kind { RELATIVE32, ABSOLUTE64 } kind = RELATIVE32;
  std::size_t offset = 0;
  std::string target;
  long long addend = 0;
};

struct RelocatableSection
{
  std::size_t alignment = 1;
  std::vector<unsigned char> bytes;
  std::vector<RelocatableLabel> labels;
  std::vector<RelocatableRelocation> relocations;
};

struct RelocatableObject
{
  std::vector<RelocatableSection> sections;
};

class ProgramLoweringSession
{
public:
  ProgramLoweringSession(const lowir_model::LowirProgram & program,
                         const std::string & target, int optimization_level = 0,
                         Stats * stats = 0);
  ~ProgramLoweringSession();

  std::size_t function_count() const;
  const lowir_model::LowirProgram & prepared_program() const;
  mir_model::MirFunction lower_function(std::size_t index);
  mir_model::MirProgram take_program_shell();

private:
  struct Impl;
  Impl * impl_;

  ProgramLoweringSession(const ProgramLoweringSession &);
  ProgramLoweringSession & operator=(const ProgramLoweringSession &);
};

mir_model::MirProgram lower_program(const lowir_model::LowirProgram & program,
                                    const std::string & target,
                                    int optimization_level = 0,
                                    Stats * stats = 0);

void write_linux_executable(const std::string & path,
                            const mir_model::MirProgram & program,
                            Stats * stats = 0);
void write_linux_executable(const std::string & path,
                            const mir_model::MirProgram & program,
                            const std::vector<RelocatableObject> & objects,
                            Stats * stats = 0);
void write_linux_executable(const std::string & path,
                            const lowir_model::LowirProgram & program,
                            const std::string & target,
                            const std::vector<RelocatableObject> & objects,
                            int optimization_level,
                            Stats * stats = 0);

void write_linux_relocatable(const std::string & path,
                             const lowir_model::LowirProgram & program,
                             const std::string & target,
                             int optimization_level,
                             Stats * stats = 0);

}  // namespace lowir_native
