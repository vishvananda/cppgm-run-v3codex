#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "lowir_model.h"
#include "mir_model.h"

namespace lowir_native {

struct Stats
{
  std::size_t functions = 0;
  std::size_t blocks = 0;
  std::size_t lowir_instructions = 0;
  std::size_t mir_instructions = 0;
  std::size_t eh_region_states = 0;
  std::size_t eh_region_edges = 0;
  std::size_t eh_call_sites = 0;
  std::size_t fixups = 0;
  std::size_t output_bytes = 0;
  std::uint64_t lower_nanoseconds = 0;
  std::uint64_t encode_nanoseconds = 0;
  std::uint64_t write_nanoseconds = 0;
};

struct RelocatableLabel
{
  std::string name;
  std::size_t offset = 0;
};

struct RelocatableRelocation
{
  enum Kind { RELATIVE32, ABSOLUTE64 } kind = RELATIVE32;
  std::size_t offset = 0;
  std::string target;
  long long addend = 0;
};

struct RelocatableSection
{
  std::size_t alignment = 1;
  std::vector<unsigned char> bytes;
  std::vector<RelocatableLabel> labels;
  std::vector<RelocatableRelocation> relocations;
};

struct RelocatableObject
{
  std::vector<RelocatableSection> sections;
};

class ProgramLoweringSession
{
public:
  ProgramLoweringSession(const lowir_model::LowirProgram & program,
                         const std::string & target, Stats * stats = 0);
  ~ProgramLoweringSession();

  std::size_t function_count() const;
  mir_model::MirFunction lower_function(std::size_t index);
  mir_model::MirProgram take_program_shell();

private:
  struct Impl;
  Impl * impl_;

  ProgramLoweringSession(const ProgramLoweringSession &);
  ProgramLoweringSession & operator=(const ProgramLoweringSession &);
};

mir_model::MirProgram lower_program(const lowir_model::LowirProgram & program,
                                    const std::string & target,
                                    Stats * stats = 0);

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
                            Stats * stats = 0);

void write_linux_relocatable(const std::string & path,
                             const lowir_model::LowirProgram & program,
                             const std::string & target,
                             const std::vector<unsigned char> & compiler_payload,
                             Stats * stats = 0);

}  // namespace lowir_native
