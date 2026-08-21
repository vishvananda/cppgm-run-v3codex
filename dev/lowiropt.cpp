// Student-facing scaffold for the PA37 `lowiropt` binary.

#include "exceptions.h"
#include "lowir_driver_stats_report.h"
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
  if(invocation.report_stats)
    lowir_driver_stats_report::ReportOptimizer(cerr, string(), stats);
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
