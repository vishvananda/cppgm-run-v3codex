// Student-facing scaffold for the PA37 `lowiropt` binary.

#include "exceptions.h"
#include "lowir_model.h"
#include "lowir_opt.h"
#include "tool_help_text.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace {

struct LowIROptInvocation
{
  bool has_optimization_level = false;
  bool report_stats = false;
  int optimization_level = 0;
  string outfile;
  vector<string> inputs;
};

vector<string> collect_args(int argc, char ** argv)
{
  vector<string> args;
  for(int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  return args;
}

bool has_help_arg(const vector<string> & args)
{
  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == "--help" || args[i] == "-h") {
      return true;
    }
  }
  return false;
}

bool has_batch_stdin_arg(const vector<string> & args)
{
  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == "--batch-stdin") {
      return true;
    }
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
  return false;
}

bool starts_with_dash(const string & arg)
{
  return !arg.empty() && arg[0] == '-';
}

LowIROptInvocation parse_lowiropt_invocation(const vector<string> & args)
{
  LowIROptInvocation invocation;

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
    invocation.inputs.push_back(args[i]);
  }

  if(!invocation.has_optimization_level ||
     invocation.outfile.empty() ||
     invocation.inputs.empty()) {
    throw logic_error("invalid usage");
  }

  return invocation;
}

int run_lowiropt_mode(const vector<string> & args)
{
  if(has_batch_stdin_arg(args)) {
    return run_not_implemented_batch_mode();
  }

  if(has_help_arg(args)) {
    cout << lowiropt_help_text();
    return EXIT_SUCCESS;
  }

  const LowIROptInvocation invocation = parse_lowiropt_invocation(args);
  lowir_model::LowirProgram program = lowir_model::parse_lowir_program_files(
      invocation.inputs, lowir_model::LEP_ALLOW_HELPERS_ONLY);
  lowir_opt::Stats stats;
  lowir_opt::optimize(program, invocation.optimization_level,
                      invocation.report_stats ? &stats : 0);
  lowir_model::write_lowir_program_file(invocation.outfile, program);
  if(invocation.report_stats) {
    cerr << "pa37_opt_stats"
         << " functions=" << stats.functions
         << " input_instructions=" << stats.input_instructions
         << " output_instructions=" << stats.output_instructions
         << " instruction_visits=" << stats.instruction_visits
         << " block_visits=" << stats.block_visits
         << " cfg_edge_visits=" << stats.cfg_edge_visits
         << " worklist_pushes=" << stats.worklist_pushes
         << " dataflow_updates=" << stats.dataflow_updates
         << " inline_direct_edges=" << stats.inline_direct_edges
         << " inline_sccs=" << stats.inline_sccs
         << " inline_recursive_functions="
         << stats.inline_recursive_functions
         << " inline_call_visits=" << stats.inline_call_visits
         << " inline_candidate_calls=" << stats.inline_candidate_calls
         << " inline_calls=" << stats.inline_calls
         << " inline_cloned_instructions="
         << stats.inline_cloned_instructions
         << " inline_input_instructions="
         << stats.inline_input_instructions
         << " inline_output_instructions="
         << stats.inline_output_instructions
         << " inline_reject_recursive=" << stats.inline_reject_recursive
         << " inline_reject_no_inline=" << stats.inline_reject_no_inline
         << " inline_reject_argument_shape="
         << stats.inline_reject_argument_shape
         << " inline_reject_variadic=" << stats.inline_reject_variadic
         << " inline_reject_callee_size="
         << stats.inline_reject_callee_size
         << " inline_reject_prepared_size="
         << stats.inline_reject_prepared_size
         << " inline_reject_landing=" << stats.inline_reject_landing
         << " inline_reject_eh_visibility="
         << stats.inline_reject_eh_visibility
         << " inline_reject_eh_unwind="
         << stats.inline_reject_eh_unwind
         << " inline_reject_callee_eh="
         << stats.inline_reject_callee_eh
         << " inline_reachable_functions="
         << stats.inline_reachable_functions
         << " inline_pruned_functions=" << stats.inline_pruned_functions
         << " inline_unreachable_weak_functions="
         << stats.inline_unreachable_weak_functions
         << " inline_unreachable_internal_functions="
         << stats.inline_unreachable_internal_functions
         << " inline_retained_external_strong="
         << stats.inline_retained_external_strong
         << " inline_retained_address_or_relocation="
         << stats.inline_retained_address_or_relocation
         << " inline_retained_direct_call="
         << stats.inline_retained_direct_call
         << " inline_retained_lifecycle="
         << stats.inline_retained_lifecycle
         << " inline_retained_object_output_root="
         << stats.inline_retained_object_output_root
         << " inline_retained_object_output_root_weak="
         << stats.inline_retained_object_output_root_weak
         << " inline_retained_object_output_root_internal="
         << stats.inline_retained_object_output_root_internal
         << " inline_changed_callers=" << stats.inline_changed_callers
         << " inline_eh_blocked_records="
         << stats.inline_eh_blocked_records
         << " inline_revisited_callers=" << stats.inline_revisited_callers
         << " budget_skips=" << stats.budget_skips
         << " rewrites=" << stats.rewrites
              << " simplify_runs=" << stats.simplify_runs
              << " simplify_changes=" << stats.simplify_changes
              << " simplify_candidate_skips="
              << stats.simplify_candidate_skips
              << " dce_runs=" << stats.dce_runs
              << " dce_changes=" << stats.dce_changes
              << " dce_candidate_skips=" << stats.dce_candidate_skips
              << " cfg_runs=" << stats.cfg_runs
              << " cfg_changes=" << stats.cfg_changes
              << " slot_runs=" << stats.slot_runs
              << " slot_changes=" << stats.slot_changes
              << " forward_slot_runs=" << stats.forward_slot_runs
              << " forward_slot_changes=" << stats.forward_slot_changes
              << " local_slot_runs=" << stats.local_slot_runs
              << " local_slot_changes=" << stats.local_slot_changes
              << " remove_slot_runs=" << stats.remove_slot_runs
              << " remove_slot_changes=" << stats.remove_slot_changes
              << " promote_slot_runs=" << stats.promote_slot_runs
              << " promote_slot_changes=" << stats.promote_slot_changes
              << " dead_store_runs=" << stats.dead_store_runs
              << " dead_store_changes=" << stats.dead_store_changes
              << " cleanup_resume_runs=" << stats.cleanup_resume_runs
              << " cleanup_resume_block_visits="
              << stats.cleanup_resume_block_visits
              << " cleanup_resume_blocks_removed="
              << stats.cleanup_resume_blocks_removed
              << " cleanup_tail_runs=" << stats.cleanup_tail_runs
              << " cleanup_tail_block_visits="
              << stats.cleanup_tail_block_visits
              << " cleanup_tail_groups_shared="
              << stats.cleanup_tail_groups_shared
              << " cleanup_tail_blocks_rewritten="
              << stats.cleanup_tail_blocks_rewritten
              << " cleanup_tail_instructions_removed="
              << stats.cleanup_tail_instructions_removed
         << " inline_ns=" << stats.inline_nanoseconds
         << " simplify_ns=" << stats.simplify_nanoseconds
         << " dce_ns=" << stats.dce_nanoseconds
         << " cfg_ns=" << stats.cfg_nanoseconds
              << " slot_ns=" << stats.slot_nanoseconds
              << " forward_slot_ns=" << stats.forward_slot_nanoseconds
              << " local_slot_ns=" << stats.local_slot_nanoseconds
              << " remove_slot_ns=" << stats.remove_slot_nanoseconds
              << " promote_slot_ns=" << stats.promote_slot_nanoseconds
              << " dead_store_ns=" << stats.dead_store_nanoseconds
              << " cleanup_resume_ns=" << stats.cleanup_resume_nanoseconds
              << " cleanup_tail_ns=" << stats.cleanup_tail_nanoseconds
         << " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
  }
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char ** argv)
{
  try
  {
    return run_lowiropt_mode(collect_args(argc, argv));
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
