#pragma once

#include <vector>

namespace lowir_model {
struct Function;
}

namespace lowir_opt {

struct Stats;

bool share_terminal_resume_blocks(lowir_model::Function * function,
                                  Stats * stats = 0);
bool share_exact_cleanup_tails(lowir_model::Function * function,
                               Stats * stats = 0);
// Serialize blocks that cannot return -- throw, resume, and noreturn-call
// paths, and blocks reaching only such paths -- after every ordinary block,
// so the hot path stays dense.  Cold regions have no ordinary successors, so
// definition-before-use order is preserved by construction.
bool sink_cold_blocks(lowir_model::Function * function,
                      const std::vector<unsigned char> & noreturn_symbols,
                      Stats * stats = 0);

}  // namespace lowir_opt
