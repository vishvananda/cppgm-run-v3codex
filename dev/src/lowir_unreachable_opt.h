#pragma once

#include "lowir_model.h"

#include <cstddef>
#include <vector>

namespace lowir_opt {

struct Stats;

// Dense program fact used by the O1 CFG pass.  Symbol spellings are not part
// of the optimization identity.
class UnreachableRoleIndex
{
public:
  explicit UnreachableRoleIndex(const lowir_model::LowirProgram & program);

  bool eliminate_conditional_edges(lowir_model::Function * function,
                                   Stats * stats) const;
  std::size_t symbol_count() const;

private:
  std::vector<unsigned char> symbols_;
};

}  // namespace lowir_opt
