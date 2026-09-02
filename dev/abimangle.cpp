// Student-facing scaffold for the PA14 `abimangle` binary.

#include "abi/itanium/abi_mangle.h"
#include "abi/itanium/abi_mangle_errors.h"
#include "support/driver_errors.h"
#include "support/exception_types.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

namespace {

struct AbimangleInvocation
{
  string outfile;
  vector<string> inputs;
  bool collect_stats = false;
};

bool has_help_arg(int argc, char ** argv)
{
  for(int i = 1; i < argc; ++i) {
    const string arg = argv[i];
    if(arg == "--help" || arg == "-h") {
      return true;
    }
  }
  return false;
}

void print_help()
{
  cout << "usage: abimangle -o <outfile> <abi-facts-file>...\n";
}

AbimangleInvocation parse_invocation(int argc, char ** argv)
{
  AbimangleInvocation invocation;
  for(int i = 1; i < argc; ++i) {
    const string arg = argv[i];
    if(arg == "-o") {
      if(i + 1 >= argc) {
        cppgm::driver_errors::ThrowInvocation("missing output file after -o");
      }
      invocation.outfile = argv[++i];
      continue;
    }
    if(arg == "--stats") {
      invocation.collect_stats = true;
      continue;
    }
    invocation.inputs.push_back(arg);
  }
  if(invocation.outfile.empty() || invocation.inputs.empty()) {
    cppgm::driver_errors::ThrowInvocation("invalid usage");
  }
  return invocation;
}

int run_abimangle(int argc, char ** argv)
{
  if(has_help_arg(argc, argv)) {
    print_help();
    return EXIT_SUCCESS;
  }
  const AbimangleInvocation invocation = parse_invocation(argc, argv);
  ofstream out(invocation.outfile.c_str());
  if(!out) {
    cppgm::driver_errors::ThrowInputOutput(
      "unable to open output file '" + invocation.outfile + "'");
  }
  abi_mangle::AbiMangleStats stats;
  const bool collect_stats = invocation.collect_stats;
  abi_mangle::mangle_fact_files_to_stream(invocation.inputs, out,
                                          collect_stats ? &stats : nullptr);
  out.close();
  if(!out) {
    cppgm::driver_errors::ThrowInputOutput(
      "unable to write output file '" + invocation.outfile + "'");
  }
  if(collect_stats) {
    cerr << "abimangle_stats source_files=" << stats.source_files
         << " source_bytes=" << stats.source_bytes
         << " cases=" << stats.cases
         << " records=" << stats.records
         << " canonical_types=" << stats.canonical_types
         << " canonical_arguments=" << stats.canonical_arguments
         << " canonical_expressions=" << stats.canonical_expressions
         << " typed_builtin_types=" << stats.typed_builtin_types
         << " text_builtin_types=" << stats.text_builtin_types
         << " typed_standard_substitutions="
         << stats.typed_standard_substitutions
         << " text_standard_substitutions="
         << stats.text_standard_substitutions
         << " typed_vendor_qualifiers=" << stats.typed_vendor_qualifiers
         << " text_vendor_qualifiers=" << stats.text_vendor_qualifiers
         << " typed_array_bounds=" << stats.typed_array_bounds
         << " text_array_bounds=" << stats.text_array_bounds
         << " typed_local_presentations="
         << stats.typed_local_presentations
         << " text_local_presentations=" << stats.text_local_presentations
         << " typed_type_source_names=" << stats.typed_type_source_names
         << " text_type_source_names=" << stats.text_type_source_names
         << " typed_type_tags=" << stats.typed_type_tags
         << " text_type_tags=" << stats.text_type_tags
         << " typed_argument_source_names="
         << stats.typed_argument_source_names
         << " text_argument_source_names="
         << stats.text_argument_source_names
         << " typed_local_source_names=" << stats.typed_local_source_names
         << " text_local_source_names=" << stats.text_local_source_names
         << " typed_literal_suffixes=" << stats.typed_literal_suffixes
         << " text_literal_suffixes=" << stats.text_literal_suffixes
         << " typed_main_contexts=" << stats.typed_main_contexts
         << " external_assembly_names=" << stats.external_assembly_names
         << " external_c_function_names=" << stats.external_c_function_names
         << " external_builtin_runtime_names="
         << stats.external_builtin_runtime_names
         << " external_c_variable_names=" << stats.external_c_variable_names
         << " external_global_tls_names=" << stats.external_global_tls_names
         << " canonical_cache_hits=" << stats.canonical_cache_hits
         << " definition_cache_hits=" << stats.definition_cache_hits
         << " path_components=" << stats.path_components
         << " substitution_lookups=" << stats.substitution_lookups
         << " substitution_hits=" << stats.substitution_hits
         << " substitution_entries=" << stats.substitution_entries
         << " isolated_entity_encodings=" << stats.isolated_entity_encodings
         << " output_bytes=" << stats.output_bytes
         << " peak_input_bytes=" << stats.peak_input_bytes
         << " parse_ns=" << stats.parse_nanoseconds
         << " encode_ns=" << stats.encode_nanoseconds
         << '\n';
  }
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char ** argv)
{
  try {
    return run_abimangle(argc, argv);
  } catch(const CompilerError & e) {
    cerr << "abimangle: " << e.what() << "\n";
    return EXIT_FAILURE;
  } catch(const exception & e) {
    cerr << "abimangle: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
}
