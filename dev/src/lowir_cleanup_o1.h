#pragma once

namespace lowir_model {
struct Function;
}

namespace lowir_opt {

struct Stats;

bool share_terminal_resume_blocks(lowir_model::Function * function,
                                  Stats * stats = 0);

}  // namespace lowir_opt
