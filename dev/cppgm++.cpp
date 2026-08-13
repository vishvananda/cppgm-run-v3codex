// Student-facing scaffold for the PA10+ `cppgm++` binary.

#include "exceptions.h"
#include "pa10_syntax.h"
#include "pa11_semantic.h"
#include "pa12_semantic.h"
#include "pa15_lowering.h"
#include "pa30_lowir_adapter.h"
#include "pa30_object.h"
#include "pa30_elf_object.h"
#include "lowir_native.h"
#include "tool_help_text.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <utility>
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
  string output;
  string target;
  vector<string> inputs;
  vector<string> include_paths;
  vector<string> system_include_paths;
  vector<string> library_paths;
  vector<string> libraries;
  vector<string> predefined_macros;

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
      if(explicit_outfile) {
        throw logic_error("multiple output files provided");
      }
      explicit_outfile = true;
      invocation.output = args[i];
      continue;
    }
    if(args[i] == "-I" || args[i] == "-L" || args[i] == "-l" ||
       args[i] == "-D") {
      const string option = args[i];
      consume_required_option_argument(args, i, option,
          option == "-I" || option == "-L" ? "path" :
          option == "-l" ? "library name" : "macro definition");
      if(option == "-I") invocation.include_paths.push_back(args[i]);
      else if(option == "-L") invocation.library_paths.push_back(args[i]);
      else if(option == "-l") invocation.libraries.push_back(args[i]);
      else invocation.predefined_macros.push_back(args[i]);
      continue;
    }
    if(args[i] == "-isystem") {
      consume_required_option_argument(args, i, "-isystem", "path");
      invocation.system_include_paths.push_back(args[i]);
      continue;
    }
    if(starts_with(args[i], "-isystem") &&
       args[i].size() > string("-isystem").size()) {
      invocation.system_include_paths.push_back(
          args[i].substr(string("-isystem").size()));
      continue;
    }
    if(starts_with(args[i], "-I") && args[i].size() > 2) {
      invocation.include_paths.push_back(args[i].substr(2));
      continue;
    }
    if(starts_with(args[i], "-L") && args[i].size() > 2) {
      invocation.library_paths.push_back(args[i].substr(2));
      continue;
    }
    if(starts_with(args[i], "-l") && args[i].size() > 2) {
      invocation.libraries.push_back(args[i].substr(2));
      continue;
    }
    if(starts_with(args[i], "-D") && args[i].size() > 2) {
      invocation.predefined_macros.push_back(args[i].substr(2));
      continue;
    }
    if(args[i] == "--target") {
      consume_required_option_argument(args, i, "--target", "target");
      if(!invocation.target.empty()) throw logic_error("multiple targets provided");
      invocation.target = args[i];
      continue;
    }
    if(starts_with(args[i], "--target=")) {
      if(!invocation.target.empty()) throw logic_error("multiple targets provided");
      invocation.target = args[i].substr(string("--target=").size());
      if(invocation.target.empty()) throw missing_option_argument("--target", "target");
      continue;
    }
    if(consume_dependency_option(args, i) ||
       is_debug_info_flag(args[i]) || is_optimization_flag(args[i]) ||
       is_benign_driver_flag(args[i])) {
      continue;
    }
    if(starts_with(args[i], "-")) {
      throw logic_error("unsupported driver option: " + args[i]);
    }
    invocation.inputs.push_back(args[i]);
  }

  if(compile_only && preprocess_only) {
    throw logic_error("cannot combine -c and -E");
  }
  if(invocation.inputs.empty()) {
    throw logic_error("invalid usage");
  }
  if((compile_only || preprocess_only) && explicit_outfile && invocation.inputs.size() != 1) {
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

cppgm::PreprocessingOptions make_preprocessing_options();

string normalize_native_target(const string & target)
{
  if(target.empty() || target == "linux" ||
     target == "x86_64-unknown-linux-gnu" ||
     target == "x86_64-linux-gnu") {
    return "linux";
  }
  throw runtime_error("unsupported native target: " + target);
}

bool regular_file_exists(const string & path)
{
  struct stat data;
  return stat(path.c_str(), &data) == 0 && S_ISREG(data.st_mode);
}

string read_source_file(const string & path)
{
  ifstream input(path.c_str(), ios::in | ios::binary);
  if(!input) throw runtime_error("unable to open source file: " + path);
  return string(istreambuf_iterator<char>(input), istreambuf_iterator<char>());
}

cppgm::PreprocessingOptions make_driver_preprocessing_options(
    const DriverInvocation & invocation)
{
  cppgm::PreprocessingOptions options = make_preprocessing_options();
  options.include_search_paths = invocation.include_paths;
  options.system_include_search_paths = invocation.system_include_paths;
  options.predefined_macros = invocation.predefined_macros;
  return options;
}

cppgm::pa30::CompilerObject compile_source_object(
    const string & path,
    const DriverInvocation & invocation,
    const string & target)
{
	const bool collect_stats = getenv("CPPGM_DRIVER_STATS") != 0;
  vector<cppgm::LowIRSource> sources;
  const string source = read_source_file(path);
  sources.push_back(cppgm::LowIRSource(path, source));
  cppgm::LowIRLoweringStats stats;
  const cppgm::pa15_lowir_detail::TypedProgram typed =
      cppgm::BuildTypedLowIRProgram(sources,
          make_driver_preprocessing_options(invocation),
          collect_stats ? &stats : 0, true, true);
	chrono::steady_clock::time_point adapt_started;
	if(collect_stats) adapt_started = chrono::steady_clock::now();
  cppgm::pa30::CompilerObject object;
  object.target = target;
  object.lowir = cppgm::AdaptTypedLowIRForNative(typed);
	uint64_t adapt_nanoseconds = 0;
	if(collect_stats) adapt_nanoseconds = static_cast<uint64_t>(
		chrono::duration_cast<chrono::nanoseconds>(
			chrono::steady_clock::now() - adapt_started).count());
  object.lowir.source_bytes = source.size();
	if(collect_stats) {
		const cppgm::SemanticAnalysisStats & semantic = stats.semantic;
		cerr << "pa30_compile_stats"
			 << " file=" << path
			 << " source_bytes=" << stats.source_bytes
			 << " tokens=" << semantic.tokens
			 << " semantic_nodes=" << semantic.semantic_nodes
			 << " declarations=" << semantic.declarations
			 << " lookup_queries=" << semantic.lookup_queries
			 << " lookup_scope_visits=" << semantic.lookup_scope_visits
			 << " overload_candidates=" << semantic.overload_candidates
			 << " template_requests=" << semantic.template_specialization_requests
			 << " template_cache_hits=" << semantic.template_specialization_cache_hits
			 << " demand_pushes=" << semantic.demand_worklist_pushes
			 << " demanded_functions=" << semantic.demanded_function_emissions
			 << " functions=" << stats.functions
			 << " globals=" << stats.globals
			 << " instructions=" << stats.instructions
			 << " semantic_peak_bytes=" << semantic.peak_stage_storage_bytes
			 << " typed_bytes=" << stats.typed_storage_bytes
			 << " semantic_ns=" << semantic.analysis_nanoseconds
			 << " lowering_ns=" << stats.lowering_nanoseconds
			 << " adapt_ns=" << adapt_nanoseconds << '\n';
	}
  return object;
}

string find_library_object(const DriverInvocation & invocation,
                           const string & library)
{
  for(size_t i = 0; i < invocation.library_paths.size(); ++i) {
    string path = invocation.library_paths[i];
    if(!path.empty() && path[path.size() - 1] != '/') path.push_back('/');
    path += "lib" + library + ".o";
    if(regular_file_exists(path)) return path;
  }
  throw runtime_error("library not found: " + library);
}

int run_compile_driver(const DriverInvocation & invocation,
                       const string & target)
{
  if(invocation.inputs.size() != 1 || invocation.output.empty())
    throw logic_error("compile mode requires one input and -o");
  const cppgm::pa30::CompilerObject object =
      compile_source_object(invocation.inputs[0], invocation, target);
  const vector<unsigned char> compiler_payload =
      cppgm::pa30::SerializeCompilerObject(object);
  lowir_native::Stats native_stats;
  lowir_native::write_linux_relocatable(
      invocation.output, object.lowir, target, compiler_payload,
      getenv("CPPGM_DRIVER_STATS") ? &native_stats : 0);
  if(getenv("CPPGM_DRIVER_STATS")) {
    cerr << "pa31_object_stats"
         << " functions=" << native_stats.functions
         << " lowir_instructions=" << native_stats.lowir_instructions
         << " mir_instructions=" << native_stats.mir_instructions
         << " eh_region_states=" << native_stats.eh_region_states
         << " eh_region_edges=" << native_stats.eh_region_edges
         << " eh_call_sites=" << native_stats.eh_call_sites
         << " fixups=" << native_stats.fixups
         << " output_bytes=" << native_stats.output_bytes
         << " lower_ns=" << native_stats.lower_nanoseconds
         << " encode_ns=" << native_stats.encode_nanoseconds
         << " write_ns=" << native_stats.write_nanoseconds << '\n';
  }
  return EXIT_SUCCESS;
}

int run_link_driver(const DriverInvocation & invocation,
                    const string & target)
{
  if(invocation.output.empty()) throw logic_error("link mode requires -o");
  const bool collect_stats = getenv("CPPGM_DRIVER_STATS") != 0;
  vector<cppgm::pa30::CompilerObject> objects;
  vector<lowir_native::RelocatableObject> foreign_objects;
	chrono::steady_clock::time_point input_started;
	if(collect_stats) input_started = chrono::steady_clock::now();
  for(size_t i = 0; i < invocation.inputs.size(); ++i) {
    if(cppgm::pa30::IsCompilerObject(invocation.inputs[i]))
      objects.push_back(cppgm::pa30::ReadCompilerObject(invocation.inputs[i]));
    else
      objects.push_back(compile_source_object(invocation.inputs[i], invocation,
                                              target));
  }
  for(size_t i = 0; i < invocation.libraries.size(); ++i) {
    const string path = find_library_object(invocation, invocation.libraries[i]);
    if(cppgm::pa30::IsCompilerObject(path))
      objects.push_back(cppgm::pa30::ReadCompilerObject(path));
    else
      foreign_objects.push_back(cppgm::pa30::ReadElfRelocatableObject(
          path, foreign_objects.size()));
  }
	uint64_t input_nanoseconds = 0;
	if(collect_stats) input_nanoseconds = static_cast<uint64_t>(
		chrono::duration_cast<chrono::nanoseconds>(
			chrono::steady_clock::now() - input_started).count());
  cppgm::pa30::LinkStats link_stats;
  const lowir_model::LowirProgram lowir = cppgm::pa30::LinkCompilerObjects(
      std::move(objects), target,
      collect_stats ? &link_stats : 0);
  lowir_native::Stats native_stats;
  lowir_native::write_linux_executable(invocation.output, lowir, target,
      foreign_objects, collect_stats ? &native_stats : 0);
  if(collect_stats) {
    cerr << "pa30_driver_stats"
         << " objects=" << link_stats.objects
         << " symbols=" << link_stats.symbols
         << " symbol_probes=" << link_stats.symbol_probes
		 << " rename_probes=" << link_stats.rename_probes
         << " definitions=" << link_stats.definitions
         << " weak_coalesces=" << link_stats.coalesced_weak_definitions
         << " functions=" << native_stats.functions
         << " lowir_instructions=" << native_stats.lowir_instructions
         << " mir_instructions=" << native_stats.mir_instructions
		 << " output_bytes=" << native_stats.output_bytes
		 << " input_ns=" << input_nanoseconds
		 << " link_ns=" << link_stats.link_nanoseconds
		 << " lower_ns=" << native_stats.lower_nanoseconds
		 << " encode_ns=" << native_stats.encode_nanoseconds
		 << " write_ns=" << native_stats.write_nanoseconds << '\n';
  }
  return EXIT_SUCCESS;
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
		   << " virtual_base_layout_edge_visits="
		   << stats.virtual_base_layout_edge_visits
		   << " virtual_base_layout_facts="
		   << stats.virtual_base_layout_facts
		   << " virtual_base_layout_lookups="
		   << stats.virtual_base_layout_lookups
		   << " virtual_base_layout_probes="
		   << stats.virtual_base_layout_probes
		   << " direct_base_validation_visits="
		   << stats.direct_base_validation_visits
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
		   << " constructor_delegation_action_visits="
		   << stats.constructor_delegation_action_visits
		   << " destructor_subobject_action_visits="
		   << stats.destructor_subobject_action_visits
		   << " lexical_cleanup_action_visits="
		   << stats.lexical_cleanup_action_visits
		   << " unwind_cleanup_scope_visits="
		   << stats.unwind_cleanup_scope_visits
		   << " unwind_cleanup_action_visits="
		   << stats.unwind_cleanup_action_visits
		   << " initializer_list_lifetime_queries="
		   << stats.initializer_list_lifetime_queries
		   << " empty_constructor_chain_requests="
		   << stats.empty_constructor_chain_requests
		   << " empty_constructor_chain_cache_hits="
		   << stats.empty_constructor_chain_cache_hits
		   << " empty_constructor_chain_entity_visits="
		   << stats.empty_constructor_chain_entity_visits
		   << " empty_constructor_chain_dependency_edges="
		   << stats.empty_constructor_chain_dependency_edges
		   << " empty_destructor_chain_visits="
		   << stats.empty_destructor_chain_visits
		   << " empty_destructor_chain_cache_hits="
		   << stats.empty_destructor_chain_cache_hits
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
           << " base_path_queries=" << stats.base_path_queries
           << " base_path_cache_hits=" << stats.base_path_cache_hits
           << " base_path_cache_misses=" << stats.base_path_cache_misses
           << " base_path_edge_visits=" << stats.base_path_edge_visits
           << " virtual_base_path_visits="
           << stats.virtual_base_path_visits
           << " associated_scope_visits="
           << stats.associated_scope_visits
           << " associated_declaration_visits="
           << stats.associated_declaration_visits
           << " function_candidate_index_visits="
           << stats.function_candidate_index_visits
           << " overload_candidates=" << stats.overload_candidates
           << " overload_order_comparisons="
           << stats.overload_order_comparisons
           << " conversion_checks=" << stats.conversion_checks
           << " call_conversion_cache_hits="
           << stats.call_conversion_cache_hits
           << " call_conversion_cache_misses="
           << stats.call_conversion_cache_misses
           << " braced_fact_cache_hits=" << stats.braced_fact_cache_hits
           << " braced_fact_cache_misses=" << stats.braced_fact_cache_misses
           << " function_signature_lookups="
           << stats.function_signature_lookups
           << " polymorphic_classes=" << stats.polymorphic_classes
           << " virtual_slots=" << stats.virtual_slots
           << " virtual_signature_lookups="
           << stats.virtual_signature_lookups
		   << " virtual_overrides=" << stats.virtual_overrides
		   << " polymorphic_virtual_view_lookups="
		   << stats.polymorphic_virtual_view_lookups
		   << " polymorphic_virtual_view_merges="
		   << stats.polymorphic_virtual_view_merges
           << " virtual_slot_lookups=" << stats.virtual_slot_lookups
           << " vtable_demands=" << stats.vtable_demands
           << " access_checks=" << stats.access_checks
           << " access_path_visits=" << stats.access_path_visits
           << " access_grant_probes=" << stats.access_grant_probes
           << " template_specialization_requests="
           << stats.template_specialization_requests
           << " template_specialization_cache_hits="
           << stats.template_specialization_cache_hits
		   << " function_template_default_materializations="
		   << stats.function_template_default_materializations
		   << " function_template_default_request_cache_hits="
		   << stats.function_template_default_request_cache_hits
		   << " function_template_default_failure_cache_hits="
		   << stats.function_template_default_failure_cache_hits
		   << " function_template_exception_specification_requests="
		   << stats.function_template_exception_specification_requests
		   << " function_template_exception_specification_cache_hits="
		   << stats.function_template_exception_specification_cache_hits
		   << " function_template_exception_specification_evaluations="
		   << stats.function_template_exception_specification_evaluations
		   << " template_argument_list_requests="
		   << stats.template_argument_list_requests
		   << " template_argument_list_cache_hits="
		   << stats.template_argument_list_cache_hits
		   << " template_argument_list_index_probes="
		   << stats.template_argument_list_index_probes
		   << " template_partition_requests="
		   << stats.template_partition_requests
		   << " template_partition_cache_hits="
		   << stats.template_partition_cache_hits
		   << " template_partition_index_probes="
		   << stats.template_partition_index_probes
		   << " function_template_result_identity_requests="
		   << stats.function_template_result_identity_requests
		   << " function_template_result_identity_cache_hits="
		   << stats.function_template_result_identity_cache_hits
		   << " function_template_result_identity_index_probes="
		   << stats.function_template_result_identity_index_probes
		   << " function_template_result_identity_atom_visits="
		   << stats.function_template_result_identity_atom_visits
		   << " function_template_result_identity_syntax_visits="
		   << stats.function_template_result_identity_syntax_visits
		   << " function_template_result_identity_environment_probes="
		   << stats.function_template_result_identity_environment_probes
		   << " function_template_result_identity_alias_expansions="
		   << stats.function_template_result_identity_alias_expansions
		   << " template_partial_candidates="
		   << stats.template_partial_candidates
		   << " template_partial_order_comparisons="
		   << stats.template_partial_order_comparisons
		   << " template_partial_shape_materializations="
		   << stats.template_partial_shape_materializations
		   << " template_partial_shape_cache_hits="
		   << stats.template_partial_shape_cache_hits
			   << " template_partial_deduction_visits="
			   << stats.template_partial_deduction_visits
			   << " function_template_deduction_visits="
			   << stats.function_template_deduction_visits
			   << " lambda_closure_requests="
			   << stats.lambda_closure_requests
			   << " lambda_closure_cache_hits="
			   << stats.lambda_closure_cache_hits
			   << " lambda_capture_summary_requests="
			   << stats.lambda_capture_summary_requests
			   << " lambda_capture_summary_cache_hits="
			   << stats.lambda_capture_summary_cache_hits
			   << " lambda_capture_syntax_visits="
			   << stats.lambda_capture_syntax_visits
			   << " lambda_capture_name_uses="
			   << stats.lambda_capture_name_uses
	           << " constexpr_call_requests=" << stats.constexpr_call_requests
	           << " constexpr_call_cache_hits=" << stats.constexpr_call_cache_hits
	           << " constant_conversion_fact_requests="
	           << stats.constant_conversion_fact_requests
	           << " constant_conversion_fact_cache_hits="
	           << stats.constant_conversion_fact_cache_hits
	           << " constexpr_local_index_probes="
           << stats.constexpr_local_index_probes
           << " constexpr_scope_index_probes="
           << stats.constexpr_scope_index_probes
           << " constexpr_object_projection_visits="
           << stats.constexpr_object_projection_visits
           << " constexpr_step_visits=" << stats.constexpr_step_visits
           << " constexpr_max_depth=" << stats.constexpr_max_depth
           << " constexpr_peak_locals=" << stats.constexpr_peak_locals
           << " constexpr_scratch_peak_nodes="
           << stats.constexpr_scratch_peak_nodes
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
			 << " scopes=" << semantic.scopes
			 << " declarations=" << semantic.declarations
			 << " semantic_nodes=" << semantic.semantic_nodes
			 << " semantic_edges=" << semantic.semantic_edges
			 << " lowered_nodes=" << stats.lowered_nodes
			 << " class_layouts=" << semantic.class_layouts
			 << " class_layout_member_visits="
			 << semantic.class_layout_member_visits
			 << " virtual_base_layout_edge_visits="
			 << semantic.virtual_base_layout_edge_visits
			 << " virtual_base_layout_facts="
			 << semantic.virtual_base_layout_facts
			 << " virtual_base_layout_lookups="
			 << semantic.virtual_base_layout_lookups
			 << " virtual_base_layout_probes="
			 << semantic.virtual_base_layout_probes
			 << " direct_base_validation_visits="
			 << semantic.direct_base_validation_visits
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
			 << " constructor_delegation_action_visits="
			 << semantic.constructor_delegation_action_visits
			 << " destructor_subobject_action_visits="
			 << semantic.destructor_subobject_action_visits
			 << " lexical_cleanup_action_visits="
			 << semantic.lexical_cleanup_action_visits
			 << " unwind_cleanup_scope_visits="
			 << semantic.unwind_cleanup_scope_visits
			 << " unwind_cleanup_action_visits="
			 << semantic.unwind_cleanup_action_visits
			 << " enclosing_lifetime_queries="
			 << semantic.enclosing_lifetime_queries
			 << " initializer_list_lifetime_queries="
			 << semantic.initializer_list_lifetime_queries
			 << " temporary_dependency_visits="
			 << semantic.temporary_dependency_visits
			 << " materialized_demand_visits="
			 << semantic.materialized_demand_visits
			 << " nonthrowing_action_visits="
			 << semantic.nonthrowing_action_visits
			 << " runtime_initializer_visits="
			 << semantic.runtime_initializer_visits
			 << " static_constant_initializer_visits="
			 << semantic.static_constant_initializer_visits
			 << " static_constant_dependency_edges="
			 << semantic.static_constant_dependency_edges
			 << " empty_constructor_chain_requests="
			 << semantic.empty_constructor_chain_requests
			 << " empty_constructor_chain_cache_hits="
			 << semantic.empty_constructor_chain_cache_hits
			 << " empty_constructor_chain_entity_visits="
			 << semantic.empty_constructor_chain_entity_visits
			 << " empty_constructor_chain_dependency_edges="
			 << semantic.empty_constructor_chain_dependency_edges
			 << " empty_destructor_chain_visits="
			 << semantic.empty_destructor_chain_visits
			 << " empty_destructor_chain_cache_hits="
			 << semantic.empty_destructor_chain_cache_hits
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
			 << " base_path_queries=" << semantic.base_path_queries
			 << " base_path_cache_hits="
			 << semantic.base_path_cache_hits
			 << " base_path_cache_misses="
			 << semantic.base_path_cache_misses
			 << " base_path_edge_visits="
			 << semantic.base_path_edge_visits
			 << " virtual_base_path_visits="
			 << semantic.virtual_base_path_visits
			 << " associated_scope_visits="
			 << semantic.associated_scope_visits
			 << " associated_declaration_visits="
			 << semantic.associated_declaration_visits
			 << " function_candidate_index_visits="
			 << semantic.function_candidate_index_visits
			 << " overload_candidates=" << semantic.overload_candidates
			 << " overload_order_comparisons="
			 << semantic.overload_order_comparisons
			 << " conversion_checks=" << semantic.conversion_checks
			 << " call_conversion_cache_hits="
			 << semantic.call_conversion_cache_hits
			 << " call_conversion_cache_misses="
			 << semantic.call_conversion_cache_misses
			 << " braced_fact_cache_hits=" << semantic.braced_fact_cache_hits
			 << " braced_fact_cache_misses=" << semantic.braced_fact_cache_misses
			 << " function_signature_lookups="
			 << semantic.function_signature_lookups
			 << " polymorphic_classes=" << semantic.polymorphic_classes
			 << " virtual_slots=" << semantic.virtual_slots
			 << " virtual_signature_lookups="
			 << semantic.virtual_signature_lookups
			 << " virtual_overrides=" << semantic.virtual_overrides
			 << " polymorphic_virtual_view_lookups="
			 << semantic.polymorphic_virtual_view_lookups
			 << " polymorphic_virtual_view_merges="
			 << semantic.polymorphic_virtual_view_merges
			 << " virtual_slot_lookups="
			 << semantic.virtual_slot_lookups
			 << " vtable_demands=" << semantic.vtable_demands
			 << " access_checks=" << semantic.access_checks
			 << " access_path_visits=" << semantic.access_path_visits
			 << " access_grant_probes=" << semantic.access_grant_probes
			 << " template_specialization_requests="
			 << semantic.template_specialization_requests
			 << " template_specialization_cache_hits="
			 << semantic.template_specialization_cache_hits
			 << " function_template_default_materializations="
			 << semantic.function_template_default_materializations
			 << " function_template_default_request_cache_hits="
			 << semantic.function_template_default_request_cache_hits
			 << " function_template_default_failure_cache_hits="
			 << semantic.function_template_default_failure_cache_hits
			 << " function_template_exception_specification_requests="
			 << semantic.function_template_exception_specification_requests
			 << " function_template_exception_specification_cache_hits="
			 << semantic.function_template_exception_specification_cache_hits
			 << " function_template_exception_specification_evaluations="
			 << semantic.function_template_exception_specification_evaluations
			 << " template_argument_list_requests="
			 << semantic.template_argument_list_requests
			 << " template_argument_list_cache_hits="
			 << semantic.template_argument_list_cache_hits
			 << " template_argument_list_index_probes="
			 << semantic.template_argument_list_index_probes
			 << " template_partition_requests="
			 << semantic.template_partition_requests
			 << " template_partition_cache_hits="
			 << semantic.template_partition_cache_hits
			 << " template_partition_index_probes="
			 << semantic.template_partition_index_probes
			 << " function_template_result_identity_requests="
			 << semantic.function_template_result_identity_requests
			 << " function_template_result_identity_cache_hits="
			 << semantic.function_template_result_identity_cache_hits
			 << " function_template_result_identity_index_probes="
			 << semantic.function_template_result_identity_index_probes
			 << " function_template_result_identity_atom_visits="
			 << semantic.function_template_result_identity_atom_visits
			 << " function_template_result_identity_syntax_visits="
			 << semantic.function_template_result_identity_syntax_visits
			 << " function_template_result_identity_environment_probes="
			 << semantic.function_template_result_identity_environment_probes
			 << " function_template_result_identity_alias_expansions="
			 << semantic.function_template_result_identity_alias_expansions
			 << " template_partial_candidates="
			 << semantic.template_partial_candidates
			 << " template_partial_order_comparisons="
			 << semantic.template_partial_order_comparisons
			 << " template_partial_shape_materializations="
			 << semantic.template_partial_shape_materializations
			 << " template_partial_shape_cache_hits="
			 << semantic.template_partial_shape_cache_hits
				 << " template_partial_deduction_visits="
				 << semantic.template_partial_deduction_visits
				 << " function_template_deduction_visits="
				 << semantic.function_template_deduction_visits
				 << " lambda_closure_requests="
				 << semantic.lambda_closure_requests
				 << " lambda_closure_cache_hits="
				 << semantic.lambda_closure_cache_hits
				 << " lambda_capture_summary_requests="
				 << semantic.lambda_capture_summary_requests
				 << " lambda_capture_summary_cache_hits="
				 << semantic.lambda_capture_summary_cache_hits
				 << " lambda_capture_syntax_visits="
				 << semantic.lambda_capture_syntax_visits
				 << " lambda_capture_name_uses="
				 << semantic.lambda_capture_name_uses
				 << " constexpr_call_requests="
			 << semantic.constexpr_call_requests
				 << " constexpr_call_cache_hits="
			 << semantic.constexpr_call_cache_hits
			 << " constant_conversion_fact_requests="
			 << semantic.constant_conversion_fact_requests
			 << " constant_conversion_fact_cache_hits="
			 << semantic.constant_conversion_fact_cache_hits
			 << " constexpr_local_index_probes="
			 << semantic.constexpr_local_index_probes
			 << " constexpr_scope_index_probes="
			 << semantic.constexpr_scope_index_probes
			 << " constexpr_object_projection_visits="
			 << semantic.constexpr_object_projection_visits
			 << " constexpr_step_visits="
			 << semantic.constexpr_step_visits
			 << " constexpr_max_depth="
			 << semantic.constexpr_max_depth
			 << " constexpr_peak_locals="
			 << semantic.constexpr_peak_locals
			 << " constexpr_scratch_peak_nodes="
			 << semantic.constexpr_scratch_peak_nodes
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
			 << " slot_implicit_object_fact_reads="
			 << stats.slot_implicit_object_fact_reads
			 << " virtual_calls=" << stats.virtual_calls
			 << " vptr_stores=" << stats.vptr_stores
			 << " virtual_base_boundary_scan_nodes="
			 << stats.virtual_base_boundary_scan_nodes
			 << " virtual_base_boundary_facts="
			 << stats.virtual_base_boundary_facts
			 << " virtual_base_call_arguments="
			 << stats.virtual_base_call_arguments
			 << " virtual_base_boundary_binding_steps="
			 << stats.virtual_base_boundary_binding_steps
			 << " virtual_base_boundary_binding_cache_hits="
			 << stats.virtual_base_boundary_binding_cache_hits
			 << " virtual_base_boundary_binding_table_growth="
			 << stats.virtual_base_boundary_binding_table_growth
			 << " vtable_offset_rows=" << stats.vtable_offset_rows
			 << " vtable_slots=" << stats.vtable_slots
			 << " vtable_thunk_requests=" << stats.vtable_thunk_requests
			 << " vtable_thunk_cache_hits="
			 << stats.vtable_thunk_cache_hits
			 << " vtable_thunk_index_probes="
			 << stats.vtable_thunk_index_probes
			 << " deleting_destructors=" << stats.deleting_destructors
			 << " rtti_graph_nodes_visited="
			 << stats.rtti_graph_nodes_visited
			 << " rtti_demand_requests=" << stats.rtti_demand_requests
			 << " rtti_types_demanded=" << stats.rtti_types_demanded
			 << " rtti_symbol_lookups=" << stats.rtti_symbol_lookups
			 << " rtti_base_dependency_visits="
			 << stats.rtti_base_dependency_visits
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
				<< " branch_cleanup_actions="
				<< stats.branch_cleanup_actions
				<< " statement_scheduler_entries="
				<< stats.statement_scheduler_entries
				<< " statement_scheduler_nested_entries="
				<< stats.statement_scheduler_nested_entries
				<< " statement_scheduler_tasks="
				<< stats.statement_scheduler_tasks
				<< " statement_scheduler_peak_tasks="
				<< stats.statement_scheduler_peak_tasks
				<< " exception_selector_resets="
			 << stats.exception_selector_resets
			 << " exception_selector_table_growth="
			 << stats.exception_selector_table_growth
			 << " exception_selector_assignments="
			 << stats.exception_selector_assignments
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
  const string target = normalize_native_target(invocation.target);
  switch(invocation.mode) {
  case DriverMode::Query:
    return run_unimplemented_mode("driver query mode", "PA34");
  case DriverMode::Preprocess:
    return run_unimplemented_mode("hosted preprocess driver mode (-E)", "PA34");
  case DriverMode::Compile:
    return run_compile_driver(invocation, target);
  case DriverMode::Link:
    return run_link_driver(invocation, target);
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
