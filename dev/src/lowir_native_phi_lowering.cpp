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

// Register-homed phi destinations add a hazard frame destinations never had:
// a pending source may read a destination register through a dereference
// base or index, not only by occupying the same location.
bool move_reads_location(const Move & move,
                         const mir_model::MirOperand & location)
{
  if(move.source_is_address) return false;
  if(same_location(location, move.source)) return true;
  return location.kind == mir_model::MirOperand::OP_REG &&
    move.source.kind == mir_model::MirOperand::OP_DEREF &&
    (move.source.reg == location.reg ||
     (move.source.has_index && move.source.index == location.reg));
}

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
        if(i != j && moves[j].pending &&
           move_reads_location(moves[j], moves[i].destination)) {
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
    // Break the cycle at a move whose source reads a pending destination:
    // buffering that source in the scratch slot unblocks the destination it
    // reads.  A frame-destination cycle always reads through OP_FRAME
    // sources; register-homed phis add OP_REG and OP_DEREF readers.
    std::size_t cycle = moves.size();
    for(std::size_t i = 0; i < moves.size() && cycle == moves.size(); ++i) {
      if(!moves[i].pending) continue;
      for(std::size_t j = 0; j < moves.size(); ++j)
        if(i != j && moves[j].pending &&
           move_reads_location(moves[i], moves[j].destination)) {
          cycle = i;
          break;
        }
    }
    if(cycle == moves.size())
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
    // A dereference source is not location-identical to anything, so the
    // chosen move redirects itself explicitly.
    moves[cycle].source = scratch;
    moves[cycle].source_is_address = false;
  }
}

}  // namespace phi_detail
}  // namespace lowir_native
