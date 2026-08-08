// Student-facing scaffold for the PA10+ `cppgm++` binary.

#include "exceptions.h"
#include "pa10_syntax.h"
#include "pa11_semantic.h"
#include "pa12_semantic.h"
#include "pa15_lowering.h"
#include "tool_help_text.h"

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace {

enum class EmitMode
{
  None,
  Ast,
  Types,
  Semantics,
  LowIR,
};

enum class DriverMode
{
  Query,
  Preprocess,
  Compile,
  Link,
};

struct DriverInvocation
{
  DriverMode mode;

  DriverInvocation()
      : mode(DriverMode::Link)
  {
  }
};

vector<string> collect_args(int argc, char ** argv)
{
  vector<string> args;
  for(int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  return args;
}

bool has_arg(const vector<string> & args, const string & needle)
{
  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == needle) {
      return true;
    }
  }
  return false;
}

bool has_help_arg(const vector<string> & args)
{
  return has_arg(args, "--help") || has_arg(args, "-h");
}

bool starts_with(const string & value, const string & prefix)
{
  return value.size() >= prefix.size() &&
      value.compare(0, prefix.size(), prefix) == 0;
}

bool is_query_driver_flag(const string & arg)
{
  return arg == "--version" ||
      arg == "-v" ||
      arg == "-dumpmachine" ||
      arg == "-dumpversion" ||
      arg == "-print-search-dirs";
}

bool is_optimization_flag(const string & arg)
{
  return starts_with(arg, "-O");
}

bool is_debug_info_flag(const string & arg)
{
  return arg == "-g0" ||
      arg == "-gline-tables-only" ||
      arg == "-g" ||
      starts_with(arg, "-g");
}

bool is_benign_driver_flag(const string & arg)
{
  return arg == "-Wall" ||
      arg == "-Winvalid-offsetof" ||
      arg == "-pipe" ||
      arg == "-w" ||
      arg == "-pg" ||
      arg == "-pedantic" ||
      arg == "-pedantic-errors" ||
      starts_with(arg, "-W") ||
      starts_with(arg, "-f") ||
      starts_with(arg, "-m") ||
      starts_with(arg, "-std=");
}

logic_error missing_option_argument(const string & option,
                                    const string & expected)
{
  return logic_error("missing " + expected + " after " + option);
}

void consume_required_option_argument(const vector<string> & args,
                                      size_t & i,
                                      const string & option,
                                      const string & expected)
{
  if(i + 1 >= args.size()) {
    throw missing_option_argument(option, expected);
  }
  ++i;
}

bool consume_joined_or_separate_option(const vector<string> & args,
                                       size_t & i,
                                       const string & option,
                                       const string & expected)
{
  if(args[i] == option) {
    consume_required_option_argument(args, i, option, expected);
    return true;
  }
  if(starts_with(args[i], option) && args[i].size() > option.size()) {
    return true;
  }
  return false;
}

int run_not_implemented_batch_mode()
{
  string line;
  while(getline(cin, line)) {
    (void)line;
    cout << "EXIT_NOT_IMPLEMENTED" << endl;
  }
  return EXIT_SUCCESS;
}

void consume_emit_flag(vector<string> & args,
                       const string & flag,
                       EmitMode value,
                       EmitMode & out)
{
  vector<string> kept;
  bool found = false;
  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == flag) {
      found = true;
      continue;
    }
    kept.push_back(args[i]);
  }

  if(!found) {
    return;
  }

  if(out != EmitMode::None) {
    throw logic_error("multiple --emit-* options provided");
  }
  out = value;
  args.swap(kept);
}

EmitMode parse_emit_mode(vector<string> & args)
{
  EmitMode mode = EmitMode::None;
  consume_emit_flag(args, "--emit-ast", EmitMode::Ast, mode);
  consume_emit_flag(args, "--emit-types", EmitMode::Types, mode);
  consume_emit_flag(args, "--emit-semantics", EmitMode::Semantics, mode);
  consume_emit_flag(args, "--emit-lowir", EmitMode::LowIR, mode);
  return mode;
}

struct SourceOutputInvocation
{
  string output;
  vector<string> inputs;
};

SourceOutputInvocation parse_source_output_invocation(
                                    const vector<string> & args,
                                    bool allow_lowir_options)
{
  SourceOutputInvocation invocation;

  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == "-o") {
      consume_required_option_argument(args, i, "-o", "output file");
      invocation.output = args[i];
      continue;
    }
    if(allow_lowir_options &&
       (is_optimization_flag(args[i]) || is_debug_info_flag(args[i]))) {
      continue;
    }
    if(allow_lowir_options &&
       (args[i] == "--witness" || args[i] == "--witness-debug")) {
      consume_required_option_argument(args, i, args[i], "output file");
      continue;
    }
    if(args[i] == "-c" || args[i] == "-E" || is_query_driver_flag(args[i])) {
      throw logic_error("invalid usage");
    }
    if(starts_with(args[i], "-")) {
      throw logic_error("unsupported option in emit mode: " + args[i]);
    }
    invocation.inputs.push_back(args[i]);
  }

  if(invocation.output.empty() || invocation.inputs.empty()) {
    throw logic_error("invalid usage");
  }
  return invocation;
}

bool consume_preprocess_option(const vector<string> & args, size_t & i)
{
  if(consume_joined_or_separate_option(args, i, "-D", "macro definition")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-U", "macro name")) {
    return true;
  }
  if(args[i] == "-include") {
    consume_required_option_argument(args, i, "-include", "file");
    return true;
  }
  return false;
}

bool consume_search_option(const vector<string> & args, size_t & i)
{
  if(consume_joined_or_separate_option(args, i, "-I", "path")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-isystem", "path")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-L", "path")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-l", "library name")) {
    return true;
  }
  return false;
}

bool consume_dependency_option(const vector<string> & args, size_t & i)
{
  if(args[i] == "-MMD" || args[i] == "-MD" || args[i] == "-MP") {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-MF", "depfile path")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-MT", "target")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-MQ", "target")) {
    return true;
  }
  return false;
}

bool consume_toolchain_option(const vector<string> & args, size_t & i)
{
  if(is_debug_info_flag(args[i])) {
    return true;
  }
  if(is_optimization_flag(args[i])) {
    return true;
  }
  if(args[i] == "--target") {
    consume_required_option_argument(args, i, "--target", "target");
    return true;
  }
  if(starts_with(args[i], "--target=")) {
    if(args[i].size() == string("--target=").size()) {
      throw missing_option_argument("--target", "target");
    }
    return true;
  }
  if(args[i] == "-std") {
    consume_required_option_argument(args, i, "-std", "language standard");
    return true;
  }
  if(args[i] == "-stdlib") {
    consume_required_option_argument(args, i, "-stdlib", "standard library name");
    return true;
  }
  if(starts_with(args[i], "-stdlib=")) {
    return true;
  }
  if(args[i] == "-pthread") {
    throw logic_error("option not yet supported: -pthread");
  }
  return false;
}

DriverInvocation parse_driver_invocation(const vector<string> & args)
{
  if(args.empty()) {
    throw logic_error("invalid usage");
  }

  DriverInvocation invocation;
  if(is_query_driver_flag(args[0])) {
    if(args.size() != 1) {
      throw logic_error("query flag must be used as a direct invocation");
    }
    invocation.mode = DriverMode::Query;
    return invocation;
  }

  bool compile_only = false;
  bool preprocess_only = false;
  bool explicit_outfile = false;
  vector<string> inputs;

  for(size_t i = 0; i < args.size(); ++i) {
    if(is_query_driver_flag(args[i])) {
      throw logic_error("query flag must be used as a direct invocation");
    }
    if(args[i] == "-c") {
      compile_only = true;
      continue;
    }
    if(args[i] == "-E") {
      preprocess_only = true;
      continue;
    }
    if(args[i] == "-o") {
      consume_required_option_argument(args, i, "-o", "output file");
      explicit_outfile = true;
      continue;
    }
    if(consume_preprocess_option(args, i) ||
       consume_search_option(args, i) ||
       consume_dependency_option(args, i) ||
       consume_toolchain_option(args, i) ||
       is_benign_driver_flag(args[i])) {
      continue;
    }
    if(starts_with(args[i], "-")) {
      throw logic_error("unsupported driver option: " + args[i]);
    }
    inputs.push_back(args[i]);
  }

  if(compile_only && preprocess_only) {
    throw logic_error("cannot combine -c and -E");
  }
  if(inputs.empty()) {
    throw logic_error("invalid usage");
  }
  if((compile_only || preprocess_only) && explicit_outfile && inputs.size() != 1) {
    throw logic_error("cannot specify -o when generating multiple output files");
  }

  invocation.mode =
      preprocess_only ? DriverMode::Preprocess :
      compile_only ? DriverMode::Compile :
      DriverMode::Link;
  return invocation;
}

int run_unimplemented_mode(const char * feature,
                           const char * owner)
{
  (void)feature;
  (void)owner;
  throw NotImplementedException();
}

cppgm::PreprocessingOptions make_preprocessing_options()
{
  const time_t now = time(0);
  const tm * local = localtime(&now);
  if(!local) {
    throw runtime_error("unable to determine build time");
  }
  const char * text = asctime(local);
  if(!text) {
    throw runtime_error("unable to format build time");
  }
  const string formatted(text);
  if(formatted.size() < 24) {
    throw runtime_error("invalid asctime result");
  }
  cppgm::PreprocessingOptions options;
  options.build_date = formatted.substr(4, 7) + formatted.substr(20, 4);
  options.build_time = formatted.substr(11, 8);
  options.author = "Vishvananda Abrams";
  return options;
}

int run_emit_ast_mode(const vector<string> & args)
{
  const SourceOutputInvocation invocation =
      parse_source_output_invocation(args, false);
  ofstream output(invocation.output.c_str(), ios::out | ios::trunc);
  if(!output) {
    throw runtime_error("unable to open output file: " + invocation.output);
  }

  const cppgm::PreprocessingOptions options = make_preprocessing_options();

  output << invocation.inputs.size() << " translation units\n";
  for(size_t i = 0; i < invocation.inputs.size(); ++i) {
    const string & path = invocation.inputs[i];
    ifstream input(path.c_str(), ios::in | ios::binary);
    if(!input) {
      throw runtime_error("unable to open source file: " + path);
    }
    const string source((istreambuf_iterator<char>(input)),
                        istreambuf_iterator<char>());
    output << "start translation unit " << i + 1 << '\n';
    cppgm::SyntaxStats stats;
    cppgm::WriteSyntaxTranslationUnit(path, source, options, output,
        getenv("CPPGM_FRONTEND_STATS") ? &stats : 0);
    if(getenv("CPPGM_FRONTEND_STATS")) {
      cerr << "pa10_stats file=" << path
           << " tokens=" << stats.tokens
           << " syntax_nodes=" << stats.syntax_nodes
           << " syntax_edges=" << stats.syntax_edges
           << " syntax_output_bytes=" << stats.syntax_output_bytes
           << " max_syntax_depth=" << stats.max_syntax_depth
           << " checkpoints=" << stats.parser_checkpoints
           << " rollbacks=" << stats.parser_rollbacks
           << " template_probes=" << stats.template_argument_probes
           << " template_scans=" << stats.template_argument_scans
           << " template_cache_hits="
           << stats.template_argument_cache_hits
           << " template_scan_tokens=" << stats.template_argument_scan_tokens
           << " max_template_scan_tokens="
           << stats.max_template_argument_scan_tokens
           << " failed_template_scans="
           << stats.failed_template_argument_scans
           << " parser_fact_changes=" << stats.parser_fact_changes
           << " parser_storage_bytes=" << stats.parser_storage_bytes
           << " render_stack_storage_bytes="
           << stats.render_stack_storage_bytes
           << " peak_stage_storage_bytes=" << stats.peak_stage_storage_bytes
           << " parse_ns=" << stats.parse_nanoseconds
           << " render_ns=" << stats.render_nanoseconds
           << " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
    }
    output << "end translation unit\n";
  }
  if(!output) {
    throw runtime_error("unable to write output file: " + invocation.output);
  }
  return EXIT_SUCCESS;
}

int run_emit_types_mode(const vector<string> & args)
{
  const SourceOutputInvocation invocation =
      parse_source_output_invocation(args, false);
  ofstream output(invocation.output.c_str(), ios::out | ios::trunc);
  if(!output) {
    throw runtime_error("unable to open output file: " + invocation.output);
  }
  const cppgm::PreprocessingOptions options = make_preprocessing_options();
  output << invocation.inputs.size() << " translation units\n";
  for(size_t i = 0; i < invocation.inputs.size(); ++i) {
    const string & path = invocation.inputs[i];
    ifstream input(path.c_str(), ios::in | ios::binary);
    if(!input) {
      throw runtime_error("unable to open source file: " + path);
    }
    const string source((istreambuf_iterator<char>(input)),
                        istreambuf_iterator<char>());
    output << "start translation unit " << i + 1 << '\n';
    cppgm::TypeAnalysisStats stats;
    cppgm::WriteTypeTranslationUnit(path, source, options, output,
        getenv("CPPGM_FRONTEND_STATS") ? &stats : 0);
    if(getenv("CPPGM_FRONTEND_STATS")) {
      cerr << "pa11_stats file=" << path
           << " tokens=" << stats.tokens
           << " syntax_nodes=" << stats.syntax_nodes
           << " names=" << stats.interned_names
           << " canonical_types=" << stats.canonical_types
           << " scopes=" << stats.scopes
           << " declarations=" << stats.declarations
           << " lookup_queries=" << stats.lookup_queries
           << " lookup_scope_visits=" << stats.lookup_scope_visits
           << " lookup_edge_visits=" << stats.lookup_edge_visits
           << " name_index_probes=" << stats.name_index_probes
           << " type_index_probes=" << stats.type_index_probes
           << " using_index_probes=" << stats.using_index_probes
           << " rendered_type_nodes=" << stats.rendered_type_nodes
           << " max_scope_depth=" << stats.max_scope_depth
           << " render_stack_storage_bytes="
           << stats.render_stack_storage_bytes
           << " semantic_storage_bytes=" << stats.semantic_storage_bytes
           << " peak_stage_storage_bytes=" << stats.peak_stage_storage_bytes
           << " analysis_ns=" << stats.analysis_nanoseconds
           << " render_ns=" << stats.render_nanoseconds
           << " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
    }
    output << "end translation unit\n";
  }
  if(!output) {
    throw runtime_error("unable to write output file: " + invocation.output);
  }
  return EXIT_SUCCESS;
}

int run_emit_semantics_mode(const vector<string> & args)
{
  const SourceOutputInvocation invocation =
      parse_source_output_invocation(args, false);
  ofstream output(invocation.output.c_str(), ios::out | ios::trunc);
  if(!output) {
    throw runtime_error("unable to open output file: " + invocation.output);
  }
  const cppgm::PreprocessingOptions options = make_preprocessing_options();
  output << invocation.inputs.size() << " translation units\n";
  for(size_t i = 0; i < invocation.inputs.size(); ++i) {
    const string & path = invocation.inputs[i];
    ifstream input(path.c_str(), ios::in | ios::binary);
    if(!input) {
      throw runtime_error("unable to open source file: " + path);
    }
    const string source((istreambuf_iterator<char>(input)),
                        istreambuf_iterator<char>());
    output << "start translation unit " << i + 1 << '\n';
    cppgm::SemanticAnalysisStats stats;
    cppgm::WriteSemanticTranslationUnit(path, source, options, output,
        getenv("CPPGM_FRONTEND_STATS") ? &stats : 0);
    if(getenv("CPPGM_FRONTEND_STATS")) {
      cerr << "pa12_stats file=" << path
           << " tokens=" << stats.tokens
           << " syntax_nodes=" << stats.syntax_nodes
           << " semantic_nodes=" << stats.semantic_nodes
           << " semantic_edges=" << stats.semantic_edges
           << " names=" << stats.interned_names
           << " canonical_types=" << stats.canonical_types
           << " scopes=" << stats.scopes
           << " declarations=" << stats.declarations
           << " expressions=" << stats.expressions
           << " class_layouts=" << stats.class_layouts
           << " class_layout_member_visits="
           << stats.class_layout_member_visits
           << " class_zero_offset_subobject_visits="
           << stats.class_zero_offset_subobject_visits
           << " special_member_fact_lookups="
           << stats.special_member_fact_lookups
           << " special_member_subobject_visits="
           << stats.special_member_subobject_visits
           << " constructor_member_action_visits="
           << stats.constructor_member_action_visits
		   << " constructor_base_action_visits="
		   << stats.constructor_base_action_visits
		   << " destructor_subobject_action_visits="
		   << stats.destructor_subobject_action_visits
		   << " lexical_cleanup_action_visits="
		   << stats.lexical_cleanup_action_visits
		   << " unwind_cleanup_scope_visits="
		   << stats.unwind_cleanup_scope_visits
		   << " unwind_cleanup_action_visits="
		   << stats.unwind_cleanup_action_visits
		   << " namespace_object_actions="
		   << stats.namespace_object_actions
           << " lookup_queries=" << stats.lookup_queries
           << " lookup_scope_visits=" << stats.lookup_scope_visits
           << " lookup_edge_visits=" << stats.lookup_edge_visits
           << " lookup_cache_hits=" << stats.lookup_cache_hits
           << " lookup_cache_misses=" << stats.lookup_cache_misses
           << " lookup_cache_invalidations="
           << stats.lookup_cache_invalidations
           << " lookup_cache_dependency_edges="
           << stats.lookup_cache_dependency_edges
           << " lookup_cache_invalidation_pushes="
           << stats.lookup_cache_invalidation_pushes
           << " associated_scope_visits="
           << stats.associated_scope_visits
           << " associated_declaration_visits="
           << stats.associated_declaration_visits
           << " overload_candidates=" << stats.overload_candidates
           << " overload_order_comparisons="
           << stats.overload_order_comparisons
           << " conversion_checks=" << stats.conversion_checks
           << " call_conversion_cache_hits="
           << stats.call_conversion_cache_hits
           << " call_conversion_cache_misses="
           << stats.call_conversion_cache_misses
           << " function_signature_lookups="
           << stats.function_signature_lookups
           << " access_checks=" << stats.access_checks
           << " access_path_visits=" << stats.access_path_visits
           << " access_grant_probes=" << stats.access_grant_probes
           << " template_specialization_requests="
           << stats.template_specialization_requests
           << " template_specialization_cache_hits="
           << stats.template_specialization_cache_hits
           << " demand_worklist_pushes=" << stats.demand_worklist_pushes
           << " demanded_function_emissions="
           << stats.demanded_function_emissions
           << " default_constructor_emissions="
           << stats.default_constructor_emissions
           << " semantic_storage_bytes=" << stats.semantic_storage_bytes
           << " peak_stage_storage_bytes=" << stats.peak_stage_storage_bytes
           << " analysis_ns=" << stats.analysis_nanoseconds
           << " render_ns=" << stats.render_nanoseconds
           << " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
    }
    output << "end translation unit\n";
  }
  if(!output) {
    throw runtime_error("unable to write output file: " + invocation.output);
  }
  return EXIT_SUCCESS;
}

int run_emit_lowir_mode(const vector<string> & args)
{
	const SourceOutputInvocation invocation =
		parse_source_output_invocation(args, true);
	ofstream output(invocation.output.c_str(), ios::out | ios::trunc);
	if(!output) {
		throw runtime_error("unable to open output file: " + invocation.output);
	}
	const cppgm::PreprocessingOptions options = make_preprocessing_options();
	vector<cppgm::LowIRSource> sources;
	for(size_t i = 0; i < invocation.inputs.size(); ++i) {
		const string & path = invocation.inputs[i];
		ifstream input(path.c_str(), ios::in | ios::binary);
		if(!input) throw runtime_error("unable to open source file: " + path);
		sources.push_back(cppgm::LowIRSource(path,
			string((istreambuf_iterator<char>(input)), istreambuf_iterator<char>())));
	}
	cppgm::LowIRLoweringStats stats;
	cppgm::WriteLowIRProgram(sources, options, output,
		getenv("CPPGM_FRONTEND_STATS") ? &stats : 0);
	if(getenv("CPPGM_FRONTEND_STATS")) {
		const cppgm::SemanticAnalysisStats & semantic = stats.semantic;
		cerr << "pa15_stats"
			 << " source_bytes=" << stats.source_bytes
			 << " tokens=" << semantic.tokens
			 << " semantic_nodes=" << semantic.semantic_nodes
			 << " semantic_edges=" << semantic.semantic_edges
			 << " lowered_nodes=" << stats.lowered_nodes
			 << " class_layouts=" << semantic.class_layouts
			 << " class_layout_member_visits="
			 << semantic.class_layout_member_visits
			 << " class_zero_offset_subobject_visits="
			 << semantic.class_zero_offset_subobject_visits
			 << " special_member_fact_lookups="
			 << semantic.special_member_fact_lookups
			 << " special_member_subobject_visits="
			 << semantic.special_member_subobject_visits
			 << " constructor_member_action_visits="
			 << semantic.constructor_member_action_visits
			 << " constructor_base_action_visits="
			 << semantic.constructor_base_action_visits
			 << " destructor_subobject_action_visits="
			 << semantic.destructor_subobject_action_visits
			 << " lexical_cleanup_action_visits="
			 << semantic.lexical_cleanup_action_visits
			 << " unwind_cleanup_scope_visits="
			 << semantic.unwind_cleanup_scope_visits
			 << " unwind_cleanup_action_visits="
			 << semantic.unwind_cleanup_action_visits
			 << " namespace_object_actions="
			 << semantic.namespace_object_actions
			 << " lookup_queries=" << semantic.lookup_queries
			 << " lookup_scope_visits=" << semantic.lookup_scope_visits
			 << " lookup_edge_visits=" << semantic.lookup_edge_visits
			 << " lookup_cache_hits=" << semantic.lookup_cache_hits
			 << " lookup_cache_misses=" << semantic.lookup_cache_misses
			 << " lookup_cache_invalidations="
			 << semantic.lookup_cache_invalidations
			 << " lookup_cache_dependency_edges="
			 << semantic.lookup_cache_dependency_edges
			 << " lookup_cache_invalidation_pushes="
			 << semantic.lookup_cache_invalidation_pushes
			 << " associated_scope_visits="
			 << semantic.associated_scope_visits
			 << " associated_declaration_visits="
			 << semantic.associated_declaration_visits
			 << " overload_candidates=" << semantic.overload_candidates
			 << " overload_order_comparisons="
			 << semantic.overload_order_comparisons
			 << " conversion_checks=" << semantic.conversion_checks
			 << " call_conversion_cache_hits="
			 << semantic.call_conversion_cache_hits
			 << " call_conversion_cache_misses="
			 << semantic.call_conversion_cache_misses
			 << " function_signature_lookups="
			 << semantic.function_signature_lookups
			 << " access_checks=" << semantic.access_checks
			 << " access_path_visits=" << semantic.access_path_visits
			 << " access_grant_probes=" << semantic.access_grant_probes
			 << " template_specialization_requests="
			 << semantic.template_specialization_requests
			 << " template_specialization_cache_hits="
			 << semantic.template_specialization_cache_hits
			 << " demand_worklist_pushes=" << semantic.demand_worklist_pushes
			 << " demanded_function_emissions="
			 << semantic.demanded_function_emissions
			 << " default_constructor_emissions="
			 << semantic.default_constructor_emissions
			 << " functions=" << stats.functions
			 << " globals=" << stats.globals
			 << " blocks=" << stats.blocks
			 << " instructions=" << stats.instructions
			 << " binding_index_probes=" << stats.binding_index_probes
			 << " cleanup_dispatch_probes="
			 << stats.cleanup_dispatch_probes
			 << " cleanup_dispatch_cache_hits="
			 << stats.cleanup_dispatch_cache_hits
			 << " cleanup_dispatch_entries="
			 << stats.cleanup_dispatch_entries
			 << " conditional_lifetime_slots="
			 << stats.conditional_lifetime_slots
			 << " conditional_lifetime_marks="
			 << stats.conditional_lifetime_marks
			 << " typed_storage_bytes=" << stats.typed_storage_bytes
			 << " semantic_peak_stage_bytes="
			 << semantic.peak_stage_storage_bytes
			 << " output_bytes=" << stats.output_bytes
			 << " semantic_ns=" << semantic.analysis_nanoseconds
			 << " lowering_ns=" << stats.lowering_nanoseconds
			 << " render_ns=" << stats.render_nanoseconds << '\n';
	}
	return EXIT_SUCCESS;
}

int run_driver_mode(const vector<string> & args)
{
  const DriverInvocation invocation = parse_driver_invocation(args);
  switch(invocation.mode) {
  case DriverMode::Query:
    return run_unimplemented_mode("driver query mode", "PA34");
  case DriverMode::Preprocess:
    return run_unimplemented_mode("hosted preprocess driver mode (-E)", "PA34");
  case DriverMode::Compile:
    return run_unimplemented_mode("compile driver mode (-c)", "PA29");
  case DriverMode::Link:
    return run_unimplemented_mode("link driver mode", "PA29");
  }
  throw logic_error("unreachable driver mode");
}

int run_cppgm(const vector<string> & raw_args)
{
  if(has_arg(raw_args, "--batch-stdin")) {
    return run_not_implemented_batch_mode();
  }

  if(has_help_arg(raw_args)) {
    cout << cppgm_help_text();
    return EXIT_SUCCESS;
  }

  vector<string> args = raw_args;
  const EmitMode mode = parse_emit_mode(args);

  switch(mode) {
  case EmitMode::Ast:
    return run_emit_ast_mode(args);
  case EmitMode::Types:
    return run_emit_types_mode(args);
  case EmitMode::Semantics:
    return run_emit_semantics_mode(args);
  case EmitMode::LowIR:
    return run_emit_lowir_mode(args);
  case EmitMode::None:
    return run_driver_mode(args);
  }

  throw logic_error("unreachable emit mode");
}

}  // namespace

int main(int argc, char ** argv)
{
  try
  {
    return run_cppgm(collect_args(argc, argv));
  }
  catch(const NotImplementedException & e)
  {
    cerr << "ERROR: " << e.what() << endl;
    return CPPGM_EXIT_NOT_IMPLEMENTED;
  }
  catch(const exception & e)
  {
    cerr << "ERROR: " << e.what() << endl;
    return EXIT_FAILURE;
  }
}
