#include "pa12_semantic_detail.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

NodeId FirstResultStructure(const SyntaxArena& arena, NodeId root)
{
	if (root == kNoNode) return kNoNode;
	std::vector<NodeId> pending(1, root);
	for (std::size_t next = 0; next < pending.size(); ++next)
	{
		const NodeId node = pending[next];
		if (arena.IsTag(node, "structured-type-name")) return node;
		for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
			edge = arena.NextEdge(edge))
			pending.push_back(arena.EdgeChild(edge));
	}
	return kNoNode;
}

bool ResultLookupFactLess(const FunctionTemplateResultLookupFact& fact,
	NodeId syntax)
{
	return fact.syntax < syntax;
}

}

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

LookupResult SemanticAnalyzer::ResolveClassDirectBase(
	NodeId base_name, ScopeId scope)
{
	LookupResult result;
	const NodeId structured = FindChild(base_name, "structured-type-name");
	if (structured != kNoNode)
		result.type = ResolveStructuredTypeName(structured, scope);
	else
	{
		const NodeId expression = FirstSemanticChild(base_name);
		if (expression != kNoNode)
			result.type = DecltypeType(expression, scope);
		else result = LookupSpelling(
			scope, arena_->Payload(base_name), LOOKUP_TYPE);
	}
	return result;
}

bool SemanticAnalyzer::HasDependentQualifiedType(NodeId node,
	const std::unordered_set<NameId>& names, ScopeId scope,
	std::size_t alias_depth)
{
	if (node == kNoNode) return false;
	if (alias_depth > alias_templates_.size()) return true;
	if (arena_->IsTag(node, "decltype-specifier") &&
		SyntaxUsesAnyTemplateParameter(node, names)) return true;
	if (arena_->IsTag(node, "decl-specifier") &&
		PayloadSource(node).compare(0, 8, "decltype") == 0 &&
		SyntaxUsesAnyTemplateParameter(node, names)) return true;
	const bool type_spelling = arena_->IsTag(node, "decl-specifier") ||
		arena_->IsTag(node, "type-name");
	const NodeId structure = type_spelling ?
		FindChild(node, "structured-type-name") : kNoNode;
	if (structure != kNoNode)
	{
		std::vector<NodeId> components;
		for (std::uint32_t edge = arena_->FirstEdge(structure);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
			if (arena_->IsTag(arena_->EdgeChild(edge), "name-component"))
				components.push_back(arena_->EdgeChild(edge));
		if ((arena_->Flags(node) & SYNTAX_FLAG_TYPENAME) != 0)
			for (std::size_t i = 0; i + 1 < components.size(); ++i)
				if (SyntaxUsesAnyTemplateParameter(components[i], names))
					return true;
		const NamePath path = StructuredNamePath(structure);
		if (!components.empty())
		{
			const NodeId arguments = FindChild(
				components.back(), "template-type-argument-list");
			if (arguments != kNoNode)
				for (std::uint32_t edge = arena_->FirstEdge(arguments);
					edge != kNoEdge; edge = arena_->NextEdge(edge))
				{
					const NodeId argument = arena_->EdgeChild(edge);
					NodeId direct = argument;
					if (arena_->IsTag(direct, "pack-expansion-expression"))
						direct = FirstSemanticChild(direct);
					if (!arena_->IsTag(argument, "type-id") &&
						SyntaxUsesAnyTemplateParameter(argument, names) &&
						!IsDirectTemplateParameterExpression(direct, names))
						return true;
				}
		}
		if (!components.empty() && IsUnqualifiedAliasTemplateName(scope, path))
		{
			const LookupResult marker = LookupSpelling(
				scope, program_->names.Get(path.Last()), LOOKUP_TYPE);
			const std::size_t alias = FindAliasTemplateIndex(marker, path.Last());
			if (alias < alias_templates_.size())
			{
				const AliasTemplatePattern& pattern = alias_templates_[alias];
				std::unordered_set<NameId> alias_names;
				for (std::size_t i = 0; i < pattern.parameters.size(); ++i)
					if (pattern.parameters[i].name != 0)
						alias_names.insert(pattern.parameters[i].name);
				if (HasDependentQualifiedType(pattern.type_id, alias_names,
					pattern.lexical_scope, alias_depth + 1)) return true;
			}
			const NodeId arguments = FindChild(
				components.back(), "template-type-argument-list");
			if (arguments != kNoNode)
				for (std::uint32_t edge = arena_->FirstEdge(arguments);
					edge != kNoEdge; edge = arena_->NextEdge(edge))
					if (HasDependentQualifiedType(
						arena_->EdgeChild(edge), names, scope,
						alias_depth)) return true;
		}
		return false;
	}
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		// A terminal alias-id is non-deduced when either its retained result or
		// one of its arguments requires dependent qualified type lookup.
		if (HasDependentQualifiedType(
			child, names, scope, alias_depth)) return true;
	}
	return false;
}

void SemanticAnalyzer::ValidateDeferredFunctionTemplateResult(NodeId node,
	ScopeId scope, FunctionTemplatePattern* pattern,
	const std::unordered_set<NameId>& dependent_names)
{
	if (node == kNoNode || scope == kNoScope || !pattern)
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
				arena_->IsTag(callee, "id-expression"))
			{
				const NameId name = arena_->SemanticPayloadId(callee);
				const NodeId structure = FindChild(
					callee, "structured-type-name");
				const bool local_dependent = structure == kNoNode &&
					dependent_names.count(name) != 0;
				const bool dependent =
					SyntaxUsesAnyTemplateParameter(arguments, dependent_names) ||
					SyntaxUsesAnyTemplateParameter(callee, dependent_names);
				bool dependent_qualifier = false;
				if (structure != kNoNode)
				{
					std::vector<NodeId> components;
					for (std::uint32_t edge = arena_->FirstEdge(structure);
						edge != kNoEdge; edge = arena_->NextEdge(edge))
						if (arena_->IsTag(
							arena_->EdgeChild(edge), "name-component"))
							components.push_back(arena_->EdgeChild(edge));
					for (std::size_t component = 0;
						component + 1 < components.size(); ++component)
						dependent_qualifier = dependent_qualifier ||
							SyntaxUsesAnyTemplateParameter(
								components[component], dependent_names);
				}
				const bool qualified = structure != kNoNode &&
					(FindChild(structure, "global-qualifier") != kNoNode ||
					 StructuredNamePath(structure).Size() > 1);
				const bool replayable =
					!local_dependent && !dependent_qualifier;
				if (replayable)
				{
					const LookupResult found = structure == kNoNode ?
						program_->LookupName(scope, name, LOOKUP_ORDINARY) :
						LookupStructuredName(callee, scope, LOOKUP_ORDINARY);
					const std::vector<std::size_t> templates =
						structure == kNoNode ?
							FindFunctionTemplates(scope, PayloadSource(callee)) :
							FindFunctionTemplates(
								scope, StructuredNamePath(callee));
					const bool has_functions = found.ordinary != kNoBinding &&
						program_->bindings[found.ordinary].kind == BIND_FUNCTION;
					if (has_functions || !templates.empty() ||
						(dependent && !qualified))
						RecordRetainedCallLookup(callee, scope,
							PayloadSource(callee), dependent && !qualified);
					if ((!dependent || qualified) &&
						found.ordinary == kNoBinding &&
						templates.empty())
					{
						const LookupResult type = structure == kNoNode ?
							program_->LookupName(scope, name, LOOKUP_TYPE) :
							LookupStructuredName(callee, scope, LOOKUP_TYPE);
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
					const bool inherited_variable_template =
						kind == LOOKUP_TYPE && found.type == kNoType &&
						template_arguments != kNoNode &&
						!FindVariableTemplates(scope, direct).empty();
					if (inherited_variable_template) break;
					if ((kind == LOOKUP_TYPE && found.type == kNoType) ||
						(kind == LOOKUP_SCOPE_CARRIER &&
						 found.type == kNoType && found.name_space == kNoScope))
						throw std::runtime_error(
							"non-dependent result type was not declared: " +
							program_->names.Get(name));
					pattern->result_lookup_facts.push_back(
						FunctionTemplateResultLookupFact(component_node, found));
					if (structure == pattern->result_root_structure && component == 0)
					{
						pattern->result_root_name = name;
						pattern->result_root_global = FindChild(
							structure, "global-qualifier") != kNoNode;
						pattern->result_root_declaration =
							found.type_declaration_canonical;
						pattern->result_root_namespace = found.name_space;
					}
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
					{
						if (!FunctionTemplateTypeIsDependent(found.type))
							EnsureClassDefinition(found.type);
						carrier = program_->ScopeForType(found.type);
					}
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
	FunctionTemplatePattern* pattern,
	const DeclaratorInfo& declarator, ScopeId shape_scope,
	const std::unordered_set<NameId>& parameter_names,
	bool defer_trailing_return)
{
	if (!pattern) throw std::logic_error("function template result has no owner");
	const NodeId result = pattern->trailing_return_syntax != kNoNode ?
		pattern->trailing_return_syntax : pattern->specifiers;
	pattern->result_root_structure = FirstResultStructure(*arena_, result);
	if (pattern->deferred_result_formation)
		ValidateDeferredFunctionTemplateResult(
			pattern->specifiers, shape_scope, pattern, parameter_names);
	if (defer_trailing_return && pattern->trailing_return_syntax != kNoNode &&
		declarator.trailing_return_scope != kNoScope)
	{
		std::unordered_set<NameId> dependent_names = parameter_names;
		for (std::size_t parameter = 0;
			parameter < declarator.parameters.size(); ++parameter)
			if (declarator.parameters[parameter].name != 0 &&
				FunctionTemplateTypeIsDependent(
					declarator.parameters[parameter].declared_type))
				dependent_names.insert(declarator.parameters[parameter].name);
		ValidateDeferredFunctionTemplateResult(pattern->trailing_return_syntax,
			declarator.trailing_return_scope, pattern, dependent_names);
	}
	std::sort(pattern->result_lookup_facts.begin(),
		pattern->result_lookup_facts.end(),
		[](const FunctionTemplateResultLookupFact& left,
			const FunctionTemplateResultLookupFact& right) {
			return left.syntax < right.syntax;
		});
	pattern->result_lookup_facts.erase(std::unique(
		pattern->result_lookup_facts.begin(), pattern->result_lookup_facts.end(),
		[](const FunctionTemplateResultLookupFact& left,
			const FunctionTemplateResultLookupFact& right) {
			return left.syntax == right.syntax;
		}), pattern->result_lookup_facts.end());
}

bool SemanticAnalyzer::FindFunctionTemplateResultLookup(NodeId syntax,
	LookupResult* result) const
{
	if (!result || !active_function_template_result_pattern_) return false;
	const std::vector<FunctionTemplateResultLookupFact>& facts =
		active_function_template_result_pattern_->result_lookup_facts;
	const std::vector<FunctionTemplateResultLookupFact>::const_iterator found =
		std::lower_bound(facts.begin(), facts.end(), syntax,
			ResultLookupFactLess);
	if (found == facts.end() || found->syntax != syntax) return false;
	result->type = found->type;
	result->type_declaration = found->declaration;
	result->type_declaration_canonical = found->declaration == kNoBinding ?
		kNoBinding : program_->bindings[found->declaration].canonical;
	result->name_space = found->name_space;
	result->naming_class = found->naming_class;
	return true;
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
