#pragma once

#include "lowir_model.h"
#include "lowir_native_mir.h"
#include "lowir_native_selection.h"
#include "mir_model.h"

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
  virtual void DefinePhi(lowir_model::ValueId value,
                         const lowir_model::LowType & type) = 0;
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
  void DefinePhi(lowir_model::ValueId value,
                 const lowir_model::LowType & type) override
  {
    Derived & lowerer = static_cast<Derived &>(*this);
    const long long offset = lowerer.allocate_frame_binding(
      mir_model::MirFrameBinding::FB_TEMP,
      lowir_model::lowir_value_presentation(lowerer.source_, value), type);
    lowerer.define(value, type, build::frame_operand(offset,
      static_cast<std::uint32_t>(lowerer.target_.frame_bindings.size())));
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
};

void emit_parallel_transfers(const std::vector<Transfer> & transfers,
                             Emitter * emitter,
                             std::vector<mir_model::MirInstruction> * out);
void plan_transfers(
  const lowir_model::LowirFunction & function, Emitter * emitter,
  std::vector<std::vector<Transfer> > * transfers);

}  // namespace phi_detail
}  // namespace lowir_native
