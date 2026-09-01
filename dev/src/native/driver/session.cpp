#include "native/driver/stats.h"

#include "lowir/optimize/force_inline.h"
#include "native/errors.h"
#include "native/eh/lowering.h"
#include "native/mir/control_flow.h"
#include "native/mir/optimize.h"
#include "lowir/analysis/phi_edges.h"
#include "native/driver/program.h"
#include "native/allocation/registers.h"
#include "native/driver/session.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace lowir_native {

namespace {

std::unique_ptr<lowir_model::LowirProgram> rewrite_native_program(
    const lowir_model::LowirProgram & program)
{
  std::unique_ptr<lowir_model::LowirProgram> rewritten =
    force_inline::rewrite_program(program);
  const lowir_model::LowirProgram & current = rewritten ? *rewritten : program;
  if(!lowir_phi_edges::has_critical_phi_edges(current)) return rewritten;
  if(!rewritten)
    rewritten.reset(new lowir_model::LowirProgram(program));
  lowir_phi_edges::split_critical_phi_edges(rewritten.get());
  return rewritten;
}

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
		function.block_presentation_order.capacity() * sizeof(std::uint32_t) +
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
    : rewritten(rewrite_native_program(program)),
      source(rewritten ? *rewritten : program), stats(output_stats),
      optimization_level(level), mir_shell_bytes(0)
  {
    std::chrono::steady_clock::time_point started;
    if(stats) started = std::chrono::steady_clock::now();
    if(target != "linux")
      native_errors::ThrowInvocation("unsupported native target: " + target);
	shell.target = mir_model::MirProgram::TARGET_LINUX;
	shell.symbol_names = source.symbol_names;
	shell.strings = source.strings.seal();
	if(stats) {
	  stats->mir_string_entries = shell.strings.retained_size();
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

  struct FunctionCensusSnapshot
  {
    std::size_t loads, stores, copies;
    std::size_t scalar_loads, scalar_stores;
    std::size_t call_loads, call_stores, call_copies;
    std::size_t planned, grants, releases, spills, frame_homes;
    std::size_t phi_planned, phi_homes;
    std::size_t grant_busy, grant_busy_parameters, grant_busy_values;
    std::size_t defined_in_plan, defined_frame, defined_other_register;
    std::size_t region_candidates, region_assignments;
    std::size_t region_grants, region_residencies, region_busy;
    std::size_t edge_staging, edge_copy, edge_load, edge_index;
    std::size_t edge_binary, edge_call;
  };

  FunctionCensusSnapshot TakeCensusSnapshot() const
  {
    FunctionCensusSnapshot snapshot;
    snapshot.loads = snapshot.stores = snapshot.copies = 0;
    for(std::size_t reason = 0; reason < NMR_COUNT; ++reason) {
      snapshot.loads += stats->movement_loads_by_reason[reason];
      snapshot.stores += stats->movement_stores_by_reason[reason];
      snapshot.copies += stats->movement_register_copies_by_reason[reason];
    }
    snapshot.scalar_loads =
      stats->movement_loads_by_reason[NMR_SCALAR_TEMPORARY];
    snapshot.scalar_stores =
      stats->movement_stores_by_reason[NMR_SCALAR_TEMPORARY];
    snapshot.call_loads = stats->movement_loads_by_reason[NMR_CALL_BOUNDARY];
    snapshot.call_stores = stats->movement_stores_by_reason[NMR_CALL_BOUNDARY];
    snapshot.call_copies =
      stats->movement_register_copies_by_reason[NMR_CALL_BOUNDARY];
    snapshot.planned = stats->planned_value_registers;
    snapshot.grants = stats->planned_register_grants;
    snapshot.releases = stats->planned_interval_releases;
    snapshot.spills = stats->spills;
    snapshot.frame_homes = stats->temporary_frame_homes_created;
    snapshot.phi_planned = stats->planned_phi_registers;
    snapshot.phi_homes = stats->phi_register_homes;
    snapshot.grant_busy = stats->planned_grant_busy_fails;
    snapshot.grant_busy_parameters =
      stats->planned_grant_busy_parameter_holder;
    snapshot.grant_busy_values = stats->planned_grant_busy_value_holder;
    snapshot.defined_in_plan = stats->planned_defined_in_plan;
    snapshot.defined_frame = stats->planned_defined_frame;
    snapshot.defined_other_register =
      stats->planned_defined_other_register;
    snapshot.region_candidates = stats->planner_cyclic_region_candidates;
    snapshot.region_assignments = stats->planner_cyclic_region_assignments;
    snapshot.region_grants = stats->planned_cyclic_region_grants;
    snapshot.region_residencies =
      stats->planned_cyclic_region_residencies;
    snapshot.region_busy = stats->planned_cyclic_region_busy_fails;
    snapshot.edge_staging = stats->edge_staging_total;
    snapshot.edge_copy =
      stats->edge_staging_by_kind[lowir_model::Instruction::IK_COPY];
    snapshot.edge_load =
      stats->edge_staging_by_kind[lowir_model::Instruction::IK_LOAD];
    snapshot.edge_index =
      stats->edge_staging_by_kind[lowir_model::Instruction::IK_INDEX];
    snapshot.edge_binary =
      stats->edge_staging_by_kind[lowir_model::Instruction::IK_BINARY];
    snapshot.edge_call =
      stats->edge_staging_by_kind[lowir_model::Instruction::IK_CALL];
    return snapshot;
  }

  void AppendCensusLine(std::size_t index, std::size_t mir_instructions,
                        const FunctionCensusSnapshot & before)
  {
    const FunctionCensusSnapshot after = TakeCensusSnapshot();
    std::string line = "function_census symbol=" +
      lowir_model::lowir_symbol_name(source, source.functions[index].symbol);
    const auto field = [&line](const char * name, std::size_t value) {
      line += ' ';
      line += name;
      line += '=';
      line += std::to_string(value);
    };
    field("mir", mir_instructions);
    field("mov_loads", after.loads - before.loads);
    field("mov_stores", after.stores - before.stores);
    field("mov_copies", after.copies - before.copies);
    field("scalar_loads", after.scalar_loads - before.scalar_loads);
    field("scalar_stores", after.scalar_stores - before.scalar_stores);
    field("call_loads", after.call_loads - before.call_loads);
    field("call_stores", after.call_stores - before.call_stores);
    field("call_copies", after.call_copies - before.call_copies);
    field("planned", after.planned - before.planned);
    field("grants", after.grants - before.grants);
    field("releases", after.releases - before.releases);
    field("spills", after.spills - before.spills);
    field("frame_homes", after.frame_homes - before.frame_homes);
    field("phi_planned", after.phi_planned - before.phi_planned);
    field("phi_homes", after.phi_homes - before.phi_homes);
    field("grant_busy", after.grant_busy - before.grant_busy);
    field("grant_busy_parameters",
          after.grant_busy_parameters - before.grant_busy_parameters);
    field("grant_busy_values",
          after.grant_busy_values - before.grant_busy_values);
    field("defined_in_plan",
          after.defined_in_plan - before.defined_in_plan);
    field("defined_frame", after.defined_frame - before.defined_frame);
    field("defined_other_register",
          after.defined_other_register - before.defined_other_register);
    field("region_candidates",
          after.region_candidates - before.region_candidates);
    field("region_assignments",
          after.region_assignments - before.region_assignments);
    field("region_grants", after.region_grants - before.region_grants);
    field("region_residencies",
          after.region_residencies - before.region_residencies);
    field("region_busy", after.region_busy - before.region_busy);
    field("edge_staging", after.edge_staging - before.edge_staging);
    field("edge_copy", after.edge_copy - before.edge_copy);
    field("edge_load", after.edge_load - before.edge_load);
    field("edge_index", after.edge_index - before.edge_index);
    field("edge_binary", after.edge_binary - before.edge_binary);
    field("edge_call", after.edge_call - before.edge_call);
    stats->function_census_lines.push_back(std::move(line));
  }

  struct LoopCensusRegion
  {
    std::size_t header;
    std::vector<bool> members;
  };

  static void AppendLoopSuccessor(
      std::vector<std::size_t> * successors, std::size_t target)
  {
    if(std::find(successors->begin(), successors->end(), target) ==
       successors->end())
      successors->push_back(target);
  }

  static bool ReachesWithoutBlock(
      const std::vector<std::vector<std::size_t> > & successors,
      std::size_t target, std::size_t omitted)
  {
    if(successors.empty() || omitted == 0) return false;
    std::vector<bool> seen(successors.size(), false);
    std::vector<std::size_t> work(1, 0);
    seen[0] = true;
    while(!work.empty()) {
      const std::size_t block = work.back();
      work.pop_back();
      if(block == target) return true;
      for(std::size_t edge = 0; edge < successors[block].size(); ++edge) {
        const std::size_t next = successors[block][edge];
        if(next == omitted || seen[next]) continue;
        seen[next] = true;
        work.push_back(next);
      }
    }
    return false;
  }

  void AppendLoopCensusLines(std::size_t index,
                             const mir_model::MirFunction & function)
  {
    const std::size_t no_block = static_cast<std::size_t>(-1);
    std::vector<std::size_t> block_by_id;
    for(std::size_t block = 0; block < function.blocks.size(); ++block) {
      const std::uint32_t id = function.blocks[block].id;
      if(block_by_id.size() <= id)
        block_by_id.resize(static_cast<std::size_t>(id) + 1, no_block);
      block_by_id[id] = block;
    }

    std::vector<std::vector<std::size_t> > successors(
      function.blocks.size());
    std::vector<std::vector<std::size_t> > predecessors(
      function.blocks.size());
    for(std::size_t block = 0; block < function.blocks.size(); ++block) {
      const std::vector<mir_model::MirInstruction> & instructions =
        function.blocks[block].instructions;
      bool falls_through = true;
      for(std::size_t ins = 0; ins < instructions.size(); ++ins) {
        const mir_model::MirInstruction & instruction = instructions[ins];
        if(instruction.opcode == mir_model::MirInstruction::MI_JCC ||
           instruction.opcode == mir_model::MirInstruction::MI_JMP ||
           instruction.opcode == mir_model::MirInstruction::MI_JNE ||
           instruction.opcode == mir_model::MirInstruction::MI_EH_PUSH) {
          for(std::size_t operand = 0;
              operand < instruction.operands.size(); ++operand) {
            const mir_model::MirOperand & target_operand =
              instruction.operands[operand];
            if(target_operand.kind != mir_model::MirOperand::OP_LABEL)
              continue;
            const std::uint32_t id = target_operand.block;
            const std::size_t target = id < block_by_id.size() ?
              block_by_id[id] : no_block;
            if(target != no_block)
              AppendLoopSuccessor(&successors[block], target);
          }
        }
        if(mir_control_flow::ends_unconditional_control_flow(
             instruction.opcode)) falls_through = false;
      }
      if(falls_through && block + 1 < function.blocks.size())
        AppendLoopSuccessor(&successors[block], block + 1);
      for(std::size_t edge = 0; edge < successors[block].size(); ++edge)
        predecessors[successors[block][edge]].push_back(block);
    }

    std::vector<bool> reachable(function.blocks.size(), false);
    std::vector<std::size_t> reach_work;
    if(!function.blocks.empty()) {
      reachable[0] = true;
      reach_work.push_back(0);
    }
    while(!reach_work.empty()) {
      const std::size_t block = reach_work.back();
      reach_work.pop_back();
      for(std::size_t edge = 0; edge < successors[block].size(); ++edge) {
        const std::size_t next = successors[block][edge];
        if(reachable[next]) continue;
        reachable[next] = true;
        reach_work.push_back(next);
      }
    }

    std::vector<LoopCensusRegion> loops;
    for(std::size_t latch = 0; latch < successors.size(); ++latch) {
      if(!reachable[latch]) continue;
      for(std::size_t edge = 0; edge < successors[latch].size(); ++edge) {
        const std::size_t header = successors[latch][edge];
        if(!reachable[header]) continue;
        if(header != latch &&
           ReachesWithoutBlock(successors, latch, header)) continue;
        std::size_t loop = 0;
        while(loop < loops.size() && loops[loop].header != header) ++loop;
        if(loop == loops.size()) {
          LoopCensusRegion region;
          region.header = header;
          region.members.assign(function.blocks.size(), false);
          loops.push_back(region);
        }
        std::vector<bool> natural(function.blocks.size(), false);
        natural[header] = true;
        natural[latch] = true;
        std::vector<std::size_t> work(1, latch);
        while(!work.empty()) {
          const std::size_t block = work.back();
          work.pop_back();
          for(std::size_t pred = 0;
            pred < predecessors[block].size(); ++pred) {
            const std::size_t previous = predecessors[block][pred];
            if(natural[previous]) continue;
            natural[previous] = true;
            if(previous != header) work.push_back(previous);
          }
        }
        for(std::size_t block = 0; block < natural.size(); ++block)
          loops[loop].members[block] =
            loops[loop].members[block] || natural[block];
      }
    }

    std::sort(loops.begin(), loops.end(),
      [](const LoopCensusRegion & left, const LoopCensusRegion & right) {
        return left.header < right.header;
      });
    const std::string symbol = lowir_model::lowir_symbol_name(
      source, source.functions[index].symbol);
    for(std::size_t loop = 0; loop < loops.size(); ++loop) {
      std::size_t blocks = 0, mir = 0, calls = 0, eh = 0;
      std::size_t frame_operands = 0;
      for(std::size_t block = 0; block < function.blocks.size(); ++block) {
        if(!loops[loop].members[block]) continue;
        ++blocks;
        const std::vector<mir_model::MirInstruction> & instructions =
          function.blocks[block].instructions;
        mir += instructions.size();
        for(std::size_t ins = 0; ins < instructions.size(); ++ins) {
          const mir_model::MirInstruction & instruction = instructions[ins];
          if(instruction.opcode == mir_model::MirInstruction::MI_CALL ||
             instruction.opcode ==
               mir_model::MirInstruction::MI_CALL_INDIRECT)
            ++calls;
          if(instruction.opcode >= mir_model::MirInstruction::MI_EH_PUSH &&
             instruction.opcode <= mir_model::MirInstruction::MI_RESUME)
            ++eh;
          for(std::size_t operand = 0;
              operand < instruction.operands.size(); ++operand)
            if(instruction.operands[operand].kind ==
                 mir_model::MirOperand::OP_FRAME)
              ++frame_operands;
        }
      }
      std::size_t depth = 0;
      for(std::size_t outer = 0; outer < loops.size(); ++outer) {
        if(outer == loop) continue;
        bool contains = true, strict = false;
        for(std::size_t block = 0; block < function.blocks.size(); ++block) {
          if(loops[loop].members[block] &&
             !loops[outer].members[block]) contains = false;
          if(loops[outer].members[block] &&
             !loops[loop].members[block]) strict = true;
        }
        if(contains && strict)
          ++depth;
      }
      const lowir_model::BlockId header =
        function.blocks[loops[loop].header].id;
      const std::uint32_t header_index = header;
      std::string header_label = "block_" +
        std::to_string(header_index);
      if(header_index < source.functions[index].block_labels.size() &&
         source.functions[index].block_labels[header_index].valid())
        header_label = lowir_model::lowir_block_label(
          source.strings, source.functions[index], header);
      std::string line = "loop_census symbol=" + symbol +
        " header_label=" + header_label;
      const auto field = [&line](const char * name, std::size_t value) {
        line += ' ';
        line += name;
        line += '=';
        line += std::to_string(value);
      };
      field("header", static_cast<std::uint32_t>(header));
      field("blocks", blocks);
      field("mir", mir);
      field("calls", calls);
      field("eh", eh);
      field("depth", depth);
      field("frame_operands", frame_operands);
      field("frame_bindings", function.frame_bindings.size());
      field("callee_saved", function.callee_saved_regs.size());
      stats->function_census_lines.push_back(std::move(line));
    }
  }

  mir_model::MirFunction LowerFunction(std::size_t index)
  {
    if(index >= source.functions.size())
      native_errors::ThrowInternal("native function index is out of bounds");
    std::chrono::steady_clock::time_point started;
    if(stats) started = std::chrono::steady_clock::now();
    FunctionCensusSnapshot census_before;
    const bool census = stats && stats->function_census;
    if(census) census_before = TakeCensusSnapshot();
    mir_model::MirFunction result = session_detail::lower_native_function(
      source, source.functions[index], pointer_globals, tls_wrappers,
      signatures, optimization_level, stats, 0);
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
      stats->machine_opt_identity_moves += opt_stats.identity_moves;
      stats->machine_opt_medium_copy_chunks +=
        opt_stats.medium_copy_chunks;
      stats->machine_opt_block_recolor_candidates +=
        opt_stats.block_recolor_candidates;
      stats->machine_opt_block_recolor_registers +=
        opt_stats.block_recolor_registers;
      stats->machine_opt_block_recolor_blocks +=
        opt_stats.block_recolor_blocks;
      stats->machine_opt_terminal_call_results +=
        opt_stats.terminal_call_results;
      stats->machine_opt_sibling_parameter_retains +=
        opt_stats.sibling_parameter_retains;
      stats->machine_opt_frameless_functions += opt_stats.frameless_functions;
      stats->machine_opt_frameless_call_functions +=
        opt_stats.frameless_call_functions;
      stats->machine_opt_frameless_saved_registers +=
        opt_stats.frameless_saved_registers;
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
    if(census) {
      AppendCensusLine(index, opt_stats.output_instructions, census_before);
      AppendLoopCensusLines(index, result);
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

const lowir_model::LowirProgram & ProgramLoweringSession::prepared_program() const
{
  return impl_->source;
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
