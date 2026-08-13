#pragma once

#include "lowir_native.h"
#include "lowir_native_mir.h"
#include "lowir_native_registers.h"

#include <cstddef>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lowir_native {
namespace object_elf_detail {

struct EncodedFixup
{
  enum Kind { EF_RELATIVE32, EF_ABSOLUTE64, EF_ADDRESS32 }
    kind = EF_RELATIVE32;
  std::size_t offset = 0;
  std::string target;
  long long addend = 0;
};

struct EncodedSection
{
  std::vector<unsigned char> bytes;
  std::unordered_map<std::string, std::size_t> labels;
  std::vector<EncodedFixup> fixups;
};

struct HostFunctionLayout
{
  struct CallSite
  {
    std::size_t start = 0;
    std::size_t length = 0;
    std::string landing_pad;
  };

  std::string internal_symbol;
  std::string object_symbol;
  std::size_t offset = 0;
  std::size_t size = 0;
  std::vector<X64Register> callee_saved_regs;
  std::map<std::string, std::vector<mir_model::MirHostEhClause> > clauses;
  std::vector<CallSite> call_sites;
  std::size_t lsda_offset = 0;
};

std::string host_symbol_spelling(const std::string & raw);
std::unordered_map<std::string, std::string> declaration_object_symbols(
  const lowir_model::LowirProgram & program);
std::unordered_set<std::string> host_external_global_definitions(
  const lowir_model::LowirProgram & source,
  const mir_model::MirProgram & program);

std::vector<unsigned char> make_linux_relocatable_image(
  const lowir_model::LowirProgram & program,
  const EncodedSection & text,
  const EncodedSection & data,
  std::vector<HostFunctionLayout> & functions,
  const std::vector<unsigned char> & compiler_payload,
  std::size_t & relocation_count);

}  // namespace object_elf_detail
}  // namespace lowir_native
