#pragma once

#include "lowir_model.h"
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
  virtual mir_model::MirOperand PhiCycleScratch() = 0;
  virtual void EmitPhiMove(
    const mir_model::MirOperand & destination,
    const mir_model::MirOperand & source,
    const lowir_model::LowType & type,
    std::vector<mir_model::MirInstruction> * out) = 0;
  virtual void ConsumePhiSource(const lowir_model::Operand & operand) = 0;
};

void emit_parallel_transfers(const std::vector<Transfer> & transfers,
                             Emitter * emitter,
                             std::vector<mir_model::MirInstruction> * out);
void plan_transfers(
  const lowir_model::LowirFunction & function, Emitter * emitter,
  std::vector<std::vector<Transfer> > * transfers);

}  // namespace phi_detail
}  // namespace lowir_native
