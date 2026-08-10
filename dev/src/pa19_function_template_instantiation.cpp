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

std::size_t TemplateParameterOrdinal(
	const std::vector<TemplateParameter>& parameters, NameId name)
{
	if (name == 0) return parameters.size();
	for (std::size_t i = 0; i < parameters.size(); ++i)
		if (parameters[i].name == name) return i;
	return parameters.size();
}

bool EquivalentNormalizedTemplateSyntax(const SyntaxArena& arena,
	NodeId left, NodeId right,
	const std::vector<TemplateParameter>& left_parameters,
	const std::vector<TemplateParameter>& right_parameters)
{
	// A dependent non-type parameter type is not always safe to materialize
	// against an incomplete shape type. Compare the retained parsed structure
	// once at declaration insertion, normalizing parameter names to ordinals.
	if (left == kNoNode || right == kNoNode) return left == right;
	std::vector<std::pair<NodeId, NodeId> > pending(
		1, std::make_pair(left, right));
	while (!pending.empty())
	{
		const NodeId left_node = pending.back().first;
		const NodeId right_node = pending.back().second;
		pending.pop_back();
		if (arena.Tag(left_node) != arena.Tag(right_node)) return false;
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
			if (arena.IsTag(left_node, "decl-specifier"))
				for (std::uint32_t edge = arena.FirstEdge(left_node);
					edge != kNoEdge; edge = arena.NextEdge(edge))
					if (arena.IsTag(
						arena.EdgeChild(edge), "structured-type-name"))
					{
						structured_wrapper = true;
						break;
					}
			if (!structured_wrapper && (left_name != right_name ||
				arena.Payload(left_node) != arena.Payload(right_node))) return false;
		}
		std::uint32_t left_edge = arena.FirstEdge(left_node);
		std::uint32_t right_edge = arena.FirstEdge(right_node);
		while (left_edge != kNoEdge && right_edge != kNoEdge)
		{
			pending.push_back(std::make_pair(
				arena.EdgeChild(left_edge), arena.EdgeChild(right_edge)));
			left_edge = arena.NextEdge(left_edge);
			right_edge = arena.NextEdge(right_edge);
		}
		if (left_edge != right_edge) return false;
	}
	return true;
}

bool EquivalentFunctionTemplateParameterLists(const SyntaxArena& arena,
	const std::vector<TemplateParameter>& left,
	const std::vector<TemplateParameter>& right)
{
	if (left.size() != right.size()) return false;
	for (std::size_t i = 0; i < left.size(); ++i)
	{
		if (left[i].kind != right[i].kind || left[i].pack != right[i].pack)
			return false;
		if (left[i].kind == TEMPLATE_ARGUMENT_TEMPLATE &&
			!EquivalentFunctionTemplateParameterLists(arena,
				left[i].template_parameters, right[i].template_parameters))
			return false;
		if (left[i].kind != TEMPLATE_ARGUMENT_INTEGRAL) continue;
		if (left[i].dependent_type != right[i].dependent_type) return false;
		if (!left[i].dependent_type)
		{
			if (left[i].value_type != right[i].value_type) return false;
			continue;
		}
		if (!EquivalentNormalizedTemplateSyntax(arena,
				left[i].specifiers, right[i].specifiers, left, right) ||
			!EquivalentNormalizedTemplateSyntax(arena,
				left[i].declarator, right[i].declarator, left, right)) return false;
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
	std::uint32_t left_edge = NextDependentResultEdge(arena,
		left.specifiers == kNoNode ? kNoEdge : arena.FirstEdge(left.specifiers));
	std::uint32_t right_edge = NextDependentResultEdge(arena,
		right.specifiers == kNoNode ? kNoEdge : arena.FirstEdge(right.specifiers));
	while (left_edge != kNoEdge && right_edge != kNoEdge)
	{
		if (!EquivalentNormalizedTemplateSyntax(arena,
			arena.EdgeChild(left_edge), arena.EdgeChild(right_edge),
			left.parameters, right.parameters)) return false;
		left_edge = NextDependentResultEdge(
			arena, arena.NextEdge(left_edge));
		right_edge = NextDependentResultEdge(
			arena, arena.NextEdge(right_edge));
	}
	if (left_edge != right_edge) return false;
	return EquivalentNormalizedTemplateSyntax(arena,
		left.trailing_return_syntax, right.trailing_return_syntax,
		left.parameters, right.parameters);
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

void SemanticAnalyzer::AppendConstructorTemplateCandidates(
	TypeId initialized_type, const std::vector<ExpressionInfo>& arguments,
	std::vector<BindingId>* candidates)
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
	DeduceFunctionTemplatePatterns(patterns, arguments, &specializations);
	for (std::size_t i = 0; i < specializations.size(); ++i)
		if (std::find(candidates->begin(), candidates->end(),
			specializations[i]) == candidates->end())
			candidates->push_back(specializations[i]);
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

void SemanticAnalyzer::RegisterFunctionTemplatePattern(NodeId target,
	ScopeId scope, AccessKind member_access,
	const std::vector<TemplateParameter>& parameters, NodeId specifiers,
	NodeId declarator, bool definition, bool special_member_template,
	TypeId dependent_result_shape, bool dependent_exception_specification)
{
	const NamePath path = DeclaratorNamePath(declarator);
	if (path.Empty())
		throw std::runtime_error("function template has no name");
	FunctionTemplatePattern pattern;
	pattern.lexical_scope = scope;
	pattern.name = path.Last();
	pattern.specifiers = specifiers;
	pattern.declarator = declarator;
	pattern.definition_body = definition ?
		FindChild(target, "compound-statement") : kNoNode;
	pattern.constructor_initializer = definition ?
		FindChild(target, "ctor-initializer") : kNoNode;
	pattern.parameters = parameters;
	pattern.language_linkage = current_language_linkage_;
	pattern.member_access = member_access;
	pattern.defined = definition;
	pattern.conversion_template = special_member_template &&
		FindChild(declarator, "conversion-type-id") != kNoNode;
	pattern.constructor_template = special_member_template &&
		!pattern.conversion_template;
	pattern.dependent_exception_specification =
		dependent_exception_specification;
	const bool explicit_member_definition = definition &&
		explicit_member_template_replay_depth_ != 0;
	pattern.explicit_member_definition = explicit_member_definition;
	bool friend_syntax = false;
	for (std::uint32_t edge = specifiers == kNoNode ? kNoEdge :
		arena_->FirstEdge(specifiers); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (PayloadSource(arena_->EdgeChild(edge)) == "friend")
			friend_syntax = true;
	if (!friend_syntax)
	{
		ScopeId structured_owner = kNoScope;
		const NodeId structure = DeclaratorNameStructure(declarator);
		if (structure != kNoNode && (path.global || path.Size() > 1))
			(void)LookupStructuredName(structure, scope,
				LOOKUP_FUNCTION_TEMPLATE, &structured_owner);
		pattern.owner = structured_owner != kNoScope ? structured_owner :
			ResolveOwner(scope, path);
		if (pattern.owner == kNoScope)
			throw std::runtime_error("function template owner not found");
		if (program_->EntityForScope(pattern.owner) != kNoEntity)
			pattern.lexical_scope =
				TemplateLexicalScope(scope, pattern.owner);
	}
	const ScopeId shape_scope = NewScope(pattern.lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0,
		ScopePrefixId(pattern.lexical_scope));
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
	SpecInfo shape_spec;
	if (pattern.constructor_template)
		shape_spec.type = program_->types.Fundamental(FUND_VOID);
	else if (pattern.conversion_template)
		shape_spec.type = BuildTypeId(
			FindChild(declarator, "conversion-type-id"), shape_scope);
	else shape_spec = BuildSpecifiers(specifiers, shape_scope, std::string(), true, false, dependent_result_shape);
	pattern.deferred_result_formation = dependent_result_shape != kNoType && shape_spec.type == dependent_result_shape;
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
	const bool defer_trailing_return = pattern.trailing_return_syntax != kNoNode &&
		PayloadSource(pattern.trailing_return_syntax).compare(
			0, 8, "decltype") == 0;
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
	DeclaratorInfo shape_declarator = BuildDeclarator(declarator,
		shape_spec.type, shape_scope, false, nonstatic_member,
		defer_trailing_return, &parameter_names);
	current_class_context_ = previous_class;
	if (!program_->types.IsFunction(shape_declarator.type))
		throw std::runtime_error(
			"function template has non-function declaration");
	if (shape_spec.is_constexpr)
		shape_declarator.type = ApplyConstexprMemberFunctionType(
			shape_declarator.type, member_owner,
			shape_spec.storage_class == STORAGE_CLASS_STATIC);
	if (shape_spec.is_constexpr)
		ValidateConstexprCallableType(shape_declarator.type, false);
	pattern.shape_type = shape_declarator.type;
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
	{
		program_->entities[member_owner].has_user_declared_constructor = true;
		program_->entities[member_owner].is_aggregate = false;
	}
	InitializeFunctionTemplatePackShape(&pattern, shape_declarator);
	const std::uint64_t key =
		(static_cast<std::uint64_t>(pattern.owner) << 32) | pattern.name;
	const CompactIndexSequence* prior_patterns =
		template_function_sets_.Find(key);
	std::size_t prior_index = function_templates_.size();
	if (prior_patterns)
		for (std::size_t p = 0; p < prior_patterns->Size(); ++p)
		{
			const std::size_t candidate = (*prior_patterns)[p];
			const FunctionTemplatePattern& prior =
				function_templates_[candidate];
			const bool dependent_result =
				function_template_dependent_result_shape_ != kNoType &&
				program_->types.Get(prior.shape_type).child ==
					function_template_dependent_result_shape_;
			if (EquivalentFunctionTemplateParameterLists(*arena_,
					prior.parameters, pattern.parameters) &&
				prior.shape_type == pattern.shape_type &&
				EquivalentFunctionTemplateNondeducedShapes(
					*arena_, prior, pattern) &&
				(!dependent_result ||
				 EquivalentDependentFunctionTemplateResults(
					*arena_, prior, pattern)))
			{
				prior_index = candidate;
				break;
			}
		}
	if (prior_index != function_templates_.size())
	{
		FunctionTemplatePattern& prior = function_templates_[prior_index];
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
		{
			if (prior.defined && (!explicit_member_definition ||
				prior.explicit_member_definition))
				throw std::runtime_error(
					"duplicate function template definition");
			prior.lexical_scope = pattern.lexical_scope;
			prior.specifiers = pattern.specifiers;
			prior.declarator = pattern.declarator;
			prior.trailing_return_syntax = pattern.trailing_return_syntax;
			prior.definition_body = pattern.definition_body;
			prior.constructor_initializer = pattern.constructor_initializer;
			prior.function_parameter_names =
				pattern.function_parameter_names;
			prior.function_parameter_defaults =
				pattern.function_parameter_defaults;
			prior.language_linkage = pattern.language_linkage;
			prior.static_member = prior.static_member || pattern.static_member;
			prior.definition_in_class = pattern.definition_in_class;
			prior.explicit_member_definition =
				prior.explicit_member_definition || explicit_member_definition;
			if (pattern.lexical_scope == pattern.owner)
				prior.member_access = pattern.member_access;
			prior.defined = true;
			UpgradeFunctionTemplateSpecializations(prior_index);
		}
		return;
	}
	if (explicit_member_definition)
		throw std::runtime_error(
			"explicit member template definition target was not found");
	if (friend_owner != kNoEntity && qualified_friend)
		throw std::runtime_error(
			"qualified friend function template was not declared");
	const std::size_t index = function_templates_.size();
	function_templates_.push_back(pattern);
	template_function_sets_.Ensure(key).Push(index);
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
	if (semantic_owner != kNoEntity) current_class_context_ = semantic_owner;
	DeclaratorInfo parsed;
	try
	{
		if (pattern.constructor_template)
			spec->type = program_->types.Fundamental(FUND_VOID);
		else if (pattern.conversion_template)
			spec->type = BuildTypeId(
				FindChild(pattern.declarator, "conversion-type-id"),
				template_scope);
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
				template_scope, false, *member_owner != kNoEntity &&
					!pattern.static_member &&
					spec->storage_class != STORAGE_CLASS_STATIC);
	}
	catch (...)
	{
		current_class_context_ = previous_class;
		throw;
	}
	current_class_context_ = previous_class;
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
	if (spec->is_constexpr)
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
		if (function.explicit_specialization) continue;
		if (function.type != parsed.type)
			throw std::runtime_error(
				"function template definition does not match declaration");
		const bool constexpr_specialization = spec.is_constexpr &&
			IsConstexprCallableType(parsed.type, false);
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
	try
	{
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
				if (!value.constant || !IsIntegral(value.type, true))
				{
					if (CandidateSubstitutionActive())
					{
						RecordCandidateSubstitutionFailure();
						return false;
					}
					throw std::runtime_error(
						"default non-type function template argument is not constant");
				}
				argument.value = NormalizeIntegralConstant(
					argument.type, value.value);
			}
			for (std::size_t retained = 0;
				retained < default_scopes.size(); ++retained)
				if (default_scopes[retained] != kNoScope)
					bind_completed(default_scopes[retained],
						pattern.default_contexts[retained], parameter_index);
		}
	}
	catch (const std::runtime_error&)
	{
		return false;
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
	catch (const std::runtime_error&)
	{
		if (needs_defaults)
			function_template_default_requests_.SetRequest(
				request_key, TEMPLATE_REQUEST_FAILED);
		return kNoBinding;
	}
	if (CandidateSubstitutionFailed() || parsed.type == kNoType)
	{
		if (needs_defaults)
			function_template_default_requests_.SetRequest(
				request_key, TEMPLATE_REQUEST_FAILED);
		return kNoBinding;
	}
	bool nonthrowing = pattern.nonthrowing;
	try
	{
		if (pattern.dependent_exception_specification)
			nonthrowing = IsNonthrowing(pattern.declarator, template_scope);
	}
	catch (const std::runtime_error&)
	{
		if (needs_defaults)
			function_template_default_requests_.SetRequest(
				request_key, TEMPLATE_REQUEST_FAILED);
		return kNoBinding;
	}
	if (CandidateSubstitutionActive())
	{
		const TypeRecord& function_type = program_->types.Get(parsed.type);
		const TypeId* parameters = program_->types.Parameters(parsed.type);
		for (std::size_t i = 0; i < function_type.parameter_count; ++i)
		{
			const TypeRecord& shape = program_->types.Get(
				program_->types.RemoveTopCv(parameters[i]));
			if (shape.kind != TYPE_NAMED ||
				!program_->entities[shape.entity].abstract_class) continue;
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
		pattern.language_linkage, nonthrowing, pattern.ordinary_visible);
	BindingRecord& binding_record = program_->bindings[binding];
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
	FunctionInfo& function = GetMutableFunction(binding);
	const bool constexpr_specialization = spec.is_constexpr &&
		IsConstexprCallableType(parsed.type, false);
	function.constexpr_function =
		function.constexpr_function || constexpr_specialization;
	function.definition_in_class = pattern.definition_in_class ||
		(member_owner != kNoEntity && pattern.lexical_scope == pattern.owner);
	PublishInlineFunctionFacts(binding,
		spec.inline_specifier || constexpr_specialization ||
		function.definition_in_class);
	function.template_pattern = static_cast<std::uint32_t>(index);
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
	return binding;
}

}
}
