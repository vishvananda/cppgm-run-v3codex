#include "lowir_native.h"

#include "lowir_force_inline.h"
#include "lowir_native_eh.h"
#include "lowir_native_opt.h"
#include "lowir_native_program.h"
#include "lowir_native_session.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace lowir_native {

session_detail::StringIdentityMap::StringIdentityMap(
    const lowir_model::StringPool & source)
  : source_(source), mapped_(source.size() + 1),
    strings_(new lowir_model::StringPool)
{
}

lowir_model::StringId session_detail::StringIdentityMap::map(
    lowir_model::StringId source_literal)
{
  if(!source_literal.valid()) return lowir_model::StringId();
  const std::uint32_t source_index = source_literal;
  if(source_index >= mapped_.size())
    throw std::logic_error("invalid LowIR literal identity");
  if(mapped_[source_index].valid()) return mapped_[source_index];
  const lowir_model::StringId target = strings_->intern(
    source_.get(source_literal));
  mapped_[source_index] = target;
  return target;
}

lowir_model::PresentationName session_detail::StringIdentityMap::map(
    lowir_model::PresentationName source_name)
{
  if(!source_name.valid()) return lowir_model::PresentationName();
  if(source_name.generated()) return source_name;
  return lowir_model::PresentationName::pooled(
    map(source_name.spelling()), source_name.preserves_copy());
}

lowir_model::StringId session_detail::StringIdentityMap::intern(
    const std::string & spelling)
{
  return strings_->intern(spelling);
}

std::shared_ptr<lowir_model::StringPool>
session_detail::StringIdentityMap::strings() const
{
  return strings_;
}

struct ProgramLoweringSession::Impl
{
  std::unique_ptr<lowir_model::LowirProgram> rewritten;
  const lowir_model::LowirProgram & source;
  session_detail::StringIdentityMap strings;
  Stats * stats;
  int optimization_level;
  mir_model::MirProgram shell;
  std::vector<lowir_model::SymbolId> tls_wrappers;
  std::vector<unsigned char> pointer_globals;
  abi::FunctionSignatureIndex signatures;

  Impl(const lowir_model::LowirProgram & program, const std::string & target,
       int level, Stats * output_stats)
    : rewritten(force_inline::rewrite_program(program)),
      source(rewritten ? *rewritten : program), strings(source.strings),
      stats(output_stats),
      optimization_level(level)
  {
    std::chrono::steady_clock::time_point started;
    if(stats) started = std::chrono::steady_clock::now();
    if(target != "linux")
      throw std::runtime_error("unsupported native target: " + target);
	shell.target = target;
	shell.symbol_names.reserve(source.symbol_names.size());
	for(std::size_t i = 0; i < source.symbol_names.size(); ++i)
	  shell.symbol_names.push_back(strings.map(source.symbol_names[i]));
	shell.strings = strings.strings();
	pointer_globals.assign(source.symbol_names.size(), 0);
	signatures.resize(source.symbol_names.size());
    eh::plan_program(source, shell);
    program_lowering::lower_startup(source, shell);
    tls_wrappers = program_lowering::tls_wrapper_index(source);
    for(std::size_t i = 0; i < source.global_declarations.size(); ++i)
      if(source.global_declarations[i].has_type &&
         source.global_declarations[i].type.kind == lowir_model::LTK_PTR)
		pointer_globals[source.global_declarations[i].symbol] = 1;
    for(std::size_t i = 0; i < source.globals.size(); ++i)
      if(!source.globals[i].structured &&
         source.globals[i].type.kind == lowir_model::LTK_PTR)
		pointer_globals[source.globals[i].symbol] = 1;
    for(std::size_t i = 0; i < source.globals.size(); ++i) {
      mir_model::MirGlobalDefinition global =
        program_lowering::lower_global(
          source, source.globals[i], *shell.strings);
      const lowir_model::SymbolId wrapper =
        tls_wrappers[source.globals[i].symbol];
      if(wrapper.valid())
        global.thread_local_wrapper_symbol = wrapper;
      shell.globals.push_back(std::move(global));
    }
    IndexSignatures();
    shell.object_aliases.reserve(source.object_aliases.size());
    for(std::size_t i = 0; i < source.object_aliases.size(); ++i) {
      mir_model::MirObjectAlias alias;
      if(!source.object_aliases[i].object_symbol.empty())
        alias.object_symbol = strings.intern(
          source.object_aliases[i].object_symbol);
      alias.target = source.object_aliases[i].target_id;
      shell.object_aliases.push_back(alias);
    }
    if(stats) RecordProgramStats(started);
  }

  void IndexSignatures()
  {
    for(std::size_t i = 0; i < source.function_declarations.size(); ++i) {
      abi::FunctionSignature signature;
      signature.params = &source.function_declarations[i].params;
      signature.return_type = &source.function_declarations[i].return_type;
      signature.boundary = &source.function_declarations[i].boundary;
      signatures[source.function_declarations[i].symbol] = signature;
    }
    for(std::size_t i = 0; i < source.functions.size(); ++i) {
      abi::FunctionSignature signature;
      signature.params = &source.functions[i].params;
      signature.return_type = &source.functions[i].return_type;
      signature.boundary = &source.functions[i].boundary;
      signatures[source.functions[i].symbol] = signature;
    }
  }

  void RecordProgramStats(const std::chrono::steady_clock::time_point & started)
  {
    stats->functions = source.functions.size();
    stats->mir_instructions += shell.startup.size();
    for(std::size_t i = 0; i < source.functions.size(); ++i) {
      stats->blocks += source.functions[i].blocks.size();
      for(std::size_t j = 0; j < source.functions[i].blocks.size(); ++j)
        stats->lowir_instructions +=
          source.functions[i].blocks[j].instructions.size();
    }
    stats->lower_nanoseconds += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
  }

  mir_model::MirFunction LowerFunction(std::size_t index)
  {
    if(index >= source.functions.size())
      throw std::logic_error("native function index is out of bounds");
    std::chrono::steady_clock::time_point started;
    if(stats) started = std::chrono::steady_clock::now();
    mir_model::MirFunction result = session_detail::lower_native_function(
      source, source.functions[index], pointer_globals, tls_wrappers,
      signatures, strings, stats);
    machine_opt::Stats opt_stats;
    machine_opt::optimize_function(result, optimization_level,
                                   stats ? &opt_stats : 0);
    if(stats) {
      stats->machine_opt_functions += opt_stats.functions;
      stats->machine_opt_input_instructions += opt_stats.input_instructions;
      stats->machine_opt_output_instructions += opt_stats.output_instructions;
      stats->machine_opt_instruction_visits += opt_stats.instruction_visits;
      stats->machine_opt_cfg_edge_visits += opt_stats.cfg_edge_visits;
      stats->machine_opt_worklist_pushes += opt_stats.worklist_pushes;
      stats->machine_opt_rewrites += opt_stats.rewrites;
      stats->machine_opt_peak_analysis_bytes = std::max(
        stats->machine_opt_peak_analysis_bytes,
        opt_stats.peak_analysis_bytes);
      stats->machine_opt_nanoseconds += opt_stats.elapsed_nanoseconds;
      for(std::size_t i = 0; i < result.blocks.size(); ++i)
        stats->mir_instructions += result.blocks[i].instructions.size();
      stats->lower_nanoseconds += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - started).count());
    }
    return result;
  }
};

ProgramLoweringSession::ProgramLoweringSession(
    const lowir_model::LowirProgram & program, const std::string & target,
    int optimization_level, Stats * stats)
  : impl_(new Impl(program, target, optimization_level, stats)) {}

ProgramLoweringSession::~ProgramLoweringSession()
{
  delete impl_;
}

std::size_t ProgramLoweringSession::function_count() const
{
  return impl_->source.functions.size();
}

mir_model::MirFunction ProgramLoweringSession::lower_function(std::size_t index)
{
  return impl_->LowerFunction(index);
}

mir_model::MirProgram ProgramLoweringSession::take_program_shell()
{
  return std::move(impl_->shell);
}

mir_model::MirProgram lower_program(const lowir_model::LowirProgram & program,
                                    const std::string & target,
                                    int optimization_level,
                                    Stats * stats)
{
  ProgramLoweringSession lowering(program, target, optimization_level, stats);
  mir_model::MirProgram result = lowering.take_program_shell();
  result.functions.reserve(lowering.function_count());
  for(std::size_t i = 0; i < lowering.function_count(); ++i)
    result.functions.push_back(lowering.lower_function(i));
  return result;
}

}  // namespace lowir_native
