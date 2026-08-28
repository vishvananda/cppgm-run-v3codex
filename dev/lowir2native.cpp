// Student-facing scaffold for the PA29 `lowir2native` binary.

#include "support/exceptions.h"
#include "lowir/model/program.h"
#include "native/lowering/lowir_native.h"
#include "native/driver/stats_report.h"
#include "native/mir/model.h"
#include "support/tool_help_text.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace {

struct LowIR2NativeInvocation
{
  bool has_optimization_level = false;
  bool report_stats = false;
  int optimization_level = 0;
  string output_target;
  string outfile;
  string machine_ir_file;
  vector<string> srcfiles;
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

bool is_optimization_level(const string & arg, int & level)
{
  if(arg == "-O0") {
    level = 0;
    return true;
  }
  if(arg == "-O1") {
    level = 1;
    return true;
  }
  if(arg == "-O2") {
    level = 2;
    return true;
  }
  if(arg == "-O3") {
    level = 3;
    return true;
  }
  return false;
}

bool starts_with_dash(const string & arg)
{
  return !arg.empty() && arg[0] == '-';
}

bool has_batch_stdin_arg(const vector<string> & args)
{
  return has_arg(args, "--batch-stdin");
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

LowIR2NativeInvocation parse_lowir2native_invocation(const vector<string> & args)
{
  LowIR2NativeInvocation invocation;

  for(size_t i = 0; i < args.size(); ++i) {
    int optimization_level = 0;
    if(args[i] == "--stats") {
      invocation.report_stats = true;
      continue;
    }
    if(is_optimization_level(args[i], optimization_level)) {
      if(invocation.has_optimization_level) {
        throw logic_error("multiple optimization levels provided");
      }
      invocation.has_optimization_level = true;
      invocation.optimization_level = optimization_level;
      continue;
    }
    if(args[i] == "--target") {
      if(i + 1 >= args.size()) {
        throw logic_error("missing target after --target");
      }
      if(!invocation.output_target.empty()) {
        throw logic_error("multiple --target options provided");
      }
      invocation.output_target = args[++i];
      continue;
    }
    if(args[i] == "--dump-machine-ir" || args[i] == "--dump-native-plan") {
      if(i + 1 >= args.size()) {
        throw logic_error("missing output file after --dump-machine-ir");
      }
      if(!invocation.machine_ir_file.empty()) {
        throw logic_error("multiple machine IR dump paths provided");
      }
      invocation.machine_ir_file = args[++i];
      continue;
    }
    if(args[i] == "-o") {
      if(i + 1 >= args.size()) {
        throw logic_error("missing output file after -o");
      }
      if(!invocation.outfile.empty()) {
        throw logic_error("multiple output files provided");
      }
      invocation.outfile = args[++i];
      continue;
    }
    if(starts_with_dash(args[i])) {
      throw logic_error("unknown option: " + args[i]);
    }
    invocation.srcfiles.push_back(args[i]);
  }

  if((invocation.outfile.empty() && invocation.machine_ir_file.empty()) ||
     invocation.srcfiles.empty()) {
    throw logic_error("invalid usage");
  }

  return invocation;
}

int run_lowir2native_mode(const vector<string> & args)
{
  if(has_batch_stdin_arg(args)) {
    return run_not_implemented_batch_mode();
  }

  if(has_help_arg(args)) {
    cout << lowir2native_help_text();
    return EXIT_SUCCESS;
  }

  const LowIR2NativeInvocation invocation =
      parse_lowir2native_invocation(args);
  const bool report_stats = invocation.report_stats;
  const string target = invocation.output_target.empty() ? "linux" :
                                                     invocation.output_target;
  chrono::steady_clock::time_point parse_start;
  if(report_stats) parse_start = chrono::steady_clock::now();
  const lowir_model::LowirProgram lowir =
      lowir_model::parse_lowir_program_files(
        invocation.srcfiles,
        invocation.outfile.empty() ? lowir_model::LEP_ALLOW_HELPERS_ONLY :
                                     lowir_model::LEP_REQUIRE_ENTRY);
  chrono::steady_clock::time_point lower_start;
  if(report_stats) lower_start = chrono::steady_clock::now();
  lowir_native::Stats stats;
  const mir_model::MirProgram mir = lowir_native::lower_program(
      lowir, target, invocation.optimization_level, report_stats ? &stats : 0);
  chrono::steady_clock::time_point output_start;
  if(report_stats) output_start = chrono::steady_clock::now();
  if(!invocation.machine_ir_file.empty())
    mir_model::write_mir_program_file(invocation.machine_ir_file, mir);
  if(!invocation.outfile.empty())
    lowir_native::write_linux_executable(invocation.outfile, mir,
                                         report_stats ? &stats : 0);
  if(report_stats) {
    const chrono::steady_clock::time_point end = chrono::steady_clock::now();
    stats.lower_nanoseconds = chrono::duration_cast<chrono::nanoseconds>(
        output_start - lower_start).count();
    stats.write_nanoseconds = chrono::duration_cast<chrono::nanoseconds>(
        end - output_start).count();
    cerr << "lowir2native_stats"
         << " source_bytes=" << lowir.source_bytes
         << " tokens=" << lowir.token_count
         << " functions=" << stats.functions
         << " blocks=" << stats.blocks
         << " lowir_instructions=" << stats.lowir_instructions
         << " mir_instructions=" << stats.mir_instructions
         << " machine_opt_functions=" << stats.machine_opt_functions
         << " machine_opt_input=" << stats.machine_opt_input_instructions
         << " machine_opt_output=" << stats.machine_opt_output_instructions
         << " machine_opt_visits=" << stats.machine_opt_instruction_visits
         << " machine_opt_cfg_edges=" << stats.machine_opt_cfg_edge_visits
         << " machine_opt_pushes=" << stats.machine_opt_worklist_pushes
         << " machine_opt_rewrites=" << stats.machine_opt_rewrites
         << " machine_opt_identity_moves="
         << stats.machine_opt_identity_moves
         << " machine_opt_peak_bytes="
         << stats.machine_opt_peak_analysis_bytes
         << " live_location_scans=" << stats.live_location_scans
         << " live_location_value_visits=" << stats.live_location_value_visits
         << " live_location_alias_queries=" << stats.live_location_alias_queries
         << " live_location_updates=" << stats.live_location_updates
         << " spill_attempts=" << stats.spill_attempts
         << " spill_value_visits=" << stats.spill_value_visits
         << " spill_candidates=" << stats.spill_candidates
         << " spill_full_scan_fallbacks=" << stats.spill_full_scan_fallbacks
         << " spills=" << stats.spills
         << " temporary_frame_homes_created="
         << stats.temporary_frame_homes_created
         << " temporary_frame_homes_reused="
         << stats.temporary_frame_homes_reused
         << " planned_edge_register_retains="
         << stats.planned_edge_register_retains;
    lowir_native::report_code_shape_stats(cerr, stats);
    cerr
		 << " scratch_carried_reloads="
		 << stats.scratch_carried_reloads
         << " shared_storage_lifetime_extensions="
         << stats.shared_storage_lifetime_extensions
         << " reclaim_attempts=" << stats.reclaim_attempts
         << " reclaim_parameter_visits=" << stats.reclaim_parameter_visits
         << " reclaims=" << stats.reclaims
         << " immediate_stores_selected=" << stats.immediate_stores_selected
         << " memory_rhs_operations_selected="
         << stats.memory_rhs_operations_selected
         << " direct_zero_operations_selected="
         << stats.direct_zero_operations_selected
         << " direct_zero_stores_emitted="
         << stats.direct_zero_stores_emitted
         << " direct_zero_bytes=" << stats.direct_zero_bytes
         << " native_returns=" << stats.native_returns
         << " physical_epilogues=" << stats.physical_epilogues
         << " fixups=" << stats.fixups
         << " output_bytes=" << stats.output_bytes
         << " parse_ns=" << chrono::duration_cast<chrono::nanoseconds>(
              lower_start - parse_start).count()
         << " lower_ns=" << stats.lower_nanoseconds
         << " machine_opt_ns=" << stats.machine_opt_nanoseconds
         << " encode_ns=" << stats.encode_nanoseconds
         << " write_ns=" << stats.write_nanoseconds << '\n';
  }
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char ** argv)
{
  try
  {
    return run_lowir2native_mode(collect_args(argc, argv));
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
