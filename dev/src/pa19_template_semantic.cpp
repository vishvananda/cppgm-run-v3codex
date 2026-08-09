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

bool ClassTemplateArgumentsAreLayoutReady(const Program& program,
	const std::vector<TemplateArgument>& arguments)
{
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		if (arguments[i].IsDependent()) return false;
		if (arguments[i].kind != TEMPLATE_ARGUMENT_TYPE) continue;
		const TypeId argument = program.types.RemoveTopCv(arguments[i].type);
		const TypeRecord& record = program.types.Get(argument);
		if (record.kind == TYPE_NAMED &&
			!program.entities[record.entity].complete)
			return false;
	}
	return true;
}

bool ClassTemplateArgumentsAllowPartialSelection(const Program& program,
	const std::vector<TemplateArgument>& arguments)
{
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		if (arguments[i].IsDependent()) return false;
		if (arguments[i].kind != TEMPLATE_ARGUMENT_TYPE) continue;
		const TypeId argument = program.types.RemoveTopCv(arguments[i].type);
		const TypeRecord& record = program.types.Get(argument);
		if (record.kind != TYPE_NAMED) continue;
		const EntityRecord& entity = program.entities[record.entity];
		if (entity.complete) continue;
		// Shape parameters and deferred dependent specializations are not
		// complete keys. An ordinary forward-declared class is nevertheless a
		// valid actual argument for partial-specialization matching.
		if (entity.flavor == NAMED_TYPENAME_PARAMETER ||
			entity.flavor == NAMED_TEMPLATE_PARAMETER ||
			entity.deferred_template_completion) return false;
	}
	return true;
}

bool TypeContainsLocalContext(const Program& program, TypeId type,
	std::size_t depth)
{
	if (depth > program.types.Size()) return true;
	const TypeRecord& record = program.types.Get(type);
	if (record.kind == TYPE_NAMED)
	{
		const EntityRecord& entity = program.entities[record.entity];
		if (entity.local_context != kNoBinding) return true;
		const std::size_t first = entity.template_argument_begin;
		if (first == kNoBinding || first > program.template_arguments.size() ||
			entity.template_argument_count >
				program.template_arguments.size() - first) return false;
		for (std::size_t argument = 0;
			argument < entity.template_argument_count; ++argument)
			if ((first + argument >=
				 program.canonical_template_arguments.size() ||
				 program.canonical_template_arguments[first + argument].kind ==
					TEMPLATE_ARGUMENT_TYPE) &&
				TypeContainsLocalContext(program,
					program.template_arguments[first + argument], depth + 1))
				return true;
		return false;
	}
	if (record.kind == TYPE_FUNCTION)
	{
		if (TypeContainsLocalContext(program, record.child, depth + 1))
			return true;
		const TypeId* parameters = program.types.Parameters(type);
		for (std::size_t parameter = 0;
			parameter < record.parameter_count; ++parameter)
			if (TypeContainsLocalContext(
				program, parameters[parameter], depth + 1)) return true;
		return false;
	}
	if (record.kind == TYPE_MEMBER_POINTER && TypeContainsLocalContext(
		program, static_cast<TypeId>(record.bound), depth + 1)) return true;
	return record.kind == TYPE_QUALIFIED || record.kind == TYPE_POINTER ||
		record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE || record.kind == TYPE_ARRAY ||
		record.kind == TYPE_MEMBER_POINTER ?
		TypeContainsLocalContext(program, record.child, depth + 1) : false;
}

bool TemplateArgumentsNeedInternalEmission(const Program& program,
	const std::vector<TemplateArgument>& arguments)
{
	for (std::size_t argument = 0; argument < arguments.size(); ++argument)
		if ((arguments[argument].kind == TEMPLATE_ARGUMENT_TYPE ||
			 arguments[argument].kind == TEMPLATE_ARGUMENT_TEMPLATE) &&
			TypeContainsLocalContext(program, arguments[argument].type, 0))
			return true;
	return false;
}

std::string TemplateArgumentTypeName(const std::string& source)
{
	std::string spelling = source;
	const char* prefixes[] = {"struct ", "class ", "union ", "enum "};
	for (std::size_t prefix = 0; prefix < 4; ++prefix)
	{
		const std::size_t length =
			std::char_traits<char>::length(prefixes[prefix]);
		if (spelling.compare(0, length, prefixes[prefix]) == 0)
		{
			spelling.erase(0, length);
			break;
		}
	}
	return spelling;
}

std::string CanonicalTemplateArgumentPresentation(const Program& program,
	const TemplateArgument& argument)
{
	if (argument.kind == TEMPLATE_ARGUMENT_TYPE ||
		argument.kind == TEMPLATE_ARGUMENT_TEMPLATE)
		return TemplateArgumentTypeName(program.RenderType(argument.type));
	if (argument.IsDependent())
	{
		std::ostringstream result;
		result << "dependent(" << TemplateArgumentTypeName(
			program.RenderType(argument.type)) << ", "
			<< argument.dependent_parameter << ')';
		return result.str();
	}
	const TypeId type = program.types.RemoveTopCv(argument.type);
	const TypeRecord& record = program.types.Get(type);
	if (record.kind == TYPE_FUNDAMENTAL &&
		record.fundamental == FUND_BOOL)
		return argument.value == 0 ? "false" : "true";
	if (record.kind == TYPE_NAMED && record.entity != kNoEntity &&
		program.entities[record.entity].flavor == NAMED_ENUM)
		return "(" + program.names.Get(program.entities[record.entity].name) +
			")" + std::to_string(argument.value);
	return std::to_string(argument.value);
}

std::string ClassTemplateSpecializationName(const Program& program,
	NameId primary, const std::vector<TemplateArgument>& arguments)
{
	std::string source = program.names.Get(primary) + "<";
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		if (i != 0) source += ", ";
		source += CanonicalTemplateArgumentPresentation(program, arguments[i]);
	}
	source += '>';
	std::string result;
	result.reserve(source.size());
	for (std::size_t i = 0; i < source.size(); ++i)
	{
		const unsigned char character =
			static_cast<unsigned char>(source[i]);
		result += std::isalnum(character) || character == '_' ?
			static_cast<char>(character) : '_';
	}
	return result;
}

std::string ClassTemplateSpecializationScopeName(std::size_t pattern,
	std::size_t ordinal)
{
	std::ostringstream result;
	result << "__cppgm_class_template_" << pattern << '_' << ordinal;
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
	if (path.Size() <= 1)
	{
		if (!path.global && path.Size() == 1 &&
			(kind == LOOKUP_TYPE || kind == LOOKUP_SCOPE_CARRIER))
		{
			TypeId alias = kNoType;
			if (FindConstexprTypeAlias(path[0], &alias))
			{
				LookupResult result;
				result.type = alias;
				return result;
			}
		}
		LookupResult result = program_->Lookup(scope, path, kind);
		if (path.global || path.Size() == 0) return result;
		std::vector<ScopeId> using_scopes;
		FindConstexprUsingNamespaces(&using_scopes);
		for (std::size_t i = 0; i < using_scopes.size(); ++i)
		{
			NamePath terminal;
			terminal.Push(path[0]);
			const LookupResult candidate = program_->LookupQualified(
				using_scopes[i], terminal, kind);
			if (result.Empty()) result = candidate;
			else
			{
				for (std::size_t ordinary = 0;
					ordinary < candidate.OrdinaryCount(); ++ordinary)
					result.AddOrdinary(candidate.OrdinaryAt(ordinary));
				if (candidate.HasFunctionTemplateLookup())
				{
					if (!result.HasFunctionTemplateLookup())
						result.BeginFunctionTemplateLookup();
					for (std::size_t owner = 0;
						owner < candidate.FunctionTemplateOwnerCount(); ++owner)
						result.AddFunctionTemplateOwner(
							candidate.FunctionTemplateOwnerAt(owner));
				}
				if (result.type == kNoType) result.type = candidate.type;
				if (result.name_space == kNoScope)
					result.name_space = candidate.name_space;
			}
		}
		return result;
	}
	ScopeId carrier = path.global ? program_->GlobalScope() : kNoScope;
	std::size_t component = 0;
	if (!path.global)
	{
		TypeId alias = kNoType;
		if (FindConstexprTypeAlias(path[0], &alias))
		{
			EnsureClassDefinition(alias);
			carrier = program_->ScopeForType(alias);
		}
		else
		{
			const LookupResult first = program_->LookupName(
				scope, path[0], LOOKUP_SCOPE_CARRIER);
			if (first.type != kNoType) EnsureClassDefinition(first.type);
			carrier = first.name_space != kNoScope ? first.name_space :
				first.type != kNoType ? program_->ScopeForType(first.type) :
				kNoScope;
		}
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
		{
			NamePath unqualified;
			unqualified.Push(component);
			found = LookupPath(scope, unqualified, component_kind);
		}
		else
		{
			NamePath direct;
			direct.Push(component);
			found = program_->LookupQualified(
				carrier, direct, component_kind);
		}
		if (argument_list == kNoNode &&
			found.type_declaration != kNoBinding &&
			!CanAccessMember(found.type_declaration, found.naming_class))
			throw std::runtime_error("inaccessible qualified type");

		if (argument_list != kNoNode)
		{
			std::vector<NodeId> argument_syntax;
			for (std::uint32_t edge = arena_->FirstEdge(argument_list);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
				argument_syntax.push_back(arena_->EdgeChild(edge));
			if (found.type_declaration != kNoBinding &&
				!CanAccessMember(found.type_declaration, found.naming_class))
				throw std::runtime_error("inaccessible template type");
			const std::size_t alias =
				FindAliasTemplateIndex(found, component);
			if (alias != NoTemplatePattern())
			{
				const AliasTemplatePattern& alias_pattern =
					alias_templates_[alias];
				std::vector<TemplateArgument> arguments;
				if (!BuildTemplateArguments(alias_pattern.parameters,
					argument_syntax, scope, alias_pattern.lexical_scope,
					&arguments)) return LookupResult();
				const TypeId type = InstantiateAliasTemplate(alias, arguments);
				if (type == kNoType) return LookupResult();
				found = LookupResult();
				found.type = type;
			}
			else
			{
				const std::size_t pattern =
					FindClassTemplateIndex(found, component);
				if (pattern == NoTemplatePattern()) return LookupResult();
				std::vector<TemplateArgument> arguments;
				const ClassTemplatePattern& class_pattern =
					class_templates_[pattern];
				if (!BuildTemplateArguments(class_pattern.parameters,
					argument_syntax, scope, class_pattern.lexical_scope,
					&arguments)) return LookupResult();
				const BindingId specialization =
					InstantiateClassTemplate(pattern, arguments);
				if (specialization == kNoBinding) return LookupResult();
				found = LookupResult();
				found.type = program_->bindings[specialization].type;
				found.type_declaration = specialization;
				found.type_declaration_canonical =
					program_->bindings[specialization].canonical;
			}
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
		const NodeId declarator = arena_->IsTag(argument, "type-id") ?
			FindChild(argument, "abstract-declarator") : kNoNode;
		if (!arena_->IsTag(argument, "type-id"))
		{
			arguments->clear();
			return false;
		}
		if (declarator != kNoNode &&
			FindChild(declarator, "parameter-pack") != kNoNode)
		{
			std::vector<ScopeId> element_scopes;
			if (!ExpandPackElementScopes(
				argument, scope, &element_scopes))
			{
				arguments->clear();
				return false;
			}
			for (std::size_t element = 0;
				element < element_scopes.size(); ++element)
			{
				const TypeId type = BuildTypeId(
					argument, element_scopes[element]);
				if (type == kNoType)
				{
					arguments->clear();
					return false;
				}
				arguments->push_back(type);
			}
			continue;
		}
		const TypeId type = BuildTypeId(argument, scope);
		if (type == kNoType)
			throw std::runtime_error("unknown explicit template type argument");
		arguments->push_back(type);
	}
	return true;
}

bool SemanticAnalyzer::ClassTemplateMemberNamesPrimaryParameters(
	const std::vector<TemplateParameter>& parameters,
	const std::vector<TemplateArgument>& arguments) const
{
	if (parameters.size() != arguments.size()) return false;
	for (std::size_t i = 0; i < parameters.size(); ++i)
	{
		const TemplateParameter& parameter = parameters[i];
		const TemplateArgument& argument = arguments[i];
		if (argument.kind != parameter.kind ||
			argument.pack_expansion != parameter.pack) return false;
		if (argument.kind == TEMPLATE_ARGUMENT_TYPE)
		{
			if (i >= function_template_shape_parameters_.size() ||
				argument.type != function_template_shape_parameters_[i])
				return false;
		}
		else if (argument.kind == TEMPLATE_ARGUMENT_INTEGRAL)
		{
			if (!argument.IsDependent() || argument.dependent_parameter != i)
				return false;
		}
		else
		{
			const TypeRecord marker = program_->types.Get(
				program_->types.RemoveTopCv(argument.type));
			if (marker.kind != TYPE_NAMED ||
				marker.entity >= class_template_pattern_by_entity_.size())
				return false;
			const std::uint32_t pattern =
				class_template_pattern_by_entity_[marker.entity];
			if (pattern == kNoDumpEdge || pattern >= class_templates_.size() ||
				!class_templates_[pattern].template_parameter_proxy ||
				class_templates_[pattern].template_parameter_ordinal != i)
				return false;
		}
	}
	return true;
}

bool SemanticAnalyzer::AnalyzeClassTemplateMember(NodeId declaration,
	ScopeId scope, const std::vector<TemplateParameter>& parameters)
{
	// Out-of-class member templates have one template head for the class and
	// another for the member.  The outer head owns attachment to each concrete
	// class specialization; retain the inner declaration intact so its own head
	// is analyzed exactly once when that attachment is replayed.
	NodeId described_declaration = declaration;
	while (arena_->IsTag(described_declaration, "template-declaration"))
	{
		const NodeId clause = FindChild(
			described_declaration, "template-parameter-clause");
		NodeId target = kNoNode;
		for (std::uint32_t edge = arena_->FirstEdge(described_declaration);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (child != clause) target = child;
		}
		if (target == kNoNode) return false;
		described_declaration = target;
	}
	NodeId declarator = FindChild(described_declaration, "declarator");
	if (arena_->IsTag(described_declaration, "simple-declaration"))
	{
		const NodeId list = FindChild(
			described_declaration, "init-declarator-list");
		const NodeId item = list == kNoNode ? kNoNode :
			FirstSemanticChild(list);
		declarator = item == kNoNode ? kNoNode :
			FindChild(item, "declarator");
	}
	NodeId structure = kNoNode;
	if (arena_->IsTag(described_declaration, "class-specifier") ||
		arena_->IsTag(described_declaration, "class-forward-declaration"))
		structure = FindChild(
			described_declaration, "structured-type-name");
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
	if ((!HasTrailingTemplateParameterPack(owner_pattern.parameters) &&
		 owner_arguments.size() != owner_pattern.parameters.size()) ||
		(HasTrailingTemplateParameterPack(owner_pattern.parameters) &&
		 owner_arguments.size() <
			FixedTemplateParameterCount(owner_pattern.parameters)))
		throw std::runtime_error("invalid class template member owner shape");

	ClassTemplateMemberPattern member;
	member.lexical_scope = scope;
	member.declaration = declaration;
	member.parameters = parameters;
	for (std::size_t component = owner_component + 1;
		component + 1 < path.Size(); ++component)
		if (path[component] != path[owner_component])
			member.nested_owner_path.push_back(path[component]);
	if (owner_pattern.defined && !member.nested_owner_path.empty() &&
		FindChild(owner_pattern.declaration, "base-clause") == kNoNode &&
		!RetainedClassDeclaresNestedPath(
			owner_pattern.declaration, member.nested_owner_path))
		throw std::runtime_error(
			"class template member has a missing nested owner");
	std::uint8_t owner_shape_state = 0;
	if (!MaterializeTemplatePartialArguments(owner_pattern.parameters,
		parameters, owner_arguments, scope, &member.canonical_owner_arguments,
		&owner_shape_state) || owner_shape_state != 1)
		throw std::runtime_error(
			"class template member owner pattern is not deducible");
	for (std::size_t partial_index = 0;
		partial_index < owner_pattern.partial_specializations.size();
		++partial_index)
	{
		ClassTemplatePartialPattern& partial = class_templates_[
			pattern_index].partial_specializations[partial_index];
		if (!MaterializeTemplatePartialArguments(owner_pattern.parameters,
			partial.parameters, partial.arguments, partial.lexical_scope,
			&partial.canonical_arguments,
			&partial.canonical_argument_state)) continue;
		FunctionTemplateDeduction partial_from_member(partial.parameters);
		FunctionTemplateDeduction member_from_partial(member.parameters);
		if (!MatchTemplatePartialArguments(partial.parameters,
			partial.canonical_arguments, member.canonical_owner_arguments,
			&partial_from_member) ||
			!MatchTemplatePartialArguments(member.parameters,
				member.canonical_owner_arguments, partial.canonical_arguments,
				&member_from_partial)) continue;
		if (partial_index > std::numeric_limits<std::uint32_t>::max())
			throw std::runtime_error("too many class partial patterns");
		member.owner_partial_pattern =
			static_cast<std::uint32_t>(partial_index);
		break;
	}
	if (member.owner_partial_pattern == kNoDumpEdge &&
		!ClassTemplateMemberNamesPrimaryParameters(
			parameters, member.canonical_owner_arguments))
		throw std::runtime_error(
			"class template member owner is not a declared specialization");

	const bool demand_definition =
		arena_->IsTag(described_declaration, "simple-declaration");
	if (demand_definition)
		class_templates_[pattern_index].demanded_member_definitions.push_back(
			member);
	else class_templates_[pattern_index].member_definitions.push_back(member);
	if (parameters.empty())
	{
		bool concrete_owner = true;
		for (std::size_t i = 0;
			i < member.canonical_owner_arguments.size(); ++i)
			if (member.canonical_owner_arguments[i].IsDependent())
				concrete_owner = false;
		if (concrete_owner)
			(void)InstantiateClassTemplate(
				pattern_index, member.canonical_owner_arguments);
	}
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
		if (demand_definition)
			QueueClassTemplateMemberDefinitions(
				pattern_index, specializations[i]);
		else ApplyClassTemplateMemberDefinitions(
			pattern_index, specializations[i], arguments);
	}
	return true;
}

bool SemanticAnalyzer::RetainedClassDeclaresNestedPath(NodeId declaration,
	const std::vector<NameId>& path)
{
	NodeId owner = declaration;
	for (std::size_t part = 0; part < path.size(); ++part)
	{
		NodeId selected = kNoNode;
		for (std::uint32_t edge = arena_->FirstEdge(owner);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			NodeId candidate = arena_->EdgeChild(edge);
			while (arena_->IsTag(candidate, "template-declaration"))
			{
				const NodeId clause = FindChild(
					candidate, "template-parameter-clause");
				NodeId target = kNoNode;
				for (std::uint32_t nested = arena_->FirstEdge(candidate);
					nested != kNoEdge; nested = arena_->NextEdge(nested))
				{
					const NodeId child = arena_->EdgeChild(nested);
					if (child != clause) target = child;
				}
				candidate = target;
			}
			if (candidate == kNoNode ||
				(!arena_->IsTag(candidate, "class-specifier") &&
				 !arena_->IsTag(candidate, "class-forward-declaration")))
				continue;
			const NodeId structure = FindChild(
				candidate, "structured-type-name");
			const NameId name = structure == kNoNode ?
				program_->names.Intern(arena_->Payload(candidate)) :
				StructuredNamePath(structure).Last();
			if (name == path[part])
			{
				selected = candidate;
				break;
			}
		}
		if (selected == kNoNode) return false;
		owner = selected;
	}
	return true;
}

bool SemanticAnalyzer::RetainVariableTemplate(NodeId declaration,
	ScopeId scope, const std::vector<TemplateParameter>& parameters)
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
	pattern.parameters = parameters;
	if (terminal_component != kNoNode)
	{
		const NodeId arguments = FindChild(
			terminal_component, "template-type-argument-list");
		if (arguments != kNoNode)
			for (std::uint32_t edge = arena_->FirstEdge(arguments);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
				pattern.specialization_arguments.push_back(
					arena_->EdgeChild(edge));
	}
	pattern.partial_specialization = partial;
	const std::size_t index = variable_templates_.size();
	if (index > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many variable templates");
	variable_templates_.push_back(pattern);
	variable_template_sets_.Ensure(key).Push(index);
	return true;
}

void SemanticAnalyzer::PublishClassTemplateFriendGrants(
	const ClassTemplatePattern& pattern, EntityId specialization)
{
	if (specialization == kNoEntity) return;
	for (std::size_t i = 0; i < pattern.friend_owners.size(); ++i)
	{
		const std::uint64_t key =
			(static_cast<std::uint64_t>(pattern.friend_owners[i]) << 32) |
			specialization;
		CompactIndexSequence& grants = friend_class_grants_.Ensure(key);
		if (grants.Size() == 0) grants.Push(0);
	}
}

void SemanticAnalyzer::RegisterClassTemplateFriend(
	std::size_t pattern_index, EntityId owner)
{
	if (pattern_index >= class_templates_.size() || owner == kNoEntity)
		throw std::logic_error("invalid class template friend owner");
	ClassTemplatePattern& pattern = class_templates_[pattern_index];
	if (std::find(pattern.friend_owners.begin(), pattern.friend_owners.end(),
		owner) == pattern.friend_owners.end())
		pattern.friend_owners.push_back(owner);
	for (std::size_t i = 0; i < pattern.specialization_bindings.size(); ++i)
	{
		const EntityId specialization = EntityOf(
			program_->bindings[pattern.specialization_bindings[i]].type);
		PublishClassTemplateFriendGrants(pattern, specialization);
	}
}

bool SemanticAnalyzer::AnalyzeFriendClassTemplate(NodeId target,
	ScopeId scope, const std::vector<TemplateParameter>& parameters)
{
	if (!arena_->IsTag(target, "simple-declaration")) return false;
	const NodeId specifiers = FindChild(target, "decl-specifier-seq");
	const NodeId declaration = specifiers == kNoNode ? kNoNode :
		FindChild(specifiers, "class-forward-declaration");
	if (declaration == kNoNode) return false;
	bool friend_specifier = false;
	for (std::uint32_t edge = arena_->FirstEdge(specifiers); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (PayloadSource(arena_->EdgeChild(edge)) == "friend")
			friend_specifier = true;
	if (!friend_specifier) return false;
	const NodeId declarators = FindChild(target, "init-declarator-list");
	if (declarators != kNoNode && FirstSemanticChild(declarators) != kNoNode)
		throw std::runtime_error(
			"friend class template has an unexpected declarator");
	ScopeId class_scope = scope;
	while (class_scope != kNoScope &&
		program_->KindOfScope(class_scope) != SCOPE_CLASS)
		class_scope = program_->ParentScope(class_scope);
	const EntityId friend_owner = class_scope == kNoScope ? kNoEntity :
		program_->EntityForScope(class_scope);
	if (friend_owner == kNoEntity)
		throw std::runtime_error("friend class template has no class owner");

	NamePath path;
	const NodeId structure = FindChild(declaration, "structured-type-name");
	if (structure != kNoNode) path = StructuredNamePath(structure);
	else path.Push(program_->names.Intern(arena_->Payload(declaration)));
	if (path.Empty())
		throw std::runtime_error("friend class template has no name");
	ScopeId declaration_scope = scope;
	if (!path.global && path.Size() == 1)
	{
		declaration_scope = program_->entities[friend_owner].owner;
		while (declaration_scope != kNoScope &&
			program_->KindOfScope(declaration_scope) != SCOPE_NAMESPACE)
			declaration_scope = program_->ParentScope(declaration_scope);
	}
	if (declaration_scope == kNoScope)
		throw std::runtime_error("friend class template has no namespace owner");
	AnalyzeClassTemplate(declaration, declaration_scope, parameters);
	const std::size_t pattern = FindClassTemplate(declaration_scope, path);
	if (pattern == NoTemplatePattern())
		throw std::logic_error("friend class template was not registered");
	RegisterClassTemplateFriend(pattern, friend_owner);
	return true;
}

void SemanticAnalyzer::AnalyzeClassTemplate(NodeId declaration, ScopeId scope,
	const std::vector<TemplateParameter>& parameters,
	AccessKind member_access)
{
	NamePath partial_primary;
	std::vector<NodeId> partial_arguments;
	if (CollectExplicitTemplateArguments(
		declaration, &partial_primary, &partial_arguments))
	{
		const std::size_t primary = FindClassTemplate(scope, partial_primary);
		if (primary == NoTemplatePattern())
			throw std::runtime_error(
				"class partial specialization has no primary");
		ClassTemplatePartialPattern partial;
		partial.lexical_scope = scope;
		partial.declaration = declaration;
		partial.parameters = parameters;
		partial.arguments = partial_arguments;
		ClassTemplatePattern& owner = class_templates_[primary];
		(void)MaterializeTemplatePartialArguments(owner.parameters,
			partial.parameters, partial.arguments, partial.lexical_scope,
			&partial.canonical_arguments, &partial.canonical_argument_state);
		for (std::size_t i = 0; i < owner.partial_specializations.size(); ++i)
		{
			ClassTemplatePartialPattern& prior = owner.partial_specializations[i];
			if (!MaterializeTemplatePartialArguments(owner.parameters,
				prior.parameters, prior.arguments, prior.lexical_scope,
				&prior.canonical_arguments, &prior.canonical_argument_state) ||
				partial.canonical_argument_state != 1) continue;
			FunctionTemplateDeduction prior_from_new(prior.parameters);
			FunctionTemplateDeduction new_from_prior(partial.parameters);
			if (!MatchTemplatePartialArguments(prior.parameters,
				prior.canonical_arguments, partial.canonical_arguments,
				&prior_from_new) ||
				!MatchTemplatePartialArguments(partial.parameters,
					partial.canonical_arguments, prior.canonical_arguments,
					&new_from_prior)) continue;
			const bool prior_definition =
				(arena_->Flags(prior.declaration) & SYNTAX_FLAG_DEFINITION) != 0;
			const bool definition =
				(arena_->Flags(declaration) & SYNTAX_FLAG_DEFINITION) != 0;
			if (prior_definition && definition)
				throw std::runtime_error(
					"duplicate class template partial definition");
			if (definition)
			{
				if (prior.revision ==
					std::numeric_limits<std::uint32_t>::max())
					throw std::runtime_error(
						"too many class partial redeclarations");
				partial.revision = prior.revision + 1;
				prior = partial;
			}
			return;
		}
		owner.partial_specializations.push_back(partial);
		return;
	}
	NamePath path;
	const NodeId name_structure = FindChild(
		declaration, "structured-type-name");
	if (name_structure != kNoNode)
		path = StructuredNamePath(name_structure);
	else path.Push(program_->names.Intern(arena_->Payload(declaration)));
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
			if (parameters[i].kind == TEMPLATE_ARGUMENT_TEMPLATE &&
				(!TemplateTemplateParameterMatches(
					prior.parameters[i].template_parameters,
					parameters[i].template_parameters) ||
				 !TemplateTemplateParameterMatches(
					parameters[i].template_parameters,
					prior.parameters[i].template_parameters)))
				throw std::runtime_error(
					"class template template-parameter shape mismatch");
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
	const BindingId marker_binding = program_->AddBinding(owner, BIND_TYPE,
		name, marker_type, false, 0, NAMED_TEMPLATE_PARAMETER);
	const EntityId class_owner = program_->EntityForScope(owner);
	if (class_owner != kNoEntity)
	{
		program_->bindings[marker_binding].member_owner = class_owner;
		program_->bindings[marker_binding].access = member_access;
	}
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
			arguments, fixed, arguments.size());
	return template_scope;
}

void SemanticAnalyzer::ApplyClassTemplateMemberDefinitions(
	std::size_t index, BindingId specialization,
	const std::vector<TemplateArgument>& arguments, bool demanded)
{
	if (index >= class_templates_.size() || specialization == kNoBinding)
		throw std::logic_error("invalid class template member application");
	const EntityId entity = EntityOf(program_->bindings[specialization].type);
	if (entity == kNoEntity || !program_->entities[entity].complete) return;
	std::vector<std::uint32_t>& counts = demanded ?
		class_template_demanded_member_definition_counts_ :
		class_template_member_definition_counts_;
	const std::deque<ClassTemplateMemberPattern>& definitions = demanded ?
		class_templates_[index].demanded_member_definitions :
		class_templates_[index].member_definitions;
	if (counts.size() <= specialization)
		counts.resize(
			static_cast<std::size_t>(specialization) + 1, 0);
	while (counts[specialization] < definitions.size())
	{
		const std::size_t definition_index =
			counts[specialization]++;
		const ClassTemplateMemberPattern& definition =
			definitions[definition_index];
		const std::uint32_t selected_partial = specialization <
			class_template_partial_selections_.size() ?
			class_template_partial_selections_[specialization].pattern : kNoDumpEdge;
		if (definition.owner_partial_pattern != selected_partial) continue;
		FunctionTemplateDeduction owner_bindings(definition.parameters);
		if (!MatchTemplatePartialArguments(definition.parameters,
			definition.canonical_owner_arguments, arguments, &owner_bindings))
			continue;
		const ScopeId member_scope = program_->entities[entity].member_scope;
		if (member_scope == kNoScope)
			throw std::logic_error(
				"class template member definition has no class scope");
		const ClassTemplateMemberPattern* definition_pointer = &definition;
		const FunctionTemplateDeduction* binding_pointer = &owner_bindings;
		const auto make_definition_scope = [this, definition_pointer,
			binding_pointer](ScopeId parent) {
			const ScopeId result = NewScope(parent, SCOPE_TEMPLATE_PARAMETERS,
				0, ScopePrefixId(parent));
			for (std::size_t parameter = 0;
				parameter < definition_pointer->parameters.size(); ++parameter)
				if (definition_pointer->parameters[parameter].pack)
					BindTemplateArgumentPack(result,
						definition_pointer->parameters[parameter],
						binding_pointer->pack_arguments[parameter], 0,
						binding_pointer->pack_arguments[parameter].size());
				else BindTemplateArgument(result,
					definition_pointer->parameters[parameter],
					binding_pointer->fixed_arguments[parameter]);
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

		if (arena_->IsTag(node, "template-declaration"))
		{
			++class_template_member_replay_depth_;
			try
			{
				AnalyzeTemplate(node, definition_scope, ACCESS_PUBLIC);
			}
			catch (...)
			{
				--class_template_member_replay_depth_;
				throw;
			}
			--class_template_member_replay_depth_;
		}
		else if (arena_->IsTag(node, "function-definition"))
			AnalyzeFunction(node, definition_scope, root_, true);
		else if (arena_->IsTag(node, "special-member-definition") ||
			arena_->IsTag(node, "special-member-declaration"))
			AnalyzeOutOfClassSpecialMember(node, definition_scope,
				definition_scope, true);
		else if (arena_->IsTag(node, "simple-declaration"))
			AnalyzeSimple(node, definition_scope, root_, false, true, demanded);
		else if (arena_->IsTag(node, "class-specifier") ||
			arena_->IsTag(node, "class-forward-declaration"))
		{
			const NodeId structure = FindChild(node, "structured-type-name");
			const NameId terminal_name = structure == kNoNode ?
				program_->names.Intern(arena_->Payload(node)) :
				StructuredNamePath(structure).Last();
			const std::string terminal = program_->names.Get(terminal_name);
			(void)AnalyzeClass(node, definition_scope, std::string(), false,
				terminal, actual_owner, 0, true);
		}
		else throw std::runtime_error(
			"unsupported class template member definition");
	}
}

void SemanticAnalyzer::QueueClassTemplateMemberDefinitions(
	std::size_t pattern, BindingId specialization)
{
	if (pattern >= class_templates_.size() || specialization == kNoBinding)
		throw std::logic_error("invalid class template member demand");
	if (class_template_member_definition_demand_states_.size() <= specialization)
		class_template_member_definition_demand_states_.resize(
			static_cast<std::size_t>(specialization) + 1, 0);
	std::uint8_t& state =
		class_template_member_definition_demand_states_[specialization];
	if ((state & 1U) == 0 || (state & 2U) != 0) return;
	const std::size_t applied =
		specialization <
			class_template_demanded_member_definition_counts_.size() ?
		class_template_demanded_member_definition_counts_[specialization] : 0;
	if (applied >=
		class_templates_[pattern].demanded_member_definitions.size()) return;
	state |= 2U;
	demanded_class_template_member_definitions_.push_back(specialization);
	++demand_worklist_pushes_;
}

void SemanticAnalyzer::DemandClassTemplateMemberDefinitions(EntityId entity)
{
	for (std::size_t depth = 0; entity != kNoEntity &&
		depth <= program_->entities.size(); ++depth)
	{
		if (entity >= program_->entities.size())
			throw std::logic_error("invalid class template member demand entity");
		if (entity >= class_template_pattern_by_entity_.size() ||
			class_template_pattern_by_entity_[entity] == kNoDumpEdge)
		{
			entity = program_->entities[entity].enclosing_class;
			continue;
		}
		const std::size_t pattern = class_template_pattern_by_entity_[entity];
		if (pattern >= class_templates_.size())
			throw std::logic_error("invalid class template member demand owner");
		const BindingId specialization = program_->entities[entity].declaration;
		if (specialization == kNoBinding) return;
		if (class_template_member_definition_demand_states_.size() <= specialization)
			class_template_member_definition_demand_states_.resize(
				static_cast<std::size_t>(specialization) + 1, 0);
		class_template_member_definition_demand_states_[specialization] |= 1U;
		QueueClassTemplateMemberDefinitions(pattern, specialization);
		return;
	}
}

void SemanticAnalyzer::ApplyDemandedClassTemplateMemberDefinitions(
	BindingId specialization)
{
	if (specialization == kNoBinding ||
		specialization >= program_->bindings.size())
		throw std::logic_error("invalid demanded class template member owner");
	const EntityId entity = EntityOf(program_->bindings[specialization].type);
	if (entity == kNoEntity || entity >= class_template_pattern_by_entity_.size())
		throw std::logic_error("demanded class template member has no entity");
	const std::size_t pattern = class_template_pattern_by_entity_[entity];
	if (pattern == kNoDumpEdge || pattern >= class_templates_.size())
		throw std::logic_error("demanded class template member has no pattern");
	if (class_template_member_definition_demand_states_.size() <= specialization)
		throw std::logic_error("class template member demand state is missing");
	class_template_member_definition_demand_states_[specialization] &= 1U;
	const EntityRecord& record = program_->entities[entity];
	if (record.template_argument_begin == kNoBinding)
		throw std::logic_error("demanded class template member has no arguments");
	const std::vector<TemplateArgument> arguments = StoredTemplateArguments(
		record.template_argument_begin, record.template_argument_count);
	ApplyClassTemplateMemberDefinitions(
		pattern, specialization, arguments, true);
	QueueClassTemplateMemberDefinitions(pattern, specialization);
}

void SemanticAnalyzer::CompleteClassTemplateSpecialization(std::size_t index,
	BindingId binding, const std::vector<TemplateArgument>& arguments)
{
	if (index >= class_templates_.size())
		throw std::logic_error("invalid class template completion pattern");
	if (class_template_specialization_states_.size() <= binding)
		class_template_specialization_states_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	if (class_template_specialization_states_[binding] != 0) return;

	// Pattern storage is stable across re-entrant publication of nested
	// templates, so replay borrows the one published pattern rather than copying
	// its growing specialization and member-definition sequences.
	ClassTemplatePattern& pattern = class_templates_[index];
	if ((!HasTrailingTemplateParameterPack(pattern.parameters) &&
		 arguments.size() != pattern.parameters.size()) ||
		(HasTrailingTemplateParameterPack(pattern.parameters) &&
		 arguments.size() < FixedTemplateParameterCount(pattern.parameters)))
		throw std::logic_error("class template completion argument mismatch");
	ClassTemplatePartialSelection* selection =
		binding < class_template_partial_selections_.size() ?
		&class_template_partial_selections_[binding] : 0;
	const std::size_t selected_partial = selection ?
		selection->pattern : kNoDumpEdge;
	NodeId declaration = pattern.declaration;
	ScopeId template_scope = kNoScope;
	if (selected_partial != kNoDumpEdge)
	{
		if (selected_partial >= pattern.partial_specializations.size())
			throw std::logic_error(
				"selected class partial completion owner is invalid");
		ClassTemplatePartialPattern& selected =
			pattern.partial_specializations[selected_partial];
		if ((arena_->Flags(selected.declaration) &
			SYNTAX_FLAG_DEFINITION) == 0) return;
		if (!selection)
			throw std::logic_error(
				"selected class partial has no substitution owner");
		if (selection->revision != selected.revision)
		{
			FunctionTemplateDeduction refreshed(selected.parameters);
			if (!MatchTemplatePartialArguments(selected.parameters,
				selected.canonical_arguments, arguments, &refreshed))
				throw std::logic_error(
					"redeclared class partial no longer matches its shell");
			selection->bindings.fixed_arguments.swap(
				refreshed.fixed_arguments);
			selection->bindings.pack_arguments.swap(
				refreshed.pack_arguments);
			selection->bindings.pack_deduction_positions.swap(
				refreshed.pack_deduction_positions);
			selection->revision = selected.revision;
		}
		const FunctionTemplateDeduction& bindings = selection->bindings;
		if (bindings.fixed_arguments.size() != selected.parameters.size() ||
			bindings.pack_arguments.size() != selected.parameters.size())
			throw std::logic_error(
				"selected class partial substitution shape is invalid");
		template_scope = NewScope(selected.lexical_scope,
			SCOPE_TEMPLATE_PARAMETERS, 0,
			ScopePrefixId(selected.lexical_scope));
		for (std::size_t parameter = 0;
			parameter < selected.parameters.size(); ++parameter)
			if (selected.parameters[parameter].pack)
				BindTemplateArgumentPack(template_scope,
					selected.parameters[parameter],
					bindings.pack_arguments[parameter], 0,
					bindings.pack_arguments[parameter].size());
			else BindTemplateArgument(template_scope,
				selected.parameters[parameter],
				bindings.fixed_arguments[parameter]);
		declaration = selected.declaration;
	}
	else
	{
		if (!pattern.defined) return;
		template_scope = BindClassTemplateArguments(pattern, arguments);
	}
	class_template_specialization_states_[binding] = 1;
	const std::string specialization_name =
		ClassTemplateSpecializationName(*program_, pattern.name, arguments);
	const NameId specialization_emission_name =
		TemplateArgumentsNeedInternalEmission(*program_, arguments) ?
			program_->bindings[binding].name :
			program_->names.Intern(specialization_name);
	const TypeId completed = AnalyzeClass(declaration, template_scope,
		std::string(), false, specialization_name, pattern.owner,
		pattern.name, true, program_->bindings[binding].name,
		specialization_emission_name);
	const EntityId entity = EntityOf(completed);
	if (entity == kNoEntity || !program_->entities[entity].complete)
		throw std::logic_error("class template completion remained incomplete");
	class_template_specialization_states_[binding] = 2;
	ApplyClassTemplateMemberDefinitions(index, binding, arguments);
	QueueClassTemplateMemberDefinitions(index, binding);
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

bool SemanticAnalyzer::MaterializeTemplatePartialArguments(
	const std::vector<TemplateParameter>& primary_parameters,
	const std::vector<TemplateParameter>& partial_parameters,
	const std::vector<NodeId>& syntax, ScopeId lexical_scope,
	std::vector<TemplateArgument>* arguments, std::uint8_t* state)
{
	if (*state == 1 || *state == 3)
	{
		++template_partial_shape_cache_hits_;
		return *state == 1;
	}
	if (*state == 2) return false;
	++template_partial_shape_materializations_;
	while (function_template_shape_parameters_.size() <
		partial_parameters.size())
	{
		std::ostringstream generated;
		generated << "__function_template_parameter_shape_"
			<< function_template_shape_parameters_.size();
		const NameId name = program_->names.Intern(generated.str());
		const EntityId entity = program_->NewEntity(name,
			NAMED_TYPENAME_PARAMETER, false, kNoType,
			program_->GlobalScope(), name);
		function_template_shape_parameters_.push_back(
			program_->types.Named(entity));
	}
	const ScopeId shape_scope = NewScope(lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(lexical_scope));
	for (std::size_t parameter = 0;
		parameter < partial_parameters.size(); ++parameter)
	{
		const TemplateParameter& record = partial_parameters[parameter];
		if (record.name == 0) continue;
		if (record.kind == TEMPLATE_ARGUMENT_TYPE)
			program_->AddBinding(shape_scope, BIND_TYPE_ALIAS, record.name,
				function_template_shape_parameters_[parameter]);
		else if (record.kind == TEMPLATE_ARGUMENT_TEMPLATE)
			CreateTemplateTemplateParameterProxy(
				shape_scope, record, parameter);
		else program_->AddBinding(shape_scope, BIND_PARAMETER, record.name,
			record.dependent_type ? program_->types.Fundamental(FUND_INT) :
				record.value_type, false, static_cast<std::int64_t>(parameter));
	}
	*state = 2;
	bool built = false;
	try
	{
		built = BuildTemplateArguments(primary_parameters, syntax, shape_scope,
			lexical_scope, arguments);
	}
	catch (...)
	{
		*state = 0;
		throw;
	}
	if (!built || arguments->empty())
	{
		arguments->clear();
		// Pattern lookup is fixed at the partial declaration's lexical point;
		// a failed typed shape is therefore a complete negative cache key.
		*state = 3;
		return false;
	}
	*state = 1;
	return true;
}

bool SemanticAnalyzer::DeduceTemplatePartialArgument(
	const TemplateArgument& pattern, const TemplateArgument& argument,
	const std::vector<TemplateParameter>& parameters,
	FunctionTemplateDeduction* deduced) const
{
	++template_partial_deduction_visits_;
	if (pattern.kind != argument.kind) return false;
	std::size_t dependent = parameters.size();
	if (pattern.kind == TEMPLATE_ARGUMENT_TYPE)
	{
		for (std::size_t i = 0;
			i < function_template_shape_parameters_.size() &&
			i < parameters.size(); ++i)
			if (pattern.type == function_template_shape_parameters_[i])
			{
				dependent = i;
				break;
			}
	}
	else if (pattern.IsDependent()) dependent = pattern.dependent_parameter;
	if (dependent < parameters.size())
	{
		if (parameters[dependent].kind != argument.kind) return false;
		if (parameters[dependent].pack)
		{
			std::size_t& position =
				deduced->pack_deduction_positions[dependent];
			if (position < deduced->pack_arguments[dependent].size())
			{
				if (deduced->pack_arguments[dependent][position] != argument)
					return false;
			}
			else deduced->pack_arguments[dependent].push_back(argument);
			++position;
			return true;
		}
		TemplateArgument& prior = deduced->fixed_arguments[dependent];
		if (prior.type != kNoType && prior != argument) return false;
		prior = argument;
		return true;
	}
	if (pattern.kind == TEMPLATE_ARGUMENT_TYPE)
		return DeduceTemplatePartialType(
			pattern.type, argument.type, parameters, deduced);
	if (pattern.value == argument.value &&
		FunctionTemplateTypeIsDependent(pattern.type))
		return DeduceTemplatePartialType(
			pattern.type, argument.type, parameters, deduced);
	return pattern == argument;
}

std::size_t SemanticAnalyzer::TemplatePartialPackParameter(TypeId type,
	const std::vector<TemplateParameter>& parameters, std::size_t depth) const
{
	if (depth > program_->types.Size()) return parameters.size();
	for (std::size_t i = 0;
		i < function_template_shape_parameters_.size() &&
		i < parameters.size(); ++i)
		if (parameters[i].pack &&
			type == function_template_shape_parameters_[i]) return i;
	const TypeRecord& record = program_->types.Get(type);
	switch (record.kind)
	{
	case TYPE_QUALIFIED:
	case TYPE_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
	case TYPE_ARRAY:
	case TYPE_MEMBER_POINTER:
		return TemplatePartialPackParameter(record.child, parameters, depth + 1);
	case TYPE_NAMED:
	{
		const EntityRecord& entity = program_->entities[record.entity];
		if (entity.template_argument_begin == kNoBinding) break;
		std::size_t found = parameters.size();
		for (std::size_t i = 0; i < entity.template_argument_count; ++i)
		{
			const TemplateArgument argument = StoredTemplateArgument(
				entity.template_argument_begin + i);
			std::size_t candidate = parameters.size();
			if (argument.kind == TEMPLATE_ARGUMENT_TYPE)
				candidate = TemplatePartialPackParameter(
					argument.type, parameters, depth + 1);
			else if (argument.IsDependent() &&
				argument.dependent_parameter < parameters.size() &&
				parameters[argument.dependent_parameter].pack)
				candidate = argument.dependent_parameter;
			if (candidate == parameters.size()) continue;
			if (found != parameters.size() && found != candidate)
				return parameters.size();
			found = candidate;
		}
		return found;
	}
	case TYPE_FUNCTION:
	case TYPE_FUNDAMENTAL:
	case TYPE_INVALID:
		break;
	}
	return parameters.size();
}

bool SemanticAnalyzer::DeduceTemplatePartialType(TypeId pattern,
	TypeId argument, const std::vector<TemplateParameter>& parameters,
	FunctionTemplateDeduction* deduced) const
{
	++template_partial_deduction_visits_;
	for (std::size_t i = 0;
		i < function_template_shape_parameters_.size() &&
		i < parameters.size(); ++i)
		if (pattern == function_template_shape_parameters_[i])
			return DeduceTemplatePartialArgument(
				TemplateArgument(TEMPLATE_ARGUMENT_TYPE, pattern),
				TemplateArgument(TEMPLATE_ARGUMENT_TYPE, argument),
				parameters, deduced);
	if (!FunctionTemplateTypeIsDependent(pattern)) return pattern == argument;
	const TypeRecord& pattern_record = program_->types.Get(pattern);
	const TypeRecord& argument_record = program_->types.Get(argument);
	if (pattern_record.kind == TYPE_QUALIFIED)
	{
		if (argument_record.kind != TYPE_QUALIFIED ||
			(argument_record.cv & pattern_record.cv) != pattern_record.cv)
			return false;
		const std::uint8_t extra_cv = static_cast<std::uint8_t>(
			argument_record.cv & ~pattern_record.cv);
		TypeId adjusted = argument_record.child;
		if (extra_cv != CV_NONE)
			adjusted = program_->types.Qualify(adjusted, extra_cv);
		return DeduceTemplatePartialType(pattern_record.child,
			adjusted, parameters, deduced);
	}
	if (pattern_record.kind != argument_record.kind) return false;
	switch (pattern_record.kind)
	{
	case TYPE_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
		return DeduceTemplatePartialType(pattern_record.child,
			argument_record.child, parameters, deduced);
	case TYPE_ARRAY:
	{
		if (pattern_record.dependent_bound_parameter != kNoTemplateParameter)
		{
			const std::size_t dependent =
				pattern_record.dependent_bound_parameter;
			if (dependent >= parameters.size() ||
				parameters[dependent].kind != TEMPLATE_ARGUMENT_INTEGRAL ||
				(argument_record.bound == 0 &&
				 argument_record.dependent_bound_parameter ==
					kNoTemplateParameter))
				return false;
			const TemplateArgument pattern_bound(TEMPLATE_ARGUMENT_INTEGRAL,
				pattern_record.dependent_bound_type, 0,
				static_cast<std::uint32_t>(dependent));
			const TemplateArgument argument_bound(TEMPLATE_ARGUMENT_INTEGRAL,
				argument_record.dependent_bound_parameter == kNoTemplateParameter ?
					pattern_record.dependent_bound_type :
					argument_record.dependent_bound_type,
				static_cast<std::int64_t>(argument_record.bound),
				argument_record.dependent_bound_parameter);
			if (!DeduceTemplatePartialArgument(pattern_bound, argument_bound,
				parameters, deduced)) return false;
		}
		else if (argument_record.dependent_bound_parameter !=
			kNoTemplateParameter ||
			pattern_record.bound != argument_record.bound) return false;
		return DeduceTemplatePartialType(pattern_record.child,
			argument_record.child, parameters, deduced);
	}
	case TYPE_FUNCTION:
	{
		const TypeId* pattern_types = program_->types.Parameters(pattern);
		const TypeId* argument_types = program_->types.Parameters(argument);
		const std::size_t pack_parameter = pattern_record.parameter_count == 0 ?
			parameters.size() : TemplatePartialPackParameter(
				pattern_types[pattern_record.parameter_count - 1], parameters);
		const bool has_pack = pack_parameter < parameters.size();
		const std::size_t fixed_parameters = pattern_record.parameter_count -
			(has_pack ? 1 : 0);
		if ((!has_pack && pattern_record.parameter_count !=
			 argument_record.parameter_count) ||
			(has_pack && argument_record.parameter_count < fixed_parameters) ||
			(!has_pack &&
			 pattern_record.variadic != argument_record.variadic) ||
			pattern_record.cv != argument_record.cv ||
			pattern_record.ref_qualifier != argument_record.ref_qualifier ||
			!DeduceTemplatePartialType(pattern_record.child,
				argument_record.child, parameters, deduced)) return false;
		for (std::size_t i = 0; i < fixed_parameters; ++i)
			if (!DeduceTemplatePartialType(pattern_types[i], argument_types[i],
				parameters, deduced)) return false;
		if (has_pack)
		{
			const std::size_t prior_size =
				deduced->pack_arguments[pack_parameter].size();
			deduced->pack_deduction_positions[pack_parameter] = 0;
			for (std::size_t i = fixed_parameters;
				i < argument_record.parameter_count; ++i)
				if (!DeduceTemplatePartialType(
					pattern_types[pattern_record.parameter_count - 1],
					argument_types[i], parameters, deduced)) return false;
			if ((prior_size != 0 &&
				 deduced->pack_arguments[pack_parameter].size() != prior_size) ||
				deduced->pack_deduction_positions[pack_parameter] !=
					deduced->pack_arguments[pack_parameter].size()) return false;
		}
		return true;
	}
	case TYPE_MEMBER_POINTER:
		return pattern_record.entity == argument_record.entity &&
			DeduceTemplatePartialType(pattern_record.child,
				argument_record.child, parameters, deduced);
	case TYPE_NAMED:
	{
		const EntityId pattern_entity = pattern_record.entity;
		const EntityId argument_entity = argument_record.entity;
		if (pattern_entity >= class_template_pattern_by_entity_.size() ||
			argument_entity >= class_template_pattern_by_entity_.size())
			return false;
		const std::uint32_t class_pattern =
			class_template_pattern_by_entity_[pattern_entity];
		const std::uint32_t argument_pattern =
			class_template_pattern_by_entity_[argument_entity];
		if (class_pattern == kNoDumpEdge || argument_pattern == kNoDumpEdge ||
			class_pattern >= class_templates_.size() ||
			argument_pattern >= class_templates_.size()) return false;
		const ClassTemplatePattern& pattern_template =
			class_templates_[class_pattern];
		if (pattern_template.template_parameter_proxy)
		{
			const std::size_t ordinal =
				pattern_template.template_parameter_ordinal;
			if (ordinal >= parameters.size() ||
				parameters[ordinal].kind != TEMPLATE_ARGUMENT_TEMPLATE)
				return false;
			const ClassTemplatePattern& argument_template =
				class_templates_[argument_pattern];
			if (!TemplateTemplateParameterMatches(
				parameters[ordinal].template_parameters,
				argument_template.parameters)) return false;
			const TypeId argument_marker = program_->entities[
				argument_template.marker_entity].type;
			if (!DeduceTemplatePartialArgument(
				TemplateArgument(TEMPLATE_ARGUMENT_TEMPLATE,
					program_->entities[pattern_template.marker_entity].type,
					0, static_cast<std::uint32_t>(ordinal)),
				TemplateArgument(TEMPLATE_ARGUMENT_TEMPLATE, argument_marker),
				parameters, deduced)) return false;
		}
		else if (class_pattern != argument_pattern) return false;
		const EntityRecord& pattern_owner = program_->entities[pattern_entity];
		const EntityRecord& argument_owner = program_->entities[argument_entity];
		if (pattern_owner.template_argument_begin == kNoBinding ||
			argument_owner.template_argument_begin == kNoBinding) return false;
		const std::vector<TemplateParameter>& class_parameters =
			pattern_template.parameters;
		std::size_t pattern_index = 0, argument_index = 0;
		while (pattern_index < pattern_owner.template_argument_count)
		{
			const TemplateArgument item = StoredTemplateArgument(
				pattern_owner.template_argument_begin + pattern_index);
			std::size_t dependent = parameters.size();
			if (item.kind == TEMPLATE_ARGUMENT_TYPE)
			{
				for (std::size_t i = 0;
					i < function_template_shape_parameters_.size() &&
					i < parameters.size(); ++i)
					if (item.type == function_template_shape_parameters_[i])
					{
						dependent = i;
						break;
					}
				if (dependent == parameters.size())
					dependent = TemplatePartialPackParameter(
						item.type, parameters);
			}
			else if (item.IsDependent()) dependent = item.dependent_parameter;
			const bool expansion = item.pack_expansion &&
				dependent < parameters.size() &&
				parameters[dependent].pack && !class_parameters.empty() &&
				TemplateParameterForArgument(
					class_parameters, pattern_index).pack;
			if (expansion)
			{
				const std::size_t remaining =
					pattern_owner.template_argument_count - pattern_index - 1;
				if (argument_index + remaining >
					argument_owner.template_argument_count)
					return false;
				const std::size_t last =
					argument_owner.template_argument_count - remaining;
				const std::size_t prior_size =
					deduced->pack_arguments[dependent].size();
				deduced->pack_deduction_positions[dependent] = 0;
				while (argument_index < last)
					if (!DeduceTemplatePartialArgument(item,
						StoredTemplateArgument(
							argument_owner.template_argument_begin +
							argument_index++), parameters,
						deduced)) return false;
				if ((prior_size != 0 &&
					 deduced->pack_arguments[dependent].size() != prior_size) ||
					deduced->pack_deduction_positions[dependent] !=
						deduced->pack_arguments[dependent].size()) return false;
				++pattern_index;
				continue;
			}
			if (argument_index >= argument_owner.template_argument_count)
				return false;
			const TemplateArgument argument_item = StoredTemplateArgument(
				argument_owner.template_argument_begin + argument_index);
			if (argument_item.pack_expansion ||
				!DeduceTemplatePartialArgument(
					item, argument_item, parameters, deduced)) return false;
			++pattern_index;
			++argument_index;
		}
		return argument_index == argument_owner.template_argument_count;
	}
	case TYPE_FUNDAMENTAL:
	case TYPE_INVALID:
	case TYPE_QUALIFIED:
		return pattern == argument;
	}
	return false;
}

bool SemanticAnalyzer::MatchTemplatePartialArguments(
	const std::vector<TemplateParameter>& parameters,
	const std::vector<TemplateArgument>& pattern_arguments,
	const std::vector<TemplateArgument>& arguments,
	FunctionTemplateDeduction* bindings) const
{
	*bindings = FunctionTemplateDeduction(parameters);
	std::size_t pattern_index = 0, argument_index = 0;
	while (pattern_index < pattern_arguments.size())
	{
		const TemplateArgument& item = pattern_arguments[pattern_index];
		std::size_t dependent = parameters.size();
		if (item.kind == TEMPLATE_ARGUMENT_TYPE)
		{
			for (std::size_t i = 0;
				i < function_template_shape_parameters_.size() &&
				i < parameters.size(); ++i)
				if (item.type == function_template_shape_parameters_[i])
				{
					dependent = i;
					break;
				}
			if (dependent == parameters.size())
				dependent = TemplatePartialPackParameter(item.type, parameters);
		}
		else if (item.IsDependent()) dependent = item.dependent_parameter;
		const bool expansion = item.pack_expansion &&
			dependent < parameters.size() &&
			parameters[dependent].pack;
		if (expansion)
		{
			const std::size_t remaining =
				pattern_arguments.size() - pattern_index - 1;
			if (argument_index + remaining > arguments.size()) return false;
			const std::size_t last = arguments.size() - remaining;
			while (argument_index < last)
				if (!DeduceTemplatePartialArgument(item,
					arguments[argument_index++], parameters, bindings))
					return false;
			++pattern_index;
			continue;
		}
		if (argument_index >= arguments.size() ||
			arguments[argument_index].pack_expansion ||
			!DeduceTemplatePartialArgument(item, arguments[argument_index],
				parameters, bindings)) return false;
		++pattern_index;
		++argument_index;
	}
	if (argument_index != arguments.size()) return false;
	for (std::size_t parameter = 0; parameter < parameters.size(); ++parameter)
		if (!parameters[parameter].pack &&
			bindings->fixed_arguments[parameter].type == kNoType)
			return false;
	return true;
}

int SemanticAnalyzer::CompareTemplatePartialPatterns(
	const std::vector<TemplateParameter>& left_parameters,
	const std::vector<TemplateArgument>& left_arguments,
	const std::vector<TemplateParameter>& right_parameters,
	const std::vector<TemplateArgument>& right_arguments) const
{
	++template_partial_order_comparisons_;
	FunctionTemplateDeduction left_from_right(left_parameters);
	FunctionTemplateDeduction right_from_left(right_parameters);
	const bool left_accepts_right = MatchTemplatePartialArguments(
		left_parameters, left_arguments, right_arguments, &left_from_right);
	const bool right_accepts_left = MatchTemplatePartialArguments(
		right_parameters, right_arguments, left_arguments, &right_from_left);
	if (left_accepts_right == right_accepts_left) return 0;
	return right_accepts_left ? 1 : -1;
}

std::size_t SemanticAnalyzer::SelectClassTemplatePartial(
	ClassTemplatePattern& pattern,
	const std::vector<TemplateArgument>& arguments,
	FunctionTemplateDeduction* selected_bindings)
{
	if (!ClassTemplateArgumentsAllowPartialSelection(*program_, arguments))
		return NoTemplatePattern();
	std::vector<std::size_t> matches;
	for (std::size_t candidate_index = 0;
		candidate_index < pattern.partial_specializations.size();
		++candidate_index)
	{
		++template_partial_candidates_;
		ClassTemplatePartialPattern& candidate =
			pattern.partial_specializations[candidate_index];
		if (!MaterializeTemplatePartialArguments(pattern.parameters,
			candidate.parameters, candidate.arguments, candidate.lexical_scope,
			&candidate.canonical_arguments,
			&candidate.canonical_argument_state))
			continue;
		FunctionTemplateDeduction bindings(candidate.parameters);
		if (!MatchTemplatePartialArguments(candidate.parameters,
			candidate.canonical_arguments, arguments, &bindings)) continue;
		matches.push_back(candidate_index);
	}
	if (matches.empty()) return NoTemplatePattern();
	std::size_t selected = matches.size();
	for (std::size_t i = 0; i < matches.size(); ++i)
	{
		bool best = true;
		for (std::size_t j = 0; j < matches.size(); ++j)
		{
			if (i == j) continue;
			const ClassTemplatePartialPattern& left =
				pattern.partial_specializations[matches[i]];
			const ClassTemplatePartialPattern& right =
				pattern.partial_specializations[matches[j]];
			if (CompareTemplatePartialPatterns(left.parameters,
				left.canonical_arguments, right.parameters,
				right.canonical_arguments) <= 0)
			{
				best = false;
				break;
			}
		}
		if (!best) continue;
		if (selected != matches.size())
			throw std::runtime_error(
				"ambiguous class template partial specialization");
		selected = i;
	}
	if (matches.size() != 1 && selected == matches.size())
		throw std::runtime_error(
			"ambiguous class template partial specialization");
	const std::size_t result = matches[
		matches.size() == 1 ? 0 : selected];
	const ClassTemplatePartialPattern& winner =
		pattern.partial_specializations[result];
	if (!MatchTemplatePartialArguments(winner.parameters,
		winner.canonical_arguments, arguments, selected_bindings))
		throw std::logic_error("selected class partial no longer matches");
	return result;
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
		if ((old >= class_template_specialization_states_.size() ||
			 class_template_specialization_states_[old] == 0) &&
			ClassTemplateArgumentsAreLayoutReady(*program_, cached_arguments))
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
		else if (parameter.kind == TEMPLATE_ARGUMENT_TEMPLATE)
		{
			if (!BuildTemplateTemplateArgument(
				source, argument_scope, parameter, &argument))
				return kNoBinding;
		}
		else
		{
			argument.type = ResolveTemplateParameterType(
				parameter, argument_scope);
			++constant_expression_required_depth_;
			ExpressionInfo expression;
			try
			{
				expression = AnalyzeExpression(
					source, argument_scope, argument.type);
			}
			catch (...)
			{
				--constant_expression_required_depth_;
				throw;
			}
			--constant_expression_required_depth_;
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
		if ((old >= class_template_specialization_states_.size() ||
			 class_template_specialization_states_[old] == 0) &&
			ClassTemplateArgumentsAreLayoutReady(*program_, arguments))
			CompleteClassTemplateSpecialization(index, old, arguments);
		if (old < class_template_specialization_states_.size() &&
			class_template_specialization_states_[old] == 2)
			ApplyClassTemplateMemberDefinitions(index, old, arguments);
		return old;
	}
	if (pattern.template_parameter_proxy)
	{
		const std::string specialization_name =
			ClassTemplateSpecializationName(
				*program_, pattern.name, arguments);
		const NameId name = program_->names.Intern(specialization_name);
		const EntityId entity = program_->NewEntity(name,
			NAMED_TYPENAME_PARAMETER, false, kNoType, pattern.owner,
			pattern.name);
		const TypeId type = program_->entities[entity].type;
		const BindingId binding = program_->AddBinding(pattern.owner,
			BIND_TYPE, name, type, false, 0, NAMED_TYPENAME_PARAMETER);
		StoreTemplateArguments(arguments,
			&program_->entities[entity].template_argument_begin,
			&program_->entities[entity].template_argument_count);
		if (class_template_pattern_by_entity_.size() <= entity)
			class_template_pattern_by_entity_.resize(
				static_cast<std::size_t>(entity) + 1, kNoDumpEdge);
		class_template_pattern_by_entity_[entity] =
			static_cast<std::uint32_t>(index);
		class_template_instantiations_.Insert(key, binding);
		if (arguments != supplied_arguments)
			class_template_instantiations_.Insert(request_key, binding);
		pattern.specialization_bindings.push_back(binding);
		PublishClassTemplateFriendGrants(pattern, entity);
		return binding;
	}

	FunctionTemplateDeduction partial_bindings(pattern.parameters);
	const std::size_t selected_partial = SelectClassTemplatePartial(
		pattern, arguments, &partial_bindings);

	const std::string specialization_name =
		ClassTemplateSpecializationName(*program_, pattern.name, arguments);
	ScopeId template_scope = BindClassTemplateArguments(pattern, arguments);
	NodeId selected_declaration = pattern.declaration;
	if (selected_partial != NoTemplatePattern())
	{
		const ClassTemplatePartialPattern& selected =
			pattern.partial_specializations[selected_partial];
		template_scope = NewScope(selected.lexical_scope,
			SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(selected.lexical_scope));
		for (std::size_t parameter = 0;
			parameter < selected.parameters.size(); ++parameter)
			if (selected.parameters[parameter].pack)
				BindTemplateArgumentPack(template_scope,
					selected.parameters[parameter],
					partial_bindings.pack_arguments[parameter], 0,
					partial_bindings.pack_arguments[parameter].size());
			else BindTemplateArgument(template_scope,
				selected.parameters[parameter],
				partial_bindings.fixed_arguments[parameter]);
		selected_declaration = selected.declaration;
	}
	const NameId specialization_lookup_name = program_->names.Intern(
		ClassTemplateSpecializationScopeName(
			index, pattern.specialization_bindings.size()));
	const TypeId shell = AnalyzeClass(selected_declaration, template_scope,
		std::string(), false, specialization_name, pattern.owner,
		pattern.name, false,
		specialization_lookup_name);
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
		!specialization.complete &&
		!ClassTemplateArgumentsAreLayoutReady(*program_, arguments);
	if (specialization.template_argument_begin == kNoBinding)
		StoreTemplateArguments(arguments,
			&specialization.template_argument_begin,
			&specialization.template_argument_count);
	const BindingId binding = program_->entities[entity].declaration;
	class_template_instantiations_.Insert(key, binding);
	if (arguments != supplied_arguments)
		class_template_instantiations_.Insert(request_key, binding);
	pattern.specialization_bindings.push_back(binding);
	PublishClassTemplateFriendGrants(pattern, entity);
	if (class_template_specialization_states_.size() <= binding)
		class_template_specialization_states_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	if (class_template_partial_selections_.size() <= binding)
		class_template_partial_selections_.resize(
			static_cast<std::size_t>(binding) + 1);
	if (selected_partial != NoTemplatePattern() && selected_partial >
		std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many class template partial patterns");
	ClassTemplatePartialSelection& selection =
		class_template_partial_selections_[binding];
	selection.pattern = selected_partial == NoTemplatePattern() ?
		kNoDumpEdge : static_cast<std::uint32_t>(selected_partial);
	selection.revision = selected_partial == NoTemplatePattern() ? 0 :
		pattern.partial_specializations[selected_partial].revision;
	if (selected_partial != NoTemplatePattern())
	{
		selection.bindings.fixed_arguments.swap(
			partial_bindings.fixed_arguments);
		selection.bindings.pack_arguments.swap(
			partial_bindings.pack_arguments);
		selection.bindings.pack_deduction_positions.swap(
			partial_bindings.pack_deduction_positions);
	}
	if (ClassTemplateArgumentsAreLayoutReady(*program_, arguments))
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
		if (ClassTemplateArgumentsAreLayoutReady(*program_, arguments))
			CompleteClassTemplateSpecialization(index, binding, arguments);
	}
}

void SemanticAnalyzer::AnalyzeExplicitInstantiation(NodeId node,
	ScopeId scope, bool definition)
{
	const NodeId target = FirstSemanticChild(node);
	if (target == kNoNode)
		throw std::runtime_error("explicit instantiation has no target");
	if (!arena_->IsTag(target, "class-forward-declaration") &&
		!arena_->IsTag(target, "class-specifier"))
	{
		if (AnalyzeExplicitFunctionInstantiation(target, scope, definition))
			return;
		throw std::runtime_error(
			"explicit instantiation target is not a supported template");
	}
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
	const bool object_output_root = (state & 1) == 0;
	state |= 2;
	const auto demand_member = [this, object_output_root](BindingId binding) {
		if (binding == kNoBinding) return;
		binding = program_->bindings[binding].canonical;
		const FunctionInfo& function = GetFunction(binding);
		if (!function.defined || function.implicit_constructor ||
			function.implicit_destructor || function.implicit_special_member ||
			function.deleted_constructor || function.deleted_destructor ||
			function.deleted_special_member)
			return;
		program_->bindings[binding].weak_odr = true;
		program_->bindings[binding].object_output_root |= object_output_root;
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
