#include "pa12_semantic_detail.h"

#include <cctype>
#include <limits>
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

struct RetainedScope
{
	ScopeId semantic_scope;
	std::size_t parent;
	std::unordered_map<NameId, std::uint8_t> names;
	bool defer_unknown_members;

	RetainedScope(ScopeId semantic, std::size_t owner, bool defer)
		: semantic_scope(semantic), parent(owner), defer_unknown_members(defer) {}
};

}

class RetainedTemplateValidator
{
public:
	RetainedTemplateValidator(SemanticAnalyzer& analyzer, NodeId target,
		ScopeId lexical_scope, const std::vector<NameId>& parameters)
		: analyzer_(analyzer), target_(target), lexical_scope_(lexical_scope),
		  parameters_(parameters) {}

	void Run();

private:
	std::size_t AddScope(ScopeId semantic_scope, std::size_t parent,
		bool defer_unknown_members);
	std::size_t AddChildScope(std::size_t parent, ScopeKind kind,
		bool defer_unknown_members = false);
	void DeclareParameter(std::size_t scope, NameId name);
	void Declare(std::size_t scope, NameId name, RetainedNameKind kind,
		bool allow_existing = false);
	std::uint8_t LookupLocal(std::size_t scope, NameId name) const;
	bool DefersUnknownMembers(std::size_t scope) const;
	bool IsQualifiedMemberDefinition(NodeId node) const;
	bool IsTypedef(NodeId specifiers) const;
	bool HasBaseClass(NodeId node) const;
	bool SpellingUsesTemplateParameter(const std::string& spelling) const;
	void Visit(NodeId node, std::size_t scope, bool unknown_callee = false);
	void VisitChildren(NodeId node, std::size_t scope);
	void VisitClass(NodeId node, std::size_t scope);
	void PredeclareClassMembers(NodeId node, std::size_t scope);
	void PredeclareClassSimple(NodeId node, std::size_t scope);
	void DeclareEnumValues(NodeId node, std::size_t scope);
	void VisitFunction(NodeId node, std::size_t scope);
	void BindFunctionParameters(NodeId declarator, std::size_t scope);
	void VisitSimple(NodeId node, std::size_t scope, bool predeclared);
	void VisitUsing(NodeId node, std::size_t scope);
	void VisitIdExpression(NodeId node, std::size_t scope,
		bool unknown_callee);
	void ValidateSpecialMemberExceptionSpecification();
	bool IsNonthrowingSyntax(NodeId declarator) const;
	std::size_t ParameterCount(NodeId declarator) const;

	SemanticAnalyzer& analyzer_;
	NodeId target_;
	ScopeId lexical_scope_;
	const std::vector<NameId>& parameters_;
	std::unordered_set<NameId> parameter_names_;
	std::vector<RetainedScope> scopes_;
};

std::size_t RetainedTemplateValidator::AddScope(ScopeId semantic_scope,
	std::size_t parent, bool defer_unknown_members)
{
	const std::size_t index = scopes_.size();
	scopes_.push_back(RetainedScope(semantic_scope, parent,
		defer_unknown_members));
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
	NameId name)
{
	if (name == 0) return;
	if (!parameter_names_.insert(name).second)
		throw std::runtime_error("duplicate template parameter");
	scopes_[scope].names[name] |= RETAINED_TYPE_NAME;
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

bool RetainedTemplateValidator::DefersUnknownMembers(std::size_t scope) const
{
	while (scope != std::numeric_limits<std::size_t>::max())
	{
		if (scopes_[scope].defer_unknown_members) return true;
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

bool RetainedTemplateValidator::SpellingUsesTemplateParameter(
	const std::string& spelling) const
{
	for (std::size_t parameter = 0; parameter < parameters_.size(); ++parameter)
	{
		if (parameters_[parameter] == 0) continue;
		const std::string name =
			analyzer_.program_->names.Get(parameters_[parameter]);
		std::size_t found = spelling.find(name);
		while (found != std::string::npos)
		{
			const bool left = found == 0 ||
				(!std::isalnum(static_cast<unsigned char>(spelling[found - 1])) &&
				 spelling[found - 1] != '_');
			const std::size_t after = found + name.size();
			const bool right = after == spelling.size() ||
				(!std::isalnum(static_cast<unsigned char>(spelling[after])) &&
				 spelling[after] != '_');
			if (left && right) return true;
			found = spelling.find(name, found + 1);
		}
	}
	return false;
}

void RetainedTemplateValidator::VisitChildren(NodeId node, std::size_t scope)
{
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		Visit(analyzer_.arena_->EdgeChild(edge), scope);
}

void RetainedTemplateValidator::BindFunctionParameters(NodeId declarator,
	std::size_t scope)
{
	const NodeId clause = analyzer_.FindChild(declarator, "parameter-clause");
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
			Declare(scope, analyzer_.DeclaratorName(parameter_declarator),
				RETAINED_VALUE_NAME);
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
	}
}

void RetainedTemplateValidator::VisitClass(NodeId node, std::size_t scope)
{
	const std::size_t class_scope = AddChildScope(
		scope, SCOPE_CLASS, HasBaseClass(node));
	PredeclareClassMembers(node, class_scope);
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
						RETAINED_VALUE_NAME);
			}
	}
	VisitChildren(node, scope);
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
	const NamePath path = analyzer_.ParseNamePath(target);
	const NameId name = path.Last();
	if (parameter_names_.find(name) != parameter_names_.end())
		throw std::runtime_error("using declaration redeclares template parameter");
	if (SpellingUsesTemplateParameter(target))
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
	if (!analyzer_.FindFunctionTemplates(
		scopes_[scope].semantic_scope, target).empty())
	{
		Declare(scope, name, RETAINED_VALUE_NAME, true);
		return;
	}
	const LookupResult ordinary = analyzer_.LookupPath(
		scopes_[scope].semantic_scope, path, LOOKUP_ORDINARY);
	if (ordinary.ordinary != kNoBinding)
		Declare(scope, name, RETAINED_VALUE_NAME, true);
	else
	{
		const LookupResult type = analyzer_.LookupPath(
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
	const NamePath path = analyzer_.ParseNamePath(spelling);
	if (path.Empty()) return;
	if (path.global || path.Size() > 1)
	{
		if (SpellingUsesTemplateParameter(spelling)) return;
		if (!path.global &&
			(LookupLocal(scope, path[0]) & RETAINED_TYPE_NAME) != 0)
			return;
		const LookupResult ordinary = analyzer_.LookupSpelling(
			scopes_[scope].semantic_scope, spelling, LOOKUP_ORDINARY);
		if (ordinary.ordinary != kNoBinding) return;
		const LookupResult type = analyzer_.LookupSpelling(
			scopes_[scope].semantic_scope, spelling, LOOKUP_TYPE);
		if (type.type != kNoType)
		{
			if (unknown_callee) return;
			throw std::runtime_error("type name used as retained value");
		}
		return;
	}
	const NameId name = path.Last();
	const std::uint8_t local = LookupLocal(scope, name);
	if ((local & RETAINED_VALUE_NAME) != 0) return;
	if ((local & RETAINED_TYPE_NAME) != 0)
	{
		if (unknown_callee) return;
		throw std::runtime_error("type name used as retained value");
	}
	const LookupResult ordinary = analyzer_.program_->LookupName(
		scopes_[scope].semantic_scope, name, LOOKUP_ORDINARY);
	if (ordinary.ordinary != kNoBinding) return;
	const LookupResult type = analyzer_.program_->LookupName(
		scopes_[scope].semantic_scope, name, LOOKUP_TYPE);
	if (type.type != kNoType)
	{
		if (unknown_callee) return;
		throw std::runtime_error("type name used as retained value");
	}
	if (unknown_callee || DefersUnknownMembers(scope)) return;
	throw std::runtime_error("unknown nondependent name in template definition");
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
	if (analyzer_.arena_->IsTag(node, "compound-statement"))
	{
		const std::size_t block = AddChildScope(scope, SCOPE_BLOCK);
		VisitChildren(node, block);
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
	const NodeId clause = analyzer_.FindChild(declarator, "parameter-clause");
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
	std::string owner = analyzer_.program_->names.Get(path[0]);
	const std::size_t angle = owner.find('<');
	if (angle != std::string::npos) owner.erase(angle);
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
	if (!definition) return;
	ValidateSpecialMemberExceptionSpecification();
	const ScopeId semantic = analyzer_.NewScope(lexical_scope_,
		SCOPE_TEMPLATE_PARAMETERS, 0,
		analyzer_.ScopePrefixId(lexical_scope_));
	const std::size_t root = AddScope(semantic,
		std::numeric_limits<std::size_t>::max(),
		IsQualifiedMemberDefinition(target_));
	for (std::size_t i = 0; i < parameters_.size(); ++i)
		DeclareParameter(root, parameters_[i]);
	Visit(target_, root);
}

void SemanticAnalyzer::ValidateRetainedTemplateDefinition(NodeId target,
	ScopeId scope, const std::vector<NameId>& parameters)
{
	RetainedTemplateValidator(*this, target, scope, parameters).Run();
}

}
}
