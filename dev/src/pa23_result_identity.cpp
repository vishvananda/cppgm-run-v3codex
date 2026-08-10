#include "pa12_semantic_detail.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <string>
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
	// Alias expansion borrows immutable syntax and carries only the lexical
	// scope and substitution environment that give that syntax meaning.
	NodeId node;
	ScopeId scope;
	const ResultSyntaxEnvironment* environment;

	ResultSyntaxReference(NodeId node_value = kNoNode,
		ScopeId scope_value = kNoScope,
		const ResultSyntaxEnvironment* environment_value = 0)
		: node(node_value), scope(scope_value),
		  environment(environment_value) {}
};

struct ResultSyntaxBinding
{
	NameId name;
	std::vector<ResultSyntaxReference> values;

	explicit ResultSyntaxBinding(NameId name_value = 0) : name(name_value) {}
};

struct ResultSyntaxEnvironment
{
	std::vector<ResultSyntaxBinding> bindings;
};

const std::vector<ResultSyntaxReference>* FindResultSyntaxBinding(
	const ResultSyntaxEnvironment* environment, NameId name)
{
	if (!environment || name == 0) return 0;
	for (std::size_t i = 0; i < environment->bindings.size(); ++i)
		if (environment->bindings[i].name == name)
			return &environment->bindings[i].values;
	return 0;
}

void AppendCanonicalField(std::string* output, const std::string& value)
{
	output->append(std::to_string(value.size()));
	output->push_back(':');
	output->append(value);
}

}

bool SemanticAnalyzer::EquivalentExpandedFunctionTemplateResults(
	const FunctionTemplatePattern& left,
	const FunctionTemplatePattern& right)
{
	if (left.result_root_structure == kNoNode ||
		right.result_root_structure == kNoNode) return false;
	const auto root_names_alias = [this](
		const FunctionTemplatePattern& pattern) {
		NamePath name;
		std::vector<NodeId> arguments;
		if (!CollectExplicitTemplateArguments(
			pattern.result_root_structure, &name, &arguments)) return false;
		const LookupResult marker = LookupPath(
			pattern.lexical_scope, name, LOOKUP_TYPE);
		return FindAliasTemplateIndex(marker, name.Last()) <
			alias_templates_.size();
	};
	if (!root_names_alias(left) && !root_names_alias(right)) return false;
	// Declaration insertion already scans the owner-local overload set. Keep
	// each fallback comparison bounded by the translation unit's retained graph
	// even when aliases duplicate parameters or recurse through other aliases.
	const std::size_t nodes = arena_->Nodes();
	const std::size_t visit_limit = nodes >
		(std::numeric_limits<std::size_t>::max() - 64) / 4 ?
		std::numeric_limits<std::size_t>::max() : nodes * 4 + 64;

	typedef std::function<bool(const ResultSyntaxReference&, std::string*,
		std::size_t*, std::size_t*)> RenderFunction;
	RenderFunction render;
	const FunctionTemplatePattern* active_root = 0;

	const auto root_parameter = [this](NameId name,
		const FunctionTemplatePattern& pattern) -> std::size_t {
		for (std::size_t i = 0; i < pattern.parameters.size(); ++i)
			if (pattern.parameters[i].name == name) return i;
		return pattern.parameters.size();
	};

	const auto direct_pack = [this](const ResultSyntaxReference& reference)
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
					reference.environment, direct_name);
		}
		if (!arena_->IsTag(reference.node, "pack-expansion-expression"))
			return 0;
		const NodeId operand = FirstSemanticChild(reference.node);
		const NameId name = operand == kNoNode ? 0 :
			arena_->SemanticPayloadId(operand);
		return FindResultSyntaxBinding(reference.environment, name);
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

	render = [this, &active_root, &root_parameter, &collect_arguments,
		&render, visit_limit](const ResultSyntaxReference& reference,
		std::string* output, std::size_t* visits,
		std::size_t* expansions) -> bool {
		if (++*visits > visit_limit || reference.node == kNoNode) return false;
		const NameId semantic_name =
			arena_->SemanticPayloadId(reference.node);
		const std::vector<ResultSyntaxReference>* substitution =
			FindResultSyntaxBinding(reference.environment, semantic_name);
		if (substitution)
		{
			if (substitution->size() != 1) output->append("S[");
			for (std::size_t i = 0; i < substitution->size(); ++i)
				if (!render((*substitution)[i], output, visits, expansions))
					return false;
			if (substitution->size() != 1) output->push_back(']');
			return true;
		}
		if (!active_root) return false;
		const FunctionTemplatePattern& root = *active_root;
		const std::size_t parameter = root_parameter(semantic_name, root);
		if (parameter < root.parameters.size())
		{
			output->append("P");
			output->append(std::to_string(parameter));
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
					FindResultSyntaxBinding(reference.environment, name);
				if (bound)
				{
					for (std::size_t i = 0; i < bound->size(); ++i)
						if (!render((*bound)[i], output, visits, expansions))
							return false;
					return true;
				}
				const std::size_t ordinal = root_parameter(name, root);
				if (ordinal < root.parameters.size())
				{
					output->append("P");
					output->append(std::to_string(ordinal));
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
					const AliasTemplatePattern& pattern = alias_templates_[alias];
					std::vector<ResultSyntaxReference> arguments;
					const NodeId terminal_list = FindChild(
						components.back(), "template-type-argument-list");
					collect_arguments(terminal_list, reference, &arguments);
					ResultSyntaxEnvironment environment;
					environment.bindings.reserve(pattern.parameters.size());
					std::size_t argument = 0;
					for (std::size_t p = 0; p < pattern.parameters.size(); ++p)
					{
						environment.bindings.push_back(
							ResultSyntaxBinding(pattern.parameters[p].name));
						ResultSyntaxBinding& binding = environment.bindings.back();
						if (pattern.parameters[p].pack)
						{
							binding.values.insert(binding.values.end(),
								arguments.begin() + argument, arguments.end());
							argument = arguments.size();
						}
						else if (argument < arguments.size())
							binding.values.push_back(arguments[argument++]);
						else if (pattern.parameters[p].default_argument != kNoNode)
							binding.values.push_back(ResultSyntaxReference(
								pattern.parameters[p].default_argument,
								pattern.lexical_scope, &environment));
						else return false;
					}
					if (argument != arguments.size()) return false;
					++*expansions;
					return render(ResultSyntaxReference(pattern.type_id,
						pattern.lexical_scope, &environment), output,
						visits, expansions);
				}
			}

			// Bind canonical declaration identity as well as spelling. Equal alias
			// expansions from different lexical owners must remain distinct.
			output->append("Q{");
			if (FindChild(reference.node, "global-qualifier") != kNoNode)
				output->append("G;");
			for (std::size_t c = 0; c < components.size(); ++c)
			{
				const NameId name = arena_->SemanticPayloadId(components[c]);
				const LookupResult marker = LookupSpelling(reference.scope,
					program_->names.Get(name), LOOKUP_TYPE);
				output->append("C");
				AppendCanonicalField(output, program_->names.Get(name));
				if (marker.type_declaration != kNoBinding)
				{
					output->append("D");
					output->append(std::to_string(program_->bindings[
						marker.type_declaration].canonical));
				}
				else if (marker.type != kNoType &&
					EntityOf(marker.type) != kNoEntity)
				{
					output->append("E");
					output->append(std::to_string(EntityOf(marker.type)));
				}
				const NodeId list = FindChild(
					components[c], "template-type-argument-list");
				if (list == kNoNode) continue;
				std::vector<ResultSyntaxReference> arguments;
				collect_arguments(list, reference, &arguments);
				const std::size_t class_index =
					FindClassTemplateIndex(marker, name);
				ResultSyntaxEnvironment defaults;
				if (class_index < class_templates_.size())
				{
					const ClassTemplatePattern& pattern =
						class_templates_[class_index];
					defaults.bindings.reserve(pattern.parameters.size());
					std::size_t argument = 0;
					for (std::size_t p = 0; p < pattern.parameters.size(); ++p)
					{
						defaults.bindings.push_back(
							ResultSyntaxBinding(pattern.parameters[p].name));
						ResultSyntaxBinding& binding = defaults.bindings.back();
						if (pattern.parameters[p].pack)
						{
							binding.values.insert(binding.values.end(),
								arguments.begin() + argument, arguments.end());
							argument = arguments.size();
						}
						else if (argument < arguments.size())
							binding.values.push_back(arguments[argument++]);
						else if (pattern.parameters[p].default_argument != kNoNode)
						{
							ResultSyntaxReference value(
								pattern.parameters[p].default_argument,
								pattern.lexical_scope, &defaults);
							binding.values.push_back(value);
							arguments.push_back(value);
						}
					}
				}
				output->push_back('[');
				for (std::size_t a = 0; a < arguments.size(); ++a)
					if (!render(arguments[a], output, visits, expansions))
						return false;
				output->push_back(']');
			}
			output->push_back('}');
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
			output->push_back('(');
			AppendCanonicalField(output, arena_->Tag(reference.node));
			const bool structured = arena_->HasDirectChildTag(
				reference.node, "structured-type-name");
			const std::string payload = structured ? std::string() :
				semantic_name == 0 ? arena_->Payload(reference.node) :
				program_->names.Get(semantic_name);
			AppendCanonicalField(output, payload);
		}
		for (std::uint32_t edge = arena_->FirstEdge(reference.node);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
			if (!render(ResultSyntaxReference(arena_->EdgeChild(edge),
				reference.scope, reference.environment), output,
				visits, expansions)) return false;
		if (!transparent) output->push_back(')');
		return true;
	};

	std::string left_key, right_key;
	std::size_t left_visits = 0, right_visits = 0;
	std::size_t left_expansions = 0, right_expansions = 0;
	active_root = &left;
	const bool left_formed = render(ResultSyntaxReference(
		left.result_root_structure, left.lexical_scope),
		&left_key, &left_visits, &left_expansions);
	active_root = &right;
	const bool right_formed = render(ResultSyntaxReference(
		right.result_root_structure, right.lexical_scope),
		&right_key, &right_visits, &right_expansions);
	if (!left_formed || !right_formed) return false;
	return left_expansions + right_expansions != 0 && left_key == right_key;
}

}
}
