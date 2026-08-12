#pragma once

#include "pa10_syntax.h"
#include "pa10_syntax_model.h"
#include "pa11_model.h"
#include "pa12_semantic.h"
#include "pa12_semantic_model.h"
#include "pa12_semantic_tables.h"
#include "pa25_lambda_capture_semantic.h"

#include <deque>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

using namespace pa10_syntax_detail;
using namespace pa11;

struct BracedInitializationContext;
class RetainedTemplateValidator;

struct SemanticGraphStorage
{
	InternedStringTable strings;
	Program program;
	DumpArena dump;
	std::vector<NamespaceObjectAction> namespace_objects;
	std::vector<LocalStaticObjectAction> local_static_objects;
	std::vector<AggregateHelperInfo> aggregate_helpers;
	std::vector<ClassPolymorphismFacts> class_polymorphism;
	std::uint32_t root;

	SemanticGraphStorage()
		: strings(), program(strings), root(kNoDumpEdge) {}
	SemanticGraphView View() const;
};

class SemanticAnalyzer : public SyntaxTreeConsumer
{
public:
	SemanticAnalyzer(SemanticGraphStorage& graph, std::ostream& output,
		SemanticAnalysisStats* stats, bool retain_lowering_facts = false,
		bool render_output = true)
		: arena_(0), output_(output), stats_(stats), strings_(graph.strings),
		  program_(&graph.program),
		  retain_lowering_facts_(retain_lowering_facts),
		  render_output_(render_output),
		  dump_(graph.dump), root_(graph.root),
		  class_polymorphism_(graph.class_polymorphism),
		  function_template_dependent_result_shape_(kNoType),
		  function_template_nondeduced_type_shape_(kNoType),
		  class_template_nondeduced_type_shape_(kNoType),
		  active_function_template_result_pattern_(0),
		  class_template_member_replay_depth_(0),
		  explicit_member_template_replay_depth_(0),
		  class_template_completion_suppressed_depth_(0),
		  namespace_objects_(graph.namespace_objects),
		  local_static_objects_(graph.local_static_objects),
		  aggregate_helpers_(graph.aggregate_helpers),
		  current_language_linkage_(LANGUAGE_LINKAGE_CPP),
		  current_return_type_(kNoType), current_class_context_(kNoEntity),
		  current_function_context_(kNoBinding),
		  braced_initialization_context_(0),
		  current_pack_alignment_(0),
		  loop_depth_(0), switch_depth_(0), exception_handler_depth_(0),
		  unevaluated_depth_(0),
		  decltype_operand_depth_(0),
		  conditionally_evaluated_operand_depth_(0),
		  constant_evaluation_suppressed_depth_(0),
		  resolved_call_demand_suppressed_depth_(0),
		  constant_expression_required_depth_(0),
		  constant_initializer_required_depth_(0),
		  local_constant_initializer_depth_(0),
		  preserve_constant_initializer_recipe_depth_(0),
		  constexpr_evaluation_depth_(0), constexpr_evaluation_steps_(0),
		  next_constexpr_storage_identity_(1),
		  expression_count_(0),
		  associated_generation_(0), candidate_generation_(0),
		  associated_scope_visits_(0), associated_declaration_visits_(0),
		  function_candidate_index_visits_(0),
		  overload_candidates_(0), overload_order_comparisons_(0),
		  conversion_checks_(0), call_conversion_cache_hits_(0),
		  call_conversion_cache_misses_(0), braced_fact_cache_hits_(0),
		  braced_fact_cache_misses_(0), function_signature_lookups_(0),
		  polymorphic_classes_(0), virtual_slots_(0),
		  virtual_signature_lookups_(0), virtual_overrides_(0),
		  virtual_slot_lookups_(0), vtable_demands_(0),
		  access_checks_(0), access_path_visits_(0),
		  access_grant_probes_(0),
		  template_specialization_requests_(0),
		  template_specialization_cache_hits_(0),
		  function_template_default_materializations_(0),
		  function_template_default_request_cache_hits_(0),
		  function_template_default_failure_cache_hits_(0),
		  function_template_exception_specification_requests_(0),
		  function_template_exception_specification_cache_hits_(0),
		  function_template_exception_specification_evaluations_(0),
		  template_partial_candidates_(0),
		  template_partial_order_comparisons_(0),
		  template_partial_shape_materializations_(0),
		  template_partial_shape_cache_hits_(0),
		  template_partial_deduction_visits_(0),
		  function_template_deduction_visits_(0),
		  lambda_closure_requests_(0), lambda_closure_cache_hits_(0),
		  constexpr_call_requests_(0), constexpr_call_cache_hits_(0),
		  constant_conversion_fact_requests_(0),
		  constant_conversion_fact_cache_hits_(0),
		  constexpr_local_index_probes_(0),
		  constexpr_scope_index_probes_(0),
		  constexpr_object_projection_visits_(0),
		  constexpr_step_visits_(0), constexpr_max_depth_(0),
		  constexpr_peak_locals_(0), constexpr_scratch_peak_nodes_(0),
		  demand_worklist_pushes_(0), demanded_function_emissions_(0),
		  default_constructor_emissions_(0),
		  class_layouts_(0), class_layout_member_visits_(0),
		  class_zero_offset_subobject_visits_(0),
		  special_member_fact_lookups_(0),
		  special_member_subobject_visits_(0),
		  zero_offset_subobject_generation_(0),
		  empty_constructor_chain_generation_(0),
		  empty_constructor_chain_requests_(0),
		  empty_constructor_chain_cache_hits_(0),
		  empty_constructor_chain_entity_visits_(0),
		  empty_constructor_chain_dependency_edges_(0),
		  constructor_member_action_visits_(0),
		  constructor_base_action_visits_(0),
		  constructor_delegation_action_visits_(0),
		  destructor_subobject_action_visits_(0),
		  lexical_cleanup_action_visits_(0),
		  unwind_cleanup_scope_visits_(0),
		  unwind_cleanup_action_visits_(0),
		  enclosing_lifetime_queries_(0),
		  initializer_list_lifetime_queries_(0),
		  temporary_dependency_visits_(0),
		  materialized_demand_visits_(0),
		  nonthrowing_action_visits_(0),
		  runtime_initializer_visits_(0),
		  static_constant_initializer_visits_(0),
		  static_constant_dependency_edges_(0),
		  empty_destructor_chain_visits_(0),
		  empty_destructor_chain_cache_hits_(0),
		  anonymous_enum_count_(0), local_type_count_(0),
		  branch_cleanup_scan_epoch_(0) {}

	void Consume(const SyntaxArena& arena, NodeId root);
	InternedStringTable& SharedStrings() { return strings_; }

private:
	friend class RetainedTemplateValidator;
	NodeId FindChild(NodeId node, const char* tag) const;
	bool HasDeclSpecifier(NodeId specifiers, const char* spelling) const;
	NodeId FirstSemanticChild(NodeId node) const;
	std::string PayloadSource(NodeId node) const;
	NamePath ParseNamePath(const std::string& spelling);
	LookupResult LookupPath(ScopeId scope, const NamePath& path,
		LookupKind kind);
	LookupResult LookupStructuredName(NodeId syntax, ScopeId scope,
		LookupKind kind, ScopeId* terminal_owner = 0);
	LookupResult LookupExplicitUnqualifiedTemplateName(
		ScopeId scope, NameId name, LookupKind kind);
	NamePath StructuredNamePath(NodeId syntax);
	LookupResult LookupSpelling(ScopeId scope, const std::string& spelling,
		LookupKind kind);
	ScopeId ResolveScopeSpelling(ScopeId scope, const std::string& spelling);
	ScopeId ResolveOwner(ScopeId scope, const NamePath& name);
	const std::string& ScopePrefix(ScopeId scope);
	NameId ScopePrefixId(ScopeId scope);
	NameId DisplayName(ScopeId owner, NameId name);
	NameId EmissionName(ScopeId owner, NameId name);
	ScopeId NewScope(ScopeId parent, ScopeKind kind, NameId name,
		NameId prefix);
	void InitializeInitializerListLifetimeScope(ScopeId scope, ScopeId parent);
	bool HasInternalLinkageScope(ScopeId scope) const;
	bool IsDeclaration(NodeId node) const;

	void AnalyzeDeclaration(NodeId node, ScopeId scope,
		std::uint32_t output_parent, bool local);
	void AnalyzeStaticAssert(NodeId node, ScopeId scope);
	void AnalyzeNamespace(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeUsing(NodeId node, ScopeId scope,
		std::uint32_t output_parent, bool local,
		AccessKind access = ACCESS_PUBLIC);
	void AnalyzeTemplate(NodeId node, ScopeId scope,
		AccessKind member_access = ACCESS_PUBLIC);
	void RegisterFunctionTemplatePattern(NodeId target, ScopeId scope,
		AccessKind member_access,
		const std::vector<TemplateParameter>& parameters, NodeId specifiers,
		NodeId declarator, bool definition, bool special_member_template,
		TypeId dependent_result_shape,
		bool dependent_exception_specification);
	std::size_t FindPriorFunctionTemplatePattern(
		const FunctionTemplatePattern& pattern, EntityId friend_owner,
		bool qualified_friend, bool definition);
	ScopeId FunctionTemplateExceptionScope(
		const FunctionTemplatePattern& pattern,
		const FunctionInfo& function);
	TypeId DependentFunctionTemplateResultShape();
	void RegisterFunctionTemplateFriend(std::size_t pattern,
		EntityId owner, bool hidden);
	void PublishFunctionTemplateFriendGrants(
		const FunctionTemplatePattern& pattern, BindingId specialization);
	void RecordFunctionTemplateUsing(ScopeId owner, NameId name,
		std::size_t pattern, AccessKind access);
	BindingId MaterializeFunctionTemplateUsing(ScopeId owner, NameId name,
		std::size_t pattern, BindingId specialization);
	bool AnalyzeFriendClassTemplate(NodeId target, ScopeId scope,
		const std::vector<TemplateParameter>& parameters);
	void RegisterClassTemplateFriend(std::size_t pattern, EntityId owner);
	void PublishClassTemplateFriendGrants(
		const ClassTemplatePattern& pattern, EntityId specialization);
	bool AnalyzeExplicitTemplateSpecialization(NodeId target, ScopeId scope,
		AccessKind member_access);
	void ParseTemplateParameters(NodeId list, ScopeId scope,
		std::vector<TemplateParameter>* parameters,
		std::vector<NameId>* names, std::vector<NodeId>* defaults,
		const std::unordered_set<NameId>* enclosing_dependent_names = 0);
	void ParseTemplateParametersWithDependentNames(NodeId list, ScopeId scope,
		std::vector<TemplateParameter>* parameters,
		std::vector<NameId>* names, std::vector<NodeId>* defaults,
		std::unordered_set<NameId>* visible_local_names,
		const std::unordered_set<NameId>* enclosing_dependent_names);
	void AnalyzeExplicitInstantiation(NodeId node, ScopeId scope,
		bool definition);
	bool AnalyzeExplicitFunctionInstantiation(NodeId target, ScopeId scope,
		bool definition);
	bool AnalyzeExplicitVariableInstantiation(NodeId target, ScopeId scope,
		bool definition);
	bool ConstructorSubobjectsAreEmpty(BindingId constructor);
	void ValidateRetainedTemplateDefinition(NodeId target, ScopeId scope,
		const std::vector<TemplateParameter>& parameters);
	void RecordRetainedCallLookup(NodeId callee, ScopeId scope,
		const std::string& spelling, bool adl_eligible);
	void PublishRetainedCallLookup(NodeId callee,
		const std::vector<BindingId>& functions,
		const std::vector<std::size_t>& templates, EntityId naming_class,
		bool adl_eligible);
	void AnalyzeClassTemplate(NodeId declaration, ScopeId scope,
		const std::vector<TemplateParameter>& parameters,
		AccessKind member_access = ACCESS_PUBLIC);
	void RegisterAliasTemplate(NodeId declaration, ScopeId scope,
		AccessKind member_access,
		const std::vector<TemplateParameter>& parameters);
	std::size_t FindAliasTemplateIndex(
		const LookupResult& found, NameId requested) const;
	bool IsUnqualifiedAliasTemplateName(ScopeId scope, const NamePath& path);
	LookupResult LookupStructuredTypeSpecifier(
		NodeId syntax, ScopeId scope, TypeId deferred_type);
	TypeId InstantiateAliasTemplate(std::size_t index,
		const std::vector<TemplateArgument>& arguments);
	bool BuildTemplateTemplateArgument(NodeId syntax, ScopeId scope,
		const TemplateParameter& parameter, TemplateArgument* argument);
	bool BuildTemplateTemplateArgument(NodeId syntax, ScopeId lookup_scope,
		ScopeId parameter_scope, const TemplateParameter& parameter,
		TemplateArgument* argument);
	bool TemplateTemplateParameterMatches(
		const std::vector<TemplateParameter>& parameter,
		const std::vector<TemplateParameter>& argument) const;
	bool TemplateTemplateParameterMatchesAtScope(
		const std::vector<TemplateParameter>& parameter,
		const std::vector<TemplateParameter>& argument, ScopeId scope);
	bool TemplateTemplateParameterMatchesAtScope(
		const std::vector<TemplateParameter>& parameter,
		const std::vector<TemplateParameter>& argument, ScopeId scope,
		std::unordered_set<NameId>* local_names);
	TypeId CreateTemplateTemplateParameterProxy(ScopeId scope,
		const TemplateParameter& parameter, std::size_t ordinal);
	bool RetainVariableTemplate(NodeId declaration, ScopeId scope,
		const std::vector<TemplateParameter>& parameters);
	std::vector<std::size_t> FindVariableTemplates(
		ScopeId scope, const NamePath& path);
	BindingId InstantiateVariableTemplate(NodeId syntax, ScopeId scope);
	bool AnalyzeClassTemplateMember(NodeId declaration, ScopeId scope,
		const std::vector<TemplateParameter>& parameters);
	bool ClassTemplateMemberNamesPrimaryParameters(
		const std::vector<TemplateParameter>& parameters,
		const std::vector<TemplateArgument>& arguments) const;
	void SelectClassTemplateMemberOwner(
		std::size_t pattern, ClassTemplateMemberPattern* member);
	ScopeId TemplateLexicalScope(ScopeId source, ScopeId owner) const;
	bool RouteClassTemplateMemberDefinition(
		const ClassTemplateMemberPattern& definition,
		std::size_t component, ScopeId owner, ScopeId lexical_scope,
		bool demanded);
	bool RetainedClassDeclaresNestedPath(NodeId declaration,
		const std::vector<NameId>& path);
	void AnalyzeSimple(NodeId node, ScopeId scope,
		std::uint32_t output_parent, bool local,
		bool qualified_lexical_scope = false,
		bool demanded_template_storage = false);
	ScopeId ResolveStructuredDeclaratorOwner(
		NodeId declarator, ScopeId scope, bool routed_owner = false);
	void AnalyzeSimpleFunctionDeclaration(NodeId item, NodeId declarator,
		ScopeId syntax_scope, ScopeId declaration_scope,
		std::uint32_t output_parent, const NamePath& declared_path,
		const SpecInfo& spec, DeclaratorInfo parsed);
	bool AnalyzeAmbiguousCallStatement(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	bool AnalyzeAmbiguousRelationalDeclaration(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	bool AnalyzeAmbiguousDirectInitializer(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void PublishVariableDeclarationFacts(BindingId binding,
		ScopeId declaration_scope, NameId name, TypeId type,
		const SpecInfo& spec, bool local);
	TypeId CompleteQualifiedStaticArrayType(
		BindingId prior, TypeId declared) const;
	bool IsStaticConstantDefinition(
		BindingId binding, NodeId initializer) const;
	void InheritVariableRedeclarationFacts(BindingId binding);
	void AnalyzeFunction(NodeId node, ScopeId scope,
		std::uint32_t output_parent,
		bool deferred_member_definition = false);
	void AnalyzeCompound(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeStatement(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeReturnStatement(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeSubstatement(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeRangeFor(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	NameId NextRangeForHiddenName(const char* prefix);
	ExpressionInfo MakeRangeForBindingExpression(BindingId binding);
	BindingId AddRangeForLocal(ScopeId scope, std::uint32_t output_parent,
		NameId name, TypeId type, ExpressionInfo initializer,
		bool array_initializer = false);
	void FinishRangeForLocalInitializer(ScopeId scope,
		std::uint32_t declaration, TypeId type,
		const ExpressionInfo& initializer);
	void FinishRangeForFullExpression(ScopeId scope,
		std::uint32_t owner, const ExpressionInfo& expression);
	ExpressionInfo AnalyzeRangeForUnary(const char* operation,
		const char* display, ExpressionInfo operand, ScopeId scope);
	ExpressionInfo AnalyzeRangeForSubscript(ExpressionInfo range,
		ExpressionInfo index, ScopeId scope);
	ExpressionInfo AnalyzeRangeForMemberCall(ExpressionInfo object,
		ScopeId scope, const LookupResult& found);
	ExpressionInfo AnalyzeRangeForAdlCall(ExpressionInfo object,
		ScopeId scope, NameId name);
	void AddRangeForLoopVariable(NodeId declaration,
		ExpressionInfo initializer, ScopeId scope, std::uint32_t output_parent);
	void AnalyzeCondition(NodeId node, ScopeId scope,
		std::uint32_t output_parent, bool switch_condition);
	ConstexprFlow EvaluateConstexprCompound(NodeId node, ScopeId scope,
		TypeId result_type, ConstexprScalarValue* result,
		bool* result_has_scalar,
		std::uint32_t* result_address, std::uint32_t* result_object,
		std::uint32_t* result_complete_object);
	ConstexprFlow EvaluateConstexprStatement(NodeId node, ScopeId scope,
		TypeId result_type, ConstexprScalarValue* result,
		bool* result_has_scalar,
		std::uint32_t* result_address, std::uint32_t* result_object,
		std::uint32_t* result_complete_object);
	ConstexprFlow EvaluateConstexprReturn(NodeId expression, ScopeId scope,
		TypeId result_type, ConstexprScalarValue* result,
		bool* result_has_scalar,
		std::uint32_t* result_address, std::uint32_t* result_object,
		std::uint32_t* result_complete_object);
	bool EvaluateConstexprCondition(NodeId node, ScopeId scope, bool* value);
	bool EvaluateConstexprDeclaration(NodeId node, ScopeId scope);
	bool ConsumeConstexprStep();
	void PushConstexprBlock();
	void PopConstexprBlock();
	void ReleaseConstexprLocals(std::size_t first);
	void ReleaseConstexprScopeFacts(std::size_t first);
	bool AddConstexprLocalValue(ConstexprLocalValue value,
		std::size_t* local);
	bool AddConstexprLocal(NameId name, NameId pack_name, TypeId type,
		const ConstexprScalarValue& value, std::size_t* local = 0);
	bool AddConstexprLocal(NameId name, NameId pack_name, TypeId type,
		std::uint32_t object, std::size_t* local = 0);
	bool AddConstexprLocal(NameId name, NameId pack_name, TypeId type,
		std::uint32_t object, std::uint32_t complete_object,
		std::size_t* local);
	bool AddConstexprAddressLocal(NameId name, NameId pack_name, TypeId type,
		std::uint32_t address, std::size_t* local = 0);
	bool FindConstexprLocal(NameId name, std::size_t* local) const;
	bool FindConstexprPack(NameId name,
		std::vector<std::size_t>* locals) const;
	bool AddConstexprTypeAlias(NameId name, TypeId type);
	void AddConstexprUsingNamespace(ScopeId name_space);
	bool FindConstexprTypeAlias(NameId name, TypeId* type) const;
	void FindConstexprUsingNamespaces(std::vector<ScopeId>* scopes) const;
	bool TryAnalyzeConstexprLocal(const std::string& spelling, TypeId target,
		ExpressionInfo* result);
	bool AnalyzeConstexprExpression(NodeId node, ScopeId scope, TypeId target,
		ExpressionInfo* result);
	bool AnalyzeConstexprInitializer(NodeId node, ScopeId scope, TypeId target,
		ExpressionInfo* result);
	void ReleaseConstexprScratch(std::size_t nodes, std::size_t edges);
	void RegisterConditionLifetime(ScopeId scope, BindingId object,
		TypeId type, const ExpressionInfo& initializer,
		std::uint32_t condition);

	TypeId AnalyzeClass(NodeId node, ScopeId scope,
		const std::string& hint, bool elaborated,
		const std::string& specialization_name = std::string(),
		ScopeId specialization_owner = kNoScope,
		NameId specialization_identity = 0,
		bool complete_definition = true,
		NameId specialization_lookup_name = 0,
		NameId specialization_emission_name = 0);
	bool CompleteClassDefinition(NodeId node, ScopeId scope, TypeId type,
		EntityId entity, NamedFlavor flavor, ScopeId owner, NameId name,
		NameId lookup_name, ScopeId specialization_owner,
		NameId specialization_identity, NameId emission_name);
	bool CollectClassDirectBases(NodeId clause, ScopeId scope,
		EntityId entity, NamedFlavor flavor,
		std::vector<DirectBaseEdge>* direct_bases);
	TypeId AnalyzeEnum(NodeId node, ScopeId scope,
		const std::string& hint, bool elaborated);
	SpecInfo BuildSpecifiers(NodeId node, ScopeId scope,
		const std::string& hint, bool has_declarators,
		bool type_id_context = false,
		TypeId deferred_type = kNoType);
	SpecInfo BuildIdentityOnlySpecifiers(NodeId node, ScopeId scope,
		const std::string& hint, bool has_declarators);
	TypeId BuildTypeId(NodeId node, ScopeId scope);
	TypeId BuildIdentityOnlyTypeId(NodeId node, ScopeId scope);
	DeclaratorInfo BuildDeclarator(NodeId node, TypeId base, ScopeId scope,
		bool placeholder_auto = false,
		bool member_implicit_object = false,
		bool defer_trailing_return = false,
		const std::unordered_set<NameId>* template_parameter_names = 0);
	void ApplyPlaceholderDeclaratorOperator(
		const std::string& operation, DeclaratorInfo* declarator) const;
	DeclaratorInfo BuildVariableDeclarator(NodeId item, NodeId declarator,
		const SpecInfo& spec, ScopeId scope, bool local,
		ExpressionInfo* prepared_initializer);
	DeclaratorInfo BuildMemberDeclarator(NodeId item, NodeId declarator,
		const SpecInfo& spec, ScopeId scope, bool definition,
		ExpressionInfo* prepared_initializer);
	void ConfigurePlaceholderFunctionReturn(BindingId function,
		const DeclaratorInfo& declarator, std::uint8_t placeholder_cv);
	TypeId DeducePlaceholderFunctionReturnType(
		const FunctionInfo& function, const ExpressionInfo* expression);
	void PublishPlaceholderFunctionReturn(
		BindingId function, const ExpressionInfo* expression);
	void CompletePlaceholderFunctionReturn(BindingId function);
	void AnalyzeRetainedPlaceholderFunctionBody(BindingId function);
	TypeId BuildArrayDeclaratorType(NodeId suffix, TypeId element,
		ScopeId scope,
		const std::unordered_set<NameId>* template_parameter_names);
	std::vector<ParameterInfo> BuildParameters(NodeId node, ScopeId scope,
		bool* variadic,
		const std::unordered_set<NameId>* template_parameter_names = 0);
	bool SyntaxUsesAnyTemplateParameter(NodeId node,
		const std::unordered_set<NameId>& names) const;
	bool SyntaxUsesUnqualifiedValueName(NodeId node,
		const std::unordered_set<NameId>& names) const;
	bool FunctionTemplateResultUsesDependentParameter(NodeId declarator,
		NodeId result, const std::unordered_set<NameId>& template_names);
	bool IsDirectTemplateParameterExpression(NodeId node,
		const std::unordered_set<NameId>& names) const;
	LookupResult ResolveClassDirectBase(NodeId base_name, ScopeId scope);
	bool HasDependentQualifiedType(NodeId node,
		const std::unordered_set<NameId>& names, ScopeId scope,
		std::size_t alias_depth = 0);
	void ValidateDeferredFunctionTemplateResult(NodeId node, ScopeId scope,
		FunctionTemplatePattern* pattern,
		const std::unordered_set<NameId>& dependent_names);
	void ValidateFunctionTemplatePatternResults(
		FunctionTemplatePattern* pattern,
		const DeclaratorInfo& declarator, ScopeId shape_scope,
		const std::unordered_set<NameId>& parameter_names,
		bool defer_trailing_return);
	bool FindFunctionTemplateResultLookup(NodeId syntax,
		LookupResult* result) const;
	void InheritFunctionTemplateResultLookups(
		const FunctionTemplatePattern& source,
		FunctionTemplatePattern* destination);
	void AdoptFunctionTemplateDefinition(std::size_t pattern,
		FunctionTemplatePattern* retained,
		FunctionTemplatePattern* incoming,
		bool explicit_member_definition);
	void CopyRetainedCallLookup(NodeId source, NodeId destination);
	TypeId FunctionTemplateNondeducedTypeShape();
	NameId DeclaratorName(NodeId node);
	NamePath DeclaratorNamePath(NodeId node);
	NodeId DeclaratorNameStructure(NodeId node) const;
	TypeId AdjustParameterType(TypeId type);
	TypeId ParameterBindingType(const ParameterInfo& parameter) const;
	TypeId DecltypeType(NodeId node, ScopeId scope);

	BindingId DeclareFunction(ScopeId owner, NameId name, TypeId type,
		const std::vector<ParameterInfo>& parameters, bool definition,
		bool template_specialization = false,
		StorageClass storage_class = STORAGE_CLASS_NONE,
		LanguageLinkage language_linkage = LANGUAGE_LINKAGE_CPP,
		bool nonthrowing = false, bool ordinary_visible = true);
	void MergeFunctionRedeclarationParameters(FunctionInfo* function,
		const std::vector<ParameterInfo>& parameters, bool definition);
	void AnalyzeFriendFunction(NodeId node, ScopeId class_scope,
		TypeId owner_type, const SpecInfo& spec);
	void AnalyzeFriendClass(NodeId node, ScopeId class_scope,
		TypeId owner_type);
	void ValidateNonmemberOperator(BindingId binding) const;
	void ValidateFunctionRefQualifier(BindingId binding);
	bool RefQualifierViable(const ExpressionInfo& object,
		const TypeRecord& function_type) const;
	int CompareImplicitObjectBindings(ValueCategory category,
		const TypeRecord& left, const TypeRecord& right) const;
	ConversionRank MemberCandidateSelectionRank(
		const ExpressionInfo& object, BindingId candidate,
		ConversionRank actual, std::size_t* base_distance) const;
	int CompareReferenceBindings(const ExpressionInfo& argument,
		TypeId left, TypeId right) const;
	std::vector<BindingId> FunctionCandidates(ScopeId scope,
		const std::string& spelling, EntityId* naming_class = 0,
		NodeId syntax = kNoNode,
		bool exclude_template_specializations = false);
	std::vector<BindingId> UsingFunctionCandidates(ScopeId scope,
		const NamePath& path, const std::string& spelling,
		ScopeId* target_owner, bool* names_owner_alias,
		NodeId syntax = kNoNode);
	std::vector<BindingId> FunctionCallCandidates(ScopeId scope,
		const std::string& spelling, EntityId* naming_class = 0,
		NodeId syntax = kNoNode,
		bool exclude_template_specializations = false);
	std::vector<BindingId> RetainedFunctionCallCandidates(NodeId callee,
		ScopeId scope, const std::string& spelling, EntityId* naming_class,
		bool* retained_lookup);
	void CompleteFunctionCallTemplateCandidates(NodeId callee, ScopeId scope,
		const std::string& spelling,
		const std::vector<NodeId>& argument_syntax,
		const std::vector<ExpressionInfo>& arguments, bool retained_lookup,
		std::vector<BindingId>* candidates, EntityId* naming_class);
	bool RetainedCallAllowsArgumentDependentLookup(NodeId callee) const;
	std::vector<BindingId> FunctionSet(BindingId binding,
		bool exclude_template_specializations = false);
	void AppendFunctionSet(BindingId binding,
		std::vector<BindingId>* result,
		bool exclude_template_specializations = false);
	std::vector<std::size_t> FindFunctionTemplates(ScopeId scope,
		const std::string& spelling);
	std::vector<std::size_t> FindFunctionTemplates(ScopeId scope,
		const NamePath& path);
	std::vector<std::size_t> FindStructuredFunctionTemplates(
		NodeId syntax, ScopeId scope);
	std::vector<ScopeId> FindFunctionTemplateOwners(ScopeId scope,
		const std::string& spelling);
	std::vector<ScopeId> FindFunctionTemplateOwners(ScopeId scope,
		const NamePath& path);
	std::vector<BindingId> FunctionTemplateTargetCandidates(
		ScopeId scope, const std::string& spelling, TypeId target,
		NodeId syntax = kNoNode);
	bool HasUniqueFunctionAddressTarget(
		ScopeId scope, NodeId syntax, TypeId target);
	bool AnalyzeFunctionId(NodeId node, ScopeId scope, TypeId target,
		ExpressionInfo* result);
	bool ParseExplicitTemplateArguments(NodeId syntax, ScopeId scope,
		NamePath* base, std::vector<TypeId>* arguments);
	bool CollectExplicitTemplateArguments(NodeId syntax, NamePath* base,
		std::vector<NodeId>* arguments);
	bool BuildTemplateArguments(const std::vector<TemplateParameter>& parameters,
		const std::vector<NodeId>& syntax, ScopeId use_scope,
		ScopeId lexical_scope, std::vector<TemplateArgument>* arguments,
		bool require_complete = true,
		const std::unordered_set<NameId>* dependent_names = 0);
	bool AppendTemplateArgument(
		const std::vector<TemplateParameter>& parameters, NodeId source,
		ScopeId source_scope, ScopeId parameter_scope,
		const std::unordered_set<NameId>* source_dependent_names,
		bool has_pack, std::size_t fixed,
		std::vector<TemplateArgument>* arguments);
	bool IsNonTypeTemplateParameterType(TypeId type) const;
	bool FormNonTypeTemplateArgumentValue(ExpressionInfo expression,
		TemplateArgument* argument);
	bool CandidateSubstitutionActive() const;
	bool CandidateSubstitutionFailed() const;
	void RecordCandidateSubstitutionFailure();
	TypeId CandidateTypeFormation(TypeId type, const char* message);
	ExpressionInfo CandidateSubstitutionFailure();
	BindingId CandidateOverloadFailure(const char* message);
	ExpressionInfo CandidateExpressionFailure(const char* message);
	TypeId BuildCanonicalTemplateTypeArgument(NodeId type_id,
		ScopeId source_scope,
		const std::unordered_set<NameId>* dependent_names);
	TypeId ClassTemplateNondeducedTypeShape();
	TypeId ResolveTemplateParameterType(const TemplateParameter& parameter,
		ScopeId parameter_scope);
	void BindTemplateArgument(ScopeId scope,
		const TemplateParameter& parameter, const TemplateArgument& argument);
	void BindTemplateArgumentPack(ScopeId scope,
		const TemplateParameter& parameter,
		const std::vector<TemplateArgument>& arguments, std::size_t first,
		std::size_t last);
	bool LookupTemplateArgumentPack(ScopeId scope, NameId name,
		std::vector<TemplateArgument>* arguments) const;
	bool LookupFunctionParameterPack(ScopeId scope, NameId name,
		std::vector<BindingId>* bindings) const;
	ExpressionInfo AnalyzeSizeofPackExpression(NodeId node, ScopeId scope);
	bool ExpandCallArgumentPacks(const std::vector<NodeId>& original,
		ScopeId scope, std::vector<NodeId>* syntax,
		std::vector<ExpressionInfo>* arguments);
	void CollectPackExpansionNames(NodeId node, ScopeId scope,
		std::vector<NameId>* names) const;
	void CollectPackExpansionNamesImpl(NodeId node, ScopeId scope,
		std::vector<NameId>* names, bool root) const;
	bool ExpandPackElementScopes(NodeId pattern, ScopeId scope,
		std::vector<ScopeId>* element_scopes);
	void BindLexicalTypeNames(NodeId pattern, ScopeId lexical_owner,
		ScopeId target_scope);
	void ExpandExpressionPack(NodeId expansion, ScopeId scope,
		std::vector<NodeId>* syntax,
		std::vector<ExpressionInfo>* expressions);
	bool TryAnalyzeExpandedBracedInit(NodeId node, ScopeId scope,
		TypeId target, ExpressionInfo* result);
	void InitializeFunctionTemplatePackShape(FunctionTemplatePattern* pattern,
		const DeclaratorInfo& shape);
	void BindFunctionParameterPackElement(ScopeId scope, NameId pack,
		BindingId binding);
	NameId FunctionParameterPackName(NodeId declarator);
	std::vector<TemplateArgument> TypeTemplateArguments(
		const std::vector<TypeId>& arguments) const;
	std::vector<TemplateArgument> StoredTemplateArguments(
		std::size_t first, std::size_t count) const;
	TemplateArgument StoredTemplateArgument(std::size_t index) const;
	TemplateSpecializationKey CanonicalTemplateSpecializationKey(
		std::size_t pattern,
		const std::vector<TemplateArgument>& arguments);
	TemplateSpecializationKey CanonicalTemplateSpecializationKey(
		std::size_t pattern,
		const std::vector<TemplateArgument>& arguments,
		const std::vector<std::uint32_t>& parameter_offsets);
	void StoreTemplateArguments(const std::vector<TemplateArgument>& arguments,
		TemplateArgumentListId* identity,
		std::uint32_t* first, std::uint32_t* count);
	TypeId ResolveStructuredTypeName(NodeId name, ScopeId scope);
	std::size_t FindClassTemplate(ScopeId scope,
		const std::string& spelling);
	std::size_t FindClassTemplate(ScopeId scope, const NamePath& path);
	std::size_t FindClassTemplateIndex(const LookupResult& found,
		NameId requested) const;
	BindingId InstantiateClassTemplate(std::size_t pattern,
		const std::vector<TypeId>& arguments);
	BindingId InstantiateClassTemplate(std::size_t pattern,
		const std::vector<TemplateArgument>& arguments);
	bool IsInitializerListType(TypeId type,
		TypeId* element_type = 0) const;
	bool IsInitializerListFunction(TypeId type) const;
	bool IsStandardInitializerListTemplate(NameId name, ScopeId owner,
		const std::vector<TemplateParameter>& parameters) const;
	void ConfigureInitializerListSpecialization(TypeId type);
	void ConfigureInitializerListBackingLifetime(
		std::uint32_t backing, TypeId element_type);
	std::uint32_t PrepareNamespaceInitializerListLifetime(TypeId type,
		std::uint32_t initializer, std::uint32_t destructor,
		std::uint32_t* backing);
	ExpressionInfo AnalyzeInitializerList(
		NodeId list, ScopeId scope, TypeId type);
	ExpressionInfo BuildInitializerListFromValues(TypeId type,
		const std::vector<ExpressionInfo>& values);
	bool DeduceInitializerListElementType(
		NodeId list, ScopeId scope, TypeId* element_type);
	BindingId ReuseClassTemplateSpecialization(
		std::size_t pattern, BindingId specialization);
	std::size_t SelectClassTemplatePartial(ClassTemplatePattern& pattern,
		const std::vector<TemplateArgument>& arguments,
		FunctionTemplateDeduction* bindings);
	bool MaterializeTemplatePartialArguments(
		const std::vector<TemplateParameter>& primary_parameters,
		const std::vector<TemplateParameter>& partial_parameters,
		const std::vector<NodeId>& syntax, ScopeId lexical_scope,
		std::vector<TemplateArgument>* arguments, std::uint8_t* state);
	bool EquivalentNondeducedTypeArgumentShape(NodeId left,
		const std::vector<TemplateParameter>& left_parameters, NodeId right,
		const std::vector<TemplateParameter>& right_parameters);
	bool MatchTemplatePartialArguments(
		const std::vector<TemplateParameter>& parameters,
		const std::vector<TemplateArgument>& pattern_arguments,
		const std::vector<TemplateArgument>& arguments,
		FunctionTemplateDeduction* bindings) const;
	bool DeduceTemplatePartialArgument(const TemplateArgument& pattern,
		const TemplateArgument& argument,
		const std::vector<TemplateParameter>& parameters,
		FunctionTemplateDeduction* deduced) const;
	bool DeduceTemplatePartialType(TypeId pattern, TypeId argument,
		const std::vector<TemplateParameter>& parameters,
		FunctionTemplateDeduction* deduced) const;
	std::size_t TemplatePartialPackParameter(TypeId type,
		const std::vector<TemplateParameter>& parameters,
		std::size_t depth = 0) const;
	std::size_t TemplatePartialMemberPointerPackParameter(
		const TypeRecord& type, const std::vector<TemplateParameter>& parameters,
		std::size_t depth) const;
	bool DeduceTemplatePartialMemberPointerType(const TypeRecord& pattern,
		const TypeRecord& argument,
		const std::vector<TemplateParameter>& parameters,
		FunctionTemplateDeduction* deduced) const;
	int CompareTemplatePartialPatterns(
		const std::vector<TemplateParameter>& left_parameters,
		const std::vector<TemplateArgument>& left_arguments,
		const std::vector<TemplateParameter>& right_parameters,
		const std::vector<TemplateArgument>& right_arguments) const;
	void CompleteClassTemplateSpecialization(std::size_t pattern,
		BindingId specialization,
		const std::vector<TemplateArgument>& arguments);
	void EnsureClassDefinition(TypeId type);
	bool ClassTemplateSpecializationArgumentsComplete(EntityId entity) const;
	bool IsClassTemplateSpecializationEntity(EntityId entity) const;
	bool ClassTemplateHasNonTypeParameter(EntityId entity) const;
	bool IsClassTemplateSpecializationContext(EntityId entity) const;
	ScopeId BindClassTemplateArguments(const ClassTemplatePattern& pattern,
		const std::vector<TemplateArgument>& arguments);
	void UpgradeClassTemplateSpecializations(std::size_t pattern);
	void ResetClassTemplateSpecializationDefinition(BindingId specialization);
	void ApplyClassTemplateMemberDefinitions(std::size_t pattern,
		BindingId specialization,
		const std::vector<TemplateArgument>& arguments, bool demanded = false);
	void DemandClassTemplateMemberDefinitions(EntityId entity);
	void MarkClassTemplateSpecializationUse(EntityId entity);
	void QueueClassTemplateMemberDefinitions(std::size_t pattern,
		BindingId specialization);
	void ApplyDemandedClassTemplateMemberDefinitions(BindingId specialization);
	BindingId InstantiateFunctionTemplate(std::size_t pattern,
		const std::vector<TypeId>& arguments);
	BindingId InstantiateFunctionTemplate(std::size_t pattern,
		const std::vector<TemplateArgument>& arguments);
	BindingId InstantiateFunctionTemplate(std::size_t pattern,
		const std::vector<TemplateArgument>& arguments,
		const std::vector<std::uint32_t>& parameter_offsets);
	bool MaterializeFunctionTemplateDefaults(
		const FunctionTemplatePattern& pattern,
		const std::vector<TemplateArgument>& arguments,
		const std::vector<std::uint32_t>& parameter_offsets,
		std::vector<TemplateArgument>* completed);
	ScopeId BindFunctionTemplateArguments(
		const FunctionTemplatePattern& pattern,
		const std::vector<TemplateArgument>& arguments,
		const std::vector<std::uint32_t>& parameter_offsets);
	DeclaratorInfo BuildFunctionTemplateSpecializationDeclarator(
		const FunctionTemplatePattern& pattern, ScopeId template_scope,
		SpecInfo* spec, EntityId* member_owner);
	bool EquivalentExpandedFunctionTemplateResults(
		const FunctionTemplatePattern& left,
		const FunctionTemplatePattern& right);
	void InternExpandedFunctionTemplateResult(
		FunctionTemplatePattern* pattern);
	void PublishFunctionTemplateSpecialMemberRole(
		const FunctionTemplatePattern& pattern, BindingId binding,
		EntityId member_owner, TypeId function_type);
	bool BuildFunctionTemplateArgumentOffsets(
		const std::vector<TemplateParameter>& parameters,
		std::size_t argument_count,
		std::vector<std::uint32_t>* offsets) const;
	bool BuildExplicitFunctionTemplateArguments(
		const FunctionTemplatePattern& pattern,
		const std::vector<NodeId>& syntax, ScopeId use_scope,
		std::vector<TemplateArgument>* arguments,
		std::vector<std::uint32_t>* parameter_offsets);
	void UpgradeFunctionTemplateSpecializations(std::size_t pattern);
	bool FunctionTemplateTypeIsDependent(TypeId type) const;
	bool FunctionTemplateTypeUsesUnspecifiedParameter(TypeId type,
		const std::vector<TemplateParameter>& parameters,
		const std::vector<std::uint8_t>& explicitly_specified) const;
	std::size_t FunctionTemplateShapePackParameter(TypeId type,
		const std::vector<TemplateParameter>& parameters) const;
	bool FunctionTemplatePatternAccepts(TypeId pattern,
		TypeId exemplar,
		const std::vector<TemplateParameter>& pattern_parameters,
		const std::vector<TemplateParameter>& exemplar_parameters) const;
	bool FunctionTemplateArgumentPatternAccepts(
		const TemplateArgument& pattern, const TemplateArgument& exemplar,
		const std::vector<TemplateParameter>& pattern_parameters,
		const std::vector<TemplateParameter>& exemplar_parameters) const;
	bool FunctionTemplateParameterListAccepts(
		const FunctionTemplatePattern& pattern,
		const FunctionTemplatePattern& exemplar) const;
	bool DeduceFunctionTemplateType(TypeId pattern, TypeId argument,
		std::vector<TypeId>* deduced) const;
	bool DeduceFunctionTemplatePackType(TypeId pattern, TypeId argument,
		const std::vector<TemplateParameter>& parameters,
		FunctionTemplateDeduction* deduced) const;
	bool DeduceFunctionTemplatePackArgument(
		const TemplateArgument& pattern, const TemplateArgument& argument,
		const std::vector<TemplateParameter>& parameters,
		FunctionTemplateDeduction* deduced) const;
	bool DeduceFunctionTemplateOverloadArgument(TypeId parameter,
		NodeId syntax, ScopeId scope,
		const std::vector<TemplateParameter>& parameters,
		FunctionTemplateDeduction* deduced);
	std::size_t RequiredFunctionParameterCount(
		const std::vector<ParameterInfo>& parameters) const;
	int CompareFunctionTemplateConstraints(
		const FunctionInfo& left, const FunctionInfo& right) const;
	void DeduceFunctionTemplatePatterns(
		const std::vector<std::size_t>& patterns,
		const std::vector<ExpressionInfo>& arguments,
		std::vector<BindingId>* specializations = 0,
		const std::vector<TypeId>* explicit_arguments = 0,
		const std::vector<TemplateArgument>* canonical_explicit_arguments = 0,
		ScopeId argument_scope = kNoScope,
		const std::vector<NodeId>* argument_syntax = 0);
	void DeduceFunctionTemplatePatternsWithExplicitSyntax(
		const std::vector<std::size_t>& patterns,
		const std::vector<ExpressionInfo>& arguments,
		const std::vector<NodeId>& explicit_syntax, ScopeId use_scope,
		std::vector<BindingId>* specializations,
		const std::vector<NodeId>* argument_syntax = 0);
	void DeduceFunctionTemplates(ScopeId scope, const std::string& spelling,
		const std::vector<ExpressionInfo>& arguments,
		NodeId syntax = kNoNode);
	void DemandFunction(BindingId binding);
	void DemandRuntimeFunction(BindingId binding);
	void EnsureFunctionExceptionSpecification(BindingId binding);
	bool FunctionIsNonthrowing(BindingId binding);
	void DemandDefaultConstructor(EntityId entity);
	void DemandConstructorDefinition(BindingId binding);
	void DemandMaterializedConstructorActions(std::uint32_t node,
		bool demand_calls = false);
	void DemandRetainedRuntimeCalls(std::uint32_t node);
	void DemandConditionallyEvaluatedConstructors(std::uint32_t node);
	bool ShouldDemandResolvedCall(BindingId binding, bool folded,
		bool compile_time_only) const;
	void PublishInlineFunctionFacts(BindingId binding, bool inline_specifier);
	TypeId AdaptMemberFunctionType(BindingId binding);
	void EmitDefaultConstructor(EntityId entity);
	void EmitDemandedFunction(BindingId binding);
	const FunctionInfo& GetFunction(BindingId binding) const;
	FunctionInfo& GetMutableFunction(BindingId binding);

	ExpressionInfo AnalyzeExpression(NodeId node, ScopeId scope,
		TypeId target = kNoType);
	ExpressionInfo AnalyzeLambdaExpression(NodeId node, ScopeId scope,
		TypeId target);
	void InstallLambdaCaptureBindings(ScopeId scope, BindingId this_binding,
		const FunctionInfo& function);
	ExpressionInfo BuildLambdaInvocationPointer(BindingId conversion_function,
		TypeId target);
	bool IsCapturelessLambdaType(TypeId type) const;
	std::vector<ExpressionInfo> LambdaConstructorDeductionArguments(
		const std::vector<ExpressionInfo>& arguments);
	ExpressionInfo AnalyzeNamedValue(const std::string& spelling,
		ScopeId scope, TypeId target = kNoType, NodeId syntax = kNoNode);
	BindingId SelectOverload(ScopeId scope,
		const std::vector<NodeId>& argument_syntax,
		const std::vector<ExpressionInfo>& arguments,
		const std::vector<BindingId>& candidates,
		const ExpressionInfo* object = 0,
		ObjectConversionFact* object_conversion = 0,
		std::vector<CallConversionFact>* argument_conversions = 0);
	ExpressionInfo BuildResolvedCall(BindingId selected, ScopeId scope,
		const std::vector<NodeId>& argument_syntax,
		const std::vector<ExpressionInfo>& arguments,
		const ExpressionInfo* object, TypeId target,
		EntityId naming_class = kNoEntity,
		const ObjectConversionFact* object_conversion = 0,
		const std::vector<CallConversionFact>* argument_conversions = 0,
		bool suppress_virtual_dispatch = false);
	void PublishCallImplicitObject(
		std::uint32_t call, std::uint32_t object);
	CallConversionFact CallConversion(const ExpressionInfo& source,
		TypeId target, CallConversionTable* cache, std::size_t source_ordinal);
	int CompareCallConversions(const CallConversionFact& left,
		const CallConversionFact& right) const;
	CallConversionFact ConvertingConstructor(const ExpressionInfo& source,
		TypeId target);
	CallConversionFact ConvertingFunction(const ExpressionInfo& source,
		TypeId target, bool allow_explicit = false);
	void AppendConversionFunctions(EntityId entity,
		std::vector<BindingId>* candidates) const;
	void AppendConversionFunctionTemplateCandidates(EntityId entity,
		TypeId target, std::vector<BindingId>* candidates);
	void AppendBuiltinConversionTargets(const ExpressionInfo& source,
		std::vector<TypeId>* targets) const;
	bool BuiltinBinaryParameterTypes(const std::string& operation,
		const ExpressionInfo& left, TypeId left_type,
		const ExpressionInfo& right, TypeId right_type,
		TypeId* left_target, TypeId* right_target);
	bool ApplyBuiltinUnaryConversion(const std::string& operation,
		ExpressionInfo* operand);
	bool ApplyBuiltinBinaryConversions(const std::string& operation,
		ExpressionInfo* left, ExpressionInfo* right,
		std::vector<ConversionRank>* selected_ranks = 0,
		bool apply = true);
	bool ApplyBuiltinAssignmentConversion(const std::string& operation,
		const ExpressionInfo& left, ExpressionInfo* right);
	ExpressionInfo ApplyCallArgument(ExpressionInfo value, TypeId target,
		const CallConversionFact* conversion = 0);
	ExpressionInfo PrepareConversionFunctionObject(
		ExpressionInfo value, BindingId conversion_function);
	ExpressionInfo ApplyExplicitConversion(ExpressionInfo value, TypeId target);
	ExpressionInfo ApplyContextualBool(ExpressionInfo value);
	ExpressionInfo BuildConvertingArgument(const ExpressionInfo& source,
		TypeId target, const CallConversionFact& conversion);
	bool IsDirectTrivialClassValueType(TypeId type) const;
	ExpressionInfo BuildDirectClassValueTransfer(
		const ExpressionInfo& source, TypeId target,
		BindingId selected_constructor = kNoBinding);
	ExpressionInfo AnalyzeVariableInitializer(NodeId initializer,
		ScopeId scope, TypeId type, bool local);
	bool TryAnalyzeInitializerListVariable(NodeId expression, ScopeId scope,
		TypeId type, EntityId class_entity, bool local,
		ExpressionInfo* initializer);
	bool TryAnalyzeClassExpressionInitializer(NodeId expression, ScopeId scope,
		TypeId type, ExpressionInfo* initializer);
	ExpressionInfo AnalyzeConstantAwareVariableInitializer(NodeId initializer,
		ScopeId scope, TypeId type, bool local, bool require_constant,
		bool preserve_recipe = false);
	bool ShouldProbeConstantInitialization(bool local, const SpecInfo& spec,
		TypeId type) const;
	bool ShouldPreserveRuntimeInitializerRecipe(bool local,
		const SpecInfo& spec, TypeId type, NodeId initializer) const;
	void PublishVariableInitializer(BindingId binding, TypeId type,
		const SpecInfo& spec, const ExpressionInfo& initializer,
		bool preserve_runtime_recipe);
	bool HasConstantInitializerFact(const ExpressionInfo& initializer) const;
	ExpressionInfo AnalyzeInClassStaticInitializer(NodeId initializer,
		ScopeId scope, TypeId type);
	ExpressionInfo FinalizeVariableInitializer(ExpressionInfo initializer,
		TypeId type, EntityId class_entity, bool local);
	ExpressionInfo AnalyzeDefaultConstexprObjectInitializer(
		TypeId type, ScopeId scope, bool local);
	void PublishConstantVariableInitializer(BindingId binding, TypeId type,
		const SpecInfo& spec, const ExpressionInfo& initializer);
	void RecordStaticConstantInitializer(
		BindingId binding, std::uint32_t initializer);
	void PublishStaticConstantEvaluationStats() const;
	void PublishInitializationStats() const;
	ExpressionInfo AnalyzeCall(NodeId node, ScopeId scope, TypeId target);
	std::vector<NodeId> CollectCallArgumentSyntax(
		NodeId call, NodeId* arguments_node) const;
	ExpressionInfo AnalyzeBuiltinInvoke(ScopeId scope,
		const std::vector<NodeId>& argument_syntax,
		const std::vector<ExpressionInfo>* analyzed_arguments, TypeId target);
	bool TryAnalyzeImmediateBuiltinCall(const std::string& spelling,
		ScopeId scope, const std::vector<NodeId>& argument_syntax,
		TypeId target, ExpressionInfo* result);
	TypeId ResolveArrowOperand(ExpressionInfo* object, ScopeId scope,
		NodeId object_syntax);
	bool FunctionalCastPrecedesFunctions(const std::string& spelling,
		ScopeId scope, TypeId cast_type, NodeId syntax,
		const std::vector<BindingId>& candidates);
	bool AnalyzeRetainedNamedCall(NodeId name_syntax,
		const std::string& spelling, ScopeId scope,
		const std::vector<NodeId>& argument_syntax,
		const std::vector<ExpressionInfo>& arguments, TypeId target,
		ExpressionInfo* result);
	TypeId ResolveFunctionalCastType(ScopeId scope,
		const std::string& spelling, NodeId syntax = kNoNode);
	bool IsClassObjectType(TypeId type) const;
	bool IsConstexprLiteralType(TypeId type) const;
	bool IsConstexprDefaultConstructibleType(TypeId type) const;
	bool IsConstexprImplicitDefaultConstructor(EntityId entity) const;
	bool IsVolatileSubobjectType(TypeId type) const;
	bool IsConstexprCallableType(TypeId type, bool constructor) const;
	TypeId ApplyConstexprMemberFunctionType(TypeId type, EntityId owner,
		bool static_member);
	TypeId ApplyConstexprDeclaredFunctionType(TypeId type, ScopeId owner,
		NameId name, EntityId entity);
	void ValidateConstexprCallableType(TypeId type, bool constructor) const;
	void ValidateConstexprClassDeclarations(EntityId entity);
	BindingId EnsureBuiltinFunction(BuiltinFunctionKind kind);
	bool AnalyzeBuiltinCall(const std::string& spelling, ScopeId scope,
		const std::vector<NodeId>& argument_syntax, TypeId target,
		ExpressionInfo* result);
	bool AnalyzeDirectMemberCall(NodeId callee, ScopeId scope,
		const std::vector<NodeId>& argument_syntax, TypeId target,
		ExpressionInfo* result);
	bool AnalyzeExplicitDestructorCall(NodeId callee, ScopeId scope,
		const std::vector<NodeId>& argument_syntax, TypeId target,
		ExpressionInfo* result);
	void AppendArgumentDependentCandidates(NameId name,
		const std::vector<ExpressionInfo>& arguments,
		std::vector<BindingId>* candidates, bool enum_operator_only = false,
		const std::vector<NodeId>* explicit_syntax = 0,
		ScopeId use_scope = kNoScope,
		const std::vector<NodeId>* argument_syntax = 0);
	void CompleteArgumentDependentCallCandidates(NameId name,
		const std::vector<NodeId>* explicit_syntax, ScopeId use_scope,
		const std::vector<NodeId>& argument_syntax,
		const std::vector<ExpressionInfo>& arguments,
		bool suppress_adl, std::vector<BindingId>* candidates);
	bool TryAnalyzeOverloadedOperator(const std::string& operation,
		ScopeId scope, const std::vector<NodeId>& operand_syntax,
		const std::vector<ExpressionInfo>& operands, bool member_only,
		TypeId target, ExpressionInfo* result,
		const std::vector<ConversionRank>* competing_builtin_ranks = 0);
	bool TryAnalyzeCallOperator(ScopeId scope, const ExpressionInfo& callee,
		const std::vector<NodeId>& argument_syntax,
		const std::vector<ExpressionInfo>* analyzed_arguments, TypeId target,
		ExpressionInfo* result);
	bool TryAnalyzeCallSurrogate(ScopeId scope, const ExpressionInfo& callee,
		const std::vector<ExpressionInfo>& arguments, TypeId target,
		ExpressionInfo* result);
	BindingId SelectOperatorOverload(ScopeId scope,
		const std::vector<NodeId>& operand_syntax,
		const std::vector<ExpressionInfo>& operands,
		const std::vector<BindingId>& candidates,
		const ExpressionInfo& object, bool* selected_member,
		ObjectConversionFact* object_conversion,
		std::vector<CallConversionFact>* argument_conversions);
	ExpressionInfo MakeImplicitObjectPointer(const ExpressionInfo& object);
	void BeginAssociatedLookup();
	void AddAssociatedType(TypeId type);
	void AddAssociatedEntity(EntityId entity);
	void AddAssociatedScope(ScopeId scope);
	void BeginCandidateCollection();
	void AddCandidate(BindingId binding,
		std::vector<BindingId>* candidates);
	TypeId EnumOperatorOperandType(TypeId type) const;
	bool MatchesEnumOnlyOperatorCandidate(BindingId binding,
		const std::vector<ExpressionInfo>& operands) const;
	void IndexEnumOperatorCandidate(BindingId binding);
	void AppendIndexedEnumOperatorCandidates(ScopeId owner, NameId name,
		const std::vector<ExpressionInfo>& operands,
		std::vector<BindingId>* candidates);
	void AppendVisibleEnumOperatorCandidates(ScopeId scope, NameId name,
		const std::vector<ExpressionInfo>& operands,
		std::vector<BindingId>* candidates);
	void AppendDirectFunctionCandidates(ScopeId owner, NameId name,
		bool exclude_template_specializations,
		std::vector<BindingId>* candidates);
	void AppendHiddenFriendCandidates(EntityId owner, NameId name,
		const std::vector<ExpressionInfo>& arguments, bool enum_operator_only,
		std::vector<BindingId>* candidates,
		const std::vector<NodeId>* explicit_syntax = 0,
		ScopeId use_scope = kNoScope,
		const std::vector<NodeId>* argument_syntax = 0);
	ExpressionInfo AnalyzeUnary(NodeId node, ScopeId scope,
		TypeId target = kNoType);
	TypeId UnaryAddressOperandTarget(const std::string& operation,
		TypeId target) const;
	TypeId UnaryAddressContextTarget(const std::string& operation,
		TypeId target, NodeId operand, ScopeId scope);
	TypeId MemberPointerAddressSyntaxTarget(NodeId syntax, ScopeId scope);
	TypeId MemberPointerAddressTarget(const ExpressionInfo& operand,
		NodeId operand_syntax, TypeId target) const;
	bool FormMemberPointerAddress(const ExpressionInfo& operand, TypeId target,
		TypeId* result_type, bool* constant, ConstexprScalarValue* scalar,
		BindingId* member) const;
	ExpressionInfo AnalyzeBinary(NodeId node, ScopeId scope);
	bool TryAnalyzeMemberPointerApplication(const std::string& operation,
		const std::string& display_operation, const ExpressionInfo& left,
		const ExpressionInfo& right, ExpressionInfo* result);
	bool IsBuiltinLogicalOperand(const ExpressionInfo& operand) const;
	bool PrepareBuiltinComparison(const std::string& operation,
		ExpressionInfo* left, ExpressionInfo* right, TypeId* operand_type);
	TypeId PrepareBuiltinArithmetic(const std::string& operation,
		const ExpressionInfo& left, const ExpressionInfo& right);
	ExpressionInfo BuildBinaryExpression(const std::string& operation,
		const std::string& display_operation, NodeId left_syntax,
		NodeId right_syntax, ExpressionInfo left, ExpressionInfo right,
		ScopeId scope);
	ExpressionInfo AnalyzeAssignment(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeAssignmentInBracedContext(
		NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeCast(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeTypeid(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeThrowExpression(NodeId node, ScopeId scope);
	bool AnalyzeExceptionStatement(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeTryStatement(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeExceptionHandler(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	bool TryAnalyzeTypeidComparison(const std::string& operation,
		const std::string& display_operation, NodeId left_syntax,
		NodeId right_syntax, const ExpressionInfo& left,
		const ExpressionInfo& right, ScopeId scope, ExpressionInfo* result);
	bool TryAnalyzeDynamicCast(TypeId target, const ExpressionInfo& operand,
		ExpressionInfo* result);
	bool AnalyzeParenthesizedFunctionTemplateCast(NodeId type_id,
		NodeId operand, ScopeId scope, ExpressionInfo* result);
	bool AnalyzeParenthesizedValueBinaryCast(NodeId type_id,
		NodeId operand, ScopeId scope, ExpressionInfo* result);
	void AppendParenthesizedCallArguments(NodeId node,
		std::vector<NodeId>* arguments) const;
	ExpressionInfo AnalyzeConditional(NodeId node, ScopeId scope);
	void ApplyConditionalClassConversion(
		ExpressionInfo* yes, ExpressionInfo* no);
	ExpressionInfo BuildClassConditional(std::uint32_t condition,
		const ExpressionInfo& yes, const ExpressionInfo& no, TypeId type,
		bool preserve_xvalue);
	ExpressionInfo RetargetClassConditional(const ExpressionInfo& value,
		TypeId type);
	ExpressionInfo AnalyzeSubscript(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeSizeof(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeBracedInit(NodeId node, ScopeId scope, TypeId target);
	ExpressionInfo AnalyzeAggregateInit(TypeId type, ScopeId scope,
		std::uint32_t* element_edge);
	ExpressionInfo AnalyzeArrayAggregateInit(TypeId type, ScopeId scope,
		std::uint32_t* element_edge);
	ExpressionInfo AnalyzeAggregateElement(TypeId type, ScopeId scope,
		std::uint32_t* element_edge);
	bool DefaultInitializationOverwritesObject(EntityId entity) const;
	ExpressionInfo AnalyzePreparedAggregateElement(TypeId type, ScopeId scope,
		std::uint32_t* element_edge);
	ExpressionInfo AnalyzeAggregateDescent(TypeId type, ScopeId scope,
		std::uint32_t* element_edge);
	CallConversionFact PreparedAggregateElementConversion(NodeId source,
		TypeId target, const ExpressionInfo& expression);
	ExpressionInfo BuildLocalAggregateArrayActions(
		const ExpressionInfo& initializer);
	std::uint32_t BuildAggregateConstructionAction(TypeId type,
		std::uint32_t aggregate_list, bool allow_array_members = false);
	ExpressionInfo AnalyzeNewExpression(NodeId node, ScopeId scope,
		TypeId target);
	ExpressionInfo AnalyzeArrayNewExpression(NodeId node, NodeId type_node,
		ScopeId scope, TypeId target);
	ExpressionInfo AnalyzeDeleteExpression(NodeId node, ScopeId scope,
		TypeId target);
	BindingId SelectUsualDeallocation(ScopeId scope, EntityId entity,
		bool explicit_global, bool array, TypeId object_type);
	ExpressionInfo MaterializeTemporary(const ExpressionInfo& initializer);
	ExpressionInfo MaterializeDiscardedClassResult(ExpressionInfo value);
	ExpressionInfo AnalyzeClassFunctionalCast(TypeId cast_type, ScopeId scope,
		const std::vector<NodeId>& argument_syntax, NodeId arguments_node,
		TypeId target,
		const std::vector<ExpressionInfo>* prepared_arguments = 0);
	ExpressionInfo ApplyClassObjectTarget(ExpressionInfo value, TypeId target);
	bool TryFoldConstantClassConversion(const ExpressionInfo& value,
		BindingId conversion, TypeId target, std::int64_t* result);
	ExpressionInfo AnalyzeMember(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeImplicitDataMember(BindingId member, ScopeId scope,
		TypeId target, EntityId naming_class);
	void AnalyzeClassMember(NodeId node, ScopeId scope, TypeId owner_type,
		AccessKind access);
	bool IsCallableDeclaration(NodeId node) const;
	void ValidateOrdinaryMemberFunctionBodies(EntityId entity);
	void ValidateOrdinaryMemberFunctionBody(BindingId function);
	void ValidateStaticAssertionsInBlock(NodeId block, ScopeId scope,
		std::uint32_t detached_output);
	void RegisterClassMemberFunction(EntityId entity, BindingId function);
	void AnalyzeBitField(NodeId node, ScopeId scope, TypeId owner_type,
		AccessKind access);
	void AnalyzeSpecialMember(NodeId node, ScopeId scope, TypeId owner_type,
		AccessKind access);
	void AnalyzeConversionFunction(NodeId node, ScopeId scope,
		TypeId owner_type, AccessKind access);
	void AnalyzeOutOfClassSpecialMember(NodeId node, ScopeId scope,
		ScopeId declaration_scope = kNoScope,
		bool defer_demand = false);
	void ConfigureVirtualFunction(BindingId binding, const SpecInfo& spec,
		NodeId declarator, NodeId initializer);
	void CompleteClassPolymorphism(EntityId entity);
	void MarkVtableDemand(EntityId entity);
	bool CovariantVirtualReturn(TypeId derived, TypeId base) const;
	FunctionSignatureKey VirtualSignatureKey(BindingId binding) const;
	bool VirtualSignatureMatches(BindingId derived, BindingId base) const;
	std::uint32_t VirtualSlotFor(BindingId binding) const;
	void CompleteOutOfClassDefaultedConstructor(EntityId entity,
		BindingId constructor);
	void CompleteDefaultedDefaultConstructor(EntityId entity,
		BindingId constructor);
	void ValidateConstexprConstructorDefinition(
		const FunctionInfo& constructor);
	void CompleteDefaultedDestructor(EntityId entity, BindingId destructor);
	void RegisterClassSpecialMember(BindingId binding);
	void ConfigureAssignmentSpecialMember(BindingId binding,
		NodeId initializer, bool defaulted_inline = true);
	bool AnalyzeQualifiedAssignmentStatement(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void CompleteClassSpecialMembers(EntityId entity);
	BindingId AssignmentForSubobject(TypeId type,
		SpecialMemberKind kind) const;
	BindingId ConstructorForSubobject(TypeId type,
		SpecialMemberKind kind) const;
	void EvaluateSynthesizedAssignment(EntityId entity,
		SpecialMemberKind kind, bool* deleted, bool* trivial,
		bool* nonthrowing) const;
	void EvaluateSynthesizedConstructor(EntityId entity,
		SpecialMemberKind kind, bool* deleted, bool* trivial,
		bool* nonthrowing) const;
	void ConfigureSynthesizedStoragePrefix(EntityId entity,
		FunctionInfo* function) const;
	BindingId DeclareImplicitAssignment(EntityId entity,
		SpecialMemberKind kind);
	BindingId DeclareImplicitCopyMoveConstructor(EntityId entity,
		SpecialMemberKind kind);
	void AddSynthesizedAssignmentBody(const FunctionInfo& function,
		const std::vector<BindingId>& parameters, std::uint32_t body);
	void AddSynthesizedConstructorBody(const FunctionInfo& function,
		const std::vector<BindingId>& parameters, std::uint32_t body);
	void DemandSynthesizedConstructorDependencies(BindingId constructor);
	bool CanAccessMember(BindingId member,
		EntityId naming_class = kNoEntity,
		EntityId object_class = kNoEntity) const;
	bool HasClassPrivilege(EntityId owner) const;
	bool HasDerivedClassPrivilege(EntityId base) const;
	bool HasProtectedObjectPrivilege(EntityId owner,
		EntityId object_class) const;
	bool AccessIsBaseOf(EntityId base, EntityId derived) const;
	void PublishUsingAccess(BindingId alias, BindingId source,
		AccessKind access);
	bool BaseConversionAllowed(EntityId derived, EntityId base) const;
	std::size_t BaseConversionDistance(TypeId source, TypeId target) const;
	std::size_t BaseProjectionCount(TypeId source, TypeId target,
		std::uint64_t* offset = 0) const;
	EntityId ZeroOffsetClassEntity(TypeId type) const;
	bool VisitZeroOffsetSubobjects(EntityId root, std::uint32_t marker,
		std::uint32_t conflict_marker);
	bool ZeroOffsetSubobjectConflict(EntityId base, TypeId member_type);
	const EntityRecord* InitializeClassBaseLayout(EntityId entity,
		std::size_t packing_alignment, std::size_t* size,
		std::size_t* alignment, std::size_t* natural_alignment);
	bool ClassBasesAreEmpty(EntityId entity) const;
	void InitializeImplicitBaseConstructorFacts(EntityId entity);
	void CompleteClassMemberDestructionFacts(EntityId entity,
		bool is_union, bool defaulted_destructor);
	void CompleteClassLayout(EntityId entity);
	std::size_t RequestedAlignment(NodeId node, ScopeId scope);
	void InheritConstructors(EntityId entity,
		const std::vector<BindingId>& constructors);
	void PublishStableFunctionTemplateResultAbi(
		const FunctionTemplatePattern& pattern, TypeId function_type,
		EntityId member_owner, BindingId canonical_binding);
	void CompleteFunctionTemplatePlaceholderResult(std::size_t pattern,
		BindingId binding, EntityId member_owner);
	bool TryInheritConstructors(EntityId entity, ScopeId scope,
		ScopeId target_owner, NameId target_name, bool names_owner_alias,
		const std::vector<BindingId>& constructors,
		const std::vector<std::size_t>& template_patterns);
	BindingId EnsureConstructorBaseEntry(BindingId constructor);
	BindingId EnsureDestructorBaseEntry(BindingId destructor);
	void EnsureStaticMemberStorage(BindingId member,
		bool constant_storage = false);
	void DemandStaticConstantInitializerDependencies(BindingId member);
	BindingId EnsureImplicitConstructor(EntityId entity);
	BindingId EnsureImplicitDestructor(EntityId entity);
	const std::vector<BindingId>& ConstructorCandidates(EntityId entity) const;
	BindingId DestructorForType(TypeId type) const;
	bool CacheDestructorChainDecision(BindingId destructor,
		bool proven_empty) const;
	bool CanElideDestructorChain(BindingId destructor) const;
	bool IsElidableAutomaticDestructor(BindingId destructor) const;
	EntityId DestructedEntity(TypeId type) const;
	BindingId SelectConstructor(ScopeId scope,
		const std::vector<NodeId>& argument_syntax,
		const std::vector<ExpressionInfo>& arguments,
		const std::vector<BindingId>& candidates, bool copy_initialization,
		bool list_initialization,
		std::vector<CallConversionFact>* selected_conversions = 0,
		bool quiet = false, NodeId source_list = kNoNode,
		TypeId initialized_type = kNoType,
		NodeId* selected_list_source = 0);
	BindingId SelectInitializerListConstructorPhase(ScopeId scope,
		TypeId initialized_type, NodeId source_list,
		const std::vector<NodeId>& argument_syntax,
		const std::vector<BindingId>& candidates, bool copy_initialization,
		std::vector<CallConversionFact>* selected_conversions, bool quiet,
		NodeId* selected_source);
	void AppendConstructorTemplateCandidates(TypeId initialized_type,
		const std::vector<ExpressionInfo>& arguments,
		std::vector<BindingId>* candidates,
		const std::vector<NodeId>* argument_syntax = 0,
		ScopeId argument_scope = kNoScope);
	void PrepareBracedInitialization(NodeId list, ScopeId scope);
	bool NeedsBracedCallContext(
		const std::vector<NodeId>& arguments) const;
	ExpressionInfo AnalyzeCallInBracedContext(
		NodeId call, ScopeId scope, TypeId target);
	ExpressionInfo AnalyzeUntypedCallArgument(NodeId argument, ScopeId scope);
	CallConversionFact UntypedCallArgumentConversion(
		NodeId argument, ScopeId scope, TypeId target);
	ExpressionInfo MaterializeCallArgument(NodeId syntax, ScopeId scope,
		TypeId target, const ExpressionInfo& prepared,
		const CallConversionFact* conversion = 0);
	ExpressionInfo MaterializeBracedConstructorArgument(
		NodeId syntax, ScopeId scope, TypeId parameter_type);
	bool ReusePreparedBracedExpression(NodeId node, TypeId target,
		ExpressionInfo* result);
	CallConversionFact BracedInitializationConversion(
		NodeId list, ScopeId scope, TypeId target);
	bool IsBracedNarrowing(
		const ExpressionInfo& source, TypeId target,
		const CallConversionFact* conversion = 0) const;
	ExpressionInfo AnalyzeBracedCallArgument(
		NodeId list, ScopeId scope, TypeId target);
	bool EmptyDefaultConstructorChain(BindingId constructor,
		std::vector<BindingId>* base_entries);
	std::uint32_t BuildConstructorAction(TypeId type, ScopeId scope,
		const std::vector<NodeId>& argument_syntax, bool copy_initialization,
		bool list_initialization, bool base_subobject = false,
		bool demand = true, NodeId source_list = kNoNode,
		const std::vector<ExpressionInfo>* prepared_arguments = 0);
	std::uint32_t BuildClassValueConstructorAction(TypeId type,
		const ExpressionInfo& source, bool copy_initialization = true,
		bool demand = true);
	bool TryBuildElidedClassValueTransfer(TypeId type,
		const ExpressionInfo& source, BindingId selected_constructor,
		ExpressionInfo* result);
	BindingId ValidateClassValueConstruction(TypeId type,
		const ExpressionInfo& source, bool copy_initialization = true);
	void FinalizeNamedReturnSlot(std::uint32_t function);
	std::uint32_t BuildDefaultConstructorAction(TypeId type, ScopeId scope);
	void AddConstructorMemberActions(const FunctionInfo& constructor,
		ScopeId function_scope, const std::vector<BindingId>& parameters,
		std::uint32_t body);
	void CollectConstructorInitializers(const FunctionInfo& constructor,
		EntityId entity, ScopeId function_scope,
		std::vector<NodeId>* syntax, std::vector<ScopeId>* scopes,
		std::vector<std::uint8_t>* expanded);
	void RecordDelegatingConstructor(BindingId source, BindingId selected);
	void AddBaseInitializationAction(EntityId entity, std::size_t base_ordinal,
		NodeId initializer, ScopeId scope, std::uint32_t body,
		bool pack_expanded = false);
	void AddMemberInitializationAction(BindingId member, NodeId initializer,
		ScopeId scope, std::uint32_t body);
	bool InitializationActionsAreNonthrowing(std::uint32_t body);
	void AddDefaultConstructor(std::uint32_t variable, BindingId binding,
		TypeId type);
	void AddDestructorSubobjectActions(EntityId entity, std::uint32_t body);
	ScopeId CompoundCleanupStop(ScopeId scope) const;
	void AddLifetimeObligation(ScopeId scope, BindingId object, TypeId type,
		bool allow_elision = true);
	void AddTemporaryLifetimeObligation(ScopeId scope,
		std::uint32_t temporary);
	void MarkInitializerListLifetimeScope(ScopeId scope,
		std::uint32_t temporary);
	bool ExtendInitializerListVariableLifetime(TypeId type, ScopeId scope,
		std::uint32_t initializer, bool control_dependent);
	std::uint32_t InitializerListBackingTemporary(
		TypeId type, std::uint32_t initializer) const;
	bool HasActiveInitializerListBacking(ScopeId scope) const;
	void MarkInitializerListLifetimeCalls(std::uint32_t node);
	bool CollectTemporaryObjects(std::uint32_t node,
		std::vector<std::uint32_t>* temporaries);
	bool CollectTemporaryObjectsImpl(std::uint32_t node,
		std::vector<std::uint32_t>* temporaries, bool conditionally_evaluated,
		std::uint32_t branch_owner, std::uint32_t branch_child,
		std::size_t branch_depth, bool projected_subobject);
	void MarkFullExpressionCalls(std::uint32_t node,
		bool managed_cleanup = false, bool allocation_call = false);
	void MarkDefaultArgumentSubtree(std::uint32_t node);
	bool HasControlDependentTemporary(std::uint32_t node);
	void AppendFullExpressionDestructionActions(std::uint32_t expression,
		std::uint32_t output_parent,
		bool preserve_nontrivial_actions = false);
	void FinalizeStaticallyUnreachableBranchCleanup(
		std::uint32_t function_definition);
	bool RequiresManagedConditionalFullExpression(
		std::uint32_t expression, std::size_t first_edge);
	std::uint32_t PublishVariableInitializerActions(std::uint32_t variable,
		BindingId binding, TypeId type, const ExpressionInfo& initializer,
		bool has_initializer, bool declaration_only,
		bool qualified_lexical_scope);
	bool StageNestedTemplateTemporaryCleanup(std::uint32_t expression,
		std::uint32_t statement);
	void StageExceptionalFullExpression(std::uint32_t expression,
		std::uint32_t statement, ScopeId scope, bool force = false);
	void StageAutomaticInitializerException(std::uint32_t expression,
		std::uint32_t variable, ScopeId scope, BindingId binding, TypeId type,
		bool eligible);
	void StageControlFullExpression(std::uint32_t expression,
		std::uint32_t statement, ScopeId scope);
	void StageReturnTemporaryCleanup(std::uint32_t expression,
		std::uint32_t statement, ScopeId scope);
	void AppendUnwindDestructionActions(ScopeId scope,
		std::uint32_t output_parent, ScopeId stop_exclusive = kNoScope);
	bool HasUnwindDestructionActions(ScopeId scope,
		ScopeId stop_exclusive = kNoScope) const;
	bool HasEnclosingNontrivialObjectLifetime(ScopeId scope,
		ScopeId stop_exclusive = kNoScope) const;
	void AddNamespaceObjectAction(std::uint32_t variable, BindingId object,
		TypeId type, std::uint32_t initializer);
	void AddLocalStaticObjectAction(std::uint32_t variable, BindingId object,
		TypeId type, std::uint32_t initializer, NameId source_file,
		std::uint32_t source_line, std::uint32_t source_column,
		std::uint32_t source_token_first, std::uint32_t source_token_last,
		bool constant_initialized);
	void RegisterVariableLifetimeAndStorage(ScopeId scope, bool local,
		bool declaration_only, std::uint32_t variable, BindingId object,
		TypeId type, NameId source_file, std::uint32_t source_line,
		std::uint32_t source_column, std::uint32_t source_token_first,
		std::uint32_t source_token_last, bool constant_initialized);
	bool DemandRuntimeInitializerFunctions(std::uint32_t initializer,
		bool function_addresses_only = false);
	void AppendScopeDestructionActions(ScopeId scope,
		std::uint32_t output_parent, ScopeId stop_exclusive = kNoScope);
	std::uint32_t MakeDestructorAction(TypeId type, BindingId destructor,
		BindingId object, std::uint32_t base_projections = 0,
		bool demand = true);
	std::uint32_t MakeTemporaryDestructorAction(std::uint32_t temporary,
		BindingId destructor = kNoBinding,
		bool preserve_nontrivial_action = false);
	EntityId EntityOf(TypeId type) const;
	ExpressionInfo MakeLiteral(TypeId type, NameId text,
		ValueCategory category = VALUE_PRVALUE);
	ExpressionInfo MakeStringLiteral(const std::string& spelling,
		std::size_t* character_count = 0);
	ExpressionInfo MakeBuiltinScalarLiteral(const std::string& spelling,
		NodeId syntax = kNoNode);
	bool TryAnalyzeUserDefinedStringLiteral(const std::string& spelling,
		ScopeId scope, TypeId target, ExpressionInfo* result);
	bool TryAnalyzeUserDefinedNumericLiteral(const std::string& spelling,
		ScopeId scope, TypeId target, ExpressionInfo* result);
	ExpressionInfo AnalyzeThisExpression(ScopeId scope);
	bool IsNonthrowing(NodeId declarator, ScopeId scope);
	ExpressionInfo AnalyzeNoexcept(NodeId node, ScopeId scope);
	void RecordExpressionFacts(const ExpressionInfo& value);
	ExpressionInfo ApplyTarget(ExpressionInfo value, TypeId target,
		ConversionRank known_conversion = CONVERSION_INVALID);
	ConversionRank MemberPointerConversion(TypeId source, bool integer_zero,
		TypeId target) const;
	bool ApplyMemberPointerTarget(ExpressionInfo* value, TypeId source,
		TypeId target);
	bool HasTargetTypedSpecializedMemberImmediate(
		const ExpressionInfo& destination,
		const ExpressionInfo& value) const;
	ConversionRank MemberObjectConversion(const ExpressionInfo& source,
		TypeId target, BindingId member) const;
	ExpressionInfo ApplyMemberObjectTarget(ExpressionInfo value,
		TypeId target, BindingId member,
		const ObjectConversionFact* conversion_fact = 0);
	bool ApplyQualifiedMemberNamingTarget(ExpressionInfo* value,
		EntityId naming_class, BindingId member);
	void ApplyQualifiedCallNamingTarget(ExpressionInfo* value,
		EntityId naming_class, const std::vector<BindingId>& candidates);
	ConversionRank Conversion(TypeId source, ValueCategory category,
		bool integer_zero, TypeId target) const;
	ConversionRank Conversion(const ExpressionInfo& source, TypeId target) const;
	std::uint8_t ArrayElementCv(TypeId type) const;
	bool QualificationConversion(TypeId source, TypeId target) const;
	bool SimilarUnqualified(TypeId source, TypeId target) const;
	TypeId EffectiveType(TypeId type) const;
	TypeId Decay(TypeId type) const;
	TypeId CommonArithmeticType(TypeId left, TypeId right) const;
	bool IsIntegral(TypeId type, bool allow_scoped_enum = false) const;
	bool IsFloating(TypeId type) const;
	bool IsArithmetic(TypeId type) const;
	bool IsPointer(TypeId type) const;
	bool IsMemberPointer(TypeId type) const;
	bool IsMeasurableObjectType(TypeId type, bool alignment_query);
	bool IsPointerToCompleteObject(TypeId type);
	bool IsNullptr(TypeId type) const;
	bool IsVoid(TypeId type) const;
	bool IsConst(TypeId type) const;
	bool IsModifiableLvalue(const ExpressionInfo& value) const;
	FundamentalKind FundamentalOf(TypeId type) const;
	int IntegralRank(TypeId type) const;
	TypeId IntegralPromotionType(TypeId type) const;
	bool IsUnsignedIntegral(TypeId type) const;
	std::size_t IntegralWidth(TypeId type) const;
	std::int64_t NormalizeIntegralConstant(TypeId type,
		std::int64_t value) const;
	ConstexprScalarValue ExpressionScalar(const ExpressionInfo& value) const;
	ConstexprScalarValue NormalizeScalarConstant(TypeId type,
		const ConstexprScalarValue& value) const;
	ConstexprScalarValue ConvertScalarConstant(TypeId source_type,
		TypeId target_type, const ConstexprScalarValue& value) const;
	void SetExpressionScalar(ExpressionInfo* expression,
		const ConstexprScalarValue& value) const;
	void SetExpressionObject(ExpressionInfo* expression,
		std::uint32_t object) const;
	void SetExpressionSubobject(ExpressionInfo* expression,
		std::uint32_t object, std::uint32_t complete_object) const;
	void SetExpressionAddress(ExpressionInfo* expression,
		std::uint32_t address) const;
	void SetExpressionLvalueAddress(ExpressionInfo* expression,
		std::uint32_t address) const;
	std::uint32_t ExpressionAddress(const ExpressionInfo& expression) const;
	std::uint32_t LvalueAddress(ExpressionInfo* expression);
	std::uint32_t InternConstexprAddress(
		const ConstexprAddressValue& address);
	const ConstexprAddressValue* ConstexprAddressAt(
		std::uint32_t address) const;
	std::uint32_t OffsetConstexprAddress(std::uint32_t address,
		std::int64_t byte_offset, bool narrow, std::int64_t extent = 0);
	std::uint32_t NullConstexprAddress();
	bool ExpressionTruth(const ExpressionInfo& expression) const;
	bool TryAnalyzeConstexprIndirectCall(ExpressionInfo* callee,
		ScopeId scope, const std::vector<NodeId>& argument_syntax,
		const std::vector<ExpressionInfo>& arguments, TypeId target,
		ExpressionInfo* result,
		const std::vector<CallConversionFact>* argument_conversions = 0);
	std::uint32_t ExpressionObject(const ExpressionInfo& expression) const;
	std::uint32_t ExpressionCompleteObject(
		const ExpressionInfo& expression) const;
	void SetExpressionDumpObject(ExpressionInfo* expression) const;
	void PublishDumpObject(std::uint32_t node, std::uint32_t object);
	ConstexprScalarValue BindingScalar(BindingId binding) const;
	std::uint32_t BindingObject(BindingId binding) const;
	std::uint32_t BindingAddress(BindingId binding) const;
	void PublishBindingScalar(BindingId binding,
		const ConstexprScalarValue& value);
	void PublishBindingObject(BindingId binding, std::uint32_t object);
	void PublishBindingAddress(BindingId binding, std::uint32_t address);
	void PublishBindingConstant(BindingId binding,
		const ExpressionInfo& value);
	void PublishCanonicalBindingConstant(BindingId binding);
	ExpressionInfo AnalyzeConstantRequiredExpression(NodeId node,
		ScopeId scope, TypeId type, bool required);
	void SetExpressionBindingConstant(ExpressionInfo* expression,
		BindingId binding) const;
	bool BuildConstexprObjectElement(TypeId type, BindingId member,
		const ExpressionInfo& value, ConstexprObjectElement* result) const;
	std::uint32_t InternConstexprObject(TypeId type,
		const std::vector<ConstexprObjectElement>& elements);
	const ConstexprObjectElement* ConstexprObjectElementAt(
		std::uint32_t object, std::size_t ordinal) const;
	std::uint32_t ProjectConstexprObject(
		std::uint32_t object, TypeId target,
		std::uint64_t* byte_offset = 0) const;
	const ConstexprObjectElement* ConstexprClassMemberAt(
		std::uint32_t object, BindingId member) const;
	void SetExpressionObjectElement(ExpressionInfo* expression,
		const ConstexprObjectElement& element) const;
	ExpressionInfo MaterializeConstexprObject(std::uint32_t object,
		TypeId type);
	ExpressionInfo MaterializeConstexprObjectElement(
		const ConstexprObjectElement& element, TypeId type);
	ExpressionInfo MaterializeConstexprAddress(std::uint32_t address,
		TypeId type);
	bool MaterializeConstantDefinitionInitializer(BindingId binding,
		TypeId* type, ExpressionInfo* initializer);
	bool PreferMaterializedConstantDefinition(BindingId canonical) const;
	void PublishInClassStaticDefinitionPolicy(BindingId binding, TypeId type,
		const SpecInfo& spec, NodeId initializer);
	bool ScalarTruth(const ConstexprScalarValue& value) const;
	ConstexprScalarValue ApplyConstantScalarBinary(
		const std::string& operation, const ConstexprScalarValue& left,
		const ConstexprScalarValue& right, TypeId operand_type) const;
	NameId InternScalar(TypeId type, const ConstexprScalarValue& value);
	bool TryEvaluateConstexprFunction(BindingId function,
		const std::vector<ExpressionInfo>& arguments,
		ConstexprScalarValue* value, bool* has_scalar,
		std::uint32_t* address,
		std::uint32_t* object,
		std::uint32_t* complete_object,
		const ExpressionInfo* receiver = 0);
	bool TryEvaluateConstexprConstructor(BindingId constructor,
		const std::vector<ExpressionInfo>& arguments,
		std::uint32_t* object);
	struct ConstexprConstructorPlan;
	bool PlanConstexprConstructorInitializers(const FunctionInfo& constructor,
		EntityId entity, std::size_t argument_count,
		ConstexprConstructorPlan* plan);
	bool EvaluateConstexprConstructorInitializers(
		const FunctionInfo& constructor, EntityId entity,
		const std::vector<ExpressionInfo>& arguments,
		const ConstexprConstructorPlan& plan, std::uint32_t* object);
	bool AnalyzeConstexprMemberInitializer(NodeId initializer, ScopeId scope,
		TypeId type, ExpressionInfo* value);
	bool AddConstexprInvocationArguments(const FunctionInfo& function,
		const std::vector<ExpressionInfo>& arguments);
	std::int64_t ParseInteger(const std::string& spelling) const;
	std::int64_t ApplyConstantBinary(const std::string& operation,
		std::int64_t left, std::int64_t right, TypeId operand_type) const;
	NameId InternNumber(std::int64_t value);
	std::size_t SideStorageBytes() const;

	std::uint32_t MakeDump(DumpKind kind, TypeId type = kNoType,
		ValueCategory category = VALUE_NONE, NameId text = 0,
		BindingId binding = kNoBinding);
	void Render();
	void RenderNode(std::uint32_t node, std::size_t depth);
	void RenderLine(const DumpNode& node, std::size_t depth);

	const SyntaxArena* arena_;
	std::ostream& output_;
	SemanticAnalysisStats* stats_;
	InternedStringTable& strings_;
	Program* program_;
	bool retain_lowering_facts_;
	bool render_output_;
	DumpArena& dump_;
	std::vector<std::uint32_t> string_literal_units_;
	std::uint32_t& root_;
	std::vector<NameId> scope_prefixes_;
	std::vector<NameId> scope_prefix_segments_;
	std::vector<ScopeId> scope_parents_;
	std::vector<NameId> scope_prefix_scratch_;
	IndexedSequenceTable function_sets_;
	IndexedSequenceTable ordinary_function_sets_;
	// ADL combines ordinary declarations with function-template deduction.
	// Keep the direct non-specialization slice indexed so that it need not scan
	// every prior instantiation of the same template name.
	IndexedSequenceTable ordinary_nontemplate_function_sets_;
	EnumOperatorCandidateTable enum_operator_candidates_;
	IndexedSequenceTable hidden_friend_sets_;
	IndexedSequenceTable hidden_friend_template_sets_;
	IndexedSequenceTable friend_class_grants_;
	IndexedSequenceTable friend_function_grants_;
	FunctionSignatureTable function_declarations_;
	FunctionSignatureTable using_function_declarations_;
	FunctionSignatureTable function_template_specialization_declarations_;
	FunctionSignatureTable member_ref_qualifier_shapes_;
	std::vector<std::uint32_t> function_fact_by_binding_;
	std::vector<FunctionInfo> functions_;
	std::vector<BindingId> builtin_functions_;
	std::vector<std::vector<BindingId> > entity_data_members_;
	std::vector<std::vector<ClassLayoutMember> > entity_layout_members_;
	std::vector<std::uint32_t> zero_offset_subobject_marks_;
	std::vector<EntityId> zero_offset_subobject_scratch_;
	std::vector<std::vector<BindingId> > entity_constructors_;
	std::vector<std::vector<BindingId> > entity_conversion_functions_;
	std::vector<std::vector<std::size_t> >
		entity_conversion_function_templates_;
	std::vector<std::vector<BindingId> > entity_member_functions_;
	std::vector<ClassPolymorphismFacts>& class_polymorphism_;
	std::vector<std::uint32_t> virtual_slot_by_binding_;
	std::vector<std::uint32_t> variable_node_by_binding_;
	std::vector<ClassSpecialMemberFacts> class_special_members_;
	std::vector<BindingId> implicit_constructor_by_entity_;
	std::vector<BindingId> constructor_base_entry_by_binding_;
	std::vector<BindingId> destructor_base_entry_by_binding_;
	// Canonical constructor identity owns this monotonic elision fact.  A
	// failure remains a correct conservative result after later declarations;
	// success is published only from complete class facts and known bodies.
	std::vector<std::uint8_t> empty_constructor_chain_states_;
	std::vector<std::uint32_t>
		empty_constructor_chain_dependency_begins_;
	std::vector<std::uint32_t>
		empty_constructor_chain_dependency_counts_;
	std::vector<BindingId> empty_constructor_chain_dependencies_;
	std::vector<std::uint32_t> empty_constructor_chain_entity_marks_;
	std::vector<std::uint32_t> empty_constructor_chain_binding_marks_;
	std::vector<BindingId> empty_constructor_chain_pending_;
	std::vector<BindingId> empty_constructor_chain_member_dependencies_;
	std::vector<BindingId> empty_constructor_chain_base_dependencies_;
	std::vector<std::uint32_t> static_member_storage_by_binding_;
	std::vector<std::uint8_t> explicit_static_member_specialization_states_;
	struct StaticConstantInitializerFact
	{
		std::uint32_t initializer;
		std::vector<BindingId> function_dependencies;
		bool prefer_materialized_definition;
		StaticConstantInitializerFact()
			: initializer(kNoDumpEdge), prefer_materialized_definition(false) {}
	};
	// Canonical member identity owns the immutable initializer recipe and only
	// the callable edges needed if an ODR-use later demands storage.
	std::vector<StaticConstantInitializerFact>
		static_constant_initializers_by_binding_;
	std::vector<BindingId> static_constant_dependency_owner_marks_;
	std::vector<BindingId> entity_destructor_by_entity_;
	std::vector<BindingId> hidden_friend_anchor_by_entity_;
	std::vector<NodeId> member_initializer_by_binding_;
	std::vector<NodeId> constructor_initializer_scratch_;
	std::vector<BindingId> constructor_initializer_touched_;
	// Replay can publish nested patterns, so published pattern owners must not
	// move while semantic construction is re-entrant.
	std::deque<FunctionTemplatePattern> function_templates_;
	std::vector<TypeId> function_template_shape_parameters_;
	std::vector<TypeId> dependent_template_argument_shapes_;
	TypeId function_template_dependent_result_shape_;
	TypeId function_template_nondeduced_type_shape_;
	TypeId class_template_nondeduced_type_shape_;
	const FunctionTemplatePattern* active_function_template_result_pattern_;
	mutable std::vector<std::uint8_t> function_template_dependency_cache_;
	IndexedSequenceTable template_function_sets_;
	struct FunctionTemplateUsingFact
	{
		std::uint32_t pattern;
		AccessKind access;
		FunctionTemplateUsingFact(std::uint32_t pattern_value,
			AccessKind access_value)
			: pattern(pattern_value), access(access_value) {}
	};
	IndexedSequenceTable function_template_using_fact_sets_;
	std::vector<FunctionTemplateUsingFact> function_template_using_facts_;
	IndexedSequenceTable template_argument_pack_bindings_;
	std::vector<TemplateArgument> template_argument_pack_values_;
	IndexedSequenceTable function_parameter_pack_bindings_;
	IndexedSequenceTable retained_call_function_sets_;
	IndexedSequenceTable retained_call_template_sets_;
	std::vector<std::uint8_t> retained_call_lookup_states_;
	std::vector<EntityId> retained_call_naming_classes_;
	TemplateArgumentPartitionTable template_argument_partitions_;
	FunctionTemplateResultIdentityTable function_template_result_identities_;
	TemplateSpecializationTable template_instantiations_;
	TemplateSpecializationTable function_template_default_requests_;
	IndexedSequenceTable lambda_closure_index_;
	pa25_semantic_detail::LambdaCaptureUseTable lambda_capture_uses_;
	std::vector<LambdaClosureFact> lambda_closures_;
	std::vector<LambdaCaptureFact> lambda_captures_;
	std::vector<std::uint32_t> lambda_count_by_function_;
	std::vector<std::uint32_t> lambda_count_by_namespace_;
	std::deque<ClassTemplatePattern> class_templates_;
	IndexedSequenceTable demanded_static_member_definitions_;
	std::vector<AliasTemplatePattern> alias_templates_;
	std::vector<std::uint32_t> alias_template_pattern_by_entity_;
	TemplateSpecializationTable alias_template_instantiations_;
	std::vector<std::uint8_t> alias_template_instantiation_states_;
	std::vector<VariableTemplatePattern> variable_templates_;
	IndexedSequenceTable variable_template_sets_;
	std::vector<std::uint8_t> variable_template_bindings_;
	std::vector<std::uint32_t> class_template_pattern_by_entity_;
	TemplateSpecializationTable class_template_instantiations_;
	TemplateSpecializationTable variable_template_instantiations_;
	std::vector<std::uint8_t> class_template_specialization_states_;
	std::vector<std::uint8_t> class_template_specialization_use_states_;
	// A specialization shell retains the selected partial declaration and its
	// narrow substitution overlay until definition completion consumes them.
	std::vector<ClassTemplatePartialSelection>
		class_template_partial_selections_;
	std::vector<std::uint8_t> class_template_explicit_instantiation_states_;
	std::vector<std::uint8_t> class_template_explicit_specialization_states_;
	std::vector<std::uint8_t> function_explicit_instantiation_states_;
	std::vector<std::uint8_t> function_explicit_specialization_states_;
	std::vector<std::uint32_t> class_template_member_definition_counts_;
	std::vector<std::uint32_t>
		class_template_demanded_member_definition_counts_;
	// Bit 0 is monotonic owner demand; bit 1 is deduplicated queued work.
	std::vector<std::uint8_t> class_template_member_definition_demand_states_;
	std::vector<BindingId> demanded_class_template_member_definitions_;
	std::size_t class_template_member_replay_depth_;
	std::size_t explicit_member_template_replay_depth_;
	std::size_t class_template_completion_suppressed_depth_;
	std::vector<std::uint8_t> candidate_substitution_failures_;
	std::vector<NodeId> deferred_class_definition_by_entity_;
	std::vector<ScopeId> deferred_class_scope_by_entity_;
	std::vector<std::uint32_t> injected_fact_by_binding_;
	std::vector<InjectedMemberInfo> injected_members_;
	std::vector<std::vector<LifetimeObligation> > scope_lifetimes_;
	std::vector<ScopeId> nearest_lifetime_scopes_;
	std::vector<ScopeId> nearest_initializer_list_lifetime_scopes_;
	// Children copy the active automatic-object count on scope entry; local
	// declarations increment only their scope's compact prefix entry.
	std::vector<std::uint32_t> scope_nontrivial_object_lifetime_prefixes_;
	std::vector<NamespaceObjectAction>& namespace_objects_;
	std::vector<LocalStaticObjectAction>& local_static_objects_;
	std::vector<std::uint32_t> local_static_count_by_function_;
	std::vector<AggregateHelperInfo>& aggregate_helpers_;
	FunctionSignatureTable aggregate_helper_index_;
	std::vector<std::uint32_t> widest_aggregate_helper_by_entity_;
	std::vector<ScopeId> break_cleanup_stops_;
	std::vector<ScopeId> continue_cleanup_stops_;
	std::vector<EntityId> demanded_default_constructor_entities_;
	std::vector<std::uint8_t> default_constructor_demand_states_;
	std::vector<BindingId> demanded_functions_;
	std::vector<BindingId> constexpr_evaluation_stack_;
	std::vector<ConstexprFrame> constexpr_frames_;
	std::vector<ConstexprLocalValue> constexpr_locals_;
	std::vector<std::size_t> constexpr_local_by_name_;
	std::vector<std::size_t> constexpr_local_by_pack_;
	std::vector<ConstexprScopeFact> constexpr_scope_facts_;
	std::vector<std::size_t> constexpr_type_alias_by_name_;
	std::vector<ConstexprBlockOffset> constexpr_block_offsets_;
	// Binding-indexed O(1) access with dense payloads for non-integral facts.
	std::vector<std::uint32_t> floating_constant_fact_by_binding_;
	std::vector<long double> floating_constant_values_;
	std::vector<BindingId> constexpr_member_pointer_by_binding_;
	// Completed object values are immutable and structurally interned. Bindings
	// carry only a compact object identity; elements remain dense by ordinal.
	std::vector<std::uint32_t> constexpr_object_by_binding_;
	std::vector<std::uint32_t> constexpr_address_by_binding_;
	std::vector<ConstexprAddressValue> constexpr_addresses_;
	std::unordered_map<ConstexprAddressValue, std::uint32_t,
		ConstexprAddressValueHash> constexpr_address_index_;
	std::vector<ConstexprObjectValue> constexpr_objects_;
	std::vector<ConstexprObjectElement> constexpr_object_elements_;
	std::unordered_multimap<std::size_t, std::uint32_t>
		constexpr_object_index_;
	std::vector<std::uint32_t> constexpr_object_by_dump_;
	std::vector<std::uint32_t> constexpr_scratch_object_by_dump_;
	DumpArena constexpr_scratch_dump_;
	std::unordered_map<ConstexprCallKey, ConstexprCallFact,
		ConstexprCallKeyHash> constexpr_call_facts_;
	std::unordered_map<BindingId, std::int64_t>
		constant_conversion_return_values_;
	std::vector<EntityId> associated_entities_;
	std::vector<ScopeId> associated_scopes_;
	std::vector<TypeId> associated_type_scratch_;
	std::vector<std::uint32_t> associated_entity_marks_;
	std::vector<std::uint32_t> associated_scope_marks_;
	std::vector<std::uint32_t> associated_type_marks_;
	std::vector<std::uint32_t> candidate_marks_;
	mutable std::vector<std::uint8_t> empty_destructor_chain_cache_;
	LanguageLinkage current_language_linkage_;
	TypeId current_return_type_;
	EntityId current_class_context_;
	BindingId current_function_context_;
	BracedInitializationContext* braced_initialization_context_;
	std::size_t current_pack_alignment_;
	std::vector<std::size_t> pack_alignment_stack_;
	std::size_t loop_depth_;
	std::size_t switch_depth_;
	std::size_t exception_handler_depth_;
	std::vector<ScopeId> exception_cleanup_stops_;
	std::vector<ScopeId> exception_handler_cleanup_stops_;
	std::size_t unevaluated_depth_;
	std::size_t decltype_operand_depth_;
	std::size_t conditionally_evaluated_operand_depth_;
	std::size_t constant_evaluation_suppressed_depth_;
	std::size_t resolved_call_demand_suppressed_depth_;
	std::size_t constant_expression_required_depth_;
	std::size_t constant_initializer_required_depth_;
	std::size_t local_constant_initializer_depth_;
	std::size_t preserve_constant_initializer_recipe_depth_;
	std::size_t constexpr_evaluation_depth_;
	std::size_t constexpr_evaluation_steps_;
	std::uint64_t next_constexpr_storage_identity_;
	std::size_t expression_count_;
	std::uint32_t associated_generation_;
	std::uint32_t candidate_generation_;
	std::size_t associated_scope_visits_;
	std::size_t associated_declaration_visits_;
	std::size_t function_candidate_index_visits_;
	std::size_t overload_candidates_;
	std::size_t overload_order_comparisons_;
	mutable std::size_t conversion_checks_;
	std::size_t call_conversion_cache_hits_;
	std::size_t call_conversion_cache_misses_;
	std::size_t braced_fact_cache_hits_;
	std::size_t braced_fact_cache_misses_;
	std::size_t function_signature_lookups_;
	std::size_t polymorphic_classes_;
	std::size_t virtual_slots_;
	std::size_t virtual_signature_lookups_;
	std::size_t virtual_overrides_;
	mutable std::size_t virtual_slot_lookups_;
	std::size_t vtable_demands_;
	mutable std::size_t access_checks_;
	mutable std::size_t access_path_visits_;
	mutable std::size_t access_grant_probes_;
	std::size_t template_specialization_requests_;
	std::size_t template_specialization_cache_hits_;
	std::size_t function_template_default_materializations_;
	std::size_t function_template_default_request_cache_hits_;
	std::size_t function_template_default_failure_cache_hits_;
	std::size_t function_template_exception_specification_requests_;
	std::size_t function_template_exception_specification_cache_hits_;
	std::size_t function_template_exception_specification_evaluations_;
	mutable std::size_t template_partial_candidates_;
	mutable std::size_t template_partial_order_comparisons_;
	std::size_t template_partial_shape_materializations_;
	std::size_t template_partial_shape_cache_hits_;
	mutable std::size_t template_partial_deduction_visits_;
	mutable std::size_t function_template_deduction_visits_;
	std::size_t lambda_closure_requests_;
	std::size_t lambda_closure_cache_hits_;
	std::size_t constexpr_call_requests_;
	std::size_t constexpr_call_cache_hits_;
	std::size_t constant_conversion_fact_requests_;
	std::size_t constant_conversion_fact_cache_hits_;
	mutable std::size_t constexpr_local_index_probes_;
	mutable std::size_t constexpr_scope_index_probes_;
	mutable std::size_t constexpr_object_projection_visits_;
	std::size_t constexpr_step_visits_;
	std::size_t constexpr_max_depth_;
	std::size_t constexpr_peak_locals_;
	std::size_t constexpr_scratch_peak_nodes_;
	std::size_t demand_worklist_pushes_;
	std::size_t demanded_function_emissions_;
	std::size_t default_constructor_emissions_;
	std::size_t class_layouts_;
	std::size_t class_layout_member_visits_;
	std::size_t class_zero_offset_subobject_visits_;
	mutable std::size_t special_member_fact_lookups_;
	mutable std::size_t special_member_subobject_visits_;
	std::uint32_t zero_offset_subobject_generation_;
	std::uint32_t empty_constructor_chain_generation_;
	std::size_t empty_constructor_chain_requests_;
	std::size_t empty_constructor_chain_cache_hits_;
	std::size_t empty_constructor_chain_entity_visits_;
	std::size_t empty_constructor_chain_dependency_edges_;
	std::size_t constructor_member_action_visits_;
	std::size_t constructor_base_action_visits_;
	std::size_t constructor_delegation_action_visits_;
	std::size_t destructor_subobject_action_visits_;
	std::size_t lexical_cleanup_action_visits_;
	std::size_t unwind_cleanup_scope_visits_;
	std::size_t unwind_cleanup_action_visits_;
	mutable std::size_t enclosing_lifetime_queries_;
	mutable std::size_t initializer_list_lifetime_queries_;
	std::size_t temporary_dependency_visits_;
	std::size_t materialized_demand_visits_;
	mutable std::size_t nonthrowing_action_visits_;
	std::size_t runtime_initializer_visits_;
	std::size_t static_constant_initializer_visits_;
	std::size_t static_constant_dependency_edges_;
	mutable std::size_t empty_destructor_chain_visits_;
	mutable std::size_t empty_destructor_chain_cache_hits_;
	std::size_t anonymous_enum_count_;
	std::size_t local_type_count_;
	std::vector<std::uint32_t> range_for_hidden_count_by_function_;
	std::vector<std::uint32_t> branch_cleanup_node_epochs_;
	std::vector<std::uint32_t> branch_cleanup_binding_epochs_;
	std::vector<std::uint32_t> branch_cleanup_binding_uses_;
	std::vector<std::int8_t> branch_cleanup_literal_truth_;
	std::uint32_t branch_cleanup_scan_epoch_;
};

}
}
