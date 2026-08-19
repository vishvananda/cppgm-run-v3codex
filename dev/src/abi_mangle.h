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
  bool internal_linkage = false;
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
  bool discriminator_after_terminal = false;
  std::string context_ref;
  std::size_t resolved_context = ABI_NO_RESOLVED_REFERENCE;
  std::size_t resolved_context_identity = ABI_NO_RESOLVED_REFERENCE;
  std::string source_name;
  std::string discriminator;
  std::string terminal;
  std::string literal_suffix;
  AbiType type;
  std::vector<AbiType> types;
  AbiReferenceList argument_refs;
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

struct AbiResolvedContextBinding
{
  std::size_t identity = ABI_NO_RESOLVED_REFERENCE;
  std::size_t context = ABI_NO_RESOLVED_REFERENCE;
};

// Compact in-memory case used by integrated compiler clients.  The standalone
// fact-file adapter retains AbiFactRecord because its input is record ordered;
// production does not pay the union size for every record family.
struct AbiTypedCase
{
  std::vector<AbiDefinitionRecord> definitions;
  std::vector<AbiFunctionRecord> functions;
  std::vector<AbiResolvedContextBinding> contexts;
  AbiTargetRecord target;
  bool has_target = false;
};

struct AbiMangleStats
{
	std::size_t production_mangles = 0;
	std::size_t production_fact_bytes = 0;
	std::size_t production_type_definitions = 0;
	std::size_t production_argument_definitions = 0;
	std::size_t production_expression_definitions = 0;
	std::size_t production_context_definitions = 0;
	std::size_t production_entity_definitions = 0;
	std::size_t resolved_type_cache_requests = 0;
	std::size_t resolved_type_cache_hits = 0;
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

class AbiMangleContext
{
public:
  explicit AbiMangleContext(AbiMangleStats * stats = nullptr);
  ~AbiMangleContext();

  std::string mangle_case(const AbiFactCase & fact_case);
  std::string mangle_case(const AbiTypedCase & fact_case);
  std::size_t resolve_type(const AbiType & type);
  std::size_t resolve_argument(const AbiTemplateArgument & argument);
  std::size_t resolve_expression(const AbiDependentExpression & expression);
  std::size_t store_context(const AbiLocalContext & context);
  bool resolved_type_uses_case_facts(std::size_t type) const;
  bool resolved_argument_uses_case_facts(std::size_t argument) const;
  bool resolved_expression_uses_case_facts(std::size_t expression) const;
  bool find_resolved_type(std::size_t source, std::size_t function,
                          std::size_t recipe, std::size_t * result) const;
  std::size_t cache_resolved_type(std::size_t source, std::size_t function,
                                  std::size_t recipe, const AbiType & type);

private:
  AbiMangleContext(const AbiMangleContext &);
  AbiMangleContext & operator=(const AbiMangleContext &);
  struct Impl;
  Impl * impl_;
};

AbiFactRecord parse_fact_record_words(const std::vector<std::string> & words);
AbiFactFile parse_fact_text(const std::string & text);
std::string serialize_fact_file(const AbiFactFile & file);
std::size_t abi_fact_storage_bytes(const AbiFactFile & file);
std::size_t abi_typed_case_storage_bytes(const AbiTypedCase & fact_case);
std::string mangle_fact_file(const AbiFactFile & file);
void mangle_fact_file_to_stream(const AbiFactFile & file, std::ostream & output,
                                AbiMangleStats * stats = nullptr);
std::string mangle_fact_files(const std::vector<std::string> & input_paths);
void mangle_fact_files_to_stream(const std::vector<std::string> & input_paths,
                                 std::ostream & output,
                                 AbiMangleStats * stats = nullptr);


}  // namespace abi_mangle
