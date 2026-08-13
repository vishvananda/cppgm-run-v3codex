#include "pa12_semantic_detail.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

bool FunctionTemplateNeedsPartitionIdentity(
	const std::vector<TemplateParameter>& parameters)
{
	std::size_t packs = 0;
	for (std::size_t i = 0; i < parameters.size(); ++i)
		if (parameters[i].pack)
		{
			++packs;
			if (i + 1 != parameters.size()) return true;
		}
	return packs > 1;
}

FunctionTemplateAbiRecipeId PublishFunctionTemplateAbiRecipe(Program* program,
	const std::vector<TemplateParameter>& parameters,
	const std::vector<TypeId>& parameter_shapes,
	const FunctionTemplatePattern& pattern)
{
	if (parameters.size() > std::numeric_limits<std::uint32_t>::max() ||
		program->function_template_parameter_shapes.size() >
			std::numeric_limits<std::uint32_t>::max() - parameters.size() ||
		program->function_template_abi_template_parameter_types.size() >
			std::numeric_limits<std::uint32_t>::max() - parameters.size() ||
		pattern.abi_function_parameter_types.size() >
			std::numeric_limits<std::uint32_t>::max() ||
		program->function_template_abi_function_parameter_types.size() >
			std::numeric_limits<std::uint32_t>::max() -
				pattern.abi_function_parameter_types.size() ||
		program->function_template_abi_recipes.size() >=
			kNoFunctionTemplateAbiRecipe)
		throw std::runtime_error("too many function template ABI recipes");
	const std::uint32_t shape_begin = static_cast<std::uint32_t>(
		program->function_template_parameter_shapes.size());
	const std::uint32_t source_type_begin = static_cast<std::uint32_t>(
		program->function_template_abi_template_parameter_types.size());
	const std::uint32_t function_source_type_begin =
		static_cast<std::uint32_t>(
			program->function_template_abi_function_parameter_types.size());
	for (std::size_t parameter = 0; parameter < parameters.size(); ++parameter)
	{
		const TypeId shape = parameter_shapes[parameter];
		program->function_template_parameter_shapes.push_back(shape);
		const TypeRecord& shape_record = program->types.Get(shape);
		if (shape_record.kind != TYPE_NAMED) continue;
		EntityRecord& entity = program->entities[shape_record.entity];
		if (entity.template_parameter_ordinal != kNoTemplateParameter &&
			entity.template_parameter_ordinal != parameter)
			throw std::logic_error(
				"function template ABI proxy ordinal diverged");
		entity.template_parameter_ordinal =
			static_cast<std::uint32_t>(parameter);
	}
	if (pattern.abi_template_parameter_types.size() != parameters.size())
		throw std::logic_error(
			"function template ABI source-type recipe is incomplete");
	program->function_template_abi_template_parameter_types.insert(
		program->function_template_abi_template_parameter_types.end(),
		pattern.abi_template_parameter_types.begin(),
		pattern.abi_template_parameter_types.end());
	if (!program->types.IsFunction(pattern.shape_type) ||
		program->types.Get(pattern.shape_type).parameter_count !=
			pattern.abi_function_parameter_types.size())
		throw std::logic_error(
			"function template ABI parameter source-type recipe is incomplete");
	program->function_template_abi_function_parameter_types.insert(
		program->function_template_abi_function_parameter_types.end(),
		pattern.abi_function_parameter_types.begin(),
		pattern.abi_function_parameter_types.end());
	const FunctionTemplateAbiRecipeId recipe =
		static_cast<FunctionTemplateAbiRecipeId>(
			program->function_template_abi_recipes.size());
	program->function_template_abi_recipes.push_back(
		FunctionTemplateAbiRecipe(pattern.shape_type, shape_begin,
			static_cast<std::uint32_t>(parameters.size()),
			HasTrailingTemplateParameterPack(parameters) &&
				!FunctionTemplateNeedsPartitionIdentity(parameters),
			pattern.function_parameter_pack));
	program->function_template_abi_recipes.back().result_type =
		pattern.abi_result_type;
	program->function_template_abi_recipes.back().template_parameter_type_begin =
		source_type_begin;
	program->function_template_abi_recipes.back().function_parameter_type_begin =
		function_source_type_begin;
	program->function_template_abi_recipes.back().function_parameter_count =
		static_cast<std::uint32_t>(
			pattern.abi_function_parameter_types.size());
	return recipe;
}

void MarkOverloadedFunctionTemplateAbiRecipes(Program* program,
	const std::deque<FunctionTemplatePattern>& patterns,
	const CompactIndexSequence& overloads)
{
	if (overloads.Size() <= 1) return;
	for (std::size_t overload = 0; overload < overloads.Size(); ++overload)
	{
		const FunctionTemplateAbiRecipeId recipe =
			patterns[overloads[overload]].abi_recipe;
		if (recipe == kNoFunctionTemplateAbiRecipe ||
			recipe >= program->function_template_abi_recipes.size())
			throw std::logic_error(
				"function template overload has no ABI recipe");
		program->function_template_abi_recipes[recipe].overloaded_pattern = true;
	}
}

bool TypeContainsDependentResultShape(const TypeTable& types, TypeId type,
	TypeId dependent_result)
{
	if (dependent_result == kNoType || type == kNoType) return false;
	if (type == dependent_result) return true;
	const TypeRecord& record = types.Get(type);
	if (record.kind == TYPE_MEMBER_POINTER)
		return TypeContainsDependentResultShape(types,
			static_cast<TypeId>(record.bound), dependent_result) ||
			TypeContainsDependentResultShape(types, record.child, dependent_result);
	if (record.kind == TYPE_QUALIFIED || record.kind == TYPE_POINTER ||
		record.kind == TYPE_BLOCK_POINTER ||
		record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE || record.kind == TYPE_ARRAY ||
		record.kind == TYPE_FUNCTION)
		return TypeContainsDependentResultShape(
			types, record.child, dependent_result);
	return false;
}

bool TypeContainsAbstractArrayElement(const Program& program, TypeId type,
	std::size_t depth)
{
	if (type == kNoType || depth > program.types.Size()) return false;
	const TypeRecord& record = program.types.Get(type);
	if (record.kind == TYPE_QUALIFIED || record.kind == TYPE_POINTER ||
		record.kind == TYPE_BLOCK_POINTER ||
		record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE || record.kind == TYPE_ARRAY ||
		record.kind == TYPE_MEMBER_POINTER)
	{
		TypeId element = record.child;
		if (record.kind == TYPE_ARRAY)
		{
			while (program.types.Get(element).kind == TYPE_QUALIFIED ||
				program.types.Get(element).kind == TYPE_ARRAY)
				element = program.types.Get(element).child;
			const TypeRecord& shape = program.types.Get(element);
			if (shape.kind == TYPE_NAMED &&
				program.entities[shape.entity].abstract_class) return true;
		}
		return TypeContainsAbstractArrayElement(
			program, record.child, depth + 1);
	}
	if (record.kind == TYPE_FUNCTION)
	{
		if (TypeContainsAbstractArrayElement(
			program, record.child, depth + 1)) return true;
		const TypeId* parameters = program.types.Parameters(type);
		for (std::size_t i = 0; i < record.parameter_count; ++i)
			if (TypeContainsAbstractArrayElement(
				program, parameters[i], depth + 1)) return true;
	}
	return false;
}

bool CaptureFunctionTemplateSpecifierFacts(const SyntaxArena& arena,
	const Program& program, NodeId specifiers, FunctionTemplatePattern* pattern)
{
	bool friend_syntax = false;
	for (std::uint32_t edge = specifiers == kNoNode ? kNoEdge :
		arena.FirstEdge(specifiers); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		const NameId semantic_name = arena.SemanticPayloadId(child);
		const std::string value = semantic_name == 0 ? arena.Payload(child) :
			program.names.Get(semantic_name);
		if (value == "friend") friend_syntax = true;
		else if (value == "constexpr") pattern->constexpr_specifier = true;
		else if (value == "explicit") pattern->explicit_specifier = true;
		else if (value == "inline") pattern->inline_specifier = true;
	}
	return friend_syntax;
}

bool ConstructorTemplateMayAcceptNoArguments(
	const FunctionTemplatePattern& pattern)
{
	if (!pattern.constructor_template || pattern.required_parameter_count != 0)
		return false;
	for (std::size_t parameter = 0;
		parameter < pattern.parameters.size(); ++parameter)
		if (!pattern.parameters[parameter].pack &&
			pattern.parameters[parameter].default_argument == kNoNode)
			return false;
	return true;
}

NodeId DirectChild(const SyntaxArena& arena, NodeId node, const char* tag)
{
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
		if (arena.IsTag(arena.EdgeChild(edge), tag))
			return arena.EdgeChild(edge);
	return kNoNode;
}

void RecordConstructorTemplateDeclaration(const SyntaxArena& arena,
	Program* program, EntityId owner_id, NodeId target, bool definition)
{
	EntityRecord& owner = program->entities[owner_id];
	owner.has_user_declared_constructor = true;
	const NodeId initializer = DirectChild(arena, target, "initializer");
	const NodeId special = initializer == kNoNode ? kNoNode :
		DirectChild(arena, initializer, "special-initializer");
	const bool explicitly_defaulted = special != kNoNode &&
		arena.Payload(special) == "default";
	const bool deleted = special != kNoNode &&
		arena.Payload(special) == "delete";
	owner.has_user_provided_constructor |=
		definition || (!explicitly_defaulted && !deleted);
	owner.is_aggregate = false;
}

void ApplyFunctionTemplateSpecifierFacts(
	const FunctionTemplatePattern& pattern, SpecInfo* spec)
{
	spec->is_constexpr = spec->is_constexpr || pattern.constexpr_specifier;
	spec->inline_specifier = spec->inline_specifier || pattern.inline_specifier;
}

void MergeFunctionTemplateSpecifierFacts(FunctionTemplatePattern* retained,
	const FunctionTemplatePattern& incoming)
{
	retained->constexpr_specifier |= incoming.constexpr_specifier;
	retained->explicit_specifier |= incoming.explicit_specifier;
	retained->inline_specifier |= incoming.inline_specifier;
	retained->deleted_function |= incoming.deleted_function;
}

std::size_t TemplateParameterOrdinal(
	const std::vector<TemplateParameter>& parameters, NameId name)
{
	if (name == 0) return parameters.size();
	for (std::size_t i = 0; i < parameters.size(); ++i)
		if (parameters[i].name == name) return i;
	return parameters.size();
}

std::uint32_t NextComparableTemplateSyntaxEdge(const SyntaxArena& arena,
	std::uint32_t edge, bool ignore_global_qualifier)
{
	while (edge != kNoEdge && ignore_global_qualifier &&
		arena.IsTag(arena.EdgeChild(edge), "global-qualifier"))
		edge = arena.NextEdge(edge);
	return edge;
}

bool EquivalentNormalizedTemplateSyntax(const SyntaxArena& arena,
	NodeId left, NodeId right,
	const std::vector<TemplateParameter>& left_parameters,
	const std::vector<TemplateParameter>& right_parameters,
	NodeId left_global_owner = kNoNode,
	NodeId right_global_owner = kNoNode,
	Program* program = 0, ScopeId left_scope = kNoScope,
	ScopeId right_scope = kNoScope)
{
	// A dependent non-type parameter type is not always safe to materialize
	// against an incomplete shape type. Compare the retained parsed structure,
	// normalizing parameters and concrete owner aliases without demanding it.
	if (left == kNoNode || right == kNoNode) return left == right;
	std::vector<std::pair<NodeId, NodeId> > pending(
		1, std::make_pair(left, right));
	while (!pending.empty())
	{
		const NodeId left_node = pending.back().first;
		const NodeId right_node = pending.back().second;
		pending.pop_back();
		if (arena.TagId(left_node) != arena.TagId(right_node)) return false;
		const NameId left_name = arena.SemanticPayloadId(left_node);
		const NameId right_name = arena.SemanticPayloadId(right_node);
		const std::size_t left_parameter = TemplateParameterOrdinal(
			left_parameters, left_name);
		const std::size_t right_parameter = TemplateParameterOrdinal(
			right_parameters, right_name);
		const bool parameter_name = left_parameter < left_parameters.size() ||
			right_parameter < right_parameters.size();
		if (parameter_name)
		{
			if (left_parameter != right_parameter) return false;
		}
		else
		{
			bool structured_wrapper = false;
			for (std::uint32_t edge = arena.FirstEdge(left_node);
				edge != kNoEdge; edge = arena.NextEdge(edge))
				if (arena.IsTag(
					arena.EdgeChild(edge), "structured-type-name"))
				{
					structured_wrapper = true;
					break;
				}
			const bool spelling_differs = left_name != right_name ||
				(left_name == 0 && right_name == 0 &&
				 arena.PayloadId(left_node) != arena.PayloadId(right_node));
			bool equivalent_owner_type = false;
			if (!structured_wrapper && spelling_differs && program &&
				left_name != 0 && right_name != 0 &&
				left_scope != kNoScope && right_scope != kNoScope)
			{
				const LookupResult left_lookup = program->LookupName(
					left_scope, left_name, LOOKUP_TYPE);
				const LookupResult right_lookup = program->LookupName(
					right_scope, right_name, LOOKUP_TYPE);
				equivalent_owner_type = left_lookup.type != kNoType &&
					left_lookup.type == right_lookup.type;
			}
			if (!structured_wrapper && spelling_differs &&
				!equivalent_owner_type) return false;
		}
		const bool ignore_global_qualifier =
			left_node == left_global_owner && right_node == right_global_owner;
		std::uint32_t left_edge = NextComparableTemplateSyntaxEdge(arena,
			arena.FirstEdge(left_node), ignore_global_qualifier);
		std::uint32_t right_edge = NextComparableTemplateSyntaxEdge(arena,
			arena.FirstEdge(right_node), ignore_global_qualifier);
		while (left_edge != kNoEdge && right_edge != kNoEdge)
		{
			pending.push_back(std::make_pair(
				arena.EdgeChild(left_edge), arena.EdgeChild(right_edge)));
			left_edge = NextComparableTemplateSyntaxEdge(
				arena, arena.NextEdge(left_edge), ignore_global_qualifier);
			right_edge = NextComparableTemplateSyntaxEdge(
				arena, arena.NextEdge(right_edge), ignore_global_qualifier);
		}
		if (left_edge != right_edge) return false;
	}
	return true;
}

bool EquivalentFunctionTemplateParameterLists(const SyntaxArena& arena,
	const std::vector<TemplateParameter>& left,
	const std::vector<TemplateParameter>& right,
	Program* program = 0, ScopeId left_scope = kNoScope,
	ScopeId right_scope = kNoScope)
{
	if (left.size() != right.size()) return false;
	for (std::size_t i = 0; i < left.size(); ++i)
	{
		if (left[i].kind != right[i].kind || left[i].pack != right[i].pack)
			return false;
		if (left[i].kind == TEMPLATE_ARGUMENT_TEMPLATE &&
			!EquivalentFunctionTemplateParameterLists(arena,
				left[i].template_parameters, right[i].template_parameters,
				program, left_scope, right_scope))
			return false;
		if (left[i].kind != TEMPLATE_ARGUMENT_INTEGRAL) continue;
		if (left[i].dependent_type != right[i].dependent_type) return false;
		if (!left[i].dependent_type)
		{
			if (left[i].value_type != right[i].value_type) return false;
			continue;
		}
		if (!EquivalentNormalizedTemplateSyntax(arena,
				left[i].specifiers, right[i].specifiers, left, right,
				kNoNode, kNoNode, program, left_scope, right_scope) ||
			!EquivalentNormalizedTemplateSyntax(arena,
				left[i].declarator, right[i].declarator, left, right,
				kNoNode, kNoNode, program, left_scope, right_scope)) return false;
	}
	return true;
}

bool EquivalentFunctionTemplateNondeducedShapes(const SyntaxArena& arena,
	const FunctionTemplatePattern& left,
	const FunctionTemplatePattern& right)
{
	if (left.function_parameter_nondeduced_syntax.size() !=
		right.function_parameter_nondeduced_syntax.size()) return false;
	for (std::size_t i = 0;
		i < left.function_parameter_nondeduced_syntax.size(); ++i)
		if (!EquivalentNormalizedTemplateSyntax(arena,
				left.function_parameter_nondeduced_syntax[i],
				right.function_parameter_nondeduced_syntax[i],
				left.parameters, right.parameters)) return false;
	return true;
}

bool IsFunctionOnlyDeclSpecifier(const SyntaxArena& arena, NodeId node)
{
	if (!arena.IsTag(node, "decl-specifier")) return false;
	const std::string& spelling = arena.Payload(node);
	return spelling == "friend" || spelling == "inline" ||
		spelling == "constexpr" || spelling == "virtual" ||
		spelling == "explicit" || spelling == "static" ||
		spelling == "extern" || spelling == "register" ||
		spelling == "thread_local" || spelling == "mutable";
}

bool EquivalentResultRootLookup(const FunctionTemplatePattern& left,
	const FunctionTemplatePattern& right,
	NodeId* left_structure, NodeId* right_structure)
{
	if (!left_structure || !right_structure) return false;
	*left_structure = kNoNode;
	*right_structure = kNoNode;
	if (left.result_root_structure == kNoNode ||
		right.result_root_structure == kNoNode ||
		left.result_root_global == right.result_root_global ||
		left.result_root_name == 0 ||
		left.result_root_name != right.result_root_name ||
		!((left.result_root_declaration != kNoBinding &&
		   left.result_root_declaration == right.result_root_declaration) ||
		  (left.result_root_namespace != kNoScope &&
		   left.result_root_namespace == right.result_root_namespace)))
		return false;
	*left_structure = left.result_root_structure;
	*right_structure = right.result_root_structure;
	return true;
}

std::uint32_t NextDependentResultEdge(
	const SyntaxArena& arena, std::uint32_t edge)
{
	while (edge != kNoEdge &&
		IsFunctionOnlyDeclSpecifier(arena, arena.EdgeChild(edge)))
		edge = arena.NextEdge(edge);
	return edge;
}

bool EquivalentDependentFunctionTemplateResults(const SyntaxArena& arena,
	const FunctionTemplatePattern& left,
	const FunctionTemplatePattern& right)
{
	NodeId left_global_owner = kNoNode;
	NodeId right_global_owner = kNoNode;
	(void)EquivalentResultRootLookup(left, right,
		&left_global_owner, &right_global_owner);
	if (left.result_root_structure != kNoNode &&
		right.result_root_structure != kNoNode)
		return EquivalentNormalizedTemplateSyntax(arena,
			left.result_root_structure, right.result_root_structure,
			left.parameters, right.parameters,
			left_global_owner, right_global_owner);
	std::uint32_t left_edge = NextDependentResultEdge(arena,
		left.specifiers == kNoNode ? kNoEdge : arena.FirstEdge(left.specifiers));
	std::uint32_t right_edge = NextDependentResultEdge(arena,
		right.specifiers == kNoNode ? kNoEdge : arena.FirstEdge(right.specifiers));
	while (left_edge != kNoEdge && right_edge != kNoEdge)
	{
		if (!EquivalentNormalizedTemplateSyntax(arena,
			arena.EdgeChild(left_edge), arena.EdgeChild(right_edge),
			left.parameters, right.parameters,
			left_global_owner, right_global_owner)) return false;
		left_edge = NextDependentResultEdge(
			arena, arena.NextEdge(left_edge));
		right_edge = NextDependentResultEdge(
			arena, arena.NextEdge(right_edge));
	}
	if (left_edge != right_edge) return false;
	return EquivalentNormalizedTemplateSyntax(arena,
		left.trailing_return_syntax, right.trailing_return_syntax,
		left.parameters, right.parameters,
		left_global_owner, right_global_owner);
}

void CaptureFunctionParameterMetadata(FunctionTemplatePattern* pattern,
	const DeclaratorInfo& declarator)
{
	pattern->function_parameter_names.reserve(declarator.parameters.size());
	pattern->function_parameter_defaults.reserve(declarator.parameters.size());
	pattern->function_parameter_nondeduced_syntax.reserve(
		declarator.parameters.size());
	pattern->function_parameter_nondeduced.reserve(
		declarator.parameters.size());
	for (std::size_t parameter = 0;
		parameter < declarator.parameters.size(); ++parameter)
	{
		pattern->function_parameter_names.push_back(
			declarator.parameters[parameter].name);
		pattern->function_parameter_defaults.push_back(
			declarator.parameters[parameter].default_argument);
		pattern->function_parameter_nondeduced_syntax.push_back(
			declarator.parameters[parameter].nondeduced_type_syntax);
		pattern->function_parameter_nondeduced.push_back(
			declarator.parameters[parameter].nondeduced ? 1 : 0);
	}
}

void InheritFunctionParameterMetadata(FunctionTemplatePattern* retained,
	FunctionTemplatePattern* incoming, bool incoming_is_definition)
{
	if (retained->function_parameter_names.size() !=
			incoming->function_parameter_names.size() ||
		retained->function_parameter_defaults.size() !=
			incoming->function_parameter_defaults.size() ||
		retained->function_parameter_nondeduced !=
			incoming->function_parameter_nondeduced)
		throw std::runtime_error("function template parameter count mismatch");
	FunctionTemplatePattern* destination = incoming_is_definition ?
		incoming : retained;
	const FunctionTemplatePattern& source = incoming_is_definition ?
		*retained : *incoming;
	for (std::size_t parameter = 0;
		parameter < destination->function_parameter_names.size(); ++parameter)
	{
		if (destination->function_parameter_names[parameter] == 0)
			destination->function_parameter_names[parameter] =
				source.function_parameter_names[parameter];
		if (destination->function_parameter_defaults[parameter] == kNoNode)
			destination->function_parameter_defaults[parameter] =
				source.function_parameter_defaults[parameter];
	}
}

void CaptureFunctionTemplateDefaultContexts(FunctionTemplatePattern* pattern)
{
	pattern->default_context_by_parameter.assign(
		pattern->parameters.size(), kNoDumpEdge);
	bool has_default = false;
	for (std::size_t i = 0; i < pattern->parameters.size(); ++i)
		has_default = has_default ||
			pattern->parameters[i].default_argument != kNoNode;
	if (!has_default) return;
	FunctionTemplateDefaultContext context;
	context.lexical_scope = pattern->lexical_scope;
	context.parameters = pattern->parameters;
	pattern->default_contexts.push_back(context);
	for (std::size_t i = 0; i < pattern->parameters.size(); ++i)
		if (pattern->parameters[i].default_argument != kNoNode)
			pattern->default_context_by_parameter[i] = 0;
}

void MergeFunctionTemplateDefaults(FunctionTemplatePattern* retained,
	const FunctionTemplatePattern& incoming, bool incoming_is_definition)
{
	if (retained->parameters.size() != incoming.parameters.size() ||
		retained->default_context_by_parameter.size() !=
			retained->parameters.size() ||
		incoming.default_context_by_parameter.size() !=
			incoming.parameters.size())
		throw std::logic_error("function template default metadata mismatch");
	for (std::size_t i = 0; i < retained->parameters.size(); ++i)
		if (retained->parameters[i].default_argument != kNoNode &&
			incoming.parameters[i].default_argument != kNoNode)
			throw std::runtime_error("duplicate default template argument");

	std::vector<std::uint32_t> remapped(
		incoming.default_contexts.size(), kNoDumpEdge);
	for (std::size_t i = 0; i < retained->parameters.size(); ++i)
	{
		if (incoming.parameters[i].default_argument == kNoNode) continue;
		const std::uint32_t source =
			incoming.default_context_by_parameter[i];
		if (source == kNoDumpEdge ||
			source >= incoming.default_contexts.size())
			throw std::logic_error(
				"function template default has no declaration context");
		if (remapped[source] == kNoDumpEdge)
		{
			if (retained->default_contexts.size() >= kNoDumpEdge)
				throw std::runtime_error(
					"too many function template default contexts");
			remapped[source] = static_cast<std::uint32_t>(
				retained->default_contexts.size());
			retained->default_contexts.push_back(
				incoming.default_contexts[source]);
		}
		retained->default_context_by_parameter[i] = remapped[source];
	}

	if (incoming_is_definition)
	{
		std::vector<TemplateParameter> merged = incoming.parameters;
		for (std::size_t i = 0; i < merged.size(); ++i)
			if (merged[i].default_argument == kNoNode)
				merged[i].default_argument =
					retained->parameters[i].default_argument;
		retained->parameters.swap(merged);
	}
	else
		for (std::size_t i = 0; i < retained->parameters.size(); ++i)
			if (retained->parameters[i].default_argument == kNoNode)
				retained->parameters[i].default_argument =
					incoming.parameters[i].default_argument;
}

}

void SemanticAnalyzer::InheritFunctionTemplateResultLookups(
	const FunctionTemplatePattern& source,
	FunctionTemplatePattern* destination)
{
	if (!destination)
		throw std::logic_error("function template result lookup has no target");
	NodeId source_global_owner = kNoNode;
	NodeId destination_global_owner = kNoNode;
	(void)EquivalentResultRootLookup(source, *destination,
		&source_global_owner, &destination_global_owner);
	const std::vector<FunctionTemplateResultLookupFact> destination_facts =
		destination->result_lookup_facts;
	bool mapping_diverged = false;
	std::vector<std::pair<NodeId, NodeId> > pending;
	std::uint32_t source_specifier = NextDependentResultEdge(*arena_,
		source.specifiers == kNoNode ? kNoEdge :
		arena_->FirstEdge(source.specifiers));
	std::uint32_t destination_specifier = NextDependentResultEdge(*arena_,
		destination->specifiers == kNoNode ? kNoEdge :
		arena_->FirstEdge(destination->specifiers));
	while (source_specifier != kNoEdge && destination_specifier != kNoEdge)
	{
		pending.push_back(std::make_pair(arena_->EdgeChild(source_specifier),
			arena_->EdgeChild(destination_specifier)));
		source_specifier = NextDependentResultEdge(
			*arena_, arena_->NextEdge(source_specifier));
		destination_specifier = NextDependentResultEdge(
			*arena_, arena_->NextEdge(destination_specifier));
	}
	if (source_specifier != destination_specifier)
		mapping_diverged = true;
	else pending.push_back(std::make_pair(source.trailing_return_syntax,
		destination->trailing_return_syntax));
	std::vector<FunctionTemplateResultLookupFact> remapped;
	remapped.reserve(source.result_lookup_facts.size());
	while (!pending.empty())
	{
		const NodeId left = pending.back().first;
		const NodeId right = pending.back().second;
		pending.pop_back();
		if (left == kNoNode || right == kNoNode)
		{
			if (left != right)
			{
				mapping_diverged = true;
				pending.clear();
			}
			continue;
		}
		CopyRetainedCallLookup(left, right);
		const std::vector<FunctionTemplateResultLookupFact>::const_iterator fact =
			std::lower_bound(source.result_lookup_facts.begin(),
				source.result_lookup_facts.end(), left,
				[](const FunctionTemplateResultLookupFact& candidate,
					NodeId syntax) { return candidate.syntax < syntax; });
		if (fact != source.result_lookup_facts.end() && fact->syntax == left)
		{
			FunctionTemplateResultLookupFact inherited = *fact;
			inherited.syntax = right;
			remapped.push_back(inherited);
		}
		const bool ignore_global_qualifier =
			left == source_global_owner && right == destination_global_owner;
		std::uint32_t left_edge = NextComparableTemplateSyntaxEdge(
			*arena_, arena_->FirstEdge(left), ignore_global_qualifier);
		std::uint32_t right_edge = NextComparableTemplateSyntaxEdge(
			*arena_, arena_->FirstEdge(right), ignore_global_qualifier);
		while (left_edge != kNoEdge && right_edge != kNoEdge)
		{
			pending.push_back(std::make_pair(
				arena_->EdgeChild(left_edge), arena_->EdgeChild(right_edge)));
			left_edge = NextComparableTemplateSyntaxEdge(*arena_,
				arena_->NextEdge(left_edge), ignore_global_qualifier);
			right_edge = NextComparableTemplateSyntaxEdge(*arena_,
				arena_->NextEdge(right_edge), ignore_global_qualifier);
		}
		if (left_edge != right_edge)
		{
			mapping_diverged = true;
			pending.clear();
		}
	}
	bool expanded_equivalent = false;
	if (mapping_diverged)
	{
		expanded_equivalent =
			EquivalentExpandedFunctionTemplateResults(source, *destination);
		// An expanded definition has lookup nodes absent from the alias-id (or
		// vice versa). Preserve those destination facts, while replacing every
		// directly corresponding node with first-declaration identity below.
		if (expanded_equivalent) remapped = destination_facts;
		else remapped.clear();
		std::vector<std::uint8_t> used(destination_facts.size(), 0);
		for (std::size_t i = 0; i < source.result_lookup_facts.size(); ++i)
		{
			const FunctionTemplateResultLookupFact& source_fact =
				source.result_lookup_facts[i];
			std::size_t match = destination_facts.size();
			for (std::size_t j = 0; j < destination_facts.size(); ++j)
			{
				if (used[j] != 0) continue;
				const FunctionTemplateResultLookupFact& destination_fact =
					destination_facts[j];
				const bool same_declaration =
					source_fact.declaration != kNoBinding &&
					destination_fact.declaration != kNoBinding &&
					program_->bindings[source_fact.declaration].canonical ==
						program_->bindings[destination_fact.declaration].canonical;
				const bool same_namespace =
					source_fact.declaration == kNoBinding &&
					destination_fact.declaration == kNoBinding &&
					source_fact.name_space != kNoScope &&
					source_fact.name_space == destination_fact.name_space;
				if (same_declaration || same_namespace)
				{
					match = j;
					break;
				}
			}
			if (match == destination_facts.size())
			{
				bool expanded_type = false;
				if (expanded_equivalent &&
					source_fact.declaration != kNoBinding)
				{
					const BindingKind kind =
						program_->bindings[source_fact.declaration].kind;
					expanded_type =
						kind == BIND_TYPE || kind == BIND_TYPE_ALIAS;
				}
				if (expanded_type) continue;
				throw std::logic_error(
					"equivalent function template result lookup diverged");
			}
			used[match] = 1;
			FunctionTemplateResultLookupFact inherited = source_fact;
			inherited.syntax = destination_facts[match].syntax;
			if (expanded_equivalent) remapped[match] = inherited;
			else remapped.push_back(inherited);
		}

		const auto collect_calls = [this](
			const FunctionTemplatePattern& pattern,
			std::vector<NodeId>* calls) {
			std::vector<NodeId> nodes;
			if (pattern.specifiers != kNoNode)
				nodes.push_back(pattern.specifiers);
			if (pattern.trailing_return_syntax != kNoNode)
				nodes.push_back(pattern.trailing_return_syntax);
			while (!nodes.empty())
			{
				const NodeId node = nodes.back();
				nodes.pop_back();
				if (node < retained_call_lookup_states_.size() &&
					retained_call_lookup_states_[node] != 0)
					calls->push_back(node);
				for (std::uint32_t edge = arena_->FirstEdge(node);
					edge != kNoEdge; edge = arena_->NextEdge(edge))
					nodes.push_back(arena_->EdgeChild(edge));
			}
			std::sort(calls->begin(), calls->end());
		};
		std::vector<NodeId> source_calls, destination_calls;
		collect_calls(source, &source_calls);
		collect_calls(*destination, &destination_calls);
		std::vector<std::uint8_t> used_calls(destination_calls.size(), 0);
		for (std::size_t i = 0; i < source_calls.size(); ++i)
		{
			std::size_t match = destination_calls.size();
			for (std::size_t j = 0; j < destination_calls.size(); ++j)
				if (used_calls[j] == 0 && EquivalentNormalizedTemplateSyntax(
					*arena_, source_calls[i], destination_calls[j],
					source.parameters, destination->parameters))
				{
					match = j;
					break;
				}
			if (match == destination_calls.size())
				throw std::logic_error(
					"equivalent function template retained call diverged");
			used_calls[match] = 1;
			CopyRetainedCallLookup(source_calls[i], destination_calls[match]);
		}
	}
	if (!expanded_equivalent &&
		remapped.size() != source.result_lookup_facts.size())
		throw std::logic_error(
			"function template result lookup remap is incomplete");
	std::sort(remapped.begin(), remapped.end(),
		[](const FunctionTemplateResultLookupFact& left,
			const FunctionTemplateResultLookupFact& right) {
			return left.syntax < right.syntax;
		});
	destination->result_lookup_facts.swap(remapped);
	destination->result_root_declaration = source.result_root_declaration;
	destination->result_root_namespace = source.result_root_namespace;
}

void SemanticAnalyzer::AdoptFunctionTemplateDefinition(
	std::size_t pattern_index, FunctionTemplatePattern* retained,
	FunctionTemplatePattern* incoming, bool explicit_member_definition)
{
	if (!retained || !incoming)
		throw std::logic_error("function template definition has no pattern");
	if (retained->defined && (!explicit_member_definition ||
		retained->explicit_member_definition))
		throw std::runtime_error("duplicate function template definition");
	retained->lexical_scope = incoming->lexical_scope;
	retained->specifiers = incoming->specifiers;
	retained->declarator = incoming->declarator;
	retained->trailing_return_syntax = incoming->trailing_return_syntax;
	retained->result_lookup_facts.swap(incoming->result_lookup_facts);
	retained->result_root_structure = incoming->result_root_structure;
	retained->result_root_name = incoming->result_root_name;
	retained->result_root_declaration = incoming->result_root_declaration;
	retained->result_root_namespace = incoming->result_root_namespace;
	retained->expanded_result_identity = incoming->expanded_result_identity;
	retained->result_root_global = incoming->result_root_global;
	retained->expanded_result_has_alias = incoming->expanded_result_has_alias;
	retained->definition_body = incoming->definition_body;
	retained->constructor_initializer = incoming->constructor_initializer;
	retained->function_parameter_names = incoming->function_parameter_names;
	retained->function_parameter_defaults = incoming->function_parameter_defaults;
	retained->language_linkage = incoming->language_linkage;
	retained->static_member = retained->static_member || incoming->static_member;
	retained->definition_in_class = incoming->definition_in_class;
	retained->explicit_member_definition =
		retained->explicit_member_definition || explicit_member_definition;
	if (incoming->lexical_scope == incoming->owner)
		retained->member_access = incoming->member_access;
	retained->defined = true;
	UpgradeFunctionTemplateSpecializations(pattern_index);
}

void SemanticAnalyzer::AppendConstructorTemplateCandidates(
	TypeId initialized_type, const std::vector<ExpressionInfo>& arguments,
	std::vector<BindingId>* candidates,
	const std::vector<NodeId>* argument_syntax, ScopeId argument_scope)
{
	const EntityId entity = initialized_type == kNoType ?
		kNoEntity : EntityOf(initialized_type);
	if (entity == kNoEntity ||
		program_->entities[entity].member_scope == kNoScope) return;
	const ScopeId owner = program_->entities[entity].member_scope;
	const NameId name = program_->entities[entity].identity_name;
	const std::uint64_t key =
		(static_cast<std::uint64_t>(owner) << 32) | name;
	const CompactIndexSequence* indexed = template_function_sets_.Find(key);
	if (!indexed) return;
	std::vector<std::size_t> patterns;
	for (std::size_t i = 0; i < indexed->Size(); ++i)
		if (function_templates_[(*indexed)[i]].constructor_template)
			patterns.push_back((*indexed)[i]);
	std::vector<BindingId> specializations;
	DeduceFunctionTemplatePatterns(patterns, arguments, &specializations,
		0, 0, argument_scope, argument_syntax);
	std::vector<BindingId> inherited_sources;
	for (std::size_t i = 0; i < specializations.size(); ++i)
	{
		const EntityId source = EntityOf(
			GetFunction(specializations[i]).member_owner);
		if (source != entity)
			inherited_sources.push_back(specializations[i]);
		else if (std::find(candidates->begin(), candidates->end(),
			specializations[i]) == candidates->end())
			candidates->push_back(specializations[i]);
	}
	const std::size_t first_inherited = entity_constructors_[entity].size();
	InheritConstructors(entity, inherited_sources);
	for (std::size_t i = first_inherited;
		i < entity_constructors_[entity].size(); ++i)
		candidates->push_back(entity_constructors_[entity][i]);
}

void SemanticAnalyzer::PublishFunctionTemplateFriendGrants(
	const FunctionTemplatePattern& pattern, BindingId specialization)
{
	if (specialization == kNoBinding) return;
	specialization = program_->bindings[specialization].canonical;
	FunctionInfo& function = GetMutableFunction(specialization);
	for (std::size_t i = 0; i < pattern.friend_owners.size(); ++i)
	{
		const EntityId owner = pattern.friend_owners[i];
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owner) << 32) | specialization;
		CompactIndexSequence& grants = friend_function_grants_.Ensure(key);
		if (grants.Size() == 0) grants.Push(0);
		if (function.friend_of == kNoEntity) function.friend_of = owner;
	}
}

void SemanticAnalyzer::RegisterFunctionTemplateFriend(
	std::size_t pattern_index, EntityId owner, bool hidden)
{
	if (pattern_index >= function_templates_.size() || owner == kNoEntity)
		throw std::logic_error("invalid function template friend owner");
	FunctionTemplatePattern& pattern = function_templates_[pattern_index];
	if (std::find(pattern.friend_owners.begin(), pattern.friend_owners.end(),
		owner) == pattern.friend_owners.end())
		pattern.friend_owners.push_back(owner);
	if (hidden)
	{
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owner) << 32) | pattern.name;
		CompactIndexSequence& patterns =
			hidden_friend_template_sets_.Ensure(key);
		if (!patterns.Contains(pattern_index)) patterns.Push(pattern_index);
	}
	for (std::size_t i = 0; i < pattern.specialization_bindings.size(); ++i)
		PublishFunctionTemplateFriendGrants(
			pattern, pattern.specialization_bindings[i]);
}

TypeId SemanticAnalyzer::DependentFunctionTemplateResultShape()
{
	if (function_template_dependent_result_shape_ == kNoType)
	{
		const NameId shape_name = program_->names.Intern(
			"__function_template_dependent_result_shape");
		const EntityId shape = program_->NewEntity(shape_name,
			NAMED_TYPENAME_PARAMETER, false, kNoType,
			program_->GlobalScope(), shape_name);
		function_template_dependent_result_shape_ =
			program_->types.Named(shape);
	}
	return function_template_dependent_result_shape_;
}

void SemanticAnalyzer::EnsureFunctionTemplateShapeParameters(std::size_t count)
{
	while (function_template_shape_parameters_.size() < count)
	{
		const NameId name = program_->names.Intern(
			"__function_template_parameter_shape_" + std::to_string(
				function_template_shape_parameters_.size()));
		const EntityId entity = program_->NewEntity(name,
			NAMED_TYPENAME_PARAMETER, false, kNoType,
			program_->GlobalScope(), name);
		function_template_shape_parameters_.push_back(
			program_->types.Named(entity));
	}
}

void SemanticAnalyzer::ApplyGenericLambdaSpecializationFacts(
	const FunctionTemplatePattern& pattern, BindingId binding,
	EntityId member_owner)
{
	if (member_owner == kNoEntity || member_owner >= program_->entities.size() ||
		!program_->entities[member_owner].lambda_closure) return;
	EntityRecord& owner = program_->entities[member_owner];
	if (owner.lambda_call_operator == kNoBinding)
		owner.lambda_call_operator = binding;
	FunctionInfo& function = GetMutableFunction(binding);
	function.lexical_access_function =
		pattern.lambda_lexical_access_function;
	function.lambda_capture_begin = pattern.lambda_capture_begin;
	function.lambda_capture_count = pattern.lambda_capture_count;
	function.lambda_this_capture_member = pattern.lambda_this_capture_member;
}

std::size_t SemanticAnalyzer::FindPriorFunctionTemplatePattern(
	const FunctionTemplatePattern& pattern, EntityId friend_owner,
	bool qualified_friend, bool definition)
{
	const std::uint64_t key =
		(static_cast<std::uint64_t>(pattern.owner) << 32) | pattern.name;
	const CompactIndexSequence* candidates = template_function_sets_.Find(key);
	// A hidden definition normally redeclares only within its class
	// specialization.  A published namespace declaration remains eligible.
	if (friend_owner != kNoEntity && !qualified_friend && definition)
	{
		NamePath direct;
		direct.Push(pattern.name);
		const LookupResult visible = program_->LookupQualified(
			pattern.owner, direct, LOOKUP_FUNCTION_TEMPLATE);
		if (visible.FunctionTemplateOwnerCount() == 0)
		{
			const std::uint64_t hidden_key =
				(static_cast<std::uint64_t>(friend_owner) << 32) |
				pattern.name;
			candidates = hidden_friend_template_sets_.Find(hidden_key);
		}
	}
	if (!candidates) return function_templates_.size();
	for (std::size_t p = 0; p < candidates->Size(); ++p)
	{
		const std::size_t candidate = (*candidates)[p];
		const FunctionTemplatePattern& prior = function_templates_[candidate];
		const bool distinct_hidden_owner = friend_owner != kNoEntity &&
			!qualified_friend && definition && prior.definition_in_class &&
			std::find(prior.friend_owners.begin(), prior.friend_owners.end(),
				friend_owner) == prior.friend_owners.end();
		if (distinct_hidden_owner) continue;
		const bool dependent_result = TypeContainsDependentResultShape(
			program_->types, program_->types.Get(prior.shape_type).child,
			function_template_dependent_result_shape_);
		if (EquivalentFunctionTemplateParameterLists(*arena_, prior.parameters,
				pattern.parameters, program_, prior.lexical_scope,
				pattern.lexical_scope) &&
			prior.shape_type == pattern.shape_type &&
			EquivalentFunctionTemplateNondeducedShapes(*arena_, prior, pattern) &&
			(!dependent_result ||
			 EquivalentDependentFunctionTemplateResults(*arena_, prior, pattern) ||
			 EquivalentExpandedFunctionTemplateResults(prior, pattern)))
			return candidate;
	}
	return function_templates_.size();
}

ScopeId SemanticAnalyzer::FunctionTemplateExceptionScope(
	const FunctionTemplatePattern& pattern,
	const FunctionInfo& function)
{
	const ScopeId scope = NewScope(function.lexical_scope, SCOPE_FUNCTION,
		program_->bindings[function.binding].name,
		ScopePrefixId(function.lexical_scope));
	BindFunctionParameterPackElement(
		scope, function.parameter_pack_name, kNoBinding);
	for (std::size_t i = 0; i < function.parameters.size(); ++i)
	{
		const ParameterInfo& parameter = function.parameters[i];
		const BindingId binding = program_->AddBinding(scope, BIND_PARAMETER,
			parameter.name, ParameterBindingType(parameter));
		BindFunctionParameterPackElement(scope, parameter.pack_name, binding);
	}
	if (function.member_owner != kNoType && !pattern.static_member)
	{
		TypeId object = function.member_owner;
		const TypeRecord& function_type = program_->types.Get(function.type);
		if (function_type.cv != CV_NONE)
			object = program_->types.Qualify(object, function_type.cv);
		program_->AddBinding(scope, BIND_PARAMETER,
			program_->names.Intern("this"), program_->types.Pointer(object));
	}
	return scope;
}

void SemanticAnalyzer::RegisterFunctionTemplatePattern(NodeId target,
	ScopeId scope, AccessKind member_access,
	const std::vector<TemplateParameter>& parameters, NodeId specifiers,
	NodeId declarator, bool definition, bool special_member_template,
	TypeId dependent_result_shape, bool dependent_exception_specification)
{
	const NamePath path = DeclaratorNamePath(declarator);
	if (path.Empty()) throw std::runtime_error("function template has no name");
	FunctionTemplatePattern pattern;
	pattern.lexical_scope = scope;
	pattern.name = path.Last();
	pattern.specifiers = specifiers;
	pattern.declarator = declarator;
	pattern.definition_body = definition ?
		FindChild(target, "compound-statement") : kNoNode;
	pattern.constructor_initializer = definition ? FindChild(
		target, "ctor-initializer") : kNoNode;
	pattern.parameters = parameters;
	pattern.language_linkage = current_language_linkage_;
	pattern.member_access = member_access;
	pattern.defined = definition;
	pattern.conversion_template = special_member_template && FindChild(
		declarator, "conversion-type-id") != kNoNode;
	pattern.constructor_template =
		special_member_template && !pattern.conversion_template;
	NodeId declaration_initializer = FindChild(target, "initializer");
	const NodeId declarator_list = FindChild(target, "init-declarator-list");
	for (std::uint32_t edge = declarator_list == kNoNode ? kNoEdge :
		arena_->FirstEdge(declarator_list);
		edge != kNoEdge && declaration_initializer == kNoNode;
		edge = arena_->NextEdge(edge))
	{
		const NodeId item = arena_->EdgeChild(edge);
		if (FindChild(item, "declarator") == declarator)
			declaration_initializer = FindChild(item, "initializer");
	}
	const NodeId special_initializer = declaration_initializer == kNoNode ?
		kNoNode : FindChild(declaration_initializer, "special-initializer");
	pattern.deleted_function = special_initializer != kNoNode &&
		arena_->Payload(special_initializer) == "delete";
	pattern.dependent_exception_specification =
		dependent_exception_specification;
	const bool explicit_member_definition = definition &&
		explicit_member_template_replay_depth_ != 0;
	pattern.explicit_member_definition = explicit_member_definition;
	const bool friend_syntax = CaptureFunctionTemplateSpecifierFacts(
		*arena_, *program_, specifiers, &pattern);
	if (!friend_syntax)
	{
		if (special_member_template &&
			program_->EntityForScope(scope) != kNoEntity)
			pattern.owner = scope;
		else
		{
			ScopeId structured_owner = kNoScope;
			const NodeId structure = DeclaratorNameStructure(declarator);
			if (structure != kNoNode && (path.global || path.Size() > 1))
				(void)LookupStructuredName(structure, scope,
					LOOKUP_FUNCTION_TEMPLATE, &structured_owner);
			pattern.owner = structured_owner != kNoScope ? structured_owner :
				ResolveOwner(scope, path);
		}
		if (pattern.owner == kNoScope)
			throw std::runtime_error("function template owner not found");
		if (program_->EntityForScope(pattern.owner) != kNoEntity)
			pattern.lexical_scope =
				TemplateLexicalScope(scope, pattern.owner);
	}
	const ScopeId shape_scope = NewScope(pattern.lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(pattern.lexical_scope));
	std::unordered_set<NameId> parameter_names;
	for (std::size_t p = 0; p < parameters.size(); ++p)
	{
		if (parameters[p].name == 0) continue;
		parameter_names.insert(parameters[p].name);
		if (parameters[p].kind == TEMPLATE_ARGUMENT_TYPE)
			program_->AddBinding(shape_scope, BIND_TYPE_ALIAS,
				parameters[p].name, function_template_shape_parameters_[p]);
		else if (parameters[p].kind == TEMPLATE_ARGUMENT_TEMPLATE)
			CreateTemplateTemplateParameterProxy(shape_scope,
				parameters[p], p);
		else program_->AddBinding(shape_scope, BIND_PARAMETER,
			parameters[p].name, parameters[p].dependent_type ?
				program_->types.Fundamental(FUND_INT) :
				parameters[p].value_type, false,
			static_cast<std::int64_t>(p));
	}
	const NodeId trailing_return = FindChild(declarator, "trailing-return-type");
	const bool dependent_trailing_return = trailing_return != kNoNode &&
		(PayloadSource(trailing_return).find("decltype") == 0 ||
		 SyntaxUsesAnyTemplateParameter(trailing_return, parameter_names) ||
		 FunctionTemplateResultUsesDependentParameter(
			declarator, trailing_return, parameter_names));
	if (dependent_result_shape == kNoType && dependent_trailing_return)
		dependent_result_shape = DependentFunctionTemplateResultShape();
	SpecInfo shape_spec;
	if (pattern.constructor_template)
		shape_spec.type = program_->types.Fundamental(FUND_VOID);
	else if (pattern.conversion_template)
		shape_spec.type = BuildTypeId(
			FindChild(declarator, "conversion-type-id"), shape_scope);
	else shape_spec = BuildSpecifiers(specifiers, shape_scope, std::string(), true, false, dependent_result_shape);
	ApplyFunctionTemplateSpecifierFacts(pattern, &shape_spec);
	if (dependent_result_shape != kNoType && shape_spec.placeholder_auto &&
		trailing_return != kNoNode)
		shape_spec.type = dependent_result_shape;
	pattern.deferred_result_formation = (dependent_result_shape != kNoType &&
		shape_spec.type == dependent_result_shape) ||
		(shape_spec.placeholder_auto && trailing_return == kNoNode);
	EntityId friend_owner = kNoEntity;
	const bool qualified_friend = path.global || path.Size() > 1;
	if (shape_spec.is_friend)
	{
		ScopeId class_scope = scope;
		while (class_scope != kNoScope &&
			program_->KindOfScope(class_scope) != SCOPE_CLASS)
			class_scope = program_->ParentScope(class_scope);
		friend_owner = class_scope == kNoScope ? kNoEntity :
			program_->EntityForScope(class_scope);
		if (friend_owner == kNoEntity)
			throw std::runtime_error(
				"friend function template has no class owner");
		if (qualified_friend) pattern.owner = ResolveOwner(scope, path);
		else
		{
			pattern.owner = program_->entities[friend_owner].owner;
			while (pattern.owner != kNoScope &&
				program_->KindOfScope(pattern.owner) != SCOPE_NAMESPACE)
				pattern.owner = program_->ParentScope(pattern.owner);
		}
		pattern.ordinary_visible = qualified_friend;
		pattern.definition_in_class = definition;
	}
	else if (pattern.owner == kNoScope)
		pattern.owner = ResolveOwner(scope, path);
	if (pattern.owner == kNoScope)
		throw std::runtime_error("function template owner not found");
	pattern.nonthrowing = dependent_exception_specification ?
		false : IsNonthrowing(declarator, pattern.owner);
	pattern.trailing_return_syntax = FindChild(declarator, "trailing-return-type");
	const bool defer_trailing_return = dependent_trailing_return;
	const EntityId member_owner = program_->EntityForScope(pattern.owner);
	if (special_member_template && member_owner == kNoEntity)
		throw std::runtime_error(
			"special-member template owner is not a class");
	const bool nonstatic_member = member_owner != kNoEntity &&
		shape_spec.storage_class != STORAGE_CLASS_STATIC;
	pattern.static_member = member_owner != kNoEntity &&
		shape_spec.storage_class == STORAGE_CLASS_STATIC;
	const EntityId previous_class = current_class_context_;
	if (member_owner != kNoEntity) current_class_context_ = member_owner;
	DeclaratorInfo shape_declarator = BuildDeclarator(declarator, shape_spec.type,
		shape_scope, shape_spec.placeholder_auto, nonstatic_member,
		defer_trailing_return, &parameter_names);
	current_class_context_ = previous_class;
	ValidateFunctionTemplatePatternResults(&pattern, shape_declarator, shape_scope,
		parameter_names, defer_trailing_return);
	if (!program_->types.IsFunction(shape_declarator.type)) throw std::runtime_error(
			"function template has non-function declaration");
	if (shape_spec.is_constexpr && !pattern.constructor_template)
		shape_declarator.type = ApplyConstexprMemberFunctionType(
			shape_declarator.type, member_owner,
			shape_spec.storage_class == STORAGE_CLASS_STATIC);
	if (shape_spec.is_constexpr && shape_declarator.placeholder_return_kind ==
		PLACEHOLDER_DECLARATOR_NONE)
		ValidateConstexprCallableType(
			shape_declarator.type, pattern.constructor_template);
	pattern.shape_type = shape_declarator.type;
	pattern.result_type_dependent = FunctionTemplateTypeIsDependent(program_->types.Get(pattern.shape_type).child);
	CaptureFunctionParameterMetadata(&pattern, shape_declarator);
	CaptureFunctionTemplateDefaultContexts(&pattern);
	if (pattern.constructor_template && pattern.name !=
		program_->entities[member_owner].identity_name)
		throw std::runtime_error(
			"only constructor special-member templates are supported");
	if (pattern.conversion_template &&
		(!shape_declarator.parameters.empty() ||
		 program_->types.Get(shape_declarator.type).child != shape_spec.type))
		throw std::runtime_error(
			"invalid conversion function template declarator");
	if (pattern.constructor_template)
		RecordConstructorTemplateDeclaration(*arena_, program_, member_owner,
			target, definition);
	InitializeFunctionTemplatePackShape(&pattern, shape_declarator);
	if (ConstructorTemplateMayAcceptNoArguments(pattern))
		program_->entities[member_owner].default_constructible = true;
	const std::size_t prior_index = FindPriorFunctionTemplatePattern(
		pattern, friend_owner, qualified_friend, definition);
	if (prior_index != function_templates_.size())
	{
		FunctionTemplatePattern& prior = function_templates_[prior_index];
		if (definition && (prior.deferred_result_formation ||
			prior.trailing_return_syntax != kNoNode))
			InheritFunctionTemplateResultLookups(prior, &pattern);
		MergeFunctionTemplateSpecifierFacts(&prior, pattern);
		InheritFunctionParameterMetadata(&prior, &pattern, definition);
		MergeFunctionTemplateDefaults(&prior, pattern, definition);
		prior.required_parameter_count = std::min(
			prior.required_parameter_count, pattern.required_parameter_count);
		if (prior.nonthrowing != pattern.nonthrowing ||
			prior.dependent_exception_specification !=
				pattern.dependent_exception_specification)
			throw std::runtime_error(
				"conflicting function template exception specification");
		if (friend_owner != kNoEntity)
			RegisterFunctionTemplateFriend(
				prior_index, friend_owner, !qualified_friend);
		if (pattern.ordinary_visible && !prior.ordinary_visible)
		{
			prior.ordinary_visible = true;
			program_->PublishFunctionTemplateName(prior.owner, prior.name);
		}
		if (definition)
			AdoptFunctionTemplateDefinition(prior_index, &prior, &pattern,
				explicit_member_definition);
		return;
	}
	if (explicit_member_definition)
		throw std::runtime_error(
			"explicit member template definition target was not found");
	if (friend_owner != kNoEntity && qualified_friend)
		throw std::runtime_error(
			"qualified friend function template was not declared");
	pattern.abi_recipe = PublishFunctionTemplateAbiRecipe(program_, parameters,
		function_template_shape_parameters_, pattern);
	const std::size_t index = function_templates_.size();
	function_templates_.push_back(pattern);
	const std::uint64_t key =
		(static_cast<std::uint64_t>(pattern.owner) << 32) | pattern.name;
	CompactIndexSequence& overloads = template_function_sets_.Ensure(key);
	overloads.Push(index);
	MarkOverloadedFunctionTemplateAbiRecipes(
		program_, function_templates_, overloads);
	if (pattern.conversion_template)
	{
		if (entity_conversion_function_templates_.size() <= member_owner)
			entity_conversion_function_templates_.resize(
				static_cast<std::size_t>(member_owner) + 1);
		entity_conversion_function_templates_[member_owner].push_back(index);
	}
	if (pattern.ordinary_visible)
		program_->PublishFunctionTemplateName(pattern.owner, pattern.name);
	if (friend_owner != kNoEntity)
		RegisterFunctionTemplateFriend(index, friend_owner, true);
}

std::vector<std::size_t> SemanticAnalyzer::FindFunctionTemplates(
	ScopeId scope, const std::string& spelling)
{
	NamePath path = ParseNamePath(spelling);
	if (path.Empty()) return std::vector<std::size_t>();
	return FindFunctionTemplates(scope, path);
}

std::vector<std::size_t> SemanticAnalyzer::FindFunctionTemplates(
	ScopeId scope, const NamePath& path)
{
	if (path.Empty()) return std::vector<std::size_t>();
	const std::vector<ScopeId> owners =
		FindFunctionTemplateOwners(scope, path);
	const NameId name = path.Last();
	std::vector<std::size_t> result;
	for (std::size_t owner = 0; owner < owners.size(); ++owner)
	{
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owners[owner]) << 32) | name;
		const CompactIndexSequence* found =
			template_function_sets_.Find(key);
		if (!found) continue;
		for (std::size_t i = 0; i < found->Size(); ++i)
			result.push_back((*found)[i]);
	}
	return result;
}

std::vector<std::size_t> SemanticAnalyzer::FindStructuredFunctionTemplates(
	NodeId syntax, ScopeId scope)
{
	const NamePath path = StructuredNamePath(syntax);
	if (path.Empty()) return std::vector<std::size_t>();
	const LookupResult found = LookupStructuredName(
		syntax, scope, LOOKUP_FUNCTION_TEMPLATE);
	const NameId name = path.Last();
	std::vector<std::size_t> result;
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
			result.push_back((*indexed)[i]);
	}
	return result;
}

std::vector<ScopeId> SemanticAnalyzer::FindFunctionTemplateOwners(
	ScopeId scope, const std::string& spelling)
{
	NamePath path = ParseNamePath(spelling);
	if (path.Empty()) return std::vector<ScopeId>();
	return FindFunctionTemplateOwners(scope, path);
}

std::vector<ScopeId> SemanticAnalyzer::FindFunctionTemplateOwners(
	ScopeId scope, const NamePath& path)
{
	if (path.Empty()) return std::vector<ScopeId>();
	LookupResult found;
	if (path.global || path.Size() > 1)
	{
		const ScopeId owner = ResolveOwner(scope, path);
		if (owner == kNoScope) return std::vector<ScopeId>();
		NamePath name;
		name.Push(path.Last());
		found = program_->LookupQualified(
			owner, name, LOOKUP_FUNCTION_TEMPLATE);
	}
	else found = LookupPath(scope, path, LOOKUP_FUNCTION_TEMPLATE);
	std::vector<ScopeId> result;
	result.reserve(found.FunctionTemplateOwnerCount());
	for (std::size_t i = 0; i < found.FunctionTemplateOwnerCount(); ++i)
		result.push_back(found.FunctionTemplateOwnerAt(i));
	return result;
}

bool SemanticAnalyzer::BuildFunctionTemplateArgumentOffsets(
	const std::vector<TemplateParameter>& parameters,
	std::size_t argument_count, std::vector<std::uint32_t>* offsets) const
{
	offsets->clear();
	offsets->reserve(parameters.size() + 1);
	std::size_t cursor = 0;
	for (std::size_t parameter = 0; parameter < parameters.size(); ++parameter)
	{
		if (cursor > std::numeric_limits<std::uint32_t>::max()) return false;
		offsets->push_back(static_cast<std::uint32_t>(cursor));
		if (parameters[parameter].pack)
		{
			if (parameter + 1 != parameters.size()) return false;
			cursor = argument_count;
		}
		else
		{
			if (cursor >= argument_count) return false;
			++cursor;
		}
	}
	if (cursor != argument_count ||
		cursor > std::numeric_limits<std::uint32_t>::max()) return false;
	offsets->push_back(static_cast<std::uint32_t>(cursor));
	return true;
}

bool SemanticAnalyzer::BuildExplicitFunctionTemplateArguments(
	const FunctionTemplatePattern& pattern,
	const std::vector<NodeId>& syntax, ScopeId use_scope,
	std::vector<TemplateArgument>* arguments,
	std::vector<std::uint32_t>* parameter_offsets)
{
	std::size_t first_pack = pattern.parameters.size();
	for (std::size_t parameter = 0;
		parameter < pattern.parameters.size(); ++parameter)
		if (pattern.parameters[parameter].pack)
		{
			first_pack = parameter;
			break;
		}
	if (!FunctionTemplateNeedsPartitionIdentity(pattern.parameters))
	{
		parameter_offsets->clear();
		return BuildTemplateArguments(pattern.parameters, syntax, use_scope,
			pattern.lexical_scope, arguments);
	}

	const std::vector<TemplateParameter> explicit_parameters(
		pattern.parameters.begin(), pattern.parameters.begin() + first_pack + 1);
	std::vector<TemplateArgument> explicit_arguments;
	if (!BuildTemplateArguments(explicit_parameters, syntax, use_scope,
		pattern.lexical_scope, &explicit_arguments, false) ||
		explicit_arguments.size() < first_pack) return false;
	arguments->clear();
	parameter_offsets->clear();
	arguments->reserve(explicit_arguments.size() +
		pattern.parameters.size() - first_pack - 1);
	parameter_offsets->reserve(pattern.parameters.size() + 1);
	for (std::size_t parameter = 0;
		parameter < pattern.parameters.size(); ++parameter)
	{
		if (arguments->size() > std::numeric_limits<std::uint32_t>::max())
			return false;
		parameter_offsets->push_back(
			static_cast<std::uint32_t>(arguments->size()));
		if (parameter < first_pack)
			arguments->push_back(explicit_arguments[parameter]);
		else if (parameter == first_pack)
			arguments->insert(arguments->end(),
				explicit_arguments.begin() + first_pack,
				explicit_arguments.end());
		else if (!pattern.parameters[parameter].pack)
		{
			TemplateArgument argument;
			argument.kind = pattern.parameters[parameter].kind;
			arguments->push_back(argument);
		}
	}
	if (arguments->size() > std::numeric_limits<std::uint32_t>::max())
		return false;
	parameter_offsets->push_back(
		static_cast<std::uint32_t>(arguments->size()));
	return true;
}

ScopeId SemanticAnalyzer::BindFunctionTemplateArguments(
	const FunctionTemplatePattern& pattern,
	const std::vector<TemplateArgument>& arguments,
	const std::vector<std::uint32_t>& parameter_offsets)
{
	if (parameter_offsets.size() != pattern.parameters.size() + 1 ||
		parameter_offsets.empty() || parameter_offsets.front() != 0 ||
		parameter_offsets.back() != arguments.size())
		throw std::logic_error("function template argument offsets are invalid");
	const ScopeId template_scope = NewScope(pattern.lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(pattern.lexical_scope));
	for (std::size_t parameter = 0;
		parameter < pattern.parameters.size(); ++parameter)
	{
		const std::size_t first = parameter_offsets[parameter];
		const std::size_t last = parameter_offsets[parameter + 1];
		if (first > last || last > arguments.size())
			throw std::logic_error(
				"function template argument offset range is invalid");
		if (pattern.parameters[parameter].pack)
			BindTemplateArgumentPack(template_scope,
				pattern.parameters[parameter], arguments, first, last);
		else
		{
			if (last != first + 1)
				throw std::logic_error(
					"fixed function template parameter has invalid arity");
			BindTemplateArgument(template_scope, pattern.parameters[parameter],
				arguments[first]);
		}
	}
	return template_scope;
}

DeclaratorInfo SemanticAnalyzer::BuildFunctionTemplateSpecializationDeclarator(
	const FunctionTemplatePattern& pattern, ScopeId template_scope,
	SpecInfo* spec, EntityId* member_owner)
{
	*member_owner = program_->EntityForScope(pattern.owner);
	const EntityId semantic_owner = *member_owner != kNoEntity ?
		*member_owner : pattern.friend_owners.empty() ? kNoEntity :
		pattern.friend_owners.front();
	const EntityId previous_class = current_class_context_;
	const FunctionTemplatePattern* previous_result_pattern =
		active_function_template_result_pattern_;
	if (semantic_owner != kNoEntity) current_class_context_ = semantic_owner;
	active_function_template_result_pattern_ = &pattern;
	DeclaratorInfo parsed;
	try
	{
		if (pattern.constructor_template)
		{
			spec->type = program_->types.Fundamental(FUND_VOID);
			spec->is_constexpr = pattern.constexpr_specifier;
			spec->inline_specifier = pattern.inline_specifier;
		}
		else if (pattern.conversion_template)
		{
			spec->type = BuildTypeId(
				FindChild(pattern.declarator, "conversion-type-id"),
				template_scope);
			spec->is_constexpr = pattern.constexpr_specifier;
			spec->inline_specifier = pattern.inline_specifier;
		}
		else if (pattern.deferred_result_formation)
		{
			// Forming a candidate's retained alias result does not demand the
			// body of a terminal class specialization. Qualified lookup still
			// completes non-terminal carriers, and selected uses demand layout.
			++class_template_completion_suppressed_depth_;
			try
			{
				*spec = BuildSpecifiers(pattern.specifiers, template_scope,
					std::string(), true);
			}
			catch (...)
			{
				--class_template_completion_suppressed_depth_;
				throw;
			}
			--class_template_completion_suppressed_depth_;
		}
		else *spec = BuildSpecifiers(pattern.specifiers, template_scope,
			std::string(), true);
		if (spec->type == kNoType && CandidateSubstitutionActive() &&
			!CandidateSubstitutionFailed())
			RecordCandidateSubstitutionFailure();
		if (!CandidateSubstitutionFailed() && spec->type != kNoType)
				parsed = BuildDeclarator(pattern.declarator, spec->type,
					template_scope, spec->placeholder_auto,
					*member_owner != kNoEntity &&
						!pattern.static_member &&
						spec->storage_class != STORAGE_CLASS_STATIC);
	}
	catch (...)
	{
		current_class_context_ = previous_class;
		active_function_template_result_pattern_ = previous_result_pattern;
		throw;
	}
	current_class_context_ = previous_class;
	active_function_template_result_pattern_ = previous_result_pattern;
	if (CandidateSubstitutionFailed() || spec->type == kNoType) return parsed;
	const std::size_t metadata_count =
		pattern.function_parameter_names.size();
	if (metadata_count != pattern.function_parameter_defaults.size())
		throw std::logic_error(
			"function template parameter metadata is truncated");
	const std::size_t pack_position = metadata_count == 0 ? 0 :
		metadata_count - 1;
	if (parsed.parameters.size() != metadata_count &&
		(!pattern.function_parameter_pack || metadata_count == 0 ||
		 parsed.parameters.size() < pack_position))
		throw std::logic_error(
			"function template parameter metadata does not match declarator");
	for (std::size_t parameter = 0;
		parameter < parsed.parameters.size(); ++parameter)
	{
		const std::size_t source = pattern.function_parameter_pack &&
			parameter >= pack_position ? pack_position : parameter;
		if (parsed.parameters[parameter].name == 0)
		{
			parsed.parameters[parameter].name =
				pattern.function_parameter_names[source];
			if (parameter > pack_position &&
				parsed.parameters[parameter].name != 0)
			{
				const std::string spelling = program_->names.Get(
					parsed.parameters[parameter].name);
				parsed.parameters[parameter].name = program_->names.Intern(
					spelling + "__pack" + std::to_string(
						parameter - pack_position + 1));
			}
		}
		if (parsed.parameters[parameter].default_argument == kNoNode &&
			pattern.function_parameter_defaults[source] != kNoNode)
		{
			parsed.parameters[parameter].default_argument =
				pattern.function_parameter_defaults[source];
			parsed.parameters[parameter].default_scope = template_scope;
		}
	}
	if (spec->is_constexpr && !pattern.constructor_template)
		parsed.type = ApplyConstexprMemberFunctionType(parsed.type,
			*member_owner, pattern.static_member ||
				spec->storage_class == STORAGE_CLASS_STATIC);
	return parsed;
}

void SemanticAnalyzer::UpgradeFunctionTemplateSpecializations(
	std::size_t index)
{
	if (index >= function_templates_.size())
		throw std::logic_error("invalid function template upgrade");
	const FunctionTemplatePattern& pattern = function_templates_[index];
	const std::vector<BindingId> specializations =
		pattern.specialization_bindings;
	const std::vector<TemplateArgument> specialization_arguments =
		pattern.specialization_arguments;
	const std::vector<std::uint32_t> specialization_offsets =
		pattern.specialization_argument_offsets;
	const std::vector<std::uint32_t> parameter_offsets =
		pattern.specialization_parameter_offsets;
	if (specialization_offsets.size() != specializations.size())
		throw std::logic_error(
			"function template specialization offsets are invalid");
	for (std::size_t specialization = 0;
		specialization < specializations.size(); ++specialization)
	{
		const std::size_t first = specialization_offsets[specialization];
		const std::size_t last = specialization + 1 < specialization_offsets.size() ?
			specialization_offsets[specialization + 1] :
			specialization_arguments.size();
		if (first > last || last > specialization_arguments.size())
			throw std::logic_error(
				"function template specialization argument range is invalid");
		std::vector<TemplateArgument> arguments;
		arguments.reserve(last - first);
		for (std::size_t p = first; p < last; ++p)
			arguments.push_back(specialization_arguments[p]);
		std::vector<std::uint32_t> partitions;
		if (FunctionTemplateNeedsPartitionIdentity(pattern.parameters))
		{
			const std::size_t partition_first = specialization *
				(pattern.parameters.size() + 1);
			if (partition_first > parameter_offsets.size() ||
				pattern.parameters.size() + 1 >
					parameter_offsets.size() - partition_first)
				throw std::logic_error(
					"function template specialization partitions are invalid");
			partitions.assign(parameter_offsets.begin() + partition_first,
				parameter_offsets.begin() + partition_first +
					pattern.parameters.size() + 1);
		}
		else if (!BuildFunctionTemplateArgumentOffsets(
			pattern.parameters, arguments.size(), &partitions))
			throw std::logic_error(
				"function template specialization shape is invalid");
		const ScopeId template_scope =
			BindFunctionTemplateArguments(pattern, arguments, partitions);
		SpecInfo spec;
		EntityId member_owner = kNoEntity;
		DeclaratorInfo parsed = BuildFunctionTemplateSpecializationDeclarator(
			pattern, template_scope, &spec, &member_owner);
		FunctionInfo& function = GetMutableFunction(
			specializations[specialization]);
		ConfigurePlaceholderFunctionReturn(function.binding, parsed,
			spec.placeholder_cv);
		if (function.explicit_specialization) continue;
		if (function.type != parsed.type)
			throw std::runtime_error(
				"function template definition does not match declaration");
		const bool constexpr_specialization = spec.is_constexpr &&
			IsConstexprCallableType(parsed.type, pattern.constructor_template);
		function.parameters = parsed.parameters;
		function.constexpr_function =
			function.constexpr_function || constexpr_specialization;
		function.defined = true;
		function.deferred = true;
		function.definition_body = pattern.definition_body;
		if (pattern.constructor_template)
			function.constructor_initializer = pattern.constructor_initializer;
		function.lexical_scope = template_scope;
		function.definition_in_class = pattern.definition_in_class ||
			(member_owner != kNoEntity &&
			 pattern.lexical_scope == pattern.owner);
		PublishFunctionTemplateFriendGrants(
			pattern, specializations[specialization]);
		PublishInlineFunctionFacts(function.binding,
			spec.inline_specifier || constexpr_specialization ||
			function.definition_in_class);
		CompleteFunctionTemplatePlaceholderResult(
			index, function.binding, member_owner);
	}
}

BindingId SemanticAnalyzer::InstantiateFunctionTemplate(std::size_t index,
	const std::vector<TypeId>& arguments)
{
	if (index >= function_templates_.size())
		throw std::logic_error("invalid PA12 function template pattern");
	const FunctionTemplatePattern& pattern = function_templates_[index];
	std::vector<std::uint32_t> offsets;
	if (!BuildFunctionTemplateArgumentOffsets(
		pattern.parameters, arguments.size(), &offsets)) return kNoBinding;
	std::vector<TemplateArgument> canonical;
	canonical.reserve(arguments.size());
	for (std::size_t parameter = 0;
		parameter < pattern.parameters.size(); ++parameter)
		for (std::size_t argument = offsets[parameter];
			argument < offsets[parameter + 1]; ++argument)
		{
			if (pattern.parameters[parameter].kind != TEMPLATE_ARGUMENT_TYPE)
				return kNoBinding;
			canonical.push_back(TemplateArgument(
				TEMPLATE_ARGUMENT_TYPE, arguments[argument]));
		}
	return InstantiateFunctionTemplate(index, canonical, offsets);
}

BindingId SemanticAnalyzer::InstantiateFunctionTemplate(std::size_t index,
	const std::vector<TemplateArgument>& arguments)
{
	if (index >= function_templates_.size())
		throw std::logic_error("invalid PA12 function template pattern");
	const FunctionTemplatePattern& pattern = function_templates_[index];
	std::vector<std::uint32_t> offsets;
	if (!BuildFunctionTemplateArgumentOffsets(
		pattern.parameters, arguments.size(), &offsets)) return kNoBinding;
	return InstantiateFunctionTemplate(index, arguments, offsets);
}

void SemanticAnalyzer::PublishFunctionTemplateSpecialMemberRole(
	const FunctionTemplatePattern& pattern, BindingId binding,
	EntityId member_owner, TypeId function_type)
{
	if (!pattern.constructor_template && !pattern.conversion_template) return;
	if (member_owner == kNoEntity)
		throw std::logic_error(pattern.constructor_template ?
			"constructor template has no class owner" :
			"conversion function template has no class owner");
	BindingRecord& record = program_->bindings[binding];
	FunctionInfo& function = GetMutableFunction(binding);
	function.member_owner = program_->entities[member_owner].type;
	if (pattern.constructor_template)
	{
		record.constructor = true;
		function.constructor = true;
		function.explicit_constructor =
			function.explicit_constructor || pattern.explicit_specifier;
		function.complete_constructor = binding;
		function.constructor_initializer = pattern.constructor_initializer;
		if (entity_constructors_.size() <= member_owner)
			entity_constructors_.resize(
				static_cast<std::size_t>(member_owner) + 1);
		std::vector<BindingId>& constructors = entity_constructors_[member_owner];
		if (std::find(constructors.begin(), constructors.end(), binding) ==
			constructors.end()) constructors.push_back(binding);
		program_->entities[member_owner].has_user_declared_constructor = true;
		program_->entities[member_owner].has_user_provided_constructor =
			program_->entities[member_owner].has_user_provided_constructor ||
			pattern.defined;
		return;
	}
	const TypeId conversion_target = program_->types.Get(function_type).child;
	record.conversion_function = true;
	record.conversion_target = conversion_target;
	function.conversion_function = true;
	function.explicit_conversion =
		function.explicit_conversion || pattern.explicit_specifier;
	function.conversion_target = conversion_target;
	if (entity_conversion_functions_.size() <= member_owner)
		entity_conversion_functions_.resize(
			static_cast<std::size_t>(member_owner) + 1);
	std::vector<BindingId>& conversions =
		entity_conversion_functions_[member_owner];
	if (std::find(conversions.begin(), conversions.end(), binding) ==
		conversions.end()) conversions.push_back(binding);
}

bool SemanticAnalyzer::MaterializeFunctionTemplateDefaults(
	const FunctionTemplatePattern& pattern,
	const std::vector<TemplateArgument>& arguments,
	const std::vector<std::uint32_t>& parameter_offsets,
	std::vector<TemplateArgument>* completed)
{
	if (!completed)
		throw std::logic_error("missing completed function template arguments");
	*completed = arguments;
	if (pattern.default_context_by_parameter.size() !=
		pattern.parameters.size())
		throw std::logic_error(
			"function template default context map is truncated");
	std::vector<ScopeId> default_scopes(
		pattern.default_contexts.size(), kNoScope);
	const auto bind_completed = [&](ScopeId scope,
		const FunctionTemplateDefaultContext& context,
		std::size_t parameter_index)
	{
		if (parameter_index >= context.parameters.size())
			throw std::logic_error(
				"function template default context has wrong arity");
		const std::size_t first = parameter_offsets[parameter_index];
		const std::size_t last = parameter_offsets[parameter_index + 1];
		if (context.parameters[parameter_index].pack)
			BindTemplateArgumentPack(scope,
				context.parameters[parameter_index], *completed, first, last);
		else BindTemplateArgument(scope, context.parameters[parameter_index],
			(*completed)[first]);
	};
	for (std::size_t parameter_index = 0;
		parameter_index < pattern.parameters.size(); ++parameter_index)
	{
		const TemplateParameter& parameter =
			pattern.parameters[parameter_index];
		const std::size_t first = parameter_offsets[parameter_index];
		const std::size_t last = parameter_offsets[parameter_index + 1];
		if (parameter.pack)
		{
			for (std::size_t argument = first; argument < last; ++argument)
				if ((*completed)[argument].type == kNoType) return false;
			for (std::size_t context = 0;
				context < default_scopes.size(); ++context)
				if (default_scopes[context] != kNoScope)
					bind_completed(default_scopes[context],
						pattern.default_contexts[context], parameter_index);
			continue;
		}
		TemplateArgument& argument = (*completed)[first];
		if (argument.type != kNoType)
		{
			for (std::size_t context = 0;
				context < default_scopes.size(); ++context)
				if (default_scopes[context] != kNoScope)
					bind_completed(default_scopes[context],
						pattern.default_contexts[context], parameter_index);
			continue;
		}
		if (parameter.default_argument == kNoNode) return false;
		const std::uint32_t context_index =
			pattern.default_context_by_parameter[parameter_index];
		if (context_index == kNoDumpEdge ||
			context_index >= pattern.default_contexts.size())
			throw std::logic_error(
				"function template default has no retained context");
		const FunctionTemplateDefaultContext& context =
			pattern.default_contexts[context_index];
		if (context.parameters.size() != pattern.parameters.size())
			throw std::logic_error(
				"function template default context has wrong arity");
		ScopeId& default_scope = default_scopes[context_index];
		if (default_scope == kNoScope)
		{
			default_scope = NewScope(context.lexical_scope,
				SCOPE_TEMPLATE_PARAMETERS, 0,
				ScopePrefixId(context.lexical_scope));
			for (std::size_t prior = 0; prior < parameter_index; ++prior)
				bind_completed(default_scope, context, prior);
		}
		const TemplateParameter& source_parameter =
			context.parameters[parameter_index];
		NodeId source = FirstSemanticChild(
			source_parameter.default_argument);
		if (source == kNoNode)
			throw std::logic_error(
				"empty function template default argument");
		if (source_parameter.kind == TEMPLATE_ARGUMENT_TYPE)
		{
			NodeId type_id = arena_->IsTag(source, "type-id") ? source :
				FindChild(source, "type-id");
			if (type_id == kNoNode) return false;
			argument.type = BuildTypeId(type_id, default_scope);
			if (argument.type == kNoType) return false;
		}
		else if (source_parameter.kind == TEMPLATE_ARGUMENT_TEMPLATE)
		{
			if (!BuildTemplateTemplateArgument(
				source, default_scope, source_parameter, &argument)) return false;
		}
		else
		{
			argument.type = ResolveTemplateParameterType(
				source_parameter, default_scope);
			if (CandidateSubstitutionFailed() || argument.type == kNoType)
				return false;
			++constant_expression_required_depth_;
			ExpressionInfo value;
			try
			{
				value = AnalyzeExpression(
					source, default_scope, argument.type);
			}
			catch (...)
			{
				--constant_expression_required_depth_;
				throw;
			}
			--constant_expression_required_depth_;
			if (CandidateSubstitutionFailed()) return false;
			if (!FormNonTypeTemplateArgumentValue(value, &argument))
			{
				if (CandidateSubstitutionActive())
				{
					RecordCandidateSubstitutionFailure();
					return false;
				}
				throw std::runtime_error(
					"default non-type function template argument is not constant");
			}
		}
		for (std::size_t retained = 0;
			retained < default_scopes.size(); ++retained)
			if (default_scopes[retained] != kNoScope)
				bind_completed(default_scopes[retained],
					pattern.default_contexts[retained], parameter_index);
	}
	return true;
}

BindingId SemanticAnalyzer::InstantiateFunctionTemplate(std::size_t index,
	const std::vector<TemplateArgument>& arguments,
	const std::vector<std::uint32_t>& parameter_offsets)
{
	if (index >= function_templates_.size())
		throw std::logic_error("invalid PA12 function template pattern");
	const FunctionTemplatePattern& pattern = function_templates_[index];
	if (parameter_offsets.size() != pattern.parameters.size() + 1 ||
		parameter_offsets.empty() || parameter_offsets.front() != 0 ||
		parameter_offsets.back() != arguments.size()) return kNoBinding;
	for (std::size_t parameter = 0;
		parameter < pattern.parameters.size(); ++parameter)
	{
		const std::size_t first = parameter_offsets[parameter];
		const std::size_t last = parameter_offsets[parameter + 1];
		if (first > last || last > arguments.size() ||
			(!pattern.parameters[parameter].pack && last != first + 1))
			return kNoBinding;
		for (std::size_t argument = first; argument < last; ++argument)
			if (arguments[argument].kind != pattern.parameters[parameter].kind ||
				arguments[argument].IsDependent()) return kNoBinding;
	}
	++template_specialization_requests_;
	const std::vector<std::uint32_t> no_identity_offsets;
	const std::vector<std::uint32_t>& identity_offsets =
		FunctionTemplateNeedsPartitionIdentity(pattern.parameters) ?
			parameter_offsets : no_identity_offsets;
	bool needs_defaults = false;
	for (std::size_t parameter = 0;
		parameter < pattern.parameters.size(); ++parameter)
		if (!pattern.parameters[parameter].pack &&
			arguments[parameter_offsets[parameter]].type == kNoType)
		{
			needs_defaults = true;
			if (pattern.parameters[parameter].default_argument == kNoNode)
				return kNoBinding;
		}
	TemplateSpecializationKey request_key;
	if (needs_defaults)
	{
		request_key = CanonicalTemplateSpecializationKey(
			index, arguments, identity_offsets);
		BindingId requested = kNoBinding;
		const TemplateRequestState request_state =
			function_template_default_requests_.FindRequest(
				request_key, &requested);
		if (request_state != TEMPLATE_REQUEST_NOT_STARTED)
		{
			++template_specialization_cache_hits_;
			++function_template_default_request_cache_hits_;
			if (request_state != TEMPLATE_REQUEST_SUCCEEDED)
				++function_template_default_failure_cache_hits_;
			return request_state == TEMPLATE_REQUEST_SUCCEEDED ?
				requested : kNoBinding;
		}
		function_template_default_requests_.SetRequest(
			request_key, TEMPLATE_REQUEST_IN_PROGRESS);
		++function_template_default_materializations_;
	}

	std::vector<TemplateArgument> completed;
	if (!MaterializeFunctionTemplateDefaults(
		pattern, arguments, parameter_offsets, &completed))
	{
		if (needs_defaults)
			function_template_default_requests_.SetRequest(
				request_key, TEMPLATE_REQUEST_FAILED);
		return kNoBinding;
	}
	const TemplateSpecializationKey cache_key =
		CanonicalTemplateSpecializationKey(
			index, completed, identity_offsets);
	BindingId old = template_instantiations_.Find(cache_key);
	if (old != kNoBinding)
	{
		++template_specialization_cache_hits_;
		AnalyzeRetainedPlaceholderFunctionBody(old);
		if (needs_defaults)
			function_template_default_requests_.SetRequest(
				request_key, TEMPLATE_REQUEST_SUCCEEDED, old);
		return old;
	}
	ScopeId template_scope = kNoScope;
	SpecInfo spec;
	EntityId member_owner = kNoEntity;
	DeclaratorInfo parsed;
	try
	{
		template_scope = BindFunctionTemplateArguments(
			pattern, completed, parameter_offsets);
		parsed = BuildFunctionTemplateSpecializationDeclarator(
			pattern, template_scope, &spec, &member_owner);
	}
	catch (...)
	{
		if (needs_defaults)
			function_template_default_requests_.SetRequest(
				request_key, TEMPLATE_REQUEST_FAILED);
		throw;
	}
	if (CandidateSubstitutionFailed() || parsed.type == kNoType)
	{
		if (needs_defaults)
			function_template_default_requests_.SetRequest(
				request_key, TEMPLATE_REQUEST_FAILED);
		return kNoBinding;
	}
	const ScopeId exception_specification_scope = pattern.dependent_exception_specification ? parsed.trailing_return_scope : kNoScope;
	if (CandidateSubstitutionActive())
	{
		const TypeRecord& function_type = program_->types.Get(parsed.type);
		const TypeId* parameters = program_->types.Parameters(parsed.type);
		for (std::size_t i = 0; i < function_type.parameter_count; ++i)
		{
			const TypeRecord& shape = program_->types.Get(
				program_->types.RemoveTopCv(parameters[i]));
			if ((shape.kind != TYPE_NAMED ||
				 !program_->entities[shape.entity].abstract_class) &&
				!TypeContainsAbstractArrayElement(
					*program_, parameters[i], 0)) continue;
			RecordCandidateSubstitutionFailure();
			if (needs_defaults)
				function_template_default_requests_.SetRequest(
					request_key, TEMPLATE_REQUEST_FAILED);
			return kNoBinding;
		}
	}
	const BindingId binding = DeclareFunction(pattern.owner, pattern.name,
		parsed.type, parsed.parameters, pattern.defined, true,
		member_owner == kNoEntity ? spec.storage_class : STORAGE_CLASS_NONE,
		pattern.language_linkage, pattern.nonthrowing, pattern.ordinary_visible);
	const BindingId canonical_binding = program_->bindings[binding].canonical;
	if (pattern.nonthrowing) program_->bindings[canonical_binding].exception_boundary = FUNCTION_EXCEPTION_BOUNDARY_TERMINATE;
	const FunctionSignatureKey declaration_key(pattern.owner, pattern.name, GetFunction(canonical_binding).signature);
	++function_signature_lookups_;
	if (function_template_specialization_declarations_.Find(
		declaration_key) == kNoBinding)
		function_template_specialization_declarations_.Insert(
			declaration_key, canonical_binding);
	BindingRecord& binding_record = program_->bindings[binding];
	PublishFunctionTemplateInternalEmission(program_, binding, canonical_binding, completed);
	if (pattern.abi_recipe == kNoFunctionTemplateAbiRecipe ||
		pattern.abi_recipe >= program_->function_template_abi_recipes.size())
		throw std::logic_error(
			"function template specialization has no ABI recipe");
	binding_record.function_template_abi_recipe = pattern.abi_recipe;
	program_->bindings[canonical_binding].function_template_abi_recipe =
		pattern.abi_recipe;
	for (std::size_t i = 0;
		i < completed.size() && !binding_record.closure_template_specialization;
		++i)
	{
		const EntityId argument_entity = completed[i].type == kNoType ?
			kNoEntity : EntityOf(completed[i].type);
		binding_record.closure_template_specialization =
			argument_entity != kNoEntity &&
			program_->entities[argument_entity].lambda_closure;
	}
	if (member_owner != kNoEntity)
	{
		binding_record.member_owner = member_owner;
		binding_record.access = pattern.member_access;
		binding_record.static_member_function =
			pattern.static_member ||
			spec.storage_class == STORAGE_CLASS_STATIC ||
			binding_record.operator_kind == OPERATOR_NEW ||
			binding_record.operator_kind == OPERATOR_NEW_ARRAY ||
			binding_record.operator_kind == OPERATOR_DELETE ||
			binding_record.operator_kind == OPERATOR_DELETE_ARRAY;
		FunctionInfo& member_function = GetMutableFunction(binding);
		if (!binding_record.static_member_function)
			member_function.member_owner =
				program_->entities[member_owner].type;
		RegisterClassMemberFunction(member_owner, binding);
	}
	PublishFunctionTemplateSpecialMemberRole(
		pattern, binding, member_owner, parsed.type);
	if (binding_record.template_argument_count == 0)
		StoreTemplateArguments(completed,
			&binding_record.template_argument_list,
			&binding_record.template_argument_begin,
			&binding_record.template_argument_count);
	ValidateFunctionRefQualifier(binding);
	ValidateNonmemberOperator(binding);
	ApplyGenericLambdaSpecializationFacts(pattern, binding, member_owner);
	FunctionInfo& function = GetMutableFunction(binding);
	ConfigurePlaceholderFunctionReturn(binding, parsed, spec.placeholder_cv);
	function.deleted_function =
		function.deleted_function || pattern.deleted_function;
	if (pattern.dependent_exception_specification)
	{
		function.exception_specification_scope =
			exception_specification_scope;
		function.exception_specification_state =
			EXCEPTION_SPECIFICATION_DEFERRED;
	}
	const bool constexpr_specialization = spec.is_constexpr && parsed.placeholder_return_kind ==
		PLACEHOLDER_DECLARATOR_NONE &&
		IsConstexprCallableType(parsed.type, pattern.constructor_template);
	function.constexpr_function =
		function.constexpr_function || constexpr_specialization;
	function.definition_in_class = pattern.definition_in_class ||
		(member_owner != kNoEntity && pattern.lexical_scope == pattern.owner);
	PublishInlineFunctionFacts(binding,
		spec.inline_specifier || constexpr_specialization ||
		function.definition_in_class);
	function.template_pattern = static_cast<std::uint32_t>(index);
	PublishStableFunctionTemplateResultAbi(pattern, parsed.type,
		member_owner, canonical_binding);
	function.parameter_pack_name = FunctionParameterPackName(pattern.declarator);
	function.deferred = true;
	function.lexical_scope = template_scope;
	if (pattern.defined) function.definition_body = pattern.definition_body;
	PublishFunctionTemplateFriendGrants(pattern, binding);
	template_instantiations_.Insert(cache_key, binding);
	if (needs_defaults)
		function_template_default_requests_.SetRequest(
			request_key, TEMPLATE_REQUEST_SUCCEEDED, binding);
	FunctionTemplatePattern& mutable_pattern = function_templates_[index];
	if (mutable_pattern.specialization_arguments.size() >
		std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error(
			"too many function template specialization arguments");
	mutable_pattern.specialization_bindings.push_back(binding);
	mutable_pattern.specialization_argument_offsets.push_back(
		static_cast<std::uint32_t>(
			mutable_pattern.specialization_arguments.size()));
	mutable_pattern.specialization_arguments.insert(
		mutable_pattern.specialization_arguments.end(),
		completed.begin(), completed.end());
	if (!identity_offsets.empty())
	{
		if (mutable_pattern.specialization_parameter_offsets.size() >
			std::numeric_limits<std::uint32_t>::max() - parameter_offsets.size())
			throw std::runtime_error(
				"too many function template specialization partitions");
		mutable_pattern.specialization_parameter_offsets.insert(
			mutable_pattern.specialization_parameter_offsets.end(),
			parameter_offsets.begin(), parameter_offsets.end());
	}
	CompleteFunctionTemplatePlaceholderResult(index, binding, member_owner);
	return binding;
}

void SemanticAnalyzer::EnsureFunctionExceptionSpecification(BindingId binding)
{
	if (binding == kNoBinding) return;
	binding = program_->bindings[binding].canonical;
	const ExceptionSpecificationState state =
		GetFunction(binding).exception_specification_state;
	if (state == EXCEPTION_SPECIFICATION_FIXED) return;
	++function_template_exception_specification_requests_;
	if (state == EXCEPTION_SPECIFICATION_SUCCEEDED)
	{
		++function_template_exception_specification_cache_hits_;
		return;
	}
	if (state == EXCEPTION_SPECIFICATION_FAILED)
	{
		++function_template_exception_specification_cache_hits_;
		throw std::runtime_error(
			"invalid dependent function exception specification");
	}
	if (state == EXCEPTION_SPECIFICATION_IN_PROGRESS)
		throw std::runtime_error(
			"recursive dependent function exception specification");
	GetMutableFunction(binding).exception_specification_state =
		EXCEPTION_SPECIFICATION_IN_PROGRESS;
	++function_template_exception_specification_evaluations_;
	const FunctionInfo function = GetFunction(binding);
	try
	{
		if (function.inherited_constructor_source != kNoBinding &&
			function.template_pattern == kNoDumpEdge)
		{
			const BindingId source = program_->bindings[
				function.inherited_constructor_source].canonical;
			EnsureFunctionExceptionSpecification(source);
			program_->bindings[binding].nonthrowing =
				program_->bindings[source].nonthrowing;
		}
		else
		{
			if (function.template_pattern >= function_templates_.size())
				throw std::logic_error(
					"deferred exception specification has no owner");
			ScopeId scope = function.exception_specification_scope;
			if (scope == kNoScope)
			{
				scope = FunctionTemplateExceptionScope(
					function_templates_[function.template_pattern], function);
				GetMutableFunction(binding).exception_specification_scope = scope;
			}
			const bool nonthrowing = IsNonthrowing(
				function_templates_[function.template_pattern].declarator,
				scope);
			program_->bindings[binding].nonthrowing = nonthrowing;
			ConfigureFunctionExceptionSpecification(binding,
				function_templates_[function.template_pattern].declarator, scope);
		}
	}
	catch (const std::runtime_error&)
	{
		GetMutableFunction(binding).exception_specification_state =
			EXCEPTION_SPECIFICATION_FAILED;
		throw;
	}
	catch (...)
	{
		GetMutableFunction(binding).exception_specification_state =
			EXCEPTION_SPECIFICATION_DEFERRED;
		throw;
	}
	GetMutableFunction(binding).exception_specification_state =
		EXCEPTION_SPECIFICATION_SUCCEEDED;
}

bool SemanticAnalyzer::FunctionIsNonthrowing(BindingId binding)
{
	if (binding == kNoBinding) return false;
	EnsureFunctionExceptionSpecification(binding);
	return program_->bindings[program_->bindings[binding].canonical].nonthrowing;
}

void SemanticAnalyzer::RecordFunctionTemplateUsing(ScopeId owner,
	NameId name, std::size_t pattern, AccessKind access)
{
	if (pattern > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many function template patterns");
	const std::uint64_t key =
		(static_cast<std::uint64_t>(owner) << 32) | name;
	CompactIndexSequence& indexed =
		function_template_using_fact_sets_.Ensure(key);
	for (std::size_t i = 0; i < indexed.Size(); ++i)
	{
		const std::size_t fact = indexed[i];
		if (fact >= function_template_using_facts_.size())
			throw std::logic_error(
				"function template using fact index is invalid");
		if (function_template_using_facts_[fact].pattern == pattern)
			return;
	}
	if (function_template_using_facts_.size() >
		std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many function template using facts");
	indexed.Push(function_template_using_facts_.size());
	function_template_using_facts_.push_back(FunctionTemplateUsingFact(
		static_cast<std::uint32_t>(pattern), access));
}

BindingId SemanticAnalyzer::MaterializeFunctionTemplateUsing(ScopeId owner,
	NameId name, std::size_t pattern, BindingId specialization)
{
	if (specialization == kNoBinding ||
		specialization >= program_->bindings.size() ||
		pattern >= function_templates_.size())
		throw std::logic_error(
			"invalid function template using specialization");
	const std::uint64_t key =
		(static_cast<std::uint64_t>(owner) << 32) | name;
	const CompactIndexSequence* indexed =
		function_template_using_fact_sets_.Find(key);
	if (!indexed) return specialization;
	const FunctionTemplateUsingFact* selected = 0;
	for (std::size_t i = 0; i < indexed->Size(); ++i)
	{
		const std::size_t fact = (*indexed)[i];
		if (fact >= function_template_using_facts_.size())
			throw std::logic_error(
				"function template using fact index is invalid");
		if (function_template_using_facts_[fact].pattern == pattern)
		{
			selected = &function_template_using_facts_[fact];
			break;
		}
	}
	if (!selected) return specialization;
	const FunctionInfo& function = GetFunction(specialization);
	const FunctionSignatureKey signature_key(owner, name, function.signature);
	++function_signature_lookups_;
	if (function_template_specialization_declarations_.Find(
		signature_key) != kNoBinding)
		return kNoBinding;
	++function_signature_lookups_;
	if (function_declarations_.Find(signature_key) != kNoBinding)
		return kNoBinding;
	++function_signature_lookups_;
	const BindingId old = using_function_declarations_.Find(signature_key);
	if (old != kNoBinding) return old;
	const BindingId alias = program_->AddBinding(owner, BIND_FUNCTION, name,
		function.type, false, 0, NAMED_NONE, 0, specialization);
	PublishUsingAccess(alias, specialization, selected->access);
	CompactIndexSequence& aliases = function_sets_.Ensure(key);
	CompactIndexSequence& ordinary_aliases =
		ordinary_function_sets_.Ensure(key);
	aliases.Push(alias);
	ordinary_aliases.Push(alias);
	IndexEnumOperatorCandidate(alias);
	using_function_declarations_.Insert(signature_key, alias);
	return alias;
}

}
}
