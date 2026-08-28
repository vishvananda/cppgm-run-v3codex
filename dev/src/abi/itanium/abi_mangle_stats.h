#pragma once

// Numeric counters for standalone and integrated ABI mangling.

#include <cstddef>

namespace abi_mangle {

struct AbiMangleStats
{
  std::size_t production_mangles = 0;
  std::size_t production_fact_bytes = 0;
  std::size_t production_type_definitions = 0;
  std::size_t production_argument_definitions = 0;
  std::size_t production_expression_definitions = 0;
  std::size_t production_context_definitions = 0;
  std::size_t production_entity_definitions = 0;
  std::size_t resolved_type_cache_requests = 0;
  std::size_t resolved_type_cache_hits = 0;
  std::size_t source_files = 0;
  std::size_t source_bytes = 0;
  std::size_t cases = 0;
  std::size_t records = 0;
  std::size_t canonical_types = 0;
  std::size_t canonical_arguments = 0;
  std::size_t canonical_expressions = 0;
  std::size_t typed_expression_operations = 0;
  std::size_t text_expression_operations = 0;
  std::size_t typed_builtin_types = 0;
  std::size_t text_builtin_types = 0;
  std::size_t typed_standard_substitutions = 0;
  std::size_t text_standard_substitutions = 0;
  std::size_t typed_vendor_qualifiers = 0;
  std::size_t text_vendor_qualifiers = 0;
  std::size_t typed_array_bounds = 0;
  std::size_t text_array_bounds = 0;
  std::size_t typed_local_presentations = 0;
  std::size_t text_local_presentations = 0;
  std::size_t typed_type_source_names = 0;
  std::size_t text_type_source_names = 0;
  std::size_t typed_type_tags = 0;
  std::size_t text_type_tags = 0;
  std::size_t typed_argument_source_names = 0;
  std::size_t text_argument_source_names = 0;
  std::size_t typed_local_source_names = 0;
  std::size_t text_local_source_names = 0;
  std::size_t typed_literal_suffixes = 0;
  std::size_t text_literal_suffixes = 0;
  std::size_t typed_main_contexts = 0;
  std::size_t external_assembly_names = 0;
  std::size_t external_c_function_names = 0;
  std::size_t external_builtin_runtime_names = 0;
  std::size_t external_c_variable_names = 0;
  std::size_t external_global_tls_names = 0;
  std::size_t canonical_cache_hits = 0;
  std::size_t definition_cache_hits = 0;
  std::size_t path_components = 0;
  std::size_t text_type_path_components = 0;
  std::size_t text_function_path_components = 0;
  std::size_t text_object_path_components = 0;
  std::size_t text_entity_path_components = 0;
  std::size_t text_substitution_path_components = 0;
  std::size_t substitution_lookups = 0;
  std::size_t substitution_hits = 0;
  std::size_t substitution_entries = 0;
  std::size_t isolated_entity_encodings = 0;
  std::size_t output_bytes = 0;
  std::size_t peak_input_bytes = 0;
  unsigned long long parse_nanoseconds = 0;
  unsigned long long encode_nanoseconds = 0;
};

}  // namespace abi_mangle
