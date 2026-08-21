#pragma once

#include "lowir_model.h"
#include "lowir_native_registers.h"
#include "mir_model.h"

#include <memory>
#include <string>
#include <vector>

namespace lowir_native {

struct Stats;

namespace location_planning {

class LiveLocationIndex
{
public:
  explicit LiveLocationIndex(Stats * stats);

  void add(lowir_model::ValueId value,
           const mir_model::MirOperand & location);
  void remove(lowir_model::ValueId value,
              const mir_model::MirOperand & location);
  bool has_alias(lowir_model::ValueId value,
                 const mir_model::MirOperand & location,
                 bool value_is_live) const;
  const std::vector<lowir_model::ValueId> &
    gpr_values(X64Register reg) const;

private:
  Stats * stats_;
  std::vector<lowir_model::ValueId> gpr_values_[16];
  std::vector<lowir_model::ValueId> xmm_values_[8];
};

class GeneratedFrameNames
{
public:
  explicit GeneratedFrameNames(const lowir_model::LowirFunction & function);

  lowir_model::PresentationName name(lowir_model::ValueId value);

private:
  const lowir_model::LowirFunction & function_;
  std::unique_ptr<lowir_model::GeneratedNameReservations> reservations_;
};

struct ParallelXmmMove
{
  XmmRegister destination = XMM_0;
  mir_model::MirOperand source;
  lowir_model::LowType type;
  bool pending = true;
};

bool xmm_destination_is_safe(
    const std::vector<ParallelXmmMove> & moves,
    std::size_t candidate);

bool managed_register(X64Register reg);

bool should_retain_edge_register(
    const mir_model::MirOperand & location,
    int optimization_level,
    bool function_has_eh,
    bool loop_carried,
    bool crosses_call,
    bool has_narrow_alias,
    bool crosses_fixed_register_clobber,
    const allocation::RegisterPool & registers,
    const allocation::XmmPool & xmms);

std::string diagnostic_value_name(
    const lowir_model::LowirProgram & program,
    const lowir_model::LowirFunction & function,
    lowir_model::ValueId value);

}  // namespace location_planning
}  // namespace lowir_native
