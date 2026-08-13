#include "lowir_native_host_eh.h"

#include <unordered_set>

namespace lowir_native {
namespace host_eh_detail {

bool requires_host_eh_storage(const lowir_model::LowirFunction & function)
{
  for(std::size_t i = 0; i < function.blocks.size(); ++i)
    for(std::size_t j = 0; j < function.blocks[i].instructions.size(); ++j)
      if(function.blocks[i].instructions[j].kind ==
           lowir_model::Instruction::IK_EH_TRY ||
         function.blocks[i].instructions[j].kind ==
           lowir_model::Instruction::IK_EH_CLEANUP)
        return true;
  return false;
}

void collect_host_eh_clauses(mir_model::MirFunction * function)
{
  if(!function->host_eh_enabled) return;
  std::unordered_set<std::string> landing_pads;
  for(std::size_t i = 0; i < function->blocks.size(); ++i)
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const mir_model::MirInstruction & instruction =
        function->blocks[i].instructions[j];
      if(instruction.opcode == mir_model::MirInstruction::MI_EH_PUSH &&
         !instruction.operands.empty())
        landing_pads.insert(instruction.operands[0].text);
    }
  for(std::size_t i = 0; i < function->blocks.size(); ++i) {
    std::vector<mir_model::MirHostEhClause> clauses;
    for(std::size_t j = 0; j < function->blocks[i].instructions.size(); ++j) {
      const mir_model::MirInstruction & instruction =
        function->blocks[i].instructions[j];
      if(instruction.opcode != mir_model::MirInstruction::MI_EH_CATCH) continue;
      mir_model::MirHostEhClause clause;
      clause.kind = mir_model::MirHostEhClause::HC_CATCH;
      clause.selector = instruction.operands[0].imm;
      clause.catch_all = instruction.operands.size() == 1;
      if(!clause.catch_all) clause.type_symbol = instruction.operands[1].text;
      clauses.push_back(clause);
    }
    if(!landing_pads.count(function->blocks[i].label)) continue;
    if(clauses.empty()) {
      mir_model::MirHostEhClause cleanup;
      cleanup.kind = mir_model::MirHostEhClause::HC_CLEANUP;
      clauses.push_back(cleanup);
    }
    function->host_eh_clauses[function->blocks[i].label] = clauses;
  }
}

}  // namespace host_eh_detail
}  // namespace lowir_native
