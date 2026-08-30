#pragma once

#include "lowir/model/program.h"
#include "native/mir/construction.h"
#include "native/lowering/selection.h"
#include "native/mir/model.h"

#include <vector>

namespace lowir_native {
namespace phi_detail {

struct Transfer
{
  lowir_model::ValueId destination;
  lowir_model::Operand source;
  lowir_model::LowType type;
};

class Emitter
{
public:
  virtual bool PhiBlockIsCyclic(std::size_t block) = 0;
  virtual void DefinePhi(const lowir_model::Instruction & phi,
                         bool loop_carried, std::size_t block,
                         bool target_is_cyclic,
                         const std::vector<std::size_t> & phi_blocks,
                         const std::vector<std::size_t> & definition_blocks,
                         const std::vector<std::size_t> &
                           block_last_positions) = 0;
  virtual mir_model::MirOperand PhiDestination(
    lowir_model::ValueId value) const = 0;
  virtual mir_model::MirOperand PhiSource(
    const lowir_model::Operand & operand) const = 0;
  virtual bool PhiSourceIsAddress(
    const lowir_model::Operand & operand) const = 0;
  virtual mir_model::MirOperand PhiCycleScratch() = 0;
  virtual void EmitPhiMove(
    const mir_model::MirOperand & destination,
    const mir_model::MirOperand & source,
    const lowir_model::LowType & type,
    bool source_is_address,
    std::vector<mir_model::MirInstruction> * out) = 0;
  virtual void ConsumePhiSource(const lowir_model::Operand & operand) = 0;
};

template <class Derived>
class PhiLowering : public Emitter
{
protected:
  bool planned_phi_source_home(
      lowir_model::ValueId value, mir_model::MirOperand * home) const
  {
    if(!value.valid()) return false;
    const std::uint32_t raw = static_cast<std::uint32_t>(value);
    if(raw >= merge_source_home_known_.size() ||
       !merge_source_home_known_[raw])
      return false;
    *home = merge_source_homes_[raw];
    return true;
  }

  void plan_immediate_call_phi_source_home(
      Derived & lowerer, const lowir_model::Instruction & phi,
      std::size_t block, bool target_is_cyclic,
      const std::vector<std::size_t> & definition_blocks)
  {
    // A one-use incoming temporary may be born in the merge home when the
    // merge is consumed immediately by a call.  Inside a cycle, require the
    // definition to be refreshed in that same component on every transfer.
    if(lowerer.optimization_level_ < 1 || lowerer.facts_.uses[phi.dest] != 1 ||
       block >= lowerer.source_.blocks.size())
      return;
    const std::vector<lowir_model::Instruction> & instructions =
      lowerer.source_.blocks[block].instructions;
    std::size_t consumer = 0;
    while(consumer < instructions.size() &&
          instructions[consumer].kind == lowir_model::Instruction::IK_PHI)
      ++consumer;
    if(consumer >= instructions.size() ||
       instructions[consumer].kind != lowir_model::Instruction::IK_CALL)
      return;
    bool is_argument = false;
    for(std::size_t i = 0; i < instructions[consumer].args.size(); ++i)
      if(instructions[consumer].args[i].kind ==
           lowir_model::Operand::OP_TEMP &&
         instructions[consumer].args[i].value == phi.dest) {
        is_argument = true;
        break;
      }
    if(!is_argument) return;
    const mir_model::MirOperand destination =
      lowerer.selected_value_location(phi.dest);
    if(destination.kind != mir_model::MirOperand::OP_FRAME) return;

    lowir_model::ValueId selected;
    bool selected_is_call = true;
    for(std::size_t incoming = 1;
        incoming < phi.args.size(); incoming += 2) {
      const lowir_model::Operand & operand = phi.args[incoming];
      if(operand.kind != lowir_model::Operand::OP_TEMP) continue;
      const lowir_model::ValueId source = operand.value;
      const std::uint32_t raw = static_cast<std::uint32_t>(source);
      if(raw >= definition_blocks.size() ||
         definition_blocks[raw] >= lowerer.source_.blocks.size() ||
         lowerer.facts_.uses[raw] != 1 || lowerer.value_known_[raw] ||
         !lowir_model::same_lowir_type(
           lowir_model::lowir_value_type(lowerer.source_, source), phi.type) ||
         (target_is_cyclic &&
          !lowerer.control_flow_.BlocksShareCyclicComponent(
            definition_blocks[raw], block)))
        continue;
      bool definition_is_call = false;
      const std::vector<lowir_model::Instruction> & definitions =
        lowerer.source_.blocks[definition_blocks[raw]].instructions;
      for(std::size_t i = 0; i < definitions.size(); ++i)
        if(definitions[i].dest == source) {
          definition_is_call =
            definitions[i].kind == lowir_model::Instruction::IK_CALL;
          break;
        }
      if(!selected.valid() || (selected_is_call && !definition_is_call)) {
        selected = source;
        selected_is_call = definition_is_call;
      }
    }
    if(!selected.valid()) return;
    if(merge_source_homes_.empty()) {
      merge_source_homes_.resize(lowerer.source_.value_names.size());
      merge_source_home_known_.assign(
        lowerer.source_.value_names.size(), 0);
    }
    merge_source_homes_[selected] = destination;
    merge_source_home_known_[selected] = 1;
  }

  bool PhiBlockIsCyclic(std::size_t block) override
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    lowerer.control_flow_.SelectBlock(block);
    return lowerer.control_flow_.CurrentBlockIsCyclic();
  }

  void DefinePhi(const lowir_model::Instruction & phi,
                 bool loop_carried, std::size_t block,
                 bool target_is_cyclic,
                 const std::vector<std::size_t> & phi_blocks,
                 const std::vector<std::size_t> & definition_blocks,
                 const std::vector<std::size_t> &
                   block_last_positions) override
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    const lowir_model::ValueId value = phi.dest;
    const lowir_model::LowType & type = phi.type;
    if(lowerer.try_claim_planned_phi_register(value, type)) return;
    if(lowerer.optimization_level_ >= 1 && !loop_carried)
      for(std::size_t incoming = 1;
          incoming < phi.args.size(); incoming += 2) {
        const lowir_model::Operand & operand = phi.args[incoming];
        if(operand.kind != lowir_model::Operand::OP_TEMP) continue;
        const std::uint32_t source = operand.value;
        const lowir_model::Operand & predecessor = phi.args[incoming - 1];
        const std::uint32_t predecessor_id = predecessor.block;
        const bool source_dies_on_transfer =
          lowerer.facts_.uses[source] == 1 ||
          (lowerer.optimization_level_ >= 3 &&
           predecessor.kind == lowir_model::Operand::OP_LABEL &&
           predecessor_id < block_last_positions.size() &&
           block_last_positions[predecessor_id] !=
             static_cast<std::size_t>(-1) &&
           lowerer.facts_.last_use[source] ==
             block_last_positions[predecessor_id] &&
           !lowerer.facts_.has(
             operand.value,
             analysis::FunctionFacts::VF_LOOP_INVARIANT));
        if(source >= lowerer.merge_phi_frame_home_.size() ||
           !lowerer.merge_phi_frame_home_[source] ||
           !source_dies_on_transfer ||
           !lowerer.value_known_[source] ||
           !lowir_model::same_lowir_type(
             lowerer.values_[source].type, type))
          continue;
        // A static single use can execute repeatedly.  Inside a cycle, the
        // source must be another non-loop-carried phi in this exact SCC, so
        // its home is refreshed before each dynamic use.  A loop invariant
        // would otherwise be destroyed by the target's alternate edge.
        if(target_is_cyclic &&
           (source >= phi_blocks.size() ||
            !lowerer.control_flow_.BlocksShareCyclicComponent(
              phi_blocks[source], block)))
          continue;
        const mir_model::MirOperand home =
          lowerer.selected_value_location(operand.value);
        if(home.kind != mir_model::MirOperand::OP_FRAME) continue;
        const std::uint32_t binding = lowerer.append_frame_binding(
          mir_model::MirFrameBinding::FB_TEMP,
          lowir_model::lowir_value_presentation(lowerer.source_, value),
          type, home.offset);
        lowerer.define(value, type,
          build::frame_operand(home.offset, binding));
        lowerer.merge_phi_frame_home_[value] = 1;
        plan_immediate_call_phi_source_home(
          lowerer, phi, block, target_is_cyclic, definition_blocks);
        return;
      }
    const long long offset = lowerer.allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP,
      lowir_model::lowir_value_presentation(lowerer.source_, value), type);
    lowerer.define(value, type, build::frame_operand(offset,
      static_cast<std::uint32_t>(lowerer.target_.frame_bindings.size())));
    lowerer.merge_phi_frame_home_[value] = loop_carried ? 0 : 1;
    plan_immediate_call_phi_source_home(
      lowerer, phi, block, target_is_cyclic, definition_blocks);
  }

  mir_model::MirOperand PhiDestination(
      lowir_model::ValueId value) const override
  {
    return static_cast<const Derived &>(*this).selected_value_location(value);
  }

  mir_model::MirOperand PhiSource(
      const lowir_model::Operand & operand) const override
  {
    return static_cast<const Derived &>(*this).resolve(operand);
  }

  bool PhiSourceIsAddress(
      const lowir_model::Operand & operand) const override
  {
    return static_cast<const Derived &>(*this).is_frame_address(operand);
  }

  mir_model::MirOperand PhiCycleScratch() override
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(lowerer.phi_cycle_scratch_.kind == mir_model::MirOperand::OP_FRAME)
      return lowerer.phi_cycle_scratch_;
    const lowir_model::LowType & type =
      lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
    const long long offset = lowerer.allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP,
      lowir_model::FPN_PHI_CYCLE_SCRATCH, type);
    lowerer.phi_cycle_scratch_ = build::frame_operand(offset,
      static_cast<std::uint32_t>(lowerer.target_.frame_bindings.size()));
    return lowerer.phi_cycle_scratch_;
  }

  void EmitPhiMove(const mir_model::MirOperand & destination,
                   const mir_model::MirOperand & source,
                   const lowir_model::LowType & type,
                   bool source_is_address,
                   std::vector<mir_model::MirInstruction> * out) override
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    if(destination.kind == mir_model::MirOperand::OP_REG) {
      if(source_is_address)
        lowerer.append_address(*out, destination.reg, source);
      else
        lowerer.move_value_to_register(*out, destination.reg, source, type);
      return;
    }
    if(selection::is_scalar_float(type)) {
      if(source.kind == mir_model::MirOperand::OP_XMM)
        build::append_float_move(*out, destination, source, type);
      else {
        build::append_float_move(
          *out, build::xmm_operand(XMM_7), source, type);
        build::append_float_move(
          *out, destination, build::xmm_operand(XMM_7), type);
      }
      return;
    }
    if(source_is_address) {
      lowerer.append_address(*out, XR_R11, source);
      build::append_store(
        *out, destination, build::reg_operand(XR_R11), type);
      return;
    }
    if(source.kind == mir_model::MirOperand::OP_REG)
      build::append_store(*out, destination, source, type);
    else {
      lowerer.move_value_to_register(*out, XR_R11, source, type);
      build::append_store(
        *out, destination, build::reg_operand(XR_R11), type);
    }
  }

  void ConsumePhiSource(const lowir_model::Operand & operand) override
  {
    static_cast<Derived &>(*this).consume(operand);
  }

  void emit_phi_transfers(lowir_model::BlockId predecessor,
                          std::vector<mir_model::MirInstruction> & out)
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    const std::uint32_t predecessor_id = predecessor;
    if(predecessor_id >= lowerer.phi_transfers_.size()) return;
    emit_parallel_transfers(
      lowerer.phi_transfers_[predecessor_id], this, &out);
  }

private:
  std::vector<mir_model::MirOperand> merge_source_homes_;
  std::vector<unsigned char> merge_source_home_known_;
};

void emit_parallel_transfers(const std::vector<Transfer> & transfers,
                             Emitter * emitter,
                             std::vector<mir_model::MirInstruction> * out);
void plan_transfers(
  const lowir_model::LowirFunction & function, Emitter * emitter,
  std::vector<std::vector<Transfer> > * transfers);

}  // namespace phi_detail
}  // namespace lowir_native
