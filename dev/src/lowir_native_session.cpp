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

namespace {

std::size_t mir_instruction_storage_bytes(
	const mir_model::MirInstruction & instruction)
{
	return instruction.operands.capacity() * sizeof(mir_model::MirOperand);
}

std::size_t mir_function_storage_bytes(const mir_model::MirFunction & function)
{
	std::size_t bytes = function.params.capacity() *
			sizeof(mir_model::MirParamBinding) +
		function.callee_saved_regs.capacity() * sizeof(X64Register) +
		function.frame_bindings.capacity() * sizeof(mir_model::MirFrameBinding) +
		function.debug_variables.capacity() * sizeof(mir_model::MirDebugVariable) +
		function.host_eh_clauses.capacity() *
			sizeof(std::vector<mir_model::MirHostEhClause>) +
		function.block_labels.capacity() * sizeof(lowir_model::StringId) +
		function.blocks.capacity() * sizeof(mir_model::MirBlock);
	for(std::size_t i = 0; i < function.debug_variables.size(); ++i)
		bytes += function.debug_variables[i].ranges.capacity() *
			sizeof(mir_model::DebugVariable::Range);
	for(std::size_t i = 0; i < function.host_eh_clauses.size(); ++i) {
		bytes += function.host_eh_clauses[i].capacity() *
			sizeof(mir_model::MirHostEhClause);
		for(std::size_t j = 0; j < function.host_eh_clauses[i].size(); ++j)
			bytes += function.host_eh_clauses[i][j].filter_type_symbols.capacity() *
				sizeof(lowir_model::SymbolId);
	}
	for(std::size_t b = 0; b < function.blocks.size(); ++b) {
		const std::vector<mir_model::MirInstruction> & instructions =
			function.blocks[b].instructions;
		bytes += instructions.capacity() * sizeof(mir_model::MirInstruction);
		for(std::size_t i = 0; i < instructions.size(); ++i)
			bytes += mir_instruction_storage_bytes(instructions[i]);
	}
	return bytes;
}

std::size_t mir_shell_storage_bytes(const mir_model::MirProgram & program)
{
	// The sealed spelling store is already owned and counted by the LowIR
	// program. MIR adds only one shared owner, not another string allocation.
	std::size_t bytes = 0;
	bytes += program.symbol_names.capacity() * sizeof(lowir_model::StringId) +
		program.startup.capacity() * sizeof(mir_model::MirInstruction) +
		program.globals.capacity() * sizeof(mir_model::MirGlobalDefinition) +
		program.object_aliases.capacity() * sizeof(mir_model::MirObjectAlias) +
		program.runtime_functions.capacity() * sizeof(mir_model::RuntimeFunction) +
		program.runtime_data.capacity() * sizeof(mir_model::RuntimeData);
	for(std::size_t i = 0; i < program.startup.size(); ++i)
		bytes += mir_instruction_storage_bytes(program.startup[i]);
	for(std::size_t i = 0; i < program.globals.size(); ++i)
		bytes += program.globals[i].data_items.capacity() *
			sizeof(mir_model::MirGlobalDefinition::DataItem);
	return bytes;
}

}  // namespace

struct ProgramLoweringSession::Impl
{
  std::unique_ptr<lowir_model::LowirProgram> rewritten;
  const lowir_model::LowirProgram & source;
  Stats * stats;
  int optimization_level;
  mir_model::MirProgram shell;
  std::vector<lowir_model::SymbolId> tls_wrappers;
  std::vector<unsigned char> pointer_globals;
  abi::FunctionSignatureIndex signatures;
	std::size_t mir_shell_bytes;

  Impl(const lowir_model::LowirProgram & program, const std::string & target,
       int level, Stats * output_stats)
    : rewritten(force_inline::rewrite_program(program)),
      source(rewritten ? *rewritten : program), stats(output_stats),
      optimization_level(level), mir_shell_bytes(0)
  {
    std::chrono::steady_clock::time_point started;
    if(stats) started = std::chrono::steady_clock::now();
    if(target != "linux")
      throw std::runtime_error("unsupported native target: " + target);
	shell.target = mir_model::MirProgram::TARGET_LINUX;
	shell.symbol_names = source.symbol_names;
	shell.strings = source.strings.seal();
	if(stats) {
	  stats->mir_string_entries = shell.strings.size();
	  stats->mir_spelling_bytes = shell.strings.spelling_bytes();
	  stats->mir_string_storage_bytes = shell.strings.storage_bytes();
	}
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
        program_lowering::lower_global(source.globals[i]);
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
      if(source.object_aliases[i].object_symbol.valid())
        alias.object_symbol = source.object_aliases[i].object_symbol;
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
      signatures, stats);
    machine_opt::Stats opt_stats;
    machine_opt::optimize_function(result, optimization_level,
                                   stats ? &opt_stats : 0);
    if(stats) {
		stats->mir_model_peak_live_bytes = std::max(
			stats->mir_model_peak_live_bytes,
			mir_shell_bytes + mir_function_storage_bytes(result));
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

	mir_model::MirProgram TakeProgramShell()
	{
		if(stats) {
			mir_shell_bytes = mir_shell_storage_bytes(shell);
			stats->mir_model_peak_live_bytes = std::max(
				stats->mir_model_peak_live_bytes, mir_shell_bytes);
		}
		return std::move(shell);
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
  return impl_->TakeProgramShell();
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
