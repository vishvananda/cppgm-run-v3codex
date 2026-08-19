#include "pa11_semantic.h"

#include "pa10_syntax.h"
#include "pa10_syntax_model.h"
#include "pa11_model.h"
#include "hosted_extension_semantic.h"

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace
{

using namespace pa10_syntax_detail;
using namespace pa11;

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
	TypeId type;
	ParameterInfo(NameId name_value, TypeId type_value)
		: name(name_value), type(type_value) {}
};

struct DeclaratorInfo
{
	NameId name;
	TypeId type;
	std::vector<ParameterInfo> parameters;
	DeclaratorInfo() : name(0), type(kNoType) {}
};

struct ConstantValue
{
	std::int64_t value;
	TypeId type;
	bool lvalue;
	BindingId binding;
	ConstantValue()
		: value(0), type(kNoType), lvalue(false), binding(kNoBinding) {}
};

class TypeAnalyzer : public SyntaxTreeConsumer
{
public:
	TypeAnalyzer(std::ostream& output, TypeAnalysisStats* stats)
		: arena_(0), output_(output), stats_(stats), program_(0) {}

	void Consume(const SyntaxArena& arena, NodeId root)
	{
		arena_ = &arena;
		Program program(arena.SharedStrings());
		program_ = &program;
		const std::chrono::steady_clock::time_point analysis_started =
			std::chrono::steady_clock::now();
		for (std::uint32_t edge = arena.FirstEdge(root); edge != kNoEdge;
			edge = arena.NextEdge(edge))
			AnalyzeDeclaration(arena.EdgeChild(edge), program_->GlobalScope());
		const std::chrono::steady_clock::time_point render_started =
			std::chrono::steady_clock::now();
		program_->Render(output_, stats_ ? &stats_->max_scope_depth : 0,
			stats_ ? &stats_->render_stack_storage_bytes : 0,
			stats_ ? &stats_->rendered_type_nodes : 0);
		if (stats_)
		{
			const std::chrono::steady_clock::time_point finished =
				std::chrono::steady_clock::now();
			stats_->interned_names = program_->names.Size();
			stats_->canonical_types = program_->types.Size();
			stats_->scopes = program_->ScopeCount();
			stats_->declarations = program_->bindings.size();
			stats_->lookup_queries = program_->lookup_queries;
			stats_->lookup_scope_visits = program_->lookup_scope_visits;
			stats_->lookup_edge_visits = program_->lookup_edge_visits;
			stats_->name_index_probes = program_->name_index_probes;
			stats_->type_index_probes = program_->types.IndexProbes();
			stats_->using_index_probes = program_->using_index_probes;
			stats_->semantic_storage_bytes = program_->StorageBytes();
			stats_->analysis_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					render_started - analysis_started).count());
			stats_->render_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					finished - render_started).count());
		}
		program_ = 0;
	}

private:
	NodeId FindChild(NodeId node, SyntaxTagCode tag) const;
	NodeId FirstSemanticChild(NodeId node) const;
	std::string PayloadSource(NodeId node) const;
	bool CanContainBlockOrDeclaration(NodeId node) const;
	NamePath ParseNamePath(const std::string& spelling,
		TypeNamePathParseFamily family);
	LookupResult LookupSpelling(ScopeId scope, const std::string& spelling,
		LookupKind kind, TypeNamePathParseFamily family);
	ScopeId ResolveScopeSpelling(ScopeId scope, const std::string& spelling,
		TypeNamePathParseFamily family);
	ScopeId ResolveOwner(ScopeId scope, const NamePath& name);
	bool IsDeclaration(NodeId node) const;
	void AnalyzeDeclaration(NodeId node, ScopeId scope);
	void AnalyzeNamespace(NodeId node, ScopeId scope);
	void AnalyzeUsing(NodeId node, ScopeId scope);
	void AnalyzeTemplate(NodeId node, ScopeId scope);
	void AnalyzeSimple(NodeId node, ScopeId scope);
	void AnalyzeFunction(NodeId node, ScopeId scope);
	void AnalyzeCompound(NodeId node, ScopeId scope);
	void WalkStatement(NodeId node, ScopeId scope);
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
	ConstantValue Evaluate(NodeId node, ScopeId scope);
	ConstantValue EvaluateBinary(NodeId node, ScopeId scope);
	ConstantValue EvaluateUnary(NodeId node, ScopeId scope);
	ConstantValue EvaluateTrait(NodeId node, ScopeId scope);
	TypeId DecltypeType(NodeId node, ScopeId scope);
	std::int64_t ParseInteger(const std::string& spelling) const;
	std::int64_t ApplyBinary(const std::string& operation,
		std::int64_t left, std::int64_t right) const;
	NamedFlavor ClassFlavor(NodeId node) const;
	EntityId NamedEntity(TypeId type) const;
	bool IsClassFlavor(NamedFlavor flavor) const;

	const SyntaxArena* arena_;
	std::ostream& output_;
	TypeAnalysisStats* stats_;
	Program* program_;
};

NodeId TypeAnalyzer::FindChild(NodeId node, SyntaxTagCode tag) const
{
	return arena_->FindDirectChildTag(node, tag);
}

NodeId TypeAnalyzer::FirstSemanticChild(NodeId node) const
{
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (!arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_STRUCTURED_TYPE_NAME)) return child;
	}
	return kNoNode;
}

std::string TypeAnalyzer::PayloadSource(NodeId node) const
{
	return arena_->SemanticPayload(node);
}

bool TypeAnalyzer::CanContainBlockOrDeclaration(NodeId node) const
{
	return arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_LABELED_STATEMENT) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_CASE_STATEMENT) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_DEFAULT_STATEMENT) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_IF_STATEMENT) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_SWITCH_STATEMENT) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_WHILE_STATEMENT) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_DO_STATEMENT) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_FOR_STATEMENT) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_FOR_INIT_STATEMENT) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_TRY_BLOCK) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_HANDLER) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_THEN) || arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_ELSE);
}

NamePath TypeAnalyzer::ParseNamePath(const std::string& spelling,
	TypeNamePathParseFamily family)
{
	if (stats_)
	{
		++stats_->name_path_parse_requests;
		++stats_->name_path_parse_families[family];
	}
	NamePath result;
	std::size_t first = 0;
	result.global = spelling.size() >= 2 && spelling[0] == ':' &&
		spelling[1] == ':';
	if (result.global) first = 2;
	std::size_t part_count = 1;
	for (std::size_t scan = first; (scan = spelling.find("::", scan)) !=
		std::string::npos; scan += 2) ++part_count;
	result.Reserve(part_count);
	while (first < spelling.size())
	{
		const std::size_t separator = spelling.find("::", first);
		const std::size_t last = separator == std::string::npos ?
			spelling.size() : separator;
		if (last == first)
			throw std::runtime_error("invalid qualified name");
		result.Push(program_->names.InternRange(spelling, first, last - first));
		if (stats_) ++stats_->name_path_parse_components;
		if (separator == std::string::npos) break;
		first = separator + 2;
	}
	if (stats_ && result.Size() == 1)
		++stats_->name_path_single_component_parses;
	return result;
}

LookupResult TypeAnalyzer::LookupSpelling(ScopeId scope,
	const std::string& spelling, LookupKind kind,
	TypeNamePathParseFamily family)
{
	if (stats_)
	{
		++stats_->lookup_spelling_requests;
		++stats_->lookup_spelling_families[family];
	}
	if (spelling.find("::") == std::string::npos)
		return program_->LookupName(scope, program_->names.Intern(spelling), kind);
	return program_->Lookup(scope, ParseNamePath(spelling, family), kind);
}

ScopeId TypeAnalyzer::ResolveScopeSpelling(ScopeId scope,
	const std::string& spelling, TypeNamePathParseFamily family)
{
	return program_->ResolveScope(scope, ParseNamePath(spelling, family));
}

ScopeId TypeAnalyzer::ResolveOwner(ScopeId scope, const NamePath& name)
{
	if (!name.global && name.Size() <= 1) return scope;
	NamePath owner = name;
	if (!owner.Empty()) owner.Pop();
	if (owner.Empty())
		return owner.global ? program_->GlobalScope() : scope;
	return program_->ResolveScope(scope, owner);
}

bool TypeAnalyzer::IsDeclaration(NodeId node) const
{
	return arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_SIMPLE_DECLARATION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_FUNCTION_DEFINITION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_ALIAS_DECLARATION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_USING_DECLARATION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_USING_DIRECTIVE) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_NAMESPACE_DEFINITION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_NAMESPACE_ALIAS_DEFINITION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_TEMPLATE_DECLARATION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_CLASS_SPECIFIER) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_CLASS_FORWARD_DECLARATION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_ENUM_SPECIFIER) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_STATIC_ASSERT_DECLARATION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_EMPTY_DECLARATION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_LINKAGE_SPECIFICATION);
}

void TypeAnalyzer::AnalyzeDeclaration(NodeId node, ScopeId scope)
{
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_EMPTY_DECLARATION)) return;
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_NAMESPACE_DEFINITION))
	{
		AnalyzeNamespace(node, scope);
		return;
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_NAMESPACE_ALIAS_DEFINITION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_USING_DIRECTIVE) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_USING_DECLARATION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_ALIAS_DECLARATION))
	{
		AnalyzeUsing(node, scope);
		return;
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_TEMPLATE_DECLARATION))
	{
		AnalyzeTemplate(node, scope);
		return;
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_SIMPLE_DECLARATION))
	{
		AnalyzeSimple(node, scope);
		return;
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_FUNCTION_DEFINITION))
	{
		AnalyzeFunction(node, scope);
		return;
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_CLASS_SPECIFIER) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_CLASS_FORWARD_DECLARATION))
	{
		AnalyzeClass(node, scope, std::string(), false);
		return;
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_ENUM_SPECIFIER))
	{
		AnalyzeEnum(node, scope, std::string(), false);
		return;
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_STATIC_ASSERT_DECLARATION))
	{
		const ConstantValue value = Evaluate(FirstSemanticChild(node), scope);
		if (value.value == 0) throw std::runtime_error("static assertion failed");
		return;
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_LINKAGE_SPECIFICATION))
	{
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
			AnalyzeDeclaration(arena_->EdgeChild(edge), scope);
		return;
	}
	throw std::runtime_error("unsupported PA11 declaration: " +
		arena_->Tag(node));
}

void TypeAnalyzer::AnalyzeNamespace(NodeId node, ScopeId scope)
{
	const std::string spelling = arena_->Payload(node);
	const bool unnamed = spelling.empty() || spelling == "<unnamed>";
	const NameId name = program_->names.Intern(
		unnamed ? "<unnamed>" : spelling);
	const ScopeId child = program_->OpenNamespace(scope, name,
		FindChild(node, ::cppgm::pa10_syntax_detail::STAG_INLINE) != kNoNode, unnamed);
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId declaration = arena_->EdgeChild(edge);
		if (IsDeclaration(declaration)) AnalyzeDeclaration(declaration, child);
	}
}

void TypeAnalyzer::AnalyzeUsing(NodeId node, ScopeId scope)
{
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_ALIAS_DECLARATION))
	{
		const NodeId type_id = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_TYPE_ID);
		const TypeId type = BuildTypeId(type_id, scope);
		const NameId name = program_->names.UseInterned(arena_->PayloadId(node));
		program_->AddBinding(scope, BIND_TYPE_ALIAS, name, type);
		return;
	}
	const NodeId target_node = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_TARGET);
	if (target_node == kNoNode) throw std::runtime_error("missing using target");
	const std::string target = arena_->Payload(target_node);
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_NAMESPACE_ALIAS_DEFINITION))
	{
		const ScopeId target_scope = ResolveScopeSpelling(
			scope, target, TYPE_NAME_PATH_PARSE_USING);
		if (target_scope == kNoScope)
			throw std::runtime_error("namespace alias target is not a namespace");
		program_->AddNamespaceAlias(scope,
			program_->names.UseInterned(arena_->PayloadId(node)), target_scope);
		return;
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_USING_DIRECTIVE))
	{
		const ScopeId target_scope = ResolveScopeSpelling(
			scope, target, TYPE_NAME_PATH_PARSE_USING);
		if (target_scope == kNoScope)
			throw std::runtime_error("using-directive target is not a namespace");
		program_->AddUsingEdge(scope, target_scope);
		return;
	}
	if (target.find('<') != std::string::npos)
		throw std::runtime_error("using-declaration names a template-id");
	const NamePath target_name = ParseNamePath(
		target, TYPE_NAME_PATH_PARSE_USING);
	const NameId name = target_name.Last();
	const LookupResult type = program_->Lookup(scope, target_name, LOOKUP_TYPE);
	if (type.type != kNoType)
	{
		program_->AddBinding(scope,
			program_->types.IsNamed(type.type) ? BIND_TYPE : BIND_TYPE_ALIAS,
			name, type.type, false, 0, NAMED_NONE, 0,
			type.type_declaration_canonical);
		return;
	}
	const LookupResult value =
		program_->Lookup(scope, target_name, LOOKUP_ORDINARY);
	if (value.ordinary == kNoBinding)
	{
		if (hosted_extension::HasGnuAttribute(
			*arena_, node, "__using_if_exists__")) return;
		throw std::runtime_error("using-declaration target was not found");
	}
	const BindingRecord& source = program_->bindings[value.ordinary];
	program_->AddBinding(scope, source.kind, name, source.type,
		source.constant, source.value, source.display_flavor,
		source.display_type_name, source.canonical);
}

void TypeAnalyzer::AnalyzeTemplate(NodeId node, ScopeId scope)
{
	const ScopeId parameters = program_->NewScope(scope,
		SCOPE_TEMPLATE_PARAMETERS, 0);
	const NodeId clause = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_TEMPLATE_PARAMETER_CLAUSE);
	const NodeId list = clause == kNoNode ? kNoNode :
		FindChild(clause, ::cppgm::pa10_syntax_detail::STAG_TEMPLATE_PARAMETER_LIST);
	if (list != kNoNode)
	{
		for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId parameter = arena_->EdgeChild(edge);
			if (!arena_->IsTag(parameter, ::cppgm::pa10_syntax_detail::STAG_TYPE_PARAMETER))
				throw std::runtime_error("non-type template parameter in PA11");
			const NodeId identifier = FindChild(parameter, ::cppgm::pa10_syntax_detail::STAG_IDENTIFIER);
			if (identifier == kNoNode) continue;
			const NameId name =
				program_->names.UseInterned(arena_->PayloadId(identifier));
			const NamedFlavor flavor =
				FindChild(parameter, ::cppgm::pa10_syntax_detail::STAG_TEMPLATE_TEMPLATE_PARAMETER) != kNoNode ?
				NAMED_TEMPLATE_PARAMETER : NAMED_TYPENAME_PARAMETER;
			const EntityId entity = program_->NewEntity(name, flavor, true,
				kNoType, parameters);
			program_->AddBinding(parameters, BIND_TYPE, name,
				program_->entities[entity].type);
		}
	}
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (child != clause && IsDeclaration(child))
			AnalyzeDeclaration(child, parameters);
	}
}

NamedFlavor TypeAnalyzer::ClassFlavor(NodeId node) const
{
	const NodeId key = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_CLASS_KEY);
	if (key == kNoNode) throw std::runtime_error("class without class-key");
	const std::string spelling = PayloadSource(key);
	if (spelling == "struct") return NAMED_STRUCT;
	if (spelling == "class") return NAMED_CLASS;
	if (spelling == "union") return NAMED_UNION;
	throw std::runtime_error("invalid class-key");
}

bool TypeAnalyzer::IsClassFlavor(NamedFlavor flavor) const
{
	return flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
		flavor == NAMED_UNION;
}

EntityId TypeAnalyzer::NamedEntity(TypeId type) const
{
	type = program_->types.RemoveTopCv(type);
	const TypeRecord& record = program_->types.Get(type);
	return record.kind == TYPE_NAMED ? record.entity : kNoEntity;
}

TypeId TypeAnalyzer::AnalyzeClass(NodeId node, ScopeId scope,
	const std::string& hint, bool elaborated)
{
	const NamedFlavor flavor = ClassFlavor(node);
	std::string spelling = arena_->Payload(node);
	if (spelling.empty()) spelling = hint;
	const bool definition = arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_CLASS_SPECIFIER) &&
		(arena_->Flags(node) & SYNTAX_FLAG_DEFINITION) != 0;
	if (spelling.empty() && flavor == NAMED_UNION)
	{
		spelling = "__anonymous_union_type__" +
			std::to_string(arena_->TokenFirst(node)) + "_" +
			std::to_string(arena_->TokenLast(node));
	}
	if (spelling.empty()) throw std::runtime_error("unnamed class has no owner");
	const NamePath declared_name = ParseNamePath(
		spelling, TYPE_NAME_PATH_PARSE_CLASS);
	const bool qualified = declared_name.global || declared_name.Size() > 1;
	const NameId name = declared_name.Last();
	const NameId display_name = qualified ? program_->names.Intern(spelling) : name;
	const ScopeId semantic_owner = ResolveOwner(scope, declared_name);
	if (semantic_owner == kNoScope)
		throw std::runtime_error("qualified class owner was not found");
	EntityId entity = kNoEntity;
	const LookupResult existing = qualified ?
		program_->LookupDirect(semantic_owner, name, LOOKUP_TYPE) :
		(elaborated ? program_->LookupName(scope, name, LOOKUP_TYPE) :
		 program_->LookupDirect(scope, name, LOOKUP_TYPE));
	if (existing.type != kNoType)
	{
		entity = NamedEntity(existing.type);
		if (entity == kNoEntity || !IsClassFlavor(program_->entities[entity].flavor))
			throw std::runtime_error("class redeclared as a different type");
		const bool old_union = program_->entities[entity].flavor == NAMED_UNION;
		if (old_union != (flavor == NAMED_UNION))
			throw std::runtime_error("union/non-union redeclaration mismatch");
		if (definition) program_->entities[entity].complete = true;
	}
	else
	{
		if (qualified)
			throw std::runtime_error("qualified class was not declared");
		entity = program_->NewEntity(name, flavor, definition,
			kNoType, semantic_owner);
		program_->SetTypeName(semantic_owner, name,
			program_->entities[entity].type);
	}
	const TypeId type = program_->entities[entity].type;
	const bool anonymous_union = arena_->Payload(node).empty() &&
		hint.empty() && flavor == NAMED_UNION;
	if (!elaborated && !anonymous_union && !qualified)
		program_->AddBinding(semantic_owner, BIND_TYPE, name, type,
			false, 0, flavor);
	else if (existing.type == kNoType && !anonymous_union)
		program_->AddBinding(semantic_owner, BIND_TYPE, name, type,
			false, 0, flavor);
	else if (!elaborated && qualified)
		program_->AddOutputTypeBinding(scope, display_name, type, flavor);
	if (definition)
	{
		ScopeId member_scope = program_->entities[entity].member_scope;
		if (member_scope == kNoScope)
		{
			member_scope = program_->NewScope(semantic_owner, SCOPE_CLASS,
				qualified ? display_name : name, entity,
				qualified ? scope : kNoScope);
			program_->SetEntityScope(entity, member_scope);
		}
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId member = arena_->EdgeChild(edge);
			if (IsDeclaration(member)) AnalyzeDeclaration(member, member_scope);
		}
		if (anonymous_union)
		{
			const std::size_t count = program_->bindings.size();
			for (BindingId binding = 0; binding < count; ++binding)
			{
				const BindingRecord source = program_->bindings[binding];
				if (source.owner != member_scope ||
					source.kind != BIND_VARIABLE) continue;
				program_->AddBinding(scope, source.kind, source.name, source.type,
					source.constant, source.value, source.display_flavor,
					source.display_type_name, source.canonical);
			}
		}
	}
	return type;
}

TypeId TypeAnalyzer::AnalyzeEnum(NodeId node, ScopeId scope,
	const std::string& hint, bool elaborated)
{
	std::string spelling = arena_->Payload(node);
	if (spelling.empty()) spelling = hint;
	if (spelling.empty()) throw std::runtime_error("unnamed enum has no owner");
	const bool scoped = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_ENUM_KEY) != kNoNode;
	const NamedFlavor flavor = scoped ? NAMED_ENUM_CLASS : NAMED_ENUM;
	const bool definition =
		(arena_->Flags(node) & SYNTAX_FLAG_DEFINITION) != 0;
	const NamePath declared_name = ParseNamePath(
		spelling, TYPE_NAME_PATH_PARSE_ENUM);
	const bool qualified = declared_name.global || declared_name.Size() > 1;
	const NameId name = declared_name.Last();
	const NameId display_name = qualified ? program_->names.Intern(spelling) : name;
	const ScopeId semantic_owner = ResolveOwner(scope, declared_name);
	if (semantic_owner == kNoScope)
		throw std::runtime_error("qualified enum owner was not found");
	const NodeId underlying_node = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_TYPE_ID);
	const bool explicit_underlying = underlying_node != kNoNode;
	const TypeId underlying = explicit_underlying ?
		BuildTypeId(underlying_node, semantic_owner) :
		program_->types.Fundamental(FUND_INT);
	const LookupResult existing = qualified ?
		program_->LookupDirect(semantic_owner, name, LOOKUP_TYPE) :
		(elaborated ? program_->LookupName(scope, name, LOOKUP_TYPE) :
		 program_->LookupDirect(scope, name, LOOKUP_TYPE));
	if (elaborated)
	{
		if (existing.type == kNoType)
			throw std::runtime_error("elaborated enum was not declared");
		const EntityId entity = NamedEntity(existing.type);
		if (entity == kNoEntity ||
			(program_->entities[entity].flavor != NAMED_ENUM &&
			 program_->entities[entity].flavor != NAMED_ENUM_CLASS))
			throw std::runtime_error("elaborated enum names a non-enum");
		return existing.type;
	}
	if (qualified && existing.type == kNoType)
		throw std::runtime_error("qualified enum was not declared");
	if (!definition && !scoped && !explicit_underlying)
		throw std::runtime_error("opaque unscoped enum without underlying type");
	EntityId entity = kNoEntity;
	if (existing.type != kNoType)
	{
		entity = NamedEntity(existing.type);
		if (entity == kNoEntity ||
			(program_->entities[entity].flavor != NAMED_ENUM &&
			 program_->entities[entity].flavor != NAMED_ENUM_CLASS) ||
			program_->entities[entity].flavor != flavor)
			throw std::runtime_error("incompatible enum redeclaration");
		if (explicit_underlying &&
			program_->entities[entity].underlying != underlying)
			throw std::runtime_error("enum underlying type changed");
		if (definition) program_->entities[entity].complete = true;
	}
	else
	{
		entity = program_->NewEntity(name, flavor,
			definition || scoped || explicit_underlying, underlying,
			semantic_owner);
		program_->SetTypeName(semantic_owner, name,
			program_->entities[entity].type);
	}
	const TypeId type = program_->entities[entity].type;
	if (qualified)
		program_->AddOutputTypeBinding(scope, display_name, type, flavor);
	else program_->AddBinding(semantic_owner, BIND_TYPE, name, type,
		false, 0, flavor);
	ScopeId enum_scope = program_->entities[entity].member_scope;
	if (scoped && qualified && definition)
	{
		enum_scope = program_->NewScope(semantic_owner, SCOPE_ENUM,
			display_name, entity, scope);
		program_->SetEntityScope(entity, enum_scope);
	}
	else if (scoped && enum_scope == kNoScope)
	{
		enum_scope = program_->NewScope(scope, SCOPE_ENUM, name, entity);
		program_->SetEntityScope(entity, enum_scope);
	}
	std::int64_t next_value = 0;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId enumerator = arena_->EdgeChild(edge);
		if (!arena_->IsTag(enumerator, ::cppgm::pa10_syntax_detail::STAG_ENUMERATOR)) continue;
		const NodeId initializer = FirstSemanticChild(enumerator);
		const ScopeId value_scope = scoped ? enum_scope : scope;
		const std::int64_t value = initializer == kNoNode ? next_value :
			Evaluate(initializer, value_scope).value;
		const NameId enumerator_name =
			program_->names.UseInterned(arena_->PayloadId(enumerator));
		program_->AddBinding(value_scope, BIND_ENUMERATOR, enumerator_name,
			type, true, value, qualified ? flavor : NAMED_NONE,
			qualified ? display_name : 0);
		if (value == INT64_MAX)
			throw std::runtime_error("enumerator value overflow");
		next_value = value + 1;
	}
	return type;
}

SpecInfo TypeAnalyzer::BuildSpecifiers(NodeId node, ScopeId scope,
	const std::string& hint, bool has_declarators)
{
	SpecInfo result;
	std::uint8_t cv = CV_NONE;
	bool is_unsigned = false;
	bool is_signed = false;
	bool is_short = false;
	int longs = 0;
	bool is_char = false;
	bool is_void = false;
	bool is_bool = false;
	bool is_float = false;
	bool is_double = false;
	bool is_wchar = false;
	bool is_char16 = false;
	bool is_char32 = false;
	bool saw_int = false;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_ATOMIC_TYPE_SPECIFIER))
		{
			const TypeId underlying = BuildTypeId(
				FirstSemanticChild(child), scope);
			if (program_->types.IsAtomic(underlying))
				throw std::runtime_error("nested _Atomic type");
			result.type = program_->types.Qualify(underlying, CV_ATOMIC);
			continue;
		}
		const bool class_specifier = arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_CLASS_SPECIFIER);
		const bool class_forward =
			arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_CLASS_FORWARD_DECLARATION);
		if (class_specifier || class_forward)
		{
			result.type = AnalyzeClass(child, scope, hint,
				has_declarators && class_forward);
			continue;
		}
		if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_ENUM_SPECIFIER))
		{
			const bool definition =
				(arena_->Flags(child) & SYNTAX_FLAG_DEFINITION) != 0;
			result.type = AnalyzeEnum(child, scope, hint,
				has_declarators && !definition && arena_->Payload(child).size() != 0);
			continue;
		}
		if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_DECLTYPE_SPECIFIER) ||
			(arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_DECL_SPECIFIER) &&
			 FirstSemanticChild(child) != kNoNode))
		{
			result.type = DecltypeType(FirstSemanticChild(child), scope);
			continue;
		}
		if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_CV_QUALIFIER) ||
			arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_DECL_SPECIFIER) ||
			arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_TYPE_SPECIFIER) ||
			arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_TYPE_NAME))
		{
			const std::string spelling = PayloadSource(child);
			if (spelling == "typedef") result.is_typedef = true;
			else if (spelling == "constexpr") result.is_constexpr = true;
			else if (spelling == "const") cv |= CV_CONST;
			else if (spelling == "volatile") cv |= CV_VOLATILE;
			else if (spelling == "unsigned") is_unsigned = true;
			else if (spelling == "signed") is_signed = true;
			else if (spelling == "short") is_short = true;
			else if (spelling == "long") ++longs;
			else if (spelling == "int") saw_int = true;
			else if (spelling == "char") is_char = true;
			else if (spelling == "void") is_void = true;
			else if (spelling == "bool") is_bool = true;
			else if (spelling == "float") is_float = true;
			else if (spelling == "double") is_double = true;
			else if (spelling == "wchar_t") is_wchar = true;
			else if (spelling == "char16_t") is_char16 = true;
			else if (spelling == "char32_t") is_char32 = true;
			else if (spelling == "__int128_t")
				result.type = program_->types.Fundamental(FUND_INT128);
			else if (spelling == "__uint128_t")
				result.type = program_->types.Fundamental(FUND_UINT128);
			else if (spelling != "extern" && spelling != "static" &&
				spelling != "thread_local" && spelling != "inline" &&
				spelling != "virtual")
			{
				const LookupResult found =
					LookupSpelling(scope, spelling, LOOKUP_TYPE,
						TYPE_NAME_PATH_PARSE_TYPE_LOOKUP);
				if (found.type == kNoType)
					throw std::runtime_error("unknown type name: " + spelling);
				result.type = found.type;
			}
		}
	}
	result.type = hosted_extension::ApplyIntegerSignedness(
		program_->types, result.type, is_unsigned);
	if (result.type == kNoType)
	{
		FundamentalKind fundamental = FUND_INT;
		if (is_void) fundamental = FUND_VOID;
		else if (is_bool) fundamental = FUND_BOOL;
		else if (is_wchar) fundamental = FUND_WCHAR_T;
		else if (is_char16) fundamental = FUND_CHAR16_T;
		else if (is_char32) fundamental = FUND_CHAR32_T;
		else if (is_float) fundamental = FUND_FLOAT;
		else if (is_double && longs != 0) fundamental = FUND_LONG_DOUBLE;
		else if (is_double) fundamental = FUND_DOUBLE;
		else if (is_char && is_unsigned) fundamental = FUND_UNSIGNED_CHAR;
		else if (is_char && is_signed) fundamental = FUND_SIGNED_CHAR;
		else if (is_char) fundamental = FUND_CHAR;
		else if (is_short && is_unsigned) fundamental = FUND_UNSIGNED_SHORT_INT;
		else if (is_short) fundamental = FUND_SHORT_INT;
		else if (longs > 1 && is_unsigned)
			fundamental = FUND_UNSIGNED_LONG_LONG_INT;
		else if (longs > 1) fundamental = FUND_LONG_LONG_INT;
		else if (longs == 1 && is_unsigned) fundamental = FUND_UNSIGNED_LONG_INT;
		else if (longs == 1) fundamental = FUND_LONG_INT;
		else if (is_unsigned) fundamental = FUND_UNSIGNED_INT;
		else if (saw_int || is_signed) fundamental = FUND_INT;
		else throw std::runtime_error("declaration has no type specifier");
		result.type = program_->types.Fundamental(fundamental);
	}
	result.type = program_->types.Qualify(result.type, cv);
	return result;
}

TypeId TypeAnalyzer::BuildTypeId(NodeId node, ScopeId scope)
{
	if (node == kNoNode) throw std::runtime_error("missing type-id");
	NodeId specifiers = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_TYPE_SPECIFIER_SEQ);
	if (specifiers == kNoNode)
		specifiers = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_DECL_SPECIFIER_SEQ);
	if (specifiers == kNoNode) throw std::runtime_error("type-id has no type");
	const SpecInfo spec = BuildSpecifiers(specifiers, scope, std::string(), false);
	NodeId declarator = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_ABSTRACT_DECLARATOR);
	if (declarator == kNoNode) declarator = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_DECLARATOR);
	return declarator == kNoNode ? spec.type :
		BuildDeclarator(declarator, spec.type, scope).type;
}

NameId TypeAnalyzer::DeclaratorName(NodeId node)
{
	return DeclaratorNamePath(node).Last();
}

NamePath TypeAnalyzer::DeclaratorNamePath(NodeId node)
{
	const NodeId identifier = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_IDENTIFIER);
	if (identifier != kNoNode)
		return ParseNamePath(arena_->Payload(identifier),
			TYPE_NAME_PATH_PARSE_DECLARATOR);
	const NodeId nested = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_NESTED_DECLARATOR);
	return nested == kNoNode ? NamePath() :
		DeclaratorNamePath(FirstSemanticChild(nested));
}

std::vector<ParameterInfo> TypeAnalyzer::BuildParameters(NodeId node,
	ScopeId scope, bool* variadic)
{
	std::vector<ParameterInfo> result;
	*variadic = false;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_PARAMETER_PACK))
		{
			*variadic = true;
			continue;
		}
		if (!arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_PARAMETER_DECLARATION)) continue;
		const NodeId specifiers = FindChild(child, ::cppgm::pa10_syntax_detail::STAG_DECL_SPECIFIER_SEQ);
		const NodeId declarator = FindChild(child, ::cppgm::pa10_syntax_detail::STAG_DECLARATOR);
		const SpecInfo spec = BuildSpecifiers(specifiers, scope,
			std::string(), declarator != kNoNode);
		if (declarator == kNoNode)
			result.push_back(ParameterInfo(0, spec.type));
		else
		{
			const DeclaratorInfo parsed =
				BuildDeclarator(declarator, spec.type, scope);
			result.push_back(ParameterInfo(parsed.name, parsed.type));
		}
	}
	if (result.size() == 1 && result[0].name == 0)
	{
		const TypeRecord& type = program_->types.Get(result[0].type);
		if (type.kind == TYPE_FUNDAMENTAL && type.fundamental == FUND_VOID)
			result.clear();
	}
	return result;
}

DeclaratorInfo TypeAnalyzer::BuildDeclarator(NodeId node, TypeId base,
	ScopeId scope)
{
	DeclaratorInfo result;
	result.name = DeclaratorName(node);
	TypeId type = base;
	std::vector<NodeId> suffixes;
	NodeId nested = kNoNode;
	std::uint8_t function_cv = CV_NONE;
	std::uint8_t function_ref = FUNCTION_REF_NONE;
	bool saw_function_suffix = false;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_PTR_OPERATOR))
		{
			const std::string operation = PayloadSource(child);
			if (operation == "*") type = program_->types.Pointer(type);
			else if (operation == "^") type = program_->types.BlockPointer(type);
			else if (operation == "&")
				type = program_->types.Reference(TYPE_LVALUE_REFERENCE, type);
			else if (operation == "&&")
				type = program_->types.Reference(TYPE_RVALUE_REFERENCE, type);
			else throw std::runtime_error("member pointer outside PA11");
		}
		else if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_CV_QUALIFIER))
		{
			const std::string qualifier = PayloadSource(child);
			const std::uint8_t flag = qualifier == "const" ?
				CV_CONST : CV_VOLATILE;
			if (saw_function_suffix) function_cv |= flag;
			else type = program_->types.Qualify(type, flag);
		}
		else if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_REF_QUALIFIER))
		{
			if (function_ref != FUNCTION_REF_NONE)
				throw std::runtime_error("duplicate function ref-qualifier");
			function_ref = PayloadSource(child) == "&" ?
				FUNCTION_REF_LVALUE : FUNCTION_REF_RVALUE;
		}
		else if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_NESTED_DECLARATOR))
			nested = FirstSemanticChild(child);
		else if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_ARRAY_SUFFIX) ||
			arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_PARAMETER_CLAUSE))
		{
			suffixes.push_back(child);
			if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_PARAMETER_CLAUSE))
				saw_function_suffix = true;
		}
	}
	for (std::size_t i = suffixes.size(); i != 0; --i)
	{
		const NodeId suffix = suffixes[i - 1];
		if (arena_->IsTag(suffix, ::cppgm::pa10_syntax_detail::STAG_ARRAY_SUFFIX))
		{
			const NodeId bound_node = FirstSemanticChild(suffix);
			if (bound_node == kNoNode)
				throw std::runtime_error("incomplete PA11 array type");
			const std::int64_t bound = Evaluate(bound_node, scope).value;
			if (bound <= 0) throw std::runtime_error("non-positive array bound");
			type = program_->types.Array(type, static_cast<std::uint64_t>(bound));
		}
		else
		{
			bool variadic = false;
			const std::vector<ParameterInfo> parameters =
				BuildParameters(suffix, scope, &variadic);
			std::vector<TypeId> parameter_types;
			for (std::size_t p = 0; p < parameters.size(); ++p)
				parameter_types.push_back(parameters[p].type);
			type = program_->types.Function(type, parameter_types, variadic,
				function_cv, function_ref);
			result.parameters = parameters;
		}
	}
	if (nested != kNoNode)
	{
		DeclaratorInfo inner = BuildDeclarator(nested, type, scope);
		if (!result.parameters.empty()) inner.parameters = result.parameters;
		return inner;
	}
	result.type = type;
	return result;
}

void TypeAnalyzer::AnalyzeSimple(NodeId node, ScopeId scope)
{
	const NodeId specifiers = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_DECL_SPECIFIER_SEQ);
	const NodeId list = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_INIT_DECLARATOR_LIST);
	std::string hint;
	if (list != kNoNode)
	{
		const NodeId item = FirstSemanticChild(list);
		const NodeId declarator = item == kNoNode ? kNoNode :
			FindChild(item, ::cppgm::pa10_syntax_detail::STAG_DECLARATOR);
		if (declarator != kNoNode)
			hint = program_->names.Get(DeclaratorName(declarator));
	}
	const SpecInfo spec = BuildSpecifiers(specifiers, scope, hint,
		list != kNoNode);
	if (list == kNoNode) return;
	for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId item = arena_->EdgeChild(edge);
		const NodeId declarator = FindChild(item, ::cppgm::pa10_syntax_detail::STAG_DECLARATOR);
		DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, scope);
		if (parsed.name == 0) throw std::runtime_error("unnamed declaration");
		if (spec.is_typedef)
		{
			program_->AddBinding(scope, BIND_TYPE_ALIAS, parsed.name, parsed.type);
			continue;
		}
		if (program_->types.IsFunction(parsed.type))
		{
			program_->AddBinding(scope, BIND_FUNCTION, parsed.name, parsed.type);
			continue;
		}
		if (spec.is_constexpr)
			parsed.type = program_->types.Qualify(parsed.type, CV_CONST);
		const NodeId initializer = FindChild(item, ::cppgm::pa10_syntax_detail::STAG_INITIALIZER);
		bool constant = false;
		std::int64_t value = 0;
		const TypeRecord& top = program_->types.Get(parsed.type);
		if (initializer != kNoNode &&
			(spec.is_constexpr || top.kind == TYPE_QUALIFIED))
		{
			const NodeId expression = FirstSemanticChild(initializer);
			if (expression != kNoNode)
			{
				value = Evaluate(expression, scope).value;
				constant = true;
			}
		}
		program_->AddBinding(scope, BIND_VARIABLE, parsed.name, parsed.type,
			constant, value);
	}
}

void TypeAnalyzer::AnalyzeFunction(NodeId node, ScopeId scope)
{
	const NodeId specifiers = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_DECL_SPECIFIER_SEQ);
	const NodeId declarator = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_DECLARATOR);
	const NamePath full_name = DeclaratorNamePath(declarator);
	ScopeId owner = scope;
	if (full_name.global || full_name.Size() > 1)
	{
		NamePath owner_name = full_name;
		owner_name.Pop();
		owner = owner_name.Empty() && owner_name.global ?
			program_->GlobalScope() : program_->ResolveScope(scope, owner_name);
		if (owner == kNoScope)
			throw std::runtime_error("qualified function owner was not found");
	}
	const SpecInfo spec = BuildSpecifiers(specifiers, owner,
		std::string(), true);
	DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, owner);
	parsed.name = full_name.Last();
	if (!program_->types.IsFunction(parsed.type))
		throw std::runtime_error("function definition has non-function type");
	program_->AddBinding(owner, BIND_FUNCTION, parsed.name, parsed.type);
	const ScopeId function_scope = program_->NewScope(owner, SCOPE_FUNCTION,
		parsed.name);
	for (std::size_t i = 0; i < parsed.parameters.size(); ++i)
		program_->AddBinding(function_scope, BIND_PARAMETER,
			parsed.parameters[i].name, parsed.parameters[i].type);
	const NodeId body = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_COMPOUND_STATEMENT);
	if (body != kNoNode) AnalyzeCompound(body, function_scope);
}

void TypeAnalyzer::AnalyzeCompound(NodeId node, ScopeId scope)
{
	const ScopeId block = program_->NewScope(scope, SCOPE_BLOCK, 0);
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (IsDeclaration(child)) AnalyzeDeclaration(child, block);
		else if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_COMPOUND_STATEMENT))
			AnalyzeCompound(child, block);
		else if (CanContainBlockOrDeclaration(child))
			WalkStatement(child, block);
	}
}

void TypeAnalyzer::WalkStatement(NodeId node, ScopeId scope)
{
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_COMPOUND_STATEMENT))
			AnalyzeCompound(child, scope);
		else if (IsDeclaration(child)) AnalyzeDeclaration(child, scope);
		else if (CanContainBlockOrDeclaration(child))
			WalkStatement(child, scope);
	}
}

std::int64_t TypeAnalyzer::ParseInteger(const std::string& spelling) const
{
	std::size_t last = spelling.size();
	while (last != 0 && (spelling[last - 1] == 'u' ||
		spelling[last - 1] == 'U' || spelling[last - 1] == 'l' ||
		spelling[last - 1] == 'L')) --last;
	const std::string digits = spelling.substr(0, last);
	errno = 0;
	char* end = 0;
	const unsigned long long value = std::strtoull(digits.c_str(), &end, 0);
	if (errno == ERANGE || end == digits.c_str() || *end != '\0' ||
		value > static_cast<unsigned long long>(INT64_MAX))
		throw std::runtime_error("integer literal is outside PA11 range");
	return static_cast<std::int64_t>(value);
}

std::int64_t TypeAnalyzer::ApplyBinary(const std::string& operation,
	std::int64_t left, std::int64_t right) const
{
	if (operation == "+")
	{
		if ((right > 0 && left > INT64_MAX - right) ||
			(right < 0 && left < INT64_MIN - right))
			throw std::runtime_error("signed constant overflow");
		return left + right;
	}
	if (operation == "-")
	{
		if ((right < 0 && left > INT64_MAX + right) ||
			(right > 0 && left < INT64_MIN + right))
			throw std::runtime_error("signed constant overflow");
		return left - right;
	}
	if (operation == "*")
	{
		if (left != 0 && right != 0 &&
			(left == INT64_MIN || right == INT64_MIN ||
			 std::llabs(left) > INT64_MAX / std::llabs(right)))
			throw std::runtime_error("signed constant overflow");
		return left * right;
	}
	if (operation == "/" || operation == "%")
	{
		if (right == 0) throw std::runtime_error("division by zero");
		if (left == INT64_MIN && right == -1)
			throw std::runtime_error("signed constant overflow");
		return operation == "/" ? left / right : left % right;
	}
	if (operation == "<<" || operation == ">>")
	{
		if (right < 0 || right >= 63) throw std::runtime_error("invalid shift");
		if (operation == ">>") return left >> right;
		if (left < 0 || left > (INT64_MAX >> right))
			throw std::runtime_error("invalid left shift");
		return left << right;
	}
	if (operation == "==") return left == right;
	if (operation == "!=") return left != right;
	if (operation == "<") return left < right;
	if (operation == ">") return left > right;
	if (operation == "<=") return left <= right;
	if (operation == ">=") return left >= right;
	if (operation == "&") return left & right;
	if (operation == "|") return left | right;
	if (operation == "^") return left ^ right;
	throw std::runtime_error("unsupported constant binary operator");
}

ConstantValue TypeAnalyzer::EvaluateBinary(NodeId node, ScopeId scope)
{
	const std::uint32_t first_edge = arena_->FirstEdge(node);
	if (first_edge == kNoEdge) throw std::runtime_error("binary expression empty");
	const NodeId left_node = arena_->EdgeChild(first_edge);
	const std::uint32_t second_edge = arena_->NextEdge(first_edge);
	if (second_edge == kNoEdge) throw std::runtime_error("binary expression unary");
	const NodeId right_node = arena_->EdgeChild(second_edge);
	const std::string operation = PayloadSource(node);
	ConstantValue result = Evaluate(left_node, scope);
	if (operation == "&&")
	{
		if (result.value == 0) result.value = 0;
		else result.value = Evaluate(right_node, scope).value != 0;
		result.type = program_->types.Fundamental(FUND_BOOL);
		return result;
	}
	if (operation == "||")
	{
		if (result.value != 0) result.value = 1;
		else result.value = Evaluate(right_node, scope).value != 0;
		result.type = program_->types.Fundamental(FUND_BOOL);
		return result;
	}
	const ConstantValue right = Evaluate(right_node, scope);
	result.value = ApplyBinary(operation, result.value, right.value);
	result.type = program_->types.Fundamental(FUND_INT);
	result.lvalue = false;
	result.binding = kNoBinding;
	return result;
}

ConstantValue TypeAnalyzer::EvaluateUnary(NodeId node, ScopeId scope)
{
	ConstantValue result = Evaluate(FirstSemanticChild(node), scope);
	const std::string operation = PayloadSource(node);
	if (operation == "+") return result;
	if (operation == "-")
	{
		if (result.value == INT64_MIN)
			throw std::runtime_error("signed constant overflow");
		result.value = -result.value;
	}
	else if (operation == "!") result.value = !result.value;
	else if (operation == "~") result.value = ~result.value;
	else throw std::runtime_error("unsupported constant unary operator");
	result.lvalue = false;
	result.binding = kNoBinding;
	return result;
}

ConstantValue TypeAnalyzer::EvaluateTrait(NodeId node, ScopeId scope)
{
	const NodeId operand = FirstSemanticChild(node);
	if (operand == kNoNode) throw std::runtime_error("empty type trait");
	TypeId type = kNoType;
	if (arena_->IsTag(operand, ::cppgm::pa10_syntax_detail::STAG_TYPE_ID)) type = BuildTypeId(operand, scope);
	else if (arena_->IsTag(operand, ::cppgm::pa10_syntax_detail::STAG_ID_EXPRESSION))
	{
		const LookupResult named = LookupSpelling(scope,
			arena_->Payload(operand), LOOKUP_TYPE,
			TYPE_NAME_PATH_PARSE_EXPRESSION);
		if (named.type != kNoType) type = named.type;
	}
	if (type == kNoType) type = Evaluate(operand, scope).type;
	ConstantValue result;
	result.type = program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	result.value = arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_SIZEOF_EXPRESSION) ?
		static_cast<std::int64_t>(program_->SizeOf(type)) :
		static_cast<std::int64_t>(program_->AlignOf(type));
	return result;
}

ConstantValue TypeAnalyzer::Evaluate(NodeId node, ScopeId scope)
{
	if (node == kNoNode) throw std::runtime_error("missing constant expression");
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_LITERAL))
	{
		ConstantValue result;
		result.value = ParseInteger(arena_->Payload(node));
		result.type = program_->types.Fundamental(FUND_INT);
		return result;
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_KEYWORD_LITERAL))
	{
		ConstantValue result;
		const std::string value = PayloadSource(node);
		if (value != "true" && value != "false")
			throw std::runtime_error("non-integral keyword literal");
		result.value = value == "true";
		result.type = program_->types.Fundamental(FUND_BOOL);
		return result;
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_ID_EXPRESSION))
	{
		const LookupResult found = LookupSpelling(scope,
			arena_->Payload(node), LOOKUP_ORDINARY,
			TYPE_NAME_PATH_PARSE_EXPRESSION);
		if (found.ordinary == kNoBinding)
			throw std::runtime_error("constant name was not found");
		const BindingRecord& binding = program_->bindings[found.ordinary];
		if (!binding.constant)
			throw std::runtime_error("name is not a constant expression");
		ConstantValue result;
		result.value = binding.value;
		result.type = binding.type;
		result.lvalue = binding.kind != BIND_ENUMERATOR;
		result.binding = found.ordinary;
		return result;
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_PARENTHESIZED_EXPRESSION))
		return Evaluate(FirstSemanticChild(node), scope);
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_BINARY_EXPRESSION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_ASSIGNMENT_EXPRESSION))
		return EvaluateBinary(node, scope);
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_UNARY_EXPRESSION))
		return EvaluateUnary(node, scope);
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_SIZEOF_EXPRESSION) ||
		arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_TYPE_TRAIT_EXPRESSION))
		return EvaluateTrait(node, scope);
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_CAST_EXPRESSION))
	{
		const NodeId type_id = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_TYPE_ID);
		ConstantValue result;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (child != type_id) result = Evaluate(child, scope);
		}
		result.type = BuildTypeId(type_id, scope);
		const TypeRecord& target = program_->types.Get(
			program_->types.RemoveTopCv(result.type));
		if (target.kind == TYPE_FUNDAMENTAL &&
			target.fundamental == FUND_UNSIGNED_CHAR)
			result.value &= 0xff;
		result.lvalue = false;
		return result;
	}
	throw std::runtime_error("unsupported PA11 constant expression: " +
		arena_->Tag(node));
}

TypeId TypeAnalyzer::DecltypeType(NodeId node, ScopeId scope)
{
	if (node == kNoNode) throw std::runtime_error("empty decltype");
	bool parenthesized = false;
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_PARENTHESIZED_EXPRESSION))
	{
		parenthesized = true;
		node = FirstSemanticChild(node);
	}
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_ID_EXPRESSION))
	{
		const LookupResult found = LookupSpelling(scope,
			arena_->Payload(node), LOOKUP_ORDINARY,
			TYPE_NAME_PATH_PARSE_EXPRESSION);
		if (found.ordinary == kNoBinding)
			throw std::runtime_error("decltype name was not found");
		const BindingRecord& binding = program_->bindings[found.ordinary];
		if (!parenthesized || binding.kind == BIND_ENUMERATOR)
			return binding.type;
		return program_->types.Reference(TYPE_LVALUE_REFERENCE, binding.type);
	}
	const ConstantValue value = Evaluate(node, scope);
	return value.lvalue ?
		program_->types.Reference(TYPE_LVALUE_REFERENCE, value.type) : value.type;
}

}

TypeAnalysisStats::TypeAnalysisStats()
	: tokens(0), syntax_nodes(0), interned_names(0), canonical_types(0),
	  scopes(0), declarations(0), lookup_queries(0), lookup_scope_visits(0),
	  lookup_edge_visits(0), name_index_probes(0), type_index_probes(0),
	  using_index_probes(0), name_path_parse_requests(0),
	  name_path_parse_components(0), name_path_single_component_parses(0),
	  lookup_spelling_requests(0), structured_name_path_requests(0),
	  syntax_name_path_requests(0), syntax_name_path_direct(0),
	  syntax_name_path_fallbacks(0), rendered_type_nodes(0), max_scope_depth(0),
	  render_stack_storage_bytes(0), semantic_storage_bytes(0),
	  peak_stage_storage_bytes(0), analysis_nanoseconds(0),
	  render_nanoseconds(0), elapsed_nanoseconds(0)
{
	for (std::size_t family = 0;
		family < TYPE_NAME_PATH_PARSE_FAMILY_COUNT; ++family)
	{
		name_path_parse_families[family] = 0;
		lookup_spelling_families[family] = 0;
	}
}

void WriteTypeTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, TypeAnalysisStats* stats)
{
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	if (stats) *stats = TypeAnalysisStats();
	SyntaxStats syntax_stats;
	TypeAnalyzer analyzer(output, stats);
	ConsumeSyntaxTranslationUnit(path, source, options, analyzer,
		stats ? &syntax_stats : 0);
	if (stats)
	{
		stats->preprocessing = syntax_stats.preprocessing;
		stats->tokens = syntax_stats.tokens;
		stats->syntax_nodes = syntax_stats.syntax_nodes;
		stats->peak_stage_storage_bytes = source.size() +
			syntax_stats.token_storage_bytes + syntax_stats.syntax_storage_bytes +
			syntax_stats.parser_storage_bytes + stats->semantic_storage_bytes +
			stats->render_stack_storage_bytes;
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count());
	}
}

}
