#include "pa12_semantic_detail.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{
namespace
{

std::size_t NoTemplatePattern()
{
	return std::numeric_limits<std::size_t>::max();
}

bool ClassTemplateArgumentsAreComplete(const Program& program,
	const std::vector<TemplateArgument>& arguments)
{
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		if (arguments[i].kind != TEMPLATE_ARGUMENT_TYPE) continue;
		const TypeId argument = program.types.RemoveTopCv(arguments[i].type);
		const TypeRecord& record = program.types.Get(argument);
		if (record.kind == TYPE_NAMED &&
			!program.entities[record.entity].complete)
			return false;
	}
	return true;
}

std::string TemplateArgumentName(const std::string& source)
{
	std::string spelling = source;
	const char* prefixes[] = {"struct ", "class ", "union "};
	for (std::size_t prefix = 0; prefix < 3; ++prefix)
	{
		const std::size_t length =
			std::char_traits<char>::length(prefixes[prefix]);
		if (spelling.compare(0, length, prefixes[prefix]) == 0)
		{
			spelling.erase(0, length);
			break;
		}
	}
	std::string result;
	result.reserve(spelling.size());
	bool underscore = false;
	for (std::size_t i = 0; i < spelling.size(); ++i)
	{
		const unsigned char character =
			static_cast<unsigned char>(spelling[i]);
		if (std::isalnum(character))
		{
			result += static_cast<char>(character);
			underscore = false;
		}
		else if (!underscore)
		{
			result += '_';
			underscore = true;
		}
	}
	while (!result.empty() && result[result.size() - 1] == '_')
		result.erase(result.size() - 1);
	return result.empty() ? "type" : result;
}

std::string CanonicalTemplateArgumentName(const Program& program,
	const TemplateArgument& argument)
{
	if (argument.kind == TEMPLATE_ARGUMENT_TYPE)
		return TemplateArgumentName(program.RenderType(argument.type));
	std::ostringstream result;
	result << "value_" << argument.type << '_';
	if (argument.value < 0) result << 'm' << -(argument.value + 1) + 1;
	else result << 'p' << argument.value;
	return result.str();
}

std::string ClassTemplateSpecializationScopeName(std::size_t pattern,
	const std::vector<TemplateArgument>& arguments)
{
	std::ostringstream result;
	result << "__cppgm_class_template_" << pattern;
	for (std::size_t i = 0; i < arguments.size(); ++i)
		result << '_' << arguments[i].kind << '_' << arguments[i].type << '_'
			<< arguments[i].value;
	return result.str();
}

}

bool SemanticAnalyzer::IsDeclaration(NodeId node) const
{
	return arena_->IsTag(node, "simple-declaration") ||
		arena_->IsTag(node, "function-definition") ||
		arena_->IsTag(node, "alias-declaration") ||
		arena_->IsTag(node, "using-declaration") ||
		arena_->IsTag(node, "using-directive") ||
		arena_->IsTag(node, "namespace-definition") ||
		arena_->IsTag(node, "namespace-alias-definition") ||
		arena_->IsTag(node, "template-declaration") ||
		arena_->IsTag(node, "explicit-instantiation-declaration") ||
		arena_->IsTag(node, "explicit-instantiation-definition") ||
		arena_->IsTag(node, "special-member-declaration") ||
		arena_->IsTag(node, "special-member-definition") ||
		arena_->IsTag(node, "class-specifier") ||
		arena_->IsTag(node, "class-forward-declaration") ||
		arena_->IsTag(node, "enum-specifier") ||
		arena_->IsTag(node, "static-assert-declaration") ||
		arena_->IsTag(node, "empty-declaration") ||
		arena_->IsTag(node, "layout-pack-push") ||
		arena_->IsTag(node, "layout-pack-pop") ||
		arena_->IsTag(node, "linkage-specification");
}

void SemanticAnalyzer::RegisterClassMemberFunction(EntityId entity,
	BindingId function)
{
	if (entity == kNoEntity || function == kNoBinding) return;
	if (entity_member_functions_.size() <= entity)
		entity_member_functions_.resize(static_cast<std::size_t>(entity) + 1);
	function = program_->bindings[function].canonical;
	std::vector<BindingId>& functions = entity_member_functions_[entity];
	if (std::find(functions.begin(), functions.end(), function) ==
		functions.end())
		functions.push_back(function);
}

LookupResult SemanticAnalyzer::LookupPath(ScopeId scope,
	const NamePath& path, LookupKind kind)
{
	if (path.Size() <= 1) return program_->Lookup(scope, path, kind);
	ScopeId carrier = path.global ? program_->GlobalScope() : kNoScope;
	std::size_t component = 0;
	if (!path.global)
	{
		const LookupResult first = program_->LookupName(
			scope, path[0], LOOKUP_SCOPE_CARRIER);
		if (first.type != kNoType) EnsureClassDefinition(first.type);
		carrier = first.name_space != kNoScope ? first.name_space :
			first.type != kNoType ? program_->ScopeForType(first.type) :
			kNoScope;
		component = 1;
	}
	for (; carrier != kNoScope && component + 1 < path.Size(); ++component)
	{
		NamePath one;
		one.Push(path[component]);
		const LookupResult next = program_->LookupQualified(
			carrier, one, LOOKUP_SCOPE_CARRIER);
		if (next.type != kNoType) EnsureClassDefinition(next.type);
		carrier = next.name_space != kNoScope ? next.name_space :
			next.type != kNoType ? program_->ScopeForType(next.type) :
			kNoScope;
	}
	if (carrier == kNoScope) return LookupResult();
	NamePath terminal;
	terminal.Push(path.Last());
	return program_->LookupQualified(carrier, terminal, kind);
}

LookupResult SemanticAnalyzer::LookupStructuredName(NodeId syntax,
	ScopeId scope, LookupKind kind, ScopeId* terminal_owner)
{
	if (terminal_owner) *terminal_owner = kNoScope;
	const NodeId structure = syntax != kNoNode &&
		arena_->IsTag(syntax, "structured-type-name") ? syntax :
		syntax == kNoNode ? kNoNode :
		FindChild(syntax, "structured-type-name");
	if (structure == kNoNode) return LookupResult();
	std::uint32_t component_edge = arena_->FirstEdge(structure);
	while (component_edge != kNoEdge && !arena_->IsTag(
		arena_->EdgeChild(component_edge), "name-component"))
		component_edge = arena_->NextEdge(component_edge);
	if (component_edge == kNoEdge) return LookupResult();

	ScopeId carrier = FindChild(structure, "global-qualifier") != kNoNode ?
		program_->GlobalScope() : kNoScope;
	while (component_edge != kNoEdge)
	{
		const NodeId component_node = arena_->EdgeChild(component_edge);
		std::uint32_t next_component_edge = arena_->NextEdge(component_edge);
		while (next_component_edge != kNoEdge && !arena_->IsTag(
			arena_->EdgeChild(next_component_edge), "name-component"))
			next_component_edge = arena_->NextEdge(next_component_edge);
		const bool terminal = next_component_edge == kNoEdge;
		if (terminal && terminal_owner && carrier != kNoScope)
			*terminal_owner = carrier;
		const NameId component = program_->names.UseInterned(
			arena_->SemanticPayloadId(component_node));
		const NodeId argument_list = FindChild(
			component_node, "template-type-argument-list");
		const LookupKind component_kind = argument_list != kNoNode ?
			LOOKUP_TYPE : terminal ? kind : LOOKUP_SCOPE_CARRIER;
		LookupResult found;
		if (carrier == kNoScope)
			found = program_->LookupName(scope, component, component_kind);
		else
		{
			NamePath direct;
			direct.Push(component);
			found = program_->LookupQualified(
				carrier, direct, component_kind);
		}

		if (argument_list != kNoNode)
		{
			const std::size_t pattern =
				FindClassTemplateIndex(found, component);
			if (pattern == NoTemplatePattern()) return LookupResult();
			std::vector<NodeId> argument_syntax;
			for (std::uint32_t edge = arena_->FirstEdge(argument_list);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
				argument_syntax.push_back(arena_->EdgeChild(edge));
			std::vector<TemplateArgument> arguments;
			const ClassTemplatePattern& class_pattern =
				class_templates_[pattern];
			if (!BuildTemplateArguments(class_pattern.parameters,
				argument_syntax, scope, class_pattern.lexical_scope, &arguments))
				return LookupResult();
			const BindingId specialization =
				InstantiateClassTemplate(pattern, arguments);
			if (specialization == kNoBinding) return LookupResult();
			found = LookupResult();
			found.type = program_->bindings[specialization].type;
			found.type_declaration = specialization;
			found.type_declaration_canonical =
				program_->bindings[specialization].canonical;
		}
		if (terminal) return found;
		if (found.type != kNoType)
		{
			EnsureClassDefinition(found.type);
			carrier = program_->ScopeForType(found.type);
		}
		else carrier = found.name_space;
		if (carrier == kNoScope) return LookupResult();
		component_edge = next_component_edge;
	}
	return LookupResult();
}

NamePath SemanticAnalyzer::StructuredNamePath(NodeId syntax)
{
	NamePath path;
	const NodeId structure = syntax != kNoNode &&
		arena_->IsTag(syntax, "structured-type-name") ? syntax :
		syntax == kNoNode ? kNoNode :
		FindChild(syntax, "structured-type-name");
	if (structure == kNoNode) return path;
	path.global = FindChild(structure, "global-qualifier") != kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(structure); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, "name-component"))
			path.Push(program_->names.UseInterned(
				arena_->SemanticPayloadId(child)));
	}
	return path;
}

LookupResult SemanticAnalyzer::LookupSpelling(ScopeId scope,
	const std::string& spelling, LookupKind kind)
{
	if (spelling.find("::") == std::string::npos)
		return program_->LookupName(scope, program_->names.Intern(spelling), kind);
	return LookupPath(scope, ParseNamePath(spelling), kind);
}

ScopeId SemanticAnalyzer::ResolveScopeSpelling(ScopeId scope,
	const std::string& spelling)
{
	const LookupResult result =
		LookupSpelling(scope, spelling, LOOKUP_SCOPE_CARRIER);
	if (result.type != kNoType) EnsureClassDefinition(result.type);
	return result.name_space != kNoScope ? result.name_space :
		result.type != kNoType ? program_->ScopeForType(result.type) : kNoScope;
}

ScopeId SemanticAnalyzer::ResolveOwner(ScopeId scope, const NamePath& name)
{
	if (!name.global && name.Size() <= 1) return scope;
	NamePath owner = name;
	if (!owner.Empty()) owner.Pop();
	if (owner.Empty()) return owner.global ? program_->GlobalScope() : scope;
	const ScopeId lookup_scope = owner.global ?
		program_->GlobalScope() : scope;
	const LookupResult result =
		LookupPath(lookup_scope, owner, LOOKUP_SCOPE_CARRIER);
	return result.name_space != kNoScope ? result.name_space :
		result.type != kNoType ? program_->ScopeForType(result.type) : kNoScope;
}

std::vector<BindingId> SemanticAnalyzer::UsingFunctionCandidates(
	ScopeId scope, const NamePath& path, const std::string& spelling,
	ScopeId* target_owner, bool* names_owner_alias, NodeId syntax)
{
	std::vector<BindingId> functions =
		FunctionCandidates(scope, spelling, 0, syntax);
	const NodeId structure = syntax == kNoNode ? kNoNode :
		FindChild(syntax, "structured-type-name");
	if (structure != kNoNode)
	{
		ScopeId structured_owner = kNoScope;
		(void)LookupStructuredName(
			syntax, scope, LOOKUP_ORDINARY, &structured_owner);
		*target_owner = structured_owner;
		std::vector<NameId> names;
		for (std::uint32_t edge = arena_->FirstEdge(structure);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "name-component"))
				names.push_back(program_->names.UseInterned(
					arena_->SemanticPayloadId(child)));
		}
		*names_owner_alias = names.size() > 1 &&
			names[names.size() - 1] == names[names.size() - 2];
	}
	else
	{
		*target_owner = path.Size() > 1 ? ResolveOwner(scope, path) : kNoScope;
		*names_owner_alias = path.Size() > 1 &&
			path.Last() == path[path.Size() - 2];
	}
	const EntityId target_entity = *target_owner == kNoScope ? kNoEntity :
		program_->EntityForScope(*target_owner);
	if (!functions.empty() || !*names_owner_alias || target_entity == kNoEntity)
		return functions;
	const NameId identity = program_->entities[target_entity].identity_name;
	return FunctionCandidates(*target_owner, program_->names.Get(identity));
}

bool SemanticAnalyzer::ParseExplicitTemplateArguments(NodeId syntax,
	ScopeId scope, NamePath* base, std::vector<TypeId>* arguments)
{
	std::vector<NodeId> syntax_arguments;
	if (!CollectExplicitTemplateArguments(syntax, base, &syntax_arguments))
		return false;
	arguments->clear();
	for (std::size_t i = 0; i < syntax_arguments.size(); ++i)
	{
		const NodeId argument = syntax_arguments[i];
		if (!arena_->IsTag(argument, "type-id"))
		{
			arguments->clear();
			return false;
		}
		const TypeId type = BuildTypeId(argument, scope);
		if (type == kNoType)
			throw std::runtime_error("unknown explicit template type argument");
		arguments->push_back(type);
	}
	return true;
}

bool SemanticAnalyzer::AnalyzeClassTemplateMember(NodeId declaration,
	ScopeId scope, const std::vector<TemplateParameter>& parameters)
{
	NodeId declarator = FindChild(declaration, "declarator");
	if (arena_->IsTag(declaration, "simple-declaration"))
	{
		const NodeId list = FindChild(declaration, "init-declarator-list");
		const NodeId item = list == kNoNode ? kNoNode :
			FirstSemanticChild(list);
		declarator = item == kNoNode ? kNoNode :
			FindChild(item, "declarator");
	}
	NodeId structure = kNoNode;
	if (arena_->IsTag(declaration, "class-specifier") ||
		arena_->IsTag(declaration, "class-forward-declaration"))
		structure = FindChild(declaration, "structured-type-name");
	else if (declarator != kNoNode)
		structure = DeclaratorNameStructure(declarator);
	else return false;
	if (structure == kNoNode) return false;
	std::vector<NodeId> components;
	NamePath path;
	path.global = FindChild(structure, "global-qualifier") != kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(structure); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (!arena_->IsTag(child, "name-component")) continue;
		components.push_back(child);
		path.Push(program_->names.UseInterned(
			arena_->SemanticPayloadId(child)));
	}
	if (path.Size() <= 1) return false;

	std::size_t owner_component = path.Size();
	NodeId owner_argument_list = kNoNode;
	for (std::size_t i = 0; i + 1 < path.Size(); ++i)
	{
		const NodeId arguments = FindChild(
			components[i], "template-type-argument-list");
		if (arguments != kNoNode)
		{
			owner_component = i;
			owner_argument_list = arguments;
			break;
		}
	}
	if (owner_component == path.Size()) return false;

	NamePath primary;
	primary.global = path.global;
	for (std::size_t i = 0; i <= owner_component; ++i)
		primary.Push(path[i]);
	const std::size_t pattern_index = FindClassTemplate(scope, primary);
	if (pattern_index == NoTemplatePattern())
		throw std::runtime_error("class template member owner was not found");
	const ClassTemplatePattern& owner_pattern =
		class_templates_[pattern_index];
	std::vector<NodeId> owner_arguments;
	for (std::uint32_t edge = arena_->FirstEdge(owner_argument_list);
		edge != kNoEdge; edge = arena_->NextEdge(edge))
		owner_arguments.push_back(arena_->EdgeChild(edge));
	if (!owner_pattern.defined || owner_arguments.size() !=
		owner_pattern.parameters.size())
		throw std::runtime_error("invalid class template member owner shape");

	ClassTemplateMemberPattern member;
	member.lexical_scope = scope;
	member.declaration = declaration;
	member.parameters = parameters;
	member.owner_parameter_indices.assign(owner_arguments.size(), kNoDumpEdge);
	member.owner_fixed_arguments.assign(owner_arguments.size(),
		TemplateArgument());
	for (std::size_t component = owner_component + 1;
		component + 1 < path.Size(); ++component)
		member.nested_owner_path.push_back(path[component]);
	std::vector<std::uint8_t> used(parameters.size(), 0);
	for (std::size_t argument = 0; argument < owner_arguments.size(); ++argument)
	{
		std::size_t parameter = parameters.size();
		NameId argument_id = 0;
		if (arena_->IsTag(owner_arguments[argument], "type-id"))
		{
			const NodeId argument_specifiers = FindChild(
				owner_arguments[argument], "type-specifier-seq");
			const NodeId argument_name = argument_specifiers == kNoNode ?
				kNoNode : FirstSemanticChild(argument_specifiers);
			if (argument_name != kNoNode &&
				arena_->IsTag(argument_name, "type-name"))
				argument_id = program_->names.Intern(PayloadSource(argument_name));
		}
		else if (arena_->IsTag(owner_arguments[argument], "id-expression") &&
			FindChild(owner_arguments[argument], "structured-type-name") == kNoNode)
			argument_id = program_->names.Intern(
				PayloadSource(owner_arguments[argument]));
		for (std::size_t candidate = 0;
			candidate < parameters.size(); ++candidate)
			if (parameters[candidate].name != 0 &&
				argument_id == parameters[candidate].name &&
				parameters[candidate].kind ==
					owner_pattern.parameters[argument].kind)
			{
				parameter = candidate;
				break;
			}
		if (parameter != parameters.size())
		{
			if (used[parameter])
				throw std::runtime_error(
					"repeated class template member owner parameter");
			used[parameter] = 1;
			member.owner_parameter_indices[argument] =
				static_cast<std::uint32_t>(parameter);
			continue;
		}
		if (owner_pattern.parameters[argument].kind != TEMPLATE_ARGUMENT_TYPE ||
			!arena_->IsTag(owner_arguments[argument], "type-id"))
			throw std::runtime_error(
				"fixed non-type class template member owners are unsupported");
		member.owner_fixed_arguments[argument] = TemplateArgument(
			TEMPLATE_ARGUMENT_TYPE,
			BuildTypeId(owner_arguments[argument], scope));
	}
	for (std::size_t i = 0; i < used.size(); ++i)
		if (!used[i])
			throw std::runtime_error(
				"class template member parameter is not bound by its owner");

	class_templates_[pattern_index].member_definitions.push_back(member);
	const std::vector<BindingId> specializations =
		class_templates_[pattern_index].specialization_bindings;
	const std::size_t parameter_count = owner_pattern.parameters.size();
	for (std::size_t i = 0; i < specializations.size(); ++i)
	{
		const EntityId entity = EntityOf(
			program_->bindings[specializations[i]].type);
		if (entity == kNoEntity)
			throw std::logic_error("class specialization has no entity");
		const EntityRecord& record = program_->entities[entity];
		const std::size_t first = record.template_argument_begin;
		if (record.template_argument_count != parameter_count ||
			first > program_->template_arguments.size() || parameter_count >
				program_->template_arguments.size() - first)
			throw std::logic_error("class specialization arguments are invalid");
		const std::vector<TemplateArgument> arguments =
			StoredTemplateArguments(first, parameter_count);
		ApplyClassTemplateMemberDefinitions(
			pattern_index, specializations[i], arguments);
	}
	return true;
}

bool SemanticAnalyzer::RetainVariableTemplate(NodeId declaration,
	ScopeId scope, const std::vector<NameId>& parameters,
	const std::vector<NodeId>& defaults)
{
	if (!arena_->IsTag(declaration, "simple-declaration")) return false;
	const NodeId list = FindChild(declaration, "init-declarator-list");
	if (list == kNoNode) return false;
	NodeId item = kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		if (item != kNoNode)
			throw std::runtime_error(
				"variable template must declare one variable");
		item = arena_->EdgeChild(edge);
	}
	const NodeId declarator = item == kNoNode ? kNoNode :
		FindChild(item, "declarator");
	if (declarator == kNoNode) return false;
	NodeId named_declarator = declarator;
	while (FindChild(named_declarator, "identifier") == kNoNode)
	{
		const NodeId nested = FindChild(named_declarator, "nested-declarator");
		named_declarator = nested == kNoNode ? kNoNode :
			FindChild(nested, "declarator");
		if (named_declarator == kNoNode) break;
	}
	if (named_declarator != kNoNode &&
		FindChild(named_declarator, "parameter-clause") != kNoNode) return false;
	const NamePath path = DeclaratorNamePath(declarator);
	if (path.Empty()) throw std::runtime_error("unnamed variable template");
	const ScopeId owner = ResolveOwner(scope, path);
	if (owner == kNoScope)
		throw std::runtime_error("variable template owner not found");
	std::string terminal = program_->names.Get(path.Last());
	const NodeId structure = DeclaratorNameStructure(declarator);
	NodeId terminal_component = kNoNode;
	if (structure != kNoNode)
		for (std::uint32_t edge = arena_->FirstEdge(structure);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "name-component"))
				terminal_component = child;
		}
	const bool partial = terminal_component != kNoNode && FindChild(
		terminal_component, "template-type-argument-list") != kNoNode;
	if (terminal.empty())
		throw std::runtime_error("invalid variable template name");
	const NameId name = program_->names.Intern(terminal);
	const std::uint64_t key =
		(static_cast<std::uint64_t>(owner) << 32) | name;
	const CompactIndexSequence* related = variable_template_sets_.Find(key);
	bool has_primary = false;
	if (related)
		for (std::size_t i = 0; i < related->Size(); ++i)
			if (!variable_templates_[(*related)[i]].partial_specialization)
				has_primary = true;
	if (partial && !has_primary)
		throw std::runtime_error(
			"variable template partial specialization has no primary");
	VariableTemplatePattern pattern;
	pattern.owner = owner;
	pattern.lexical_scope = scope;
	pattern.name = name;
	pattern.declaration = declaration;
	pattern.specifiers = FindChild(declaration, "decl-specifier-seq");
	pattern.declarator = declarator;
	pattern.initializer = FindChild(item, "initializer");
	pattern.type_parameters = parameters;
	pattern.default_arguments = defaults;
	pattern.partial_specialization = partial;
	const std::size_t index = variable_templates_.size();
	if (index > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many variable templates");
	variable_templates_.push_back(pattern);
	variable_template_sets_.Ensure(key).Push(index);
	return true;
}

void SemanticAnalyzer::AnalyzeClassTemplate(NodeId declaration, ScopeId scope,
	const std::vector<TemplateParameter>& parameters)
{
	const NamePath path = ParseNamePath(arena_->Payload(declaration));
	const NameId name = path.Last();
	const ScopeId owner = ResolveOwner(scope, path);
	if (name == 0 || owner == kNoScope)
		throw std::runtime_error("invalid class template owner or name");
	const LookupResult prior_name =
		program_->LookupDirect(owner, name, LOOKUP_TYPE);
	std::size_t prior_index = NoTemplatePattern();
	if (prior_name.type != kNoType)
	{
		const TypeRecord& prior_type = program_->types.Get(
			program_->types.RemoveTopCv(prior_name.type));
		if (prior_type.kind != TYPE_NAMED ||
			prior_type.entity >= class_template_pattern_by_entity_.size() ||
			class_template_pattern_by_entity_[prior_type.entity] == kNoDumpEdge)
			throw std::runtime_error(
				"class template conflicts with an existing type");
		prior_index = class_template_pattern_by_entity_[prior_type.entity];
	}
	const bool definition =
		(arena_->Flags(declaration) & SYNTAX_FLAG_DEFINITION) != 0;
	if (prior_index != NoTemplatePattern())
	{
		ClassTemplatePattern& prior = class_templates_[prior_index];
		if (prior.parameters.size() != parameters.size())
			throw std::runtime_error(
				"class template parameter count mismatch");
		std::vector<TemplateParameter> merged = parameters;
		for (std::size_t i = 0; i < parameters.size(); ++i)
		{
			if (prior.parameters[i].kind != parameters[i].kind)
				throw std::runtime_error(
					"class template parameter kind mismatch");
			if (merged[i].default_argument == kNoNode)
				merged[i].default_argument =
					prior.parameters[i].default_argument;
		}
		prior.parameters.swap(merged);
		if (!definition) return;
		if (prior.defined)
			throw std::runtime_error("duplicate class template definition");
		prior.lexical_scope = scope;
		prior.declaration = declaration;
		prior.defined = true;
		UpgradeClassTemplateSpecializations(prior_index);
		return;
	}

	ClassTemplatePattern pattern;
	pattern.owner = owner;
	pattern.lexical_scope = scope;
	pattern.name = name;
	pattern.declaration = declaration;
	pattern.parameters = parameters;
	pattern.defined = definition;
	const NameId marker_name = EmissionName(owner, name);
	pattern.marker_entity = program_->NewEntity(marker_name,
		NAMED_TEMPLATE_PARAMETER, false, kNoType, owner, name);
	const TypeId marker_type =
		program_->entities[pattern.marker_entity].type;
	program_->SetTypeName(owner, name, marker_type);
	program_->AddBinding(owner, BIND_TYPE, name, marker_type,
		false, 0, NAMED_TEMPLATE_PARAMETER);
	const std::size_t index = class_templates_.size();
	if (index > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many class templates");
	class_templates_.push_back(pattern);
	if (class_template_pattern_by_entity_.size() <= pattern.marker_entity)
		class_template_pattern_by_entity_.resize(
			static_cast<std::size_t>(pattern.marker_entity) + 1, kNoDumpEdge);
	class_template_pattern_by_entity_[pattern.marker_entity] =
		static_cast<std::uint32_t>(index);
}

std::size_t SemanticAnalyzer::FindClassTemplateIndex(
	const LookupResult& found, NameId requested) const
{
	if (found.type == kNoType) return NoTemplatePattern();
	const TypeRecord& type = program_->types.Get(
		program_->types.RemoveTopCv(found.type));
	if (type.kind != TYPE_NAMED ||
		type.entity >= class_template_pattern_by_entity_.size() ||
		class_template_pattern_by_entity_[type.entity] == kNoDumpEdge)
		return NoTemplatePattern();
	const std::size_t index =
		class_template_pattern_by_entity_[type.entity];
	if (index >= class_templates_.size())
		throw std::logic_error("invalid class template entity index");
	const ClassTemplatePattern& pattern = class_templates_[index];
	if (type.entity == pattern.marker_entity) return index;
	// A specialization's injected primary name denotes the current template,
	// but an arbitrary alias to that specialization is not itself a template.
	if (requested != pattern.name || found.type_declaration == kNoBinding)
		return NoTemplatePattern();
	const BindingRecord& declaration =
		program_->bindings[found.type_declaration];
	return declaration.owner == program_->entities[type.entity].member_scope &&
		declaration.member_owner == type.entity &&
		declaration.name == pattern.name ? index : NoTemplatePattern();
}

std::size_t SemanticAnalyzer::FindClassTemplate(ScopeId scope,
	const std::string& spelling)
{
	const NamePath path = ParseNamePath(spelling);
	return FindClassTemplate(scope, path);
}

std::size_t SemanticAnalyzer::FindClassTemplate(ScopeId scope,
	const NamePath& path)
{
	if (path.Empty()) return NoTemplatePattern();
	return FindClassTemplateIndex(
		LookupPath(scope, path, LOOKUP_TYPE), path.Last());
}

TypeId SemanticAnalyzer::ResolveStructuredTypeName(NodeId name,
	ScopeId argument_scope)
{
	return LookupStructuredName(
		name, argument_scope, LOOKUP_TYPE).type;
}

ScopeId SemanticAnalyzer::BindClassTemplateArguments(
	const ClassTemplatePattern& pattern,
	const std::vector<TemplateArgument>& arguments)
{
	const ScopeId template_scope = NewScope(pattern.lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(pattern.lexical_scope));
	const std::size_t fixed =
		FixedTemplateParameterCount(pattern.parameters);
	for (std::size_t i = 0; i < arguments.size() && i < fixed; ++i)
		BindTemplateArgument(template_scope, pattern.parameters[i], arguments[i]);
	if (HasTrailingTemplateParameterPack(pattern.parameters))
		BindTemplateArgumentPack(template_scope, pattern.parameters.back(),
			arguments, fixed);
	return template_scope;
}

void SemanticAnalyzer::ApplyClassTemplateMemberDefinitions(
	std::size_t index, BindingId specialization,
	const std::vector<TemplateArgument>& arguments)
{
	if (index >= class_templates_.size() || specialization == kNoBinding)
		throw std::logic_error("invalid class template member application");
	const EntityId entity = EntityOf(program_->bindings[specialization].type);
	if (entity == kNoEntity || !program_->entities[entity].complete) return;
	if (class_template_member_definition_counts_.size() <= specialization)
		class_template_member_definition_counts_.resize(
			static_cast<std::size_t>(specialization) + 1, 0);
	while (class_template_member_definition_counts_[specialization] <
		class_templates_[index].member_definitions.size())
	{
		const std::size_t definition_index =
			class_template_member_definition_counts_[specialization]++;
		const ClassTemplateMemberPattern& definition =
			class_templates_[index].member_definitions[definition_index];
		const bool pack_owner =
			definition.owner_parameter_indices.size() == 1 &&
			definition.owner_parameter_indices[0] != kNoDumpEdge &&
			definition.owner_parameter_indices[0] < definition.parameters.size() &&
			definition.parameters[definition.owner_parameter_indices[0]].pack;
		if (!pack_owner &&
			(definition.owner_parameter_indices.size() != arguments.size() ||
			 definition.owner_fixed_arguments.size() != arguments.size()))
			throw std::logic_error("class template member argument shape changed");
		std::vector<TemplateArgument> bindings(definition.parameters.size());
		std::vector<std::uint8_t> bound(definition.parameters.size(), 0);
		for (std::size_t owner = 0;
			!pack_owner && owner < arguments.size(); ++owner)
		{
			const std::uint32_t parameter =
				definition.owner_parameter_indices[owner];
			if (parameter == kNoDumpEdge)
			{
				if (definition.owner_fixed_arguments[owner] != arguments[owner])
					throw std::runtime_error(
						"class template member owner does not match specialization");
				continue;
			}
			if (parameter >= bindings.size())
				throw std::logic_error(
					"class template member parameter index is invalid");
			if (bound[parameter] && bindings[parameter] != arguments[owner])
				throw std::runtime_error(
					"class template member owner deduction conflict");
			bindings[parameter] = arguments[owner];
			bound[parameter] = 1;
		}
		const ScopeId member_scope = program_->entities[entity].member_scope;
		if (member_scope == kNoScope)
			throw std::logic_error(
				"class template member definition has no class scope");
		for (std::size_t parameter = 0;
			!pack_owner && parameter < bindings.size(); ++parameter)
			if (!bound[parameter] || definition.parameters[parameter].name == 0)
				throw std::runtime_error(
					"unbound class template member parameter");
		const ClassTemplateMemberPattern* definition_pointer = &definition;
		const std::vector<TemplateArgument>* binding_pointer = &bindings;
		const std::vector<TemplateArgument>* argument_pointer = &arguments;
		const auto make_definition_scope = [this, definition_pointer,
			binding_pointer, argument_pointer, pack_owner](ScopeId parent) {
			const ScopeId result = NewScope(parent, SCOPE_TEMPLATE_PARAMETERS,
				0, ScopePrefixId(parent));
			if (pack_owner)
			{
				const std::uint32_t parameter =
					definition_pointer->owner_parameter_indices[0];
				BindTemplateArgumentPack(result,
					definition_pointer->parameters[parameter],
					*argument_pointer, 0);
				return result;
			}
			for (std::size_t parameter = 0;
				parameter < binding_pointer->size(); ++parameter)
				BindTemplateArgument(result,
					definition_pointer->parameters[parameter],
					(*binding_pointer)[parameter]);
			return result;
		};
		ScopeId actual_owner = member_scope;
		for (std::size_t part = 0;
			part < definition.nested_owner_path.size(); ++part)
		{
			const LookupResult nested = program_->LookupDirect(actual_owner,
				definition.nested_owner_path[part], LOOKUP_SCOPE_CARRIER);
			actual_owner = nested.name_space != kNoScope ? nested.name_space :
				program_->ScopeForType(nested.type);
			if (actual_owner == kNoScope) break;
		}
		if (actual_owner == kNoScope)
			throw std::runtime_error(
				"class template member definition owner was not found while resolving " +
				program_->names.Get(definition.nested_owner_path.empty() ? 0 :
					definition.nested_owner_path.back()));
		ScopeId definition_scope = make_definition_scope(actual_owner);
		const NodeId node = definition.declaration;

		if (arena_->IsTag(node, "function-definition"))
			AnalyzeFunction(node, definition_scope, root_, true);
		else if (arena_->IsTag(node, "special-member-definition") ||
			arena_->IsTag(node, "special-member-declaration"))
			AnalyzeOutOfClassSpecialMember(node, definition_scope,
				definition_scope, true);
		else if (arena_->IsTag(node, "simple-declaration"))
			AnalyzeSimple(node, definition_scope, root_, false, true);
		else if (arena_->IsTag(node, "class-specifier") ||
			arena_->IsTag(node, "class-forward-declaration"))
		{
			const NamePath nested_name =
				ParseNamePath(arena_->Payload(node));
			const std::string terminal =
				program_->names.Get(nested_name.Last());
			(void)AnalyzeClass(node, definition_scope, std::string(), false,
				terminal, actual_owner, 0, true);
		}
		else throw std::runtime_error(
			"unsupported class template member definition");
	}
}

void SemanticAnalyzer::CompleteClassTemplateSpecialization(std::size_t index,
	BindingId binding, const std::vector<TemplateArgument>& arguments)
{
	if (index >= class_templates_.size())
		throw std::logic_error("invalid class template completion pattern");
	if (!class_templates_[index].defined) return;
	if (class_template_specialization_states_.size() <= binding)
		class_template_specialization_states_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	if (class_template_specialization_states_[binding] != 0) return;

	// Pattern storage is stable across re-entrant publication of nested
	// templates, so replay borrows the one published pattern rather than copying
	// its growing specialization and member-definition sequences.
	const ClassTemplatePattern& pattern = class_templates_[index];
	if ((!HasTrailingTemplateParameterPack(pattern.parameters) &&
		 arguments.size() != pattern.parameters.size()) ||
		(HasTrailingTemplateParameterPack(pattern.parameters) &&
		 arguments.size() < FixedTemplateParameterCount(pattern.parameters)))
		throw std::logic_error("class template completion argument mismatch");
	class_template_specialization_states_[binding] = 1;
	std::string specialization_name = program_->names.Get(pattern.name);
	for (std::size_t i = 0; i < arguments.size(); ++i)
		specialization_name += "_" +
			CanonicalTemplateArgumentName(*program_, arguments[i]);
	specialization_name += "_";
	const ScopeId template_scope =
		BindClassTemplateArguments(pattern, arguments);
	(void)AnalyzeClass(pattern.declaration, template_scope,
		std::string(), false, specialization_name, pattern.owner,
		pattern.name, true, program_->bindings[binding].name);
	class_template_specialization_states_[binding] = 2;
	ApplyClassTemplateMemberDefinitions(index, binding, arguments);
}

void SemanticAnalyzer::EnsureClassDefinition(TypeId type)
{
	if (type == kNoType) return;
	const TypeRecord* record = &program_->types.Get(type);
	while (record->kind == TYPE_QUALIFIED || record->kind == TYPE_ARRAY)
	{
		type = record->child;
		record = &program_->types.Get(type);
	}
	if (record->kind != TYPE_NAMED) return;
	const EntityId entity = record->entity;
	if (program_->entities[entity].complete) return;

	if (entity < class_template_pattern_by_entity_.size() &&
		class_template_pattern_by_entity_[entity] != kNoDumpEdge)
	{
		const std::size_t index = class_template_pattern_by_entity_[entity];
		if (index >= class_templates_.size())
			throw std::logic_error("invalid class specialization owner index");
		const ClassTemplatePattern& pattern = class_templates_[index];
		if (entity == pattern.marker_entity) return;
		const EntityRecord& specialization = program_->entities[entity];
		if (specialization.template_argument_begin == kNoBinding)
			throw std::logic_error("class specialization has no arguments");
		const std::size_t first = specialization.template_argument_begin;
		const std::size_t count = specialization.template_argument_count;
		if ((!HasTrailingTemplateParameterPack(pattern.parameters) &&
			 count != pattern.parameters.size()) ||
			(HasTrailingTemplateParameterPack(pattern.parameters) &&
			 count < FixedTemplateParameterCount(pattern.parameters)) ||
			first > program_->template_arguments.size() ||
			count > program_->template_arguments.size() - first)
			throw std::logic_error("class specialization arguments are truncated");
		const std::vector<TemplateArgument> arguments =
			StoredTemplateArguments(first, count);
		CompleteClassTemplateSpecialization(index,
			program_->entities[entity].declaration, arguments);
		return;
	}

	if (entity < deferred_class_definition_by_entity_.size() &&
		deferred_class_definition_by_entity_[entity] != kNoNode)
	{
		const NodeId definition = deferred_class_definition_by_entity_[entity];
		const ScopeId scope = deferred_class_scope_by_entity_[entity];
		deferred_class_definition_by_entity_[entity] = kNoNode;
		(void)AnalyzeClass(definition, scope, std::string(), false);
	}
}

bool SemanticAnalyzer::ClassTemplateSpecializationArgumentsComplete(
	EntityId entity) const
{
	if (entity >= class_template_pattern_by_entity_.size() ||
		class_template_pattern_by_entity_[entity] == kNoDumpEdge ||
		program_->entities[entity].template_argument_begin == kNoBinding)
		return true;
	const std::size_t index = class_template_pattern_by_entity_[entity];
	if (index >= class_templates_.size())
		throw std::logic_error("invalid class specialization owner index");
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
		throw std::logic_error("class specialization arguments are truncated");
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

bool SemanticAnalyzer::IsClassTemplateSpecializationEntity(
	EntityId entity) const
{
	return entity != kNoEntity &&
		program_->entities[entity].template_argument_begin != kNoBinding;
}

bool SemanticAnalyzer::IsClassTemplateSpecializationContext(
	EntityId entity) const
{
	for (std::size_t depth = 0;
		entity != kNoEntity && depth < program_->entities.size(); ++depth)
	{
		if (IsClassTemplateSpecializationEntity(entity)) return true;
		entity = program_->entities[entity].enclosing_class;
	}
	return false;
}

BindingId SemanticAnalyzer::InstantiateClassTemplate(std::size_t index,
	const std::vector<TypeId>& supplied_arguments)
{
	if (index >= class_templates_.size())
		throw std::logic_error("invalid class template pattern");
	const ClassTemplatePattern& pattern = class_templates_[index];
	const bool has_pack = HasTrailingTemplateParameterPack(pattern.parameters);
	if (!has_pack && supplied_arguments.size() > pattern.parameters.size())
		return kNoBinding;
	std::vector<TemplateArgument> canonical;
	canonical.reserve(supplied_arguments.size());
	for (std::size_t i = 0; i < supplied_arguments.size(); ++i)
	{
		if (TemplateParameterForArgument(pattern.parameters, i).kind !=
			TEMPLATE_ARGUMENT_TYPE)
			return kNoBinding;
		canonical.push_back(TemplateArgument(
			TEMPLATE_ARGUMENT_TYPE, supplied_arguments[i]));
	}
	return InstantiateClassTemplate(index, canonical);
}

BindingId SemanticAnalyzer::InstantiateClassTemplate(std::size_t index,
	const std::vector<TemplateArgument>& supplied_arguments)
{
	if (index >= class_templates_.size())
		throw std::logic_error("invalid class template pattern");
	ClassTemplatePattern& pattern = class_templates_[index];
	const bool has_pack = HasTrailingTemplateParameterPack(pattern.parameters);
	const std::size_t fixed = FixedTemplateParameterCount(pattern.parameters);
	if (!has_pack && supplied_arguments.size() > pattern.parameters.size())
		return kNoBinding;
	++template_specialization_requests_;
	const TemplateSpecializationKey request_key(index, supplied_arguments);
	BindingId old = class_template_instantiations_.Find(request_key);
	if (old != kNoBinding)
	{
		++template_specialization_cache_hits_;
		const EntityId entity = EntityOf(program_->bindings[old].type);
		if (entity == kNoEntity)
			throw std::logic_error("cached class specialization has no entity");
		const EntityRecord& record = program_->entities[entity];
		const std::size_t first = record.template_argument_begin;
		const std::size_t count = record.template_argument_count;
		if ((!has_pack && count != pattern.parameters.size()) ||
			(has_pack && count < fixed) ||
			first > program_->template_arguments.size() || count >
				program_->template_arguments.size() - first)
			throw std::logic_error(
				"cached class specialization arguments are invalid");
		const std::vector<TemplateArgument> cached_arguments =
			StoredTemplateArguments(first, count);
		if (pattern.defined &&
			(old >= class_template_specialization_states_.size() ||
			 class_template_specialization_states_[old] == 0) &&
			ClassTemplateArgumentsAreComplete(*program_, cached_arguments))
			CompleteClassTemplateSpecialization(
				index, old, cached_arguments);
		if (old < class_template_specialization_states_.size() &&
			class_template_specialization_states_[old] == 2)
			ApplyClassTemplateMemberDefinitions(
				index, old, cached_arguments);
		return old;
	}
	std::vector<TemplateArgument> arguments = supplied_arguments;
	for (std::size_t i = 0; i < arguments.size(); ++i)
		if (arguments[i].kind !=
			TemplateParameterForArgument(pattern.parameters, i).kind)
			return kNoBinding;
	ScopeId argument_scope = kNoScope;
	if (arguments.size() < fixed)
	{
		argument_scope = BindClassTemplateArguments(pattern, arguments);
	}
	for (std::size_t i = arguments.size(); i < fixed; ++i)
	{
		const TemplateParameter& parameter = pattern.parameters[i];
		if (parameter.default_argument == kNoNode) return kNoBinding;
		NodeId source = FirstSemanticChild(parameter.default_argument);
		if (source == kNoNode)
			throw std::runtime_error("empty default template argument");
		TemplateArgument argument;
		argument.kind = parameter.kind;
		if (parameter.kind == TEMPLATE_ARGUMENT_TYPE)
		{
			NodeId type_id = arena_->IsTag(source, "type-id") ? source :
				FindChild(source, "type-id");
			if (type_id == kNoNode) return kNoBinding;
			argument.type = BuildTypeId(type_id, argument_scope);
		}
		else
		{
			argument.type = ResolveTemplateParameterType(
				parameter, argument_scope);
			const ExpressionInfo expression = AnalyzeExpression(
				source, argument_scope, argument.type);
			if (!expression.constant || !IsIntegral(expression.type, true))
				throw std::runtime_error(
					"default non-type template argument is not constant");
			argument.value = NormalizeIntegralConstant(
				argument.type, expression.value);
		}
		arguments.push_back(argument);
		BindTemplateArgument(argument_scope, parameter, argument);
	}

	const TemplateSpecializationKey key(index, arguments);
	old = class_template_instantiations_.Find(key);
	if (old != kNoBinding)
	{
		++template_specialization_cache_hits_;
		if (arguments != supplied_arguments)
			class_template_instantiations_.Insert(request_key, old);
		if (pattern.defined &&
			(old >= class_template_specialization_states_.size() ||
			 class_template_specialization_states_[old] == 0) &&
			ClassTemplateArgumentsAreComplete(*program_, arguments))
			CompleteClassTemplateSpecialization(index, old, arguments);
		if (old < class_template_specialization_states_.size() &&
			class_template_specialization_states_[old] == 2)
			ApplyClassTemplateMemberDefinitions(index, old, arguments);
		return old;
	}

	std::string specialization_name = program_->names.Get(pattern.name);
	for (std::size_t i = 0; i < arguments.size(); ++i)
		specialization_name += "_" +
			CanonicalTemplateArgumentName(*program_, arguments[i]);
	specialization_name += "_";
	const ScopeId template_scope =
		BindClassTemplateArguments(pattern, arguments);
	NameId specialization_lookup_name =
		program_->names.Intern(specialization_name);
	if (program_->LookupDirect(pattern.owner, specialization_lookup_name,
		LOOKUP_TYPE).type != kNoType)
		specialization_lookup_name = program_->names.Intern(
			ClassTemplateSpecializationScopeName(index, arguments));
	const TypeId shell = AnalyzeClass(pattern.declaration, template_scope,
		std::string(), false, specialization_name, pattern.owner,
		pattern.name, false, specialization_lookup_name);
	const EntityId entity = EntityOf(shell);
	if (entity == kNoEntity || program_->entities[entity].declaration == kNoBinding)
		throw std::logic_error("class template shell has no declaration");
	if (class_template_pattern_by_entity_.size() <= entity)
		class_template_pattern_by_entity_.resize(
			static_cast<std::size_t>(entity) + 1, kNoDumpEdge);
	class_template_pattern_by_entity_[entity] =
		static_cast<std::uint32_t>(index);
	EntityRecord& specialization = program_->entities[entity];
	specialization.deferred_template_completion =
		!ClassTemplateArgumentsAreComplete(*program_, arguments);
	if (specialization.template_argument_begin == kNoBinding)
		StoreTemplateArguments(arguments,
			&specialization.template_argument_begin,
			&specialization.template_argument_count);
	const BindingId binding = program_->entities[entity].declaration;
	class_template_instantiations_.Insert(key, binding);
	if (arguments != supplied_arguments)
		class_template_instantiations_.Insert(request_key, binding);
	pattern.specialization_bindings.push_back(binding);
	if (class_template_specialization_states_.size() <= binding)
		class_template_specialization_states_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	if (pattern.defined &&
		ClassTemplateArgumentsAreComplete(*program_, arguments))
		CompleteClassTemplateSpecialization(index, binding, arguments);
	return binding;
}

void SemanticAnalyzer::UpgradeClassTemplateSpecializations(std::size_t index)
{
	if (index >= class_templates_.size())
		throw std::logic_error("invalid class template upgrade");
	// A forward-declaration upgrade is one bounded pass over the shells that
	// existed when the definition arrived. Pattern ownership itself is stable;
	// only the mutable shell sequence needs a work snapshot.
	const std::vector<BindingId> specializations =
		class_templates_[index].specialization_bindings;
	for (std::size_t i = 0; i < specializations.size(); ++i)
	{
		const BindingId binding = specializations[i];
		if (binding < class_template_specialization_states_.size() &&
			class_template_specialization_states_[binding] != 0)
			continue;
		std::vector<TemplateArgument> arguments;
		const EntityId entity = EntityOf(program_->bindings[binding].type);
		if (entity == kNoEntity)
			throw std::logic_error("class specialization has no entity");
		const EntityRecord& record = program_->entities[entity];
		const std::size_t first = record.template_argument_begin;
		const std::size_t count = record.template_argument_count;
		const ClassTemplatePattern& pattern = class_templates_[index];
		if ((!HasTrailingTemplateParameterPack(pattern.parameters) &&
			 count != pattern.parameters.size()) ||
			(HasTrailingTemplateParameterPack(pattern.parameters) && count <
			 FixedTemplateParameterCount(pattern.parameters)) ||
			first > program_->template_arguments.size() || count >
				program_->template_arguments.size() - first)
			throw std::logic_error("class specialization arguments are invalid");
		arguments = StoredTemplateArguments(first, count);
		if (ClassTemplateArgumentsAreComplete(*program_, arguments))
			CompleteClassTemplateSpecialization(index, binding, arguments);
	}
}

void SemanticAnalyzer::AnalyzeExplicitInstantiation(NodeId node,
	ScopeId scope, bool definition)
{
	const NodeId target = FirstSemanticChild(node);
	if (target == kNoNode ||
		(!arena_->IsTag(target, "class-forward-declaration") &&
		 !arena_->IsTag(target, "class-specifier")))
		throw std::runtime_error(
			"PA19 explicit instantiation requires a class template-id");
	NamePath base;
	std::vector<TypeId> arguments;
	if (!ParseExplicitTemplateArguments(target, scope, &base, &arguments))
		throw std::runtime_error(
			"explicit class instantiation requires a simple-template-id");
	const std::size_t pattern_index = FindClassTemplate(scope, base);
	if (pattern_index == NoTemplatePattern())
		throw std::runtime_error("explicit class instantiation target was not found");
	const ClassTemplatePattern& pattern = class_templates_[pattern_index];
	const ScopeKind scope_kind = program_->KindOfScope(scope);
	if (scope_kind != SCOPE_NAMESPACE)
		throw std::runtime_error(
			"explicit class instantiation must appear at namespace scope");
	const NamePath& target_path = base;
	if (!target_path.global && target_path.Size() == 1)
	{
		ScopeId permitted = pattern.owner;
		while (permitted != scope && program_->IsInlineNamespace(permitted))
			permitted = program_->ParentScope(permitted);
		if (permitted != scope)
			throw std::runtime_error(
				"unqualified explicit instantiation is in the wrong namespace");
	}
	else
	{
		bool enclosing = false;
		for (ScopeId owner = pattern.owner; owner != kNoScope;
			owner = program_->ParentScope(owner))
			if (owner == scope)
			{
				enclosing = true;
				break;
			}
		if (!enclosing)
			throw std::runtime_error(
				"qualified explicit instantiation is outside its namespace");
	}
	const NodeId target_key = FindChild(target, "class-key");
	const NodeId pattern_key = FindChild(pattern.declaration, "class-key");
	if (target_key == kNoNode || pattern_key == kNoNode ||
		(PayloadSource(target_key) == "union") !=
			(PayloadSource(pattern_key) == "union"))
		throw std::runtime_error("explicit instantiation class-key mismatch");
	const BindingId instantiated = InstantiateClassTemplate(pattern_index, arguments);
	const TypeId type = instantiated == kNoBinding ? kNoType :
		program_->bindings[instantiated].type;
	if (type == kNoType)
		throw std::runtime_error("invalid explicit class template arguments");
	EnsureClassDefinition(type);
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity || !program_->entities[entity].complete)
		throw std::runtime_error(
			"explicit class instantiation target is incomplete");
	const BindingId specialization =
		program_->entities[entity].declaration;
	if (class_template_explicit_instantiation_states_.size() <= specialization)
		class_template_explicit_instantiation_states_.resize(
			static_cast<std::size_t>(specialization) + 1, 0);
	std::uint8_t& state =
		class_template_explicit_instantiation_states_[specialization];
	if (!definition)
	{
		if ((state & 2) != 0)
			throw std::runtime_error(
				"explicit instantiation declaration follows its definition");
		state |= 1;
		return;
	}
	if ((state & 2) != 0)
		throw std::runtime_error(
			"duplicate explicit class instantiation definition");
	state |= 2;
	const auto demand_member = [this](BindingId binding) {
		if (binding == kNoBinding) return;
		binding = program_->bindings[binding].canonical;
		const FunctionInfo& function = GetFunction(binding);
		if (!function.defined || function.implicit_constructor ||
			function.implicit_destructor || function.implicit_special_member ||
			function.deleted_constructor || function.deleted_destructor ||
			function.deleted_special_member)
			return;
		program_->bindings[binding].weak_odr = true;
		program_->bindings[binding].object_output_root = true;
		DemandFunction(binding);
	};
	if (entity < entity_member_functions_.size())
		for (std::size_t i = 0; i < entity_member_functions_[entity].size(); ++i)
			demand_member(entity_member_functions_[entity][i]);
	if (entity < entity_conversion_functions_.size())
		for (std::size_t i = 0;
			i < entity_conversion_functions_[entity].size(); ++i)
			demand_member(entity_conversion_functions_[entity][i]);
	if (entity < entity_constructors_.size())
		for (std::size_t i = 0; i < entity_constructors_[entity].size(); ++i)
			demand_member(entity_constructors_[entity][i]);
	if (entity < entity_destructor_by_entity_.size())
		demand_member(entity_destructor_by_entity_[entity]);
}

}
}
