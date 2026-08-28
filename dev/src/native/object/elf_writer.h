#pragma once

#include "lowir/model/program.h"
#include "native/mir/model.h"
#include "native/object/relocatable.h"

#include <string>
#include <vector>

namespace lowir_native {

struct Stats;

void write_linux_executable(const std::string & path,
                            const mir_model::MirProgram & program,
                            Stats * stats = 0);
void write_linux_executable(const std::string & path,
                            const mir_model::MirProgram & program,
                            const std::vector<RelocatableObject> & objects,
                            Stats * stats = 0);
void write_linux_executable(const std::string & path,
                            const lowir_model::LowirProgram & program,
                            const std::string & target,
                            const std::vector<RelocatableObject> & objects,
                            int optimization_level,
                            Stats * stats = 0);

void write_linux_relocatable(const std::string & path,
                             const lowir_model::LowirProgram & program,
                             const std::string & target,
                             int optimization_level,
                             Stats * stats = 0);

}  // namespace lowir_native
