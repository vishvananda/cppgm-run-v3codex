#include "lowir_native_phi_lowering.h"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace lowir_native {
namespace phi_detail {
namespace {

bool same_location(const mir_model::MirOperand & left,
                   const mir_model::MirOperand & right)
{
  using mir_model::MirOperand;
  if(left.kind != right.kind) return false;
  if(left.kind == MirOperand::OP_FRAME) return left.offset == right.offset;
  if(left.kind == MirOperand::OP_REG) return left.reg == right.reg;
  if(left.kind == MirOperand::OP_XMM) return left.xmm == right.xmm;
  return false;
}

struct Move
{
  mir_model::MirOperand destination;
  mir_model::MirOperand source;
  lowir_model::Operand source_operand;
  lowir_model::LowType type;
  bool source_is_address;
  bool pending;
};

}  // namespace

void plan_transfers(
    const lowir_model::LowirFunction & function, Emitter * emitter,
    std::vector<std::vector<Transfer> > * transfers)
{
  transfers->clear();
  transfers->resize(function.next_block_id);
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    const lowir_model::LowirBlock & target = function.blocks[block];
    for(std::size_t i = 0; i < target.instructions.size(); ++i) {
      const lowir_model::Instruction & phi = target.instructions[i];
      if(phi.kind != lowir_model::Instruction::IK_PHI) break;
      emitter->DefinePhi(phi.dest, phi.type);
      for(std::size_t incoming = 0;
          incoming + 1 < phi.args.size(); incoming += 2) {
        const std::uint32_t predecessor = phi.args[incoming].block;
        if(predecessor >= transfers->size())
          throw std::logic_error("invalid native phi predecessor");
        (*transfers)[predecessor].push_back(
          Transfer{phi.dest, phi.args[incoming + 1], phi.type});
      }
    }
  }
}

void emit_parallel_transfers(const std::vector<Transfer> & transfers,
                             Emitter * emitter,
                             std::vector<mir_model::MirInstruction> * out)
{
  std::vector<Move> moves;
  moves.reserve(transfers.size());
  for(std::size_t i = 0; i < transfers.size(); ++i) {
    const mir_model::MirOperand destination =
      emitter->PhiDestination(transfers[i].destination);
    const mir_model::MirOperand source = emitter->PhiSource(transfers[i].source);
    const bool source_is_address =
      emitter->PhiSourceIsAddress(transfers[i].source);
    if(!source_is_address && same_location(destination, source)) {
      emitter->ConsumePhiSource(transfers[i].source);
      continue;
    }
    moves.push_back(Move{destination, source, transfers[i].source,
                         transfers[i].type, source_is_address, true});
  }
  std::size_t remaining = moves.size();
  while(remaining) {
    bool progressed = false;
    for(std::size_t i = 0; i < moves.size(); ++i) {
      if(!moves[i].pending) continue;
      bool destination_needed = false;
      for(std::size_t j = 0; j < moves.size(); ++j)
        if(i != j && moves[j].pending && !moves[j].source_is_address &&
           same_location(moves[i].destination, moves[j].source)) {
          destination_needed = true;
          break;
        }
      if(destination_needed) continue;
      emitter->EmitPhiMove(
        moves[i].destination, moves[i].source, moves[i].type,
        moves[i].source_is_address, out);
      emitter->ConsumePhiSource(moves[i].source_operand);
      moves[i].pending = false;
      --remaining;
      progressed = true;
    }
    if(progressed) continue;
    std::size_t cycle = 0;
    while(cycle < moves.size() && !moves[cycle].pending) ++cycle;
    if(cycle == moves.size() ||
       moves[cycle].source.kind != mir_model::MirOperand::OP_FRAME)
      throw std::logic_error("invalid native phi transfer cycle");
    const mir_model::MirOperand saved_source = moves[cycle].source;
    const mir_model::MirOperand scratch = emitter->PhiCycleScratch();
    emitter->EmitPhiMove(
      scratch, saved_source, moves[cycle].type,
      moves[cycle].source_is_address, out);
    for(std::size_t i = 0; i < moves.size(); ++i)
      if(moves[i].pending && !moves[i].source_is_address &&
         same_location(moves[i].source, saved_source))
        moves[i].source = scratch;
  }
}

}  // namespace phi_detail
}  // namespace lowir_native
