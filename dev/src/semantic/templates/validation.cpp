#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cppgm
{
namespace semantic
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

enum RetainedSpecialMemberKind
{
	RETAINED_CONSTRUCTOR,
	RETAINED_DESTRUCTOR,
	RETAINED_CONVERSION_FUNCTION
};

enum RetainedExceptionState
{
	RETAINED_EXCEPTION_THROWING,
	RETAINED_EXCEPTION_NONTHROWING,
	RETAINED_EXCEPTION_DEFERRED
};

struct RetainedTemplateParameterKey
{
	NameId name;
	bool pack;

	RetainedTemplateParameterKey(NameId name_value, bool pack_value)
		: name(name_value), pack(pack_value) {}
};

struct RetainedTemplateParameterRange
{
	std::uint32_t first;
	std::uint32_t count;

	RetainedTemplateParameterRange() : first(0), count(0) {}
};

struct RetainedCurrentClass
{
	NameId name;
	NodeId source;
	RetainedTemplateParameterRange parameters;

	RetainedCurrentClass() : name(0), source(kNoNode) {}
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
	std::unordered_set<NameId> class_type_names;
	std::unordered_set<NameId> class_type_definitions;
	RetainedTemplateParameterRange template_parameters;
	RetainedCurrentClass current_class;
	std::uint32_t switch_entry_barriers;
	bool defer_unknown_members;
	bool unmodeled_fixed_base;
	bool unmodeled_current_class;

	RetainedScope(ScopeId semantic, std::size_t owner, bool defer,
		bool fixed_base, bool current_class)
		: semantic_scope(semantic), parent(owner), switch_entry_barriers(0),
		  defer_unknown_members(defer),
		  unmodeled_fixed_base(fixed_base),
		  unmodeled_current_class(current_class) {}
};

class ScopedRetainedClassContext
{
public:
	ScopedRetainedClassContext(EntityId* slot, EntityId value)
		: slot_(slot), previous_(*slot) { *slot_ = value; }
	~ScopedRetainedClassContext() { *slot_ = previous_; }

private:
	ScopedRetainedClassContext(const ScopedRetainedClassContext&);
	ScopedRetainedClassContext& operator=(const ScopedRetainedClassContext&);
	EntityId* slot_;
	EntityId previous_;
};

}

class RetainedTemplateValidator
{
public:
	RetainedTemplateValidator(Analyzer& analyzer, NodeId target,
		ScopeId lexical_scope, const std::vector<TemplateParameter>& parameters,
		NodeId class_declaration)
		: analyzer_(analyzer), target_(target), lexical_scope_(lexical_scope),
		  parameters_(parameters), class_declaration_(class_declaration) {}

	void Run();

private:
	typedef std::unordered_map<NameId, std::size_t> TemplateOrdinalMap;

	std::size_t AddScope(ScopeId semantic_scope, std::size_t parent,
		bool defer_unknown_members, bool unmodeled_fixed_base = false,
		bool unmodeled_current_class = false);
	std::size_t AddChildScope(std::size_t parent, ScopeKind kind,
		bool defer_unknown_members = false);
	void DeclareParameter(std::size_t scope,
		const TemplateParameter& parameter);
	void Declare(std::size_t scope, NameId name, RetainedNameKind kind,
		bool allow_existing = false);
	void DeclareClassType(std::size_t scope, NameId name, bool definition);
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
	bool IsCurrentInstantiationQualifier(
		NodeId component, std::size_t scope) const;
	bool RequiresDependentTypename(NodeId node, std::size_t scope) const;
	void ValidateDependentTypenameSpecifiers(
		NodeId sequence, std::size_t scope) const;
	bool IsLoneQualifiedNameSpecifier(NodeId sequence) const;
	void SetTemplateParameterRange(std::size_t scope,
		const std::vector<TemplateParameter>& parameters);
	bool SyntaxUsesRetainedType(NodeId node, std::size_t scope) const;
	bool SyntaxUsesRetainedValue(NodeId node, std::size_t scope) const;
	void Visit(NodeId node, std::size_t scope, bool unknown_callee = false);
	void VisitChildren(NodeId node, std::size_t scope);
	bool VisitSwitchLabel(NodeId node, std::size_t scope);
	bool VisitControlStatement(NodeId node, std::size_t scope);
	void VisitClass(NodeId node, std::size_t scope);
	void PredeclareClassMembers(NodeId node, std::size_t scope);
	void PredeclareClassSimple(NodeId node, std::size_t scope);
	void DeclareEnumValues(NodeId node, std::size_t scope);
	void VisitFunction(NodeId node, std::size_t scope);
	NodeId FindParameterClause(NodeId declarator) const;
	void BindFunctionParameters(NodeId declarator, std::size_t scope);
	bool DeclareStructuredBindings(NodeId declarator, std::size_t scope);
	NodeId RetainedOperatorCallArgument(NodeId node) const;
	void VisitSimple(NodeId node, std::size_t scope, bool predeclared);
	void VisitUsing(NodeId node, std::size_t scope);
	void VisitSizeof(NodeId node, std::size_t scope);
	void VisitIdExpression(NodeId node, std::size_t scope,
		bool unknown_callee);
	void ValidateKnownTemplateArgumentKinds(NodeId node, ScopeId scope);
	const TemplateParameter* TemplateParameterUsedBy(NodeId node) const;
	void ValidateSpecialMemberExceptionSpecification();
	RetainedSpecialMemberKind SpecialMemberKind(NodeId node) const;
	TemplateOrdinalMap TemplateOrdinals(
		const std::vector<TemplateParameter>& parameters) const;
	bool RetainedTypeSyntaxEquivalent(NodeId left, NodeId right,
		NodeId left_identifier, NodeId right_identifier,
		const TemplateOrdinalMap& left_parameters,
		const TemplateOrdinalMap& right_parameters) const;
	bool ParameterTypesEquivalent(NodeId left, NodeId right,
		const std::vector<TemplateParameter>& left_parameters) const;
	bool FunctionQualifiersEquivalent(NodeId left, NodeId right) const;
	RetainedExceptionState RetainedExceptionSpecificationState(
		NodeId declarator) const;

	Analyzer& analyzer_;
	NodeId target_;
	ScopeId lexical_scope_;
	const std::vector<TemplateParameter>& parameters_;
	NodeId class_declaration_;
	std::unordered_set<NameId> parameter_names_;
	std::unordered_set<NodeId> template_argument_validation_visited_;
	std::vector<RetainedTemplateParameterKey> template_parameter_keys_;
	std::vector<RetainedScope> scopes_;
	std::vector<std::size_t> switch_entry_scopes_;
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
		ThrowSemanticError("duplicate template parameter");
	scopes_[scope].names[name] |= parameter.kind == TEMPLATE_ARGUMENT_INTEGRAL ?
		RETAINED_VALUE_NAME : RETAINED_TYPE_NAME;
}

void RetainedTemplateValidator::Declare(std::size_t scope, NameId name,
	RetainedNameKind kind, bool allow_existing)
{
	if (name == 0) return;
	if (parameter_names_.find(name) != parameter_names_.end())
		ThrowSemanticError("template parameter redeclared in its scope");
	std::uint8_t& present = scopes_[scope].names[name];
	if ((present & kind) != 0 && !allow_existing)
		ThrowSemanticError("duplicate retained template declaration: " +
			analyzer_.program_->names.Get(name) +
			(kind == RETAINED_TYPE_NAME ? " (type)" : " (value)"));
	const bool newly_declared = (present & kind) == 0;
	present |= static_cast<std::uint8_t>(kind);
	if (kind == RETAINED_TYPE_NAME && newly_declared)
	{
		// Nested template-parameter parsing uses the validator's synthetic
		// semantic scope.  Mirror validation-only class type declarations there
		// so an earlier typedef/alias/injected name is visible at that boundary;
		// concrete replay will publish its canonical substituted type.
		analyzer_.program_->AddBinding(scopes_[scope].semantic_scope,
			BIND_TYPE_ALIAS, name,
			analyzer_.program_->types.Fundamental(FUND_INT));
	}
}

void RetainedTemplateValidator::DeclareClassType(std::size_t scope,
	NameId name, bool definition)
{
	const bool existing_tag = !scopes_[scope].class_type_names.insert(name).second;
	Declare(scope, name, RETAINED_TYPE_NAME, existing_tag);
	if (definition &&
		!scopes_[scope].class_type_definitions.insert(name).second)
		ThrowSemanticError("duplicate retained class definition: " +
			analyzer_.program_->names.Get(name));
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
	const NodeId declarator = analyzer_.FindChild(node, ::cppgm::syntax::STAG_DECLARATOR);
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
	const NodeId clause = analyzer_.FindChild(node, ::cppgm::syntax::STAG_BASE_CLAUSE);
	return clause != kNoNode;
}

bool RetainedTemplateValidator::SyntaxUsesTemplateParameter(NodeId node) const
{
	if (node == kNoNode) return false;
	const TextId semantic_name = analyzer_.arena_->SemanticPayloadId(node);
	if (semantic_name != 0 &&
		parameter_names_.find(semantic_name) != parameter_names_.end())
		return true;
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		if (SyntaxUsesTemplateParameter(analyzer_.arena_->EdgeChild(edge)))
			return true;
	return false;
}

bool RetainedTemplateValidator::IsCurrentInstantiationQualifier(
	NodeId component, std::size_t scope) const
{
	const NameId name = analyzer_.arena_->SemanticPayloadId(component);
	for (std::size_t current = scope;
		current != std::numeric_limits<std::size_t>::max();
		current = scopes_[current].parent)
	{
		const RetainedScope& owner = scopes_[current];
		if (owner.current_class.name != name) continue;
		const NodeId arguments = analyzer_.FindChild(
			component, ::cppgm::syntax::STAG_TEMPLATE_TYPE_ARGUMENT_LIST);
		if (arguments == kNoNode) return false;
		if (owner.current_class.source != kNoNode &&
			analyzer_.FindChild(owner.current_class.source,
				::cppgm::syntax::STAG_TEMPLATE_TYPE_ARGUMENT_LIST) != kNoNode)
		{
			if (!analyzer_.arena_->HasTokenRange(component) ||
				!analyzer_.arena_->HasTokenRange(owner.current_class.source))
				return false;
			const std::size_t left_first =
				analyzer_.arena_->TokenFirst(component);
			const std::size_t right_first =
				analyzer_.arena_->TokenFirst(owner.current_class.source);
			const std::size_t left_size =
				analyzer_.arena_->TokenLast(component) - left_first;
			const std::size_t right_size =
				analyzer_.arena_->TokenLast(owner.current_class.source) - right_first;
			if (left_size != right_size) return false;
			for (std::size_t i = 0; i < left_size; ++i)
				if (analyzer_.arena_->TokenSpellingId(left_first + i) !=
					analyzer_.arena_->TokenSpellingId(right_first + i))
					return false;
			return true;
		}
		std::vector<NodeId> explicit_arguments;
		for (std::uint32_t edge = analyzer_.arena_->FirstEdge(arguments);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
			explicit_arguments.push_back(analyzer_.arena_->EdgeChild(edge));
		if (explicit_arguments.size() != owner.current_class.parameters.count)
			return false;
		for (std::size_t i = 0; i < explicit_arguments.size(); ++i)
		{
			const RetainedTemplateParameterKey& parameter =
				template_parameter_keys_[
					owner.current_class.parameters.first + i];
			const NodeId argument = explicit_arguments[i];
			if (!analyzer_.arena_->HasTokenRange(argument)) return false;
			const std::size_t first = analyzer_.arena_->TokenFirst(argument);
			const std::size_t last = analyzer_.arena_->TokenLast(argument);
			const std::size_t expected_tokens =
				parameter.pack ? 2 : 1;
			if (last - first != expected_tokens ||
				analyzer_.arena_->TokenSpellingId(first) !=
					parameter.name)
				return false;
			if (expected_tokens == 2 &&
				analyzer_.arena_->TokenKind(first + 1) != OP_DOTS)
				return false;
		}
		return true;
	}
	return false;
}

bool RetainedTemplateValidator::RequiresDependentTypename(
	NodeId node, std::size_t scope) const
{
	if ((!analyzer_.arena_->IsTag(
			node, ::cppgm::syntax::STAG_DECL_SPECIFIER) &&
		 !analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_TYPE_NAME)) ||
		(analyzer_.arena_->Flags(node) & SYNTAX_FLAG_TYPENAME) != 0)
		return false;
	const NodeId structure = analyzer_.FindChild(
		node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
	if (structure == kNoNode) return false;
	std::vector<NodeId> components;
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(structure);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId component = analyzer_.arena_->EdgeChild(edge);
		if (analyzer_.arena_->IsTag(
			component, ::cppgm::syntax::STAG_NAME_COMPONENT))
			components.push_back(component);
	}
	if (components.size() < 2) return false;
	bool dependent_qualifier = false;
	for (std::size_t i = 0; i + 1 < components.size(); ++i)
		if (SyntaxUsesTemplateParameter(components[i]))
		{
			dependent_qualifier = true;
			break;
		}
	if (!dependent_qualifier) return false;

	// A type member found directly in the current instantiation is already
	// established as a type.  Other dependent qualified names require the
	// C++11 `typename` introducer even when a later substitution happens to
	// resolve the terminal member as a type.
	if (components.size() == 2 &&
		IsCurrentInstantiationQualifier(components[0], scope))
	{
		const NameId member = analyzer_.arena_->SemanticPayloadId(components[1]);
		if ((LookupLocal(scope, member) & RETAINED_TYPE_NAME) != 0)
			return false;
	}
	return true;
}

void RetainedTemplateValidator::ValidateDependentTypenameSpecifiers(
	NodeId sequence, std::size_t scope) const
{
	if (sequence == kNoNode) return;
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(sequence);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId specifier = analyzer_.arena_->EdgeChild(edge);
		if (RequiresDependentTypename(specifier, scope))
			ThrowSemanticError(
				"dependent qualified type requires typename: " +
				analyzer_.PayloadSource(specifier));
	}
}

bool RetainedTemplateValidator::IsLoneQualifiedNameSpecifier(
	NodeId sequence) const
{
	if (sequence == kNoNode) return false;
	const std::uint32_t edge = analyzer_.arena_->FirstEdge(sequence);
	if (edge == kNoEdge || analyzer_.arena_->NextEdge(edge) != kNoEdge)
		return false;
	const NodeId specifier = analyzer_.arena_->EdgeChild(edge);
	if (!analyzer_.arena_->IsTag(
		specifier, ::cppgm::syntax::STAG_DECL_SPECIFIER)) return false;
	const NodeId structure = analyzer_.FindChild(
		specifier, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
	return structure != kNoNode &&
		analyzer_.StructuredNamePath(structure).Size() > 1;
}

void RetainedTemplateValidator::SetTemplateParameterRange(
	std::size_t scope, const std::vector<TemplateParameter>& parameters)
{
	const std::size_t limit = std::numeric_limits<std::uint32_t>::max();
	if (parameters.size() > limit ||
		template_parameter_keys_.size() > limit - parameters.size())
		ThrowInternalCompilerError(
			"retained template parameter table overflow");
	RetainedTemplateParameterRange& range =
		scopes_[scope].template_parameters;
	range.first = static_cast<std::uint32_t>(
		template_parameter_keys_.size());
	range.count = static_cast<std::uint32_t>(parameters.size());
	for (std::size_t i = 0; i < parameters.size(); ++i)
		template_parameter_keys_.push_back(RetainedTemplateParameterKey(
			parameters[i].name, parameters[i].pack));
}

bool RetainedTemplateValidator::SyntaxUsesRetainedType(
	NodeId node, std::size_t scope) const
{
	if (node == kNoNode) return false;
	const bool structured = analyzer_.FindChild(
		node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME) != kNoNode;
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_NAME_COMPONENT) ||
		(!structured &&
		 (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_BASE_NAME) ||
		  analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_ID_EXPRESSION) ||
		  analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_TARGET) ||
		  analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_TYPE_NAME) ||
		  analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_DECL_SPECIFIER))))
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
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_ID_EXPRESSION) &&
		analyzer_.FindChild(node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME) == kNoNode)
	{
		const NamePath path = analyzer_.SyntaxNamePath(node);
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
					analyzer_.FindChild(syntax, ::cppgm::syntax::STAG_TYPE_ID);
				const NodeId specifiers = type_id == kNoNode ? kNoNode :
					analyzer_.FindChild(type_id, ::cppgm::syntax::STAG_TYPE_SPECIFIER_SEQ);
				const NodeId direct_name = specifiers == kNoNode ? kNoNode :
					analyzer_.FirstSemanticChild(specifiers);
				const TemplateParameter* source = direct_name == kNoNode ? 0 :
					TemplateParameterUsedBy(direct_name);
				const NameId direct_id = direct_name == kNoNode ? 0 :
					analyzer_.program_->names.UseInterned(
						analyzer_.arena_->SemanticPayloadId(direct_name));
				const NodeId abstract = analyzer_.FindChild(
					syntax, ::cppgm::syntax::STAG_ABSTRACT_DECLARATOR);
				const NodeId declarator = abstract == kNoNode ?
					analyzer_.FindChild(syntax, ::cppgm::syntax::STAG_DECLARATOR) : abstract;
				const bool direct_type_pack = declarator != kNoNode &&
					analyzer_.FindChild(
						declarator, ::cppgm::syntax::STAG_PARAMETER_PACK) != kNoNode;
				if (source && direct_id == source->name && source->pack &&
					direct_type_pack &&
					source->kind != destination.kind)
					ThrowSemanticError(
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

bool RetainedTemplateValidator::VisitControlStatement(
	NodeId node, std::size_t scope)
{
	if (!analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_IF_STATEMENT) &&
		!analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_SWITCH_STATEMENT) &&
		!analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_WHILE_STATEMENT) &&
		!analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_DO_STATEMENT) &&
		!analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_FOR_STATEMENT) &&
		!analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_THEN) &&
		!analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_ELSE)) return false;
	const std::size_t control = AddChildScope(scope, SCOPE_BLOCK);
	if (analyzer_.arena_->IsTag(node,
		::cppgm::syntax::STAG_SWITCH_STATEMENT))
	{
		switch_entry_scopes_.push_back(control);
		VisitChildren(node, control);
		switch_entry_scopes_.pop_back();
	}
	else VisitChildren(node, control);
	return true;
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
			if (analyzer_.arena_->IsTag(child, ::cppgm::syntax::STAG_PARAMETER_CLAUSE)) return child;
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
		if (!analyzer_.arena_->IsTag(parameter, ::cppgm::syntax::STAG_PARAMETER_DECLARATION))
			continue;
		const NodeId parameter_specifiers = analyzer_.FindChild(
			parameter, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
		const NodeId parameter_declarator =
			analyzer_.FindChild(parameter, ::cppgm::syntax::STAG_DECLARATOR);
		// A dependent qualified-id without typename does not name a type
		// (N3485 14.6/3).  A parameter-declaration that is nothing but such a
		// name therefore cannot be a parameter: the parenthesized list is a
		// direct-initializer, which the declaration owner resolves at replay.
		if (parameter_declarator != kNoNode ||
			analyzer_.FindChild(parameter,
				::cppgm::syntax::STAG_ABSTRACT_DECLARATOR) != kNoNode ||
			!IsLoneQualifiedNameSpecifier(parameter_specifiers))
			ValidateDependentTypenameSpecifiers(parameter_specifiers, scope);
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
	ValidateDependentTypenameSpecifiers(analyzer_.FindChild(
		node, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ), scope);
	const bool qualified = IsQualifiedMemberDefinition(node);
	const std::size_t function_scope = AddChildScope(
		scope, SCOPE_FUNCTION, qualified);
	const NodeId declarator = analyzer_.FindChild(node, ::cppgm::syntax::STAG_DECLARATOR);
	if (qualified && declarator != kNoNode)
	{
		const NodeId structure = analyzer_.DeclaratorNameStructure(declarator);
		std::vector<NodeId> components;
		for (std::uint32_t edge = structure == kNoNode ? kNoEdge :
			analyzer_.arena_->FirstEdge(structure);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		{
			const NodeId component = analyzer_.arena_->EdgeChild(edge);
			if (analyzer_.arena_->IsTag(
				component, ::cppgm::syntax::STAG_NAME_COMPONENT))
				components.push_back(component);
		}
		if (components.size() > 1)
		{
			const NodeId owner = components[components.size() - 2];
			scopes_[function_scope].current_class.name =
				analyzer_.arena_->SemanticPayloadId(owner);
			scopes_[function_scope].current_class.source = owner;
			scopes_[function_scope].current_class.parameters =
				scopes_[scope].template_parameters;
		}
	}
	if (declarator != kNoNode)
		BindFunctionParameters(declarator, function_scope);
	const NodeId initializer = analyzer_.FindChild(node, ::cppgm::syntax::STAG_CTOR_INITIALIZER);
	if (initializer != kNoNode) Visit(initializer, function_scope);
	const NodeId body = analyzer_.FindChild(node, ::cppgm::syntax::STAG_COMPOUND_STATEMENT);
	if (body != kNoNode) Visit(body, function_scope);
}

bool RetainedTemplateValidator::DeclareStructuredBindings(
	NodeId declarator, std::size_t scope)
{
	const NodeId bindings = analyzer_.FindChild(
		declarator, ::cppgm::syntax::STAG_STRUCTURED_BINDING);
	if (bindings == kNoNode) return false;
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(bindings);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId binding = analyzer_.arena_->EdgeChild(edge);
		if (!analyzer_.arena_->IsTag(binding, ::cppgm::syntax::STAG_BINDING_IDENTIFIER))
			ThrowSemanticError("invalid retained structured binding");
		Declare(scope, analyzer_.program_->names.Intern(
			analyzer_.arena_->Payload(binding)), RETAINED_VALUE_NAME);
	}
	return true;
}

void RetainedTemplateValidator::PredeclareClassSimple(NodeId node,
	std::size_t scope)
{
	const NodeId specifiers = analyzer_.FindChild(node, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
	NameId embedded_type = 0;
	if (specifiers != kNoNode)
		for (std::uint32_t edge = analyzer_.arena_->FirstEdge(specifiers);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		{
			const NodeId specifier = analyzer_.arena_->EdgeChild(edge);
			if (analyzer_.arena_->IsTag(specifier, ::cppgm::syntax::STAG_ENUM_SPECIFIER))
			{
				DeclareEnumValues(specifier, scope);
				continue;
			}
			if (!analyzer_.arena_->IsTag(specifier, ::cppgm::syntax::STAG_CLASS_SPECIFIER) &&
				!analyzer_.arena_->IsTag(specifier,
					::cppgm::syntax::STAG_CLASS_FORWARD_DECLARATION))
				continue;
			const std::string spelling = analyzer_.arena_->Payload(specifier);
			if (!spelling.empty())
			{
				embedded_type = analyzer_.program_->names.Intern(spelling);
				DeclareClassType(scope, embedded_type,
					analyzer_.arena_->IsTag(specifier, ::cppgm::syntax::STAG_CLASS_SPECIFIER));
			}
		}
	const bool type_declaration = IsTypedef(specifiers);
	const NodeId list = analyzer_.FindChild(node, ::cppgm::syntax::STAG_INIT_DECLARATOR_LIST);
	if (list == kNoNode) return;
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(list);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId declarator = analyzer_.FindChild(
			analyzer_.arena_->EdgeChild(edge), ::cppgm::syntax::STAG_DECLARATOR);
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
		if (!analyzer_.arena_->IsTag(enumerator, ::cppgm::syntax::STAG_ENUMERATOR)) continue;
		Declare(scope, analyzer_.program_->names.Intern(
			analyzer_.arena_->Payload(enumerator)), RETAINED_VALUE_NAME, true);
	}
}

void RetainedTemplateValidator::PredeclareClassMembers(NodeId node,
	std::size_t scope)
{
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId member = analyzer_.arena_->EdgeChild(edge);
		if (analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_SIMPLE_DECLARATION))
			PredeclareClassSimple(member, scope);
		else if (analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_ALIAS_DECLARATION))
			Declare(scope, analyzer_.program_->names.Intern(
				analyzer_.arena_->Payload(member)), RETAINED_TYPE_NAME);
		else if (analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_CLASS_SPECIFIER) ||
			analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_CLASS_FORWARD_DECLARATION))
		{
			if (analyzer_.arena_->Payload(member).empty())
				PredeclareClassMembers(member, scope);
			else DeclareClassType(scope, analyzer_.program_->names.Intern(
				analyzer_.arena_->Payload(member)),
				analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_CLASS_SPECIFIER));
		}
		else if (analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_BIT_FIELD_DECLARATION))
		{
			for (std::uint32_t field_edge =
					analyzer_.arena_->FirstEdge(member);
				field_edge != kNoEdge;
				field_edge = analyzer_.arena_->NextEdge(field_edge))
			{
				const NodeId field = analyzer_.arena_->EdgeChild(field_edge);
				if (!analyzer_.arena_->IsTag(field, ::cppgm::syntax::STAG_BIT_FIELD_DECLARATOR))
					continue;
				const NodeId declarator =
					analyzer_.FindChild(field, ::cppgm::syntax::STAG_DECLARATOR);
				if (declarator != kNoNode)
					Declare(scope, analyzer_.DeclaratorName(declarator),
						RETAINED_VALUE_NAME);
			}
		}
		else if (analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_FUNCTION_DEFINITION) ||
			analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_SPECIAL_MEMBER_DEFINITION) ||
			analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_SPECIAL_MEMBER_DECLARATION))
		{
			const NodeId declarator = analyzer_.FindChild(member, ::cppgm::syntax::STAG_DECLARATOR);
			if (declarator != kNoNode)
				Declare(scope, analyzer_.DeclaratorName(declarator),
					RETAINED_VALUE_NAME, true);
		}
		else if (analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_TEMPLATE_DECLARATION))
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
			if (analyzer_.arena_->IsTag(target, ::cppgm::syntax::STAG_SIMPLE_DECLARATION))
				PredeclareClassSimple(target, scope);
			else
			{
				const NodeId declarator =
					analyzer_.FindChild(target, ::cppgm::syntax::STAG_DECLARATOR);
				if (declarator != kNoNode)
					Declare(scope, analyzer_.DeclaratorName(declarator),
						RETAINED_VALUE_NAME, true);
			}
		}
	}
}

void RetainedTemplateValidator::VisitClass(NodeId node, std::size_t scope)
{
	NameId injected = 0;
	const NodeId injected_structure = analyzer_.FindChild(
		node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
	if (injected_structure != kNoNode)
	{
		const NamePath path =
			analyzer_.StructuredNamePath(injected_structure);
		if (!path.Empty()) injected = path.Last();
	}
	else if (!analyzer_.arena_->Payload(node).empty())
		injected = analyzer_.program_->names.Intern(
			analyzer_.arena_->Payload(node));
	bool dependent_base = false;
	const NodeId base_clause = analyzer_.FindChild(
		node, ::cppgm::syntax::STAG_BASE_CLAUSE);
	if (base_clause != kNoNode)
		for (std::uint32_t edge = analyzer_.arena_->FirstEdge(base_clause);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		{
			const NodeId base = analyzer_.arena_->EdgeChild(edge);
			if (!analyzer_.arena_->IsTag(
					base, ::cppgm::syntax::STAG_BASE_SPECIFIER)) continue;
			const NodeId name = analyzer_.FindChild(
				base, ::cppgm::syntax::STAG_BASE_NAME);
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
	if (injected != 0)
	{
		scopes_[class_scope].current_class.name = injected;
		scopes_[class_scope].current_class.source =
			injected_structure == kNoNode ? kNoNode :
				analyzer_.arena_->TerminalNameComponent(injected_structure);
		scopes_[class_scope].current_class.parameters =
			scopes_[scope].template_parameters;
		Declare(class_scope, injected, RETAINED_TYPE_NAME, true);
	}
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(node);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId member = analyzer_.arena_->EdgeChild(edge);
		if (analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_SIMPLE_DECLARATION))
			VisitSimple(member, class_scope, true);
		else if (analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_ALIAS_DECLARATION))
		{
			const NodeId type_id = analyzer_.FindChild(
				member, ::cppgm::syntax::STAG_TYPE_ID);
			ValidateDependentTypenameSpecifiers(type_id == kNoNode ? kNoNode :
				analyzer_.FindChild(type_id,
					::cppgm::syntax::STAG_TYPE_SPECIFIER_SEQ), class_scope);
			VisitChildren(member, class_scope);
		}
		else Visit(member, class_scope);
	}
}

NodeId RetainedTemplateValidator::RetainedOperatorCallArgument(
	NodeId node) const
{
	const NodeId specifiers = analyzer_.FindChild(node, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
	const NodeId list = analyzer_.FindChild(node, ::cppgm::syntax::STAG_INIT_DECLARATOR_LIST);
	const std::uint32_t specifier_edge = specifiers == kNoNode ? kNoEdge :
		analyzer_.arena_->FirstEdge(specifiers);
	const std::uint32_t item_edge = list == kNoNode ? kNoEdge :
		analyzer_.arena_->FirstEdge(list);
	if (specifier_edge == kNoEdge || item_edge == kNoEdge ||
		analyzer_.arena_->NextEdge(specifier_edge) != kNoEdge ||
		analyzer_.arena_->NextEdge(item_edge) != kNoEdge)
		return kNoNode;
	const NodeId specifier = analyzer_.arena_->EdgeChild(specifier_edge);
	const NodeId structure = analyzer_.FirstSemanticChild(specifier);
	if (structure == kNoNode ||
		!analyzer_.arena_->IsTag(structure, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME))
		return kNoNode;
	const NamePath callee = analyzer_.StructuredNamePath(specifier);
	if (callee.Empty() || analyzer_.program_->names.Get(callee.Last()).compare(
			0, 8, "operator") != 0)
		return kNoNode;
	const NodeId item = analyzer_.arena_->EdgeChild(item_edge);
	if (analyzer_.FindChild(item, ::cppgm::syntax::STAG_INITIALIZER) != kNoNode) return kNoNode;
	const NodeId declarator = analyzer_.FindChild(item, ::cppgm::syntax::STAG_DECLARATOR);
	const NodeId nested = declarator == kNoNode ? kNoNode :
		analyzer_.FindChild(declarator, ::cppgm::syntax::STAG_NESTED_DECLARATOR);
	const NodeId argument_declarator = nested == kNoNode ? kNoNode :
		analyzer_.FirstSemanticChild(nested);
	const NodeId argument = argument_declarator == kNoNode ? kNoNode :
		analyzer_.FindChild(argument_declarator, ::cppgm::syntax::STAG_IDENTIFIER);
	if (argument == kNoNode ||
		analyzer_.FindChild(argument_declarator, ::cppgm::syntax::STAG_PARAMETER_CLAUSE) != kNoNode ||
		analyzer_.FindChild(argument_declarator, ::cppgm::syntax::STAG_ARRAY_SUFFIX) != kNoNode)
		return kNoNode;
	return argument;
}

void RetainedTemplateValidator::VisitSimple(NodeId node, std::size_t scope,
	bool predeclared)
{
	const NodeId call_argument = RetainedOperatorCallArgument(node);
	if (call_argument != kNoNode)
	{
		// PA10 retains `qualified::operator=(argument);` in the declaration
		// branch.  It is resolved as a call during concrete replay and must not
		// publish the parenthesized argument as a fresh local declaration.
		const NameId name = analyzer_.program_->names.Intern(
			analyzer_.PayloadSource(call_argument));
		if ((LookupLocal(scope, name) & RETAINED_VALUE_NAME) == 0 &&
			analyzer_.program_->LookupName(scopes_[scope].semantic_scope,
				name, LOOKUP_ORDINARY).ordinary == kNoBinding &&
			!DefersUnknownMembers(scope) && !HasUnmodeledFixedBase(scope) &&
			!HasUnmodeledCurrentClass(scope))
			ThrowSemanticError(
				"unknown nondependent name in template definition: " +
				analyzer_.PayloadSource(call_argument));
		return;
	}
	const NodeId specifiers = analyzer_.FindChild(node, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
	ValidateDependentTypenameSpecifiers(specifiers, scope);
	if (!predeclared)
	{
		if (specifiers != kNoNode)
			for (std::uint32_t edge = analyzer_.arena_->FirstEdge(specifiers);
				edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
			{
				const NodeId specifier = analyzer_.arena_->EdgeChild(edge);
				if (analyzer_.arena_->IsTag(specifier, ::cppgm::syntax::STAG_ENUM_SPECIFIER))
				{
					DeclareEnumValues(specifier, scope);
					continue;
				}
				if (analyzer_.arena_->IsTag(specifier, ::cppgm::syntax::STAG_CLASS_SPECIFIER) ||
					analyzer_.arena_->IsTag(specifier,
						::cppgm::syntax::STAG_CLASS_FORWARD_DECLARATION))
				{
					const std::string spelling =
						analyzer_.arena_->Payload(specifier);
					if (!spelling.empty())
						DeclareClassType(scope,
							analyzer_.program_->names.Intern(spelling),
							analyzer_.arena_->IsTag(specifier, ::cppgm::syntax::STAG_CLASS_SPECIFIER));
				}
			}
		const bool type_declaration = IsTypedef(specifiers);
		const NodeId list = analyzer_.FindChild(node, ::cppgm::syntax::STAG_INIT_DECLARATOR_LIST);
		if (list != kNoNode)
			for (std::uint32_t edge = analyzer_.arena_->FirstEdge(list);
				edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
			{
				const NodeId declarator = analyzer_.FindChild(
					analyzer_.arena_->EdgeChild(edge), ::cppgm::syntax::STAG_DECLARATOR);
				if (declarator != kNoNode &&
					!DeclareStructuredBindings(declarator, scope))
					Declare(scope, analyzer_.DeclaratorName(declarator),
						type_declaration ? RETAINED_TYPE_NAME :
						RETAINED_VALUE_NAME,
						!type_declaration &&
						FindParameterClause(declarator) != kNoNode);
			}
	}
	const NodeId list = analyzer_.FindChild(node, ::cppgm::syntax::STAG_INIT_DECLARATOR_LIST);
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
		const NodeId declarator = analyzer_.FindChild(item, ::cppgm::syntax::STAG_DECLARATOR);
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
	if (!switch_entry_scopes_.empty() && !IsTypedef(specifiers) &&
		!analyzer_.HasDeclSpecifier(specifiers, "extern") &&
		!analyzer_.HasDeclSpecifier(specifiers, "static") &&
		!analyzer_.HasDeclSpecifier(specifiers, "thread_local"))
	{
		for (std::uint32_t edge = analyzer_.arena_->FirstEdge(list);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		{
			const NodeId item = analyzer_.arena_->EdgeChild(edge);
			if (analyzer_.FindChild(item,
				::cppgm::syntax::STAG_INITIALIZER) != kNoNode)
			{
				++scopes_[scope].switch_entry_barriers;
				break;
			}
		}
	}
}

void RetainedTemplateValidator::VisitUsing(NodeId node, std::size_t scope)
{
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_ALIAS_DECLARATION))
	{
		const NodeId type_id = analyzer_.FindChild(
			node, ::cppgm::syntax::STAG_TYPE_ID);
		ValidateDependentTypenameSpecifiers(type_id == kNoNode ? kNoNode :
			analyzer_.FindChild(
				type_id, ::cppgm::syntax::STAG_TYPE_SPECIFIER_SEQ), scope);
		const NameId name = analyzer_.program_->names.Intern(
			analyzer_.arena_->Payload(node));
		Declare(scope, name, RETAINED_TYPE_NAME);
		VisitChildren(node, scope);
		return;
	}
	const NodeId target_node = analyzer_.FindChild(node, ::cppgm::syntax::STAG_TARGET);
	if (target_node == kNoNode) return;
	const std::string target = analyzer_.arena_->Payload(target_node);
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_USING_DIRECTIVE))
	{
		const ScopeId target_scope = analyzer_.ResolveScopePath(
			scopes_[scope].semantic_scope,
			analyzer_.SyntaxNamePath(target_node));
		if (target_scope == kNoScope)
			ThrowSemanticError("retained using namespace target not found");
		analyzer_.program_->AddUsingEdge(
			scopes_[scope].semantic_scope, target_scope);
		return;
	}
	const NodeId structure = analyzer_.FindChild(
		target_node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
	const NamePath path = analyzer_.SyntaxNamePath(target_node);
	const NameId name = path.Last();
	if (parameter_names_.find(name) != parameter_names_.end())
		ThrowSemanticError("using declaration redeclares template parameter");
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
			scopes_[scope].semantic_scope, path) :
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
			ThrowSemanticError("retained using declaration target not found");
		Declare(scope, name, RETAINED_TYPE_NAME);
	}
}

void RetainedTemplateValidator::VisitIdExpression(NodeId node,
	std::size_t scope, bool unknown_callee)
{
	const std::string spelling = analyzer_.PayloadSource(node);
	if (spelling == "__func__" || spelling == "__PRETTY_FUNCTION__") return;
	const NodeId structure = analyzer_.FindChild(
		node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
	const NamePath path = analyzer_.SyntaxNamePath(node);
	if (path.Empty()) return;
	if (structure != kNoNode && path.Size() == 1 &&
		analyzer_.program_->names.Get(path.Last()) == "__type_pack_element") return;
	if (path.global || path.Size() > 1)
	{
		if (SyntaxUsesTemplateParameter(node)) return;
		if (SyntaxUsesRetainedType(node, scope)) return;
		if (SyntaxUsesRetainedValue(node, scope)) return;
		if (!path.global &&
			(LookupLocal(scope, path[0]) & RETAINED_TYPE_NAME) != 0)
			return;
		const LookupResult ordinary = analyzer_.LookupSyntaxName(
			node, scopes_[scope].semantic_scope, LOOKUP_ORDINARY);
		if (ordinary.ordinary != kNoBinding)
		{
			if (unknown_callee &&
				analyzer_.program_->bindings[ordinary.ordinary].kind ==
					BIND_FUNCTION)
				analyzer_.RecordRetainedCallLookup(node,
					scopes_[scope].semantic_scope, spelling, false);
			return;
		}
		const LookupResult type = analyzer_.LookupSyntaxName(
			node, scopes_[scope].semantic_scope, LOOKUP_TYPE);
		if (type.type != kNoType)
		{
			if (unknown_callee) return;
			ThrowSemanticError("type name used as retained value: " + spelling);
		}
		const std::vector<std::size_t> templates =
			analyzer_.FindFunctionTemplates(
				scopes_[scope].semantic_scope, path);
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
		ThrowSemanticError("type name used as retained value: " + spelling);
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
		ThrowSemanticError("type name used as retained value: " + spelling);
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
	ThrowSemanticError(
		"unknown nondependent name in template definition: " + spelling);
}

void RetainedTemplateValidator::VisitSizeof(NodeId node, std::size_t scope)
{
	const NodeId operand = analyzer_.FirstSemanticChild(node);
	if (operand == kNoNode || analyzer_.arena_->IsTag(operand, ::cppgm::syntax::STAG_TYPE_ID))
		return;
	if (analyzer_.arena_->IsTag(operand, ::cppgm::syntax::STAG_ID_EXPRESSION))
	{
		const std::string spelling = analyzer_.PayloadSource(operand);
		const NamePath path = analyzer_.SyntaxNamePath(operand);
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
		const LookupResult ordinary = analyzer_.LookupSyntaxName(
			operand, scopes_[scope].semantic_scope, LOOKUP_ORDINARY);
		if (ordinary.ordinary == kNoBinding)
		{
			const LookupResult type = analyzer_.LookupSyntaxName(
				operand, scopes_[scope].semantic_scope, LOOKUP_TYPE);
			if (type.type != kNoType) return;
		}
	}
	Visit(operand, scope);
}

void RetainedTemplateValidator::Visit(NodeId node, std::size_t scope,
	bool unknown_callee)
{
	if (node == kNoNode) return;
	if (VisitSwitchLabel(node, scope)) return;
	// The parser and specialization demand own type syntax within this boundary.
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_BUILTIN_TYPE_OPERAND)) return;
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_ID_EXPRESSION))
	{
		VisitIdExpression(node, scope, unknown_callee);
		return;
	}
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_CALL_EXPRESSION))
	{
		const std::uint32_t first = analyzer_.arena_->FirstEdge(node);
		if (first == kNoEdge) return;
		const NodeId callee = analyzer_.arena_->EdgeChild(first);
		const std::string spelling = analyzer_.PayloadSource(callee);
		Visit(callee, scope, true);
		std::size_t ordinal = 0;
		for (std::uint32_t edge = analyzer_.arena_->NextEdge(first);
			edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		{
			const NodeId holder = analyzer_.arena_->EdgeChild(edge);
			if (analyzer_.arena_->IsTag(holder, ::cppgm::syntax::STAG_ARGUMENT_LIST) ||
				analyzer_.arena_->IsTag(holder, ::cppgm::syntax::STAG_PAREN_ARGUMENT_LIST))
				for (std::uint32_t argument = analyzer_.arena_->FirstEdge(holder);
					argument != kNoEdge;
					argument = analyzer_.arena_->NextEdge(argument), ++ordinal)
					Visit(analyzer_.arena_->EdgeChild(argument), scope,
						(spelling == "__builtin_bit_cast" && ordinal == 0) ||
						(spelling == "__builtin_convertvector" && ordinal == 1));
			else Visit(holder, scope);
		}
		return;
	}
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_ENUM_SPECIFIER))
	{
		DeclareEnumValues(node, scope);
		VisitChildren(node, scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_SIZEOF_EXPRESSION))
	{
		VisitSizeof(node, scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_LAMBDA_EXPRESSION))
	{
		std::size_t lambda_parent = scope;
		std::vector<NameId> introduced;
		const NodeId declarator = analyzer_.FindChild(
			node, ::cppgm::syntax::STAG_LAMBDA_DECLARATOR);
		const NodeId clause = declarator == kNoNode ? kNoNode :
			analyzer_.FindChild(declarator, ::cppgm::syntax::STAG_TEMPLATE_PARAMETER_CLAUSE);
		if (clause != kNoNode)
		{
			const NodeId list = analyzer_.FindChild(
				clause, ::cppgm::syntax::STAG_TEMPLATE_PARAMETER_LIST);
			std::vector<TemplateParameter> parameters;
			std::vector<NameId> names;
			std::vector<NodeId> defaults;
			analyzer_.ParseTemplateParameters(list,
				scopes_[scope].semantic_scope, &parameters, &names, &defaults,
				&parameter_names_);
			lambda_parent = AddChildScope(scope, SCOPE_TEMPLATE_PARAMETERS,
				DefersUnknownMembers(scope));
			for (std::size_t i = 0; i < parameters.size(); ++i)
			{
				DeclareParameter(lambda_parent, parameters[i]);
				if (parameters[i].name != 0)
					introduced.push_back(parameters[i].name);
			}
		}
		const std::size_t lambda_scope = AddChildScope(
			lambda_parent, SCOPE_FUNCTION, DefersUnknownMembers(scope));
		if (declarator != kNoNode)
		{
			BindFunctionParameters(declarator, lambda_scope);
			VisitChildren(declarator, lambda_scope);
		}
		const NodeId body = analyzer_.FindChild(node, ::cppgm::syntax::STAG_COMPOUND_STATEMENT);
		if (body != kNoNode) Visit(body, lambda_scope);
		for (std::size_t i = 0; i < introduced.size(); ++i)
			parameter_names_.erase(introduced[i]);
		return;
	}
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_RANGE_FOR_STATEMENT))
	{
		const std::size_t control = AddChildScope(scope, SCOPE_BLOCK);
		const NodeId declaration = analyzer_.FindChild(
			node, ::cppgm::syntax::STAG_RANGE_DECLARATION);
		const NodeId initializer = analyzer_.FindChild(
			node, ::cppgm::syntax::STAG_RANGE_INITIALIZER);
		if (declaration == kNoNode || initializer == kNoNode)
			ThrowSemanticError("invalid retained range-for statement");
		Visit(initializer, control);
		Visit(declaration, control);
		const NodeId declarator = analyzer_.FindChild(
			declaration, ::cppgm::syntax::STAG_DECLARATOR);
		if (declarator == kNoNode)
			ThrowSemanticError("retained range declaration has no declarator");
		if (!DeclareStructuredBindings(declarator, control))
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
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_HANDLER))
	{
		const std::size_t handler_scope = AddChildScope(scope, SCOPE_BLOCK);
		const NodeId declaration = analyzer_.FindChild(
			node, ::cppgm::syntax::STAG_EXCEPTION_DECLARATION);
		if (declaration != kNoNode)
		{
			VisitChildren(declaration, handler_scope);
			const NodeId declarator = analyzer_.FindChild(
				declaration, ::cppgm::syntax::STAG_DECLARATOR);
			if (declarator != kNoNode)
				Declare(handler_scope, analyzer_.DeclaratorName(declarator),
					RETAINED_VALUE_NAME);
		}
		const NodeId body = analyzer_.FindChild(node, ::cppgm::syntax::STAG_COMPOUND_STATEMENT);
		if (body != kNoNode) Visit(body, handler_scope);
		return;
	}
	if (VisitControlStatement(node, scope)) return;
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_COMPOUND_STATEMENT))
	{
		const std::size_t block = AddChildScope(scope, SCOPE_BLOCK);
		VisitChildren(node, block);
		return;
	}
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_TEMPLATE_DECLARATION))
	{
		const NodeId clause = analyzer_.FindChild(
			node, ::cppgm::syntax::STAG_TEMPLATE_PARAMETER_CLAUSE);
		const NodeId list = clause == kNoNode ? kNoNode :
			analyzer_.FindChild(clause, ::cppgm::syntax::STAG_TEMPLATE_PARAMETER_LIST);
		std::vector<TemplateParameter> parameters;
		std::vector<NameId> names;
		std::vector<NodeId> defaults;
		analyzer_.ParseTemplateParameters(list,
			scopes_[scope].semantic_scope, &parameters, &names, &defaults,
			&parameter_names_);
		const std::size_t template_scope = AddChildScope(
			scope, SCOPE_TEMPLATE_PARAMETERS, DefersUnknownMembers(scope));
		SetTemplateParameterRange(template_scope, parameters);
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
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_MEMBER_EXPRESSION))
	{
		const std::uint32_t object_edge = analyzer_.arena_->FirstEdge(node);
		const std::uint32_t member_edge = object_edge == kNoEdge ? kNoEdge :
			analyzer_.arena_->NextEdge(object_edge);
		if (member_edge != kNoEdge)
		{
			const NodeId object = analyzer_.arena_->EdgeChild(object_edge);
			const NodeId member = analyzer_.arena_->EdgeChild(member_edge);
			const NodeId structure = analyzer_.FindChild(
				member, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
			NodeId terminal = kNoNode;
			if (structure != kNoNode)
				for (std::uint32_t edge = analyzer_.arena_->FirstEdge(structure);
					edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
				{
					const NodeId child = analyzer_.arena_->EdgeChild(edge);
					if (analyzer_.arena_->IsTag(child, ::cppgm::syntax::STAG_NAME_COMPONENT))
						terminal = child;
				}
			const bool template_id = terminal != kNoNode &&
				analyzer_.FindChild(terminal,
					::cppgm::syntax::STAG_TEMPLATE_TYPE_ARGUMENT_LIST) != kNoNode;
			const bool template_keyword = analyzer_.PayloadSource(member).
				compare(0, 8, "template") == 0;
			if (template_id && !template_keyword &&
				analyzer_.arena_->IsTag(object, ::cppgm::syntax::STAG_ID_EXPRESSION) &&
				analyzer_.FindChild(object, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME) == kNoNode)
			{
				const NameId object_name = analyzer_.program_->names.Intern(
					analyzer_.PayloadSource(object));
				if (IsDependentValue(scope, object_name))
					ThrowSemanticError(
						"dependent member template-id requires template");
			}
		}
		VisitChildren(node, scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_CLASS_SPECIFIER))
	{
		VisitClass(node, scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_FUNCTION_DEFINITION) ||
		analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_SPECIAL_MEMBER_DEFINITION))
	{
		VisitFunction(node, scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_SIMPLE_DECLARATION))
	{
		VisitSimple(node, scope, false);
		return;
	}
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_ALIAS_DECLARATION) ||
		analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_USING_DECLARATION) ||
		analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_USING_DIRECTIVE))
	{
		VisitUsing(node, scope);
		return;
	}
	if (analyzer_.arena_->IsTag(node, ::cppgm::syntax::STAG_CONDITION_DECLARATION))
	{
		const NodeId declarator = analyzer_.FindChild(node, ::cppgm::syntax::STAG_DECLARATOR);
		if (declarator != kNoNode)
			Declare(scope, analyzer_.DeclaratorName(declarator),
				RETAINED_VALUE_NAME);
	}
	VisitChildren(node, scope);
}

bool RetainedTemplateValidator::VisitSwitchLabel(
	NodeId node, std::size_t scope)
{
	if (!analyzer_.arena_->IsTag(node,
			::cppgm::syntax::STAG_CASE_STATEMENT) &&
		!analyzer_.arena_->IsTag(node,
			::cppgm::syntax::STAG_DEFAULT_STATEMENT))
		return false;
	if (!switch_entry_scopes_.empty())
	{
		const std::size_t boundary = switch_entry_scopes_.back();
		std::size_t current = scope;
		while (current != boundary)
		{
			if (current == std::numeric_limits<std::size_t>::max())
				ThrowInternalCompilerError(
					"retained switch label is outside its switch");
			if (scopes_[current].switch_entry_barriers != 0)
				ThrowSemanticError(
					"case or default label bypasses variable initialization");
			current = scopes_[current].parent;
		}
	}
	VisitChildren(node, scope);
	return true;
}

RetainedExceptionState
RetainedTemplateValidator::RetainedExceptionSpecificationState(
	NodeId declarator) const
{
	const NodeId qualifier = analyzer_.FindChild(declarator,
		::cppgm::syntax::STAG_FUNCTION_QUALIFIER);
	if (qualifier == kNoNode) return RETAINED_EXCEPTION_THROWING;
	const std::string spelling = analyzer_.PayloadSource(qualifier);
	if (spelling == "noexcept" || spelling == "throw()")
		return RETAINED_EXCEPTION_NONTHROWING;
	if (spelling.compare(0, 8, "noexcept") != 0)
		return RETAINED_EXCEPTION_DEFERRED;
	const NodeId expression = analyzer_.FirstSemanticChild(qualifier);
	if (expression == kNoNode) return RETAINED_EXCEPTION_DEFERRED;
	FundamentalType type = FT_INT;
	std::uint64_t value = 0;
	if (!analyzer_.arena_->ScalarLiteralFact(expression, &type, &value))
		return RETAINED_EXCEPTION_DEFERRED;
	return value == 0 ? RETAINED_EXCEPTION_THROWING :
		RETAINED_EXCEPTION_NONTHROWING;
}

RetainedSpecialMemberKind RetainedTemplateValidator::SpecialMemberKind(
	NodeId node) const
{
	const NodeId declarator = analyzer_.FindChild(node, ::cppgm::syntax::STAG_DECLARATOR);
	if (declarator != kNoNode &&
		analyzer_.FindChild(declarator, ::cppgm::syntax::STAG_CONVERSION_TYPE_ID) != kNoNode)
		return RETAINED_CONVERSION_FUNCTION;
	return analyzer_.arena_->Payload(node).find('~') != std::string::npos ?
		RETAINED_DESTRUCTOR : RETAINED_CONSTRUCTOR;
}

RetainedTemplateValidator::TemplateOrdinalMap
RetainedTemplateValidator::TemplateOrdinals(
	const std::vector<TemplateParameter>& parameters) const
{
	TemplateOrdinalMap result;
	for (std::size_t i = 0; i < parameters.size(); ++i)
		if (parameters[i].name != 0) result[parameters[i].name] = i;
	return result;
}

bool RetainedTemplateValidator::RetainedTypeSyntaxEquivalent(NodeId left,
	NodeId right, NodeId left_identifier, NodeId right_identifier,
	const TemplateOrdinalMap& left_parameters,
	const TemplateOrdinalMap& right_parameters) const
{
	if (analyzer_.arena_->TagId(left) != analyzer_.arena_->TagId(right))
		return false;
	const std::string& left_payload = analyzer_.arena_->SemanticPayload(left);
	const std::string& right_payload = analyzer_.arena_->SemanticPayload(right);
	if (!left_payload.empty() || !right_payload.empty())
	{
		const NameId left_name = left_payload.empty() ? 0 :
			analyzer_.program_->names.Intern(left_payload);
		const NameId right_name = right_payload.empty() ? 0 :
			analyzer_.program_->names.Intern(right_payload);
		const TemplateOrdinalMap::const_iterator left_parameter =
			left_parameters.find(left_name);
		const TemplateOrdinalMap::const_iterator right_parameter =
			right_parameters.find(right_name);
		if (left_parameter != left_parameters.end() ||
			right_parameter != right_parameters.end())
		{
			if (left_parameter == left_parameters.end() ||
				right_parameter == right_parameters.end() ||
				left_parameter->second != right_parameter->second)
				return false;
		}
		else if (left_payload != right_payload) return false;
	}
	std::uint32_t left_edge = analyzer_.arena_->FirstEdge(left);
	std::uint32_t right_edge = analyzer_.arena_->FirstEdge(right);
	while (true)
	{
		while (left_edge != kNoEdge)
		{
			const NodeId child = analyzer_.arena_->EdgeChild(left_edge);
			if (child != left_identifier &&
				!analyzer_.arena_->IsTag(child, ::cppgm::syntax::STAG_INITIALIZER)) break;
			left_edge = analyzer_.arena_->NextEdge(left_edge);
		}
		while (right_edge != kNoEdge)
		{
			const NodeId child = analyzer_.arena_->EdgeChild(right_edge);
			if (child != right_identifier &&
				!analyzer_.arena_->IsTag(child, ::cppgm::syntax::STAG_INITIALIZER)) break;
			right_edge = analyzer_.arena_->NextEdge(right_edge);
		}
		if (left_edge == kNoEdge || right_edge == kNoEdge)
			return left_edge == right_edge;
		if (!RetainedTypeSyntaxEquivalent(
			analyzer_.arena_->EdgeChild(left_edge),
			analyzer_.arena_->EdgeChild(right_edge), left_identifier,
			right_identifier, left_parameters, right_parameters))
			return false;
		left_edge = analyzer_.arena_->NextEdge(left_edge);
		right_edge = analyzer_.arena_->NextEdge(right_edge);
	}
}

bool RetainedTemplateValidator::ParameterTypesEquivalent(NodeId left,
	NodeId right, const std::vector<TemplateParameter>& left_parameters) const
{
	const NodeId left_clause = FindParameterClause(left);
	const NodeId right_clause = FindParameterClause(right);
	if (left_clause == kNoNode || right_clause == kNoNode)
		return left_clause == right_clause;
	std::vector<NodeId> left_syntax;
	std::vector<NodeId> right_syntax;
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(left_clause);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		left_syntax.push_back(analyzer_.arena_->EdgeChild(edge));
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(right_clause);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
		right_syntax.push_back(analyzer_.arena_->EdgeChild(edge));
	if (left_syntax.size() != right_syntax.size()) return false;
	const TemplateOrdinalMap left_ordinals =
		TemplateOrdinals(left_parameters);
	const TemplateOrdinalMap right_ordinals = TemplateOrdinals(parameters_);
	for (std::size_t i = 0; i < left_syntax.size(); ++i)
	{
		const NodeId left_declarator = analyzer_.FindChild(
			left_syntax[i], ::cppgm::syntax::STAG_DECLARATOR);
		const NodeId right_declarator = analyzer_.FindChild(
			right_syntax[i], ::cppgm::syntax::STAG_DECLARATOR);
		if (!RetainedTypeSyntaxEquivalent(left_syntax[i], right_syntax[i],
			left_declarator == kNoNode ? kNoNode :
				analyzer_.arena_->DeclaratorIdentifier(left_declarator),
			right_declarator == kNoNode ? kNoNode :
				analyzer_.arena_->DeclaratorIdentifier(right_declarator),
			left_ordinals, right_ordinals))
			return false;
	}
	return true;
}

bool RetainedTemplateValidator::FunctionQualifiersEquivalent(NodeId left,
	NodeId right) const
{
	std::vector<NodeId> left_qualifiers;
	std::vector<NodeId> right_qualifiers;
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(left);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId child = analyzer_.arena_->EdgeChild(edge);
		if (analyzer_.arena_->IsTag(child, ::cppgm::syntax::STAG_CV_QUALIFIER) ||
			analyzer_.arena_->IsTag(child, ::cppgm::syntax::STAG_REF_QUALIFIER))
			left_qualifiers.push_back(child);
	}
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(right);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId child = analyzer_.arena_->EdgeChild(edge);
		if (analyzer_.arena_->IsTag(child, ::cppgm::syntax::STAG_CV_QUALIFIER) ||
			analyzer_.arena_->IsTag(child, ::cppgm::syntax::STAG_REF_QUALIFIER))
			right_qualifiers.push_back(child);
	}
	if (left_qualifiers.size() != right_qualifiers.size()) return false;
	for (std::size_t i = 0; i < left_qualifiers.size(); ++i)
		if (analyzer_.arena_->TagId(left_qualifiers[i]) !=
				analyzer_.arena_->TagId(right_qualifiers[i]) ||
			analyzer_.arena_->SemanticPayload(left_qualifiers[i]) !=
				analyzer_.arena_->SemanticPayload(right_qualifiers[i]))
			return false;
	return true;
}

void RetainedTemplateValidator::ValidateSpecialMemberExceptionSpecification()
{
	if (!analyzer_.arena_->IsTag(target_, ::cppgm::syntax::STAG_SPECIAL_MEMBER_DEFINITION)) return;
	const NodeId declarator = analyzer_.FindChild(target_, ::cppgm::syntax::STAG_DECLARATOR);
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
	const NodeId class_declaration = class_declaration_ == kNoNode ?
		pattern.declaration : class_declaration_;
	const std::vector<TemplateParameter>* declaration_parameters =
		&pattern.parameters;
	for (std::size_t partial = 0;
		partial < pattern.partial_specializations.size(); ++partial)
		if (pattern.partial_specializations[partial].declaration ==
			class_declaration)
		{
			declaration_parameters =
				&pattern.partial_specializations[partial].parameters;
			break;
		}
	const RetainedSpecialMemberKind kind = SpecialMemberKind(target_);
	const NameId terminal = path.Last();
	for (std::uint32_t edge = analyzer_.arena_->FirstEdge(class_declaration);
		edge != kNoEdge; edge = analyzer_.arena_->NextEdge(edge))
	{
		const NodeId member = analyzer_.arena_->EdgeChild(edge);
		if (!analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_SPECIAL_MEMBER_DECLARATION) &&
			!analyzer_.arena_->IsTag(member, ::cppgm::syntax::STAG_SPECIAL_MEMBER_DEFINITION))
			continue;
		if (SpecialMemberKind(member) != kind) continue;
		const NodeId prior = analyzer_.FindChild(member, ::cppgm::syntax::STAG_DECLARATOR);
		if (prior == kNoNode || analyzer_.DeclaratorName(prior) != terminal ||
			!ParameterTypesEquivalent(
				prior, declarator, *declaration_parameters) ||
			!FunctionQualifiersEquivalent(prior, declarator))
			continue;
		const RetainedExceptionState prior_exception =
			RetainedExceptionSpecificationState(prior);
		const RetainedExceptionState target_exception =
			RetainedExceptionSpecificationState(declarator);
		// A dependent exception expression is finalized during concrete replay.
		// Declaration-time validation rejects only a proven throwing/nonthrowing
		// mismatch; it must not guess from the presence of `noexcept` alone.
		if (prior_exception != RETAINED_EXCEPTION_DEFERRED &&
			target_exception != RETAINED_EXCEPTION_DEFERRED &&
			prior_exception != target_exception)
			ThrowSemanticError(
				"conflicting retained special-member exception specification");
		return;
	}
}

void RetainedTemplateValidator::Run()
{
	EntityId class_context = analyzer_.current_class_context_;
	for (ScopeId owner = lexical_scope_; owner != kNoScope;
		owner = analyzer_.program_->ParentScope(owner))
		if (analyzer_.program_->KindOfScope(owner) == SCOPE_CLASS)
		{
			class_context = analyzer_.program_->EntityForScope(owner);
			break;
		}
	ScopedRetainedClassContext retained_class_context(
		&analyzer_.current_class_context_, class_context);
	const bool definition =
		(analyzer_.arena_->Flags(target_) & SYNTAX_FLAG_DEFINITION) != 0 ||
		analyzer_.arena_->IsTag(target_, ::cppgm::syntax::STAG_FUNCTION_DEFINITION) ||
		analyzer_.arena_->IsTag(target_, ::cppgm::syntax::STAG_SPECIAL_MEMBER_DEFINITION);
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
	SetTemplateParameterRange(root, parameters_);
	if (qualified_member)
	{
		const NodeId declarator = analyzer_.FindChild(target_, ::cppgm::syntax::STAG_DECLARATOR);
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
		{
			DeclareClassType(root, analyzer_.class_templates_[pattern].name, false);
			const ClassTemplatePattern& class_pattern =
				analyzer_.class_templates_[pattern];
			for (std::size_t i = 0; i < class_pattern.parameters.size(); ++i)
				if (class_pattern.parameters[i].name != 0 &&
					parameter_names_.count(class_pattern.parameters[i].name) == 0)
					scopes_[root].names[class_pattern.parameters[i].name] |=
						class_pattern.parameters[i].kind == TEMPLATE_ARGUMENT_INTEGRAL ?
							RETAINED_VALUE_NAME : RETAINED_TYPE_NAME;
			PredeclareClassMembers(class_declaration_ == kNoNode ?
				class_pattern.declaration : class_declaration_, root);
		}
	}
	while (analyzer_.function_template_shape_parameters_.size() <
		parameters_.size())
	{
		std::ostringstream generated;
		generated << "__retained_template_parameter_shape_"
			<< analyzer_.function_template_shape_parameters_.size();
		const std::string spelling = generated.str();
		if (analyzer_.stats_)
			analyzer_.RecordGeneratedIdentityRender(
				SEMANTIC_GENERATED_FUNCTION_TEMPLATE_PARAMETER_SHAPE,
				spelling, 1);
		const NameId name = analyzer_.program_->names.Intern(spelling);
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
		if (parameters_[i].pack)
		{
			TypeId shape = parameters_[i].dependent_type ?
				analyzer_.program_->types.Fundamental(FUND_INT) :
				parameters_[i].value_type;
			if (parameters_[i].kind == TEMPLATE_ARGUMENT_TYPE)
				shape = analyzer_.function_template_shape_parameters_[i];
			else if (parameters_[i].kind == TEMPLATE_ARGUMENT_TEMPLATE)
				shape = analyzer_.CreateTemplateTemplateParameterProxy(
					semantic, parameters_[i], i);
			std::vector<TemplateArgument> symbolic(1, TemplateArgument(
				parameters_[i].kind, shape, static_cast<std::int64_t>(i),
				static_cast<std::uint32_t>(i)));
			analyzer_.BindTemplateArgumentPack(
				semantic, parameters_[i], symbolic, 0, symbolic.size());
			continue;
		}
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
	if (!definition && !analyzer_.arena_->IsTag(target_, ::cppgm::syntax::STAG_ALIAS_DECLARATION))
		return;
	Visit(target_, root);
}

void Analyzer::ValidateRetainedTemplateDefinition(NodeId target,
	ScopeId scope, const std::vector<TemplateParameter>& parameters,
	NodeId class_declaration)
{
	RetainedTemplateValidator(
		*this, target, scope, parameters, class_declaration).Run();
}

void Analyzer::PublishRetainedCallLookup(NodeId callee,
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

void Analyzer::CopyRetainedCallLookup(
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

void Analyzer::RecordRetainedCallLookup(NodeId callee, ScopeId scope,
	const std::string& spelling, bool adl_eligible)
{
	EntityId naming_class = kNoEntity;
	std::vector<BindingId> functions;
	const NodeId structure = FindChild(callee, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
	NodeId terminal_component = kNoNode;
	if (structure != kNoNode)
		for (std::uint32_t edge = arena_->FirstEdge(structure);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, ::cppgm::syntax::STAG_NAME_COMPONENT))
				terminal_component = child;
		}
	const bool explicit_template_id = terminal_component != kNoNode &&
		FindChild(terminal_component, ::cppgm::syntax::STAG_TEMPLATE_TYPE_ARGUMENT_LIST) != kNoNode;
	const std::vector<std::size_t> templates = structure != kNoNode ?
		FindFunctionTemplates(scope, StructuredNamePath(callee)) :
		FindFunctionTemplates(scope, SyntaxNamePath(callee));
	if (!explicit_template_id)
		functions = FunctionCallCandidates(scope, spelling, &naming_class, callee,
			!templates.empty());
	PublishRetainedCallLookup(callee, functions, templates, naming_class,
		adl_eligible);
}

std::vector<BindingId> Analyzer::RetainedFunctionCallCandidates(
	NodeId callee, ScopeId scope, const std::string& spelling,
	EntityId* naming_class, bool* retained_lookup)
{
	*retained_lookup = callee < retained_call_lookup_states_.size() &&
		(retained_call_lookup_states_[callee] &
			RETAINED_CALL_LOOKUP_PUBLISHED) != 0;
	if (!*retained_lookup)
	{
		NamePath explicit_base;
		std::vector<NodeId> explicit_arguments;
		if (CollectExplicitTemplateArguments(
			callee, &explicit_base, &explicit_arguments))
		{
			// A call's explicit template-id is only a prefix.  Instantiating it
			// during lookup would make every still-deducible pack empty and can
			// publish that wrong partition before the call arguments are known.
			// CompleteFunctionCallTemplateCandidates owns the argument-aware
			// specialization immediately after call-argument analysis.
			*naming_class = kNoEntity;
			return std::vector<BindingId>();
		}
		return FunctionCallCandidates(
			scope, spelling, naming_class, callee, true);
	}
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
	// Class-template specializations replay one retained syntax graph. A member
	// function template in each specialization therefore shares callee NodeIds,
	// while its nondependent alias lookup belongs to the concrete class owner.
	// Reject a fact whose member owner is unreachable from its recorded naming
	// class and rebuild that one call from the active specialization scope.
	for (std::size_t i = 0; *naming_class != kNoEntity && i < result.size(); ++i)
	{
		const EntityId owner = program_->bindings[result[i]].member_owner;
		if (owner == kNoEntity || program_->QueryBasePath(
			*naming_class, owner, 0, 0, 0, 0)) continue;
		*retained_lookup = false;
		return FunctionCallCandidates(
			scope, spelling, naming_class, callee, true);
	}
	// Several class-template specializations can publish the same replayed
	// callee, each with its own naming class, and a template-only lookup
	// records the last publisher.  A retained template candidate whose owner
	// is unreachable from the recorded naming class belongs to another
	// specialization, so rebuild that call from the active scope as well.
	const CompactIndexSequence* retained_templates =
		*naming_class == kNoEntity ? 0 :
		retained_call_template_sets_.Find(callee);
	for (std::size_t i = 0; retained_templates &&
		i < retained_templates->Size(); ++i)
	{
		const FunctionTemplatePattern& pattern =
			function_templates_[(*retained_templates)[i]];
		const EntityId owner = program_->EntityForScope(pattern.owner);
		if (owner == kNoEntity || program_->QueryBasePath(
			*naming_class, owner, 0, 0, 0, 0)) continue;
		*retained_lookup = false;
		return FunctionCallCandidates(
			scope, spelling, naming_class, callee, true);
	}
	return result;
}

void Analyzer::CompleteFunctionCallTemplateCandidates(NodeId callee,
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
			patterns = FindFunctionTemplates(scope, SyntaxNamePath(callee));
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
	NamePath retained_name = SyntaxNamePath(callee);
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
			const LookupResult active_functions = program_->LookupMember(
				current_class_context_, retained_name.Last(), LOOKUP_ORDINARY);
			const bool static_member_replay =
				current_function_context_ != kNoBinding &&
				program_->bindings[current_function_context_].member_owner ==
					current_class_context_ &&
				program_->bindings[current_function_context_].static_member_function &&
				(active_patterns || active_functions.OrdinaryCount() != 0);
			if (static_member_replay)
			{
				patterns.clear();
				candidates->clear();
			}
			if (active_patterns)
				for (std::size_t i = 0; i < active_patterns->Size(); ++i)
					if (std::find(patterns.begin(), patterns.end(),
						(*active_patterns)[i]) == patterns.end())
						patterns.push_back((*active_patterns)[i]);
			std::vector<BindingId> ordinary;
			for (std::size_t i = 0; i < active_functions.OrdinaryCount(); ++i)
				AppendFunctionSet(active_functions.OrdinaryAt(i), &ordinary,
					!patterns.empty());
			for (std::size_t i = 0; i < ordinary.size(); ++i)
			{
				const BindingId canonical =
					program_->bindings[ordinary[i]].canonical;
				bool present = false;
				for (std::size_t prior = 0; prior < candidates->size(); ++prior)
					if (program_->bindings[(*candidates)[prior]].canonical == canonical)
						present = true;
				if (!present) candidates->push_back(ordinary[i]);
			}
			*naming_class = active_functions.naming_class != kNoEntity ?
				active_functions.naming_class : current_class_context_;
		}
	}
	if (current_function_context_ != kNoBinding)
	{
		const FunctionInfo& current = GetFunction(current_function_context_);
		const BindingRecord& current_binding =
			program_->bindings[current_function_context_];
		NamePath active_name = SyntaxNamePath(callee);
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

bool Analyzer::RetainedCallAllowsArgumentDependentLookup(
	NodeId callee) const
{
	return callee < retained_call_lookup_states_.size() &&
		(retained_call_lookup_states_[callee] &
			RETAINED_CALL_ADL_ELIGIBLE) != 0;
}

bool Analyzer::ClassTemplateSpecializationArgumentsComplete(
	EntityId entity) const
{
	if (entity >= class_template_pattern_by_entity_.size() ||
		class_template_pattern_by_entity_[entity] == kNoDumpEdge ||
		program_->entities[entity].template_argument_begin == kNoBinding)
		return true;
	const std::size_t index = class_template_pattern_by_entity_[entity];
	if (index >= class_templates_.size())
		ThrowInternalCompilerError("invalid class specialization owner index");
	const EntityRecord& specialization = program_->entities[entity];
	const std::size_t first = specialization.template_argument_begin;
	const ClassTemplatePattern& pattern = class_templates_[index];
	const std::size_t count = specialization.template_argument_count;
	if ((!HasTrailingTemplateParameterPack(pattern.parameters) &&
		 count != pattern.parameters.size()) ||
		(HasTrailingTemplateParameterPack(pattern.parameters) &&
		 count < FixedTemplateParameterCount(pattern.parameters)) ||
		first > program_->template_arguments.size() ||
		count > program_->template_arguments.size() - first)
		ThrowInternalCompilerError("class specialization arguments are truncated");
	for (std::size_t i = 0; i < count; ++i)
	{
		if (first + i < program_->canonical_template_arguments.size() &&
			program_->canonical_template_arguments[first + i].kind !=
				TEMPLATE_ARGUMENT_TYPE)
			continue;
		const TypeId argument = program_->types.RemoveTopCv(
			program_->template_arguments[first + i]);
		const TypeRecord& record = program_->types.Get(argument);
		if (record.kind == TYPE_NAMED &&
			!program_->entities[record.entity].complete)
			return false;
	}
	return true;
}

}
}
