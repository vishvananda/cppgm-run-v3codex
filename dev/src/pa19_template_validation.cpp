#include "pa12_semantic_detail.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

enum RetainedNameKind
{
	RETAINED_TYPE_NAME = 1,
	RETAINED_VALUE_NAME = 2
};

enum RetainedCallLookupState
{
	RETAINED_CALL_LOOKUP_PUBLISHED = 1,
	RETAINED_CALL_ADL_ELIGIBLE = 2
};

struct RetainedScope
{
	ScopeId semantic_scope;
	std::size_t parent;
	std::unordered_map<NameId, std::uint8_t> names;
	std::unordered_map<NameId, std::vector<BindingId> > call_functions;
	std::unordered_map<NameId, std::vector<std::size_t> > call_templates;
	std::unordered_map<NameId, EntityId> call_naming_classes;
	std::unordered_set<NameId> dependent_values;
	bool defer_unknown_members;
	bool unmodeled_fixed_base;
	bool unmodeled_current_class;

	RetainedScope(ScopeId semantic, std::size_t owner, bool defer,
		bool fixed_base, bool current_class)
		: semantic_scope(semantic), parent(owner), defer_unknown_members(defer),
		  unmodeled_fixed_base(fixed_base),
		  unmodeled_current_class(current_class) {}
};

}

class RetainedTemplateValidator
{
public:
	RetainedTemplateValidator(SemanticAnalyzer& analyzer, NodeId target,
		ScopeId lexical_scope, const std::vector<TemplateParameter>& parameters)
		: analyzer_(analyzer), target_(target), lexical_scope_(lexical_scope),
		  parameters_(parameters) {}

	void Run();

private:
	std::size_t AddScope(ScopeId semantic_scope, std::size_t parent,
		bool defer_unknown_members, bool unmodeled_fixed_base = false,
		bool unmodeled_current_class = false);
	std::size_t AddChildScope(std::size_t parent, ScopeKind kind,
		bool defer_unknown_members = false);
	void DeclareParameter(std::size_t scope,
		const TemplateParameter& parameter);
	void Declare(std::size_t scope, NameId name, RetainedNameKind kind,
		bool allow_existing = false);
	std::uint8_t LookupLocal(std::size_t scope, NameId name) const;
	bool LookupLocalCallSets(std::size_t scope, NameId name,
		std::vector<BindingId>* functions,
		std::vector<std::size_t>* templates, EntityId* naming_class) const;
	bool IsDependentValue(std::size_t scope, NameId name) const;
	bool DefersUnknownMembers(std::size_t scope) const;
	bool HasUnmodeledFixedBase(std::size_t scope) const;
	bool HasUnmodeledCurrentClass(std::size_t scope) const;
	bool IsQualifiedMemberDefinition(NodeId node) const;
	bool IsTypedef(NodeId specifiers) const;
	bool HasBaseClass(NodeId node) const;
	bool SyntaxUsesTemplateParameter(NodeId node) const;
	bool SyntaxUsesRetainedType(NodeId node, std::size_t scope) const;
	bool SyntaxUsesRetainedValue(NodeId node, std::size_t scope) const;
	void Visit(NodeId node, std::size_t scope, bool unknown_callee = false);
	void VisitChildren(NodeId node, std::size_t scope);
	void VisitClass(NodeId node, std::size_t scope);
	void PredeclareClassMembers(NodeId node, std::size_t scope);
	void PredeclareClassSimple(NodeId node, std::size_t scope);
	void DeclareEnumValues(NodeId node, std::size_t scope);
	void VisitFunction(NodeId node, std::size_t scope);
	NodeId FindParameterClause(NodeId declarator) const;
	void BindFunctionParameters(NodeId declarator, std::size_t scope);
	void VisitSimple(NodeId node, std::size_t scope, bool predeclared);
	void VisitUsing(NodeId node, std::size_t scope);
	void VisitSizeof(NodeId node, std::size_t scope);
	void VisitIdExpression(NodeId node, std::size_t scope,
		bool unknown_callee);
	void ValidateKnownTemplateArgumentKinds(NodeId node, ScopeId scope);
	const TemplateParameter* TemplateParameterUsedBy(NodeId node) const;
	void ValidateSpecialMemberExceptionSpecification();
	bool IsNonthrowingSyntax(NodeId declarator) const;
	std::size_t ParameterCount(NodeId declarator) const;

	SemanticAnalyzer& analyzer_;
	NodeId target_;
	ScopeId lexical_scope_;
	const std::vector<TemplateParameter>& parameters_;
	std::unordered_set<NameId> parameter_names_;
	std::unordered_set<NodeId> template_argument_validation_visited_;
	std::vector<RetainedScope> scopes_;
};

std::size_t RetainedTemplateValidator::AddScope(ScopeId semantic_scope,
	std::size_t parent, bool defer_unknown_members, bool unmodeled_fixed_base,
	bool unmodeled_current_class)
{
	const std::size_t index = scopes_.size();
	scopes_.push_back(RetainedScope(semantic_scope, parent,
		defer_unknown_members, unmodeled_fixed_base,
		unmodeled_current_class));
	return index;
}

std::size_t RetainedTemplateValidator::AddChildScope(std::size_t parent,
	ScopeKind kind, bool defer_unknown_members)
{
	const ScopeId semantic = analyzer_.NewScope(
		scopes_[parent].semantic_scope, kind, 0,
		analyzer_.ScopePrefixId(scopes_[parent].semantic_scope));
	return AddScope(semantic, parent, defer_unknown_members);
}

void RetainedTemplateValidator::DeclareParameter(std::size_t scope,
	const TemplateParameter& parameter)
{
	const NameId name = parameter.name;
	if (name == 0) return;
	if (!parameter_names_.insert(name).second)
		throw std::runtime_error("duplicate template parameter");
	scopes_[scope].names[name] |= parameter.kind == TEMPLATE_ARGUMENT_INTEGRAL ?
		RETAINED_VALUE_NAME : RETAINED_TYPE_NAME;
}

void RetainedTemplateValidator::Declare(std::size_t scope, NameId name,
	RetainedNameKind kind, bool allow_existing)
{
	if (name == 0) return;
	if (parameter_names_.find(name) != parameter_names_.end())
		throw std::runtime_error("template parameter redeclared in its scope");
	std::uint8_t& present = scopes_[scope].names[name];
	if ((present & kind) != 0 && !allow_existing)
		throw std::runtime_error("duplicate retained template declaration");
	present |= static_cast<std::uint8_t>(kind);
}

std::uint8_t RetainedTemplateValidator::LookupLocal(std::size_t scope,
	NameId name) const
{
	while (scope != std::numeric_limits<std::size_t>::max())
	{
		const std::unordered_map<NameId, std::uint8_t>::const_iterator found =
			scopes_[scope].names.find(name);
		if (found != scopes_[scope].names.end()) return found->second;
		scope = scopes_[scope].parent;
	}
	return 0;
}

bool RetainedTemplateValidator::LookupLocalCallSets(std::size_t scope,
	NameId name, std::vector<BindingId>* functions,
	std::vector<std::size_t>* templates, EntityId* naming_class) const
{
	while (scope != std::numeric_limits<std::size_t>::max())
	{
		if (scopes_[scope].names.find(name) != scopes_[scope].names.end())
		{
			const std::unordered_map<NameId,
				std::vector<BindingId> >::const_iterator function_set =
				scopes_[scope].call_functions.find(name);
			if (function_set != scopes_[scope].call_functions.end())
				*functions = function_set->second;
			const std::unordered_map<NameId,
				std::vector<std::size_t> >::const_iterator template_set =
				scopes_[scope].call_templates.find(name);
			if (template_set != scopes_[scope].call_templates.end())
				*templates = template_set->second;
			const std::unordered_map<NameId, EntityId>::const_iterator naming =
				scopes_[scope].call_naming_classes.find(name);
			if (naming != scopes_[scope].call_naming_classes.end())
				*naming_class = naming->second;
			return true;
		}
		scope = scopes_[scope].parent;
	}
	return false;
}

bool RetainedTemplateValidator::IsDependentValue(
	std::size_t scope, NameId name) const
{
	while (scope != std::numeric_limits<std::size_t>::max())
	{
		if (scopes_[scope].dependent_values.count(name) != 0) return true;
		scope = scopes_[scope].parent;
	}
	return false;
}

bool RetainedTemplateValidator::DefersUnknownMembers(std::size_t scope) const
{
	while (scope != std::numeric_limits<std::size_t>::max())
	{
		if (scopes_[scope].defer_unknown_members) return true;
		scope = scopes_[scope].parent;
	}
	return false;
}

bool RetainedTemplateValidator::HasUnmodeledFixedBase(
	std::size_t scope) const
{
	while (scope != std::numeric_limits<std::size_t>::max())
	{
		if (scopes_[scope].unmodeled_fixed_base) return true;
		scope = scopes_[scope].parent;
	}
	return false;
}

bool RetainedTemplateValidator::HasUnmodeledCurrentClass(
	std::size_t scope) const
{
	while (scope != std::numeric_limits<std::size_t>::max())
	{
		if (scopes_[scope].unmodeled_current_class) return true;
		scope = scopes_[scope].parent;
	}
	return false;
}

bool RetainedTemplateValidator::IsQualifiedMemberDefinition(NodeId node) const
{
	const NodeId declarator = analyzer_.FindChild(node, "declarator");
	if (declarator == kNoNode) return false;
	const NamePath path = analyzer_.DeclaratorNamePath(declarator);
	return path.global || path.Size() > 1;
}

bool RetainedTemplateValidator::IsTypedef(NodeId specifiers) const
{
	if (specifiers == kNoNode) return false;
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(specifiers);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		if (analyzer_.PayloadSource(analyzer_.arena_->EdgeChild(edge)) ==
			"typedef")
			return true;
	return false;
}

bool RetainedTemplateValidator::HasBaseClass(NodeId node) const
{
	const NodeId clause = analyzer_.FindChild(node, "base-clause");
	return clause != kNoNode;
}

bool RetainedTemplateValidator::SyntaxUsesTemplateParameter(NodeId node) const
{
	if (node == kNoNode) return false;
	const bool structured = analyzer_.FindChild(
		node, "structured-type-name") != kNoNode;
	if (analyzer_.arena_->IsTag(node, "name-component") ||
		(!structured &&
		 (analyzer_.arena_->IsTag(node, "base-name") ||
		  analyzer_.arena_->IsTag(node, "id-expression") ||
		  analyzer_.arena_->IsTag(node, "target") ||
		  analyzer_.arena_->IsTag(node, "type-name") ||
		  analyzer_.arena_->IsTag(node, "decl-specifier"))))
	{
		const NameId name = analyzer_.program_->names.UseInterned(
			analyzer_.arena_->SemanticPayloadId(node));
		if (parameter_names_.find(name) != parameter_names_.end()) return true;
	}
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		if (SyntaxUsesTemplateParameter(analyzer_.arena_->EdgeChild(edge)))
			return true;
	return false;
}

bool RetainedTemplateValidator::SyntaxUsesRetainedType(
	NodeId node, std::size_t scope) const
{
	if (node == kNoNode) return false;
	const bool structured = analyzer_.FindChild(
		node, "structured-type-name") != kNoNode;
	if (analyzer_.arena_->IsTag(node, "name-component") ||
		(!structured &&
		 (analyzer_.arena_->IsTag(node, "base-name") ||
		  analyzer_.arena_->IsTag(node, "id-expression") ||
		  analyzer_.arena_->IsTag(node, "target") ||
		  analyzer_.arena_->IsTag(node, "type-name") ||
		  analyzer_.arena_->IsTag(node, "decl-specifier"))))
	{
		const NameId name = analyzer_.program_->names.UseInterned(
			analyzer_.arena_->SemanticPayloadId(node));
		if ((LookupLocal(scope, name) & RETAINED_TYPE_NAME) != 0) return true;
	}
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		if (SyntaxUsesRetainedType(
			analyzer_.arena_->EdgeChild(edge), scope)) return true;
	return false;
}

bool RetainedTemplateValidator::SyntaxUsesRetainedValue(
	NodeId node, std::size_t scope) const
{
	if (node == kNoNode) return false;
	if (analyzer_.arena_->IsTag(node, "id-expression") &&
		analyzer_.FindChild(node, "structured-type-name") == kNoNode)
	{
		const NamePath path = analyzer_.ParseNamePath(
			analyzer_.PayloadSource(node));
		if (!path.global && path.Size() == 1 &&
			(LookupLocal(scope, path.Last()) & RETAINED_VALUE_NAME) != 0)
			return true;
	}
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		if (SyntaxUsesRetainedValue(
			analyzer_.arena_->EdgeChild(edge), scope)) return true;
	return false;
}

const TemplateParameter* RetainedTemplateValidator::TemplateParameterUsedBy(
	NodeId node) const
{
	if (node == kNoNode) return 0;
	const NameId name = analyzer_.program_->names.UseInterned(
		analyzer_.arena_->SemanticPayloadId(node));
	for (std::size_t i = 0; i < parameters_.size(); ++i)
		if (parameters_[i].name != 0 && parameters_[i].name == name)
			return &parameters_[i];
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const TemplateParameter* result = TemplateParameterUsedBy(
			analyzer_.arena_->EdgeChild(edge));
		if (result) return result;
	}
	return 0;
}

void RetainedTemplateValidator::ValidateKnownTemplateArgumentKinds(
	NodeId node, ScopeId scope)
{
	if (!template_argument_validation_visited_.insert(node).second) return;
	NamePath primary;
	std::vector<NodeId> arguments;
	if (analyzer_.CollectExplicitTemplateArguments(
		node, &primary, &arguments))
	{
		const std::size_t pattern_index =
			analyzer_.FindClassTemplate(scope, primary);
		if (pattern_index != std::numeric_limits<std::size_t>::max())
		{
			const ClassTemplatePattern& pattern =
				analyzer_.class_templates_[pattern_index];
			for (std::size_t argument = 0;
				argument < arguments.size(); ++argument)
			{
				const TemplateParameter& destination =
					TemplateParameterForArgument(pattern.parameters, argument);
				const NodeId syntax = arguments[argument];
				const NodeId type_id = analyzer_.arena_->IsTag(
					syntax, "type-id") ? syntax :
					analyzer_.FindChild(syntax, "type-id");
				const NodeId specifiers = type_id == kNoNode ? kNoNode :
					analyzer_.FindChild(type_id, "type-specifier-seq");
				const NodeId direct_name = specifiers == kNoNode ? kNoNode :
					analyzer_.FirstSemanticChild(specifiers);
				const TemplateParameter* source = direct_name == kNoNode ? 0 :
					TemplateParameterUsedBy(direct_name);
				const NameId direct_id = direct_name == kNoNode ? 0 :
					analyzer_.program_->names.UseInterned(
						analyzer_.arena_->SemanticPayloadId(direct_name));
				const NodeId abstract = analyzer_.FindChild(
					syntax, "abstract-declarator");
				const NodeId declarator = abstract == kNoNode ?
					analyzer_.FindChild(syntax, "declarator") : abstract;
				const bool direct_type_pack = declarator != kNoNode &&
					analyzer_.FindChild(
						declarator, "parameter-pack") != kNoNode;
				if (source && direct_id == source->name && source->pack &&
					direct_type_pack &&
					source->kind != destination.kind)
					throw std::runtime_error(
						"template argument pack kind does not match parameter");
			}
		}
	}
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		ValidateKnownTemplateArgumentKinds(
			analyzer_.arena_->EdgeChild(edge), scope);
}

void RetainedTemplateValidator::VisitChildren(NodeId node, std::size_t scope)
{
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		Visit(analyzer_.arena_->EdgeChild(edge), scope);
}

NodeId RetainedTemplateValidator::FindParameterClause(NodeId declarator) const
{
	std::vector<NodeId> pending(1, declarator);
	while (!pending.empty())
	{
		const NodeId current = pending.back();
		pending.pop_back();
		for (std::uint32_t edge = analyzer_.arena_->FirstEdge(current);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		{
			const NodeId child = analyzer_.arena_->EdgeChild(edge);
			if (analyzer_.arena_->IsTag(child, "parameter-clause")) return child;
			pending.push_back(child);
		}
	}
	return kNoNode;
}

void RetainedTemplateValidator::BindFunctionParameters(NodeId declarator,
	std::size_t scope)
{
	const NodeId clause = FindParameterClause(declarator);
	if (clause == kNoNode) return;
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(clause);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId parameter = analyzer_.arena_->EdgeChild(edge);
		if (!analyzer_.arena_->IsTag(parameter, "parameter-declaration"))
			continue;
		const NodeId parameter_declarator =
			analyzer_.FindChild(parameter, "declarator");
		if (parameter_declarator != kNoNode)
		{
			Declare(scope, analyzer_.DeclaratorName(parameter_declarator),
				RETAINED_VALUE_NAME);
			if (SyntaxUsesTemplateParameter(parameter))
				scopes_[scope].dependent_values.insert(
					analyzer_.DeclaratorName(parameter_declarator));
		}
		VisitChildren(parameter, scope);
	}
}

void RetainedTemplateValidator::VisitFunction(NodeId node, std::size_t scope)
{
	const bool qualified = IsQualifiedMemberDefinition(node);
	const std::size_t function_scope = AddChildScope(
		scope, SCOPE_FUNCTION, qualified);
	const NodeId declarator = analyzer_.FindChild(node, "declarator");
	if (declarator != kNoNode)
		BindFunctionParameters(declarator, function_scope);
	const NodeId initializer = analyzer_.FindChild(node, "ctor-initializer");
	if (initializer != kNoNode) Visit(initializer, function_scope);
	const NodeId body = analyzer_.FindChild(node, "compound-statement");
	if (body != kNoNode) Visit(body, function_scope);
}

void RetainedTemplateValidator::PredeclareClassSimple(NodeId node,
	std::size_t scope)
{
	const NodeId specifiers = analyzer_.FindChild(node, "decl-specifier-seq");
	NameId embedded_type = 0;
	if (specifiers != kNoNode)
		for (std::uint32_t edge = analyzer_.arena_->FirstEdge(specifiers);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		{
			const NodeId specifier = analyzer_.arena_->EdgeChild(edge);
			if (analyzer_.arena_->IsTag(specifier, "enum-specifier"))
			{
				DeclareEnumValues(specifier, scope);
				continue;
			}
			if (!analyzer_.arena_->IsTag(specifier, "class-specifier") &&
				!analyzer_.arena_->IsTag(specifier,
					"class-forward-declaration"))
				continue;
			embedded_type = analyzer_.program_->names.Intern(
				analyzer_.arena_->Payload(specifier));
			Declare(scope, embedded_type, RETAINED_TYPE_NAME);
		}
	const bool type_declaration = IsTypedef(specifiers);
	const NodeId list = analyzer_.FindChild(node, "init-declarator-list");
	if (list == kNoNode) return;
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(list);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId declarator = analyzer_.FindChild(
			analyzer_.arena_->EdgeChild(edge), "declarator");
		if (declarator == kNoNode) continue;
		const NameId name = analyzer_.DeclaratorName(declarator);
		Declare(scope, name, type_declaration ? RETAINED_TYPE_NAME :
			RETAINED_VALUE_NAME,
			!type_declaration || (embedded_type != 0 && name == embedded_type));
	}
}

void RetainedTemplateValidator::DeclareEnumValues(NodeId node,
	std::size_t scope)
{
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId enumerator = analyzer_.arena_->EdgeChild(edge);
		if (!analyzer_.arena_->IsTag(enumerator, "enumerator")) continue;
		Declare(scope, analyzer_.program_->names.Intern(
			analyzer_.arena_->Payload(enumerator)), RETAINED_VALUE_NAME);
	}
}

void RetainedTemplateValidator::PredeclareClassMembers(NodeId node,
	std::size_t scope)
{
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId member = analyzer_.arena_->EdgeChild(edge);
		if (analyzer_.arena_->IsTag(member, "simple-declaration"))
			PredeclareClassSimple(member, scope);
		else if (analyzer_.arena_->IsTag(member, "alias-declaration"))
			Declare(scope, analyzer_.program_->names.Intern(
				analyzer_.arena_->Payload(member)), RETAINED_TYPE_NAME);
		else if (analyzer_.arena_->IsTag(member, "class-specifier") ||
			analyzer_.arena_->IsTag(member, "class-forward-declaration"))
		{
			if (analyzer_.arena_->Payload(member).empty())
				PredeclareClassMembers(member, scope);
			else Declare(scope, analyzer_.program_->names.Intern(
				analyzer_.arena_->Payload(member)), RETAINED_TYPE_NAME);
		}
		else if (analyzer_.arena_->IsTag(member, "bit-field-declaration"))
		{
			for (std::uint32_t field_edge =
					analyzer_.arena_->FirstEdge(member);
				field_edge != kNoEdge;
				field_edge = analyzer_.arena_->NextEdge(field_edge))
			{
				const NodeId field = analyzer_.arena_->EdgeChild(field_edge);
				if (!analyzer_.arena_->IsTag(field, "bit-field-declarator"))
					continue;
				const NodeId declarator =
					analyzer_.FindChild(field, "declarator");
				if (declarator != kNoNode)
					Declare(scope, analyzer_.DeclaratorName(declarator),
						RETAINED_VALUE_NAME);
			}
		}
		else if (analyzer_.arena_->IsTag(member, "function-definition") ||
			analyzer_.arena_->IsTag(member, "special-member-definition") ||
			analyzer_.arena_->IsTag(member, "special-member-declaration"))
		{
			const NodeId declarator = analyzer_.FindChild(member, "declarator");
			if (declarator != kNoNode)
				Declare(scope, analyzer_.DeclaratorName(declarator),
					RETAINED_VALUE_NAME, true);
		}
		else if (analyzer_.arena_->IsTag(member, "template-declaration"))
		{
			NodeId target = kNoNode;
			for (std::uint32_t target_edge =
					analyzer_.arena_->FirstEdge(member);
				target_edge != kNoEdge;
				target_edge = analyzer_.arena_->NextEdge(target_edge))
			{
				const NodeId child = analyzer_.arena_->EdgeChild(target_edge);
				if (!analyzer_.arena_->IsTag(
					child, "template-parameter-clause")) target = child;
			}
			if (target == kNoNode) continue;
			if (analyzer_.arena_->IsTag(target, "simple-declaration"))
				PredeclareClassSimple(target, scope);
			else
			{
				const NodeId declarator =
					analyzer_.FindChild(target, "declarator");
				if (declarator != kNoNode)
					Declare(scope, analyzer_.DeclaratorName(declarator),
						RETAINED_VALUE_NAME, true);
			}
		}
	}
}

void RetainedTemplateValidator::VisitClass(NodeId node, std::size_t scope)
{
	bool dependent_base = false;
	const NodeId base_clause = analyzer_.FindChild(node, "base-clause");
	if (base_clause != kNoNode)
		for (std::uint32_t edge = analyzer_.arena_->FirstEdge(base_clause);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		{
			const NodeId base = analyzer_.arena_->EdgeChild(edge);
			if (!analyzer_.arena_->IsTag(base, "base-specifier")) continue;
			const NodeId name = analyzer_.FindChild(base, "base-name");
			if (name != kNoNode && SyntaxUsesTemplateParameter(name))
				dependent_base = true;
		}
	const std::size_t class_scope = AddChildScope(
		scope, SCOPE_CLASS, HasBaseClass(node));
	if (HasBaseClass(node) && !dependent_base)
		scopes_[class_scope].unmodeled_fixed_base = true;
	PredeclareClassMembers(node, class_scope);
	// Retained class scopes have no concrete EntityId, but still own the
	// injected class name as a dependent type.
	const std::string injected_spelling = analyzer_.arena_->Payload(node);
	if (!injected_spelling.empty())
		Declare(class_scope, analyzer_.program_->names.Intern(injected_spelling),
			RETAINED_TYPE_NAME, true);
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId member = analyzer_.arena_->EdgeChild(edge);
		if (analyzer_.arena_->IsTag(member, "simple-declaration"))
			VisitSimple(member, class_scope, true);
		else if (analyzer_.arena_->IsTag(member, "alias-declaration"))
			VisitChildren(member, class_scope);
		else Visit(member, class_scope);
	}
}

void RetainedTemplateValidator::VisitSimple(NodeId node, std::size_t scope,
	bool predeclared)
{
	const NodeId specifiers = analyzer_.FindChild(node, "decl-specifier-seq");
	if (!predeclared)
	{
		if (specifiers != kNoNode)
			for (std::uint32_t edge = analyzer_.arena_->FirstEdge(specifiers);
				edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
			{
				const NodeId specifier = analyzer_.arena_->EdgeChild(edge);
				if (analyzer_.arena_->IsTag(specifier, "enum-specifier"))
				{
					DeclareEnumValues(specifier, scope);
					continue;
				}
				if (analyzer_.arena_->IsTag(specifier, "class-specifier") ||
					analyzer_.arena_->IsTag(specifier,
						"class-forward-declaration"))
					Declare(scope, analyzer_.program_->names.Intern(
						analyzer_.arena_->Payload(specifier)), RETAINED_TYPE_NAME);
			}
		const bool type_declaration = IsTypedef(specifiers);
		const NodeId list = analyzer_.FindChild(node, "init-declarator-list");
		if (list != kNoNode)
			for (std::uint32_t edge = analyzer_.arena_->FirstEdge(list);
				edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
			{
				const NodeId declarator = analyzer_.FindChild(
					analyzer_.arena_->EdgeChild(edge), "declarator");
				if (declarator != kNoNode)
					Declare(scope, analyzer_.DeclaratorName(declarator),
						type_declaration ? RETAINED_TYPE_NAME :
						RETAINED_VALUE_NAME,
						!type_declaration &&
						FindParameterClause(declarator) != kNoNode);
			}
	}
	const NodeId list = analyzer_.FindChild(node, "init-declarator-list");
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId child = analyzer_.arena_->EdgeChild(edge);
		if (child != list) Visit(child, scope);
	}
	if (list == kNoNode) return;
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(list);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId item = analyzer_.arena_->EdgeChild(edge);
		const NodeId declarator = analyzer_.FindChild(item, "declarator");
		if (declarator == kNoNode ||
			FindParameterClause(declarator) == kNoNode)
		{
			Visit(item, scope);
			continue;
		}
		const std::size_t function_scope =
			AddChildScope(scope, SCOPE_FUNCTION);
		BindFunctionParameters(declarator, function_scope);
		Visit(item, function_scope);
	}
}

void RetainedTemplateValidator::VisitUsing(NodeId node, std::size_t scope)
{
	if (analyzer_.arena_->IsTag(node, "alias-declaration"))
	{
		Declare(scope, analyzer_.program_->names.Intern(
			analyzer_.arena_->Payload(node)), RETAINED_TYPE_NAME);
		VisitChildren(node, scope);
		return;
	}
	const NodeId target_node = analyzer_.FindChild(node, "target");
	if (target_node == kNoNode) return;
	const std::string target = analyzer_.arena_->Payload(target_node);
	if (analyzer_.arena_->IsTag(node, "using-directive"))
	{
		const ScopeId target_scope = analyzer_.ResolveScopeSpelling(
			scopes_[scope].semantic_scope, target);
		if (target_scope == kNoScope)
			throw std::runtime_error("retained using namespace target not found");
		analyzer_.program_->AddUsingEdge(
			scopes_[scope].semantic_scope, target_scope);
		return;
	}
	const NodeId structure = analyzer_.FindChild(
		target_node, "structured-type-name");
	const NamePath path = structure == kNoNode ?
		analyzer_.ParseNamePath(target) :
		analyzer_.StructuredNamePath(structure);
	const NameId name = path.Last();
	if (parameter_names_.find(name) != parameter_names_.end())
		throw std::runtime_error("using declaration redeclares template parameter");
	if (SyntaxUsesTemplateParameter(target_node))
	{
		Declare(scope, name, RETAINED_TYPE_NAME, true);
		Declare(scope, name, RETAINED_VALUE_NAME, true);
		return;
	}
	if (!path.global && path.Size() > 1 &&
		(LookupLocal(scope, path[0]) & RETAINED_TYPE_NAME) != 0)
	{
		Declare(scope, name, RETAINED_TYPE_NAME, true);
		Declare(scope, name, RETAINED_VALUE_NAME, true);
		return;
	}
	EntityId naming_class = kNoEntity;
	const std::vector<BindingId> functions =
		analyzer_.FunctionCallCandidates(
			scopes_[scope].semantic_scope, target, &naming_class, target_node);
	const std::vector<std::size_t> templates =
		structure == kNoNode ? analyzer_.FindFunctionTemplates(
			scopes_[scope].semantic_scope, target) :
		analyzer_.FindFunctionTemplates(
			scopes_[scope].semantic_scope, path);
	if (!functions.empty() || !templates.empty())
	{
		Declare(scope, name, RETAINED_VALUE_NAME, true);
		scopes_[scope].call_functions[name] = functions;
		scopes_[scope].call_templates[name] = templates;
		scopes_[scope].call_naming_classes[name] = naming_class;
		return;
	}
	const LookupResult ordinary = structure != kNoNode ?
		analyzer_.LookupStructuredName(target_node,
			scopes_[scope].semantic_scope, LOOKUP_ORDINARY) :
		analyzer_.LookupPath(
			scopes_[scope].semantic_scope, path, LOOKUP_ORDINARY);
	if (ordinary.ordinary != kNoBinding)
		Declare(scope, name, RETAINED_VALUE_NAME, true);
	else
	{
		const LookupResult type = structure != kNoNode ?
			analyzer_.LookupStructuredName(target_node,
				scopes_[scope].semantic_scope, LOOKUP_TYPE) :
			analyzer_.LookupPath(
				scopes_[scope].semantic_scope, path, LOOKUP_TYPE);
		if (type.type == kNoType)
			throw std::runtime_error("retained using declaration target not found");
		Declare(scope, name, RETAINED_TYPE_NAME);
	}
}

void RetainedTemplateValidator::VisitIdExpression(NodeId node,
	std::size_t scope, bool unknown_callee)
{
	const std::string spelling = analyzer_.PayloadSource(node);
	const NodeId structure = analyzer_.FindChild(
		node, "structured-type-name");
	const NamePath path = structure == kNoNode ?
		analyzer_.ParseNamePath(spelling) :
		analyzer_.StructuredNamePath(structure);
	if (path.Empty()) return;
	if (path.global || path.Size() > 1)
	{
		if (SyntaxUsesTemplateParameter(node)) return;
		if (SyntaxUsesRetainedType(node, scope)) return;
		if (SyntaxUsesRetainedValue(node, scope)) return;
		if (!path.global &&
			(LookupLocal(scope, path[0]) & RETAINED_TYPE_NAME) != 0)
			return;
		const LookupResult ordinary = structure != kNoNode ?
			analyzer_.LookupStructuredName(node,
				scopes_[scope].semantic_scope, LOOKUP_ORDINARY) :
			analyzer_.LookupSpelling(
				scopes_[scope].semantic_scope, spelling, LOOKUP_ORDINARY);
		if (ordinary.ordinary != kNoBinding)
		{
			if (unknown_callee &&
				analyzer_.program_->bindings[ordinary.ordinary].kind ==
					BIND_FUNCTION)
				analyzer_.RecordRetainedCallLookup(node,
					scopes_[scope].semantic_scope, spelling, false);
			return;
		}
		const LookupResult type = structure != kNoNode ?
			analyzer_.LookupStructuredName(node,
				scopes_[scope].semantic_scope, LOOKUP_TYPE) :
			analyzer_.LookupSpelling(
				scopes_[scope].semantic_scope, spelling, LOOKUP_TYPE);
		if (type.type != kNoType)
		{
			if (unknown_callee) return;
			throw std::runtime_error("type name used as retained value");
		}
		const std::vector<std::size_t> templates = structure != kNoNode ?
			analyzer_.FindFunctionTemplates(
				scopes_[scope].semantic_scope,
				analyzer_.StructuredNamePath(node)) :
			analyzer_.FindFunctionTemplates(
				scopes_[scope].semantic_scope, spelling);
		if (unknown_callee && !templates.empty())
			analyzer_.RecordRetainedCallLookup(node,
				scopes_[scope].semantic_scope, spelling, false);
		return;
	}
	const NameId name = path.Last();
	if (!analyzer_.FindVariableTemplates(
		scopes_[scope].semantic_scope, path).empty()) return;
	const std::uint8_t local = LookupLocal(scope, name);
	if ((local & RETAINED_VALUE_NAME) != 0)
	{
		if (unknown_callee)
		{
			std::vector<BindingId> functions;
			std::vector<std::size_t> templates;
			EntityId naming_class = kNoEntity;
			(void)LookupLocalCallSets(scope, name, &functions, &templates,
				&naming_class);
			if (!functions.empty() || !templates.empty())
				analyzer_.PublishRetainedCallLookup(node, functions, templates,
					naming_class, true);
		}
		return;
	}
	if ((local & RETAINED_TYPE_NAME) != 0)
	{
		if (unknown_callee) return;
		throw std::runtime_error("type name used as retained value");
	}
	const LookupResult ordinary = analyzer_.program_->LookupName(
		scopes_[scope].semantic_scope, name, LOOKUP_ORDINARY);
	if (ordinary.ordinary != kNoBinding)
	{
		if (unknown_callee && !HasUnmodeledFixedBase(scope) &&
			analyzer_.program_->bindings[ordinary.ordinary].kind == BIND_FUNCTION)
			analyzer_.RecordRetainedCallLookup(node,
				scopes_[scope].semantic_scope, spelling, true);
		return;
	}
	const LookupResult type = analyzer_.program_->LookupName(
		scopes_[scope].semantic_scope, name, LOOKUP_TYPE);
	if (type.type != kNoType)
	{
		if (unknown_callee) return;
		throw std::runtime_error("type name used as retained value");
	}
	const std::vector<std::size_t> templates =
		analyzer_.FindFunctionTemplates(
			scopes_[scope].semantic_scope, path);
	if (!templates.empty())
	{
		if (unknown_callee)
			analyzer_.RecordRetainedCallLookup(node,
				scopes_[scope].semantic_scope, spelling, true);
		return;
	}
	if (unknown_callee)
	{
		if (!HasUnmodeledFixedBase(scope) &&
			!HasUnmodeledCurrentClass(scope))
			analyzer_.RecordRetainedCallLookup(node,
				scopes_[scope].semantic_scope, spelling, true);
		return;
	}
	if (DefersUnknownMembers(scope)) return;
	throw std::runtime_error(
		"unknown nondependent name in template definition: " + spelling);
}

void RetainedTemplateValidator::VisitSizeof(NodeId node, std::size_t scope)
{
	const NodeId operand = analyzer_.FirstSemanticChild(node);
	if (operand == kNoNode || analyzer_.arena_->IsTag(operand, "type-id"))
		return;
	if (analyzer_.arena_->IsTag(operand, "id-expression"))
	{
		const std::string spelling = analyzer_.PayloadSource(operand);
		const NodeId structure = analyzer_.FindChild(
			operand, "structured-type-name");
		const NamePath path = structure == kNoNode ?
			analyzer_.ParseNamePath(spelling) :
			analyzer_.StructuredNamePath(structure);
		if (SyntaxUsesTemplateParameter(operand)) return;
		if (SyntaxUsesRetainedType(operand, scope) ||
			SyntaxUsesRetainedValue(operand, scope)) return;
		if (!path.Empty() && !path.global && path.Size() == 1)
		{
			const std::uint8_t local = LookupLocal(scope, path.Last());
			if ((local & RETAINED_VALUE_NAME) != 0)
			{
				Visit(operand, scope);
				return;
			}
			if ((local & RETAINED_TYPE_NAME) != 0) return;
		}
		const LookupResult ordinary = structure != kNoNode ?
			analyzer_.LookupStructuredName(operand,
				scopes_[scope].semantic_scope, LOOKUP_ORDINARY) :
			analyzer_.LookupSpelling(
				scopes_[scope].semantic_scope, spelling, LOOKUP_ORDINARY);
		if (ordinary.ordinary == kNoBinding)
		{
			const LookupResult type = structure != kNoNode ?
				analyzer_.LookupStructuredName(operand,
					scopes_[scope].semantic_scope, LOOKUP_TYPE) :
				analyzer_.LookupSpelling(
					scopes_[scope].semantic_scope, spelling, LOOKUP_TYPE);
			if (type.type != kNoType) return;
		}
	}
	Visit(operand, scope);
}

void RetainedTemplateValidator::Visit(NodeId node, std::size_t scope,
	bool unknown_callee)
{
	if (node == kNoNode) return;
	if (analyzer_.arena_->IsTag(node, "id-expression"))
	{
		VisitIdExpression(node, scope, unknown_callee);
		return;
	}
	if (analyzer_.arena_->IsTag(node, "call-expression"))
	{
		const std::uint32_t first = analyzer_.arena_->FirstEdge(node);
		if (first == kNoEdge) return;
		Visit(analyzer_.arena_->EdgeChild(first), scope, true);
		for (std::uint32_t edge = analyzer_.arena_->NextEdge(first);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
			Visit(analyzer_.arena_->EdgeChild(edge), scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, "sizeof-expression"))
	{
		VisitSizeof(node, scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, "lambda-expression"))
	{
		const std::size_t lambda_scope = AddChildScope(
			scope, SCOPE_FUNCTION, DefersUnknownMembers(scope));
		const NodeId declarator = analyzer_.FindChild(
			node, "lambda-declarator");
		if (declarator != kNoNode)
		{
			BindFunctionParameters(declarator, lambda_scope);
			VisitChildren(declarator, lambda_scope);
		}
		const NodeId body = analyzer_.FindChild(node, "compound-statement");
		if (body != kNoNode) Visit(body, lambda_scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, "range-for-statement"))
	{
		const std::size_t control = AddChildScope(scope, SCOPE_BLOCK);
		const NodeId declaration = analyzer_.FindChild(
			node, "range-declaration");
		const NodeId initializer = analyzer_.FindChild(
			node, "range-initializer");
		if (declaration == kNoNode || initializer == kNoNode)
			throw std::runtime_error("invalid retained range-for statement");
		Visit(initializer, control);
		Visit(declaration, control);
		const NodeId declarator = analyzer_.FindChild(
			declaration, "declarator");
		if (declarator == kNoNode)
			throw std::runtime_error("retained range declaration has no declarator");
		Declare(control, analyzer_.DeclaratorName(declarator),
			RETAINED_VALUE_NAME);
		for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		{
			const NodeId child = analyzer_.arena_->EdgeChild(edge);
			if (child != declaration && child != initializer)
				Visit(child, control);
		}
		return;
	}
	if (analyzer_.arena_->IsTag(node, "handler"))
	{
		const std::size_t handler_scope = AddChildScope(scope, SCOPE_BLOCK);
		const NodeId declaration = analyzer_.FindChild(
			node, "exception-declaration");
		if (declaration != kNoNode)
		{
			VisitChildren(declaration, handler_scope);
			const NodeId declarator = analyzer_.FindChild(
				declaration, "declarator");
			if (declarator != kNoNode)
				Declare(handler_scope, analyzer_.DeclaratorName(declarator),
					RETAINED_VALUE_NAME);
		}
		const NodeId body = analyzer_.FindChild(node, "compound-statement");
		if (body != kNoNode) Visit(body, handler_scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, "compound-statement"))
	{
		const std::size_t block = AddChildScope(scope, SCOPE_BLOCK);
		VisitChildren(node, block);
		return;
	}
	if (analyzer_.arena_->IsTag(node, "template-declaration"))
	{
		const NodeId clause = analyzer_.FindChild(
			node, "template-parameter-clause");
		const NodeId list = clause == kNoNode ? kNoNode :
			analyzer_.FindChild(clause, "template-parameter-list");
		std::vector<TemplateParameter> parameters;
		std::vector<NameId> names;
		std::vector<NodeId> defaults;
		analyzer_.ParseTemplateParameters(list,
			scopes_[scope].semantic_scope, &parameters, &names, &defaults,
			&parameter_names_);
		const std::size_t template_scope = AddChildScope(
			scope, SCOPE_TEMPLATE_PARAMETERS, DefersUnknownMembers(scope));
		std::vector<NameId> introduced;
		for (std::size_t i = 0; i < parameters.size(); ++i)
		{
			DeclareParameter(template_scope, parameters[i]);
			if (parameters[i].name != 0)
				introduced.push_back(parameters[i].name);
		}
		for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		{
			const NodeId child = analyzer_.arena_->EdgeChild(edge);
			if (child != clause) Visit(child, template_scope);
		}
		for (std::size_t i = 0; i < introduced.size(); ++i)
			parameter_names_.erase(introduced[i]);
		return;
	}
	if (analyzer_.arena_->IsTag(node, "member-expression"))
	{
		const std::uint32_t object_edge = analyzer_.arena_->FirstEdge(node);
		const std::uint32_t member_edge = object_edge == kNoEdge ? kNoEdge :
			analyzer_.arena_->NextEdge(object_edge);
		if (member_edge != kNoEdge)
		{
			const NodeId object = analyzer_.arena_->EdgeChild(object_edge);
			const NodeId member = analyzer_.arena_->EdgeChild(member_edge);
			const NodeId structure = analyzer_.FindChild(
				member, "structured-type-name");
			NodeId terminal = kNoNode;
			if (structure != kNoNode)
				for (std::uint32_t edge = analyzer_.arena_->FirstEdge(structure);
					edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
				{
					const NodeId child = analyzer_.arena_->EdgeChild(edge);
					if (analyzer_.arena_->IsTag(child, "name-component"))
						terminal = child;
				}
			const bool template_id = terminal != kNoNode &&
				analyzer_.FindChild(terminal,
					"template-type-argument-list") != kNoNode;
			const bool template_keyword = analyzer_.PayloadSource(member).
				compare(0, 8, "template") == 0;
			if (template_id && !template_keyword &&
				analyzer_.arena_->IsTag(object, "id-expression") &&
				analyzer_.FindChild(object, "structured-type-name") == kNoNode)
			{
				const NameId object_name = analyzer_.program_->names.Intern(
					analyzer_.PayloadSource(object));
				if (IsDependentValue(scope, object_name))
					throw std::runtime_error(
						"dependent member template-id requires template");
			}
		}
		VisitChildren(node, scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, "class-specifier"))
	{
		VisitClass(node, scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, "function-definition") ||
		analyzer_.arena_->IsTag(node, "special-member-definition"))
	{
		VisitFunction(node, scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, "simple-declaration"))
	{
		VisitSimple(node, scope, false);
		return;
	}
	if (analyzer_.arena_->IsTag(node, "alias-declaration") ||
		analyzer_.arena_->IsTag(node, "using-declaration") ||
		analyzer_.arena_->IsTag(node, "using-directive"))
	{
		VisitUsing(node, scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, "condition-declaration"))
	{
		const NodeId declarator = analyzer_.FindChild(node, "declarator");
		if (declarator != kNoNode)
			Declare(scope, analyzer_.DeclaratorName(declarator),
				RETAINED_VALUE_NAME);
	}
	VisitChildren(node, scope);
}

bool RetainedTemplateValidator::IsNonthrowingSyntax(NodeId declarator) const
{
	const NodeId qualifier = analyzer_.FindChild(declarator,
		"function-qualifier");
	return qualifier != kNoNode &&
		analyzer_.PayloadSource(qualifier).find("noexcept") == 0;
}

std::size_t RetainedTemplateValidator::ParameterCount(NodeId declarator) const
{
	const NodeId clause = FindParameterClause(declarator);
	std::size_t result = 0;
	if (clause != kNoNode)
		for (std::uint32_t edge = analyzer_.arena_->FirstEdge(clause);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
			if (analyzer_.arena_->IsTag(analyzer_.arena_->EdgeChild(edge),
				"parameter-declaration"))
				++result;
	return result;
}

void RetainedTemplateValidator::ValidateSpecialMemberExceptionSpecification()
{
	if (!analyzer_.arena_->IsTag(target_, "special-member-definition")) return;
	const NodeId declarator = analyzer_.FindChild(target_, "declarator");
	if (declarator == kNoNode) return;
	const NamePath path = analyzer_.DeclaratorNamePath(declarator);
	if (!path.global && path.Size() <= 1) return;
	NamePath owner;
	const NodeId structure = analyzer_.DeclaratorNameStructure(declarator);
	if (structure != kNoNode)
	{
		owner = analyzer_.StructuredNamePath(structure);
		if (!owner.Empty()) owner.Pop();
	}
	else
	{
		owner = path;
		owner.Pop();
	}
	const std::size_t index = analyzer_.FindClassTemplate(lexical_scope_, owner);
	if (index >= analyzer_.class_templates_.size()) return;
	const ClassTemplatePattern& pattern = analyzer_.class_templates_[index];
	const NameId terminal = path.Last();
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(pattern.declaration);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId member = analyzer_.arena_->EdgeChild(edge);
		if (!analyzer_.arena_->IsTag(member, "special-member-declaration") &&
			!analyzer_.arena_->IsTag(member, "special-member-definition"))
			continue;
		const NodeId prior = analyzer_.FindChild(member, "declarator");
		if (prior == kNoNode || analyzer_.DeclaratorName(prior) != terminal ||
			ParameterCount(prior) != ParameterCount(declarator))
			continue;
		if (IsNonthrowingSyntax(prior) != IsNonthrowingSyntax(declarator))
			throw std::runtime_error(
				"conflicting retained special-member exception specification");
		return;
	}
}

void RetainedTemplateValidator::Run()
{
	const bool definition =
		(analyzer_.arena_->Flags(target_) & SYNTAX_FLAG_DEFINITION) != 0 ||
		analyzer_.arena_->IsTag(target_, "function-definition") ||
		analyzer_.arena_->IsTag(target_, "special-member-definition");
	if (definition) ValidateSpecialMemberExceptionSpecification();
	const ScopeId semantic = analyzer_.NewScope(lexical_scope_,
		SCOPE_TEMPLATE_PARAMETERS, 0,
		analyzer_.ScopePrefixId(lexical_scope_));
	const bool current_class =
		analyzer_.program_->KindOfScope(lexical_scope_) == SCOPE_CLASS;
	const bool qualified_member = IsQualifiedMemberDefinition(target_);
	const bool defer_members =
		qualified_member || current_class;
	const std::size_t root = AddScope(semantic,
		std::numeric_limits<std::size_t>::max(),
		defer_members, false, defer_members);
	if (qualified_member)
	{
		const NodeId declarator = analyzer_.FindChild(target_, "declarator");
		NamePath owner;
		const NodeId structure =
			analyzer_.DeclaratorNameStructure(declarator);
		if (structure != kNoNode)
			owner = analyzer_.StructuredNamePath(structure);
		else owner = analyzer_.DeclaratorNamePath(declarator);
		if (!owner.Empty()) owner.Pop();
		// Only a canonical class-template owner contributes an injected name;
		// a namespace-qualified function does not gain a current class.
		const std::size_t pattern =
			analyzer_.FindClassTemplate(lexical_scope_, owner);
		if (pattern < analyzer_.class_templates_.size())
			Declare(root, analyzer_.class_templates_[pattern].name,
				RETAINED_TYPE_NAME);
	}
	while (analyzer_.function_template_shape_parameters_.size() <
		parameters_.size())
	{
		std::ostringstream generated;
		generated << "__retained_template_parameter_shape_"
			<< analyzer_.function_template_shape_parameters_.size();
		const NameId name = analyzer_.program_->names.Intern(generated.str());
		const EntityId entity = analyzer_.program_->NewEntity(name,
			NAMED_TYPENAME_PARAMETER, false, kNoType,
			analyzer_.program_->GlobalScope(), name);
		analyzer_.function_template_shape_parameters_.push_back(
			analyzer_.program_->types.Named(entity));
	}
	for (std::size_t i = 0; i < parameters_.size(); ++i)
	{
		DeclareParameter(root, parameters_[i]);
		if (parameters_[i].name == 0) continue;
		if (parameters_[i].kind == TEMPLATE_ARGUMENT_TYPE)
			analyzer_.program_->AddBinding(semantic, BIND_TYPE_ALIAS,
				parameters_[i].name,
				analyzer_.function_template_shape_parameters_[i]);
		else if (parameters_[i].kind == TEMPLATE_ARGUMENT_TEMPLATE)
			analyzer_.CreateTemplateTemplateParameterProxy(
				semantic, parameters_[i], i);
		else analyzer_.program_->AddBinding(semantic, BIND_PARAMETER,
			parameters_[i].name, parameters_[i].dependent_type ?
				analyzer_.program_->types.Fundamental(FUND_INT) :
				parameters_[i].value_type, false,
			static_cast<std::int64_t>(i));
	}
	ValidateKnownTemplateArgumentKinds(target_, semantic);
	if (!definition && !analyzer_.arena_->IsTag(target_, "alias-declaration"))
		return;
	Visit(target_, root);
}

void SemanticAnalyzer::ValidateRetainedTemplateDefinition(NodeId target,
	ScopeId scope, const std::vector<TemplateParameter>& parameters)
{
	RetainedTemplateValidator(*this, target, scope, parameters).Run();
}

void SemanticAnalyzer::PublishRetainedCallLookup(NodeId callee,
	const std::vector<BindingId>& functions,
	const std::vector<std::size_t>& templates, EntityId naming_class,
	bool adl_eligible)
{
	if (retained_call_lookup_states_.size() <= callee)
	{
		retained_call_lookup_states_.resize(
			static_cast<std::size_t>(callee) + 1, 0);
		retained_call_naming_classes_.resize(
			static_cast<std::size_t>(callee) + 1, kNoEntity);
	}
	retained_call_lookup_states_[callee] =
		RETAINED_CALL_LOOKUP_PUBLISHED |
		(adl_eligible ? RETAINED_CALL_ADL_ELIGIBLE : 0);
	retained_call_naming_classes_[callee] = naming_class;
	if (!functions.empty())
	{
		CompactIndexSequence& function_set =
			retained_call_function_sets_.Ensure(callee);
		for (std::size_t i = 0; i < functions.size(); ++i)
			if (!function_set.Contains(functions[i]))
				function_set.Push(functions[i]);
	}
	if (!templates.empty())
	{
		CompactIndexSequence& template_set =
			retained_call_template_sets_.Ensure(callee);
		for (std::size_t i = 0; i < templates.size(); ++i)
			if (!template_set.Contains(templates[i]))
				template_set.Push(templates[i]);
	}
}

void SemanticAnalyzer::CopyRetainedCallLookup(
	NodeId source, NodeId destination)
{
	if (source == destination || source >= retained_call_lookup_states_.size() ||
		(retained_call_lookup_states_[source] &
			RETAINED_CALL_LOOKUP_PUBLISHED) == 0) return;
	const CompactIndexSequence* source_functions =
		retained_call_function_sets_.Find(source);
	const CompactIndexSequence* source_templates =
		retained_call_template_sets_.Find(source);
	const std::vector<std::size_t> functions = source_functions ?
		source_functions->Copy() : std::vector<std::size_t>();
	const std::vector<std::size_t> templates = source_templates ?
		source_templates->Copy() : std::vector<std::size_t>();
	if (retained_call_lookup_states_.size() <= destination)
	{
		retained_call_lookup_states_.resize(
			static_cast<std::size_t>(destination) + 1, 0);
		retained_call_naming_classes_.resize(
			static_cast<std::size_t>(destination) + 1, kNoEntity);
	}
	retained_call_lookup_states_[destination] =
		retained_call_lookup_states_[source];
	retained_call_naming_classes_[destination] =
		retained_call_naming_classes_[source];
	CompactIndexSequence& destination_functions =
		retained_call_function_sets_.Ensure(destination);
	destination_functions.Clear();
	for (std::size_t i = 0; i < functions.size(); ++i)
		destination_functions.Push(functions[i]);
	CompactIndexSequence& destination_templates =
		retained_call_template_sets_.Ensure(destination);
	destination_templates.Clear();
	for (std::size_t i = 0; i < templates.size(); ++i)
		destination_templates.Push(templates[i]);
}

void SemanticAnalyzer::RecordRetainedCallLookup(NodeId callee, ScopeId scope,
	const std::string& spelling, bool adl_eligible)
{
	EntityId naming_class = kNoEntity;
	std::vector<BindingId> functions;
	const NodeId structure = FindChild(callee, "structured-type-name");
	NodeId terminal_component = kNoNode;
	if (structure != kNoNode)
		for (std::uint32_t edge = arena_->FirstEdge(structure);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "name-component"))
				terminal_component = child;
		}
	const bool explicit_template_id = terminal_component != kNoNode &&
		FindChild(terminal_component, "template-type-argument-list") != kNoNode;
	const std::vector<std::size_t> templates = structure != kNoNode ?
		FindFunctionTemplates(scope, StructuredNamePath(callee)) :
		FindFunctionTemplates(scope, spelling);
	if (!explicit_template_id)
		functions = FunctionCallCandidates(scope, spelling, &naming_class, callee,
			!templates.empty());
	PublishRetainedCallLookup(callee, functions, templates, naming_class,
		adl_eligible);
}

std::vector<BindingId> SemanticAnalyzer::RetainedFunctionCallCandidates(
	NodeId callee, ScopeId scope, const std::string& spelling,
	EntityId* naming_class, bool* retained_lookup)
{
	*retained_lookup = callee < retained_call_lookup_states_.size() &&
		(retained_call_lookup_states_[callee] &
			RETAINED_CALL_LOOKUP_PUBLISHED) != 0;
	if (!*retained_lookup)
		return FunctionCallCandidates(
			scope, spelling, naming_class, callee, true);
	*naming_class = retained_call_naming_classes_[callee];
	std::vector<BindingId> result;
	const CompactIndexSequence* retained_functions =
		retained_call_function_sets_.Find(callee);
	if (retained_functions)
	{
		result.reserve(retained_functions->Size());
		for (std::size_t i = 0; i < retained_functions->Size(); ++i)
		{
			++function_candidate_index_visits_;
			result.push_back(static_cast<BindingId>((*retained_functions)[i]));
		}
	}
	return result;
}

void SemanticAnalyzer::CompleteFunctionCallTemplateCandidates(NodeId callee,
	ScopeId scope, const std::string& spelling,
	const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>& arguments, bool retained_lookup,
	std::vector<BindingId>* candidates, EntityId* naming_class)
{
	if (!retained_lookup)
	{
		const NamePath structured = StructuredNamePath(callee);
		std::vector<std::size_t> patterns;
		if (structured.Empty())
			patterns = FindFunctionTemplates(scope, spelling);
		else
		{
			// Resolve template-id owner components semantically.  A flattened
			// NamePath identifies box<int> as box and would otherwise stop at
			// the primary marker rather than the concrete member scope.
			const LookupResult found = LookupStructuredName(
				callee, scope, LOOKUP_FUNCTION_TEMPLATE);
			const NameId name = structured.Last();
			for (std::size_t owner = 0;
				owner < found.FunctionTemplateOwnerCount(); ++owner)
			{
				const std::uint64_t key =
					(static_cast<std::uint64_t>(
						found.FunctionTemplateOwnerAt(owner)) << 32) | name;
				const CompactIndexSequence* indexed =
					template_function_sets_.Find(key);
				if (!indexed) continue;
				for (std::size_t i = 0; i < indexed->Size(); ++i)
					patterns.push_back((*indexed)[i]);
			}
			if (patterns.empty())
				patterns = FindFunctionTemplates(scope, structured);
		}
		if (patterns.empty()) return;
		NamePath syntax_base;
		std::vector<NodeId> explicit_syntax;
		const bool has_explicit_syntax = CollectExplicitTemplateArguments(
			callee, &syntax_base, &explicit_syntax);
		std::vector<BindingId> specializations;
		if (has_explicit_syntax)
			DeduceFunctionTemplatePatternsWithExplicitSyntax(patterns,
				arguments, explicit_syntax, scope, &specializations,
				&argument_syntax);
		else DeduceFunctionTemplatePatterns(patterns, arguments,
			&specializations, 0, 0, scope, &argument_syntax);
		DeduceFunctionTemplates(scope, spelling, arguments, callee);
		for (std::size_t i = 0; i < specializations.size(); ++i)
		{
			const BindingId canonical =
				program_->bindings[specializations[i]].canonical;
			bool present = false;
			for (std::size_t existing = 0; existing < candidates->size(); ++existing)
				if (program_->bindings[(*candidates)[existing]].canonical == canonical)
					present = true;
			if (!present) candidates->push_back(specializations[i]);
		}
		return;
	}
	const CompactIndexSequence* retained_templates =
		retained_call_template_sets_.Find(callee);
	std::vector<std::size_t> patterns = retained_templates ?
		retained_templates->Copy() : std::vector<std::size_t>();
	NamePath syntax_base;
	std::vector<NodeId> explicit_syntax;
	const bool has_explicit_syntax = CollectExplicitTemplateArguments(
		callee, &syntax_base, &explicit_syntax);
	NamePath retained_name = StructuredNamePath(callee);
	if (retained_name.Empty()) retained_name = ParseNamePath(spelling);
	if (current_class_context_ != kNoEntity && !retained_name.Empty() &&
		!retained_name.global && retained_name.Size() == 1)
	{
		const ScopeId member_scope =
			program_->entities[current_class_context_].member_scope;
		if (member_scope != kNoScope)
		{
			const std::uint64_t key =
				(static_cast<std::uint64_t>(member_scope) << 32) |
				retained_name.Last();
			const CompactIndexSequence* active_patterns =
				template_function_sets_.Find(key);
			if (active_patterns)
				for (std::size_t i = 0; i < active_patterns->Size(); ++i)
					if (std::find(patterns.begin(), patterns.end(),
						(*active_patterns)[i]) == patterns.end())
						patterns.push_back((*active_patterns)[i]);
			*naming_class = current_class_context_;
		}
	}
	if (current_function_context_ != kNoBinding)
	{
		const FunctionInfo& current = GetFunction(current_function_context_);
		const BindingRecord& current_binding =
			program_->bindings[current_function_context_];
		NamePath active_name = StructuredNamePath(callee);
		if (active_name.Empty()) active_name = ParseNamePath(spelling);
		if (current.member_owner != kNoType &&
			!active_name.Empty() && !active_name.global &&
			current_binding.member_owner != kNoEntity)
		{
			const EntityId active_entity = current_binding.member_owner;
			patterns.erase(std::remove_if(patterns.begin(), patterns.end(),
				[this, active_entity](std::size_t pattern_index) {
					const FunctionTemplatePattern& pattern =
						function_templates_[pattern_index];
					const EntityId owner = pattern.static_member ? kNoEntity :
						program_->EntityForScope(pattern.owner);
					return owner != kNoEntity && owner != active_entity &&
						!program_->IsBaseOf(owner, active_entity);
				}), patterns.end());
			candidates->erase(std::remove_if(candidates->begin(), candidates->end(),
				[this, active_entity](BindingId candidate) {
					const BindingRecord& declaration =
						program_->bindings[candidate];
					const EntityId owner = declaration.static_member_function ?
						kNoEntity : declaration.member_owner;
					return owner != kNoEntity && owner != active_entity &&
						!program_->IsBaseOf(owner, active_entity);
				}), candidates->end());
			const ScopeId active_owner =
				program_->entities[active_entity].member_scope;
			if (active_name.Size() > 1 && active_owner != kNoScope)
			{
				const LookupResult qualifier = program_->LookupDirect(
					active_owner, active_name[0], LOOKUP_SCOPE_CARRIER);
				if (qualifier.type != kNoType ||
					qualifier.name_space != kNoScope)
				{
					patterns = FindStructuredFunctionTemplates(callee, scope);
					*candidates = FunctionCallCandidates(
						scope, spelling, naming_class, callee,
						!has_explicit_syntax);
					const LookupResult active_lookup = LookupStructuredName(
						callee, scope, LOOKUP_FUNCTION_TEMPLATE);
					if (active_lookup.naming_class != kNoEntity)
						*naming_class = active_lookup.naming_class;
				}
			}
			if (active_name.Size() == 1)
			{
				const LookupResult active_lookup = active_owner == kNoScope ?
					LookupResult() : LookupPath(
						active_owner, active_name, LOOKUP_FUNCTION_TEMPLATE);
				if (active_lookup.naming_class != kNoEntity)
				{
					*naming_class = active_lookup.naming_class;
					const NameId name = active_name.Last();
					for (std::size_t owner = 0;
						owner < active_lookup.FunctionTemplateOwnerCount(); ++owner)
					{
						const std::uint64_t key =
							(static_cast<std::uint64_t>(
								active_lookup.FunctionTemplateOwnerAt(owner)) << 32) |
							name;
						const CompactIndexSequence* active_patterns =
							template_function_sets_.Find(key);
						if (!active_patterns) continue;
						for (std::size_t i = 0;
							i < active_patterns->Size(); ++i)
							if (std::find(patterns.begin(), patterns.end(),
								(*active_patterns)[i]) == patterns.end())
								patterns.push_back((*active_patterns)[i]);
					}
				}
			}
		}
		if (!active_name.Empty() && !active_name.global &&
			active_name.Size() == 1 &&
			current.template_pattern != kNoDumpEdge &&
			current.template_pattern < function_templates_.size() &&
			function_templates_[current.template_pattern].name ==
				program_->names.Intern(spelling) &&
			std::find(patterns.begin(), patterns.end(),
				current.template_pattern) == patterns.end())
			patterns.push_back(current.template_pattern);
	}
	if (patterns.empty()) return;
	std::vector<BindingId> specializations;
	if (has_explicit_syntax)
		DeduceFunctionTemplatePatternsWithExplicitSyntax(patterns,
			arguments, explicit_syntax, scope, &specializations,
			&argument_syntax);
	else
		DeduceFunctionTemplatePatterns(patterns, arguments, &specializations,
			0, 0, scope, &argument_syntax);
	for (std::size_t i = 0; i < specializations.size(); ++i)
	{
		const BindingId canonical =
			program_->bindings[specializations[i]].canonical;
		bool present = false;
		for (std::size_t existing = 0; existing < candidates->size(); ++existing)
			if (program_->bindings[(*candidates)[existing]].canonical == canonical)
				present = true;
		if (!present) candidates->push_back(specializations[i]);
	}
}

bool SemanticAnalyzer::RetainedCallAllowsArgumentDependentLookup(
	NodeId callee) const
{
	return callee < retained_call_lookup_states_.size() &&
		(retained_call_lookup_states_[callee] &
			RETAINED_CALL_ADL_ELIGIBLE) != 0;
}

}
}
