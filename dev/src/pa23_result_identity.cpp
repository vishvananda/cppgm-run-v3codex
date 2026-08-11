#include "pa12_semantic_detail.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{
namespace
{

struct ResultSyntaxEnvironment;

struct ResultSyntaxReference
{
	// Alias expansion borrows immutable syntax. Environments are immutable,
	// parent-linked overlays whose lifetime is one identity-formation request.
	NodeId node;
	ScopeId scope;
	const ResultSyntaxEnvironment* environment;

	ResultSyntaxReference(NodeId node_value = kNoNode,
		ScopeId scope_value = kNoScope,
		const ResultSyntaxEnvironment* environment_value = 0)
		: node(node_value), scope(scope_value),
		  environment(environment_value) {}
};

struct ResultSyntaxEnvironment
{
	const ResultSyntaxEnvironment* parent;
	NameId name;
	std::vector<ResultSyntaxReference> values;

	ResultSyntaxEnvironment(const ResultSyntaxEnvironment* parent_value,
		NameId name_value)
		: parent(parent_value), name(name_value) {}
};

const std::vector<ResultSyntaxReference>* FindResultSyntaxBinding(
	const ResultSyntaxEnvironment* environment, NameId name,
	const std::vector<NameId>& indexed_names, std::size_t* probes)
{
	if (!environment || name == 0) return 0;
	std::size_t first = 0, last = indexed_names.size();
	while (first != last)
	{
		if (probes) ++*probes;
		const std::size_t middle = first + (last - first) / 2;
		if (indexed_names[middle] < name) first = middle + 1;
		else last = middle;
	}
	if (first == indexed_names.size() || indexed_names[first] != name)
		return 0;
	for (const ResultSyntaxEnvironment* frame = environment;
		frame; frame = frame->parent)
	{
		if (probes) ++*probes;
		if (frame->name == name) return &frame->values;
	}
	return 0;
}

void IndexResultSyntaxBinding(std::vector<NameId>* names, NameId name)
{
	if (name == 0) return;
	const std::vector<NameId>::iterator position = std::lower_bound(
		names->begin(), names->end(), name);
	if (position == names->end() || *position != name)
		names->insert(position, name);
}

enum ResultIdentityAtomKind
{
	RESULT_IDENTITY_NODE_BEGIN = 1,
	RESULT_IDENTITY_NODE_PAYLOAD,
	RESULT_IDENTITY_NODE_END,
	RESULT_IDENTITY_PARAMETER,
	RESULT_IDENTITY_SUBSTITUTION_BEGIN,
	RESULT_IDENTITY_SUBSTITUTION_END,
	RESULT_IDENTITY_QUALIFIED_BEGIN,
	RESULT_IDENTITY_QUALIFIED_END,
	RESULT_IDENTITY_COMPONENT,
	RESULT_IDENTITY_DECLARATION,
	RESULT_IDENTITY_ENTITY,
	RESULT_IDENTITY_ARGUMENTS_BEGIN,
	RESULT_IDENTITY_ARGUMENTS_END
};

std::uint64_t ResultIdentityAtom(ResultIdentityAtomKind kind,
	std::uint64_t value = 0)
{
	return (static_cast<std::uint64_t>(kind) << 56) |
		(value & 0x00ffffffffffffffULL);
}

}

void SemanticAnalyzer::InternExpandedFunctionTemplateResult(
	FunctionTemplatePattern* pattern)
{
	if (!pattern || pattern->result_root_structure == kNoNode) return;
	const std::size_t nodes = arena_->Nodes();
	const std::size_t visit_limit = nodes >
		(std::numeric_limits<std::size_t>::max() - 64) / 4 ?
		std::numeric_limits<std::size_t>::max() : nodes * 4 + 64;
	std::size_t environment_probes = 0;
	std::vector<NameId> environment_names;
	std::vector<std::pair<NameId, std::size_t> > root_parameters;
	for (std::size_t parameter = 0;
		parameter < pattern->parameters.size(); ++parameter)
		if (pattern->parameters[parameter].name != 0)
			root_parameters.push_back(std::make_pair(
				pattern->parameters[parameter].name, parameter));
	std::sort(root_parameters.begin(), root_parameters.end());

	typedef std::function<bool(const ResultSyntaxReference&,
		std::vector<std::uint64_t>*, std::size_t*, std::size_t*)>
		BuildFunction;
	BuildFunction build;

	const auto root_parameter = [&root_parameters, pattern](NameId name)
		-> std::size_t {
		const std::vector<std::pair<NameId, std::size_t> >::const_iterator found =
			std::lower_bound(root_parameters.begin(), root_parameters.end(),
				std::make_pair(name, static_cast<std::size_t>(0)));
		return found != root_parameters.end() && found->first == name ?
			found->second : pattern->parameters.size();
	};

	const auto direct_pack = [this, &environment_names, &environment_probes](
		const ResultSyntaxReference& reference)
		-> const std::vector<ResultSyntaxReference>* {
		if (reference.node == kNoNode || !reference.environment) return 0;
		NodeId type = arena_->IsTag(reference.node, "type-id") ?
			reference.node : FindChild(reference.node, "type-id");
		if (type != kNoNode)
		{
			NodeId declarator = FindChild(type, "abstract-declarator");
			if (declarator == kNoNode)
				declarator = FindChild(type, "declarator");
			const NodeId specifiers = FindChild(type, "type-specifier-seq");
			const NodeId name = specifiers == kNoNode ? kNoNode :
				FirstSemanticChild(specifiers);
			const NamePath path = StructuredNamePath(name);
			const NameId direct_name = !path.global && path.Size() == 1 ?
				path.Last() : name == kNoNode ? 0 :
				arena_->SemanticPayloadId(name);
			if (declarator != kNoNode &&
				FindChild(declarator, "parameter-pack") != kNoNode &&
				direct_name != 0)
				return FindResultSyntaxBinding(
					reference.environment, direct_name, environment_names,
					&environment_probes);
		}
		if (!arena_->IsTag(reference.node, "pack-expansion-expression"))
			return 0;
		const NodeId operand = FirstSemanticChild(reference.node);
		const NameId name = operand == kNoNode ? 0 :
			arena_->SemanticPayloadId(operand);
		return FindResultSyntaxBinding(
			reference.environment, name, environment_names,
			&environment_probes);
	};

	const auto collect_arguments = [this, &direct_pack](NodeId list,
		const ResultSyntaxReference& owner,
		std::vector<ResultSyntaxReference>* arguments) {
		for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const ResultSyntaxReference argument(
				arena_->EdgeChild(edge), owner.scope, owner.environment);
			const std::vector<ResultSyntaxReference>* pack = direct_pack(argument);
			if (pack) arguments->insert(
				arguments->end(), pack->begin(), pack->end());
			else arguments->push_back(argument);
		}
	};

	build = [this, pattern, &root_parameter, &collect_arguments,
		&environment_names, &environment_probes, &build, visit_limit](
		const ResultSyntaxReference& reference,
		std::vector<std::uint64_t>* atoms, std::size_t* visits,
		std::size_t* expansions) -> bool {
		if (++*visits > visit_limit || reference.node == kNoNode) return false;
		const NameId semantic_name =
			arena_->SemanticPayloadId(reference.node);
		const std::vector<ResultSyntaxReference>* substitution =
			FindResultSyntaxBinding(reference.environment, semantic_name,
				environment_names, &environment_probes);
		if (substitution)
		{
			if (substitution->size() != 1)
				atoms->push_back(ResultIdentityAtom(
					RESULT_IDENTITY_SUBSTITUTION_BEGIN,
					substitution->size()));
			for (std::size_t i = 0; i < substitution->size(); ++i)
				if (!build((*substitution)[i], atoms, visits, expansions))
					return false;
			if (substitution->size() != 1)
				atoms->push_back(ResultIdentityAtom(
					RESULT_IDENTITY_SUBSTITUTION_END));
			return true;
		}
		const std::size_t parameter = root_parameter(semantic_name);
		if (parameter < pattern->parameters.size())
		{
			atoms->push_back(ResultIdentityAtom(
				RESULT_IDENTITY_PARAMETER, parameter));
			return true;
		}

		if (arena_->IsTag(reference.node, "structured-type-name"))
		{
			std::vector<NodeId> components;
			for (std::uint32_t edge = arena_->FirstEdge(reference.node);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
				if (arena_->IsTag(arena_->EdgeChild(edge), "name-component"))
					components.push_back(arena_->EdgeChild(edge));
			if (components.size() == 1 &&
				FindChild(components[0], "template-type-argument-list") == kNoNode)
			{
				const NameId name = arena_->SemanticPayloadId(components[0]);
				const std::vector<ResultSyntaxReference>* bound =
					FindResultSyntaxBinding(reference.environment, name,
						environment_names, &environment_probes);
				if (bound)
				{
					for (std::size_t i = 0; i < bound->size(); ++i)
						if (!build((*bound)[i], atoms, visits, expansions))
							return false;
					return true;
				}
				const std::size_t ordinal = root_parameter(name);
				if (ordinal < pattern->parameters.size())
				{
					atoms->push_back(ResultIdentityAtom(
						RESULT_IDENTITY_PARAMETER, ordinal));
					return true;
				}
			}

			NamePath alias_name;
			std::vector<NodeId> alias_syntax;
			if (CollectExplicitTemplateArguments(
				reference.node, &alias_name, &alias_syntax))
			{
				const LookupResult marker = LookupPath(
					reference.scope, alias_name, LOOKUP_TYPE);
				const std::size_t alias = FindAliasTemplateIndex(
					marker, alias_name.Last());
				if (alias < alias_templates_.size())
				{
					const AliasTemplatePattern& alias_pattern =
						alias_templates_[alias];
					std::vector<ResultSyntaxReference> arguments;
					const NodeId terminal_list = FindChild(
						components.back(), "template-type-argument-list");
					collect_arguments(terminal_list, reference, &arguments);
					std::vector<ResultSyntaxEnvironment> frames;
					frames.reserve(alias_pattern.parameters.size());
					const ResultSyntaxEnvironment* environment =
						reference.environment;
					std::size_t argument = 0;
					for (std::size_t p = 0;
						p < alias_pattern.parameters.size(); ++p)
					{
						frames.push_back(ResultSyntaxEnvironment(
							environment, alias_pattern.parameters[p].name));
						IndexResultSyntaxBinding(&environment_names,
							alias_pattern.parameters[p].name);
						ResultSyntaxEnvironment& frame = frames.back();
						if (alias_pattern.parameters[p].pack)
						{
							frame.values.insert(frame.values.end(),
								arguments.begin() + argument, arguments.end());
							argument = arguments.size();
						}
						else if (argument < arguments.size())
							frame.values.push_back(arguments[argument++]);
						else if (alias_pattern.parameters[p].default_argument !=
							kNoNode)
							frame.values.push_back(ResultSyntaxReference(
								alias_pattern.parameters[p].default_argument,
								alias_pattern.lexical_scope, environment));
						else return false;
						environment = &frame;
					}
					if (argument != arguments.size()) return false;
					++*expansions;
					return build(ResultSyntaxReference(alias_pattern.type_id,
						alias_pattern.lexical_scope, environment), atoms,
						visits, expansions);
				}
			}

			atoms->push_back(ResultIdentityAtom(
				RESULT_IDENTITY_QUALIFIED_BEGIN,
				FindChild(reference.node, "global-qualifier") != kNoNode));
			for (std::size_t c = 0; c < components.size(); ++c)
			{
				const NameId name = arena_->SemanticPayloadId(components[c]);
				NamePath component_path;
				component_path.Push(name);
				const LookupResult marker = LookupPath(
					reference.scope, component_path, LOOKUP_TYPE);
				atoms->push_back(ResultIdentityAtom(
					RESULT_IDENTITY_COMPONENT, name));
				if (marker.type_declaration != kNoBinding)
					atoms->push_back(ResultIdentityAtom(
						RESULT_IDENTITY_DECLARATION,
						program_->bindings[
							marker.type_declaration].canonical));
				else if (marker.type != kNoType &&
					EntityOf(marker.type) != kNoEntity)
					atoms->push_back(ResultIdentityAtom(
						RESULT_IDENTITY_ENTITY, EntityOf(marker.type)));
				const NodeId list = FindChild(
					components[c], "template-type-argument-list");
				if (list == kNoNode) continue;
				std::vector<ResultSyntaxReference> arguments;
				collect_arguments(list, reference, &arguments);
				const std::size_t class_index =
					FindClassTemplateIndex(marker, name);
				std::vector<ResultSyntaxEnvironment> defaults;
				if (class_index < class_templates_.size())
				{
					const ClassTemplatePattern& class_pattern =
						class_templates_[class_index];
					defaults.reserve(class_pattern.parameters.size());
					const ResultSyntaxEnvironment* environment =
						reference.environment;
					std::size_t argument = 0;
					for (std::size_t p = 0;
						p < class_pattern.parameters.size(); ++p)
					{
						defaults.push_back(ResultSyntaxEnvironment(
							environment, class_pattern.parameters[p].name));
						IndexResultSyntaxBinding(&environment_names,
							class_pattern.parameters[p].name);
						ResultSyntaxEnvironment& frame = defaults.back();
						if (class_pattern.parameters[p].pack)
						{
							frame.values.insert(frame.values.end(),
								arguments.begin() + argument, arguments.end());
							argument = arguments.size();
						}
						else if (argument < arguments.size())
							frame.values.push_back(arguments[argument++]);
						else if (class_pattern.parameters[p].default_argument !=
							kNoNode)
						{
							ResultSyntaxReference value(
								class_pattern.parameters[p].default_argument,
								class_pattern.lexical_scope, environment);
							frame.values.push_back(value);
							arguments.push_back(value);
						}
						environment = &frame;
					}
				}
				atoms->push_back(ResultIdentityAtom(
					RESULT_IDENTITY_ARGUMENTS_BEGIN));
				for (std::size_t a = 0; a < arguments.size(); ++a)
					if (!build(arguments[a], atoms, visits, expansions))
						return false;
				atoms->push_back(ResultIdentityAtom(
					RESULT_IDENTITY_ARGUMENTS_END));
			}
			atoms->push_back(ResultIdentityAtom(
				RESULT_IDENTITY_QUALIFIED_END));
			return true;
		}

		const bool transparent =
			arena_->IsTag(reference.node, "type-id") ||
			arena_->IsTag(reference.node, "type-specifier-seq") ||
			arena_->IsTag(reference.node, "default-template-argument") ||
			(arena_->IsTag(reference.node, "type-name") &&
			 arena_->HasDirectChildTag(
				reference.node, "structured-type-name")) ||
			(arena_->IsTag(reference.node, "decl-specifier") &&
			 arena_->HasDirectChildTag(
				reference.node, "structured-type-name"));
		if (!transparent)
		{
			atoms->push_back(ResultIdentityAtom(
				RESULT_IDENTITY_NODE_BEGIN, arena_->TagId(reference.node)));
			const bool structured = arena_->HasDirectChildTag(
				reference.node, "structured-type-name");
			const std::uint64_t payload = structured ? 0 :
				semantic_name == 0 ? arena_->PayloadId(reference.node) :
				semantic_name;
			atoms->push_back(ResultIdentityAtom(
				RESULT_IDENTITY_NODE_PAYLOAD, payload));
		}
		for (std::uint32_t edge = arena_->FirstEdge(reference.node);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
			if (!build(ResultSyntaxReference(arena_->EdgeChild(edge),
				reference.scope, reference.environment), atoms,
				visits, expansions)) return false;
		if (!transparent)
			atoms->push_back(ResultIdentityAtom(RESULT_IDENTITY_NODE_END));
		return true;
	};

	std::vector<std::uint64_t> atoms;
	std::size_t visits = 0, expansions = 0;
	if (!build(ResultSyntaxReference(pattern->result_root_structure,
		pattern->lexical_scope), &atoms, &visits, &expansions)) return;
	if (stats_)
	{
		stats_->function_template_result_identity_syntax_visits += visits;
		stats_->function_template_result_identity_environment_probes +=
			environment_probes;
		stats_->function_template_result_identity_alias_expansions += expansions;
	}
	pattern->expanded_result_identity =
		function_template_result_identities_.Intern(atoms);
	pattern->expanded_result_has_alias = expansions != 0;
}

bool SemanticAnalyzer::EquivalentExpandedFunctionTemplateResults(
	const FunctionTemplatePattern& left,
	const FunctionTemplatePattern& right)
{
	return (left.expanded_result_has_alias ||
		right.expanded_result_has_alias) &&
		left.expanded_result_identity != kNoFunctionTemplateResultIdentity &&
		left.expanded_result_identity == right.expanded_result_identity;
}

}
}
