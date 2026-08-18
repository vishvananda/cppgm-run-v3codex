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
#include "lowir_native_eh.h"
#include "lowir_native_host_eh.h"
#include "lowir_native_index_lowering.h"
#include "lowir_native_intrinsic_lowering.h"
#include "lowir_native_mir.h"
#include "lowir_native_memory_lowering.h"
#include "lowir_native_program.h"
#include "lowir_native_registers.h"
#include "lowir_native_return_lowering.h"
#include "lowir_native_selection.h"
#include "lowir_native_session.h"
#include "lowir_native_spill_slots.h"
#include "lowir_native_stack.h"
#include "lowir_native_value.h"
#include "lowir_native_varargs.h"
#include "lowir_native_wide.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
namespace lowir_native {
namespace {
using lowir_model::Instruction; using lowir_model::LowType; using lowir_model::Operand;
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
                        private index_detail::IndexLowering<FunctionLowerer>,
                        private memory_detail::MemoryLowering<FunctionLowerer>,
                        private return_detail::ReturnLowering<FunctionLowerer>
{
  friend class IntrinsicLowering<FunctionLowerer>;
  friend class AddressLowering<FunctionLowerer>;
  friend class AtomicLowering<FunctionLowerer>;
  friend class bulk_detail::BulkLowering<FunctionLowerer>;
  friend class call_detail::CallLowering<FunctionLowerer>;
  friend class comparison_detail::CompareLowering<FunctionLowerer>;
  friend class copy_detail::CopyLowering<FunctionLowerer>;
  friend class index_detail::IndexLowering<FunctionLowerer>;
  friend class memory_detail::MemoryLowering<FunctionLowerer>;
  friend class return_detail::ReturnLowering<FunctionLowerer>;
public:
  FunctionLowerer(const lowir_model::LowirFunction & source,
                  const std::unordered_set<std::string> & pointer_globals,
                  const std::unordered_map<std::string, std::string> & tls_wrappers,
                  const FunctionSignatureIndex & signatures,
                  lowir_native::Stats * stats)
    : source_(source), pointer_globals_(pointer_globals), tls_wrappers_(tls_wrappers),
      signatures_(signatures), stats_(stats),
      facts_(analyze_function(source)), control_flow_(source), position_(0)
  {
    target_.name = source.name;
    target_.object_symbol = source.metadata.object_symbol;
    target_.return_type = source.return_type;
    target_.debug_location.file = source.debug_location.file;
    target_.debug_location.line = source.debug_location.line;
    target_.debug_location.column = source.debug_location.column;
    if(stats_)
      stats_->shared_storage_lifetime_extensions +=
        facts_.shared_storage_last_use.size();
    if(facts_.has_i128_atomic) registers_.reserve(XR_RBX);
    storage_facts_ = analyze_storage(source_, facts_, tls_wrappers_);
    slot_offsets_.resize(source_.slot_names.size(), 0);
    slot_offset_known_.assign(source_.slot_names.size(), 0);
    discarded_slots_.assign(source_.slot_names.size(), 0);
    plan_variadic_register_save();
    bind_parameters();
    plan_slots();
    plan_host_eh();
  }
  mir_model::MirFunction lower()
  {
    target_.blocks.reserve(source_.blocks.size());
    for(std::size_t i = 0; i < source_.blocks.size(); ++i) {
      mir_model::MirBlock block;
      block.label = lowir_model::lowir_block_label(source_, source_.blocks[i].id);
      block.instructions.reserve(source_.blocks[i].instructions.size() +
        (i == 0 ? parameter_moves_.size() : 0));
      control_flow_.SelectBlock(i);
      if(i == 0) block.instructions.insert(block.instructions.end(),
                                           parameter_moves_.begin(), parameter_moves_.end());
      for(std::size_t j = 0; j < source_.blocks[i].instructions.size(); ++j, ++position_) {
        const std::size_t first_machine_instruction = block.instructions.size();
        lower_instruction(source_.blocks[i], j, block.instructions);
        stabilize_edge_live_result(source_.blocks[i].instructions[j],
                                   block.instructions);
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
    target_.callee_saved_regs = registers_.preserves();
    target_.has_dynamic_stack = facts_.has_dynamic_stack;
    const bool needs_call_scratch = uses_scalar_float_ ||
      (source_.metadata.keep_internal_alias && !facts_.calls.empty());
    target_.scratch_bytes = needs_call_scratch ? 48 : 0;
    const std::size_t direct_parameter_bytes =
      !storage_facts_.has_promoted_parameter_slots &&
      !facts_.has_direct_branch_parameter ?
      abi::direct_parameter_bytes(source_.params) : 0;
    if(needs_call_scratch) {
      const std::size_t float_frame_bytes = source_.params.empty() ||
        !facts_.zero_index_parameters.empty() ? frame_bytes_ :
        std::max<std::size_t>(frame_bytes_, 16);
      target_.stack_frame_bytes = float_frame_bytes + target_.scratch_bytes;
    } else {
      target_.stack_frame_bytes = frame_bytes_;
      target_.stack_floor_bytes = direct_parameter_bytes;
    }
    if(constrained_wide_pressure()) {
      target_.stack_frame_bytes += 16;
      target_.stack_floor_bytes += 16;
    }
    target_.stack_size = align_up(std::max(target_.stack_floor_bytes,
      target_.stack_frame_bytes + target_.callee_saved_regs.size() * 8), 16);
    host_eh_detail::collect_host_eh_clauses(&target_);
    return target_;
  }
private:
  const lowir_model::LowirFunction & source_;
  const std::unordered_set<std::string> & pointer_globals_;
  const std::unordered_map<std::string, std::string> & tls_wrappers_;
  const FunctionSignatureIndex & signatures_;
  lowir_native::Stats * stats_;
  FunctionFacts facts_;
  StorageFacts storage_facts_;
  analysis::ControlFlowQueries control_flow_;
  mir_model::MirFunction target_;
  RegisterPool registers_;
  XmmPool xmms_;
  spill_slots::Pool spill_slots_;
  std::unordered_map<std::string, ValueFact> values_;
  std::vector<const std::string *> live_gpr_values_[16];
  std::vector<const std::string *> live_xmm_values_[8];
  std::vector<long long> slot_offsets_;
  std::vector<unsigned char> slot_offset_known_;
  std::unordered_map<std::string, X64Register> incoming_parameter_registers_;
  std::vector<unsigned char> discarded_slots_;
  std::vector<MirInstruction> parameter_moves_;
  const Instruction * active_instruction_ = 0;
  std::size_t position_, frame_bytes_ = 0;
  std::size_t skipped_position_ = static_cast<std::size_t>(-1);
  bool uses_scalar_float_ = false;
  bool has_xmm_call_scratch_ = false;
  long long xmm_call_scratch_ = 0;
  long long variadic_register_save_offset_ = 0;
  abi::VariadicState variadic_state_;
  long long allocate_frame_binding(mir_model::MirFrameBinding::Kind kind,
                                   const std::string & name,
                                   const LowType & type)
  {
    frame_bytes_ = align_up(frame_bytes_, type.alignment);
    frame_bytes_ += abi::frame_storage_size(type);
    mir_model::MirFrameBinding binding;
    binding.kind = kind;
    binding.name = name;
    binding.offset = -static_cast<long long>(frame_bytes_);
    binding.type = type;
    target_.frame_bindings.push_back(binding);
    return binding.offset;
  }
  std::uint32_t append_frame_binding(
                            mir_model::MirFrameBinding::Kind kind,
                            const std::string & name, const LowType & type,
                            long long offset)
  {
    if(target_.frame_bindings.size() >=
       std::numeric_limits<std::uint32_t>::max())
      throw std::runtime_error("too many native frame bindings");
    mir_model::MirFrameBinding binding;
    binding.kind = kind;
    binding.name = name;
    binding.offset = offset;
    binding.type = type;
    target_.frame_bindings.push_back(binding);
    return static_cast<std::uint32_t>(target_.frame_bindings.size());
  }
  MirOperand allocate_temp_frame_binding(const std::string & name,
                                         const LowType & type)
  {
    const std::size_t size = abi::frame_storage_size(type);
    const std::unordered_map<std::string, std::size_t>::const_iterator last =
      facts_.last_use.find(name);
    std::size_t available_after = last == facts_.last_use.end() ?
      position_ : last->second;
    const std::unordered_map<std::string, std::size_t>::const_iterator shared =
      facts_.shared_storage_last_use.find(name);
    if(shared != facts_.shared_storage_last_use.end())
      available_after = std::max(available_after, shared->second);
    const bool reusable = type.kind != lowir_model::LTK_OBJECT;
    long long offset = 0;
    if(reusable && spill_slots_.acquire(size, type.alignment, position_,
                                        available_after, &offset)) {
      if(stats_) ++stats_->temporary_frame_homes_reused;
      const std::uint32_t binding = append_frame_binding(
        mir_model::MirFrameBinding::FB_TEMP, name, type, offset);
      return frame_operand(offset, binding);
    }
    if(stats_) ++stats_->temporary_frame_homes_created;
    offset = allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP, name, type);
    if(reusable)
      spill_slots_.remember(size, type.alignment, available_after, offset);
    if(target_.frame_bindings.size() >
       std::numeric_limits<std::uint32_t>::max())
      throw std::runtime_error("too many native frame bindings");
    return frame_operand(offset,
      static_cast<std::uint32_t>(target_.frame_bindings.size()));
  }
  void plan_variadic_register_save()
  {
    if(!facts_.has_va_start) return;
    if(source_.boundary.arity != lowir_model::CAM_VARIADIC)
      throw std::runtime_error("va_start in non-variadic function");
    variadic_state_ = abi::variadic_state(source_.params);
    variadic_register_save_offset_ = allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP, "%va-register-save",
      varargs::register_save_type());
    varargs::append_register_save(variadic_register_save_offset_, parameter_moves_);
    uses_scalar_float_ = true;
  }
  void plan_slots()
  {
    for(std::size_t i = 0; i < source_.slots.size(); ++i) {
      const lowir_model::SlotId slot = source_.slots[i];
      const std::string & name = lowir_model::lowir_slot_name(source_, slot);
      const LowType & type = lowir_model::lowir_slot_type(source_, slot);
      if(!storage_facts_.parameter_slot_aliases[slot].empty() ||
         !storage_facts_.promoted_parameter_slots[slot].empty())
        continue;
      if(!storage_facts_.forwarded_parameter_slots[slot].empty()) {
        discarded_slots_[slot] = 1;
        continue;
      }
      if(storage_facts_.dead_store_slots[slot]) {
        discarded_slots_[slot] = 1;
        continue;
      }
      slot_offsets_[slot] = allocate_frame_binding(
        mir_model::MirFrameBinding::FB_SLOT, name, type);
      slot_offset_known_[slot] = 1;
    }
  }
  void plan_host_eh()
  {
    if(!host_eh_detail::requires_host_eh_storage(source_)) return;
    target_.host_eh_enabled = true;
    target_.host_eh_exception_offset = allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP, "%host-eh-exception",
      lowir_model::builtin_lowir_type(lowir_model::LTK_PTR));
    target_.host_eh_selector_offset = allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP, "%host-eh-selector",
      lowir_model::builtin_lowir_type(lowir_model::LTK_I64));
  }
  bool crosses_call(const std::string & name) const
  {
    return facts_.live_across_call.count(name) != 0;
  }
  bool constrained_wide_pressure() const { return source_.params.size() > 6 && source_.slots.empty() && !facts_.calls.empty(); }
  bool crosses_register_clobber(const std::string & name, X64Register reg) const
  {
    return analysis::crosses_register_clobber(facts_, name, reg);
  }
  bool incoming_parameter_register_is_intact(
      const std::string & name, X64Register reg) const
  {
    if(analysis::register_was_clobbered_before(facts_, reg, position_))
      return false;
    if(crosses_register_clobber(name, reg)) return false;
    for(std::unordered_map<std::string, ValueFact>::const_iterator value =
          values_.begin(); value != values_.end(); ++value) {
      if(value->first == name ||
         value->second.location.kind != MirOperand::OP_REG ||
         value->second.location.reg != reg) continue;
      const std::unordered_map<std::string, std::size_t>::const_iterator
        definition = facts_.definition.find(value->first);
      if(definition != facts_.definition.end() &&
         definition->second < position_) return false;
    }
    return true;
  }
  void reserve_direct_parameter_register(
      const mir_model::MirParamBinding & binding,
      const ValueFact & value, std::size_t uses)
  {
    if(!uses || binding.location != mir_model::MirParamBinding::PL_REG ||
       value.location.kind != MirOperand::OP_REG ||
       value.location.reg != binding.reg || !managed_register(binding.reg) ||
       registers_.is_used(binding.reg)) return;
    registers_.reserve(binding.reg);
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
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      const lowir_model::LowirParameter & parameter = source_.params[i];
      mir_model::MirParamBinding binding;
      binding.name = parameter.name;
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
        const std::size_t uses = facts_.uses[parameter.name];
        const bool clobbered =
          crosses_register_clobber(parameter.name, binding.reg);
        if(clobbered || (facts_.has_va_start &&
           ((parameter_gpr_index != 0 && uses) ||
            (parameter.type.kind == lowir_model::LTK_PTR && uses > 1)))) {
          const X64Register destination = registers_.allocate(true);
          value.location = reg_operand(destination);
          append_move(gpr_parameter_moves, value.location, reg_operand(binding.reg));
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
        binding, value, facts_.uses[parameter.name]);
      target_.params.push_back(binding);
      set_value(parameter.name, value);
    }
    parameter_moves_.insert(parameter_moves_.end(),
                            gpr_parameter_moves.begin(), gpr_parameter_moves.end());
  }
  void bind_aggregate_parameters()
  {
    const abi::Plan plan = abi::classify(source_.params);
    std::vector<long long> homes(source_.params.size(), 0);
    std::unordered_map<std::string, std::size_t> parameter_indices;
    parameter_indices.reserve(source_.params.size());
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      const lowir_model::LowirParameter & parameter = source_.params[i];
      parameter_indices[parameter.name] = i;
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
      set_value(parameter.name, value);
    }
    for(std::size_t slot = 0;
        slot < storage_facts_.parameter_slot_aliases.size(); ++slot) {
      const std::string & alias = storage_facts_.parameter_slot_aliases[slot];
      if(alias.empty()) continue;
      const std::unordered_map<std::string, std::size_t>::const_iterator parameter =
        parameter_indices.find(alias);
      if(parameter == parameter_indices.end())
        throw std::logic_error("parameter slot alias has no parameter");
      slot_offsets_[slot] = homes[parameter->second];
      slot_offset_known_[slot] = 1;
    }
    for(std::size_t i = 0; i < plan.pieces.size(); ++i) {
      const abi::Piece & piece = plan.pieces[i];
      const lowir_model::LowirParameter & parameter =
        source_.params[piece.parameter_index];
      mir_model::MirParamBinding binding;
      binding.name = parameter.name;
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
        if(facts_.uses[source_.params[i].name] == 0) continue;
        registers_.reserve(abi::argument_register(i));
        incoming_pool_reserved[i] = true;
      }
    }
    std::unordered_map<std::string, X64Register> cross_call_homes;
    std::vector<MirInstruction> register_parameter_moves;
    if(!wide_gpr_boundary)
      for(std::size_t i = std::min<std::size_t>(source_.params.size(), 6); i != 0; --i) {
        const std::string & name = source_.params[i - 1].name;
        if(facts_.uses[name] && (crosses_call(name) ||
           storage_facts_.promoted_parameters_across_call.count(name)))
          cross_call_homes[name] = registers_.allocate(true);
      }
    bool home_unused_register_parameters = wide_gpr_boundary;
    for(std::size_t i = 0; i < source_.params.size() && i < 6; ++i)
      if(facts_.uses[source_.params[i].name] != 0)
        home_unused_register_parameters = false;
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      const lowir_model::LowirParameter & parameter = source_.params[i];
      mir_model::MirParamBinding binding;
      binding.name = parameter.name;
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
        set_value(parameter.name, value);
        continue;
      }
      binding.location = mir_model::MirParamBinding::PL_REG;
      binding.reg = abi::argument_register(i);
      incoming_parameter_registers_[parameter.name] = binding.reg;
      target_.params.push_back(binding);
      value.location = reg_operand(binding.reg);
      const std::size_t uses = facts_.uses[parameter.name];
      const std::unordered_map<std::string, X64Register>::const_iterator planned =
        cross_call_homes.find(parameter.name);
      const bool incoming_clobbered = !wide_gpr_boundary && uses &&
        crosses_register_clobber(parameter.name, binding.reg);
      const bool promoted_incoming_clobbered =
        i < storage_facts_.promoted_parameter_clobbers.size() &&
        (storage_facts_.promoted_parameter_clobbers[i] &
         analysis::register_mask(binding.reg));
      if(uses == 0) {
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
        append_store(parameter_moves_, frame_operand(home), reg_operand(binding.reg),
                     parameter.type);
        value.location = frame_operand(home);
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
      } else if(planned != cross_call_homes.end()) {
        value.location = reg_operand(planned->second);
        value.fixed_register_home = storage_facts_.promoted_parameters.count(parameter.name);
        append_move(register_parameter_moves, value.location, reg_operand(binding.reg));
      } else if(!wide_gpr_boundary && promoted_incoming_clobbered) {
        const X64Register destination = registers_.is_used(XR_R9) ?
          registers_.allocate(false) : XR_R9;
        if(destination == XR_R9) registers_.reserve(XR_R9);
        value.location = reg_operand(destination);
        value.fixed_register_home = true;
        append_move(register_parameter_moves, value.location, reg_operand(binding.reg));
      } else if(!wide_gpr_boundary && !incoming_clobbered) {
        // Keep an intact incoming ABI register as the value's selected home.
        value.fixed_register_home =
          storage_facts_.promoted_parameters.count(parameter.name);
      } else if(wide_gpr_boundary && crosses_call(parameter.name)) {
        const long long home = allocate_frame_binding(
          mir_model::MirFrameBinding::FB_PARAM_SLOT, parameter.name, parameter.type);
        append_store(parameter_moves_, frame_operand(home), reg_operand(binding.reg),
                     parameter.type);
        value.location = frame_operand(home);
      } else if(!wide_gpr_boundary &&
                parameter.metadata.passing != lowir_model::PPM_DIRECT && uses) {
        const X64Register destination = registers_.allocate(crosses_call(parameter.name));
        value.location = reg_operand(destination);
        append_move(register_parameter_moves, value.location, reg_operand(binding.reg));
      } else if(!wide_gpr_boundary && incoming_clobbered) {
        const X64Register destination = registers_.allocate(crosses_call(parameter.name));
        value.location = reg_operand(destination);
        value.fixed_register_home =
          storage_facts_.promoted_parameters.count(parameter.name);
        append_move(register_parameter_moves, value.location, reg_operand(binding.reg));
      }
      if(incoming_pool_reserved[i] &&
         (value.location.kind != MirOperand::OP_REG || value.location.reg != binding.reg))
        registers_.release(binding.reg);
      if(!wide_gpr_boundary)
        reserve_direct_parameter_register(binding, value, uses);
      set_value(parameter.name, value);
    }
    parameter_moves_.insert(parameter_moves_.end(), register_parameter_moves.begin(),
                            register_parameter_moves.end());
    if(home_unused_register_parameters) {
      frame_bytes_ += 8; // Keep the six-register home area call-aligned.
      return;
    }
    if(!wide_gpr_boundary || constrained_wide_pressure()) return;
    static const std::size_t order[] = {2, 3, 4, 5, 1, 0};
    static const X64Register destinations[] = {
      XR_R15, XR_R9, XR_RBX, XR_R12, XR_R13, XR_R14
    };
    for(std::size_t step = 0; step < sizeof(order) / sizeof(order[0]); ++step) {
      const std::size_t index = order[step];
      if(index >= source_.params.size()) continue;
      const std::string & name = source_.params[index].name;
      if(facts_.uses[name] == 0 || values_[name].location.kind == MirOperand::OP_FRAME)
        continue;
      const X64Register destination = destinations[index];
      registers_.reserve(destination);
      append_move(parameter_moves_, reg_operand(destination),
                  reg_operand(abi::argument_register(index)));
      set_value_location(name, reg_operand(destination));
      values_[name].parameter = false;
      values_[name].fixed_register_home =
        storage_facts_.promoted_parameters.count(name);
    }
  }
  bool result_is_immediate_return(const lowir_model::LowirBlock & block,
    std::size_t instruction_index, const std::string & destination) const
  { return selection::result_is_immediate_return(block, instruction_index,
                                                  destination, facts_); }
  bool result_is_immediate_unary_not_branch(const lowir_model::LowirBlock & block,
    std::size_t instruction_index, const std::string & destination) const
  { return selection::result_is_immediate_unary_not_branch(
      block, instruction_index, destination, facts_); }
  bool result_is_next_call_address_argument(const lowir_model::LowirBlock & block,
                                            std::size_t instruction_index,
                                            const Instruction & producer) const
  {
    if(instruction_index + 1 >= block.instructions.size() ||
       producer.type.kind == lowir_model::LTK_PTR) return false;
    const Instruction & call = block.instructions[instruction_index + 1];
    if(call.kind != Instruction::IK_CALL) return false;
    std::vector<lowir_model::LowirParameter> parameters;
    if(call.has_call_signature) parameters = call.call_params;
    else if(call.first.kind == Operand::OP_GLOBAL) {
      const FunctionSignatureIndex::const_iterator found = signatures_.find(call.first.text);
      if(found != signatures_.end() && found->second.params)
        parameters = *found->second.params;
    }
    for(std::size_t i = 0; i < call.args.size() && i < parameters.size(); ++i)
      if(call.args[i].kind == Operand::OP_TEMP &&
         call.args[i].text == producer.dest &&
         parameters[i].metadata.passing != lowir_model::PPM_DIRECT)
        return true;
    return false;
  }
  bool result_is_next_direct_call_argument(
      const lowir_model::LowirBlock & block, std::size_t instruction_index,
      const Instruction & producer) const
  {
    const std::unordered_map<std::string, std::size_t>::const_iterator uses =
      facts_.uses.find(producer.dest);
    if(uses == facts_.uses.end() || uses->second != 1 ||
       instruction_index + 1 >= block.instructions.size()) return false;
    const Instruction & call = block.instructions[instruction_index + 1];
    if(call.kind != Instruction::IK_CALL) return false;
    const std::vector<lowir_model::LowirParameter> * parameters = 0;
    if(call.has_call_signature) parameters = &call.call_params;
    else if(call.first.kind == Operand::OP_GLOBAL) {
      const FunctionSignatureIndex::const_iterator found =
        signatures_.find(call.first.text);
      if(found != signatures_.end()) parameters = found->second.params;
    }
    for(std::size_t i = 0; i < call.args.size(); ++i) {
      if(call.args[i].kind != Operand::OP_TEMP ||
         call.args[i].text != producer.dest) continue;
      return !parameters || i >= parameters->size() ||
        (*parameters)[i].metadata.passing == lowir_model::PPM_DIRECT ||
        producer.type.kind == lowir_model::LTK_PTR;
    }
    return false;
  }
  MirOperand global_operand(MirOperand::Kind kind,
                            const Operand & operand) const
  {
    MirOperand result = named_operand(kind, operand.text);
    result.address_binding = operand.address_binding ==
      Operand::ADDRESS_PREEMPTIBLE ? MirOperand::ADDRESS_PREEMPTIBLE :
                                     MirOperand::ADDRESS_LOCAL;
    return result;
  }
  MirOperand resolve(const Operand & operand) const
  {
    if(operand.kind == Operand::OP_TEMP) {
      const std::unordered_map<std::string, ValueFact>::const_iterator found =
        values_.find(operand.text);
      if(found == values_.end()) throw std::runtime_error("missing lowered temporary");
      return found->second.location;
    }
    if(operand.kind == Operand::OP_SLOT) {
      const std::uint32_t slot = operand.slot;
      if(slot >= slot_offsets_.size() || !slot_offset_known_[slot])
        throw std::runtime_error("missing frame slot");
      return frame_operand(slot_offsets_[slot]);
    }
    if(operand.kind == Operand::OP_INTEGER)
      return immediate(integer_value(operand));
    if(operand.kind == Operand::OP_FLOAT) return float_immediate(operand.text);
    if(operand.kind == Operand::OP_GLOBAL)
      return global_operand(MirOperand::OP_SYMBOL, operand);
    if(operand.kind == Operand::OP_LABEL)
      return native_block_operand(source_, operand);
    throw std::runtime_error("foundation operand is not implemented: " + operand.text);
  }
  const LowType & operand_type(const Operand & operand) const
  {
    if(operand.kind == Operand::OP_TEMP) {
      const std::unordered_map<std::string, ValueFact>::const_iterator found =
        values_.find(operand.text);
      if(found == values_.end()) throw std::runtime_error("missing operand type");
      return found->second.type;
    }
    if(operand.kind == Operand::OP_SLOT)
      return lowir_model::lowir_slot_type(source_, operand.slot);
    if(operand.kind == Operand::OP_GLOBAL)
      return lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
    if(operand.kind == Operand::OP_FLOAT) {
      return floating_literal_type(operand.text);
    }
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
      return global_operand(MirOperand::OP_GLOBAL, operand);
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
                                  std::vector<MirInstruction> & out)
  {
    if(operand.kind == Operand::OP_GLOBAL) {
      const std::unordered_map<std::string, std::string>::const_iterator wrapper =
        tls_wrappers_.find(operand.text);
      if(wrapper == tls_wrappers_.end()) return storage(operand);
      MirInstruction address = machine_instruction(MirInstruction::MI_TLS_ADDR);
      append_operand(address, reg_operand(XR_R11));
      append_operand(address, named_operand(MirOperand::OP_SYMBOL, wrapper->second));
      address.tls_storage_symbol = operand.text;
      out.push_back(address);
      return dereference(XR_R11);
    }
    if(operand.kind == Operand::OP_SLOT)
      return storage(operand);
    if(operand.kind == Operand::OP_TEMP) {
      const std::unordered_map<std::string, ValueFact>::const_iterator value =
        values_.find(operand.text);
      if(value != values_.end() && value->second.deferred_address)
        return value->second.location;
    }
    if(is_frame_address(operand)) {
      append_address(out, XR_RCX, resolve(operand));
      return dereference(XR_RCX);
    }
    const MirOperand address = resolve(operand);
    if(address.kind == MirOperand::OP_REG) return dereference(address.reg);
    move_value_to_register(out, XR_RCX, address, operand_type(operand));
    return dereference(XR_RCX);
  }
  wide::Value wide_value(const Operand & operand) const
  {
    if(operand.kind == Operand::OP_INTEGER)
      return wide::literal_value(operand.text);
    return wide::storage_value(resolve(operand));
  }
  bool can_reuse(const Operand & operand) const
  {
    if(operand.kind != Operand::OP_TEMP) return false;
    const std::unordered_map<std::string, std::size_t>::const_iterator use =
      facts_.uses.find(operand.text);
    const std::unordered_map<std::string, ValueFact>::const_iterator value =
      values_.find(operand.text);
    const bool destructive_parameter = use != facts_.uses.end() && value != values_.end() &&
      incoming_parameter_registers_.count(operand.text) &&
      facts_.destructive_parameters.count(operand.text) && use->second == 1;
    const bool reusable_destructive_parameter = destructive_parameter &&
      !facts_.loop_invariant_values.count(operand.text);
    return use != facts_.uses.end() && use->second == 1 && value != values_.end() &&
           (!value->second.parameter || reusable_destructive_parameter) &&
           !value->second.fixed_register_home &&
           (!facts_.edge_live.count(operand.text) || reusable_destructive_parameter) &&
           value->second.location.kind == MirOperand::OP_REG;
  }
  void consume(const Operand & operand, X64Register retained = XR_RSP)
  {
    if(operand.kind != Operand::OP_TEMP) return;
    std::unordered_map<std::string, std::size_t>::iterator found = facts_.uses.find(operand.text);
    if(found == facts_.uses.end() || found->second == 0)
      throw std::runtime_error("invalid temporary use count");
    const bool stops_being_live = found->second == 1 &&
      !facts_.edge_live.count(operand.text);
    --found->second;
    if(stops_being_live)
    {
      const std::unordered_map<std::string, ValueFact>::const_iterator value =
        values_.find(operand.text);
      remove_live_location(&value->first, value->second.location);
    }
    if(found->second == 0) {
      const ValueFact & value = values_.find(operand.text)->second;
      if(value.location.kind == MirOperand::OP_REG &&
         !value.parameter &&
         !value.fixed_register_home &&
         !facts_.loop_invariant_values.count(operand.text) &&
         value.location.reg != retained && value.location.reg != XR_RAX &&
         !has_live_location_alias(operand.text, value.location)) {
        registers_.release(value.location.reg);
      }
      if(!value.parameter && value.location.kind == MirOperand::OP_XMM &&
         !has_live_location_alias(operand.text, value.location))
        xmms_.release(value.location.xmm);
      if(value.deferred_address) {
        consume(value.deferred_address_base, retained);
        consume(value.deferred_address_index, retained);
      }
    }
  }
  bool current_instruction_uses(const std::string & name) const
  {
    if(!active_instruction_) return false;
    const Operand * fixed[] = {
      &active_instruction_->first, &active_instruction_->second,
      &active_instruction_->third
    };
    for(std::size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
      if(fixed[i]->kind != Operand::OP_TEMP) continue;
      if(fixed[i]->text == name) return true;
      const std::unordered_map<std::string, ValueFact>::const_iterator value =
        values_.find(fixed[i]->text);
      if(value != values_.end() && value->second.deferred_address &&
         ((value->second.deferred_address_base.kind == Operand::OP_TEMP &&
           value->second.deferred_address_base.text == name) ||
          (value->second.deferred_address_index.kind == Operand::OP_TEMP &&
           value->second.deferred_address_index.text == name)))
        return true;
    }
    for(std::size_t i = 0; i < active_instruction_->args.size(); ++i)
      if(active_instruction_->args[i].kind == Operand::OP_TEMP &&
         active_instruction_->args[i].text == name) return true;
    return false;
  }
  static bool managed_register(X64Register reg)
  {
    return reg == XR_RDI || reg == XR_RSI || reg == XR_R8 || reg == XR_R9 ||
      is_callee_saved(reg);
  }
  bool value_is_live(const std::string & name) const
  {
    const std::unordered_map<std::string, std::size_t>::const_iterator uses =
      facts_.uses.find(name);
    return (uses != facts_.uses.end() && uses->second != 0) ||
      facts_.edge_live.count(name);
  }
  void add_live_location(const std::string * name, const MirOperand & location)
  {
    if(location.kind == MirOperand::OP_REG)
      live_gpr_values_[location.reg].push_back(name);
    else if(location.kind == MirOperand::OP_XMM)
      live_xmm_values_[location.xmm].push_back(name);
    else return;
    if(stats_) ++stats_->live_location_updates;
  }
  void remove_live_location(const std::string * name,
                            const MirOperand & location)
  {
    std::vector<const std::string *> * values = 0;
    if(location.kind == MirOperand::OP_REG) values = &live_gpr_values_[location.reg];
    else if(location.kind == MirOperand::OP_XMM) values = &live_xmm_values_[location.xmm];
    if(!values) return;
    const std::vector<const std::string *>::iterator found =
      std::find(values->begin(), values->end(), name);
    if(found == values->end())
      throw std::logic_error("native live-location index is inconsistent");
    *found = values->back();
    values->pop_back();
    if(stats_) ++stats_->live_location_updates;
  }
  void set_value(const std::string & name, const ValueFact & replacement)
  {
    std::unordered_map<std::string, ValueFact>::iterator existing =
      values_.find(name);
    const bool live = value_is_live(name);
    if(live && existing != values_.end())
      remove_live_location(&existing->first, existing->second.location);
    if(existing == values_.end())
      existing = values_.emplace(name, replacement).first;
    else existing->second = replacement;
    if(live) add_live_location(&existing->first, replacement.location);
  }
  void set_value_location(const std::string & name,
                          const MirOperand & replacement)
  {
    std::unordered_map<std::string, ValueFact>::iterator value = values_.find(name);
    if(value == values_.end())
      throw std::logic_error("cannot move an unknown native value");
    const bool live = value_is_live(name);
    if(live) remove_live_location(&value->first, value->second.location);
    value->second.location = replacement;
    if(replacement.kind == MirOperand::OP_FRAME &&
       replacement.frame_binding != 0) {
      value->second.has_spill_home = true;
      value->second.spill_home = replacement;
    }
    if(live) add_live_location(&value->first, replacement);
  }
  bool has_live_location_alias(const std::string & name,
                               const MirOperand & location) const
  {
    if(stats_) ++stats_->live_location_alias_queries;
    const std::size_t self = value_is_live(name) ? 1 : 0;
    if(location.kind == MirOperand::OP_REG)
      return live_gpr_values_[location.reg].size() > self;
    if(location.kind == MirOperand::OP_XMM)
      return live_xmm_values_[location.xmm].size() > self;
    return false;
  }
  void stabilize_edge_live_result(
      const Instruction & instruction,
      std::vector<MirInstruction> & out)
  {
    if(instruction.dest.empty() ||
       !facts_.edge_live.count(instruction.dest) ||
       (!facts_.loop_invariant_values.count(instruction.dest) &&
        !result_crosses_call(instruction.dest))) return;
    std::unordered_map<std::string, ValueFact>::iterator value =
      values_.find(instruction.dest);
    if(value == values_.end()) return;
    const MirOperand location = value->second.location;
    if(location.kind != MirOperand::OP_REG &&
       location.kind != MirOperand::OP_XMM) return;
    const MirOperand home =
      allocate_temp_frame_binding(instruction.dest, value->second.type);
    if(location.kind == MirOperand::OP_XMM) {
      append_float_move(out, home, location,
                        value->second.type);
      if(!has_live_location_alias(instruction.dest, location))
        xmms_.release(location.xmm);
    } else {
      append_store(out, home, location,
                   value->second.type);
      if(!has_live_location_alias(instruction.dest, location)) {
        registers_.release(location.reg);
      }
    }
    set_value_location(instruction.dest, home);
  }
  bool spill_candidate(
      const std::unordered_map<std::string, ValueFact>::iterator & value,
      bool needs_callee_saved) const
  {
    if(value->second.parameter ||
       value->second.location.kind != MirOperand::OP_REG ||
       !managed_register(value->second.location.reg) ||
       (needs_callee_saved && !is_callee_saved(value->second.location.reg)) ||
       has_live_location_alias(value->first, value->second.location) ||
       current_instruction_uses(value->first)) return false;
    if(!control_flow_.SpillIsSafe(value->first, position_)) return false;
    const std::unordered_map<std::string, std::size_t>::const_iterator uses =
      facts_.uses.find(value->first);
    return uses != facts_.uses.end() && uses->second != 0;
  }
  std::unordered_map<std::string, ValueFact>::iterator
  find_spill_victim_full_scan(bool needs_callee_saved)
  {
    std::unordered_map<std::string, ValueFact>::iterator victim = values_.end();
    std::size_t farthest_use = 0;
    for(std::unordered_map<std::string, ValueFact>::iterator value = values_.begin();
        value != values_.end(); ++value) {
      if(stats_) ++stats_->spill_value_visits;
      if(!spill_candidate(value, needs_callee_saved)) continue;
      const std::size_t last = facts_.last_use.count(value->first) ?
        facts_.last_use.find(value->first)->second : 0;
      if(victim == values_.end() || last >= farthest_use) {
        victim = value;
        farthest_use = last;
      }
    }
    return victim;
  }
  std::unordered_map<std::string, ValueFact>::iterator
  find_spill_victim(bool needs_callee_saved)
  {
    std::unordered_map<std::string, ValueFact>::iterator victim = values_.end();
    std::size_t farthest_use = 0;
    bool tied = false;
    for(std::size_t reg = 0; reg < 16; ++reg) {
      const std::vector<const std::string *> & occupants = live_gpr_values_[reg];
      for(std::size_t i = 0; i < occupants.size(); ++i) {
        if(stats_) ++stats_->spill_value_visits;
        std::unordered_map<std::string, ValueFact>::iterator value =
          values_.find(*occupants[i]);
        if(value == values_.end())
          throw std::logic_error("native live-location value is missing");
        if(!spill_candidate(value, needs_callee_saved)) continue;
        if(stats_) ++stats_->spill_candidates;
        const std::unordered_map<std::string, std::size_t>::const_iterator last_use =
          facts_.last_use.find(value->first);
        const std::size_t last = last_use == facts_.last_use.end() ? 0 :
          last_use->second;
        if(victim == values_.end() || last > farthest_use) {
          victim = value;
          farthest_use = last;
          tied = false;
        } else if(last == farthest_use) {
          tied = true;
        }
      }
    }
    if(!tied) return victim;
    if(stats_) ++stats_->spill_full_scan_fallbacks;
    return find_spill_victim_full_scan(needs_callee_saved);
  }
  bool spill_one(bool needs_callee_saved, std::vector<MirInstruction> & out)
  {
    if(stats_) ++stats_->spill_attempts;
    std::unordered_map<std::string, ValueFact>::iterator victim =
      find_spill_victim(needs_callee_saved);
    if(victim == values_.end()) return false;
    MirOperand home;
    if(victim->second.has_spill_home) home = victim->second.spill_home;
    else
      home = allocate_temp_frame_binding(victim->first, victim->second.type);
    append_store(out, home, victim->second.location,
                 victim->second.type);
    const X64Register released = victim->second.location.reg;
    set_value_location(victim->first, home);
    registers_.release(released);
    if(stats_) ++stats_->spills;
    return true;
  }
  bool reclaim_dead_parameter_register(bool needs_callee_saved)
  {
    if(control_flow_.CurrentBlockIsCyclic()) return false;
    if(stats_) ++stats_->reclaim_attempts;
    for(std::size_t i = 0; i < source_.params.size(); ++i) {
      if(stats_) ++stats_->reclaim_parameter_visits;
      std::unordered_map<std::string, ValueFact>::iterator value = values_.find(source_.params[i].name);
      if(value == values_.end()) continue;
      if(!value->second.parameter || value->second.fixed_register_home ||
         value->second.location.kind != MirOperand::OP_REG ||
         !managed_register(value->second.location.reg) ||
         !registers_.is_used(value->second.location.reg) ||
         (needs_callee_saved && !is_callee_saved(value->second.location.reg)))
        continue;
      const std::unordered_map<std::string, std::size_t>::const_iterator uses =
        facts_.uses.find(value->first);
      if(uses == facts_.uses.end() || uses->second != 0 || facts_.edge_live.count(value->first) ||
         !control_flow_.SpillIsSafe(value->first, position_) ||
         has_live_location_alias(value->first, value->second.location)) continue;
      registers_.release(value->second.location.reg);
      if(stats_) ++stats_->reclaims;
      return true;
    }
    return false;
  }
  bool try_reserve_result_register(const std::string & name,
                                   bool needs_callee_saved,
                                   X64Register * result)
  {
    static const X64Register caller_saved[] = {
      XR_R8, XR_R9, XR_RDI, XR_RSI
    };
    if(!needs_callee_saved)
      for(std::size_t i = 0;
          i < sizeof(caller_saved) / sizeof(caller_saved[0]); ++i)
        if(!crosses_register_clobber(name, caller_saved[i]) &&
           registers_.try_reserve(caller_saved[i])) {
          *result = caller_saved[i];
          return true;
        }
    return registers_.try_allocate(true, *result);
  }
  bool try_allocate_result(const std::string & name,
                           std::vector<MirInstruction> & out,
                           X64Register * result,
                           bool force_preserved = false)
  {
    const bool across = force_preserved || result_crosses_call(name);
    if(try_reserve_result_register(name, across, result)) return true;
    if(reclaim_dead_parameter_register(across) &&
       try_reserve_result_register(name, across, result)) return true;
    if(spill_one(across, out) &&
       try_reserve_result_register(name, across, result)) return true;
    return false;
  }
  X64Register allocate_result(const std::string & name,
                              std::vector<MirInstruction> & out,
                              bool force_preserved = false)
  {
    X64Register result = XR_RSP;
    if(try_allocate_result(name, out, &result, force_preserved)) return result;
    throw std::runtime_error("reactive GPR allocation exhausted in " +
      source_.name + " for " + name + " at LowIR position " +
	  std::to_string(position_));
  }
  MirOperand allocate_temp_home(const std::string & name, const LowType & type)
  {
    return allocate_temp_frame_binding(name, type);
  }
  MirOperand allocate_float_result(const std::string & name, const LowType & type)
  {
    uses_scalar_float_ = true;
    if(is_extended_float(type)) return allocate_temp_home(name, type);
    if(result_crosses_call(name)) return allocate_temp_home(name, type);
    XmmRegister result = XMM_0;
    if(xmms_.try_allocate(result)) return xmm_operand(result);
    return allocate_temp_home(name, type);
  }
  void define(const std::string & name, const LowType & type, const MirOperand & location)
  {
    ValueFact value;
    value.location = location;
    value.type = type;
    if(location.kind == MirOperand::OP_FRAME && location.frame_binding != 0) {
      value.has_spill_home = true;
      value.spill_home = location;
    }
    set_value(name, value);
    if(facts_.uses.count(name)) return;
    if(location.kind == MirOperand::OP_REG && registers_.is_used(location.reg) &&
       !has_live_location_alias(name, location)) {
      registers_.release(location.reg);
    }
    else if(location.kind == MirOperand::OP_XMM && xmms_.is_used(location.xmm) &&
            !has_live_location_alias(name, location))
      xmms_.release(location.xmm);
  }
  bool is_frame_address(const Operand & operand) const
  {
    if(operand.kind != Operand::OP_TEMP) return false;
    const std::unordered_map<std::string, ValueFact>::const_iterator found =
      values_.find(operand.text);
    return found != values_.end() && found->second.frame_address;
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
      const std::unordered_map<std::string, std::string>::const_iterator wrapper =
        tls_wrappers_.find(operand.text);
      if(wrapper != tls_wrappers_.end()) {
        MirInstruction address = machine_instruction(MirInstruction::MI_TLS_ADDR);
        append_operand(address, reg_operand(destination));
        append_operand(address, named_operand(MirOperand::OP_SYMBOL, wrapper->second));
        address.tls_storage_symbol = operand.text;
        out.push_back(address);
        return;
      }
      append_move(out, reg_operand(destination),
                  global_operand(MirOperand::OP_SYMBOL, operand));
      return;
    }
    if(operand.kind != Operand::OP_TEMP)
      throw std::runtime_error("bulk object operand is not addressable: " +
        operand.text + " (kind " + std::to_string(operand.kind) + ")");
    const std::unordered_map<std::string, ValueFact>::const_iterator found =
      values_.find(operand.text);
    if(found == values_.end()) throw std::runtime_error("missing address value");
    const MirOperand & location = found->second.location;
    if(found->second.frame_address || found->second.type.kind == lowir_model::LTK_OBJECT ||
       wide::is_integer(found->second.type)) {
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
      return global_operand(MirOperand::OP_SYMBOL, operand);
    if(operand.kind != Operand::OP_TEMP)
      throw std::runtime_error("call argument cannot be passed by address");
    std::unordered_map<std::string, ValueFact>::iterator found = values_.find(operand.text);
    if(found == values_.end()) throw std::runtime_error("missing addressable temporary");
    if(found->second.frame_address || found->second.location.kind == MirOperand::OP_FRAME)
      return found->second.location;
    const MirOperand home = allocate_temp_home(operand.text, found->second.type);
    if(found->second.location.kind == MirOperand::OP_XMM)
      append_float_move(out, home, found->second.location, found->second.type);
    else
      append_store(out, home, found->second.location, found->second.type);
    if(found->second.location.kind == MirOperand::OP_REG &&
       !has_live_location_alias(operand.text, found->second.location))
      registers_.release(found->second.location.reg);
    else if(found->second.location.kind == MirOperand::OP_XMM &&
            !has_live_location_alias(operand.text, found->second.location))
      xmms_.release(found->second.location.xmm);
    set_value_location(operand.text, home);
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
    const std::unordered_map<std::string, ValueFact>::const_iterator found =
      values_.find(operand.text);
    if(found == values_.end() || !found->second.has_frame_provenance) return false;
    offset = found->second.frame_provenance;
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
  static bool operand_uses_register(const MirOperand & operand,
                                    X64Register reg)
  {
    return (operand.kind == MirOperand::OP_REG && operand.reg == reg) ||
      (operand.kind == MirOperand::OP_DEREF &&
       (operand.reg == reg || (operand.has_index && operand.index == reg)));
  }
  bool result_crosses_call(const std::string & name) const
  {
    return (facts_.last_use.count(name) && crosses_call(name)) ||
      storage_facts_.tls_store_inputs.count(name);
  }
  MirOperand binary_destination(const Instruction & instruction,
                                const MirOperand & left,
                                std::vector<MirInstruction> & out,
                                bool allow_same_instruction_duplicate = false,
                                MirOperand * pressure_home = 0,
                                const LowType * pressure_type = 0)
  {
    const bool duplicate_last_use = allow_same_instruction_duplicate &&
      instruction.first.kind == Operand::OP_TEMP &&
      instruction.first.text == instruction.second.text &&
      facts_.uses.find(instruction.first.text) != facts_.uses.end() &&
      facts_.uses.find(instruction.first.text)->second == 2 &&
      !values_.find(instruction.first.text)->second.parameter;
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
  MirInstruction::Opcode float_binary_opcode(const std::string & operation) const
  {
    if(operation == "add") return MirInstruction::MI_FADD;
    if(operation == "sub") return MirInstruction::MI_FSUB;
    if(operation == "mul") return MirInstruction::MI_FMUL;
    if(operation == "div") return MirInstruction::MI_FDIV;
    throw std::runtime_error("floating binary operation is not implemented: " + operation);
  }
  MirInstruction::Opcode float_compare_opcode(const std::string & predicate) const
  {
    if(predicate == "eq") return MirInstruction::MI_FEQ;
    if(predicate == "ne") return MirInstruction::MI_FNE;
    if(predicate == "lt") return MirInstruction::MI_FLT;
    if(predicate == "gt") return MirInstruction::MI_FGT;
    if(predicate == "le") return MirInstruction::MI_FLE;
    if(predicate == "ge") return MirInstruction::MI_FGE;
    throw std::runtime_error("floating comparison predicate is not implemented: " + predicate);
  }
  X86Condition float_predicate_condition(const std::string & predicate) const
  {
    // FCMP models the right operand compared with the left operand so the
    // unsigned x86 conditions directly describe ordered floating predicates.
    if(predicate == "eq") return XC_E;
    if(predicate == "ne") return XC_NE;
    if(predicate == "lt") return XC_A;
    if(predicate == "gt") return XC_B;
    if(predicate == "le") return XC_AE;
    if(predicate == "ge") return XC_BE;
    throw std::runtime_error("floating branch predicate is not implemented: " + predicate);
  }
  void emit_float_const(const Instruction & instruction,
                        std::vector<MirInstruction> & out)
  {
    const MirOperand destination = allocate_float_result(instruction.dest, instruction.type);
    append_float_move(out, destination, float_immediate(instruction.first.text),
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
      comparison.op == "ne" ? branch.second : branch.third));
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
    MirInstruction compare = machine_instruction(float_compare_opcode(instruction.op),
                                                 instruction.type);
    append_operand(compare, reg_operand(XR_RAX));
    append_operand(compare, resolve(instruction.first));
    append_operand(compare, resolve(instruction.second));
    out.push_back(compare);
    MirOperand destination = reg_operand(XR_RAX);
    if(!result_is_immediate_return(block, instruction_index, instruction.dest)) {
      destination = reg_operand(allocate_result(instruction.dest, out));
      append_move(out, destination, reg_operand(XR_RAX));
    }
    consume(instruction.first);
    consume(instruction.second);
    define(instruction.dest, lowir_model::builtin_lowir_type(lowir_model::LTK_I64),
           destination);
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
    if(instruction.op != "neg")
      throw std::runtime_error("floating unary operation is not implemented: " + instruction.op);
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
    if(destination_wide && is_integer_or_pointer(instruction.source_type)) { const MirOperand destination = allocate_temp_home(instruction.dest, instruction.type); move_value_to_register(out, XR_RAX, resolve(instruction.first), instruction.source_type); append_integer_normalization(out, instruction.source_type, reg_operand(XR_RAX)); if(instruction.op == "sext") out.push_back(machine_instruction(MirInstruction::MI_CQO)); else append_move(out, reg_operand(XR_RDX), immediate(0)); append_store(out, destination, reg_operand(XR_RAX), machine_type(lowir_model::LTK_I64)); MirOperand high = destination; high.offset += 8; append_store(out, high, reg_operand(XR_RDX), machine_type(lowir_model::LTK_I64)); consume(instruction.first); define(instruction.dest, instruction.type, destination); return; }
    const bool source_float = is_floating(instruction.source_type);
    const bool destination_float = is_floating(instruction.type);
    if(destination_wide && source_float) {
      uses_scalar_float_ = true;
      const MirOperand destination =
        allocate_temp_home(instruction.dest, instruction.type);
      MirInstruction::Opcode opcode;
      if(instruction.op == "fptosi") opcode = MirInstruction::MI_FPTOSI;
      else if(instruction.op == "fptoui") opcode = MirInstruction::MI_FPTOUI;
      else throw std::runtime_error(
        "floating-to-i128 conversion is not implemented: " + instruction.op);
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
      if(instruction.op == "uitofp") opcode = MirInstruction::MI_UITOFP;
      else if(instruction.op == "fptosi") opcode = MirInstruction::MI_FPTOSI;
      else if(instruction.op == "fptoui") opcode = MirInstruction::MI_FPTOUI;
      else if(instruction.op == "fpext") opcode = MirInstruction::MI_FPEXT;
      else if(instruction.op == "fptrunc") opcode = MirInstruction::MI_FPTRUNC;
      else if(instruction.op != "sitofp")
        throw std::runtime_error("floating conversion is not implemented: " + instruction.op);
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
      if(instruction.op == "sext" || instruction.op == "zext") {
        append_integer_extension(out, destination,
          lowir_model::lowir_type_bit_width(instruction.source_type),
          instruction.op == "sext");
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
  void emit_division(const Instruction & instruction, const MirOperand & destination,
                     const MirOperand & right, std::vector<MirInstruction> & out)
  {
    append_move(out, reg_operand(XR_RDX), right);
    append_move(out, reg_operand(XR_RCX), reg_operand(XR_RDX));
    append_move(out, reg_operand(XR_RAX), destination);
    const bool unsigned_operation = instruction.op == "udiv" || instruction.op == "umod";
    if(unsigned_operation) append_move(out, reg_operand(XR_RDX), immediate(0));
    else out.push_back(machine_instruction(MirInstruction::MI_CQO));
    MirInstruction divide = machine_instruction(unsigned_operation ?
      MirInstruction::MI_DIV : MirInstruction::MI_IDIV);
    append_operand(divide, reg_operand(XR_RCX));
    out.push_back(divide);
    const bool remainder = instruction.op == "mod" || instruction.op == "umod";
    append_move(out, destination, reg_operand(remainder ? XR_RDX : XR_RAX));
  }
  void emit_shift(const Instruction & instruction, const MirOperand & destination,
                  const MirOperand & right, std::vector<MirInstruction> & out)
  {
    move_value_to_register(out, XR_RCX, right,
                           operand_type(instruction.second));
    MirInstruction::Opcode opcode = MirInstruction::MI_SHL_CL;
    if(instruction.op == "shr") opcode = MirInstruction::MI_SAR_CL;
    else if(instruction.op == "ushr") opcode = MirInstruction::MI_SHR_CL;
    MirInstruction shift = machine_instruction(opcode);
    append_operand(shift, destination);
    out.push_back(shift);
  }
  void emit_binary(const Instruction & instruction,
                   std::vector<MirInstruction> & out)
  {
    if(wide::is_integer(instruction.type)) { const MirOperand destination = allocate_temp_home(instruction.dest, instruction.type); wide::append_binary(destination, wide_value(instruction.first), wide_value(instruction.second), instruction.op, out); consume(instruction.first); consume(instruction.second); define(instruction.dest, instruction.type, destination); return; }
    if(is_floating(instruction.type)) {
      emit_float_binary(instruction, out);
      return;
    }
    if(!is_integer_or_pointer(instruction.type))
      throw std::runtime_error("integer selector received non-integer binary operation");
    const MirOperand left = resolve(instruction.first);
    MirOperand right = resolve(instruction.second);
    const bool pressure_leaf = constrained_wide_pressure() &&
      instruction.first.kind == Operand::OP_TEMP &&
      facts_.parameters.count(instruction.first.text);
    MirOperand pressure_home;
    MirOperand destination;
    if(!pressure_leaf)
      destination = binary_destination(instruction, left, out, true, &pressure_home);
    else if(instruction.first.text == source_.params.front().name) {
      destination = reg_operand(XR_R15);
      if(!registers_.is_used(XR_R15)) registers_.reserve(XR_R15);
      move_value_to_register(out, XR_R15, left, operand_type(instruction.first));
    } else {
      pressure_home = allocate_temp_home(instruction.dest, instruction.type);
      destination = reg_operand(XR_RAX);
      move_value_to_register(out, XR_RAX, left, operand_type(instruction.first));
    }
    const bool shift = instruction.op == "shl" || instruction.op == "shr" ||
                       instruction.op == "ushr";
    if(!shift && (right.kind == MirOperand::OP_FRAME ||
       right.kind == MirOperand::OP_GLOBAL ||
       right.kind == MirOperand::OP_DEREF)) {
      move_value_to_register(out, XR_RDX, right, operand_type(instruction.second));
      right = reg_operand(XR_RDX);
      append_integer_normalization(out, operand_type(instruction.second), right);
    }
    const bool bitwise = instruction.op == "and" || instruction.op == "or" ||
                         instruction.op == "xor";
    if(right.kind == MirOperand::OP_IMM && !bitwise &&
       (right.imm < INT32_MIN || right.imm > INT32_MAX)) {
      append_move(out, reg_operand(XR_RDX), right);
      right = reg_operand(XR_RDX);
    }
    MirInstruction::Opcode opcode = MirInstruction::MI_ADD;
    if(instruction.op == "sub") opcode = MirInstruction::MI_SUB;
    else if(instruction.op == "mul") opcode = MirInstruction::MI_IMUL;
    else if(instruction.op == "and") opcode = MirInstruction::MI_AND;
    else if(instruction.op == "or") opcode = MirInstruction::MI_OR;
    else if(instruction.op == "xor") opcode = MirInstruction::MI_XOR;
    else if(instruction.op == "div" || instruction.op == "mod" ||
            instruction.op == "udiv" || instruction.op == "umod") {
      emit_division(instruction, destination, right, out);
      append_integer_normalization(out, instruction.type, destination);
      consume(instruction.first, destination.reg);
      consume(instruction.second, destination.reg);
      if(pressure_home.kind == MirOperand::OP_FRAME)
        append_store(out, pressure_home, destination, instruction.type);
      define(instruction.dest, instruction.type,
             pressure_home.kind == MirOperand::OP_FRAME ? pressure_home : destination);
      return;
    } else if(instruction.op == "shl" || instruction.op == "shr" ||
              instruction.op == "ushr") {
      emit_shift(instruction, destination, right, out);
      append_integer_normalization(out, instruction.type, destination);
      consume(instruction.first, destination.reg);
      consume(instruction.second, destination.reg);
      if(pressure_home.kind == MirOperand::OP_FRAME)
        append_store(out, pressure_home, destination, instruction.type);
      define(instruction.dest, instruction.type,
             pressure_home.kind == MirOperand::OP_FRAME ? pressure_home : destination);
      return;
    } else if(instruction.op != "add") {
      throw std::runtime_error("integer binary operation is not implemented: " + instruction.op);
    }
    MirInstruction operation = machine_instruction(opcode, instruction.type);
    append_operand(operation, destination);
    append_operand(operation, right);
    out.push_back(operation);
    append_integer_normalization(out, instruction.type, destination);
    consume(instruction.first, destination.reg);
    consume(instruction.second, destination.reg);
    if(pressure_home.kind == MirOperand::OP_FRAME)
      append_store(out, pressure_home, destination, instruction.type);
    define(instruction.dest, instruction.type,
           pressure_home.kind == MirOperand::OP_FRAME ? pressure_home : destination);
  }
  bool comparison_feeds_branch(const lowir_model::LowirBlock & block,
                               std::size_t instruction_index,
                               const Instruction & comparison) const
  {
    if(instruction_index + 1 >= block.instructions.size() ||
       facts_.uses.find(comparison.dest) == facts_.uses.end() ||
       facts_.uses.find(comparison.dest)->second != 1) return false;
    const Instruction & branch = block.instructions[instruction_index + 1];
    return branch.kind == Instruction::IK_BRANCH && branch.first.text == comparison.dest;
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
  void emit_compare_value(const Instruction & instruction,
                          const lowir_model::LowirBlock & block,
                          std::size_t instruction_index,
                          std::vector<MirInstruction> & out)
  {
    const bool direct_return = result_is_immediate_return(
      block, instruction_index, instruction.dest);
    if(wide::is_integer(instruction.type)) {
      wide::append_compare(wide_value(instruction.first),
                           wide_value(instruction.second), instruction.op, out);
      const MirOperand destination = direct_return ? reg_operand(XR_RAX) :
        reg_operand(allocate_result(instruction.dest, out));
      append_move(out, destination, reg_operand(XR_R10));
      consume(instruction.first);
      consume(instruction.second);
      define(instruction.dest,
        lowir_model::builtin_lowir_type(lowir_model::LTK_I64), destination);
      return;
    }
    if(!is_integer_or_pointer(instruction.type))
      throw std::runtime_error("integer selector received non-integer comparison");
    const MirOperand left = resolve(instruction.first);
    const LowType result_type =
      lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
    MirOperand right = resolve(instruction.second);
    MirOperand pressure_home;
    MirOperand destination;
    const bool left_already_in_rax =
      left.kind == MirOperand::OP_REG && left.reg == XR_RAX;
    if(direct_return &&
       (left_already_in_rax || !operand_uses_register(right, XR_RAX))) {
      destination = reg_operand(XR_RAX);
      move_value_to_register(
        out, destination.reg, left, operand_type(instruction.first));
      if(left.kind == MirOperand::OP_FRAME ||
         left.kind == MirOperand::OP_GLOBAL ||
         left.kind == MirOperand::OP_DEREF)
        append_integer_normalization(out, operand_type(instruction.first), destination);
    } else {
      destination = binary_destination(
        instruction, left, out, false, &pressure_home, &result_type);
    }
    if(right.kind != MirOperand::OP_REG) {
      const bool encodable_immediate = right.kind == MirOperand::OP_IMM &&
        (lowir_model::lowir_type_bit_width(instruction.type) < 64 ||
         (right.imm >= INT32_MIN && right.imm <= INT32_MAX));
      if(!encodable_immediate || !operand_uses_register(destination, XR_RDX)) {
        const X64Register scratch = operand_uses_register(destination, XR_RDX) ?
          XR_RAX : XR_RDX;
        move_value_to_register(
          out, scratch, right, operand_type(instruction.second));
        right = reg_operand(scratch);
      }
    }
    MirInstruction compare = machine_instruction(MirInstruction::MI_CMP,
                                                 instruction.type);
    append_operand(compare, destination);
    append_operand(compare, right);
    out.push_back(compare);
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
    if(instruction.op == "decay") {
      emit_copy(instruction, out,
        result_is_immediate_return(block, instruction_index, instruction.dest));
      return;
    }
    const MirOperand source = resolve(instruction.first);
    const LowType result_type = instruction.op == "not" ?
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
    if(instruction.op == "not") {
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
      if(instruction.op == "bitnot") opcode = MirInstruction::MI_NOT;
      else if(instruction.op == "bswap") opcode = MirInstruction::MI_BSWAP;
      else if(instruction.op != "neg")
        throw std::runtime_error("integer unary operation is not implemented: " + instruction.op);
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
  bool unary_not_feeds_branch(const lowir_model::LowirBlock & block,
                              std::size_t instruction_index,
                              const Instruction & instruction) const
  {
    return instruction.op == "not" &&
      comparison_feeds_branch(block, instruction_index, instruction);
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
  bool move_destination_is_safe(const std::vector<GprMove> & moves,
                                std::size_t candidate) const
  {
    for(std::size_t i = 0; i < moves.size(); ++i) {
      if(i == candidate || !moves[i].pending ||
         moves[i].source.kind != MirOperand::OP_REG) continue;
      if(moves[i].source.reg == moves[candidate].destination) return false;
    }
    return true;
  }
  void emit_gpr_move(const GprMove & move,
                     std::vector<MirInstruction> & out)
  {
    if(move.object_chunk) {
      emit_operand_address(out, XR_R11, move.object_source);
      append_load(out, reg_operand(move.destination),
                  dereference(XR_R11, static_cast<long long>(move.chunk_offset)),
                  move.type);
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
      move.source = resolve(instruction.args[i]);
      if(instruction.args[i].kind == Operand::OP_TEMP) {
        const ValueFact & value = values_.find(instruction.args[i].text)->second;
        if(!value.forwarded_parameter.empty())
          move.source = values_.find(value.forwarded_parameter)->second.location;
      }
      move.type = operand_type(instruction.args[i]);
      move.source_is_address = is_frame_address(instruction.args[i]);
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
  struct XmmMove
  {
    XmmRegister destination = XMM_0;
    MirOperand source;
    LowType type;
    bool pending = true;
  };
  bool xmm_destination_is_safe(const std::vector<XmmMove> & moves,
                               std::size_t candidate) const
  {
    for(std::size_t i = 0; i < moves.size(); ++i) {
      if(i == candidate || !moves[i].pending ||
         moves[i].source.kind != MirOperand::OP_XMM) continue;
      if(moves[i].source.xmm == moves[candidate].destination) return false;
    }
    return true;
  }
  long long xmm_call_scratch()
  {
    if(has_xmm_call_scratch_) return xmm_call_scratch_;
    xmm_call_scratch_ = allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP, "%xmm-call-scratch",
      lowir_model::builtin_lowir_type(lowir_model::LTK_F64));
    has_xmm_call_scratch_ = true;
    return xmm_call_scratch_;
  }
  void emit_parallel_xmm_moves(const Instruction & instruction,
                               std::vector<MirInstruction> & out)
  {
    std::size_t xmm_index = 0;
    std::vector<XmmMove> moves;
    for(std::size_t i = 0; i < instruction.args.size() && xmm_index < 8; ++i) {
      const LowType & type = operand_type(instruction.args[i]);
      if(!is_scalar_float(type)) continue;
      XmmMove move;
      move.destination = static_cast<XmmRegister>(xmm_index++);
      move.source = resolve(instruction.args[i]);
      move.type = type;
      moves.push_back(move);
    }
    std::size_t remaining = moves.size();
    while(remaining) {
      bool progressed = false;
      for(std::size_t i = 0; i < moves.size(); ++i) {
        if(!moves[i].pending || !xmm_destination_is_safe(moves, i)) continue;
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
      const FunctionSignatureIndex::const_iterator found =
        signatures_.find(instruction.first.text);
      if(found != signatures_.end() && found->second.params)
        parameters = *found->second.params;
    }
    if(parameters.size() > instruction.args.size())
      parameters.resize(instruction.args.size());
    while(parameters.size() < instruction.args.size()) {
      lowir_model::LowirParameter parameter;
      parameter.name = "%arg" + std::to_string(parameters.size());
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
    const FunctionSignatureIndex::const_iterator found =
      signatures_.find(instruction.first.text);
    return found != signatures_.end() && found->second.boundary &&
      found->second.boundary->arity == lowir_model::CAM_VARIADIC;
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
      std::vector<MirInstruction> & out)
  {
    std::vector<GprMove> gpr_moves;
    std::vector<XmmMove> xmm_moves;
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
            addressable[piece.parameter_index] : resolve(argument);
          move.source_is_address = needs_address[piece.parameter_index] ||
            is_frame_address(argument);
        }
        gpr_moves.push_back(move);
      } else if(piece.location == abi::PL_XMM) {
        XmmMove move;
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
        if(!xmm_moves[i].pending || !xmm_destination_is_safe(xmm_moves, i)) continue;
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
         copy.first.kind == Operand::OP_TEMP && copy.first.text == instruction.dest) {
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
         copy.first.kind == Operand::OP_TEMP && copy.first.text == instruction.dest &&
         copy.second.kind == Operand::OP_TEMP && copy.second.text == address.dest) {
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
    if(destination) emit_operand_address(out, XR_R11, *destination);
    else append_address(out, XR_R11, *home);
    const std::size_t chunks = (type.storage_size + 7) / 8;
    for(std::size_t chunk = 0; chunk < chunks; ++chunk) {
      const LowType & chunk_type = abi::object_chunk_type(type.storage_size - chunk * 8);
      append_store(out, dereference(XR_R11, static_cast<long long>(chunk * 8)),
                   reg_operand(chunk ? XR_RDX : XR_RAX), chunk_type);
    }
  }
  void define_object_result(const std::string & name,
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
        const ValueFact & address = values_.find(destination->text)->second;
        value.location = address.location;
        value.frame_address = address.frame_address;
      } else {
        value.location = global_operand(MirOperand::OP_SYMBOL, *destination);
      }
    } else {
      value.location = home;
      value.frame_address = true;
      value.has_frame_provenance = true;
      value.frame_provenance = home.offset;
    }
    set_value(name, value);
  }
  void emit_extended_call(
      const Instruction & instruction,
      const lowir_model::LowirBlock & block,
      std::size_t instruction_index,
      const std::vector<lowir_model::LowirParameter> & parameters,
      std::vector<MirInstruction> & out)
  {
    const bool direct = instruction.first.kind == Operand::OP_GLOBAL;
    if(!direct)
      move_value_to_register(out, XR_R10, resolve(instruction.first),
                             operand_type(instruction.first));
    const abi::Plan plan = abi::classify(parameters);
    stabilize_extended_register_sources(instruction, parameters, plan, out);
    std::vector<bool> needs_address(parameters.size(), false);
    std::vector<MirOperand> addressable(parameters.size());
    for(std::size_t i = 0; i < parameters.size(); ++i) {
      needs_address[i] = argument_needs_address(parameters[i], instruction.args[i]);
      if(needs_address[i]) addressable[i] = make_addressable(instruction.args[i], out);
      if(is_floating(parameters[i].type)) uses_scalar_float_ = true;
    }
    emit_extended_stack_arguments(instruction, parameters, plan,
                                  addressable, needs_address, out);
    emit_extended_register_arguments(instruction, parameters, plan,
                                     addressable, needs_address, out);
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
      named_operand(MirOperand::OP_SYMBOL, instruction.first.text) :
      reg_operand(XR_R10));
    out.push_back(call);
    emit_stack_adjust(out, MirInstruction::MI_ADD, plan.stack_bytes);
    // The call and its stack teardown have consumed every input.  Retire them
    // before assigning a home to the return value: otherwise an extended call
    // can make all managed registers look unspillable merely because its dead
    // arguments still belong to the active LowIR instruction.
    for(std::size_t i = 0; i < instruction.args.size(); ++i)
      consume(instruction.args[i]);
    consume(instruction.first);
    active_instruction_ = 0;
    const bool materialize_result = !instruction.call_returns_void &&
      (instruction.type.kind == lowir_model::LTK_OBJECT ||
       (facts_.uses.find(instruction.dest) != facts_.uses.end() &&
        facts_.uses.find(instruction.dest)->second != 0));
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
      else home = allocate_temp_home(instruction.dest, instruction.type);
      store_object_return_registers(instruction.type,
                                    alias ? &alias_destination : 0,
                                    alias ? 0 : &home, out);
      define_object_result(instruction.dest, instruction.type,
                           alias ? &alias_destination : 0, home);
      if(alias) {
        Operand elided_use;
        elided_use.kind = Operand::OP_TEMP;
        elided_use.text = instruction.dest;
        consume(elided_use);
      }
    } else if(materialize_result && wide::is_integer(instruction.type)) { const MirOperand home = allocate_temp_home(instruction.dest, instruction.type); append_store(out, home, reg_operand(XR_RAX), machine_type(lowir_model::LTK_I64)); MirOperand high = home; high.offset += 8; append_store(out, high, reg_operand(XR_RDX), machine_type(lowir_model::LTK_I64)); define(instruction.dest, instruction.type, home); } else if(materialize_result && is_extended_float(instruction.type)) {
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
      if(!result_is_immediate_return(block, instruction_index, instruction.dest) &&
         !selection::result_is_immediately_stored(
           block, instruction_index, instruction.dest, facts_) &&
         !result_is_next_direct_call_argument(
           block, instruction_index, instruction) &&
         !result_is_immediate_unary_not_branch(block, instruction_index,
                                               instruction.dest)) {
        X64Register result = XR_RSP;
        if(try_allocate_result(instruction.dest, out, &result)) {
          location = reg_operand(result);
          append_move(out, location, reg_operand(XR_RAX));
        } else {
          pressure_home = allocate_temp_home(instruction.dest, instruction.type);
        }
      }
      if(is_integer_or_pointer(instruction.type))
        append_integer_normalization(out, instruction.type, location);
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
      allocate_temp_home(instruction.dest, instruction.type) : MirOperand();
    for(std::size_t i = 0; pressure_result && i < instruction.args.size() && i < 6; ++i) {
      if(instruction.args[i].kind != Operand::OP_TEMP) continue;
      ValueFact & argument = values_.find(instruction.args[i].text)->second;
      if(argument.location.kind != MirOperand::OP_REG ||
         (argument.location.reg != XR_R8 && argument.location.reg != XR_R9)) continue;
      frame_bytes_ = align_up(frame_bytes_, argument.type.alignment);
      frame_bytes_ += abi::frame_storage_size(argument.type);
      const MirOperand home = frame_operand(-static_cast<long long>(frame_bytes_));
      append_store(out, home, argument.location, argument.type);
      if(!has_live_location_alias(instruction.args[i].text, argument.location))
        registers_.release(argument.location.reg);
      set_value_location(instruction.args[i].text, home);
    }
    const bool direct = instruction.first.kind == Operand::OP_GLOBAL;
    if(!direct) {
      MirOperand pointer_cell;
      if(instruction.first.kind == Operand::OP_TEMP) {
        const std::unordered_map<std::string, ValueFact>::const_iterator value =
          values_.find(instruction.first.text);
        if(value != values_.end()) pointer_cell = value->second.pointer_global_cell;
      }
      if(!pointer_cell.text.empty()) {
        MirInstruction load = machine_instruction(MirInstruction::MI_LOAD, machine_type(lowir_model::LTK_PTR));
        append_operand(load, reg_operand(XR_R10));
        append_operand(load, pointer_cell);
        out.push_back(load);
      } else {
        move_value_to_register(out, XR_R10, resolve(instruction.first),
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
      append_operand(call, named_operand(MirOperand::OP_SYMBOL, instruction.first.text));
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
      append_operand(call, reg_operand(XR_R10));
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
       (facts_.uses.find(instruction.dest) != facts_.uses.end() &&
        facts_.uses.find(instruction.dest)->second != 0));
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
          instruction.dest, instruction.type);
        append_store(out, home, reg_operand(XR_RAX), instruction.type);
        define(instruction.dest, instruction.type, home);
      } else {
        if(result_is_next_call_address_argument(block, instruction_index, instruction)) {
          const MirOperand home = allocate_temp_home(instruction.dest, instruction.type);
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
          facts_.direct_branch_call_results.count(instruction.dest) &&
          !source_.blocks.empty() && block.id != source_.blocks.front().id;
        if(!forward_nonentry_branch &&
           !result_is_immediate_return(block, instruction_index, instruction.dest) &&
           !selection::result_is_immediately_stored(
             block, instruction_index, instruction.dest, facts_) &&
           !result_is_next_direct_call_argument(
             block, instruction_index, instruction) &&
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
              fallback_home = allocate_temp_home(instruction.dest, instruction.type);
          }
          if(fallback_home.kind != MirOperand::OP_FRAME) {
            location = reg_operand(result);
            append_move(out, location, reg_operand(XR_RAX));
          }
        }
        if(is_integer_or_pointer(instruction.type))
          append_integer_normalization(out, instruction.type, location);
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
    } else if(!facts_.direct_branch_call_results.count(instruction.first.text))
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
       facts_.first_use[instruction.first.text] == position_ &&
       incoming_parameter_registers_.count(instruction.first.text)) {
      const X64Register incoming =
        incoming_parameter_registers_.find(instruction.first.text)->second;
      if(incoming_parameter_register_is_intact(
           instruction.first.text, incoming))
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
    for(std::unordered_map<std::string, ValueFact>::const_iterator value =
          values_.begin(); value != values_.end(); ++value) {
      const auto uses = facts_.uses.find(value->first);
      if(uses != facts_.uses.end() && uses->second != 0 &&
         !value->second.parameter && value->second.location.kind == MirOperand::OP_REG &&
         value->second.location.reg == reg)
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
    if(instruction.kind == Instruction::IK_CONST) {
      if(wide::is_integer(instruction.type)) {
        const MirOperand destination = allocate_temp_home(instruction.dest, instruction.type);
        wide::append_copy(destination, wide::literal_value(instruction.first.text), out);
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
    else if(instruction.kind == Instruction::IK_BINARY) emit_binary(instruction, out);
    else if(instruction.kind == Instruction::IK_CMP) {
      if(facts_.deferred_branch_comparisons.count(instruction.dest)) return;
      if(wide::is_integer(instruction.type))
        emit_compare_value(instruction, block, instruction_index, out);
      else if(is_floating(instruction.type) &&
         comparison_feeds_branch(block, instruction_index, instruction))
        emit_float_direct_compare_branch(instruction, block.instructions[instruction_index + 1], out);
      else if(is_floating(instruction.type))
        emit_float_compare_value(instruction, block, instruction_index, out);
      else if(comparison_feeds_branch(block, instruction_index, instruction))
        emit_direct_compare_branch(instruction, block.instructions[instruction_index + 1], out);
      else emit_compare_value(instruction, block, instruction_index, out);
    } else if(instruction.kind == Instruction::IK_UNARY) {
      if(unary_not_feeds_branch(block, instruction_index, instruction))
        emit_direct_unary_not_branch(instruction, block.instructions[instruction_index + 1], out);
      else emit_unary_value(instruction, block, instruction_index, out);
    } else if(instruction.kind == Instruction::IK_CONVERT)
      emit_convert(instruction, block, instruction_index, out);
    else if(instruction.kind == Instruction::IK_CALL)
      emit_call(instruction, block, instruction_index, out);
    else if(instruction.kind == Instruction::IK_COPYOBJ ||
              instruction.kind == Instruction::IK_ZEROINIT) {
      emit_bulk(instruction, out);
    } else if(instruction.kind == Instruction::IK_BRANCH) {
      const std::unordered_map<std::string, const Instruction *>::const_iterator deferred =
        facts_.deferred_branch_comparisons.find(instruction.first.text);
      if(deferred == facts_.deferred_branch_comparisons.end()) emit_branch(instruction, out);
      else if(is_floating(deferred->second->type))
        emit_float_direct_compare_branch(*deferred->second, instruction, out, false);
      else emit_direct_compare_branch(*deferred->second, instruction, out, false);
    } else if(instruction.kind == Instruction::IK_SWITCH) emit_switch(instruction, out);
    else if(instruction.kind == Instruction::IK_JUMP) {
      MirInstruction jump = machine_instruction(MirInstruction::MI_JMP);
      append_operand(jump, native_block_operand(source_, instruction.first));
      out.push_back(jump);
    } else if(instruction.kind == Instruction::IK_RETURN) emit_return(instruction, out);
    else if(eh::lower_marker(source_, instruction, out)) return;
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
    const lowir_model::LowirFunction & function,
    const std::unordered_set<std::string> & pointer_globals,
    const std::unordered_map<std::string, std::string> & tls_wrappers,
    const abi::FunctionSignatureIndex & signatures,
    lowir_native::Stats * stats)
{
  return FunctionLowerer(function, pointer_globals, tls_wrappers,
                         signatures, stats).lower();
}

}  // namespace lowir_native
