#pragma once

// Typed ABI records and public PA14 mangling API.

#include "abi_mangle_facts.h"

#include <iosfwd>

namespace abi_mangle {

struct AbiDefinitionRecord
{
  AbiDefinitionRecord();
  AbiDefinitionRecord(const AbiDefinitionRecord & other);
  AbiDefinitionRecord(AbiDefinitionRecord && other) noexcept;
  AbiDefinitionRecord & operator=(const AbiDefinitionRecord & other);
  AbiDefinitionRecord & operator=(AbiDefinitionRecord && other) noexcept;
  ~AbiDefinitionRecord();

  void set_kind(AbiDefinitionKind new_kind);

  AbiDefinitionKind kind;
  std::string id;
  union
  {
    AbiType type;
    AbiTemplateArgument template_argument;
    AbiDependentExpression expression;
    AbiLocalContext context;
    AbiEntityFact entity;
  };

private:
  void construct_payload();
  void copy_payload(const AbiDefinitionRecord & other);
  void move_payload(AbiDefinitionRecord & other) noexcept;
  void destroy_payload();
};

struct AbiTargetRecord
{
  AbiTargetFactKind kind = ABI_TARGET_FACT_TYPE;
  bool c_linkage = false;
  AbiType type;
  AbiType base_type;
  AbiFunctionTarget function;
  std::string qualified_name;
  unsigned long long base_offset = 0;
  long long this_adjust = 0;
  bool has_result_adjust = false;
  long long result_adjust = 0;
  bool result_adjust_virtual = false;
  long long result_vcall_offset = 0;
  long long vcall_offset = 0;
};

struct AbiFunctionRecord
{
  AbiFunctionRecordKind kind = ABI_FUNCTION_RECORD_PARAMETER;
  std::string name;
  std::string substitution;
  std::string complete_substitution;
  std::string standard_substitution;
  bool standard_substitution_includes_arguments = false;
  std::string context_ref;
  std::string source_name;
  std::string discriminator;
  std::string terminal;
  std::string literal_suffix;
  AbiType type;
  std::vector<AbiType> types;
  std::vector<std::string> argument_refs;
  std::vector<std::string> namespace_qualifiers;
  std::vector<AbiFunctionQualifier> qualifiers;
};

struct AbiFactRecord
{
  AbiFactRecord();
  AbiFactRecord(const AbiFactRecord & other);
  AbiFactRecord(AbiFactRecord && other) noexcept;
  AbiFactRecord & operator=(const AbiFactRecord & other);
  AbiFactRecord & operator=(AbiFactRecord && other) noexcept;
  ~AbiFactRecord();

  void set_kind(AbiFactRecordKind new_kind);

  AbiFactRecordKind kind;
  union
  {
    AbiDefinitionRecord definition;
    AbiTargetRecord target;
    AbiFunctionRecord function;
  };

private:
  void construct_payload();
  void copy_payload(const AbiFactRecord & other);
  void move_payload(AbiFactRecord & other) noexcept;
  void destroy_payload();
};

struct AbiFactCase
{
  std::string label;
  std::vector<AbiFactRecord> records;
};

struct AbiFactFile
{
  std::vector<AbiFactCase> cases;
};

struct AbiMangleStats
{
  std::size_t source_files = 0;
  std::size_t source_bytes = 0;
  std::size_t cases = 0;
  std::size_t records = 0;
  std::size_t canonical_types = 0;
  std::size_t canonical_arguments = 0;
  std::size_t canonical_expressions = 0;
  std::size_t canonical_cache_hits = 0;
  std::size_t definition_cache_hits = 0;
  std::size_t path_components = 0;
  std::size_t substitution_lookups = 0;
  std::size_t substitution_hits = 0;
  std::size_t substitution_entries = 0;
  std::size_t isolated_entity_encodings = 0;
  std::size_t output_bytes = 0;
  std::size_t peak_input_bytes = 0;
  unsigned long long parse_nanoseconds = 0;
  unsigned long long encode_nanoseconds = 0;
};

AbiFactRecord parse_fact_record_words(const std::vector<std::string> & words);
AbiFactFile parse_fact_text(const std::string & text);
std::string serialize_fact_file(const AbiFactFile & file);
std::string mangle_fact_file(const AbiFactFile & file);
void mangle_fact_file_to_stream(const AbiFactFile & file, std::ostream & output,
                                AbiMangleStats * stats = nullptr);
std::string mangle_fact_files(const std::vector<std::string> & input_paths);
void mangle_fact_files_to_stream(const std::vector<std::string> & input_paths,
                                 std::ostream & output,
                                 AbiMangleStats * stats = nullptr);


}  // namespace abi_mangle
