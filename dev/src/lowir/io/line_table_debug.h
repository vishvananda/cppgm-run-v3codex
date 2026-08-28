#pragma once

#include "lowir/model/program.h"

#include <string>

namespace lowir_line_table_debug {

// Attach -gline-tables-only debug locations to a freshly lowered program by
// matching emitted function and value names back to their source lines.
void attach_line_table_debug(lowir_model::LowirProgram * program,
	const std::string & path, const std::string & source);

}  // namespace lowir_line_table_debug
