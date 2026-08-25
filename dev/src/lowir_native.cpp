#include "lowir_native.h"
#include "lowir_native_abi.h"
#include "lowir_native_address_lowering.h"
#include "lowir_native_analysis.h"
#include "lowir_native_atomic_lowering.h"
#include "lowir_native_bulk_lowering.h"
#include "lowir_native_block_labels.h"
#include "lowir_native_call_lowering.h"
#include "lowir_native_compare_lowering.h"
#include "lowir_native_control_flow.h"
#include "lowir_native_copy_lowering.h"
#include "lowir_native_division_lowering.h"
#include "lowir_native_eh.h"
#include "lowir_native_frame_layout.h"
#include "lowir_native_frame_home_planning.h"
#include "lowir_native_frame_planning.h"
#include "lowir_native_host_eh.h"
#include "lowir_native_index_lowering.h"
#include "lowir_native_integer_lowering.h"
#include "lowir_native_intrinsic_lowering.h"
#include "lowir_native_location_planning.h"
#include "lowir_native_mir.h"
#include "lowir_native_memory_lowering.h"
#include "lowir_native_movement_stats.h"
#include "lowir_native_parameter_lowering.h"
#include "lowir_native_phi_lowering.h"
#include "lowir_native_program.h"
#include "lowir_native_registers.h"
#include "lowir_native_return_lowering.h"
#include "lowir_native_selection.h"
#include "lowir_native_session.h"
#include "lowir_native_spill_selection.h"
#include "lowir_native_spill_slots.h"
#include "lowir_native_stack.h"
#include "lowir_native_value.h"
#include "lowir_native_varargs.h"
#include "lowir_native_wide.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
namespace lowir_native {
namespace {
using lowir_model::Instruction; using lowir_model::LowOperation; using lowir_model::LowType; using lowir_model::Operand;
using mir_model::MirInstruction; using mir_model::MirOperand;
using abi::FunctionSignature; using abi::FunctionSignatureIndex;
using analysis::FunctionFacts; using analysis::StorageFacts;
using analysis::analyze_function; using analysis::analyze_storage; using analysis::register_mask;
using analysis::register_was_clobbered_before;
using allocation::RegisterPool; using allocation::XmmPool;
using allocation::is_callee_saved;
using namespace build;
using namespace selection;
class FunctionLowerer : private IntrinsicLowering<FunctionLowerer>,
                        private AddressLowering<FunctionLowerer>,
                        private AtomicLowering<FunctionLowerer>,
                        private bulk_detail::BulkLowering<FunctionLowerer>,
                        private call_detail::CallLowering<FunctionLowerer>,
                        private comparison_detail::CompareLowering<FunctionLowerer>,
                        private copy_detail::CopyLowering<FunctionLowerer>,
                        private frame_planning_detail::FramePlanning<FunctionLowerer>,
                        private index_detail::IndexLowering<FunctionLowerer>,
                        private integer_detail::IntegerLowering<FunctionLowerer>,
                        private memory_detail::MemoryLowering<FunctionLowerer>,
                        private parameter_detail::ParameterRegisterState<FunctionLowerer>,
                        private phi_detail::PhiLowering<FunctionLowerer>,
                        private location_planning::PlannedResidency<FunctionLowerer>,
                        private return_detail::ReturnLowering<FunctionLowerer>,
                        private spill_detail::SpillSelection<FunctionLowerer>
{
  friend class IntrinsicLowering<FunctionLowerer>;
  friend class AddressLowering<FunctionLowerer>;
  friend class AtomicLowering<FunctionLowerer>;
  friend class bulk_detail::BulkLowering<FunctionLowerer>;
  friend class call_detail::CallLowering<FunctionLowerer>;
  friend class comparison_detail::CompareLowering<FunctionLowerer>;
  friend class copy_detail::CopyLowering<FunctionLowerer>;
  friend class frame_planning_detail::FramePlanning<FunctionLowerer>;
  friend class index_detail::IndexLowering<FunctionLowerer>;
  friend class integer_detail::IntegerLowering<FunctionLowerer>;
  friend class memory_detail::MemoryLowering<FunctionLowerer>;
  friend class parameter_detail::ParameterRegisterState<FunctionLowerer>;
  friend class phi_detail::PhiLowering<FunctionLowerer>;
  friend class location_planning::PlannedResidency<FunctionLowerer>;
  friend class return_detail::ReturnLowering<FunctionLowerer>;
  friend class spill_detail::SpillSelection<FunctionLowerer>;
public:
  FunctionLowerer(const lowir_model::LowirProgram & program,
                  const lowir_model::LowirFunction & source,
                  const std::vector<unsigned char> & pointer_globals,
                  const std::vector<lowir_model::SymbolId> & tls_wrappers,
                  const FunctionSignatureIndex & signatures,
                  int optimization_level,
                  lowir_native::Stats * stats,
                  allocation::AllocationDecisionLog * decisions)
    : program_(program), source_(source), pointer_globals_(pointer_globals),
      tls_wrappers_(tls_wrappers),
      signatures_(signatures), optimization_level_(optimization_level),
      stats_(stats),
      facts_(analyze_function(source, stats)),
      control_flow_(source), decision_log_(decisions), registers_(decisions),
      xmms_(decisions), live_locations_(stats), generated_frame_names_(source),
      position_(0)
  {
    values_.resize(source_.value_names.size());
    value_known_.assign(source_.value_names.size(), 0);
    cyclic_register_assumed_.assign(source_.value_names.size(), 0);
    phi_planned_home_.assign(source_.value_names.size(), 0);
    incoming_parameter_registers_.resize(source_.value_names.size(), XR_RSP);
    incoming_parameter_register_known_.assign(source_.value_names.size(), 0);
    target_.symbol = source.symbol;
    if(source.metadata.object_symbol.valid())
      target_.object_symbol = source.metadata.object_symbol;
    target_.block_labels = source.block_labels;
    target_.block_presentation_order = source.block_presentation_order;
    target_.return_type = source.return_type;
    target_.debug_location.file = source.debug_location.file;
    target_.debug_location.line = source.debug_location.line;
    target_.debug_location.column = source.debug_location.column;
    if(stats_)
      stats_->shared_storage_lifetime_extensions += std::count_if(
        facts_.shared_storage_last_use.begin(),
        facts_.shared_storage_last_use.end(),
        [](std::size_t position) {
          return position != FunctionFacts::missing_position();
        });
    if(facts_.has_i128_atomic) registers_.reserve(XR_RBX);
    storage_facts_ = analyze_storage(source_, facts_, tls_wrappers_);
    compute_value_register_plan(source_, facts_, optimization_level_, stats_);
    build_planned_release_schedule();
    slot_offsets_.resize(source_.slot_names.size(), 0);
    slot_offset_known_.assign(source_.slot_names.size(), 0);
    discarded_slots_.assign(source_.slot_names.size(), 0);
    plan_variadic_register_save();
    bind_parameters();
    record_parameter_setup_clobbers();
    plan_slots();
    plan_host_eh();
    phi_detail::plan_transfers(source_, this, &phi_transfers_);
  }
  mir_model::MirFunction lower()
  {
    target_.blocks.reserve(source_.blocks.size());
    for(std::size_t i = 0; i < source_.blocks.size(); ++i) {
      mir_model::MirBlock block;
      block.id = source_.blocks[i].id;
      block.instructions.reserve(source_.blocks[i].instructions.size() +
        (i == 0 ? parameter_moves_.size() : 0));
      control_flow_.SelectBlock(i);
      current_block_id_ = source_.blocks[i].id;
      flush_planned_releases();
      current_block_last_position_ = source_.blocks[i].instructions.empty() ?
        position_ : position_ + source_.blocks[i].instructions.size() - 1;
      for(std::size_t j = 0; j < source_.blocks[i].instructions.size(); ++j, ++position_) {
        const std::size_t first_machine_instruction = block.instructions.size();
        const Instruction & source_instruction =
          source_.blocks[i].instructions[j];
        active_instruction_ = &source_instruction;
        decision_log_->set_context(position_, source_instruction.dest);
        if(source_instruction.kind == Instruction::IK_JUMP ||
           source_instruction.kind == Instruction::IK_BRANCH ||
           source_instruction.kind == Instruction::IK_SWITCH)
          emit_phi_transfers(source_.blocks[i].id, block.instructions);
        lower_instruction(source_.blocks[i], j, block.instructions);
        const std::size_t after_instruction = block.instructions.size();
        if(stats_)
          movement_stats::record(stats_,
            movement_stats::classify(source_.blocks[i].instructions[j]),
            block.instructions, first_machine_instruction, after_instruction);
        stabilize_edge_live_result(source_.blocks[i].instructions[j],
                                   block.instructions);
        if(stats_)
          movement_stats::record(stats_, NMR_SCALAR_TEMPORARY,
            block.instructions, after_instruction, block.instructions.size());
        record_emitted_register_definitions(
          block.instructions, first_machine_instruction);
        record_cyclic_register_assumptions(
          block.instructions, first_machine_instruction);
        record_rax_first_use_carrier(source_.blocks[i].instructions[j],
          block.instructions, first_machine_instruction);
        const lowir_model::Instruction * debug_source =
          &source_.blocks[i].instructions[j];
        if(j + 1 < source_.blocks[i].instructions.size() &&
           source_.blocks[i].instructions[j + 1].kind == Instruction::IK_BRANCH)
          for(std::size_t k = first_machine_instruction;
              k < block.instructions.size(); ++k)
            if(block.instructions[k].opcode == MirInstruction::MI_JCC ||
               block.instructions[k].opcode == MirInstruction::MI_JMP) {
              debug_source = &source_.blocks[i].instructions[j + 1];
              break;
            }
        const lowir_model::InstructionDebugLocation & debug =
          debug_source->debug_location;
        for(std::size_t k = first_machine_instruction;
            k < block.instructions.size(); ++k) {
          block.instructions[k].has_source_position = true;
          block.instructions[k].source_position = position_;
          block.instructions[k].debug_location.file = debug.file;
          block.instructions[k].debug_location.line = debug.line;
          block.instructions[k].debug_location.column = debug.column;
        }
      }
      target_.blocks.push_back(std::move(block));
    }
    discard_unread_parameter_homes();
    if(stats_)
      movement_stats::record(stats_, NMR_PARAMETER_HOME, parameter_moves_, 0,
                             parameter_moves_.size());
    if(!target_.blocks.empty())
      target_.blocks[0].instructions.insert(
        target_.blocks[0].instructions.begin(), parameter_moves_.begin(),
        parameter_moves_.end());
    target_.callee_saved_regs = registers_.preserves();
    frame_layout::finalize_function(
      target_, source_, facts_, storage_facts_, frame_bytes_,
      uses_scalar_float_, constrained_wide_pressure());
    host_eh_detail::collect_host_eh_clauses(&target_);
    return target_;
  }
private:
  const lowir_model::LowirProgram & program_;
  const lowir_model::LowirFunction & source_;
  const std::vector<unsigned char> & pointer_globals_;
  const std::vector<lowir_model::SymbolId> & tls_wrappers_;
  const FunctionSignatureIndex & signatures_;
  int optimization_level_;
  lowir_native::Stats * stats_;
  FunctionFacts facts_;
  StorageFacts storage_facts_;
  analysis::ControlFlowQueries control_flow_;
  mir_model::MirFunction target_;
  allocation::AllocationDecisionLog * decision_log_;
  RegisterPool registers_;
  XmmPool xmms_;
  spill_slots::Pool spill_slots_;
  location_planning::LiveLocationIndex live_locations_;
  location_planning::GeneratedFrameNames generated_frame_names_;
  std::vector<ValueFact> values_;
  std::vector<unsigned char> value_known_;
  std::vector<unsigned char> cyclic_register_assumed_;
  // A phi holding its planned register outlives its counted uses:
  // predecessor terminators keep writing it until the interval end.
  std::vector<unsigned char> phi_planned_home_;
  unsigned char phi_home_registers_[16] = {};
  std::vector<long long> slot_offsets_;
  std::vector<unsigned char> slot_offset_known_;
  std::vector<X64Register> incoming_parameter_registers_;
  std::vector<unsigned char> incoming_parameter_register_known_;
  std::vector<unsigned char> discarded_slots_;
  std::vector<std::vector<phi_detail::Transfer> > phi_transfers_;
  MirOperand phi_cycle_scratch_;
  std::vector<MirInstruction> parameter_moves_;
  const Instruction * active_instruction_ = 0;
  std::uint32_t current_block_id_ = 0;
  std::size_t current_block_last_position_ = 0;
  std::size_t position_, frame_bytes_ = 0;
  std::size_t skipped_position_ = static_cast<std::size_t>(-1);
  bool uses_scalar_float_ = false;
  bool has_xmm_call_scratch_ = false;
  long long xmm_call_scratch_ = 0;
  long long variadic_register_save_offset_ = 0;
  abi::VariadicState variadic_state_;

  long long allocate_frame_binding(mir_model::MirFrameBinding::Kind kind,
                                   lowir_model::FixedPresentationName name,
                                   const LowType & type)
  {
    return frame_home_planning::allocate_binding(
      target_, frame_bytes_, kind,
      lowir_model::PresentationName::fixed(name), type);
  }
  long long allocate_frame_binding(mir_model::MirFrameBinding::Kind kind,
                                   lowir_model::StringId name,
                                   const LowType & type)
  {
    return frame_home_planning::allocate_binding(
      target_, frame_bytes_, kind,
      name.valid() ? lowir_model::PresentationName::pooled(name) :
        lowir_model::PresentationName(), type);
  }
  long long allocate_frame_binding(mir_model::MirFrameBinding::Kind kind,
                                   lowir_model::PresentationName name,
                                   const LowType & type)
  {
    return frame_home_planning::allocate_binding(
      target_, frame_bytes_, kind, name, type);
  }
  std::uint32_t append_frame_binding(
                            mir_model::MirFrameBinding::Kind kind,
                            lowir_model::PresentationName name,
                            const LowType & type,
                            long long offset)
  {
    return frame_home_planning::append_binding(
      target_, kind, name, type, offset);
  }
  MirOperand allocate_temp_frame_binding(lowir_model::ValueId value,
                                         const LowType & type,
                                         TemporaryHomeReason reason = THR_COUNT)
  {
    if(stats_ && planned_register_entry(value) != 0)
      ++stats_->planned_frame_homes_by_reason[
        reason <= THR_COUNT ? reason : THR_COUNT];
    return frame_home_planning::allocate_temporary(
      target_, frame_bytes_, spill_slots_, generated_frame_names_, facts_,
      value, type, position_, reason, result_crosses_call(value), stats_);
  }

  bool crosses_call(lowir_model::ValueId value) const
  {
    return facts_.has(value, FunctionFacts::VF_LIVE_ACROSS_CALL);
  }
  // A planned phi claims its register at construction time so every
  // predecessor transfer targets it directly; a contested reservation
  // falls back to the frame home.
  bool try_claim_planned_phi_register(lowir_model::ValueId value,
                                      const LowType & type)
  {
    const unsigned char entry = planned_register_entry(value);
    if(entry == 0) return false;
    // The constrained-wide-pressure binary path claims R15 as a fixed
    // parameter destination, below the reservation discipline.
    if(constrained_wide_pressure()) return false;
    const X64Register planned = static_cast<X64Register>(entry - 1);
    if(crosses_register_clobber(value, planned) ||
       !registers_.try_reserve(planned)) return false;
    define(value, type, reg_operand(planned));
    phi_planned_home_[value] = 1;
    phi_home_registers_[static_cast<unsigned>(planned)] = 1;
    if(stats_) ++stats_->phi_register_homes;
    return true;
  }
  bool is_phi_home_register(X64Register reg) const
  {
    return phi_home_registers_[static_cast<unsigned>(reg)] != 0;
  }
  bool constrained_wide_pressure() const { return source_.params.size() > 6 && source_.slots.empty() && !facts_.calls.empty(); }
  bool crosses_register_clobber(lowir_model::ValueId value,
                                X64Register reg) const
  {
    return analysis::crosses_register_clobber(facts_, value, reg);
  }
  bool promoted_parameter_crosses_clobber(
      std::size_t parameter, lowir_model::ValueId value,
      X64Register reg) const
  {
    return crosses_register_clobber(value, reg) ||
      (parameter < storage_facts_.promoted_parameter_clobbers.size() &&
       (storage_facts_.promoted_parameter_clobbers[parameter] &
        analysis::register_mask(reg)) != 0);
  }
  bool parameter_crosses_call(lowir_model::ValueId value) const
  {
    return crosses_call(value) || storage_facts_.has(
      value, StorageFacts::VF_PROMOTED_ACROSS_CALL);
  }
  bool incoming_parameter_register_is_intact(
      lowir_model::ValueId name, X64Register reg) const
  {
    return incoming_register_is_intact(name, reg);
  }
  void bind_mixed_parameters()
  {
    uses_scalar_float_ = true;
    std::size_t gpr_parameters = 0;
    std::size_t float_parameters = 0;
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      if(is_scalar_float(source_.params[i].type)) ++float_parameters;
      else ++gpr_parameters;
    }
    if(gpr_parameters >= 2 && float_parameters)
      registers_.reserve(XR_R8); // fixed conversion/setup scratch at mixed boundaries
    std::size_t gpr_index = 0;
    std::size_t xmm_index = 0;
    std::size_t stack_index = 0;
    std::vector<MirInstruction> gpr_parameter_moves;
    std::vector<lowir_model::ValueId> gpr_parameter_move_values;
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      const lowir_model::LowirParameter & parameter = source_.params[i];
      mir_model::MirParamBinding binding;
      binding.name = parameter.name.valid() ?
        lowir_model::PresentationName::pooled(parameter.name) :
        lowir_model::PresentationName();
      binding.type = parameter.type;
      ValueFact value;
      value.type = parameter.type;
      value.parameter = true;
      if(is_scalar_float(parameter.type) && xmm_index < 8) {
        binding.location = mir_model::MirParamBinding::PL_XMM;
        binding.xmm = static_cast<XmmRegister>(xmm_index++);
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
        append_float_move(parameter_moves_, frame_operand(home),
                          xmm_operand(binding.xmm), parameter.type);
        value.location = frame_operand(home);
      } else if(!is_scalar_float(parameter.type) && gpr_index < 6) {
        binding.location = mir_model::MirParamBinding::PL_REG;
        const std::size_t parameter_gpr_index = gpr_index++;
        binding.reg = abi::argument_register(parameter_gpr_index);
        value.location = reg_operand(binding.reg);
        const std::size_t uses = facts_.uses[parameter.value];
        const bool clobbered = promoted_parameter_crosses_clobber(
          i, parameter.value, binding.reg);
        if(clobbered || (facts_.has_va_start &&
           ((parameter_gpr_index != 0 && uses) ||
            (parameter.type.kind == lowir_model::LTK_PTR && uses > 1)))) {
          const X64Register destination =
            allocate_preserved_for_parameter(parameter.value);
          value.location = reg_operand(destination);
          append_optional_parameter_move(gpr_parameter_moves,
            gpr_parameter_move_values, value, parameter.value,
            reg_operand(binding.reg));
        }
      } else {
        binding.location = mir_model::MirParamBinding::PL_STACK;
        binding.stack_offset = 16 + static_cast<long long>(stack_index++ * 8);
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
        if(is_scalar_float(parameter.type))
          append_float_move(parameter_moves_, frame_operand(home),
                            frame_operand(binding.stack_offset), parameter.type);
        else {
          append_load(parameter_moves_, reg_operand(XR_RAX),
                      frame_operand(binding.stack_offset), parameter.type);
          append_store(parameter_moves_, frame_operand(home), reg_operand(XR_RAX),
                       parameter.type);
        }
        value.location = frame_operand(home);
      }
      reserve_direct_parameter_register(
        binding, value, facts_.uses[parameter.value]);
      target_.params.push_back(binding);
      set_value(parameter.value, value);
    }
    append_optional_parameter_moves(
      gpr_parameter_moves, gpr_parameter_move_values);
  }
  void bind_aggregate_parameters()
  {
    const abi::Plan plan = abi::classify(source_.params);
    std::vector<long long> homes(source_.params.size(), 0);
    std::vector<std::size_t> parameter_indices(
      source_.value_names.size(), FunctionFacts::missing_position());
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      const lowir_model::LowirParameter & parameter = source_.params[i];
      parameter_indices[parameter.value] = i;
      homes[i] = allocate_frame_binding(
        mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
      ValueFact value;
      value.type = parameter.type;
      value.parameter = true;
      value.location = frame_operand(homes[i]);
      if(parameter.type.kind == lowir_model::LTK_OBJECT) {
        value.frame_address = true;
        value.has_frame_provenance = true;
        value.frame_provenance = homes[i];
      }
      set_value(parameter.value, value);
    }
    for(std::size_t slot = 0;
        slot < storage_facts_.parameter_slot_aliases.size(); ++slot) {
      const lowir_model::ValueId alias =
        storage_facts_.parameter_slot_aliases[slot];
      if(!alias.valid()) continue;
      const std::size_t parameter = parameter_indices[alias];
      if(parameter == FunctionFacts::missing_position())
        throw std::logic_error("parameter slot alias has no parameter");
      slot_offsets_[slot] = homes[parameter];
      slot_offset_known_[slot] = 1;
    }
    for(std::size_t i = 0; i < plan.pieces.size(); ++i) {
      const abi::Piece & piece = plan.pieces[i];
      const lowir_model::LowirParameter & parameter =
        source_.params[piece.parameter_index];
      mir_model::MirParamBinding binding;
      binding.name = parameter.name.valid() ?
        lowir_model::PresentationName::pooled(parameter.name) :
        lowir_model::PresentationName();
      binding.type = parameter.type.kind == lowir_model::LTK_OBJECT ||
        wide::is_integer(parameter.type) ?
        piece.type : parameter.type;
      binding.chunk_offset = static_cast<long long>(piece.chunk_offset);
      const MirOperand home = frame_operand(
        homes[piece.parameter_index] + static_cast<long long>(piece.chunk_offset));
      if(piece.location == abi::PL_GPR) {
        binding.location = mir_model::MirParamBinding::PL_REG;
        binding.reg = piece.reg;
        append_store(parameter_moves_, home, reg_operand(piece.reg), piece.type);
      } else if(piece.location == abi::PL_XMM) {
        binding.location = mir_model::MirParamBinding::PL_XMM;
        binding.xmm = piece.xmm;
        uses_scalar_float_ = true;
        append_float_move(parameter_moves_, home, xmm_operand(piece.xmm), piece.type);
      } else {
        binding.location = mir_model::MirParamBinding::PL_STACK;
        binding.stack_offset = 16 + static_cast<long long>(piece.stack_offset);
        if(is_floating(piece.type)) {
          uses_scalar_float_ = true;
          append_float_move(parameter_moves_, home,
                            frame_operand(binding.stack_offset), piece.type);
        } else {
          append_load(parameter_moves_, reg_operand(XR_RAX),
                      frame_operand(binding.stack_offset), piece.type);
          append_store(parameter_moves_, home, reg_operand(XR_RAX), piece.type);
        }
      }
      target_.params.push_back(binding);
    }
  }
  void bind_parameters()
  {
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      if(source_.params[i].type.kind != lowir_model::LTK_OBJECT &&
         !wide::is_integer(source_.params[i].type) &&
         !is_extended_float(source_.params[i].type)) continue;
      bind_aggregate_parameters();
      return;
    }
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      if(!is_scalar_float(source_.params[i].type)) continue;
      bind_mixed_parameters();
      return;
    }
    const bool wide_gpr_boundary = source_.params.size() >= 6;
    bool incoming_pool_reserved[6] = {false, false, false, false, false, false};
    if(!wide_gpr_boundary || constrained_wide_pressure()) {
      for(std::size_t i = 4; i < source_.params.size() && i < 6; ++i) {
        if(storage_facts_.parameter_selected_uses[i] == 0) continue;
        registers_.reserve(abi::argument_register(i));
        incoming_pool_reserved[i] = true;
      }
    }
    std::vector<X64Register> cross_call_homes(
      source_.value_names.size(), XR_RSP);
    std::vector<unsigned char> cross_call_home_known(
      source_.value_names.size(), 0);
    std::vector<MirInstruction> register_parameter_moves;
    std::vector<lowir_model::ValueId> register_parameter_move_values;
    if(!wide_gpr_boundary)
      for(std::size_t i = std::min<std::size_t>(source_.params.size(), 6); i != 0; --i) {
        const lowir_model::ValueId value = source_.params[i - 1].value;
        if(storage_facts_.parameter_selected_uses[i - 1] &&
           parameter_crosses_call(value)) {
          cross_call_homes[value] = allocate_preserved_for_parameter(value);
          cross_call_home_known[value] = 1;
        }
      }
    bool home_unused_register_parameters = wide_gpr_boundary;
    for(std::size_t i = 0; i < source_.params.size() && i < 6; ++i)
      if(storage_facts_.parameter_selected_uses[i] != 0)
        home_unused_register_parameters = false;
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      const lowir_model::LowirParameter & parameter = source_.params[i];
      mir_model::MirParamBinding binding;
      binding.name = parameter.name.valid() ?
        lowir_model::PresentationName::pooled(parameter.name) :
        lowir_model::PresentationName();
      binding.type = parameter.type;
      ValueFact value;
      value.type = parameter.type;
      value.parameter = true;
      if(i >= 6) {
        binding.location = mir_model::MirParamBinding::PL_STACK;
        binding.stack_offset = 16 + static_cast<long long>((i - 6) * 8);
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
        append_load(parameter_moves_, reg_operand(XR_RAX),
                    frame_operand(binding.stack_offset), parameter.type);
        append_store(parameter_moves_, frame_operand(home), reg_operand(XR_RAX),
                     parameter.type);
        value.location = frame_operand(home);
        target_.params.push_back(binding);
        set_value(parameter.value, value);
        continue;
      }
      binding.location = mir_model::MirParamBinding::PL_REG;
      binding.reg = abi::argument_register(i);
      incoming_parameter_registers_[parameter.value] = binding.reg;
      incoming_parameter_register_known_[parameter.value] = 1;
      target_.params.push_back(binding);
      value.location = reg_operand(binding.reg);
      const std::size_t uses = storage_facts_.parameter_selected_uses[i];
      // Promoted slots leave direct uses the slot census cannot prove.
      const std::size_t direct_uses = facts_.uses[parameter.value];
      const bool incoming_clobbered = !wide_gpr_boundary &&
        (uses || direct_uses) &&
        crosses_register_clobber(parameter.value, binding.reg);
      const bool promoted_incoming_clobbered =
        i < storage_facts_.promoted_parameter_clobbers.size() &&
        (storage_facts_.promoted_parameter_clobbers[i] &
         analysis::register_mask(binding.reg));
      if(uses == 0 && direct_uses == 0) {
        // Slot analysis proved that every apparent use is removed during
        // selection. Keep the ABI binding for MIR, but create no value home.
      } else if(facts_.has_va_start) {
        // va_start/va_arg use the full SysV argument-register scratch set.
        // Keep named variadic parameters in stable frame homes so later uses
        // cannot observe a register clobbered while walking the argument list.
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name,
          parameter.type);
        append_store(parameter_moves_, frame_operand(home),
          reg_operand(binding.reg), parameter.type);
        value.location = frame_operand(home);
      } else if(cross_call_home_known[parameter.value]) {
        value.location = reg_operand(cross_call_homes[parameter.value]);
        value.fixed_register_home = storage_facts_.has(
          parameter.value, StorageFacts::VF_PROMOTED_PARAMETER);
        append_optional_parameter_move(register_parameter_moves,
          register_parameter_move_values, value, parameter.value,
          reg_operand(binding.reg));
      } else if(!wide_gpr_boundary && promoted_incoming_clobbered) {
        const X64Register destination = registers_.is_used(XR_R9) ?
          registers_.allocate(false) : XR_R9;
        if(destination == XR_R9) registers_.reserve(XR_R9);
        value.location = reg_operand(destination);
        value.fixed_register_home = true;
        append_optional_parameter_move(register_parameter_moves,
          register_parameter_move_values, value, parameter.value,
          reg_operand(binding.reg));
      } else if(!wide_gpr_boundary && !incoming_clobbered) {
        // Keep an intact incoming ABI register as the value's selected home.
        value.fixed_register_home =
          storage_facts_.has(parameter.value,
                             StorageFacts::VF_PROMOTED_PARAMETER);
      } else if(wide_gpr_boundary &&
                parameter_crosses_call(parameter.value)) {
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
        append_store(parameter_moves_, frame_operand(home), reg_operand(binding.reg),
                     parameter.type);
        value.location = frame_operand(home);
      } else if(wide_gpr_boundary && (uses || direct_uses) &&
                crosses_register_clobber(parameter.value, binding.reg)) {
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
        append_store(parameter_moves_, frame_operand(home), reg_operand(binding.reg),
                     parameter.type);
        value.location = frame_operand(home);
      } else if(!wide_gpr_boundary &&
                parameter.metadata.passing != lowir_model::PPM_DIRECT && uses) {
        const X64Register destination = crosses_call(parameter.value) ?
          allocate_preserved_for_parameter(parameter.value) :
          registers_.allocate(false);
        value.location = reg_operand(destination);
        append_optional_parameter_move(register_parameter_moves,
          register_parameter_move_values, value, parameter.value,
          reg_operand(binding.reg));
      } else if(!wide_gpr_boundary && incoming_clobbered) {
        const X64Register destination = crosses_call(parameter.value) ?
          allocate_preserved_for_parameter(parameter.value) :
          registers_.allocate(false);
        value.location = reg_operand(destination);
        value.fixed_register_home =
          storage_facts_.has(parameter.value,
                             StorageFacts::VF_PROMOTED_PARAMETER);
        append_optional_parameter_move(register_parameter_moves,
          register_parameter_move_values, value, parameter.value,
          reg_operand(binding.reg));
      }
      if(incoming_pool_reserved[i] &&
         (value.location.kind != MirOperand::OP_REG || value.location.reg != binding.reg))
        registers_.release(binding.reg);
      if(!wide_gpr_boundary || constrained_wide_pressure())
        reserve_direct_parameter_register(binding, value, uses);
      set_value(parameter.value, value);
    }
    append_optional_parameter_moves(
      register_parameter_moves, register_parameter_move_values);
    if(home_unused_register_parameters) {
      frame_bytes_ += 8; // Keep the six-register home area call-aligned.
      return;
    }
    if(!wide_gpr_boundary || constrained_wide_pressure()) return;
    bind_wide_scalar_parameter_homes();
  }
  bool result_is_immediate_return(const lowir_model::LowirBlock & block,
    std::size_t instruction_index, lowir_model::ValueId destination) const
  { return selection::result_is_immediate_return(block, instruction_index,
                                                  destination, facts_); }
  bool result_is_immediate_unary_not_branch(const lowir_model::LowirBlock & block,
    std::size_t instruction_index, lowir_model::ValueId destination) const
  { return selection::result_is_immediate_unary_not_branch(
      block, instruction_index, destination, facts_); }
  bool comparison_feeds_branch(const lowir_model::LowirBlock & block,
    std::size_t instruction_index, const Instruction & comparison) const
  { return selection::result_is_immediate_branch(
      block, instruction_index, comparison.dest, facts_); }
  MirOperand global_operand(MirOperand::Kind kind,
                            const Operand & operand) const
  { return build::global_operand(kind, operand); }
  MirOperand resolve(const Operand & operand) const
  {
    if(operand.kind == Operand::OP_TEMP) {
      if(!value_known_[operand.value])
        throw std::runtime_error("missing lowered temporary " +
          location_planning::diagnostic_value_name(
            program_, source_, operand.value) + " (" +
          std::to_string(static_cast<std::uint32_t>(operand.value)) + ")" +
          " in function @" +
          lowir_model::lowir_symbol_name(program_, source_.symbol) +
          " at instruction " + std::to_string(position_));
      lowir_model::ValueId incoming_value = operand.value;
      if(values_[operand.value].forwarded_parameter.valid())
        incoming_value = values_[operand.value].forwarded_parameter;
      if(incoming_parameter_register_known_[incoming_value]) {
        const X64Register incoming =
          incoming_parameter_registers_[incoming_value];
        if(!facts_.has(operand.value, FunctionFacts::VF_LOOP_INVARIANT) &&
           !facts_.has(incoming_value, FunctionFacts::VF_LOOP_INVARIANT) &&
           !(active_setup_register_clobbers_ & register_mask(incoming)) &&
           incoming_register_is_available(incoming))
          return reg_operand(incoming);
        const lowir_model::ValueId parameter =
          values_[operand.value].selected_parameter_home;
        if(parameter.valid() &&
           !facts_.has(operand.value, FunctionFacts::VF_LOOP_INVARIANT) &&
           !facts_.has(incoming_value, FunctionFacts::VF_LOOP_INVARIANT) &&
           !(active_setup_register_clobbers_ & register_mask(incoming)) &&
           incoming_register_is_available_without_parameter_setup(incoming)) {
          OptionalParameterMove * home = optional_parameter_home(parameter);
          if(!home)
            throw std::logic_error("selected parameter home has no transfer");
          home->required = true;
          return values_[operand.value].location;
        }
      }
      return selected_value_location(operand.value);
    }
    if(operand.kind == Operand::OP_SLOT) {
      const std::uint32_t slot = operand.slot;
      if(slot >= slot_offsets_.size() || !slot_offset_known_[slot])
        throw std::runtime_error("missing frame slot");
      return frame_operand(slot_offsets_[slot]);
    }
    if(operand.kind == Operand::OP_INTEGER)
      return immediate(integer_value(operand));
    if(operand.kind == Operand::OP_FLOAT)
      return float_immediate(
        operand.literal_low, operand.literal_high,
        operand.has_spelling ? operand.literal :
          lowir_model::StringId());
    if(operand.kind == Operand::OP_GLOBAL)
      return build::global_operand(MirOperand::OP_SYMBOL, operand);
    if(operand.kind == Operand::OP_LABEL)
      return native_block_operand(source_, operand);
    throw std::runtime_error("foundation operand kind is not implemented");
  }
  MirOperand selected_value_location(lowir_model::ValueId value) const
  {
    const MirOperand location = values_[value].location;
    const lowir_model::ValueId parameter =
      values_[value].selected_parameter_home;
    if(parameter.valid()) {
      OptionalParameterMove * home = optional_parameter_home(parameter);
      if(!home)
        throw std::logic_error("selected parameter home has no transfer");
      home->required = true;
    }
    return location;
  }
  const LowType & operand_type(const Operand & operand) const
  {
    if(operand.kind == Operand::OP_TEMP) {
      if(!value_known_[operand.value])
        throw std::runtime_error("missing operand type for " +
          location_planning::diagnostic_value_name(
            program_, source_, operand.value) +
          " in function @" +
          lowir_model::lowir_symbol_name(program_, source_.symbol) +
          " at instruction " + std::to_string(position_));
      return values_[operand.value].type;
    }
    if(operand.kind == Operand::OP_SLOT)
      return lowir_model::lowir_slot_type(source_, operand.slot);
    if(operand.kind == Operand::OP_GLOBAL)
      return lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
    if(operand.kind == Operand::OP_FLOAT) return operand.literal_type;
    return lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
  }
  void move_value_to_register(std::vector<MirInstruction> & out,
                              X64Register destination,
                              const MirOperand & source,
                              const LowType & type)
  {
    const MirOperand target = reg_operand(destination);
    if(source.kind == MirOperand::OP_FRAME || source.kind == MirOperand::OP_GLOBAL ||
       source.kind == MirOperand::OP_DEREF) {
      append_load(out, target, source, type);
      if(is_integer_or_pointer(type)) append_integer_normalization(out, type, target);
    } else append_move(out, target, source);
  }
  MirOperand storage(const Operand & operand) const
  {
    if(operand.kind == Operand::OP_GLOBAL)
      return build::global_operand(MirOperand::OP_GLOBAL, operand);
    if(operand.kind == Operand::OP_SLOT) {
      const std::uint32_t slot = operand.slot;
      if(slot >= slot_offsets_.size() || !slot_offset_known_[slot])
        throw std::runtime_error("missing frame slot");
      return frame_operand(slot_offsets_[slot]);
    }
    const MirOperand address = resolve(operand);
    if(address.kind != MirOperand::OP_REG)
      throw std::runtime_error("storage address is not register-resident");
    return dereference(address.reg);
  }
  MirOperand materialized_storage(const Operand & operand,
                                  std::vector<MirInstruction> & out,
                                  X64Register scratch = XR_RCX)
  {
    if(operand.kind == Operand::OP_GLOBAL) {
      const lowir_model::SymbolId wrapper = tls_wrappers_[operand.symbol];
      if(!wrapper.valid()) return storage(operand);
      MirInstruction address = machine_instruction(MirInstruction::MI_TLS_ADDR);
      append_operand(address, reg_operand(XR_R11));
      append_operand(address, symbol_operand(MirOperand::OP_SYMBOL, wrapper));
      address.tls_storage_symbol = operand.symbol;
      out.push_back(address);
      return dereference(XR_R11);
    }
    if(operand.kind == Operand::OP_SLOT)
      return storage(operand);
    if(operand.kind == Operand::OP_TEMP) {
      if(value_known_[operand.value] && values_[operand.value].deferred_address)
        return selected_value_location(operand.value);
    }
    if(is_frame_address(operand)) {
      const MirOperand address = resolve(operand);
      if(address.kind == MirOperand::OP_FRAME ||
         address.kind == MirOperand::OP_DEREF)
        return address;
      append_address(out, scratch, address);
      return dereference(scratch);
    }
    const MirOperand address = resolve(operand);
    if(address.kind == MirOperand::OP_REG) return dereference(address.reg);
    if(address.kind == MirOperand::OP_SYMBOL) {
      MirOperand global = address;
      global.kind = MirOperand::OP_GLOBAL;
      return global;
    }
    move_value_to_register(out, scratch, address, operand_type(operand));
    return dereference(scratch);
  }
  wide::Value wide_value(const Operand & operand) const
  {
    if(operand.kind == Operand::OP_INTEGER)
      return wide::literal_value(operand);
    return wide::storage_value(resolve(operand));
  }
  bool block_transfers_into(lowir_model::ValueId phi) const
  {
    if(current_block_id_ >= phi_transfers_.size()) return false;
    const std::vector<phi_detail::Transfer> & transfers =
      phi_transfers_[current_block_id_];
    for(std::size_t i = 0; i < transfers.size(); ++i)
      if(transfers[i].destination == phi) return true;
    return false;
  }
  bool value_is_block_transfer_source(lowir_model::ValueId value) const
  {
    if(current_block_id_ >= phi_transfers_.size()) return false;
    const std::vector<phi_detail::Transfer> & transfers =
      phi_transfers_[current_block_id_];
    for(std::size_t i = 0; i < transfers.size(); ++i)
      if(transfers[i].source.kind == Operand::OP_TEMP &&
         transfers[i].source.value == value) return true;
    return false;
  }
  // The active instruction's result may take over a backedge-fed phi home
  // only when it cannot outlive this block's phi transfers: it dies before
  // the terminator, or its sole use IS a transfer at the terminator (a
  // deferred-compare read after the transfers would see the rewrite).
  bool phi_home_takeover_tail_allowed() const
  {
    if(!active_instruction_ || !active_instruction_->dest.valid())
      return false;
    const lowir_model::ValueId dest = active_instruction_->dest;
    if(facts_.has(dest, FunctionFacts::VF_EDGE_LIVE)) return false;
    if(facts_.last_use[dest] == FunctionFacts::missing_position())
      return false;
    if(facts_.last_use[dest] < current_block_last_position_) return true;
    return facts_.uses[dest] == 1 && value_is_block_transfer_source(dest);
  }
  bool phi_backedge_takeover_allowed(lowir_model::ValueId id) const
  {
    return value_holds_planned_register(id) && block_transfers_into(id) &&
      phi_home_takeover_tail_allowed();
  }
  // A fully consumed planned phi awaiting its backedge rewrite may share
  // its register with the value chain that computes the next iteration's
  // value; any other co-resident blocks destructive reuse.
  bool location_alias_blocks_reuse(lowir_model::ValueId id,
                                   const MirOperand & location) const
  {
    if(location.kind != MirOperand::OP_REG) return true;
    const bool takeover = phi_home_takeover_tail_allowed();
    const std::vector<lowir_model::ValueId> & occupants =
      live_locations_.gpr_values(location.reg);
    for(std::size_t i = 0; i < occupants.size(); ++i) {
      const lowir_model::ValueId occupant = occupants[i];
      if(occupant == id) continue;
      if(takeover && facts_.uses[occupant] == 0 &&
         phi_planned_home_[occupant] != 0 &&
         block_transfers_into(occupant)) continue;
      return true;
    }
    return false;
  }
  bool can_reuse(const Operand & operand) const
  {
    if(operand.kind != Operand::OP_TEMP) return false;
    const lowir_model::ValueId id = operand.value;
    if(facts_.uses[id] != 1 || !value_known_[id] ||
       values_[id].location.kind != MirOperand::OP_REG ||
       location_alias_blocks_reuse(id, values_[id].location))
      return false;
    // A phi home register is rewritten by this block's own backedge
    // transfer, so the chain computing the next iteration's value may run
    // destructively in the register; any other consumer may not.
    if(phi_planned_home_[id] != 0) return phi_backedge_takeover_allowed(id);
    const bool destructive_parameter =
      incoming_parameter_register_known_[id] &&
      facts_.has(id, FunctionFacts::VF_DESTRUCTIVE_PARAMETER);
    const bool reusable_destructive_parameter = destructive_parameter &&
      !facts_.has(id, FunctionFacts::VF_LOOP_INVARIANT);
    return (!values_[id].parameter || reusable_destructive_parameter) &&
           !values_[id].fixed_register_home &&
           (!facts_.has(id, FunctionFacts::VF_EDGE_LIVE) ||
            facts_.has(id, FunctionFacts::VF_EXACT_FORWARD_EDGE) ||
            reusable_destructive_parameter);
  }
  void consume(const Operand & operand, X64Register retained = XR_RSP)
  {
    if(operand.kind != Operand::OP_TEMP) return;
    const lowir_model::ValueId id = operand.value;
    if(facts_.uses[id] == 0)
      throw std::runtime_error("invalid temporary use count");
    const bool interval_over = facts_.uses[id] == 1 &&
      value_outlives_counted_uses(id) &&
      planned_interval_over(id);
    // An unplanned edge-live register is also genuinely dead at a final
    // counted use lying outside every backedge and backward exception
    // span — nothing can re-execute a read.  Parameters keep their homes:
    // promoted-slot forwarding replays them beyond their counted uses.
    const bool span_free_interval_over = facts_.uses[id] == 1 &&
      value_outlives_counted_uses(id) && !interval_over &&
      phi_planned_home_[id] == 0 &&
      !values_[id].parameter && !values_[id].fixed_register_home &&
      values_[id].location.kind == MirOperand::OP_REG &&
      position_outside_extension_spans(position_);
    const bool stops_being_live = (facts_.uses[id] == 1 &&
      !value_outlives_counted_uses(id)) || interval_over ||
      span_free_interval_over;
    if(facts_.uses[id] == 1 && value_outlives_counted_uses(id) &&
       !interval_over && !span_free_interval_over)
      maybe_schedule_span_end_release(id);
    --facts_.uses[id];
    if(stops_being_live)
      live_locations_.remove(id, values_[id].location);
    if(interval_over || span_free_interval_over) note_planned_release(id);
    if(facts_.uses[id] == 0) {
      const ValueFact & value = values_[id];
      // An edge-live register outlives its final use unless the planned
      // interval end cleared every backedge and backward exception region.
      // The alias query must not count the value itself (removed above).
      // A planned loop-invariant resident may release once its extended
      // interval (which covers every re-reading span) is over.
      if(value.location.kind == MirOperand::OP_REG &&
         !value.parameter &&
         !value.fixed_register_home &&
         (!facts_.has(id, FunctionFacts::VF_LOOP_INVARIANT) ||
          interval_over || span_free_interval_over) &&
         (!value_outlives_counted_uses(id) || interval_over ||
          span_free_interval_over) &&
         value.location.reg != retained && value.location.reg != XR_RAX &&
         !live_locations_.has_alias(id, value.location, false)) {
        registers_.release(value.location.reg);
        if(interval_over || span_free_interval_over)
          hold_released_for_plan(value.location.reg);
        if(interval_over && stats_) ++stats_->planned_interval_releases;
        if(span_free_interval_over && stats_)
          ++stats_->span_free_edge_releases;
      }
      if(!value.parameter && value.location.kind == MirOperand::OP_XMM &&
         !facts_.has(id, FunctionFacts::VF_LOOP_INVARIANT) &&
         !facts_.has(id, FunctionFacts::VF_EDGE_LIVE) &&
         !has_live_location_alias(id, value.location))
        xmms_.release(value.location.xmm);
      if(value.deferred_address) {
        consume(value.deferred_address_base, retained);
        consume(value.deferred_address_index, retained);
      }
    }
  }
  bool current_instruction_uses(lowir_model::ValueId value) const
  {
    if(!active_instruction_) return false;
    const Operand * fixed[] = {
      &active_instruction_->first, &active_instruction_->second,
      &active_instruction_->third
    };
    for(std::size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
      if(fixed[i]->kind != Operand::OP_TEMP) continue;
      if(fixed[i]->value == value) return true;
      const lowir_model::ValueId fixed_value = fixed[i]->value;
      if(value_known_[fixed_value] && values_[fixed_value].deferred_address &&
         ((values_[fixed_value].deferred_address_base.kind == Operand::OP_TEMP &&
           values_[fixed_value].deferred_address_base.value == value) ||
          (values_[fixed_value].deferred_address_index.kind == Operand::OP_TEMP &&
           values_[fixed_value].deferred_address_index.value == value)))
        return true;
    }
    for(std::size_t i = 0; i < active_instruction_->args.size(); ++i)
      if(active_instruction_->args[i].kind == Operand::OP_TEMP &&
         active_instruction_->args[i].value == value) return true;
    return false;
  }
  static bool managed_register(X64Register reg)
  { return location_planning::managed_register(reg); }
  // A retained dereference operand is replayed at every later consumer, so
  // its carrier registers may never reenter the allocation pool.
  void reserve_deferred_address_carriers(const MirOperand & address)
  {
    if(address.kind != MirOperand::OP_DEREF) return;
    if(managed_register(address.reg)) {
      if(!registers_.is_used(address.reg)) registers_.reserve(address.reg);
      mark_deferred_carrier(address.reg);
    }
    if(address.has_index && managed_register(address.index)) {
      if(!registers_.is_used(address.index))
        registers_.reserve(address.index);
      mark_deferred_carrier(address.index);
    }
  }
  void set_value(lowir_model::ValueId value, const ValueFact & replacement)
  {
    const bool live = value_is_live(value);
    if(live && value_known_[value])
      live_locations_.remove(value, values_[value].location);
    values_[value] = replacement;
    value_known_[value] = 1;
    if(live) live_locations_.add(value, replacement.location);
  }
  void set_value_location(lowir_model::ValueId value,
                          const MirOperand & replacement)
  {
    if(!value_known_[value])
      throw std::logic_error("cannot move an unknown native value");
    const bool live = value_is_live(value);
    if(live) live_locations_.remove(value, values_[value].location);
    values_[value].location = replacement;
    if(replacement.kind == MirOperand::OP_FRAME &&
       replacement.frame_binding != 0) {
      values_[value].has_spill_home = true;
      values_[value].spill_home = replacement;
    }
    if(live) live_locations_.add(value, replacement);
  }
  bool has_live_location_alias(lowir_model::ValueId value,
                               const MirOperand & location) const
  {
    return live_locations_.has_alias(value, location, value_is_live(value));
  }
  void stabilize_edge_live_result(
      const Instruction & instruction,
      std::vector<MirInstruction> & out)
  {
    if(!instruction.dest.valid() ||
       !facts_.has(instruction.dest, FunctionFacts::VF_EDGE_LIVE) ||
       (!facts_.has(instruction.dest, FunctionFacts::VF_LOOP_INVARIANT) &&
        !result_crosses_call(instruction.dest))) return;
    if(!value_known_[instruction.dest]) return;
    ValueFact & value = values_[instruction.dest];
    const MirOperand location = value.location;
    if(location.kind != MirOperand::OP_REG &&
       location.kind != MirOperand::OP_XMM) return;
    if(facts_.has(instruction.dest, FunctionFacts::VF_EXACT_FORWARD_EDGE)) {
      if(stats_) ++stats_->exact_forward_edge_register_retains;
      return;
    }
    // The planner proved the register free for the whole interval.
    if(value_holds_planned_register(instruction.dest)) {
      if(stats_) ++stats_->planned_edge_residencies;
      return;
    }
    const bool crosses = result_crosses_call(instruction.dest);
    const bool narrow_register_alias = location.kind == MirOperand::OP_REG &&
      selection::is_narrow_integer(value.type) &&
      has_live_location_alias(instruction.dest, location);
    const bool fixed_register_clobber = location.kind == MirOperand::OP_REG &&
      crosses_register_clobber(instruction.dest, location.reg);
    if(location_planning::should_retain_edge_register(
         location, optimization_level_, facts_.has_eh,
         facts_.has(instruction.dest, FunctionFacts::VF_LOOP_INVARIANT),
         crosses, narrow_register_alias, fixed_register_clobber,
         registers_, xmms_)) {
      if(location.kind == MirOperand::OP_REG &&
         selection::is_narrow_integer(value.type))
        append_integer_normalization(out, value.type, location);
      if(stats_) ++stats_->planned_edge_register_retains;
      return;
    }
    const MirOperand home =
      allocate_temp_frame_binding(instruction.dest, value.type, THR_EDGE_LIVE);
    if(location.kind == MirOperand::OP_XMM) {
      append_float_move(out, home, location,
                        value.type);
      if(!has_live_location_alias(instruction.dest, location))
        xmms_.release(location.xmm);
    } else {
      append_store(out, home, location,
                   value.type);
      if(!has_live_location_alias(instruction.dest, location)) {
        registers_.release(location.reg);
      }
    }
    set_value_location(instruction.dest, home);
  }
  bool try_reserve_result_register(lowir_model::ValueId value,
                                   bool needs_callee_saved,
                                   X64Register * result)
  {
    if(try_planned_grant(value, result)) return true;
    static const X64Register caller_saved[] = {
      XR_R8, XR_R9, XR_RDI, XR_RSI
    };
    if(!needs_callee_saved)
      for(std::size_t i = 0;
          i < sizeof(caller_saved) / sizeof(caller_saved[0]); ++i)
        if(!crosses_register_clobber(value, caller_saved[i]) &&
           registers_.try_reserve(caller_saved[i])) {
          *result = caller_saved[i];
          return true;
        }
    return try_allocate_preserved_avoiding_plans(
      position_, reactive_lifetime_end(value), result);
  }
  bool try_allocate_result(lowir_model::ValueId value,
                           std::vector<MirInstruction> & out,
                           X64Register * result,
                           bool force_preserved = false)
  {
    const bool across = force_preserved || result_crosses_call(value);
    if(try_reserve_result_register(value, across, result)) return true;
    if(reclaim_dead_parameter_register(across) &&
       try_reserve_result_register(value, across, result)) return true;
    if(spill_one(across, out) &&
       try_reserve_result_register(value, across, result)) return true;
    return false;
  }
  X64Register allocate_result(lowir_model::ValueId value,
                              std::vector<MirInstruction> & out,
                              bool force_preserved = false)
  {
    X64Register result = XR_RSP;
    if(try_allocate_result(value, out, &result, force_preserved)) return result;
    throw std::runtime_error("reactive GPR allocation exhausted in " +
      lowir_model::lowir_symbol_name(program_, source_.symbol) + " for " +
      location_planning::diagnostic_value_name(program_, source_, value) +
      " at LowIR position " +
	  std::to_string(position_));
  }
  MirOperand allocate_temp_home(lowir_model::ValueId value,
                                const LowType & type,
                                TemporaryHomeReason reason = THR_COUNT)
  {
    return allocate_temp_frame_binding(value, type, reason);
  }
  MirOperand allocate_named_temp_home(lowir_model::FixedPresentationName name,
                                      const LowType & type)
  {
    const long long offset = allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP, name, type);
    return frame_operand(offset,
      static_cast<std::uint32_t>(target_.frame_bindings.size()));
  }
  MirOperand allocate_float_result(lowir_model::ValueId value,
                                   const LowType & type)
  {
    uses_scalar_float_ = true;
    if(is_extended_float(type)) return allocate_temp_home(value, type);
    if(result_crosses_call(value)) return allocate_temp_home(value, type);
    XmmRegister result = XMM_0;
    if(xmms_.try_allocate(result)) return xmm_operand(result);
    return allocate_temp_home(value, type);
  }
  void define(lowir_model::ValueId id, const LowType & type,
              const MirOperand & location)
  {
    record_planned_definition(id, location);
    remember_selected_register_definition(location, position_);
    ValueFact value;
    value.location = location;
    value.type = type;
    if(location.kind == MirOperand::OP_FRAME && location.frame_binding != 0) {
      value.has_spill_home = true;
      value.spill_home = location;
    }
    set_value(id, value);
    if(facts_.uses[id] != 0) return;
    if(location.kind == MirOperand::OP_REG && registers_.is_used(location.reg) &&
       !has_live_location_alias(id, location)) {
      registers_.release(location.reg);
    }
    else if(location.kind == MirOperand::OP_XMM && xmms_.is_used(location.xmm) &&
            !has_live_location_alias(id, location))
      xmms_.release(location.xmm);
  }
  bool is_frame_address(const Operand & operand) const
  {
    if(operand.kind != Operand::OP_TEMP) return false;
    return value_known_[operand.value] && values_[operand.value].frame_address;
  }
  void append_address(std::vector<MirInstruction> & out,
                      X64Register destination,
                      const MirOperand & source)
  {
    MirInstruction lea = machine_instruction(MirInstruction::MI_LEA);
    append_operand(lea, reg_operand(destination));
    append_operand(lea, source);
    out.push_back(lea);
  }
  void emit_operand_address(std::vector<MirInstruction> & out,
                            X64Register destination,
                            const Operand & operand)
  {
    if(operand.kind == Operand::OP_SLOT) {
      append_address(out, destination, storage(operand));
      return;
    }
    if(operand.kind == Operand::OP_GLOBAL) {
      const lowir_model::SymbolId wrapper = tls_wrappers_[operand.symbol];
      if(wrapper.valid()) {
        MirInstruction address = machine_instruction(MirInstruction::MI_TLS_ADDR);
        append_operand(address, reg_operand(destination));
        append_operand(address, symbol_operand(MirOperand::OP_SYMBOL, wrapper));
        address.tls_storage_symbol = operand.symbol;
        out.push_back(address);
        return;
      }
      append_move(out, reg_operand(destination),
                  build::global_operand(MirOperand::OP_SYMBOL, operand));
      return;
    }
    if(operand.kind != Operand::OP_TEMP)
      throw std::runtime_error("bulk object operand kind is not addressable");
    if(!value_known_[operand.value])
      throw std::runtime_error("missing address value");
    const ValueFact & value = values_[operand.value];
    const MirOperand location = selected_value_location(operand.value);
    if(value.deferred_address || value.frame_address ||
       value.type.kind == lowir_model::LTK_OBJECT ||
       wide::is_integer(value.type)) {
      if(location.kind == MirOperand::OP_FRAME || location.kind == MirOperand::OP_DEREF) {
        append_address(out, destination, location);
        return;
      }
      if(location.kind == MirOperand::OP_GLOBAL || location.kind == MirOperand::OP_SYMBOL) {
        MirOperand address = location;
        address.kind = MirOperand::OP_SYMBOL;
        append_move(out, reg_operand(destination), address);
        return;
      }
    }
    move_value_to_register(out, destination, location,
                           lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
  }
  MirOperand make_addressable(const Operand & operand,
                              std::vector<MirInstruction> & out)
  {
    if(operand.kind == Operand::OP_SLOT) return storage(operand);
    if(operand.kind == Operand::OP_GLOBAL)
      return build::global_operand(MirOperand::OP_SYMBOL, operand);
    if(operand.kind != Operand::OP_TEMP)
      throw std::runtime_error("call argument cannot be passed by address");
    if(!value_known_[operand.value])
      throw std::runtime_error("missing addressable temporary");
    ValueFact & found = values_[operand.value];
    if(found.frame_address || found.location.kind == MirOperand::OP_FRAME)
      return found.location;
    const MirOperand selected = selected_value_location(operand.value);
    const MirOperand home = allocate_temp_home(
      operand.value, found.type, THR_ADDRESS_ESCAPE);
    if(selected.kind == MirOperand::OP_XMM)
      append_float_move(out, home, selected, found.type);
    else
      append_store(out, home, selected, found.type);
    if(selected.kind == MirOperand::OP_REG &&
       !has_live_location_alias(operand.value, selected))
      registers_.release(selected.reg);
    else if(selected.kind == MirOperand::OP_XMM &&
            !has_live_location_alias(operand.value, selected))
      xmms_.release(selected.xmm);
    set_value_location(operand.value, home);
    return home;
  }
  bool frame_provenance(const Operand & operand, long long & offset) const
  {
    if(operand.kind == Operand::OP_SLOT) {
      const std::uint32_t slot = operand.slot;
      if(slot >= slot_offsets_.size() || !slot_offset_known_[slot]) return false;
      offset = slot_offsets_[slot];
      return true;
    }
    if(operand.kind != Operand::OP_TEMP) return false;
    if(!value_known_[operand.value] ||
       !values_[operand.value].has_frame_provenance) return false;
    offset = values_[operand.value].frame_provenance;
    return true;
  }
  bool aliases_same_object(const Operand & source, const Operand & destination) const
  {
    long long source_offset = 0;
    long long destination_offset = 0;
    return frame_provenance(source, source_offset) &&
      frame_provenance(destination, destination_offset) &&
      source_offset == destination_offset;
  }
  bool result_crosses_call(lowir_model::ValueId value) const
  {
    return (facts_.last_use[value] != FunctionFacts::missing_position() &&
            crosses_call(value)) ||
      storage_facts_.has(value, StorageFacts::VF_TLS_STORE_INPUT);
  }
  MirOperand binary_destination(const Instruction & instruction,
                                const MirOperand & left,
                                std::vector<MirInstruction> & out,
                                bool allow_same_instruction_duplicate = false,
                                MirOperand * pressure_home = 0,
                                const LowType * pressure_type = 0)
  {
    // A planned resident's register may be re-read after a backedge:
    // duplicates take it over only under the phi takeover conditions.
    const bool duplicate_last_use = allow_same_instruction_duplicate &&
      instruction.first.kind == Operand::OP_TEMP &&
      instruction.second.kind == Operand::OP_TEMP &&
      instruction.first.value == instruction.second.value &&
      facts_.uses[instruction.first.value] == 2 &&
      !values_[instruction.first.value].parameter &&
      (!value_holds_planned_register(instruction.first.value) ||
       (phi_planned_home_[instruction.first.value] != 0 &&
        phi_backedge_takeover_allowed(instruction.first.value)));
    const bool safe_reuse = (can_reuse(instruction.first) || duplicate_last_use) &&
      !crosses_register_clobber(instruction.dest, left.reg);
    if(safe_reuse) return left;
    X64Register result = XR_RSP;
    if(!try_allocate_result(instruction.dest, out, &result)) {
      if(!pressure_home)
        throw std::runtime_error("reactive GPR allocation exhausted");
      *pressure_home = allocate_temp_home(
        instruction.dest, pressure_type ? *pressure_type : instruction.type);
      result = XR_RAX;
    }
    const MirOperand destination = reg_operand(result);
    move_value_to_register(out, destination.reg, left, operand_type(instruction.first));
    if(left.kind == MirOperand::OP_FRAME || left.kind == MirOperand::OP_GLOBAL ||
       left.kind == MirOperand::OP_DEREF)
      append_integer_normalization(out, operand_type(instruction.first), destination);
    return destination;
  }
  void emit_float_const(const Instruction & instruction,
                        std::vector<MirInstruction> & out)
  {
    const MirOperand destination = allocate_float_result(instruction.dest, instruction.type);
    append_float_move(out, destination,
                      float_immediate(
                        instruction.first.literal_low,
                        instruction.first.literal_high,
                        instruction.first.has_spelling ?
                          instruction.first.literal :
                          lowir_model::StringId()),
                      instruction.type);
    define(instruction.dest, instruction.type, destination);
  }
  void emit_float_copy(const Instruction & instruction,
                       std::vector<MirInstruction> & out)
  {
    const MirOperand destination = allocate_float_result(instruction.dest, instruction.type);
    append_float_move(out, destination, resolve(instruction.first), instruction.type);
    consume(instruction.first);
    define(instruction.dest, instruction.type, destination);
  }
  void emit_float_binary(const Instruction & instruction,
                         std::vector<MirInstruction> & out)
  {
    const MirOperand destination = allocate_float_result(instruction.dest, instruction.type);
    MirInstruction operation = machine_instruction(
      float_binary_opcode(instruction.op), instruction.type);
    append_operand(operation, destination);
    append_operand(operation, resolve(instruction.first));
    append_operand(operation, resolve(instruction.second));
    out.push_back(operation);
    consume(instruction.first);
    consume(instruction.second);
    define(instruction.dest, instruction.type, destination);
  }
  void emit_float_direct_compare_branch(const Instruction & comparison,
                                        const Instruction & branch,
                                        std::vector<MirInstruction> & out,
                                        bool skip_branch = true)
  {
    uses_scalar_float_ = true;
    MirInstruction compare = machine_instruction(MirInstruction::MI_FCMP,
                                                 comparison.type);
    append_operand(compare, resolve(comparison.first));
    append_operand(compare, resolve(comparison.second));
    out.push_back(compare);
    MirInstruction unordered = machine_instruction(MirInstruction::MI_JCC);
    unordered.condition = XC_P;
    append_operand(unordered, native_block_operand(source_,
      comparison.op.kind == LowOperation::LOP_NE ? branch.second : branch.third));
    out.push_back(unordered);
    MirInstruction jump_true = machine_instruction(MirInstruction::MI_JCC);
    jump_true.condition = float_predicate_condition(comparison.op);
    append_operand(jump_true, native_block_operand(source_, branch.second));
    out.push_back(jump_true);
    MirInstruction jump_false = machine_instruction(MirInstruction::MI_JMP);
    append_operand(jump_false, native_block_operand(source_, branch.third));
    out.push_back(jump_false);
    consume(comparison.first);
    consume(comparison.second);
    if(skip_branch) skipped_position_ = position_ + 1;
  }
  void emit_float_compare_value(const Instruction & instruction,
                                const lowir_model::LowirBlock & block,
                                std::size_t instruction_index,
                                std::vector<MirInstruction> & out)
  {
    uses_scalar_float_ = true;
    const LowType & result_type =
      lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
    MirInstruction compare = machine_instruction(float_compare_opcode(instruction.op),
                                                 instruction.type);
    append_operand(compare, reg_operand(XR_RAX));
    append_operand(compare, resolve(instruction.first));
    append_operand(compare, resolve(instruction.second));
    out.push_back(compare);
    MirOperand destination = reg_operand(XR_RAX);
    if(!result_is_immediate_return(block, instruction_index, instruction.dest)) {
      X64Register result = XR_RSP;
      if(try_allocate_result(instruction.dest, out, &result)) {
        destination = reg_operand(result);
        append_move(out, destination, reg_operand(XR_RAX));
      } else {
        destination = allocate_temp_home(instruction.dest, result_type);
        append_store(out, destination, reg_operand(XR_RAX),
          result_type);
      }
    }
    consume(instruction.first);
    consume(instruction.second);
    define(instruction.dest, result_type, destination);
  }
  void emit_float_load(const Instruction & instruction,
                       std::vector<MirInstruction> & out)
  {
    const MirOperand destination = allocate_float_result(instruction.dest, instruction.type);
    append_float_move(out, destination, materialized_storage(instruction.first, out),
                      instruction.type);
    consume(instruction.first);
    define(instruction.dest, instruction.type, destination);
  }
  void append_float_width_conversion(std::vector<MirInstruction> & out,
                                     const MirOperand & destination,
                                     const MirOperand & source,
                                     const LowType & source_type,
                                     const LowType & destination_type)
  {
    MirInstruction conversion = machine_instruction(
      lowir_model::lowir_type_bit_width(source_type) <
        lowir_model::lowir_type_bit_width(destination_type) ?
        MirInstruction::MI_FPEXT : MirInstruction::MI_FPTRUNC,
      destination_type);
    conversion.source_type = source_type;
    append_operand(conversion, destination);
    append_operand(conversion, source);
    out.push_back(conversion);
  }
  void emit_float_store(const Instruction & instruction,
                        std::vector<MirInstruction> & out)
  {
    uses_scalar_float_ = true;
    const MirOperand destination = materialized_storage(instruction.second, out);
    const LowType & source_type = operand_type(instruction.first);
    if(is_floating(source_type) &&
       !lowir_model::same_lowir_type(source_type, instruction.type))
      append_float_width_conversion(out, destination, resolve(instruction.first),
                                    source_type, instruction.type);
    else
      append_float_move(out, destination, resolve(instruction.first), instruction.type);
    consume(instruction.first);
    consume(instruction.second);
  }
  void emit_float_unary(const Instruction & instruction,
                        std::vector<MirInstruction> & out)
  {
    if(instruction.op.kind != LowOperation::LOP_NEG)
      throw std::runtime_error(std::string("floating unary operation is not implemented: ") +
                               lowir_model::lowir_operation_text(instruction.op));
    const MirOperand destination = allocate_float_result(instruction.dest, instruction.type);
    MirInstruction negate = machine_instruction(MirInstruction::MI_FNEG,
                                                instruction.type);
    append_operand(negate, destination);
    append_operand(negate, resolve(instruction.first));
    out.push_back(negate);
    consume(instruction.first);
    define(instruction.dest, instruction.type, destination);
  }
  void emit_convert(const Instruction & instruction,
                    const lowir_model::LowirBlock & block,
                    std::size_t instruction_index,
                    std::vector<MirInstruction> & out)
  {
    const bool source_wide = wide::is_integer(instruction.source_type), destination_wide = wide::is_integer(instruction.type);
    if(source_wide && is_integer_or_pointer(instruction.type)) {
      MirOperand pressure_home;
      X64Register result = XR_RSP;
      if(!try_allocate_result(instruction.dest, out, &result)) {
        pressure_home = allocate_temp_home(instruction.dest, instruction.type);
        result = XR_RAX;
      }
      const MirOperand destination = reg_operand(result);
      wide::append_word_to_register(wide_value(instruction.first), 0,
                                    destination.reg, XR_R11, out);
      append_integer_normalization(out, instruction.type, destination);
      consume(instruction.first, destination.reg);
      if(pressure_home.kind == MirOperand::OP_FRAME)
        append_store(out, pressure_home, destination, instruction.type);
      define(instruction.dest, instruction.type,
             pressure_home.kind == MirOperand::OP_FRAME ? pressure_home : destination);
      return;
    }
    if(destination_wide && is_integer_or_pointer(instruction.source_type)) { const MirOperand destination = allocate_temp_home(instruction.dest, instruction.type); move_value_to_register(out, XR_RAX, resolve(instruction.first), instruction.source_type); append_integer_normalization(out, instruction.source_type, reg_operand(XR_RAX)); if(instruction.op.kind == LowOperation::LOP_SEXT) out.push_back(machine_instruction(MirInstruction::MI_CQO)); else append_move(out, reg_operand(XR_RDX), immediate(0)); append_store(out, destination, reg_operand(XR_RAX), machine_type(lowir_model::LTK_I64)); MirOperand high = destination; high.offset += 8; append_store(out, high, reg_operand(XR_RDX), machine_type(lowir_model::LTK_I64)); consume(instruction.first); define(instruction.dest, instruction.type, destination); return; }
    const bool source_float = is_floating(instruction.source_type);
    const bool destination_float = is_floating(instruction.type);
    if(destination_wide && source_float) {
      uses_scalar_float_ = true;
      const MirOperand destination =
        allocate_temp_home(instruction.dest, instruction.type);
      MirInstruction::Opcode opcode;
      if(instruction.op.kind == LowOperation::LOP_FPTOSI) opcode = MirInstruction::MI_FPTOSI;
      else if(instruction.op.kind == LowOperation::LOP_FPTOUI) opcode = MirInstruction::MI_FPTOUI;
      else throw std::runtime_error(
        std::string("floating-to-i128 conversion is not implemented: ") +
        lowir_model::lowir_operation_text(instruction.op));
      MirInstruction conversion = machine_instruction(
        opcode, lowir_model::builtin_lowir_type(lowir_model::LTK_I128));
      conversion.source_type = instruction.source_type;
      append_operand(conversion, reg_operand(XR_RAX));
      append_operand(conversion, reg_operand(XR_RDX));
      append_operand(conversion, resolve(instruction.first));
      out.push_back(conversion);
      append_store(out, destination, reg_operand(XR_RAX),
                   lowir_model::builtin_lowir_type(lowir_model::LTK_I64));
      MirOperand high = destination;
      high.offset += 8;
      append_store(out, high, reg_operand(XR_RDX),
                   lowir_model::builtin_lowir_type(lowir_model::LTK_I64));
      consume(instruction.first);
      define(instruction.dest, instruction.type, destination);
      return;
    }
    if(source_float || destination_float) {
      uses_scalar_float_ = true;
      MirInstruction::Opcode opcode = MirInstruction::MI_SITOFP;
      if(instruction.op.kind == LowOperation::LOP_UITOFP) opcode = MirInstruction::MI_UITOFP;
      else if(instruction.op.kind == LowOperation::LOP_FPTOSI) opcode = MirInstruction::MI_FPTOSI;
      else if(instruction.op.kind == LowOperation::LOP_FPTOUI) opcode = MirInstruction::MI_FPTOUI;
      else if(instruction.op.kind == LowOperation::LOP_FPEXT) opcode = MirInstruction::MI_FPEXT;
      else if(instruction.op.kind == LowOperation::LOP_FPTRUNC) opcode = MirInstruction::MI_FPTRUNC;
      else if(instruction.op.kind != LowOperation::LOP_SITOFP)
        throw std::runtime_error(std::string("floating conversion is not implemented: ") +
                                 lowir_model::lowir_operation_text(instruction.op));
      MirOperand pressure_home;
      MirOperand destination;
      if(destination_float)
        destination = allocate_float_result(instruction.dest, instruction.type);
      else {
        X64Register result = XR_RSP;
        if(!try_allocate_result(instruction.dest, out, &result)) {
          pressure_home = allocate_temp_home(instruction.dest, instruction.type);
          result = XR_RAX;
        }
        destination = reg_operand(result);
      }
      const LowType & source_machine_type =
        is_integer_or_pointer(instruction.source_type) ?
        integer_machine_type(lowir_model::lowir_type_bit_width(
          instruction.source_type)) :
        instruction.source_type;
      const LowType & destination_machine_type =
        is_integer_or_pointer(instruction.type) ?
        integer_machine_type(lowir_model::lowir_type_bit_width(
          instruction.type)) : instruction.type;
      MirInstruction conversion =
        machine_instruction(opcode, destination_machine_type);
      conversion.source_type = source_machine_type;
      append_operand(conversion, destination);
      if(source_wide && destination_float) {
        const wide::Value source = wide_value(instruction.first);
        wide::append_word_to_register(
          source, 0, XR_RAX, XR_R11, out);
        wide::append_word_to_register(
          source, 1, XR_RDX, XR_R11, out);
        append_operand(conversion, reg_operand(XR_RAX));
        append_operand(conversion, reg_operand(XR_RDX));
      } else append_operand(conversion, resolve(instruction.first));
      out.push_back(conversion);
      consume(instruction.first);
      if(pressure_home.kind == MirOperand::OP_FRAME)
        append_store(out, pressure_home, destination, instruction.type);
      define(instruction.dest, instruction.type,
             pressure_home.kind == MirOperand::OP_FRAME ? pressure_home : destination);
      return;
    }
    if(is_integer_or_pointer(instruction.source_type) &&
       is_integer_or_pointer(instruction.type)) {
      MirOperand pressure_home;
      X64Register result = XR_RSP;
      if(result_is_immediate_return(
           block, instruction_index, instruction.dest)) {
        result = XR_RAX;
      } else if(!try_allocate_result(instruction.dest, out, &result)) {
        pressure_home = allocate_temp_home(instruction.dest, instruction.type);
        result = XR_RAX;
      }
      const MirOperand destination = reg_operand(result);
      move_value_to_register(out, destination.reg, resolve(instruction.first),
                             instruction.source_type);
      if(instruction.op.kind == LowOperation::LOP_SEXT || instruction.op.kind == LowOperation::LOP_ZEXT) {
        append_integer_extension(out, destination,
          lowir_model::lowir_type_bit_width(instruction.source_type),
          instruction.op.kind == LowOperation::LOP_SEXT);
      } else {
        append_integer_normalization(out, instruction.source_type, destination);
      }
      append_integer_normalization(out, instruction.type, destination);
      consume(instruction.first, destination.reg);
      if(pressure_home.kind == MirOperand::OP_FRAME)
        append_store(out, pressure_home, destination, instruction.type);
      define(instruction.dest, instruction.type,
             pressure_home.kind == MirOperand::OP_FRAME ? pressure_home : destination);
      return;
    }
    throw std::runtime_error("conversion categories are not implemented");
  }
  MirOperand direct_compare_left(const Operand & operand,
                                 std::vector<MirInstruction> & out)
  {
    const MirOperand source = resolve(operand);
    if(source.kind == MirOperand::OP_REG || source.kind == MirOperand::OP_FRAME ||
       source.kind == MirOperand::OP_GLOBAL || source.kind == MirOperand::OP_DEREF)
      return source;
    append_move(out, reg_operand(XR_RAX), source);
    return reg_operand(XR_RAX);
  }
  void emit_direct_compare_branch(const Instruction & comparison,
                                  const Instruction & branch,
                                  std::vector<MirInstruction> & out,
                                  bool skip_branch = true)
  {
    MirOperand left = direct_compare_left(comparison.first, out);
    const MirOperand unresolved_right = resolve(comparison.second);
    if(unresolved_right.kind == MirOperand::OP_IMM &&
       lowir_model::lowir_type_bit_width(comparison.type) == 64 &&
       (unresolved_right.imm < INT32_MIN || unresolved_right.imm > INT32_MAX) &&
       (left.kind != MirOperand::OP_REG || left.reg != XR_RAX)) {
      move_value_to_register(out, XR_RAX, left, comparison.type);
      left = reg_operand(XR_RAX);
    }
    MirOperand right = direct_compare_right(
      comparison.second, comparison.type, left, out);
    const bool left_memory = left.kind == MirOperand::OP_FRAME ||
      left.kind == MirOperand::OP_GLOBAL || left.kind == MirOperand::OP_DEREF;
    const bool right_memory = right.kind == MirOperand::OP_FRAME ||
      right.kind == MirOperand::OP_GLOBAL || right.kind == MirOperand::OP_DEREF;
    if(left_memory && right_memory) {
      move_value_to_register(out, XR_RDX, right, comparison.type);
      right = reg_operand(XR_RDX);
    }
    MirInstruction compare = machine_instruction(MirInstruction::MI_CMP,
                                                 comparison.type);
    append_operand(compare, left);
    append_operand(compare, right);
    out.push_back(compare);
    if(integer_detail::memory_operand(right) && stats_)
      ++stats_->memory_rhs_operations_selected;
    MirInstruction jump_true = machine_instruction(MirInstruction::MI_JCC);
    jump_true.condition = predicate_condition(comparison.op);
    append_operand(jump_true, native_block_operand(source_, branch.second));
    out.push_back(jump_true);
    MirInstruction jump_false = machine_instruction(MirInstruction::MI_JMP);
    append_operand(jump_false, native_block_operand(source_, branch.third));
    out.push_back(jump_false);
    consume(comparison.first);
    consume(comparison.second);
    if(skip_branch) skipped_position_ = position_ + 1;
  }
  void emit_wide_direct_compare_branch(const Instruction & comparison,
                                       const Instruction & branch,
                                       std::vector<MirInstruction> & out,
                                       bool skip_branch = true)
  {
    wide::append_compare_branch(
      wide_value(comparison.first), wide_value(comparison.second), comparison.op,
      native_block_operand(source_, branch.second),
      native_block_operand(source_, branch.third), out);
    consume(comparison.first);
    consume(comparison.second);
    if(skip_branch) skipped_position_ = position_ + 1;
  }
  void emit_compare_value(const Instruction & instruction,
                          const lowir_model::LowirBlock & block,
                          std::size_t instruction_index,
                          std::vector<MirInstruction> & out)
  {
    const bool direct_return = result_is_immediate_return(
      block, instruction_index, instruction.dest);
    if(wide::is_integer(instruction.type)) {
      const LowType & result_type =
        lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
      wide::append_compare(wide_value(instruction.first),
                           wide_value(instruction.second), instruction.op, out);
      MirOperand destination;
      if(direct_return) {
        destination = reg_operand(XR_RAX);
        append_move(out, destination, reg_operand(XR_R10));
      } else {
        X64Register result = XR_RSP;
        if(try_allocate_result(instruction.dest, out, &result)) {
          destination = reg_operand(result);
          append_move(out, destination, reg_operand(XR_R10));
        } else {
          destination = allocate_temp_home(instruction.dest, result_type);
          append_store(out, destination, reg_operand(XR_R10), result_type);
        }
      }
      consume(instruction.first);
      consume(instruction.second);
      define(instruction.dest, result_type, destination);
      return;
    }
    if(!is_integer_or_pointer(instruction.type))
      throw std::runtime_error("integer selector received non-integer comparison");
    const LowType result_type =
      lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
    MirOperand right;
    MirOperand pressure_home;
    MirOperand destination;
    MirOperand comparison_left;
    if(direct_return) {
      destination = reg_operand(XR_RAX);
      select_direct_return_compare_operands(
        instruction, comparison_left, right, out);
    } else {
      const MirOperand left = resolve(instruction.first);
      right = resolve(instruction.second);
      if(integer_detail::memory_operand(right) &&
         operand_uses_register(right, XR_RAX)) {
        move_value_to_register(
          out, XR_RDX, right, operand_type(instruction.second));
        right = reg_operand(XR_RDX);
        append_integer_normalization(
          out, operand_type(instruction.second), right);
      }
      destination = binary_destination(
        instruction, left, out, false, &pressure_home, &result_type);
      comparison_left = destination;
      if(right.kind != MirOperand::OP_REG &&
         !integer_detail::memory_operand(right)) {
        const bool encodable_immediate = right.kind == MirOperand::OP_IMM &&
          (lowir_model::lowir_type_bit_width(instruction.type) < 64 ||
           (right.imm >= INT32_MIN && right.imm <= INT32_MAX));
        if(!encodable_immediate ||
           !operand_uses_register(destination, XR_RDX)) {
          const X64Register scratch =
            operand_uses_register(destination, XR_RDX) ? XR_RAX : XR_RDX;
          move_value_to_register(
            out, scratch, right, operand_type(instruction.second));
          right = reg_operand(scratch);
        }
      }
    }
    MirInstruction compare = machine_instruction(MirInstruction::MI_CMP,
                                                 instruction.type);
    append_operand(compare, comparison_left);
    append_operand(compare, right);
    out.push_back(compare);
    if(integer_detail::memory_operand(right) && stats_)
      ++stats_->memory_rhs_operations_selected;
    MirInstruction set = machine_instruction(MirInstruction::MI_SETCC);
    set.condition = predicate_condition(instruction.op);
    append_operand(set, destination);
    out.push_back(set);
    MirInstruction extend = machine_instruction(MirInstruction::MI_MOVZX);
    append_operand(extend, destination);
    append_operand(extend, destination);
    out.push_back(extend);
    consume(instruction.first, destination.reg);
    consume(instruction.second, destination.reg);
    if(pressure_home.kind == MirOperand::OP_FRAME)
      append_store(out, pressure_home, destination, result_type);
    define(instruction.dest, result_type,
           pressure_home.kind == MirOperand::OP_FRAME ? pressure_home : destination);
  }
  void emit_unary_value(const Instruction & instruction,
                        const lowir_model::LowirBlock & block,
                        std::size_t instruction_index,
                        std::vector<MirInstruction> & out)
  {
    if(wide::is_integer(instruction.type)) { const MirOperand destination = allocate_temp_home(instruction.dest, instruction.type); wide::append_unary(destination, wide_value(instruction.first), instruction.op, out); consume(instruction.first); define(instruction.dest, instruction.type, destination); return; }
    if(is_floating(instruction.type)) {
      emit_float_unary(instruction, out);
      return;
    }
    if(!is_integer_or_pointer(instruction.type))
      throw std::runtime_error("integer selector received non-integer unary operation");
    if(instruction.op.kind == LowOperation::LOP_DECAY) {
      emit_copy(instruction, out,
        result_is_immediate_return(block, instruction_index, instruction.dest));
      return;
    }
    const MirOperand source = resolve(instruction.first);
    const LowType result_type = instruction.op.kind == LowOperation::LOP_NOT ?
      lowir_model::builtin_lowir_type(lowir_model::LTK_I64) : instruction.type;
    MirOperand pressure_home;
    MirOperand destination;
    if(result_is_immediate_return(block, instruction_index,
                                  instruction.dest)) {
      destination = reg_operand(XR_RAX);
      move_value_to_register(
        out, destination.reg, source, operand_type(instruction.first));
      if(source.kind == MirOperand::OP_FRAME ||
         source.kind == MirOperand::OP_GLOBAL ||
         source.kind == MirOperand::OP_DEREF)
        append_integer_normalization(out, operand_type(instruction.first), destination);
    } else {
      destination = binary_destination(
        instruction, source, out, false, &pressure_home, &result_type);
    }
    if(instruction.op.kind == LowOperation::LOP_NOT) {
      MirInstruction compare = machine_instruction(MirInstruction::MI_CMP,
                                                   instruction.type);
      append_operand(compare, destination);
      append_operand(compare, immediate(0));
      out.push_back(compare);
      MirInstruction set = machine_instruction(MirInstruction::MI_SETCC);
      set.condition = XC_E;
      append_operand(set, destination);
      out.push_back(set);
      MirInstruction extend = machine_instruction(MirInstruction::MI_MOVZX);
      append_operand(extend, destination);
      append_operand(extend, destination);
      out.push_back(extend);
    } else {
      MirInstruction::Opcode opcode = MirInstruction::MI_NEG;
      if(instruction.op.kind == LowOperation::LOP_BITNOT) opcode = MirInstruction::MI_NOT;
      else if(instruction.op.kind == LowOperation::LOP_BSWAP) opcode = MirInstruction::MI_BSWAP;
      else if(instruction.op.kind != LowOperation::LOP_NEG)
        throw std::runtime_error(std::string("integer unary operation is not implemented: ") +
                                 lowir_model::lowir_operation_text(instruction.op));
      MirInstruction operation = machine_instruction(opcode, instruction.type);
      append_operand(operation, destination);
      out.push_back(operation);
      append_integer_normalization(out, instruction.type, destination);
    }
    consume(instruction.first, destination.reg);
    if(pressure_home.kind == MirOperand::OP_FRAME)
      append_store(out, pressure_home, destination, result_type);
    define(instruction.dest, result_type,
           pressure_home.kind == MirOperand::OP_FRAME ? pressure_home : destination);
  }
  void emit_direct_unary_not_branch(const Instruction & instruction,
                                    const Instruction & branch,
                                    std::vector<MirInstruction> & out)
  {
    const MirOperand value = direct_compare_left(instruction.first, out);
    MirInstruction compare = machine_instruction(MirInstruction::MI_CMP,
                                                 instruction.type);
    append_operand(compare, value);
    append_operand(compare, immediate(0));
    out.push_back(compare);
    MirInstruction jump_true = machine_instruction(MirInstruction::MI_JCC);
    jump_true.condition = XC_E;
    append_operand(jump_true, native_block_operand(source_, branch.second));
    out.push_back(jump_true);
    MirInstruction jump_false = machine_instruction(MirInstruction::MI_JMP);
    append_operand(jump_false, native_block_operand(source_, branch.third));
    out.push_back(jump_false);
    consume(instruction.first);
    skipped_position_ = position_ + 1;
  }
  void emit_gpr_move(const GprMove & move,
                     std::vector<MirInstruction> & out)
  {
    if(move.object_chunk) {
      MirOperand storage;
      if(direct_object_chunk_storage(
           move.object_source, move.chunk_offset, &storage))
        append_load(out, reg_operand(move.destination), storage, move.type);
      else {
        emit_operand_address(out, XR_R11, move.object_source);
        append_load(out, reg_operand(move.destination),
                    dereference(
                      XR_R11, static_cast<long long>(move.chunk_offset)),
                    move.type);
      }
    } else if(move.source_is_address) {
      if(move.source.kind == MirOperand::OP_FRAME ||
         move.source.kind == MirOperand::OP_DEREF)
        append_address(out, move.destination, move.source);
      else
        move_value_to_register(out, move.destination, move.source,
          lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
    } else {
      move_value_to_register(out, move.destination, move.source, move.type);
    }
  }
  void emit_parallel_gpr_moves(std::vector<GprMove> moves,
                               std::vector<MirInstruction> & out)
  {
    std::size_t remaining = moves.size();
    while(remaining) {
      bool progressed = false;
      for(std::size_t i = 0; i < moves.size(); ++i) {
        if(!moves[i].pending || !move_destination_is_safe(moves, i)) continue;
        emit_gpr_move(moves[i], out);
        moves[i].pending = false;
        --remaining;
        progressed = true;
      }
      if(progressed) continue;
      std::size_t cycle = 0;
      while(cycle < moves.size() && !moves[cycle].pending) ++cycle;
      if(cycle == moves.size() || moves[cycle].source.kind != MirOperand::OP_REG)
        throw std::logic_error("invalid parallel GPR move cycle");
      const X64Register saved = moves[cycle].source.reg;
      append_move(out, reg_operand(XR_R11), reg_operand(saved));
      for(std::size_t i = 0; i < moves.size(); ++i)
        if(moves[i].pending && moves[i].source.kind == MirOperand::OP_REG &&
           moves[i].source.reg == saved)
          moves[i].source = reg_operand(XR_R11);
    }
  }
  void emit_parallel_gpr_moves(const Instruction & instruction,
                               std::vector<MirInstruction> & out)
  {
    std::vector<GprMove> moves;
    std::size_t gpr_index = 0;
    for(std::size_t i = 0; i < instruction.args.size() && gpr_index < 6; ++i) {
      if(is_scalar_float(operand_type(instruction.args[i]))) continue;
      GprMove move;
      move.destination = abi::argument_register(gpr_index++);
      move.source = resolve_gpr_call_argument(instruction.args[i]);
      if(instruction.args[i].kind == Operand::OP_TEMP) {
        const ValueFact & value = values_[instruction.args[i].value];
        if(value.forwarded_parameter.valid())
          move.source = selected_value_location(value.forwarded_parameter);
      }
      move.type = operand_type(instruction.args[i]);
      move.source_is_address = is_frame_address(instruction.args[i]);
      if(stats_ && move.source.kind == MirOperand::OP_FRAME &&
         !move.source_is_address)
        record_call_argument_frame_load(instruction.args[i]);
      moves.push_back(move);
    }
    emit_parallel_gpr_moves(moves, out);
  }
  bool call_has_scalar_float(const Instruction & instruction) const
  {
    for(std::size_t i = 0; i < instruction.args.size(); ++i)
      if(is_scalar_float(operand_type(instruction.args[i]))) return true;
    return false;
  }
  long long xmm_call_scratch()
  {
    if(has_xmm_call_scratch_) return xmm_call_scratch_;
    xmm_call_scratch_ = allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP,
      lowir_model::FPN_XMM_CALL_SCRATCH,
      lowir_model::builtin_lowir_type(lowir_model::LTK_F64));
    has_xmm_call_scratch_ = true;
    return xmm_call_scratch_;
  }
  void emit_parallel_xmm_moves(const Instruction & instruction,
                               std::vector<MirInstruction> & out)
  {
    std::size_t xmm_index = 0;
    std::vector<location_planning::ParallelXmmMove> moves;
    for(std::size_t i = 0; i < instruction.args.size() && xmm_index < 8; ++i) {
      const LowType & type = operand_type(instruction.args[i]);
      if(!is_scalar_float(type)) continue;
      location_planning::ParallelXmmMove move;
      move.destination = static_cast<XmmRegister>(xmm_index++);
      move.source = resolve(instruction.args[i]);
      move.type = type;
      moves.push_back(move);
    }
    std::size_t remaining = moves.size();
    while(remaining) {
      bool progressed = false;
      for(std::size_t i = 0; i < moves.size(); ++i) {
        if(!moves[i].pending ||
           !location_planning::xmm_destination_is_safe(moves, i)) continue;
        append_float_move(out, xmm_operand(moves[i].destination), moves[i].source,
                          moves[i].type, true);
        moves[i].pending = false;
        --remaining;
        progressed = true;
      }
      if(progressed) continue;
      std::size_t cycle = 0;
      while(cycle < moves.size() &&
            (!moves[cycle].pending || moves[cycle].source.kind != MirOperand::OP_XMM))
        ++cycle;
      if(cycle == moves.size())
        throw std::logic_error("invalid parallel XMM move cycle");
      const XmmRegister saved = moves[cycle].source.xmm;
      const MirOperand scratch = frame_operand(xmm_call_scratch());
      append_float_move(out, scratch, xmm_operand(saved), moves[cycle].type);
      for(std::size_t i = 0; i < moves.size(); ++i)
        if(moves[i].pending && moves[i].source.kind == MirOperand::OP_XMM &&
           moves[i].source.xmm == saved)
          moves[i].source = scratch;
    }
  }
  std::size_t emit_mixed_stack_arguments(const Instruction & instruction,
                                         std::vector<MirInstruction> & out)
  {
    std::size_t gpr_index = 0;
    std::size_t xmm_index = 0;
    std::size_t stack_count = 0;
    for(std::size_t i = 0; i < instruction.args.size(); ++i) {
      const bool floating = is_scalar_float(operand_type(instruction.args[i]));
      if(floating ? xmm_index++ >= 8 : gpr_index++ >= 6) ++stack_count;
    }
    if(!stack_count) return 0;
    const std::size_t bytes = align_up(stack_count * 8, 16);
    MirInstruction subtract = machine_instruction(MirInstruction::MI_SUB);
    append_operand(subtract, reg_operand(XR_RSP));
    append_operand(subtract, immediate(static_cast<long long>(bytes)));
    out.push_back(subtract);
    gpr_index = xmm_index = stack_count = 0;
    for(std::size_t i = 0; i < instruction.args.size(); ++i) {
      const LowType & type = operand_type(instruction.args[i]);
      const bool floating = is_scalar_float(type);
      const bool on_stack = floating ? xmm_index++ >= 8 : gpr_index++ >= 6;
      if(!on_stack) continue;
      const MirOperand destination = dereference(
        XR_RSP, static_cast<long long>(stack_count++ * 8));
      if(floating)
        append_float_move(out, destination, resolve(instruction.args[i]), type);
      else {
        MirOperand value = resolve(instruction.args[i]);
        if(value.kind != MirOperand::OP_REG) {
          move_value_to_register(out, XR_R11, value, type);
          value = reg_operand(XR_R11);
        }
        append_store(out, destination, value, type);
      }
    }
    return bytes;
  }
  std::size_t emit_stack_arguments(const Instruction & instruction,
                                   std::vector<MirInstruction> & out)
  {
    if(instruction.args.size() <= 6) return 0;
    const std::size_t bytes = align_up((instruction.args.size() - 6) * 8, 16);
    MirInstruction subtract = machine_instruction(MirInstruction::MI_SUB);
    append_operand(subtract, reg_operand(XR_RSP));
    append_operand(subtract, immediate(static_cast<long long>(bytes)));
    out.push_back(subtract);
    for(std::size_t i = 6; i < instruction.args.size(); ++i) {
      if(is_frame_address(instruction.args[i])) continue;
      MirOperand value = resolve(instruction.args[i]);
      const LowType & type = operand_type(instruction.args[i]);
      if(value.kind != MirOperand::OP_REG) {
        move_value_to_register(out, XR_R11, value, type);
        value = reg_operand(XR_R11);
      }
      append_store(out, dereference(XR_RSP, static_cast<long long>((i - 6) * 8)),
                   value, type);
    }
    return bytes;
  }
  void emit_deferred_stack_addresses(const Instruction & instruction,
                                     std::vector<MirInstruction> & out)
  {
    for(std::size_t i = 6; i < instruction.args.size(); ++i) {
      if(!is_frame_address(instruction.args[i])) continue;
      append_address(out, XR_R11, resolve(instruction.args[i]));
      append_store(out, dereference(XR_RSP, static_cast<long long>((i - 6) * 8)),
                   reg_operand(XR_R11), operand_type(instruction.args[i]));
    }
  }
  std::vector<lowir_model::LowirParameter> call_parameters(
      const Instruction & instruction) const
  {
    std::vector<lowir_model::LowirParameter> parameters;
    if(instruction.has_call_signature) parameters = instruction.call_params;
    else if(instruction.first.kind == Operand::OP_GLOBAL) {
      const FunctionSignature & found = signatures_[instruction.first.symbol];
      if(found.params) parameters = *found.params;
    }
    if(parameters.size() > instruction.args.size())
      parameters.resize(instruction.args.size());
    while(parameters.size() < instruction.args.size()) {
      lowir_model::LowirParameter parameter;
      parameter.type = operand_type(instruction.args[parameters.size()]);
      parameters.push_back(parameter);
    }
    return parameters;
  }
  bool call_is_variadic(const Instruction & instruction) const
  {
    if(instruction.has_call_signature)
      return instruction.call_boundary.arity == lowir_model::CAM_VARIADIC;
    if(instruction.first.kind != Operand::OP_GLOBAL) return false;
    const FunctionSignature & found = signatures_[instruction.first.symbol];
    return found.boundary &&
      found.boundary->arity == lowir_model::CAM_VARIADIC;
  }
  bool argument_needs_address(const lowir_model::LowirParameter & parameter,
                              const Operand & argument) const
  {
    return parameter.metadata.passing != lowir_model::PPM_DIRECT &&
      operand_type(argument).kind != lowir_model::LTK_PTR;
  }
  bool requires_extended_call(const Instruction & instruction,
                              const std::vector<lowir_model::LowirParameter> & parameters) const
  {
    if(!instruction.call_returns_void &&
       (instruction.type.kind == lowir_model::LTK_OBJECT ||
        wide::is_integer(instruction.type) ||
        is_extended_float(instruction.type))) return true;
    for(std::size_t i = 0; i < parameters.size(); ++i)
      if(parameters[i].type.kind == lowir_model::LTK_OBJECT ||
         wide::is_integer(parameters[i].type) ||
         is_extended_float(parameters[i].type) ||
         parameters[i].metadata.passing != lowir_model::PPM_DIRECT)
        return true;
    for(std::size_t i = 0; i < instruction.args.size(); ++i)
      if(operand_type(instruction.args[i]).kind == lowir_model::LTK_OBJECT ||
         wide::is_integer(operand_type(instruction.args[i]))) return true;
    return false;
  }
  void emit_stack_adjust(std::vector<MirInstruction> & out,
                         MirInstruction::Opcode opcode,
                         std::size_t bytes)
  {
    if(!bytes) return;
    MirInstruction adjust = machine_instruction(opcode);
    append_operand(adjust, reg_operand(XR_RSP));
    append_operand(adjust, immediate(static_cast<long long>(bytes)));
    out.push_back(adjust);
  }
  void emit_extended_stack_arguments(
      const Instruction & instruction,
      const std::vector<lowir_model::LowirParameter> & parameters,
      const abi::Plan & plan,
      const std::vector<MirOperand> & addressable,
      const std::vector<bool> & needs_address,
      std::vector<MirInstruction> & out)
  {
    emit_stack_adjust(out, MirInstruction::MI_SUB, plan.stack_bytes);
    for(std::size_t i = 0; i < plan.pieces.size(); ++i) {
      const abi::Piece & piece = plan.pieces[i];
      if(piece.location != abi::PL_STACK) continue;
      const Operand & argument = instruction.args[piece.parameter_index];
      const MirOperand destination = dereference(
        XR_RSP, static_cast<long long>(piece.stack_offset));
      if(wide::is_integer(parameters[piece.parameter_index].type)) {
        wide::append_word_store(destination, wide_value(argument),
                                piece.chunk_offset / 8, XR_R11, XR_R10, out);
        continue;
      }
      if(parameters[piece.parameter_index].type.kind == lowir_model::LTK_OBJECT) {
        if(piece.chunk_offset) continue;
        append_address(out, XR_RDI,
                       dereference(XR_RSP, static_cast<long long>(piece.stack_offset)));
        emit_operand_address(out, XR_RSI, argument);
        MirInstruction copy = machine_instruction(MirInstruction::MI_COPY_BYTES);
        copy.byte_count = parameters[piece.parameter_index].type.storage_size;
        copy.byte_alignment = parameters[piece.parameter_index].type.alignment;
        append_operand(copy, reg_operand(XR_RDI));
        append_operand(copy, reg_operand(XR_RSI));
        out.push_back(copy);
        continue;
      }
      if(is_extended_float(piece.type)) {
        uses_scalar_float_ = true;
        append_float_move(out, destination, resolve(argument), piece.type);
        continue;
      }
      if(needs_address[piece.parameter_index] || is_frame_address(argument)) {
        GprMove move;
        move.destination = XR_R11;
        move.source = needs_address[piece.parameter_index] ?
          addressable[piece.parameter_index] : resolve(argument);
        move.type = lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
        move.source_is_address = true;
        emit_gpr_move(move, out);
        append_store(out, destination, reg_operand(XR_R11), machine_type(lowir_model::LTK_PTR));
      } else if(is_scalar_float(piece.type)) {
        uses_scalar_float_ = true;
        append_float_move(out, destination, resolve(argument), piece.type);
      } else {
        move_value_to_register(out, XR_R11, resolve(argument),
                               operand_type(argument));
        append_store(out, destination, reg_operand(XR_R11), piece.type);
      }
    }
  }
  void emit_extended_register_arguments(
      const Instruction & instruction,
      const std::vector<lowir_model::LowirParameter> & parameters,
      const abi::Plan & plan,
      const std::vector<MirOperand> & addressable,
      const std::vector<bool> & needs_address,
      bool rax_result_intact,
      std::vector<MirInstruction> & out)
  {
    std::vector<GprMove> gpr_moves;
    std::vector<location_planning::ParallelXmmMove> xmm_moves;
    for(std::size_t i = 0; i < plan.pieces.size(); ++i) {
      const abi::Piece & piece = plan.pieces[i];
      const Operand & argument = instruction.args[piece.parameter_index];
      if(piece.location == abi::PL_GPR) {
        GprMove move;
        move.destination = piece.reg;
        move.type = piece.type;
        if(wide::is_integer(parameters[piece.parameter_index].type)) {
          if(argument.kind == Operand::OP_INTEGER) {
            const wide::Value value = wide_value(argument);
            move.source = immediate(static_cast<long long>(piece.chunk_offset ?
              value.words.high : value.words.low));
          } else {
            move.object_chunk = true;
            move.object_source = argument;
            move.chunk_offset = piece.chunk_offset;
          }
        } else if(parameters[piece.parameter_index].type.kind == lowir_model::LTK_OBJECT) {
          move.object_chunk = true;
          move.object_source = argument;
          move.chunk_offset = piece.chunk_offset;
        } else {
          move.source = needs_address[piece.parameter_index] ?
            addressable[piece.parameter_index] :
            (rax_result_intact ? resolve_gpr_call_argument(argument) :
             resolve(argument));
          if(argument.kind == Operand::OP_TEMP) {
            const ValueFact & value = values_[argument.value];
            if(value.forwarded_parameter.valid())
              move.source =
                selected_value_location(value.forwarded_parameter);
          }
          move.source_is_address = needs_address[piece.parameter_index] ||
            is_frame_address(argument);
        }
        gpr_moves.push_back(move);
      } else if(piece.location == abi::PL_XMM) {
        location_planning::ParallelXmmMove move;
        move.destination = piece.xmm;
        move.source = resolve(argument);
        move.type = piece.type;
        xmm_moves.push_back(move);
      }
    }
    std::size_t remaining = gpr_moves.size();
    while(remaining) {
      bool progressed = false;
      for(std::size_t i = 0; i < gpr_moves.size(); ++i) {
        if(!gpr_moves[i].pending || !move_destination_is_safe(gpr_moves, i)) continue;
        emit_gpr_move(gpr_moves[i], out);
        gpr_moves[i].pending = false;
        --remaining;
        progressed = true;
      }
      if(progressed) continue;
      std::size_t cycle = 0;
      while(cycle < gpr_moves.size() && !gpr_moves[cycle].pending) ++cycle;
      if(cycle == gpr_moves.size() ||
         gpr_moves[cycle].source.kind != MirOperand::OP_REG)
        throw std::logic_error("invalid extended GPR move cycle");
      const X64Register saved = gpr_moves[cycle].source.reg;
      append_move(out, reg_operand(XR_R11), reg_operand(saved));
      for(std::size_t i = 0; i < gpr_moves.size(); ++i)
        if(gpr_moves[i].pending && gpr_moves[i].source.kind == MirOperand::OP_REG &&
           gpr_moves[i].source.reg == saved)
          gpr_moves[i].source = reg_operand(XR_R11);
    }
    remaining = xmm_moves.size();
    while(remaining) {
      bool progressed = false;
      for(std::size_t i = 0; i < xmm_moves.size(); ++i) {
        if(!xmm_moves[i].pending ||
           !location_planning::xmm_destination_is_safe(xmm_moves, i)) continue;
        append_float_move(out, xmm_operand(xmm_moves[i].destination),
                          xmm_moves[i].source,
                          xmm_moves[i].type, true);
        xmm_moves[i].pending = false;
        --remaining;
        progressed = true;
      }
      if(progressed) continue;
      std::size_t cycle = 0;
      while(cycle < xmm_moves.size() &&
            (!xmm_moves[cycle].pending ||
             xmm_moves[cycle].source.kind != MirOperand::OP_XMM)) ++cycle;
      if(cycle == xmm_moves.size())
        throw std::logic_error("invalid extended XMM move cycle");
      const XmmRegister saved = xmm_moves[cycle].source.xmm;
      const MirOperand scratch = frame_operand(xmm_call_scratch());
      append_float_move(out, scratch, xmm_operand(saved),
                        xmm_moves[cycle].type);
      for(std::size_t i = 0; i < xmm_moves.size(); ++i)
        if(xmm_moves[i].pending && xmm_moves[i].source.kind == MirOperand::OP_XMM &&
           xmm_moves[i].source.xmm == saved)
          xmm_moves[i].source = scratch;
    }
  }
  bool object_result_alias(const Instruction & instruction,
                           const lowir_model::LowirBlock & block,
                           std::size_t instruction_index,
                           Operand & destination,
                           std::size_t & skip_delta) const
  {
    if(instruction_index + 1 < block.instructions.size()) {
      const Instruction & copy = block.instructions[instruction_index + 1];
      if(copy.kind == Instruction::IK_COPYOBJ &&
         copy.first.kind == Operand::OP_TEMP &&
         copy.first.value == instruction.dest) {
        destination = copy.second;
        skip_delta = 1;
        return true;
      }
    }
    if(instruction_index + 2 < block.instructions.size()) {
      const Instruction & address = block.instructions[instruction_index + 1];
      const Instruction & copy = block.instructions[instruction_index + 2];
      if(address.kind == Instruction::IK_ADDR &&
         copy.kind == Instruction::IK_COPYOBJ &&
         copy.first.kind == Operand::OP_TEMP &&
         copy.first.value == instruction.dest &&
         copy.second.kind == Operand::OP_TEMP &&
         copy.second.value == address.dest) {
        destination = address.first;
        skip_delta = 2;
        return true;
      }
    }
    return false;
  }
  void store_object_return_registers(const LowType & type,
                                     const Operand * destination,
                                     const MirOperand * home,
                                     std::vector<MirInstruction> & out)
  {
    if(type.storage_size > 16)
      throw std::runtime_error("direct object return exceeds two SysV eightbytes");
    MirOperand direct_storage;
    const bool direct = destination ?
      direct_object_chunk_storage(*destination, 0, &direct_storage) : true;
    if(!destination) direct_storage = *home;
    if(!direct) emit_operand_address(out, XR_R11, *destination);
    const std::size_t chunks = (type.storage_size + 7) / 8;
    for(std::size_t chunk = 0; chunk < chunks; ++chunk) {
      const LowType & chunk_type = abi::object_chunk_type(type.storage_size - chunk * 8);
      MirOperand storage = direct ? direct_storage : dereference(XR_R11);
      storage.offset += static_cast<long long>(chunk * 8);
      append_store(out, storage,
                   reg_operand(chunk ? XR_RDX : XR_RAX), chunk_type);
    }
  }
  void define_object_result(lowir_model::ValueId value_id,
                            const LowType & type,
                            const Operand * destination,
                            const MirOperand & home)
  {
    ValueFact value;
    value.type = type;
    if(destination) {
      long long frame = 0;
      if(frame_provenance(*destination, frame)) {
        value.location = frame_operand(frame);
        value.frame_address = true;
        value.has_frame_provenance = true;
        value.frame_provenance = frame;
      } else if(destination->kind == Operand::OP_TEMP) {
        const ValueFact & address = values_[destination->value];
        value.location = address.location;
        value.frame_address = address.frame_address;
      } else {
        value.location = build::global_operand(
          MirOperand::OP_SYMBOL, *destination);
      }
    } else {
      value.location = home;
      value.frame_address = true;
      value.has_frame_provenance = true;
      value.frame_provenance = home.offset;
    }
    set_value(value_id, value);
  }
  void emit_extended_call(
      const Instruction & instruction,
      const lowir_model::LowirBlock & block,
      std::size_t instruction_index,
      const std::vector<lowir_model::LowirParameter> & parameters,
      std::vector<MirInstruction> & out)
  {
    const std::size_t setup_start = out.size();
    const bool direct = instruction.first.kind == Operand::OP_GLOBAL;
    const abi::Plan plan = abi::classify(parameters);
    MirOperand indirect_target = reg_operand(XR_R10);
    if(!direct) {
      const MirOperand selected = resolve(instruction.first);
      if(selected.kind == MirOperand::OP_REG &&
         indirect_target_can_keep_register(selected.reg, plan))
        indirect_target = selected;
      else
        move_value_to_register(out, XR_R10, selected,
                               operand_type(instruction.first));
    }
    const unsigned saved_setup_clobbers = active_setup_register_clobbers_;
    if(stabilize_extended_register_sources(
         instruction, parameters, plan, out))
      active_setup_register_clobbers_ |=
        register_mask(XR_RDI) | register_mask(XR_RSI);
    bool has_rax_first_use_argument = false;
    std::vector<bool> needs_address(parameters.size(), false);
    std::vector<MirOperand> addressable(parameters.size());
    for(std::size_t i = 0; i < parameters.size(); ++i) {
      needs_address[i] = argument_needs_address(parameters[i], instruction.args[i]);
      if(needs_address[i]) addressable[i] = make_addressable(instruction.args[i], out);
      if(is_floating(parameters[i].type)) uses_scalar_float_ = true;
      if(gpr_call_argument_can_read_rax(instruction.args[i]))
        has_rax_first_use_argument = true;
    }
    emit_extended_stack_arguments(instruction, parameters, plan,
                                  addressable, needs_address, out);
    bool rax_result_intact = has_rax_first_use_argument;
    for(std::size_t i = setup_start; rax_result_intact && i < out.size(); ++i)
      if(machine_opt::instruction_definition_mask(out[i]) &
         register_mask(XR_RAX))
        rax_result_intact = false;
    emit_extended_register_arguments(instruction, parameters, plan,
                                     addressable, needs_address,
                                     rax_result_intact, out);
    active_setup_register_clobbers_ = saved_setup_clobbers;
    const bool variadic = call_is_variadic(instruction);
    if(variadic)
      append_move(out, reg_operand(XR_RAX),
                  immediate(static_cast<long long>(abi::xmm_register_count(plan))));
    MirInstruction call = machine_instruction(direct ?
      MirInstruction::MI_CALL : MirInstruction::MI_CALL_INDIRECT);
    call.call_variadic = variadic;
    call.call_unwind_no =
      instruction.call_boundary.unwind == lowir_model::CUM_NO;
    call.call_returns_noreturn = instruction.call_boundary.returns == lowir_model::CRM_NORETURN;
    call.call_stack_bytes = plan.stack_bytes;
    abi::record_argument_registers(call, plan);
    append_operand(call, direct ?
      build::global_operand(MirOperand::OP_SYMBOL, instruction.first) :
      indirect_target);
    out.push_back(call);
    emit_stack_adjust(out, MirInstruction::MI_ADD, plan.stack_bytes);
    // Retire every input before homing the return value: dead arguments of
    // the active call otherwise make managed registers look unspillable.
    for(std::size_t i = 0; i < instruction.args.size(); ++i)
      consume(instruction.args[i]);
    consume(instruction.first);
    active_instruction_ = 0;
    const bool materialize_result = !instruction.call_returns_void &&
      (instruction.type.kind == lowir_model::LTK_OBJECT ||
       facts_.uses[instruction.dest] != 0);
    if(!materialize_result && !instruction.call_returns_void &&
       is_extended_float(instruction.type))
      out.push_back(machine_instruction(MirInstruction::MI_FPOP));
    if(materialize_result &&
       instruction.type.kind == lowir_model::LTK_OBJECT) {
      Operand alias_destination;
      std::size_t skip_delta = 0;
      const bool alias = object_result_alias(instruction, block, instruction_index,
                                             alias_destination, skip_delta);
      MirOperand home;
      if(alias) skipped_position_ = position_ + skip_delta;
      else home = allocate_temp_home(
        instruction.dest, instruction.type, THR_CALL_RESULT);
      store_object_return_registers(instruction.type,
                                    alias ? &alias_destination : 0,
                                    alias ? 0 : &home, out);
      define_object_result(instruction.dest, instruction.type,
                           alias ? &alias_destination : 0, home);
      if(alias) {
        Operand elided_use;
        elided_use.kind = Operand::OP_TEMP;
        elided_use.value = instruction.dest;
        consume(elided_use);
      }
    } else if(materialize_result && wide::is_integer(instruction.type)) { const MirOperand home = allocate_temp_home(instruction.dest, instruction.type, THR_CALL_RESULT); append_store(out, home, reg_operand(XR_RAX), machine_type(lowir_model::LTK_I64)); MirOperand high = home; high.offset += 8; append_store(out, high, reg_operand(XR_RDX), machine_type(lowir_model::LTK_I64)); define(instruction.dest, instruction.type, home); } else if(materialize_result && is_extended_float(instruction.type)) {
      const MirOperand location = allocate_float_result(instruction.dest, instruction.type);
      MirInstruction store = machine_instruction(MirInstruction::MI_FSTP,
                                                 instruction.type);
      append_operand(store, location);
      out.push_back(store);
      define(instruction.dest, instruction.type, location);
    } else if(materialize_result && is_scalar_float(instruction.type)) {
      const MirOperand location = allocate_float_result(instruction.dest, instruction.type);
      append_float_move(out, location, xmm_operand(XMM_0), instruction.type);
      define(instruction.dest, instruction.type, location);
    } else if(materialize_result) {
      MirOperand location = reg_operand(XR_RAX);
      MirOperand pressure_home;
      if(!scalar_result_can_remain_in_return_register(instruction.dest) &&
         !result_is_immediate_return(block, instruction_index, instruction.dest) &&
         !selection::result_is_immediately_stored(
           block, instruction_index, instruction.dest, facts_) &&
         !selection::result_is_next_direct_call_argument(
           block, instruction_index, instruction, facts_, signatures_) &&
         !result_is_immediate_unary_not_branch(block, instruction_index,
                                               instruction.dest)) {
        X64Register result = XR_RSP;
        if(try_allocate_result(instruction.dest, out, &result)) {
          location = reg_operand(result);
          append_move(out, location, reg_operand(XR_RAX));
        } else {
          pressure_home = allocate_temp_home(
            instruction.dest, instruction.type, THR_CALL_RESULT);
        }
      }
      if(selection::is_narrow_integer(instruction.type) &&
         selection::call_result_needs_normalization(
           block, instruction_index, instruction, facts_))
        append_integer_normalization(out, instruction.type, location);
      else if(selection::is_narrow_integer(instruction.type) && stats_)
        ++stats_->narrow_call_result_normalizations_omitted;
      if(pressure_home.kind == MirOperand::OP_FRAME)
        append_store(out, pressure_home, location, instruction.type);
      define(instruction.dest, instruction.type,
             pressure_home.kind == MirOperand::OP_FRAME ? pressure_home : location);
    }
  }
  void emit_call(const Instruction & instruction,
                 const lowir_model::LowirBlock & block,
                 std::size_t instruction_index,
                 std::vector<MirInstruction> & out)
  {
    const std::vector<lowir_model::LowirParameter> parameters =
      call_parameters(instruction);
    if(requires_extended_call(instruction, parameters)) {
      emit_extended_call(instruction, block, instruction_index, parameters, out);
      return;
    }
    const bool pressure_result = !instruction.call_returns_void &&
      instruction.args.size() >= 6 && !source_.metadata.keep_internal_alias &&
      facts_.uses[instruction.dest] && !is_scalar_float(instruction.type) &&
      !result_is_immediate_return(block, instruction_index, instruction.dest);
    const MirOperand pressure_home = pressure_result ?
      allocate_temp_home(
        instruction.dest, instruction.type, THR_CALL_RESULT) : MirOperand();
    for(std::size_t i = 0; pressure_result && i < instruction.args.size() && i < 6; ++i) {
      if(instruction.args[i].kind != Operand::OP_TEMP) continue;
      ValueFact & argument = values_[instruction.args[i].value];
      if(argument.location.kind != MirOperand::OP_REG ||
         (argument.location.reg != XR_R8 && argument.location.reg != XR_R9)) continue;
      frame_bytes_ = align_up(frame_bytes_, argument.type.alignment);
      frame_bytes_ += abi::frame_storage_size(argument.type);
      const MirOperand home = frame_operand(-static_cast<long long>(frame_bytes_));
      append_store(out, home, argument.location, argument.type);
      if(!has_live_location_alias(instruction.args[i].value, argument.location))
        registers_.release(argument.location.reg);
      set_value_location(instruction.args[i].value, home);
    }
    const bool direct = instruction.first.kind == Operand::OP_GLOBAL;
    MirOperand indirect_target = reg_operand(XR_R10);
    if(!direct) {
      MirOperand pointer_cell;
      if(instruction.first.kind == Operand::OP_TEMP) {
        if(value_known_[instruction.first.value])
          pointer_cell = values_[instruction.first.value].pointer_global_cell;
      }
      if(pointer_cell.kind == MirOperand::OP_GLOBAL &&
         pointer_cell.symbol.valid()) {
        MirInstruction load = machine_instruction(MirInstruction::MI_LOAD, machine_type(lowir_model::LTK_PTR));
        append_operand(load, reg_operand(XR_R10));
        append_operand(load, pointer_cell);
        out.push_back(load);
      } else {
        const MirOperand selected = resolve(instruction.first);
        if(selected.kind == MirOperand::OP_REG &&
           indirect_target_can_keep_register(selected.reg, parameters))
          indirect_target = selected;
        else
          move_value_to_register(out, XR_R10, selected,
                                 operand_type(instruction.first));
      }
    }
    const bool mixed_float = call_has_scalar_float(instruction);
    if(mixed_float) uses_scalar_float_ = true;
    std::vector<MirInstruction> stack_setup;
    const std::size_t stack_bytes = mixed_float ?
      emit_mixed_stack_arguments(instruction, out) :
      emit_stack_arguments(instruction, stack_setup);
	bool early_stack_arguments = false;
	for(std::size_t i = 1; i < stack_setup.size(); ++i)
	  for(std::size_t j = 1; j < stack_setup[i].operands.size(); ++j) {
		const MirOperand & operand = stack_setup[i].operands[j];
		if((operand.kind == MirOperand::OP_REG ||
			operand.kind == MirOperand::OP_DEREF) &&
		   (operand.reg == XR_RDI || operand.reg == XR_RSI ||
			operand.reg == XR_RDX || operand.reg == XR_RCX ||
			operand.reg == XR_R8 || operand.reg == XR_R9))
		  early_stack_arguments = true;
	  }
	if(!stack_setup.empty()) out.push_back(stack_setup.front());
	if(early_stack_arguments && stack_setup.size() > 1)
	  out.insert(out.end(), stack_setup.begin() + 1, stack_setup.end());
    emit_parallel_gpr_moves(instruction, out);
	if(!early_stack_arguments && stack_setup.size() > 1)
	  out.insert(out.end(), stack_setup.begin() + 1, stack_setup.end());
    if(mixed_float) emit_parallel_xmm_moves(instruction, out);
    else emit_deferred_stack_addresses(instruction, out);
    const bool variadic = call_is_variadic(instruction);
    if(variadic) {
      const abi::Plan plan = abi::classify(parameters);
      append_move(out, reg_operand(XR_RAX),
                  immediate(static_cast<long long>(abi::xmm_register_count(plan))));
    }
    if(direct) {
      MirInstruction call = machine_instruction(MirInstruction::MI_CALL);
      call.call_variadic = variadic;
      call.call_unwind_no =
        instruction.call_boundary.unwind == lowir_model::CUM_NO;
      call.call_returns_noreturn = instruction.call_boundary.returns == lowir_model::CRM_NORETURN;
      call.call_stack_bytes = stack_bytes;
      abi::record_argument_registers(call, parameters);
      append_operand(call,
        build::global_operand(MirOperand::OP_SYMBOL, instruction.first));
      out.push_back(call);
    } else {
      MirInstruction call = machine_instruction(MirInstruction::MI_CALL_INDIRECT);
      call.call_variadic = variadic;
      call.call_unwind_no =
        instruction.call_boundary.unwind == lowir_model::CUM_NO;
      call.call_returns_noreturn =
        instruction.call_boundary.returns == lowir_model::CRM_NORETURN;
      call.call_stack_bytes = stack_bytes;
      abi::record_argument_registers(call, parameters);
      append_operand(call, indirect_target);
      out.push_back(call);
    }
    if(stack_bytes) {
      MirInstruction add = machine_instruction(MirInstruction::MI_ADD);
      append_operand(add, reg_operand(XR_RSP));
      append_operand(add, immediate(static_cast<long long>(stack_bytes)));
      out.push_back(add);
    }
    const bool materialize_result = !instruction.call_returns_void &&
      (instruction.type.kind == lowir_model::LTK_OBJECT ||
       facts_.uses[instruction.dest] != 0);
    bool arguments_consumed = false;
    if(materialize_result) {
      if(is_scalar_float(instruction.type)) {
        const MirOperand location = allocate_float_result(instruction.dest, instruction.type);
        append_float_move(out, location, xmm_operand(XMM_0), instruction.type);
        define(instruction.dest, instruction.type, location);
      } else if(pressure_result) {
        append_store(out, pressure_home, reg_operand(XR_RAX), instruction.type);
        define(instruction.dest, instruction.type, pressure_home);
      } else if(selection::result_is_immediate_store_address_with_later_use(
                  block, instruction_index, instruction.dest, facts_)) {
        const MirOperand home = allocate_temp_home(
          instruction.dest, instruction.type, THR_CALL_RESULT);
        append_store(out, home, reg_operand(XR_RAX), instruction.type);
        define(instruction.dest, instruction.type, home);
      } else {
        if(result_is_next_call_address_argument(block, instruction_index, instruction)) {
          const MirOperand home = allocate_temp_home(
            instruction.dest, instruction.type, THR_CALL_RESULT);
          append_store(out, home, reg_operand(XR_RAX), instruction.type);
          define(instruction.dest, instruction.type, home);
          for(std::size_t i = 0; i < instruction.args.size(); ++i)
            consume(instruction.args[i]);
          consume(instruction.first);
          return;
        }
        MirOperand location = reg_operand(XR_RAX);
        MirOperand fallback_home;
        const bool forward_nonentry_branch =
          facts_.has(instruction.dest,
                     FunctionFacts::VF_DIRECT_BRANCH_CALL_RESULT) &&
          !source_.blocks.empty() && block.id != source_.blocks.front().id;
        if(!forward_nonentry_branch &&
           !scalar_result_can_remain_in_return_register(instruction.dest) &&
           !result_is_immediate_return(block, instruction_index, instruction.dest) &&
           !selection::result_is_immediately_stored(
             block, instruction_index, instruction.dest, facts_) &&
           !selection::result_is_next_direct_call_argument(
             block, instruction_index, instruction, facts_, signatures_) &&
           !result_is_immediate_unary_not_branch(block, instruction_index,
                                                 instruction.dest)) {
          const bool across = result_crosses_call(instruction.dest);
          X64Register result = XR_RSP;
          if(!registers_.try_allocate(across, result) &&
             !(spill_one(across, out) && registers_.try_allocate(across, result))) {
            for(std::size_t i = 0; i < instruction.args.size(); ++i)
              consume(instruction.args[i]);
            arguments_consumed = true;
            active_instruction_ = 0;
            if(!try_allocate_result(instruction.dest, out, &result))
              fallback_home = allocate_temp_home(
                instruction.dest, instruction.type, THR_CALL_RESULT);
          }
          if(fallback_home.kind != MirOperand::OP_FRAME) {
            location = reg_operand(result);
            append_move(out, location, reg_operand(XR_RAX));
          }
        }
        if(selection::is_narrow_integer(instruction.type) &&
           selection::call_result_needs_normalization(
             block, instruction_index, instruction, facts_))
          append_integer_normalization(out, instruction.type, location);
        else if(selection::is_narrow_integer(instruction.type) && stats_)
          ++stats_->narrow_call_result_normalizations_omitted;
        if(fallback_home.kind == MirOperand::OP_FRAME)
          append_store(out, fallback_home, location, instruction.type);
        define(instruction.dest, instruction.type,
               fallback_home.kind == MirOperand::OP_FRAME ? fallback_home : location);
      }
    }
    if(!arguments_consumed)
      for(std::size_t i = 0; i < instruction.args.size(); ++i)
        consume(instruction.args[i]);
    active_instruction_ = 0;
    consume(instruction.first);
  }
  void emit_branch(const Instruction & instruction, std::vector<MirInstruction> & out)
  {
    const LowType & condition_type = operand_type(instruction.first);
    if(wide::is_integer(condition_type)) {
      const wide::Value condition = wide_value(instruction.first);
      wide::append_word_to_register(condition, 0, XR_RAX, XR_R11, out);
      wide::append_word_to_register(condition, 1, XR_RDX, XR_R11, out);
      MirInstruction combine = machine_instruction(MirInstruction::MI_OR, machine_type(lowir_model::LTK_I64));
      append_operand(combine, reg_operand(XR_RAX));
      append_operand(combine, reg_operand(XR_RDX));
      out.push_back(combine);
    } else if(instruction.first.kind != Operand::OP_TEMP ||
              !facts_.has(instruction.first.value,
                          FunctionFacts::VF_DIRECT_BRANCH_CALL_RESULT))
      move_value_to_register(out, XR_RAX, resolve(instruction.first),
                             condition_type);
    MirInstruction compare = machine_instruction(MirInstruction::MI_CMP, machine_type(lowir_model::LTK_I64));
    append_operand(compare, reg_operand(XR_RAX));
    append_operand(compare, immediate(0));
    out.push_back(compare);
    MirInstruction branch = machine_instruction(MirInstruction::MI_JCC);
    branch.condition = XC_NE;
    append_operand(branch, native_block_operand(source_, instruction.second));
    out.push_back(branch);
    MirInstruction jump = machine_instruction(MirInstruction::MI_JMP);
    append_operand(jump, native_block_operand(source_, instruction.third));
    out.push_back(jump);
    consume(instruction.first);
  }
  void emit_switch(const Instruction & instruction, std::vector<MirInstruction> & out)
  {
    MirOperand source = resolve(instruction.first);
    if(instruction.first.kind == Operand::OP_TEMP &&
       facts_.first_use[instruction.first.value] == position_ &&
       incoming_parameter_register_known_[instruction.first.value]) {
      const X64Register incoming =
        incoming_parameter_registers_[instruction.first.value];
      if(incoming_parameter_register_is_intact(
           instruction.first.value, incoming))
        source = reg_operand(incoming);
    }
    move_value_to_register(out, XR_RAX, source,
                           operand_type(instruction.first));
    for(std::size_t i = 0; i < instruction.args.size(); i += 2) {
      move_value_to_register(out, XR_RCX, resolve(instruction.args[i]),
                             operand_type(instruction.args[i]));
      MirInstruction compare = machine_instruction(MirInstruction::MI_CMP, machine_type(lowir_model::LTK_I64));
      append_operand(compare, reg_operand(XR_RAX));
      append_operand(compare, reg_operand(XR_RCX));
      out.push_back(compare);
      MirInstruction branch = machine_instruction(MirInstruction::MI_JCC);
      branch.condition = XC_E;
      append_operand(branch,
        native_block_operand(source_, instruction.args[i + 1]));
      out.push_back(branch);
      consume(instruction.args[i]);
    }
    MirInstruction jump = machine_instruction(MirInstruction::MI_JMP);
    append_operand(jump, native_block_operand(source_, instruction.second));
    out.push_back(jump);
    consume(instruction.first);
  }
  bool nonparameter_value_live_in_register(X64Register reg) const
  {
    const std::vector<lowir_model::ValueId> & values =
      live_locations_.gpr_values(reg);
    for(std::size_t i = 0; i < values.size(); ++i) {
      const lowir_model::ValueId value = values[i];
      if(value_known_[value] && facts_.uses[value] != 0 &&
         !values_[value].parameter)
        return true;
    }
    return false;
  }
  void emit_exception_value(const Instruction & instruction,
                            std::vector<MirInstruction> & out)
  {
    X64Register result = XR_RSP;
    MirOperand pressure_home;
    MirOperand destination;
    if(try_allocate_result(instruction.dest, out, &result))
      destination = reg_operand(result);
    else {
      pressure_home = allocate_temp_home(instruction.dest, instruction.type);
      destination = reg_operand(XR_RAX);
    }
    MirInstruction load = machine_instruction(instruction.kind ==
      Instruction::IK_EXCEPTION ? MirInstruction::MI_LOAD_EXCEPTION :
      MirInstruction::MI_LOAD_EXCEPTION_SELECTOR, instruction.type);
    append_operand(load, destination);
    out.push_back(load);
    if(pressure_home.kind == MirOperand::OP_FRAME)
      append_store(out, pressure_home, destination, instruction.type);
    define(instruction.dest, instruction.type,
           pressure_home.kind == MirOperand::OP_FRAME ? pressure_home : destination);
  }
  void emit_throw(const Instruction & instruction,
                  std::vector<MirInstruction> & out)
  {
    MirOperand value = resolve(instruction.first);
    if(value.kind != MirOperand::OP_REG) {
      move_value_to_register(out, XR_RAX, value, operand_type(instruction.first));
      value = reg_operand(XR_RAX);
    }
    MirInstruction raise = machine_instruction(
      MirInstruction::MI_THROW, instruction.type);
    append_operand(raise, value);
    out.push_back(raise);
    consume(instruction.first);
  }
  void lower_instruction(const lowir_model::LowirBlock & block,
                         std::size_t instruction_index,
                         std::vector<MirInstruction> & out)
  {
    const Instruction & instruction = block.instructions[instruction_index];
    active_instruction_ = &instruction; if(position_ == skipped_position_) return;
    if(instruction.kind == Instruction::IK_PHI) return;
    if(instruction.kind == Instruction::IK_CONST) {
      if(wide::is_integer(instruction.type)) {
        const MirOperand destination = allocate_temp_home(instruction.dest, instruction.type);
        wide::append_copy(destination, wide::literal_value(instruction.first), out);
        define(instruction.dest, instruction.type, destination);
        return;
      }
      if(is_floating(instruction.type)) {
        emit_float_const(instruction, out);
        return;
      }
      if(is_integer_or_pointer(instruction.type)) {
        define(instruction.dest, instruction.type,
               immediate(canonical_integer_constant(
                 integer_value(instruction.first), instruction.type)));
        return;
      }
      MirOperand destination;
      X64Register reg = XR_RSP;
      if(result_is_immediate_return(block, instruction_index,
                                    instruction.dest)) {
        destination = reg_operand(XR_RAX);
        append_move(out, destination, immediate(integer_value(instruction.first)));
        append_integer_normalization(out, instruction.type, destination);
      } else if(registers_.try_allocate(
                  result_crosses_call(instruction.dest), reg)) {
        destination = reg_operand(reg);
        append_move(out, destination, immediate(integer_value(instruction.first)));
        append_integer_normalization(out, instruction.type, destination);
      } else {
        destination = allocate_temp_home(instruction.dest, instruction.type);
        append_move(out, reg_operand(XR_RAX),
                    immediate(integer_value(instruction.first)));
        append_store(out, destination, reg_operand(XR_RAX), instruction.type);
      }
      define(instruction.dest, instruction.type, destination);
    } else if(instruction.kind == Instruction::IK_COPY) {
      emit_copy(instruction, out,
        result_is_immediate_return(block, instruction_index, instruction.dest));
    }
    else if(instruction.kind == Instruction::IK_ADDR) emit_address_value(block, instruction_index, instruction, out);
    else if(instruction.kind == Instruction::IK_LOAD) {
      emit_load_instruction(instruction, block, instruction_index, out);
    } else if(instruction.kind == Instruction::IK_STORE) {
      emit_store_instruction(instruction, out);
    } else if(instruction.kind == Instruction::IK_ATOMIC_LOAD) {
      emit_atomic_load(instruction, block, instruction_index, out);
    } else if(instruction.kind == Instruction::IK_ATOMIC_STORE) {
      emit_atomic_store(instruction, out);
    } else if(instruction.kind == Instruction::IK_ATOMIC_EXCHANGE) {
      emit_atomic_exchange(instruction, block, instruction_index, out);
    } else if(instruction.kind == Instruction::IK_ATOMIC_ADD_FETCH) {
      emit_atomic_add_fetch(instruction, block, instruction_index, out);
    } else if(instruction.kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE) {
      emit_atomic_compare_exchange(instruction, block, instruction_index, out);
    } else if(instruction.kind == Instruction::IK_ATOMIC_THREAD_FENCE ||
              instruction.kind == Instruction::IK_ATOMIC_SIGNAL_FENCE) {
      emit_atomic_fence(instruction, out);
    } else if(TryEmitIntrinsic(instruction, out)) return;
    else if(instruction.kind == Instruction::IK_INDEX)
      emit_index(block, instruction_index, instruction, out);
    else if(instruction.kind == Instruction::IK_BINARY)
      emit_binary(instruction, block, instruction_index, out);
    else if(instruction.kind == Instruction::IK_CMP) {
      if(facts_.deferred_branch_comparisons[instruction.dest]) return;
      if(wide::is_integer(instruction.type) &&
         selection::result_is_immediate_branch(
           block, instruction_index, instruction.dest, facts_))
        emit_wide_direct_compare_branch(
          instruction, block.instructions[instruction_index + 1], out);
      else if(wide::is_integer(instruction.type))
        emit_compare_value(instruction, block, instruction_index, out);
      else if(is_floating(instruction.type) &&
         selection::result_is_immediate_branch(
           block, instruction_index, instruction.dest, facts_))
        emit_float_direct_compare_branch(instruction, block.instructions[instruction_index + 1], out);
      else if(is_floating(instruction.type))
        emit_float_compare_value(instruction, block, instruction_index, out);
      else if(selection::result_is_immediate_branch(
                block, instruction_index, instruction.dest, facts_))
        emit_direct_compare_branch(instruction, block.instructions[instruction_index + 1], out);
      else emit_compare_value(instruction, block, instruction_index, out);
    } else if(instruction.kind == Instruction::IK_UNARY) {
      if(instruction.op.kind == LowOperation::LOP_NOT &&
         selection::result_is_immediate_branch(
           block, instruction_index, instruction.dest, facts_))
        emit_direct_unary_not_branch(instruction, block.instructions[instruction_index + 1], out);
      else emit_unary_value(instruction, block, instruction_index, out);
    } else if(instruction.kind == Instruction::IK_CONVERT)
      emit_convert(instruction, block, instruction_index, out);
    else if(instruction.kind == Instruction::IK_SELECT)
      emit_select(instruction, out);
    else if(instruction.kind == Instruction::IK_CALL)
      emit_call(instruction, block, instruction_index, out);
    else if(instruction.kind == Instruction::IK_COPYOBJ ||
              instruction.kind == Instruction::IK_ZEROINIT) {
      emit_bulk(instruction, out);
    } else if(instruction.kind == Instruction::IK_BRANCH) {
      const Instruction * deferred =
        instruction.first.kind == Operand::OP_TEMP ?
        facts_.deferred_branch_comparisons[instruction.first.value] : 0;
      if(!deferred) emit_branch(instruction, out);
      else if(wide::is_integer(deferred->type))
        emit_wide_direct_compare_branch(*deferred, instruction, out, false);
      else if(is_floating(deferred->type))
        emit_float_direct_compare_branch(*deferred, instruction, out, false);
      else emit_direct_compare_branch(*deferred, instruction, out, false);
    } else if(instruction.kind == Instruction::IK_SWITCH) emit_switch(instruction, out);
    else if(instruction.kind == Instruction::IK_JUMP) {
      MirInstruction jump = machine_instruction(MirInstruction::MI_JMP);
      append_operand(jump, native_block_operand(source_, instruction.first));
      out.push_back(jump);
    } else if(instruction.kind == Instruction::IK_RETURN) emit_return(instruction, out);
    else if(eh::lower_marker(program_, source_, instruction, out)) return;
    else if(instruction.kind == Instruction::IK_EXCEPTION ||
            instruction.kind == Instruction::IK_EXCEPTION_SELECTOR)
      emit_exception_value(instruction, out);
    else if(instruction.kind == Instruction::IK_THROW) emit_throw(instruction, out);
    else {
      throw std::runtime_error("foundation LowIR instruction is not implemented");
    }
  }
};
}  // namespace

mir_model::MirFunction session_detail::lower_native_function(
    const lowir_model::LowirProgram & program,
    const lowir_model::LowirFunction & function,
    const std::vector<unsigned char> & pointer_globals,
    const std::vector<lowir_model::SymbolId> & tls_wrappers,
    const abi::FunctionSignatureIndex & signatures,
    int optimization_level,
    lowir_native::Stats * stats,
    allocation::AllocationDecisionLog * decisions)
{
  return FunctionLowerer(program, function, pointer_globals, tls_wrappers,
                         signatures, optimization_level, stats, decisions).lower();
}

}  // namespace lowir_native
