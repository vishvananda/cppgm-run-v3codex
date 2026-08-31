#pragma once

#include "lowir/analysis/inline.h"
#include "lowir/model/program.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {

struct DirectGlobalAliases
{
  std::vector<unsigned char> known;
  std::vector<lowir_model::Operand> values;
};

DirectGlobalAliases direct_global_aliases(
    const lowir_model::Function & function);

lowir_model::Operand normalize_direct_global(
    const lowir_model::Operand & operand,
    const DirectGlobalAliases & aliases);

const lowir_model::GlobalDefinition * structured_internal_integer_table(
    const lowir_model::LowirProgram & program,
    const lowir_model::Operand & operand);

bool private_call_table(
    const lowir_model::LowirProgram & program,
    const InlineCallGraph & call_graph,
    lowir_model::SymbolId symbol,
    std::size_t selected_target,
    std::size_t selected_parameter);

bool add_private_table_lower_prefilter(
    lowir_model::Function * function,
    const lowir_model::GlobalDefinition & global,
    const std::vector<unsigned char> & fixed_parameters);

}  // namespace lowir_opt
