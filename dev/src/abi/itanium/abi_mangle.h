#pragma once

// Typed ABI records and public PA14 mangling API.

#include "abi/itanium/abi_mangle_facts.h"
#include "abi/itanium/abi_mangle_stats.h"

#include <iosfwd>

namespace abi_mangle {

std::size_t make_semantic_substitution(
  AbiSemanticSubstitutionKind kind, std::size_t identity);

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
  AbiTerminalKind terminal_code = ABI_TERMINAL_NONE;
  AbiStandardSubstitutionKind standard_substitution_code =
    ABI_STANDARD_SUBSTITUTION_TEXT;
  AbiLocalPresentationKind local_presentation = ABI_LOCAL_PRESENTATION_TEXT;
  std::string context_ref;
  // Local/lambda context records use these as context storage and case
  // identity. Name-component records use the same kind-disjoint slots as a
  // resolved prefix path and terminal string ID.
  std::size_t resolved_context = ABI_NO_RESOLVED_REFERENCE;
  std::size_t resolved_context_identity = ABI_NO_RESOLVED_REFERENCE;
  std::string source_name;
  std::string discriminator;
  std::string terminal;
  std::string literal_suffix;
  // A typed local-context record stores its local-name ordinal in this
  // kind-disjoint type payload without enlarging the function record.
  AbiType type;
  std::vector<AbiType> types;
  AbiReferenceList argument_refs;
  std::vector<std::string> namespace_qualifiers;
  std::vector<AbiFunctionQualifier> qualifiers;

  bool has_resolved_name_component() const
  {
    return resolved_context != ABI_NO_RESOLVED_REFERENCE &&
      resolved_context_identity != ABI_NO_RESOLVED_REFERENCE;
  }

  void set_resolved_name_component(std::size_t path, std::size_t name)
  {
    resolved_context = path;
    resolved_context_identity = name;
  }

  std::size_t resolved_name_path() const { return resolved_context; }
  bool has_resolved_source_name() const
  {
    return resolved_context_identity != ABI_NO_RESOLVED_REFERENCE;
  }
  void set_resolved_source_name(std::size_t name)
  {
    resolved_context_identity = name;
  }
  std::size_t resolved_source_name() const
  {
    return resolved_context_identity;
  }
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
  std::size_t store_entity(const AbiEntityFact & entity);
  std::size_t resolve_external_name(std::size_t source,
                                    const std::string & spelling);
  std::size_t resolve_generated_name(const std::string & spelling);
  std::size_t resolve_path(const std::vector<std::size_t> & components);
  std::size_t resolve_path_component(std::size_t parent, std::size_t name);
  bool resolved_type_uses_case_facts(std::size_t type) const;
  bool find_resolved_type(std::size_t source, std::size_t function,
                          std::size_t recipe, std::size_t * result) const;
  std::size_t cache_resolved_type(std::size_t source, std::size_t function,
                                  std::size_t recipe, std::size_t type);

private:
  AbiMangleContext(const AbiMangleContext &);
  AbiMangleContext & operator=(const AbiMangleContext &);
  struct Impl;
  Impl * impl_;
};

AbiFactRecord parse_fact_record_words(const std::vector<std::string> & words);
std::string serialize_fact_file(const AbiFactFile & file);
std::size_t abi_fact_storage_bytes(const AbiFactFile & file);
std::size_t abi_typed_case_storage_bytes(const AbiTypedCase & fact_case);
void mangle_fact_file_to_stream(const AbiFactFile & file, std::ostream & output,
                                AbiMangleStats * stats = nullptr);
std::string mangle_fact_files(const std::vector<std::string> & input_paths);
void mangle_fact_files_to_stream(const std::vector<std::string> & input_paths,
                                 std::ostream & output,
                                 AbiMangleStats * stats = nullptr);


}  // namespace abi_mangle
