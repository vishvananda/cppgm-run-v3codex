#pragma once

#include "pa10_syntax.h"
#include "pa10_syntax_model.h"
#include "pa11_model.h"
#include "pa12_semantic.h"

#include <cstdint>
#include <iosfwd>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

using namespace pa10_syntax_detail;
using namespace pa11;

enum ValueCategory
{
	VALUE_NONE,
	VALUE_LVALUE,
	VALUE_PRVALUE,
	VALUE_XVALUE
};

enum DumpKind
{
	DUMP_TRANSLATION_UNIT,
	DUMP_NAMESPACE,
	DUMP_TYPE_ALIAS,
	DUMP_VARIABLE,
	DUMP_FUNCTION_DECLARATION,
	DUMP_FUNCTION_DEFINITION,
	DUMP_PARAMETER,
	DUMP_COMPOUND_STATEMENT,
	DUMP_SIMPLE_DECLARATION,
	DUMP_RETURN_STATEMENT,
	DUMP_EXPRESSION_STATEMENT,
	DUMP_IF_STATEMENT,
	DUMP_SWITCH_STATEMENT,
	DUMP_WHILE_STATEMENT,
	DUMP_DO_STATEMENT,
	DUMP_FOR_STATEMENT,
	DUMP_BREAK_STATEMENT,
	DUMP_CONTINUE_STATEMENT,
	DUMP_CONDITION,
	DUMP_CONDITION_DECLARATION,
	DUMP_FOR_INIT_STATEMENT,
	DUMP_ITERATION,
	DUMP_THEN,
	DUMP_ELSE,
	DUMP_CASE_STATEMENT,
	DUMP_DEFAULT_STATEMENT,
	DUMP_CALL_EXPRESSION,
	DUMP_CALLEE,
	DUMP_ID_EXPRESSION,
	DUMP_LITERAL,
	DUMP_UNARY_EXPRESSION,
	DUMP_POSTFIX_EXPRESSION,
	DUMP_BINARY_EXPRESSION,
	DUMP_SUBSCRIPT_EXPRESSION,
	DUMP_CONDITIONAL_EXPRESSION,
	DUMP_SIZEOF_EXPRESSION,
	DUMP_ASSIGNMENT_EXPRESSION,
	DUMP_CAST_EXPRESSION,
	DUMP_BRACED_INIT_LIST,
	DUMP_MEMBER_EXPRESSION,
	DUMP_CONSTRUCTOR_ACTION
};

const std::uint32_t kNoDumpEdge =
	std::numeric_limits<std::uint32_t>::max();

struct DumpNode
{
	DumpKind kind;
	TypeId type;
	ValueCategory category;
	NameId text;
	BindingId binding;
	std::uint32_t first_edge;
	std::uint32_t last_edge;

	explicit DumpNode(DumpKind value)
		: kind(value), type(kNoType), category(VALUE_NONE), text(0),
		  binding(kNoBinding), first_edge(kNoDumpEdge),
		  last_edge(kNoDumpEdge) {}
};

struct DumpEdge
{
	std::uint32_t child;
	std::uint32_t next;
	explicit DumpEdge(std::uint32_t value)
		: child(value), next(kNoDumpEdge) {}
};

class DumpArena
{
public:
	std::uint32_t Make(DumpKind kind)
	{
		if (nodes.size() >= kNoDumpEdge)
			throw std::runtime_error("too many PA12 semantic nodes");
		nodes.push_back(DumpNode(kind));
		return static_cast<std::uint32_t>(nodes.size() - 1);
	}

	void Add(std::uint32_t parent, std::uint32_t child)
	{
		if (edges.size() >= kNoDumpEdge)
			throw std::runtime_error("too many PA12 semantic edges");
		const std::uint32_t edge = static_cast<std::uint32_t>(edges.size());
		edges.push_back(DumpEdge(child));
		DumpNode& owner = nodes[parent];
		if (owner.first_edge == kNoDumpEdge) owner.first_edge = edge;
		else edges[owner.last_edge].next = edge;
		owner.last_edge = edge;
	}

	std::size_t StorageBytes() const
	{
		return nodes.capacity() * sizeof(DumpNode) +
			edges.capacity() * sizeof(DumpEdge);
	}

	std::vector<DumpNode> nodes;
	std::vector<DumpEdge> edges;
};

struct SpecInfo
{
	TypeId type;
	bool is_typedef;
	bool is_constexpr;
	SpecInfo() : type(kNoType), is_typedef(false), is_constexpr(false) {}
};

struct ParameterInfo
{
	NameId name;
	TypeId declared_type;
	TypeId function_type;
	ParameterInfo(NameId name_value, TypeId declared_value,
		TypeId function_value)
		: name(name_value), declared_type(declared_value),
		  function_type(function_value) {}
};

struct DeclaratorInfo
{
	NameId name;
	TypeId type;
	std::vector<ParameterInfo> parameters;
	DeclaratorInfo() : name(0), type(kNoType) {}
};

struct ExpressionInfo
{
	std::uint32_t node;
	TypeId type;
	ValueCategory category;
	BindingId binding;
	bool constant;
	std::int64_t value;
	bool integer_literal_zero;

	ExpressionInfo()
		: node(kNoDumpEdge), type(kNoType), category(VALUE_PRVALUE),
		  binding(kNoBinding), constant(false), value(0),
		  integer_literal_zero(false) {}
};

enum ConversionRank
{
	CONVERSION_EXACT = 0,
	CONVERSION_PROMOTION = 1,
	CONVERSION_STANDARD = 2,
	CONVERSION_BOOLEAN = 3,
	CONVERSION_ELLIPSIS = 4,
	CONVERSION_INVALID = 100
};

struct FunctionInfo
{
	BindingId binding;
	ScopeId owner;
	TypeId type;
	NameId display_name;
	std::vector<ParameterInfo> parameters;
	NodeId definition_body;
	bool defined;
	bool deferred;
	FunctionInfo()
		: binding(kNoBinding), owner(kNoScope), type(kNoType), display_name(0),
		  definition_body(kNoNode), defined(false), deferred(false) {}
};

struct FunctionTemplatePattern
{
	ScopeId owner;
	NameId name;
	NodeId specifiers;
	NodeId declarator;
	std::vector<NameId> type_parameters;

	FunctionTemplatePattern()
		: owner(kNoScope), name(0), specifiers(kNoNode),
		  declarator(kNoNode) {}
};

struct InjectedMemberInfo
{
	BindingId storage;
	NameId member;
	InjectedMemberInfo() : storage(kNoBinding), member(0) {}
	InjectedMemberInfo(BindingId storage_value, NameId member_value)
		: storage(storage_value), member(member_value) {}
};

class SemanticAnalyzer : public SyntaxTreeConsumer
{
public:
	SemanticAnalyzer(std::ostream& output, SemanticAnalysisStats* stats)
		: arena_(0), output_(output), stats_(stats), program_(0),
		  root_(kNoDumpEdge), current_return_type_(kNoType),
		  loop_depth_(0), switch_depth_(0), expression_count_(0),
		  overload_candidates_(0), conversion_checks_(0),
		  anonymous_enum_count_(0), local_type_count_(0) {}

	void Consume(const SyntaxArena& arena, NodeId root);

private:
	NodeId FindChild(NodeId node, const char* tag) const;
	NodeId FirstSemanticChild(NodeId node) const;
	std::string PayloadSource(NodeId node) const;
	NamePath ParseNamePath(const std::string& spelling);
	LookupResult LookupSpelling(ScopeId scope, const std::string& spelling,
		LookupKind kind);
	ScopeId ResolveScopeSpelling(ScopeId scope, const std::string& spelling);
	ScopeId ResolveOwner(ScopeId scope, const NamePath& name);
	std::string ScopePrefix(ScopeId scope) const;
	NameId DisplayName(ScopeId owner, NameId name);
	ScopeId NewScope(ScopeId parent, ScopeKind kind, NameId name,
		const std::string& prefix);
	bool IsDeclaration(NodeId node) const;

	void AnalyzeDeclaration(NodeId node, ScopeId scope,
		std::uint32_t output_parent, bool local);
	void AnalyzeNamespace(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeUsing(NodeId node, ScopeId scope,
		std::uint32_t output_parent, bool local);
	void AnalyzeTemplate(NodeId node, ScopeId scope);
	void AnalyzeSimple(NodeId node, ScopeId scope,
		std::uint32_t output_parent, bool local);
	void AnalyzeFunction(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeCompound(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeStatement(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeSubstatement(NodeId node, ScopeId scope,
		std::uint32_t output_parent);
	void AnalyzeCondition(NodeId node, ScopeId scope,
		std::uint32_t output_parent, bool switch_condition);

	TypeId AnalyzeClass(NodeId node, ScopeId scope,
		const std::string& hint, bool elaborated);
	TypeId AnalyzeEnum(NodeId node, ScopeId scope,
		const std::string& hint, bool elaborated);
	SpecInfo BuildSpecifiers(NodeId node, ScopeId scope,
		const std::string& hint, bool has_declarators);
	TypeId BuildTypeId(NodeId node, ScopeId scope);
	DeclaratorInfo BuildDeclarator(NodeId node, TypeId base, ScopeId scope);
	std::vector<ParameterInfo> BuildParameters(NodeId node, ScopeId scope,
		bool* variadic);
	NameId DeclaratorName(NodeId node);
	NamePath DeclaratorNamePath(NodeId node);
	TypeId AdjustParameterType(TypeId type);
	TypeId DecltypeType(NodeId node, ScopeId scope);

	BindingId DeclareFunction(ScopeId owner, NameId name, TypeId type,
		const std::vector<ParameterInfo>& parameters, bool definition);
	std::vector<BindingId> FunctionCandidates(ScopeId scope,
		const std::string& spelling);
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
	void EmitDemandedFunction(BindingId binding);
	const FunctionInfo& GetFunction(BindingId binding) const;
	bool SameParameterTypes(TypeId left, TypeId right) const;

	ExpressionInfo AnalyzeExpression(NodeId node, ScopeId scope,
		TypeId target = kNoType);
	BindingId SelectOverload(ScopeId scope,
		const std::vector<NodeId>& argument_syntax,
		const std::vector<ExpressionInfo>& arguments,
		const std::vector<BindingId>& candidates);
	ExpressionInfo AnalyzeCall(NodeId node, ScopeId scope, TypeId target);
	ExpressionInfo AnalyzeUnary(NodeId node, ScopeId scope,
		TypeId target = kNoType);
	ExpressionInfo AnalyzeBinary(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeAssignment(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeCast(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeConditional(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeSubscript(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeSizeof(NodeId node, ScopeId scope);
	ExpressionInfo AnalyzeBracedInit(NodeId node, ScopeId scope, TypeId target);
	ExpressionInfo AnalyzeMember(NodeId node, ScopeId scope);
	void AnalyzeClassMember(NodeId node, ScopeId scope, TypeId owner_type);
	void AddDefaultConstructor(std::uint32_t variable, BindingId binding,
		TypeId type);
	EntityId EntityOf(TypeId type) const;
	ExpressionInfo MakeLiteral(TypeId type, NameId text,
		ValueCategory category = VALUE_PRVALUE);
	ExpressionInfo ApplyTarget(ExpressionInfo value, TypeId target);
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
	std::int64_t ParseInteger(const std::string& spelling) const;
	std::int64_t ApplyConstantBinary(const std::string& operation,
		std::int64_t left, std::int64_t right) const;
	NameId InternNumber(std::int64_t value);

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
	DumpArena dump_;
	std::uint32_t root_;
	std::unordered_map<ScopeId, std::string> scope_prefixes_;
	std::unordered_map<ScopeId, ScopeId> scope_parents_;
	std::unordered_map<std::uint64_t, std::vector<BindingId> > function_sets_;
	std::unordered_map<BindingId, FunctionInfo> functions_;
	std::unordered_map<BindingId, TypeId> member_function_owners_;
	std::vector<FunctionTemplatePattern> function_templates_;
	std::unordered_map<std::uint64_t, std::vector<std::size_t> >
		template_function_sets_;
	std::unordered_map<std::string, BindingId> template_instantiations_;
	std::unordered_map<BindingId, InjectedMemberInfo> injected_members_;
	std::vector<TypeId> demanded_default_constructors_;
	std::vector<BindingId> demanded_functions_;
	TypeId current_return_type_;
	std::size_t loop_depth_;
	std::size_t switch_depth_;
	std::size_t expression_count_;
	std::size_t overload_candidates_;
	mutable std::size_t conversion_checks_;
	std::size_t anonymous_enum_count_;
	std::size_t local_type_count_;
};

}
}
