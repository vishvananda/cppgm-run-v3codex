#include "lowir/cy86/converter.h"
#include "lowir/model/program.h"
#include "support/driver_errors.h"
#include "support/exception_types.h"
#include "support/tool_help_text.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

namespace {

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

void parse_output_invocation(const vector<string> & args,
                             string & outfile,
                             vector<string> & srcfiles)
{
  if(args.size() < 3 || args[0] != "-o") {
    cppgm::driver_errors::ThrowInvocation("invalid usage");
  }

  outfile = args[1];
  srcfiles.assign(args.begin() + 2, args.end());
}

int run_lowir2cy86_mode(const vector<string> & args)
{
  if(has_help_arg(args)) {
    cout << lowir2cy86_help_text();
    return EXIT_SUCCESS;
  }

  vector<string> invocation_args;
  bool report_stats = false;
  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == "--stats") report_stats = true;
    else invocation_args.push_back(args[i]);
  }
  string outfile;
  vector<string> srcfiles;
  parse_output_invocation(invocation_args, outfile, srcfiles);

  const chrono::steady_clock::time_point parse_start = chrono::steady_clock::now();
  const lowir_model::LowirProgram program = lowir_model::parse_lowir_program_files(srcfiles);
  const chrono::steady_clock::time_point lower_start = chrono::steady_clock::now();
  lowir_cy86::Stats stats;
  const string output = lowir_cy86::render_program(program, &stats);
  const chrono::steady_clock::time_point write_start = chrono::steady_clock::now();
  ofstream stream(outfile.c_str(), ios::out | ios::binary | ios::trunc);
  if(!stream) cppgm::driver_errors::ThrowInputOutput(
    "unable to open output file: " + outfile);
  stream.write(output.data(), static_cast<streamsize>(output.size()));
  if(!stream) cppgm::driver_errors::ThrowInputOutput(
    "unable to write output file: " + outfile);
  stream.close();

  if(report_stats) {
    const chrono::steady_clock::time_point end = chrono::steady_clock::now();
    cerr << "lowir2cy86_stats source_bytes=" << program.source_bytes
         << " tokens=" << program.token_count
         << " functions=" << stats.functions
         << " blocks=" << stats.blocks
         << " instructions=" << stats.instructions
         << " output_bytes=" << stats.output_bytes
         << " parse_ns=" << chrono::duration_cast<chrono::nanoseconds>(lower_start - parse_start).count()
         << " lower_ns=" << chrono::duration_cast<chrono::nanoseconds>(write_start - lower_start).count()
         << " write_ns=" << chrono::duration_cast<chrono::nanoseconds>(end - write_start).count()
         << '\n';
  }
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char ** argv)
{
  try
  {
    return run_lowir2cy86_mode(collect_args(argc, argv));
  }
  catch(const CompilerError & e)
  {
    cerr << "ERROR: " << e.what() << endl;
    return EXIT_FAILURE;
  }
  catch(const exception & e)
  {
    cerr << "ERROR: " << e.what() << endl;
    return EXIT_FAILURE;
  }
}
