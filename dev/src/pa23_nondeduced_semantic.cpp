#include "pa12_semantic_detail.h"

#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::SyntaxUsesAnyTemplateParameter(NodeId node,
	const std::unordered_set<NameId>& names) const
{
	if (node == kNoNode) return false;
	if (names.count(arena_->SemanticPayloadId(node)) != 0) return true;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (SyntaxUsesAnyTemplateParameter(
			arena_->EdgeChild(edge), names)) return true;
	return false;
}

bool SemanticAnalyzer::IsDirectTemplateParameterExpression(NodeId node,
	const std::unordered_set<NameId>& names) const
{
	while (node != kNoNode &&
		arena_->IsTag(node, "parenthesized-expression"))
	{
		const std::uint32_t edge = arena_->FirstEdge(node);
		if (edge == kNoEdge || arena_->NextEdge(edge) != kNoEdge) return false;
		node = arena_->EdgeChild(edge);
	}
	return node != kNoNode && arena_->IsTag(node, "id-expression") &&
		names.count(arena_->SemanticPayloadId(node)) != 0;
}

bool SemanticAnalyzer::HasDependentQualifiedType(NodeId node,
	const std::unordered_set<NameId>& names) const
{
	if (node == kNoNode) return false;
	if (arena_->IsTag(node, "decltype-specifier") &&
		SyntaxUsesAnyTemplateParameter(node, names)) return true;
	if (arena_->IsTag(node, "decl-specifier") &&
		arena_->HasDirectChildTag(node, "structured-type-name") &&
		(arena_->Flags(node) & SYNTAX_FLAG_TYPENAME) == 0) return false;
	if (arena_->IsTag(node, "structured-type-name"))
	{
		std::vector<NodeId> components;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
			if (arena_->IsTag(arena_->EdgeChild(edge), "name-component"))
				components.push_back(arena_->EdgeChild(edge));
		for (std::size_t i = 0; i + 1 < components.size(); ++i)
			if (SyntaxUsesAnyTemplateParameter(components[i], names)) return true;
		return false;
	}
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (HasDependentQualifiedType(arena_->EdgeChild(edge), names)) return true;
	return false;
}

void SemanticAnalyzer::ValidateDeferredFunctionTemplateResult(NodeId node,
	ScopeId scope, const std::unordered_set<NameId>& dependent_names)
{
	if (node == kNoNode || scope == kNoScope)
		throw std::logic_error("deferred function result has no lookup context");
	std::vector<NodeId> pending(1, node);
	while (!pending.empty())
	{
		const NodeId current = pending.back();
		pending.pop_back();
		if (arena_->IsTag(current, "call-expression"))
		{
			const NodeId callee = FirstSemanticChild(current);
			const NodeId arguments = FindChild(current, "argument-list");
			if (arguments != kNoNode && callee != kNoNode &&
				arena_->IsTag(callee, "id-expression") &&
				FindChild(callee, "structured-type-name") == kNoNode &&
				!SyntaxUsesAnyTemplateParameter(arguments, dependent_names))
			{
				const NameId name = arena_->SemanticPayloadId(callee);
				if (name != 0 && dependent_names.count(name) == 0)
				{
					const LookupResult found = LookupSpelling(
						scope, PayloadSource(callee), LOOKUP_ORDINARY);
					if (found.ordinary == kNoBinding &&
						!found.HasFunctionTemplateLookup())
					{
						const LookupResult type = LookupSpelling(
							scope, PayloadSource(callee), LOOKUP_TYPE);
						if (type.type == kNoType)
							throw std::runtime_error(
								"non-dependent result call was not declared");
					}
				}
			}
		}
		if (arena_->IsTag(current, "type-name") ||
			arena_->IsTag(current, "decl-specifier"))
		{
			const NodeId structure = FindChild(
				current, "structured-type-name");
			if (structure != kNoNode)
			{
				std::vector<NodeId> components;
				for (std::uint32_t edge = arena_->FirstEdge(structure);
					edge != kNoEdge; edge = arena_->NextEdge(edge))
					if (arena_->IsTag(
						arena_->EdgeChild(edge), "name-component"))
						components.push_back(arena_->EdgeChild(edge));
				if (components.empty())
					throw std::logic_error(
						"structured deferred result name is empty");
				ScopeId carrier = FindChild(
					structure, "global-qualifier") == kNoNode ?
						kNoScope : program_->GlobalScope();
				for (std::size_t component = 0;
					component < components.size(); ++component)
				{
					const NodeId component_node = components[component];
					const NameId name = arena_->SemanticPayloadId(component_node);
					if (dependent_names.count(name) != 0) break;
					const NodeId template_arguments = FindChild(
						component_node, "template-type-argument-list");
					const bool terminal = component + 1 == components.size();
					const LookupKind kind = terminal ||
						template_arguments != kNoNode ?
							LOOKUP_TYPE : LOOKUP_SCOPE_CARRIER;
					LookupResult found;
					NamePath direct;
					direct.Push(name);
					if (carrier == kNoScope)
						found = LookupPath(scope, direct, kind);
					else found = program_->LookupQualified(
						carrier, direct, kind);
					if ((kind == LOOKUP_TYPE && found.type == kNoType) ||
						(kind == LOOKUP_SCOPE_CARRIER &&
						 found.type == kNoType && found.name_space == kNoScope))
						throw std::runtime_error(
							"non-dependent result type was not declared");
					if (template_arguments != kNoNode)
					{
						const std::size_t no_template =
							std::numeric_limits<std::size_t>::max();
						if (FindClassTemplateIndex(found, name) ==
								no_template &&
							FindAliasTemplateIndex(found, name) ==
								no_template)
							throw std::runtime_error(
								"non-dependent result template was not declared");
						if (SyntaxUsesAnyTemplateParameter(
							template_arguments, dependent_names)) break;
					}
					if (terminal) break;
					if (found.type != kNoType)
						carrier = program_->ScopeForType(found.type);
					else carrier = found.name_space;
					if (carrier == kNoScope)
					{
						if (found.type != kNoType)
						{
							const TypeRecord& type = program_->types.Get(
								program_->types.RemoveTopCv(found.type));
							if (FunctionTemplateTypeIsDependent(found.type) ||
								(type.kind == TYPE_NAMED &&
								 (program_->entities[type.entity].flavor ==
									NAMED_TYPENAME_PARAMETER ||
								  program_->entities[type.entity].flavor ==
									NAMED_TEMPLATE_PARAMETER))) break;
						}
						throw std::runtime_error(
							"non-dependent result qualifier has no scope");
					}
				}
			}
		}
		for (std::uint32_t edge = arena_->FirstEdge(current);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
			pending.push_back(arena_->EdgeChild(edge));
	}
}

void SemanticAnalyzer::ValidateFunctionTemplatePatternResults(
	const FunctionTemplatePattern& pattern,
	const DeclaratorInfo& declarator, ScopeId shape_scope,
	const std::unordered_set<NameId>& parameter_names,
	bool defer_trailing_return)
{
	if (pattern.deferred_result_formation)
		ValidateDeferredFunctionTemplateResult(
			pattern.specifiers, shape_scope, parameter_names);
	if (!defer_trailing_return || pattern.trailing_return_syntax == kNoNode ||
		declarator.trailing_return_scope == kNoScope) return;
	std::unordered_set<NameId> dependent_names = parameter_names;
	for (std::size_t parameter = 0;
		parameter < declarator.parameters.size(); ++parameter)
		if (declarator.parameters[parameter].name != 0 &&
			FunctionTemplateTypeIsDependent(
				declarator.parameters[parameter].declared_type))
			dependent_names.insert(declarator.parameters[parameter].name);
	ValidateDeferredFunctionTemplateResult(pattern.trailing_return_syntax,
		declarator.trailing_return_scope, dependent_names);
}

TypeId SemanticAnalyzer::FunctionTemplateNondeducedTypeShape()
{
	if (function_template_nondeduced_type_shape_ == kNoType)
	{
		const NameId name = program_->names.Intern(
			"__function_template_nondeduced_type_shape");
		const EntityId entity = program_->NewEntity(name,
			NAMED_TYPENAME_PARAMETER, false, kNoType,
			program_->GlobalScope(), name);
		function_template_nondeduced_type_shape_ =
			program_->types.Named(entity);
	}
	return function_template_nondeduced_type_shape_;
}

}
}
