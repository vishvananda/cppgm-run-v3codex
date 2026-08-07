#pragma once

#include "pa10_syntax.h"
#include "pa10_syntax_model.h"
#include "pa11_model.h"
#include "pa12_semantic.h"
#include "pa12_semantic_model.h"
#include "pa12_semantic_tables.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

using namespace pa10_syntax_detail;
using namespace pa11;

class SemanticAnalyzer : public SyntaxTreeConsumer
{
public:
	SemanticAnalyzer(std::ostream& output, SemanticAnalysisStats* stats,
		SemanticGraphConsumer* graph_consumer = 0, bool render_output = true)
		: arena_(0), output_(output), stats_(stats), program_(0),
		  graph_consumer_(graph_consumer), render_output_(render_output),
		  root_(kNoDumpEdge), current_language_linkage_(LANGUAGE_LINKAGE_CPP),
		  current_return_type_(kNoType), current_class_context_(kNoEntity),
		  current_function_context_(kNoBinding),
		  current_pack_alignment_(0),
		  loop_depth_(0), switch_depth_(0), unevaluated_depth_(0),
		  expression_count_(0),
		  associated_generation_(0), candidate_generation_(0),
		  associated_scope_visits_(0), associated_declaration_visits_(0),
		  overload_candidates_(0), overload_order_comparisons_(0),
		  conversion_checks_(0), call_conversion_cache_hits_(0),
		  call_conversion_cache_misses_(0), function_signature_lookups_(0),
		  access_checks_(0), access_path_visits_(0),
		  access_grant_probes_(0),
		  template_specialization_requests_(0),
		  template_specialization_cache_hits_(0),
		  demand_worklist_pushes_(0), demanded_function_emissions_(0),
		  default_constructor_emissions_(0),
		  class_layouts_(0), class_layout_member_visits_(0),
		  class_zero_offset_subobject_visits_(0),
		  special_member_fact_lookups_(0),
		  special_member_subobject_visits_(0),
		  zero_offset_subobject_generation_(0),
		  constructor_member_action_visits_(0),
		  constructor_base_action_visits_(0),
		  destructor_subobject_action_visits_(0),
		  lexical_cleanup_action_visits_(0),
		  anonymous_enum_count_(0), local_type_count_(0) {}

	void Consume(const SyntaxArena& arena, NodeId root);

private:
	NodeId FindChild(NodeId node, const char* tag) const;
	NodeId FirstSemanticChild(NodeId node) const;
	std::string PayloadSource(NodeId node) const;
	NamePath ParseNamePath(const std::string& spelling);
	LookupResult LookupPath(ScopeId scope, const NamePath& path,
		LookupKind kind);
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
	bool HasInternalLinkageScope(ScopeId scope) const;
	bool IsDeclaration(NodeId node) const;

	void AnalyzeDeclaration(NodeId node, ScopeId scope,
		std::uint32_t output_parent, bool local);
	void AnalyzeNamespace(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeUsing(NodeId node, ScopeId scope,
		std::uint32_t output_parent, bool local,
		AccessKind access = ACCESS_PUBLIC);
	void AnalyzeTemplate(NodeId node, ScopeId scope);
	void AnalyzeSimple(NodeId node, ScopeId scope,
		std::uint32_t output_parent, bool local);
	void PublishVariableDeclarationFacts(BindingId binding,
		ScopeId declaration_scope, NameId name, TypeId type,
		const SpecInfo& spec, bool local);
	void AnalyzeFunction(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeCompound(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeStatement(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeReturnStatement(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeSubstatement(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeCondition(NodeId node, ScopeId scope,
		std::uint32_t output_parent, bool switch_condition);
	void RegisterConditionLifetime(ScopeId scope, BindingId object,
		TypeId type, const ExpressionInfo& initializer,
		std::uint32_t condition);

	TypeId AnalyzeClass(NodeId node, ScopeId scope,
		const std::string& hint, bool elaborated);
	TypeId AnalyzeEnum(NodeId node, ScopeId scope,
		const std::string& hint, bool elaborated);
	SpecInfo BuildSpecifiers(NodeId node, ScopeId scope,
		const std::string& hint, bool has_declarators);
	TypeId BuildTypeId(NodeId node, ScopeId scope);
	DeclaratorInfo BuildDeclarator(NodeId node, TypeId base, ScopeId scope,
		bool placeholder_auto = false);
	std::vector<ParameterInfo> BuildParameters(NodeId node, ScopeId scope,
		bool* variadic);
	NameId DeclaratorName(NodeId node);
	NamePath DeclaratorNamePath(NodeId node);
	TypeId AdjustParameterType(TypeId type);
	TypeId DecltypeType(NodeId node, ScopeId scope);

	BindingId DeclareFunction(ScopeId owner, NameId name, TypeId type,
		const std::vector<ParameterInfo>& parameters, bool definition,
		bool template_specialization = false,
		StorageClass storage_class = STORAGE_CLASS_NONE,
		LanguageLinkage language_linkage = LANGUAGE_LINKAGE_CPP,
		bool nonthrowing = false, bool ordinary_visible = true);
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
	int CompareReferenceBindings(const ExpressionInfo& argument,
		TypeId left, TypeId right) const;
	std::vector<BindingId> FunctionCandidates(ScopeId scope,
		const std::string& spelling, EntityId* naming_class = 0);
	std::vector<BindingId> FunctionSet(BindingId binding);
	std::vector<std::size_t> FindFunctionTemplates(ScopeId scope,
		const std::string& spelling);
	bool ParseExplicitTemplateArguments(ScopeId scope,
		const std::string& spelling, std::string* base,
		std::vector<TypeId>* arguments);
	TypeId ResolveTemplateTypeArgument(ScopeId scope,
		const std::string& spelling);
	BindingId InstantiateFunctionTemplate(std::size_t pattern,
		const std::vector<TypeId>& arguments);
	void DeduceFunctionTemplates(ScopeId scope, const std::string& spelling,
		const std::vector<ExpressionInfo>& arguments);
	void DemandFunction(BindingId binding);
	TypeId AdaptMemberFunctionType(BindingId binding);
	void EmitDefaultConstructor(EntityId entity);
	void EmitDemandedFunction(BindingId binding);
	const FunctionInfo& GetFunction(BindingId binding) const;
	FunctionInfo& GetMutableFunction(BindingId binding);

	ExpressionInfo AnalyzeExpression(NodeId node, ScopeId scope,
		TypeId target = kNoType);
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
		const std::vector<CallConversionFact>* argument_conversions = 0);
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
	ExpressionInfo ApplyExplicitConversion(ExpressionInfo value, TypeId target);
	ExpressionInfo BuildConvertingArgument(const ExpressionInfo& source,
		TypeId target, const CallConversionFact& conversion);
	bool IsDirectTrivialClassValueType(TypeId type) const;
	ExpressionInfo BuildDirectClassValueTransfer(
		const ExpressionInfo& source, TypeId target);
	ExpressionInfo AnalyzeVariableInitializer(NodeId initializer,
		ScopeId scope, TypeId type, bool local);
	ExpressionInfo AnalyzeCall(NodeId node, ScopeId scope, TypeId target);
	TypeId ResolveFunctionalCastType(ScopeId scope,
		const std::string& spelling);
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
		std::vector<BindingId>* candidates);
	bool TryAnalyzeOverloadedOperator(const std::string& operation,
		ScopeId scope, const std::vector<NodeId>& operand_syntax,
		const std::vector<ExpressionInfo>& operands, bool member_only,
		TypeId target, ExpressionInfo* result,
		const std::vector<ConversionRank>* competing_builtin_ranks = 0);
	bool TryAnalyzeCallOperator(ScopeId scope, const ExpressionInfo& callee,
		const std::vector<NodeId>& argument_syntax,
		const std::vector<ExpressionInfo>* analyzed_arguments, TypeId target,
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
	void AppendDirectFunctionCandidates(ScopeId owner, NameId name,
		std::vector<BindingId>* candidates);
	void AppendHiddenFriendCandidates(EntityId owner, NameId name,
		std::vector<BindingId>* candidates);
	ExpressionInfo AnalyzeUnary(NodeId node, ScopeId scope,
		TypeId target = kNoType);
	ExpressionInfo AnalyzeBinary(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeAssignment(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeCast(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeConditional(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeSubscript(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeSizeof(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeBracedInit(NodeId node, ScopeId scope, TypeId target);
	ExpressionInfo AnalyzeAggregateInit(TypeId type, ScopeId scope,
		std::uint32_t* element_edge);
	ExpressionInfo AnalyzeArrayAggregateInit(TypeId type, ScopeId scope,
		std::uint32_t* element_edge);
	ExpressionInfo AnalyzeAggregateElement(TypeId type, ScopeId scope,
		std::uint32_t* element_edge);
	ExpressionInfo BuildLocalAggregateArrayActions(
		const ExpressionInfo& initializer);
	std::uint32_t BuildAggregateConstructionAction(TypeId type,
		std::uint32_t aggregate_list);
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
		TypeId target);
	ExpressionInfo AnalyzeMember(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeImplicitDataMember(BindingId member, ScopeId scope,
		TypeId target, EntityId naming_class);
	void AnalyzeClassMember(NodeId node, ScopeId scope, TypeId owner_type,
		AccessKind access);
	void AnalyzeBitField(NodeId node, ScopeId scope, TypeId owner_type,
		AccessKind access);
	void AnalyzeSpecialMember(NodeId node, ScopeId scope, TypeId owner_type,
		AccessKind access);
	void AnalyzeConversionFunction(NodeId node, ScopeId scope,
		TypeId owner_type, AccessKind access);
	void AnalyzeOutOfClassSpecialMember(NodeId node, ScopeId scope);
	void RegisterClassSpecialMember(BindingId binding);
	void ConfigureAssignmentSpecialMember(BindingId binding,
		NodeId initializer);
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
	std::size_t BaseProjectionCount(TypeId source, TypeId target) const;
	EntityId ZeroOffsetClassEntity(TypeId type) const;
	bool VisitZeroOffsetSubobjects(EntityId root, std::uint32_t marker,
		std::uint32_t conflict_marker);
	bool ZeroOffsetSubobjectConflict(EntityId base, TypeId member_type);
	void CompleteClassLayout(EntityId entity);
	std::size_t RequestedAlignment(NodeId node, ScopeId scope);
	void InheritConstructors(EntityId entity,
		const std::vector<BindingId>& constructors);
	BindingId EnsureConstructorBaseEntry(BindingId constructor);
	BindingId EnsureDestructorBaseEntry(BindingId destructor);
	void EnsureStaticMemberStorage(BindingId member);
	BindingId EnsureImplicitConstructor(EntityId entity);
	BindingId EnsureImplicitDestructor(EntityId entity);
	const std::vector<BindingId>& ConstructorCandidates(EntityId entity) const;
	BindingId DestructorForType(TypeId type) const;
	bool IsEmptyUnionDestructor(BindingId destructor) const;
	EntityId DestructedEntity(TypeId type) const;
	BindingId SelectConstructor(ScopeId scope,
		const std::vector<NodeId>& argument_syntax,
		const std::vector<ExpressionInfo>& arguments,
		const std::vector<BindingId>& candidates, bool copy_initialization,
		bool list_initialization);
	bool EmptyDefaultConstructorChain(BindingId constructor,
		std::vector<BindingId>* base_entries);
	std::uint32_t BuildConstructorAction(TypeId type, ScopeId scope,
		const std::vector<NodeId>& argument_syntax, bool copy_initialization,
		bool list_initialization, bool base_subobject = false,
		bool demand = true);
	std::uint32_t BuildClassValueConstructorAction(TypeId type,
		const ExpressionInfo& source, bool copy_initialization = true,
		bool demand = true);
	void ValidateClassValueConstruction(TypeId type,
		const ExpressionInfo& source, bool copy_initialization = true);
	void FinalizeNamedReturnSlot(std::uint32_t function);
	std::uint32_t BuildDefaultConstructorAction(TypeId type, ScopeId scope);
	void AddConstructorMemberActions(const FunctionInfo& constructor,
		ScopeId function_scope, const std::vector<BindingId>& parameters,
		std::uint32_t body);
	void AddBaseInitializationAction(EntityId entity, NodeId initializer,
		ScopeId scope, std::uint32_t body);
	void AddMemberInitializationAction(BindingId member, NodeId initializer,
		ScopeId scope, std::uint32_t body);
	bool InitializationActionsAreNonthrowing(std::uint32_t body) const;
	void AddDefaultConstructor(std::uint32_t variable, BindingId binding,
		TypeId type);
	void AddDestructorSubobjectActions(EntityId entity, std::uint32_t body);
	void AddLifetimeObligation(ScopeId scope, BindingId object, TypeId type);
	void AddTemporaryLifetimeObligation(ScopeId scope,
		std::uint32_t temporary);
	void CollectTemporaryObjects(std::uint32_t node,
		std::vector<std::uint32_t>* temporaries) const;
	bool HasControlDependentTemporary(std::uint32_t node) const;
	void AppendFullExpressionDestructionActions(std::uint32_t expression,
		std::uint32_t output_parent);
	void AddNamespaceObjectAction(std::uint32_t variable, BindingId object,
		TypeId type, std::uint32_t initializer);
	void AppendScopeDestructionActions(ScopeId scope,
		std::uint32_t output_parent, ScopeId stop_exclusive = kNoScope);
	std::uint32_t MakeDestructorAction(TypeId type, BindingId destructor,
		BindingId object, std::uint32_t base_projections = 0);
	std::uint32_t MakeTemporaryDestructorAction(std::uint32_t temporary,
		BindingId destructor = kNoBinding);
	EntityId EntityOf(TypeId type) const;
	ExpressionInfo MakeLiteral(TypeId type, NameId text,
		ValueCategory category = VALUE_PRVALUE);
	ExpressionInfo MakeStringLiteral(const std::string& spelling,
		std::size_t* character_count = 0);
	bool TryAnalyzeUserDefinedStringLiteral(const std::string& spelling,
		ScopeId scope, TypeId target, ExpressionInfo* result);
	ExpressionInfo AnalyzeThisExpression(ScopeId scope);
	bool IsNonthrowing(NodeId declarator, ScopeId scope);
	void RecordExpressionFacts(const ExpressionInfo& value);
	ExpressionInfo ApplyTarget(ExpressionInfo value, TypeId target,
		ConversionRank known_conversion = CONVERSION_INVALID);
	ConversionRank MemberObjectConversion(const ExpressionInfo& source,
		TypeId target, BindingId member) const;
	ExpressionInfo ApplyMemberObjectTarget(ExpressionInfo value,
		TypeId target, BindingId member,
		const ObjectConversionFact* conversion_fact = 0);
	ConversionRank Conversion(TypeId source, ValueCategory category,
		bool integer_zero, TypeId target) const;
	ConversionRank Conversion(const ExpressionInfo& source, TypeId target) const;
	bool QualificationConversion(TypeId source, TypeId target) const;
	bool SimilarUnqualified(TypeId source, TypeId target) const;
	TypeId EffectiveType(TypeId type) const;
	TypeId Decay(TypeId type) const;
	TypeId CommonArithmeticType(TypeId left, TypeId right) const;
	bool IsIntegral(TypeId type, bool allow_scoped_enum = false) const;
	bool IsFloating(TypeId type) const;
	bool IsArithmetic(TypeId type) const;
	bool IsPointer(TypeId type) const;
	bool IsNullptr(TypeId type) const;
	bool IsVoid(TypeId type) const;
	bool IsConst(TypeId type) const;
	bool IsModifiableLvalue(const ExpressionInfo& value) const;
	FundamentalKind FundamentalOf(TypeId type) const;
	int IntegralRank(TypeId type) const;
	TypeId IntegralPromotionType(TypeId type) const;
	std::int64_t ParseInteger(const std::string& spelling) const;
	std::int64_t ApplyConstantBinary(const std::string& operation,
		std::int64_t left, std::int64_t right) const;
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
	Program* program_;
	SemanticGraphConsumer* graph_consumer_;
	bool render_output_;
	DumpArena dump_;
	std::uint32_t root_;
	std::vector<NameId> scope_prefixes_;
	std::vector<NameId> scope_prefix_segments_;
	std::vector<ScopeId> scope_parents_;
	std::vector<NameId> scope_prefix_scratch_;
	IndexedSequenceTable function_sets_;
	IndexedSequenceTable ordinary_function_sets_;
	IndexedSequenceTable hidden_friend_sets_;
	IndexedSequenceTable friend_class_grants_;
	IndexedSequenceTable friend_function_grants_;
	FunctionSignatureTable function_declarations_;
	FunctionSignatureTable using_function_declarations_;
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
	std::vector<std::uint32_t> variable_node_by_binding_;
	std::vector<ClassSpecialMemberFacts> class_special_members_;
	std::vector<BindingId> implicit_constructor_by_entity_;
	std::vector<BindingId> constructor_base_entry_by_binding_;
	std::vector<BindingId> destructor_base_entry_by_binding_;
	std::vector<std::uint32_t> static_member_storage_by_binding_;
	std::vector<BindingId> entity_destructor_by_entity_;
	std::vector<BindingId> hidden_friend_anchor_by_entity_;
	std::vector<NodeId> member_initializer_by_binding_;
	std::vector<NodeId> constructor_initializer_scratch_;
	std::vector<BindingId> constructor_initializer_touched_;
	std::vector<FunctionTemplatePattern> function_templates_;
	IndexedSequenceTable template_function_sets_;
	TemplateSpecializationTable template_instantiations_;
	std::vector<std::uint32_t> injected_fact_by_binding_;
	std::vector<InjectedMemberInfo> injected_members_;
	std::vector<std::vector<LifetimeObligation> > scope_lifetimes_;
	std::vector<NamespaceObjectAction> namespace_objects_;
	std::vector<AggregateHelperInfo> aggregate_helpers_;
	FunctionSignatureTable aggregate_helper_index_;
	std::vector<ScopeId> break_cleanup_stops_;
	std::vector<ScopeId> continue_cleanup_stops_;
	std::vector<EntityId> demanded_default_constructor_entities_;
	std::vector<std::uint8_t> default_constructor_demand_states_;
	std::vector<BindingId> demanded_functions_;
	std::vector<EntityId> associated_entities_;
	std::vector<ScopeId> associated_scopes_;
	std::vector<TypeId> associated_type_scratch_;
	std::vector<std::uint32_t> associated_entity_marks_;
	std::vector<std::uint32_t> associated_scope_marks_;
	std::vector<std::uint32_t> associated_type_marks_;
	std::vector<std::uint32_t> candidate_marks_;
	LanguageLinkage current_language_linkage_;
	TypeId current_return_type_;
	EntityId current_class_context_;
	BindingId current_function_context_;
	std::size_t current_pack_alignment_;
	std::vector<std::size_t> pack_alignment_stack_;
	std::size_t loop_depth_;
	std::size_t switch_depth_;
	std::size_t unevaluated_depth_;
	std::size_t expression_count_;
	std::uint32_t associated_generation_;
	std::uint32_t candidate_generation_;
	std::size_t associated_scope_visits_;
	std::size_t associated_declaration_visits_;
	std::size_t overload_candidates_;
	std::size_t overload_order_comparisons_;
	mutable std::size_t conversion_checks_;
	std::size_t call_conversion_cache_hits_;
	std::size_t call_conversion_cache_misses_;
	std::size_t function_signature_lookups_;
	mutable std::size_t access_checks_;
	mutable std::size_t access_path_visits_;
	mutable std::size_t access_grant_probes_;
	std::size_t template_specialization_requests_;
	std::size_t template_specialization_cache_hits_;
	std::size_t demand_worklist_pushes_;
	std::size_t demanded_function_emissions_;
	std::size_t default_constructor_emissions_;
	std::size_t class_layouts_;
	std::size_t class_layout_member_visits_;
	std::size_t class_zero_offset_subobject_visits_;
	mutable std::size_t special_member_fact_lookups_;
	mutable std::size_t special_member_subobject_visits_;
	std::uint32_t zero_offset_subobject_generation_;
	std::size_t constructor_member_action_visits_;
	std::size_t constructor_base_action_visits_;
	std::size_t destructor_subobject_action_visits_;
	std::size_t lexical_cleanup_action_visits_;
	std::size_t anonymous_enum_count_;
	std::size_t local_type_count_;
};

}
}
