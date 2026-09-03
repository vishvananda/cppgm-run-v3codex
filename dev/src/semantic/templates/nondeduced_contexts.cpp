#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <vector>

namespace cppgm
{
namespace semantic
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
		if (arena.IsTag(node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME)) return node;
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

bool Analyzer::SyntaxUsesAnyTemplateParameter(NodeId node,
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

bool Analyzer::SyntaxUsesUnqualifiedValueName(NodeId node,
	const std::unordered_set<NameId>& names) const
{
	if (node == kNoNode) return false;
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_ID_EXPRESSION) &&
		FindChild(node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME) == kNoNode &&
		PayloadSource(node).find("::") == std::string::npos &&
		names.count(arena_->SemanticPayloadId(node)) != 0) return true;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (SyntaxUsesUnqualifiedValueName(
			arena_->EdgeChild(edge), names)) return true;
	return false;
}

bool Analyzer::FunctionTemplateResultUsesDependentParameter(
	NodeId declarator, NodeId result,
	const std::unordered_set<NameId>& template_names)
{
	if (declarator == kNoNode || result == kNoNode) return false;
	const NodeId clause = FindChild(declarator, ::cppgm::syntax::STAG_PARAMETER_CLAUSE);
	if (clause == kNoNode) return false;
	std::unordered_set<NameId> dependent_parameters;
	for (std::uint32_t edge = arena_->FirstEdge(clause); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId parameter = arena_->EdgeChild(edge);
		if (!arena_->IsTag(parameter, ::cppgm::syntax::STAG_PARAMETER_DECLARATION) ||
			!SyntaxUsesAnyTemplateParameter(parameter, template_names)) continue;
		const NodeId parameter_declarator = FindChild(parameter, ::cppgm::syntax::STAG_DECLARATOR);
		if (parameter_declarator == kNoNode) continue;
		const NameId name = DeclaratorName(parameter_declarator);
		if (name != 0) dependent_parameters.insert(name);
	}
	return !dependent_parameters.empty() &&
		SyntaxUsesUnqualifiedValueName(result, dependent_parameters);
}

bool Analyzer::IsDirectTemplateParameterExpression(NodeId node,
	const std::unordered_set<NameId>& names) const
{
	while (node != kNoNode &&
		arena_->IsTag(node, ::cppgm::syntax::STAG_PARENTHESIZED_EXPRESSION))
	{
		const std::uint32_t edge = arena_->FirstEdge(node);
		if (edge == kNoEdge || arena_->NextEdge(edge) != kNoEdge) return false;
		node = arena_->EdgeChild(edge);
	}
	return node != kNoNode && arena_->IsTag(node, ::cppgm::syntax::STAG_ID_EXPRESSION) &&
		names.count(arena_->SemanticPayloadId(node)) != 0;
}

LookupResult Analyzer::ResolveClassDirectBase(
	NodeId base_name, ScopeId scope)
{
	LookupResult result;
	const NodeId structured = FindChild(base_name, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
	if (structured != kNoNode)
		result = LookupStructuredName(
			structured, scope, LOOKUP_TYPE, 0, true, true);
	else
	{
		const NodeId expression = FirstSemanticChild(base_name);
		if (expression != kNoNode)
			result.type = DecltypeType(expression, scope);
		else result = LookupSyntaxName(base_name, scope, LOOKUP_TYPE);
	}
	return result;
}

bool Analyzer::RequireCompleteClassDirectBase(
	const LookupResult& base_lookup, EntityId* base)
{
	*base = EntityOf(base_lookup.type);
	if (*base == kNoEntity || !program_->entities[*base].complete ||
		IsInitializerListType(base_lookup.type))
	{
		EnsureClassDefinition(base_lookup.type);
		*base = EntityOf(base_lookup.type);
	}
	if (*base == kNoEntity || !program_->entities[*base].complete ||
		program_->entities[*base].flavor == NAMED_UNION ||
		program_->entities[*base].final_class)
	{
		if (*base != kNoEntity &&
			(FunctionTemplateTypeIsDependent(base_lookup.type) ||
			 (program_->entities[*base].flavor == NAMED_TYPENAME_PARAMETER &&
			  program_->entities[*base].deferred_template_completion)))
			return false;
		ThrowSemanticError(
			"direct base must name a complete non-union class");
	}
	if (base_lookup.type_declaration != kNoBinding &&
		!CanAccessMember(base_lookup.type_declaration,
			base_lookup.naming_class))
		ThrowSemanticError("inaccessible direct base type");
	return true;
}

void Analyzer::ValidateRetainedClassDirectBase(NodeId name, ScopeId scope)
{
	const LookupResult base_lookup = ResolveClassDirectBase(name, scope);
	if (base_lookup.type == kNoType)
		ThrowSemanticError("direct base type was not found");
	EntityId base = kNoEntity;
	RequireCompleteClassDirectBase(base_lookup, &base);
}

EntityId Analyzer::RetainedClassTemplateAccessPrincipal(
	NodeId target, ScopeId lexical_scope)
{
	if (!arena_->IsTag(target, ::cppgm::syntax::STAG_CLASS_SPECIFIER) &&
		!arena_->IsTag(
			target, ::cppgm::syntax::STAG_CLASS_FORWARD_DECLARATION))
		return current_class_template_access_principal_;
	NamePath path;
	const NodeId structure = FindChild(
		target, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
	if (structure != kNoNode) path = StructuredNamePath(structure);
	else if (!arena_->Payload(target).empty())
		path.Push(program_->names.Intern(arena_->Payload(target)));
	const std::size_t pattern = path.Empty() ? class_templates_.size() :
		FindClassTemplate(lexical_scope, path);
	return pattern < class_templates_.size() ?
		class_templates_[pattern].marker_entity :
		current_class_template_access_principal_;
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

bool Analyzer::HasDependentQualifiedType(NodeId node,
	const std::unordered_set<NameId>& names, ScopeId scope,
	std::size_t alias_depth)
{
	if (node == kNoNode) return false;
	if (alias_depth > alias_templates_.size()) return true;
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_DECLTYPE_SPECIFIER) &&
		SyntaxUsesAnyTemplateParameter(node, names)) return true;
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_DECL_SPECIFIER) &&
		PayloadSource(node).compare(0, 8, "decltype") == 0 &&
		SyntaxUsesAnyTemplateParameter(node, names)) return true;
	const bool type_spelling = arena_->IsTag(node, ::cppgm::syntax::STAG_DECL_SPECIFIER) ||
		arena_->IsTag(node, ::cppgm::syntax::STAG_TYPE_NAME);
	const NodeId structure = type_spelling ?
		FindChild(node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME) : kNoNode;
	if (structure != kNoNode)
	{
		std::vector<NodeId> components;
		for (std::uint32_t edge = arena_->FirstEdge(structure);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
			if (arena_->IsTag(arena_->EdgeChild(edge), ::cppgm::syntax::STAG_NAME_COMPONENT))
				components.push_back(arena_->EdgeChild(edge));
		if ((arena_->Flags(node) & SYNTAX_FLAG_TYPENAME) != 0)
			for (std::size_t i = 0; i + 1 < components.size(); ++i)
				if (SyntaxUsesAnyTemplateParameter(components[i], names))
					return true;
		const NamePath path = StructuredNamePath(structure);
		for (std::size_t component = 0;
			component < components.size(); ++component)
		{
			const NodeId arguments = FindChild(
				components[component], ::cppgm::syntax::STAG_TEMPLATE_TYPE_ARGUMENT_LIST);
			if (arguments != kNoNode)
				for (std::uint32_t edge = arena_->FirstEdge(arguments);
					edge != kNoEdge; edge = arena_->NextEdge(edge))
				{
					const NodeId argument = arena_->EdgeChild(edge);
					NodeId direct = argument;
					if (arena_->IsTag(direct, ::cppgm::syntax::STAG_PACK_EXPANSION_EXPRESSION))
						direct = FirstSemanticChild(direct);
					if (!arena_->IsTag(argument, ::cppgm::syntax::STAG_TYPE_ID) &&
						SyntaxUsesAnyTemplateParameter(argument, names) &&
						!IsDirectTemplateParameterExpression(direct, names))
						return true;
					if (HasDependentQualifiedType(
						argument, names, scope, alias_depth)) return true;
				}
		}
		if (!components.empty() && IsUnqualifiedAliasTemplateName(scope, path))
		{
			const LookupResult marker =
				LookupName(scope, path.Last(), LOOKUP_TYPE);
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

void Analyzer::ValidateDeferredFunctionTemplateResult(NodeId node,
	ScopeId scope, FunctionTemplatePattern* pattern,
	const std::unordered_set<NameId>& dependent_names)
{
	if (node == kNoNode || scope == kNoScope || !pattern)
		ThrowInternalCompilerError("deferred function result has no lookup context");
	std::vector<NodeId> pending(1, node);
	while (!pending.empty())
	{
		const NodeId current = pending.back();
		pending.pop_back();
		if (arena_->IsTag(current, ::cppgm::syntax::STAG_CALL_EXPRESSION))
		{
			const NodeId callee = FirstSemanticChild(current);
			const NodeId arguments = FindChild(current, ::cppgm::syntax::STAG_ARGUMENT_LIST);
			if (arguments != kNoNode && callee != kNoNode &&
				arena_->IsTag(callee, ::cppgm::syntax::STAG_ID_EXPRESSION))
			{
				const NameId name = arena_->SemanticPayloadId(callee);
				const NodeId structure = FindChild(
					callee, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
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
					(FindChild(structure, ::cppgm::syntax::STAG_GLOBAL_QUALIFIER) != kNoNode ||
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
							FindFunctionTemplates(
								scope, SyntaxNamePath(callee)) :
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
							ThrowSemanticError(
								"non-dependent result call was not declared");
					}
				}
			}
		}
		if (arena_->IsTag(current, ::cppgm::syntax::STAG_TYPE_NAME) ||
			arena_->IsTag(current, ::cppgm::syntax::STAG_DECL_SPECIFIER))
		{
			const NodeId structure = FindChild(
				current, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
			if (structure != kNoNode)
			{
				std::vector<NodeId> components;
				for (std::uint32_t edge = arena_->FirstEdge(structure);
					edge != kNoEdge; edge = arena_->NextEdge(edge))
					if (arena_->IsTag(
						arena_->EdgeChild(edge), "name-component"))
						components.push_back(arena_->EdgeChild(edge));
				if (components.empty())
					ThrowInternalCompilerError(
						"structured deferred result name is empty");
				ScopeId carrier = FindChild(
					structure, ::cppgm::syntax::STAG_GLOBAL_QUALIFIER) == kNoNode ?
						kNoScope : program_->GlobalScope();
				for (std::size_t component = 0;
					component < components.size(); ++component)
				{
					const NodeId component_node = components[component];
					const NameId name = arena_->SemanticPayloadId(component_node);
					if (dependent_names.count(name) != 0) break;
					const NodeId template_arguments = FindChild(
						component_node, ::cppgm::syntax::STAG_TEMPLATE_TYPE_ARGUMENT_LIST);
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
					const bool inherited_function_template =
						kind == LOOKUP_TYPE && found.type == kNoType &&
						template_arguments != kNoNode &&
						!FindFunctionTemplates(scope, direct).empty();
					if (inherited_variable_template ||
						inherited_function_template) break;
					if ((kind == LOOKUP_TYPE && found.type == kNoType) ||
						(kind == LOOKUP_SCOPE_CARRIER &&
						 found.type == kNoType && found.name_space == kNoScope))
						ThrowSemanticError(
							"non-dependent result type was not declared: " +
							program_->names.Get(name));
					pattern->result_lookup_facts.push_back(
						FunctionTemplateResultLookupFact(component_node, found));
					if (structure == pattern->result_root_structure && component == 0)
					{
						pattern->result_root_name = name;
						pattern->result_root_global = FindChild(
							structure, ::cppgm::syntax::STAG_GLOBAL_QUALIFIER) != kNoNode;
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
							ThrowSemanticError(
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
						ThrowSemanticError(
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

void Analyzer::ValidateFunctionTemplatePatternResults(
	FunctionTemplatePattern* pattern,
	const DeclaratorInfo& declarator, ScopeId shape_scope,
	const std::unordered_set<NameId>& parameter_names,
	bool defer_trailing_return)
{
	if (!pattern) ThrowInternalCompilerError("function template result has no owner");
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
	InternExpandedFunctionTemplateResult(pattern);
	pattern->shape_type = declarator.type;
	PublishFunctionTemplateResultAbiType(pattern, declarator);
}

bool Analyzer::FindFunctionTemplateResultLookup(NodeId syntax,
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

TypeId Analyzer::FunctionTemplateNondeducedTypeShape()
{
	if (function_template_nondeduced_type_shape_ == kNoType)
	{
		const std::string spelling =
			"__function_template_nondeduced_type_shape";
		if (stats_)
			RecordGeneratedIdentityRender(
				SEMANTIC_GENERATED_FUNCTION_TEMPLATE_NONDEDUCED_SHAPE,
				spelling, 1);
		const NameId name = program_->names.Intern(spelling);
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
