#include "pa12_semantic_detail.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

TypeId RetainedIntegralLiteralType(const SyntaxArena& arena, NodeId syntax,
	Program* program, std::int64_t* value)
{
	FundamentalType type = FT_VOID;
	std::uint64_t retained_value = 0;
	if (!program || !value ||
		!arena.ScalarLiteralFact(syntax, &type, &retained_value))
		return kNoType;
	FundamentalKind kind = FUND_VOID;
	switch (type)
	{
	case FT_SIGNED_CHAR: kind = FUND_SIGNED_CHAR; break;
	case FT_SHORT_INT: kind = FUND_SHORT_INT; break;
	case FT_INT: kind = FUND_INT; break;
	case FT_LONG_INT: kind = FUND_LONG_INT; break;
	case FT_LONG_LONG_INT: kind = FUND_LONG_LONG_INT; break;
	case FT_UNSIGNED_CHAR: kind = FUND_UNSIGNED_CHAR; break;
	case FT_UNSIGNED_SHORT_INT: kind = FUND_UNSIGNED_SHORT_INT; break;
	case FT_UNSIGNED_INT: kind = FUND_UNSIGNED_INT; break;
	case FT_UNSIGNED_LONG_INT: kind = FUND_UNSIGNED_LONG_INT; break;
	case FT_UNSIGNED_LONG_LONG_INT: kind = FUND_UNSIGNED_LONG_LONG_INT; break;
	case FT_WCHAR_T: kind = FUND_WCHAR_T; break;
	case FT_CHAR: kind = FUND_CHAR; break;
	case FT_CHAR16_T: kind = FUND_CHAR16_T; break;
	case FT_CHAR32_T: kind = FUND_CHAR32_T; break;
	case FT_BOOL: kind = FUND_BOOL; break;
	default: return kNoType;
	}
	*value = static_cast<std::int64_t>(retained_value);
	return program->types.Fundamental(kind);
}

bool SyntaxUsesTemplateParameter(const SyntaxArena& arena, NodeId node,
	const std::unordered_set<NameId>& names)
{
	if (names.count(arena.SemanticPayloadId(node)) != 0) return true;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
		if (SyntaxUsesTemplateParameter(
			arena, arena.EdgeChild(edge), names)) return true;
	return false;
}

bool SyntaxUsesTemplateParameter(const SyntaxArena& arena, NodeId node,
	const std::unordered_set<NameId>& local_names,
	const std::unordered_set<NameId>* enclosing_names)
{
	if (local_names.count(arena.SemanticPayloadId(node)) != 0 ||
		(enclosing_names &&
		 enclosing_names->count(arena.SemanticPayloadId(node)) != 0))
		return true;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
		if (SyntaxUsesTemplateParameter(arena, arena.EdgeChild(edge),
			local_names, enclosing_names)) return true;
	return false;
}

bool TypeIdNamesDependentQualifiedType(const SyntaxArena& arena,
	NodeId type_id, const std::unordered_set<NameId>& names)
{
	NodeId specifiers = kNoNode;
	for (std::uint32_t edge = arena.FirstEdge(type_id); edge != kNoEdge;
		edge = arena.NextEdge(edge))
		if (arena.IsTag(arena.EdgeChild(edge), "type-specifier-seq"))
		{
			specifiers = arena.EdgeChild(edge);
			break;
		}
	for (std::uint32_t edge = specifiers == kNoNode ? kNoEdge :
		arena.FirstEdge(specifiers); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId specifier = arena.EdgeChild(edge);
		NodeId structure = kNoNode;
		for (std::uint32_t child = arena.FirstEdge(specifier);
			child != kNoEdge; child = arena.NextEdge(child))
			if (arena.IsTag(arena.EdgeChild(child), "structured-type-name"))
			{
				structure = arena.EdgeChild(child);
				break;
			}
		if (structure == kNoNode) continue;
		std::vector<NodeId> components;
		for (std::uint32_t component = arena.FirstEdge(structure);
			component != kNoEdge; component = arena.NextEdge(component))
			if (arena.IsTag(arena.EdgeChild(component), "name-component"))
				components.push_back(arena.EdgeChild(component));
		for (std::size_t component = 0;
			component + 1 < components.size(); ++component)
			if (SyntaxUsesTemplateParameter(
				arena, components[component], names)) return true;
	}
	return false;
}

std::string ExplicitArgumentPresentation(const Program& program,
	const TemplateArgument& argument)
{
	if (argument.kind == TEMPLATE_ARGUMENT_TYPE ||
		argument.kind == TEMPLATE_ARGUMENT_TEMPLATE)
	{
		std::string result = program.RenderType(argument.type);
		const char* prefixes[] = {"struct ", "class ", "union ", "enum "};
		for (std::size_t prefix = 0; prefix < 4; ++prefix)
		{
			const std::size_t length =
				std::char_traits<char>::length(prefixes[prefix]);
			if (result.compare(0, length, prefixes[prefix]) == 0)
			{
				result.erase(0, length);
				break;
			}
		}
		return result;
	}
	if (argument.value_binding != kNoBinding)
	{
		if (argument.value_binding >= program.bindings.size())
			throw std::logic_error("template argument binding is invalid");
		const BindingRecord& binding =
			program.bindings[argument.value_binding];
		std::string result = program.names.Get(binding.qualified_name != 0 ?
			binding.qualified_name : binding.name);
		const TypeRecord& type = program.types.Get(
			program.types.RemoveTopCv(argument.type));
		if (type.kind == TYPE_POINTER) result.insert(result.begin(), '&');
		return result;
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

std::string ExplicitClassSpecializationName(const Program& program,
	NameId primary, const std::vector<TemplateArgument>& arguments)
{
	std::string source = program.names.Get(primary) + "<";
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		if (i != 0) source += ", ";
		source += ExplicitArgumentPresentation(program, arguments[i]);
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

}

void SemanticAnalyzer::ResetClassTemplateSpecializationDefinition(
	BindingId specialization)
{
	if (specialization == kNoBinding ||
		specialization >= program_->bindings.size())
		throw std::logic_error("invalid class specialization reset");
	const EntityId entity = EntityOf(program_->bindings[specialization].type);
	if (entity == kNoEntity)
		throw std::logic_error("class specialization reset has no entity");
	program_->ResetClassDefinition(entity);
	if (entity < entity_data_members_.size())
		entity_data_members_[entity].clear();
	if (entity < entity_static_data_members_.size())
		entity_static_data_members_[entity].clear();
	if (entity < entity_layout_members_.size())
		entity_layout_members_[entity].clear();
	if (entity < entity_constructors_.size())
		entity_constructors_[entity].clear();
	if (entity < entity_conversion_functions_.size())
		entity_conversion_functions_[entity].clear();
	if (entity < entity_member_functions_.size())
		entity_member_functions_[entity].clear();
	if (entity < class_polymorphism_.size())
		class_polymorphism_[entity] = ClassPolymorphismFacts();
	if (entity < class_special_members_.size())
		class_special_members_[entity] = ClassSpecialMemberFacts();
	if (entity < implicit_constructor_by_entity_.size())
		implicit_constructor_by_entity_[entity] = kNoBinding;
	if (entity < entity_destructor_by_entity_.size())
		entity_destructor_by_entity_[entity] = kNoBinding;
	if (entity < hidden_friend_anchor_by_entity_.size())
		hidden_friend_anchor_by_entity_[entity] = kNoBinding;
	if (entity < deferred_class_definition_by_entity_.size())
		deferred_class_definition_by_entity_[entity] = kNoNode;
	if (entity < deferred_class_scope_by_entity_.size())
		deferred_class_scope_by_entity_[entity] = kNoScope;
	if (entity < default_constructor_demand_states_.size())
		default_constructor_demand_states_[entity] = 0;
	if (specialization < class_template_specialization_states_.size())
		class_template_specialization_states_[specialization] = 0;
	if (specialization < class_template_member_definition_counts_.size())
		class_template_member_definition_counts_[specialization] = 0;
	if (specialization <
		class_template_demanded_member_definition_counts_.size())
		class_template_demanded_member_definition_counts_[specialization] = 0;
}

bool SemanticAnalyzer::AnalyzeExplicitTemplateSpecialization(
	NodeId target, ScopeId scope, AccessKind)
{
	NodeId declarator = FindChild(target, "declarator");
	if (arena_->IsTag(target, "simple-declaration"))
	{
		const NodeId list = FindChild(target, "init-declarator-list");
		const NodeId item = list == kNoNode ? kNoNode :
			FirstSemanticChild(list);
		declarator = item == kNoNode ? kNoNode :
			FindChild(item, "declarator");
	}
	const NodeId structure =
		arena_->IsTag(target, "class-specifier") ||
		arena_->IsTag(target, "class-forward-declaration") ?
		FindChild(target, "structured-type-name") :
		declarator == kNoNode ? kNoNode :
		DeclaratorNameStructure(declarator);
	// A concrete out-of-class member specialization names its class template-id
	// before the terminal member.  Complete that canonical owner once and feed
	// the declaration through the ordinary member-definition boundary.
	std::vector<NodeId> components;
	NamePath structured_path;
	if (structure != kNoNode)
	{
		structured_path.global =
			FindChild(structure, "global-qualifier") != kNoNode;
		for (std::uint32_t edge = arena_->FirstEdge(structure);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (!arena_->IsTag(child, "name-component")) continue;
			components.push_back(child);
			structured_path.Push(program_->names.UseInterned(
				arena_->SemanticPayloadId(child)));
		}
	}
	const bool member_class_template_specialization =
		(arena_->IsTag(target, "class-specifier") ||
		 arena_->IsTag(target, "class-forward-declaration")) &&
		!components.empty() && FindChild(components.back(),
			"template-type-argument-list") != kNoNode;
	ScopeId explicit_member_owner_scope = kNoScope;
	for (std::size_t component = 0;
		component + 1 < components.size(); ++component)
	{
		const NodeId list = FindChild(
			components[component], "template-type-argument-list");
		if (list == kNoNode) continue;
		NamePath primary;
		primary.global = structured_path.global;
		for (std::size_t i = 0; i <= component; ++i)
			primary.Push(structured_path[i]);
		const std::size_t pattern_index = FindClassTemplate(scope, primary);
		if (pattern_index == std::numeric_limits<std::size_t>::max())
			throw std::runtime_error(
				"explicit member specialization owner was not found");
		const ClassTemplatePattern& pattern =
			class_templates_[pattern_index];
		std::vector<NodeId> argument_syntax;
		for (std::uint32_t edge = arena_->FirstEdge(list);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
			argument_syntax.push_back(arena_->EdgeChild(edge));
		std::vector<TemplateArgument> arguments;
		if (!BuildTemplateArguments(pattern.parameters, argument_syntax,
			scope, pattern.lexical_scope, &arguments))
			throw std::runtime_error(
				"invalid explicit member specialization arguments");
		const BindingId owner = InstantiateClassTemplate(
			pattern_index, arguments);
		const EntityId entity = owner == kNoBinding ? kNoEntity :
			EntityOf(program_->bindings[owner].type);
		if (entity == kNoEntity || !program_->entities[entity].complete ||
			program_->entities[entity].member_scope == kNoScope)
			throw std::runtime_error(
				"explicit member specialization has incomplete owner");
		explicit_member_owner_scope = program_->entities[entity].member_scope;
		if (owner < class_template_explicit_specialization_states_.size() &&
			class_template_explicit_specialization_states_[owner] != 0 &&
			!member_class_template_specialization)
			throw std::runtime_error(
				"member of an explicit class specialization has a template header");
		if (member_class_template_specialization) continue;
		const ScopeId definition_scope = NewScope(
			program_->entities[entity].member_scope,
			SCOPE_TEMPLATE_PARAMETERS, 0,
			ScopePrefixId(program_->entities[entity].member_scope));
		SpecInfo member_spec;
		if (arena_->IsTag(target, "special-member-declaration") ||
			arena_->IsTag(target, "special-member-definition"))
			member_spec.type = program_->types.Fundamental(FUND_VOID);
		else
		{
			const NodeId specifiers = FindChild(target, "decl-specifier-seq");
			if (specifiers == kNoNode)
				throw std::runtime_error(
					"explicit member specialization has no declared type");
			member_spec = BuildSpecifiers(specifiers, definition_scope,
				std::string(), true);
		}
		const EntityId previous_class = current_class_context_;
		current_class_context_ = entity;
		DeclaratorInfo parsed = BuildDeclarator(
			declarator, member_spec.type, definition_scope);
		current_class_context_ = previous_class;
		if (!program_->types.IsFunction(parsed.type))
		{
			if (!arena_->IsTag(target, "simple-declaration"))
				throw std::runtime_error(
					"explicit member specialization is not a function");
			AnalyzeSimple(target, definition_scope, root_, false, true, true);
			const LookupResult specialized = program_->LookupDirect(
				program_->entities[entity].member_scope, parsed.name,
				LOOKUP_ORDINARY);
			if (specialized.ordinary == kNoBinding)
				throw std::logic_error(
					"explicit static member specialization lost its binding");
			const BindingId canonical =
				program_->bindings[specialized.ordinary].canonical;
			if (explicit_static_member_specialization_states_.size() <= canonical)
				explicit_static_member_specialization_states_.resize(
					static_cast<std::size_t>(canonical) + 1, 0);
			explicit_static_member_specialization_states_[canonical] = 1;
			const NodeId item = FirstSemanticChild(
				FindChild(target, "init-declarator-list"));
			if (item == kNoNode || FindChild(item, "initializer") == kNoNode)
				program_->bindings[canonical].explicit_instantiation_suppressed =
					true;
			return true;
		}

		std::vector<BindingId> candidates;
		NamePath terminal_base;
		std::vector<NodeId> terminal_arguments;
		const bool terminal_template_id = CollectExplicitTemplateArguments(
			structure, &terminal_base, &terminal_arguments);
		if (terminal_template_id)
			candidates = FunctionCandidates(scope,
				program_->names.Get(structured_path.Last()), 0, structure);
		else
		{
			const LookupResult found = LookupStructuredName(
				structure, scope, LOOKUP_ORDINARY);
			if (found.ordinary != kNoBinding &&
				program_->bindings[found.ordinary].kind == BIND_FUNCTION)
				for (std::size_t i = 0; i < found.OrdinaryCount(); ++i)
					AppendFunctionSet(found.OrdinaryAt(i), &candidates);
		}
		const std::vector<BindingId> template_candidates =
			FunctionTemplateTargetCandidates(scope,
				program_->names.Get(structured_path.Last()), parsed.type,
				structure);
		candidates.insert(candidates.end(), template_candidates.begin(),
			template_candidates.end());
		BindingId selected = kNoBinding;
		for (std::size_t i = 0; i < candidates.size(); ++i)
		{
			const BindingId candidate =
				program_->bindings[candidates[i]].canonical;
			if (GetFunction(candidate).type != parsed.type) continue;
			if (selected != kNoBinding && selected != candidate)
				throw std::runtime_error(
					"ambiguous explicit member specialization");
			selected = candidate;
		}
		if (selected == kNoBinding)
			throw std::runtime_error(
				"explicit member specialization target was not found");
		if (function_explicit_specialization_states_.size() <= selected)
			function_explicit_specialization_states_.resize(
				static_cast<std::size_t>(selected) + 1, 0);
		std::uint8_t& state =
			function_explicit_specialization_states_[selected];
		const bool target_definition =
			arena_->IsTag(target, "function-definition") ||
			arena_->IsTag(target, "special-member-definition");
		if (target_definition && (state & 2) != 0)
			throw std::runtime_error(
				"duplicate explicit member specialization definition");
		if (GetFunction(selected).demand_state >= 2)
			throw std::runtime_error(
				"explicit member specialization follows instantiation");
		state |= target_definition ? 2 : 1;
		FunctionInfo& function = GetMutableFunction(selected);
		function.explicit_specialization = true;
		std::vector<ParameterInfo> specialization_parameters = parsed.parameters;
		if (specialization_parameters.size() == function.parameters.size())
			for (std::size_t parameter = 0;
				parameter < specialization_parameters.size(); ++parameter)
				if (specialization_parameters[parameter].default_argument == kNoNode)
				{
					specialization_parameters[parameter].default_argument =
						function.parameters[parameter].default_argument;
					specialization_parameters[parameter].default_scope =
						function.parameters[parameter].default_scope;
				}
		function.parameters = specialization_parameters;
		function.defined = target_definition;
		function.deferred = true;
		function.definition_body = target_definition ?
			FindChild(target, "compound-statement") : kNoNode;
		function.constructor_initializer = target_definition ?
			FindChild(target, "ctor-initializer") : kNoNode;
		function.lexical_scope = definition_scope;
		function.constexpr_function = function.constexpr_function ||
			member_spec.is_constexpr;
		const bool nonthrowing = IsNonthrowing(declarator, definition_scope);
		program_->bindings[selected].explicit_instantiation_suppressed = false;
		program_->bindings[selected].nonthrowing = nonthrowing;
		ConfigureFunctionExceptionSpecification(
			selected, declarator, definition_scope);
		FunctionInfo& completed = GetMutableFunction(selected);
		completed.exception_specification_scope = kNoScope;
		completed.exception_specification_state =
			EXCEPTION_SPECIFICATION_FIXED;
		PublishInlineFunctionFacts(selected,
			member_spec.inline_specifier || member_spec.is_constexpr);
		return true;
	}

	NamePath primary;
	std::vector<NodeId> argument_syntax;
	const bool explicit_template_id = structure != kNoNode &&
		CollectExplicitTemplateArguments(
			structure, &primary, &argument_syntax);
	if (!explicit_template_id)
	{
		if (arena_->IsTag(target, "class-specifier") ||
			arena_->IsTag(target, "class-forward-declaration") ||
			declarator == kNoNode) return false;
		primary = DeclaratorNamePath(declarator);
		if (primary.Empty()) return false;
	}

	if (arena_->IsTag(target, "class-specifier") ||
		arena_->IsTag(target, "class-forward-declaration"))
	{
		const bool target_definition =
			(arena_->Flags(target) & SYNTAX_FLAG_DEFINITION) != 0;
		LookupResult found;
		if (member_class_template_specialization &&
			explicit_member_owner_scope != kNoScope)
		{
			NamePath terminal;
			terminal.Push(primary.Last());
			found = program_->LookupQualified(
				explicit_member_owner_scope, terminal, LOOKUP_TYPE);
		}
		else found = LookupPath(scope, primary, LOOKUP_TYPE);
		const std::size_t pattern_index =
			FindClassTemplateIndex(found, primary.Last());
		if (pattern_index == std::numeric_limits<std::size_t>::max())
			throw std::runtime_error(
				"explicit class specialization primary was not found");
		ClassTemplatePattern& pattern = class_templates_[pattern_index];
		std::vector<TemplateArgument> arguments;
		if (!BuildTemplateArguments(pattern.parameters, argument_syntax,
			scope, pattern.lexical_scope, &arguments))
			throw std::runtime_error(
				"invalid explicit class specialization arguments");
		for (std::size_t i = 0; i < arguments.size(); ++i)
			if (arguments[i].IsDependent())
				throw std::runtime_error(
					"dependent explicit class specialization");

		++template_specialization_requests_;
		const TemplateSpecializationKey key =
			CanonicalTemplateSpecializationKey(pattern_index, arguments);
		BindingId binding = class_template_instantiations_.Find(key);
		TypeId type = kNoType;
		EntityId entity = kNoEntity;
		const ScopeId specialization_scope =
			BindClassTemplateArguments(pattern, arguments);
		if (binding != kNoBinding)
		{
			++template_specialization_cache_hits_;
			type = program_->bindings[binding].type;
			entity = EntityOf(type);
			if (entity == kNoEntity)
				throw std::logic_error(
					"explicit class specialization cache has no entity");
			const BindingRecord& declaration = program_->bindings[
				program_->entities[entity].declaration];
			const NodeId key_node = FindChild(target, "class-key");
			const std::string key_text = PayloadSource(key_node);
			const NamedFlavor flavor = key_text == "struct" ? NAMED_STRUCT :
				key_text == "class" ? NAMED_CLASS :
				key_text == "union" ? NAMED_UNION : NAMED_NONE;
			if (flavor == NAMED_NONE)
				throw std::runtime_error("invalid explicit specialization class-key");
			const bool demanded =
				(binding < class_template_specialization_use_states_.size() &&
				 class_template_specialization_use_states_[binding] != 0) ||
				(binding < class_template_member_definition_demand_states_.size() &&
				 (class_template_member_definition_demand_states_[binding] & 1U) != 0) ||
				(binding < class_template_explicit_instantiation_states_.size() &&
				 class_template_explicit_instantiation_states_[binding] != 0);
			const std::uint8_t prior_explicit_state =
				binding < class_template_explicit_specialization_states_.size() ?
				class_template_explicit_specialization_states_[binding] : 0;
			if (target_definition && (prior_explicit_state & 2U) != 0)
				throw std::runtime_error(
					"duplicate explicit class specialization definition");
			if (program_->entities[entity].complete)
			{
				if (prior_explicit_state != 0 && !target_definition)
				{
					// A declaration after the explicit definition redeclares the
					// same specialization; it does not replace its member graph.
				}
				else if (demanded)
					throw std::runtime_error(
						"explicit specialization follows completed instantiation");
				else ResetClassTemplateSpecializationDefinition(binding);
			}
			if (target_definition && !program_->entities[entity].complete)
				CompleteClassDefinition(target, specialization_scope, type, entity,
					flavor, pattern.owner, declaration.name, declaration.name,
					pattern.owner, pattern.name, program_->names.Intern(
						ExplicitClassSpecializationName(
							*program_, pattern.name, arguments)));
		}
		else
		{
			const std::string name = ExplicitClassSpecializationName(
				*program_, pattern.name, arguments);
			// Publish the canonical specialization shell before analyzing its
			// members.  In particular, an injected primary name used by a member
			// declaration must already route back to this template pattern.
			type = AnalyzeClass(target, specialization_scope, std::string(),
				false, name, pattern.owner, pattern.name, false,
				program_->names.Intern(name));
			entity = EntityOf(type);
			if (entity == kNoEntity ||
				program_->entities[entity].declaration == kNoBinding)
				throw std::logic_error(
					"explicit class specialization has no declaration");
			binding = program_->entities[entity].declaration;
			StoreTemplateArguments(arguments,
				&program_->entities[entity].template_argument_list,
				&program_->entities[entity].template_argument_begin,
				&program_->entities[entity].template_argument_count);
			if (class_template_pattern_by_entity_.size() <= entity)
				class_template_pattern_by_entity_.resize(
					static_cast<std::size_t>(entity) + 1, kNoDumpEdge);
			class_template_pattern_by_entity_[entity] =
				static_cast<std::uint32_t>(pattern_index);
			class_template_instantiations_.Insert(key, binding);
			pattern.specialization_bindings.push_back(binding);
			(void)AnalyzeClass(target, specialization_scope, std::string(),
				false, name, pattern.owner, pattern.name, target_definition,
				program_->names.Intern(name));
		}
		if (program_->entities[entity].template_argument_begin == kNoBinding)
			StoreTemplateArguments(arguments,
				&program_->entities[entity].template_argument_list,
				&program_->entities[entity].template_argument_begin,
				&program_->entities[entity].template_argument_count);
		program_->entities[entity].template_argument_pack_begin =
			HasTrailingTemplateParameterPack(pattern.parameters) ?
				static_cast<std::uint32_t>(
					FixedTemplateParameterCount(pattern.parameters)) :
				kNoTemplateParameter;
		if (class_template_pattern_by_entity_.size() <= entity)
			class_template_pattern_by_entity_.resize(
				static_cast<std::size_t>(entity) + 1, kNoDumpEdge);
		class_template_pattern_by_entity_[entity] =
			static_cast<std::uint32_t>(pattern_index);
		class_template_instantiations_.Insert(key, binding);
		if (std::find(pattern.specialization_bindings.begin(),
			pattern.specialization_bindings.end(), binding) ==
			pattern.specialization_bindings.end())
			pattern.specialization_bindings.push_back(binding);
		if (class_template_specialization_states_.size() <= binding)
			class_template_specialization_states_.resize(
				static_cast<std::size_t>(binding) + 1, 0);
		if (class_template_partial_selections_.size() <= binding)
			class_template_partial_selections_.resize(
				static_cast<std::size_t>(binding) + 1);
		class_template_partial_selections_[binding] =
			ClassTemplatePartialSelection();
		class_template_specialization_states_[binding] = 2;
		if (class_template_explicit_specialization_states_.size() <= binding)
			class_template_explicit_specialization_states_.resize(
				static_cast<std::size_t>(binding) + 1, 0);
		class_template_explicit_specialization_states_[binding] |=
			target_definition ? 2U : 1U;
		program_->entities[entity].explicit_template_specialization = true;
		return true;
	}

	if (declarator == kNoNode) return false;
	const NodeId specifiers = FindChild(target, "decl-specifier-seq");
	if (specifiers == kNoNode) return false;
	ScopeId specialization_semantic_scope = scope;
	if (structure != kNoNode && structured_path.Size() > 1)
	{
		const ScopeId qualified_owner = ResolveOwner(scope, structured_path);
		if (qualified_owner != kNoScope)
			specialization_semantic_scope = qualified_owner;
	}
	const EntityId previous_class_context = current_class_context_;
	const EntityId specialization_class =
		program_->EntityForScope(specialization_semantic_scope);
	if (specialization_class != kNoEntity)
		current_class_context_ = specialization_class;
	const SpecInfo spec = BuildSpecifiers(
		specifiers, specialization_semantic_scope, std::string(), true);
	const DeclaratorInfo parsed = BuildDeclarator(
		declarator, spec.type, specialization_semantic_scope);
	current_class_context_ = previous_class_context;
	if (!program_->types.IsFunction(parsed.type)) return false;
	BindingId selected = kNoBinding;
	if (!explicit_template_id)
	{
		const NodeId identifier = FindChild(declarator, "identifier");
		const NodeId name_syntax = structure != kNoNode ? structure : identifier;
		if (name_syntax == kNoNode) return false;
		const std::vector<BindingId> candidates =
			FunctionTemplateTargetCandidates(scope,
				program_->names.Get(primary.Last()), parsed.type, name_syntax);
		for (std::size_t i = 0; i < candidates.size(); ++i)
		{
			const BindingId candidate =
				program_->bindings[candidates[i]].canonical;
			if (GetFunction(candidate).type != parsed.type) continue;
			if (selected != kNoBinding && selected != candidate)
				throw std::runtime_error(
					"ambiguous explicit function specialization");
			selected = candidate;
		}
	}
	else
	{
		const std::vector<std::size_t> patterns =
			FindFunctionTemplates(scope, primary);
		for (std::size_t i = 0; i < patterns.size(); ++i)
		{
			const FunctionTemplatePattern& pattern =
				function_templates_[patterns[i]];
			std::vector<TemplateArgument> arguments;
			if (!BuildTemplateArguments(pattern.parameters, argument_syntax,
				scope, pattern.lexical_scope, &arguments)) continue;
			std::vector<std::uint32_t> offsets;
			if (!BuildFunctionTemplateArgumentOffsets(
				pattern.parameters, arguments.size(), &offsets)) continue;
			const BindingId candidate = InstantiateFunctionTemplate(
				patterns[i], arguments, offsets);
			if (candidate == kNoBinding) continue;
			TypeId specialization_type = parsed.type;
			if (spec.is_constexpr)
				specialization_type = ApplyConstexprMemberFunctionType(
					specialization_type,
					program_->bindings[candidate].member_owner,
					program_->bindings[candidate].static_member_function);
			if (GetFunction(candidate).type != specialization_type) continue;
			if (selected != kNoBinding && selected != candidate)
				throw std::runtime_error(
					"ambiguous explicit function specialization");
			selected = candidate;
		}
	}
	if (selected == kNoBinding)
		throw std::runtime_error(
			"explicit function specialization primary was not found");
	FunctionInfo& function = GetMutableFunction(selected);
	if (function_explicit_specialization_states_.size() <= selected)
		function_explicit_specialization_states_.resize(
			static_cast<std::size_t>(selected) + 1, 0);
	std::uint8_t& specialization_state =
		function_explicit_specialization_states_[selected];
	const bool target_definition =
		arena_->IsTag(target, "function-definition");
	if (target_definition && (specialization_state & 2) != 0)
		throw std::runtime_error(
			"duplicate explicit function specialization definition");
	if (function.demand_state >= 2)
		throw std::runtime_error(
			"explicit function specialization follows instantiation");
	specialization_state |= target_definition ? 2 : 1;
	function.explicit_specialization = true;
	if (spec.is_constexpr)
		ValidateConstexprCallableType(function.type, false);
	std::vector<ParameterInfo> specialization_parameters = parsed.parameters;
	if (specialization_parameters.size() == function.parameters.size())
		for (std::size_t parameter = 0;
			parameter < specialization_parameters.size(); ++parameter)
			if (specialization_parameters[parameter].default_argument == kNoNode)
			{
				specialization_parameters[parameter].default_argument =
					function.parameters[parameter].default_argument;
				specialization_parameters[parameter].default_scope =
					function.parameters[parameter].default_scope;
			}
	function.parameters = specialization_parameters;
	function.constexpr_function =
		function.constexpr_function || spec.is_constexpr;
	function.defined = target_definition;
	function.deferred = true;
	function.definition_body = FindChild(target, "compound-statement");
	function.lexical_scope = specialization_semantic_scope;
	const bool nonthrowing = IsNonthrowing(
		declarator, specialization_semantic_scope);
	program_->bindings[selected].nonthrowing = nonthrowing;
	ConfigureFunctionExceptionSpecification(
		selected, declarator, specialization_semantic_scope);
	FunctionInfo& completed = GetMutableFunction(selected);
	completed.exception_specification_scope = kNoScope;
	completed.exception_specification_state =
		EXCEPTION_SPECIFICATION_FIXED;
	program_->bindings[selected].explicit_instantiation_suppressed = false;
	PublishInlineFunctionFacts(
		selected, spec.inline_specifier || spec.is_constexpr);
	if (target_definition) DemandFunction(selected);
	ValidateFunctionRefQualifier(selected);
	ValidateNonmemberOperator(selected);
	return true;
}

std::vector<std::size_t> SemanticAnalyzer::FindVariableTemplates(
	ScopeId scope, const NamePath& path)
{
	std::vector<std::size_t> result;
	if (path.Empty()) return result;
	if (path.global || path.Size() > 1)
	{
		const ScopeId owner = ResolveOwner(scope, path);
		if (owner == kNoScope) return result;
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owner) << 32) | path.Last();
		const CompactIndexSequence* found = variable_template_sets_.Find(key);
		if (found)
			for (std::size_t i = 0; i < found->Size(); ++i)
				result.push_back((*found)[i]);
		return result;
	}
	for (ScopeId owner = scope; owner != kNoScope;
		owner = program_->ParentScope(owner))
	{
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owner) << 32) | path.Last();
		const CompactIndexSequence* found = variable_template_sets_.Find(key);
		if (!found) continue;
		for (std::size_t i = 0; i < found->Size(); ++i)
			result.push_back((*found)[i]);
		break;
	}
	if (!result.empty()) return result;
	// Keep the lexical index as the ordinary explicit-template-id fast path.
	// Enter the shared lookup graph only when a class base can own the name.
	bool inherited_lookup = false;
	for (ScopeId owner = scope; owner != kNoScope;
		owner = program_->ParentScope(owner))
	{
		if (program_->KindOfScope(owner) != SCOPE_CLASS) continue;
		const EntityId entity = program_->EntityForScope(owner);
		if (entity != kNoEntity &&
			program_->entities[entity].direct_base_count != 0)
		{
			inherited_lookup = true;
			break;
		}
	}
	if (!inherited_lookup) return result;
	const LookupResult lookup = program_->Lookup(
		scope, path, LOOKUP_VARIABLE_TEMPLATE);
	for (std::size_t owner_index = 0;
		owner_index < lookup.VariableTemplateOwnerCount(); ++owner_index)
	{
		const ScopeId owner =
			lookup.VariableTemplateOwnerAt(owner_index);
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owner) << 32) | path.Last();
		const CompactIndexSequence* found = variable_template_sets_.Find(key);
		if (found)
			for (std::size_t i = 0; i < found->Size(); ++i)
				result.push_back((*found)[i]);
	}
	return result;
}

BindingId SemanticAnalyzer::InstantiateVariableTemplate(
	NodeId syntax, ScopeId scope)
{
	NamePath path;
	std::vector<NodeId> argument_syntax;
	if (!CollectExplicitTemplateArguments(
		syntax, &path, &argument_syntax)) return kNoBinding;
	std::vector<std::size_t> related;
	const NodeId structure = FindChild(syntax, "structured-type-name");
	if (structure == kNoNode) related = FindVariableTemplates(scope, path);
	else
	{
		const LookupResult found = LookupStructuredName(
			syntax, scope, LOOKUP_VARIABLE_TEMPLATE);
		for (std::size_t owner = 0;
			owner < found.VariableTemplateOwnerCount(); ++owner)
		{
			const std::uint64_t key =
				(static_cast<std::uint64_t>(
					found.VariableTemplateOwnerAt(owner)) << 32) | path.Last();
			const CompactIndexSequence* indexed = variable_template_sets_.Find(key);
			for (std::size_t i = 0; indexed && i < indexed->Size(); ++i)
				related.push_back((*indexed)[i]);
		}
	}
	std::size_t primary_index = variable_templates_.size();
	for (std::size_t i = 0; i < related.size(); ++i)
		if (!variable_templates_[related[i]].partial_specialization)
		{
			primary_index = related[i];
			break;
		}
	if (primary_index == variable_templates_.size()) return kNoBinding;
	const VariableTemplatePattern& primary = variable_templates_[primary_index];
	std::vector<TemplateArgument> arguments;
	if (!BuildTemplateArguments(primary.parameters, argument_syntax,
		scope, primary.lexical_scope, &arguments)) return kNoBinding;

	++template_specialization_requests_;
	const TemplateSpecializationKey key =
		CanonicalTemplateSpecializationKey(primary_index, arguments);
	BindingId cached = variable_template_instantiations_.Find(key);
	if (cached != kNoBinding)
	{
		++template_specialization_cache_hits_;
		return cached;
	}

	std::size_t selected_index = primary_index;
	FunctionTemplateDeduction selected_bindings(primary.parameters);
	const std::size_t primary_fixed =
		FixedTemplateParameterCount(primary.parameters);
	for (std::size_t argument = 0;
		argument < arguments.size() && argument < primary_fixed; ++argument)
		selected_bindings.fixed_arguments[argument] = arguments[argument];
	if (HasTrailingTemplateParameterPack(primary.parameters))
		selected_bindings.pack_arguments.back().assign(
			arguments.begin() + primary_fixed, arguments.end());
	std::vector<std::size_t> matches;
	for (std::size_t candidate_ordinal = 0;
		candidate_ordinal < related.size(); ++candidate_ordinal)
	{
		const std::size_t candidate_index = related[candidate_ordinal];
		VariableTemplatePattern& candidate =
			variable_templates_[candidate_index];
		if (!candidate.partial_specialization) continue;
		++template_partial_candidates_;
		if (!MaterializeTemplatePartialArguments(primary.parameters,
			candidate.parameters, candidate.specialization_arguments,
			candidate.lexical_scope,
			&candidate.canonical_specialization_arguments,
			&candidate.canonical_argument_state)) continue;
		FunctionTemplateDeduction bindings(candidate.parameters);
		if (!MatchTemplatePartialArguments(candidate.parameters,
			candidate.canonical_specialization_arguments,
			arguments, &bindings)) continue;
		matches.push_back(candidate_index);
	}
	if (!matches.empty())
	{
		std::size_t winner = matches.size();
		for (std::size_t i = 0; i < matches.size(); ++i)
		{
			bool best = true;
			for (std::size_t j = 0; j < matches.size(); ++j)
			{
				if (i == j) continue;
				const VariableTemplatePattern& left =
					variable_templates_[matches[i]];
				const VariableTemplatePattern& right =
					variable_templates_[matches[j]];
				if (CompareTemplatePartialPatterns(left.parameters,
					left.canonical_specialization_arguments,
					right.parameters,
					right.canonical_specialization_arguments) <= 0)
				{
					best = false;
					break;
				}
			}
			if (best) winner = i;
		}
		if (matches.size() != 1 && winner == matches.size())
			throw std::runtime_error(
				"ambiguous variable template partial specialization");
		selected_index = matches[matches.size() == 1 ? 0 : winner];
		const VariableTemplatePattern& selected_partial =
			variable_templates_[selected_index];
		if (!MatchTemplatePartialArguments(selected_partial.parameters,
			selected_partial.canonical_specialization_arguments,
			arguments, &selected_bindings))
			throw std::logic_error(
				"selected variable partial no longer matches");
	}

	const VariableTemplatePattern& selected =
		variable_templates_[selected_index];
	const ScopeId substitution_scope = NewScope(selected.lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(selected.lexical_scope));
	if (selected.parameters.size() !=
		selected_bindings.fixed_arguments.size())
		throw std::logic_error(
			"variable template substitution shape is invalid");
	for (std::size_t parameter = 0;
		parameter < selected.parameters.size(); ++parameter)
		if (selected.parameters[parameter].pack)
			BindTemplateArgumentPack(substitution_scope,
				selected.parameters[parameter],
				selected_bindings.pack_arguments[parameter], 0,
				selected_bindings.pack_arguments[parameter].size());
		else BindTemplateArgument(substitution_scope,
			selected.parameters[parameter],
			selected_bindings.fixed_arguments[parameter]);
	const SpecInfo spec = BuildSpecifiers(selected.specifiers,
		substitution_scope, std::string(), true);
	DeclaratorInfo parsed = BuildDeclarator(selected.declarator,
		spec.type, substitution_scope);
	if (program_->types.IsFunction(parsed.type))
		throw std::runtime_error("variable template declares a function");
	if (spec.is_constexpr)
		parsed.type = program_->types.Qualify(parsed.type, CV_CONST);
	if (spec.is_constexpr && !IsConstexprLiteralType(parsed.type))
		throw std::runtime_error(
			"constexpr variable template does not have literal type");
	const BindingId binding = program_->AddUnindexedBinding(
		selected.owner, BIND_VARIABLE, primary.name, parsed.type);
	if (variable_template_bindings_.size() <= binding)
		variable_template_bindings_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	variable_template_bindings_[binding] = 1;
	BindingRecord& record = program_->bindings[binding];
	record.storage_class = spec.storage_class;
	record.member_owner = program_->EntityForScope(selected.owner);
	record.weak_odr = true;
	record.variable_template_specialization = true;
	StoreTemplateArguments(arguments,
		&record.template_argument_list,
		&record.template_argument_begin, &record.template_argument_count);
	if (record.member_owner != kNoEntity)
	{
		const NameId specialization_name = program_->names.Intern(
			ExplicitClassSpecializationName(*program_, primary.name, arguments));
		record.qualified_name = EmissionName(selected.owner, specialization_name);
	}
	else record.qualified_name = EmissionName(selected.owner, primary.name);
	bool dependent = false;
	for (std::size_t i = 0; i < arguments.size(); ++i)
		if (arguments[i].IsDependent() ||
			((arguments[i].kind == TEMPLATE_ARGUMENT_TYPE ||
			  arguments[i].kind == TEMPLATE_ARGUMENT_TEMPLATE) &&
			 FunctionTemplateTypeIsDependent(arguments[i].type)))
			dependent = true;
	if (!dependent)
	{
		if (selected.initializer == kNoNode)
			throw std::runtime_error(
				"variable template specialization has no initializer");
		ExpressionInfo initializer =
			AnalyzeConstantAwareVariableInitializer(selected.initializer,
				substitution_scope, parsed.type, false, spec.is_constexpr, true);
		PublishVariableInitializer(binding, parsed.type, spec,
			initializer, false);
		PublishCanonicalBindingConstant(binding);
		const std::uint32_t object = ExpressionObject(initializer);
		if (spec.is_constexpr && object != kNoConstexprObject &&
			IsClassObjectType(parsed.type))
			initializer = MaterializeConstexprObject(object, parsed.type);
		if (IsClassObjectType(parsed.type) ||
			!program_->bindings[binding].constant)
		{
			const std::uint32_t variable = MakeDump(DUMP_VARIABLE,
				parsed.type, VALUE_NONE, primary.name, binding);
			dump_.Add(variable, initializer.node);
			dump_.Add(root_, variable);
			if (record.member_owner != kNoEntity)
			{
				if (static_member_storage_by_binding_.size() <= binding)
					static_member_storage_by_binding_.resize(
						static_cast<std::size_t>(binding) + 1, kNoDumpEdge);
				static_member_storage_by_binding_[binding] = variable;
			}
			RegisterVariableLifetimeAndStorage(selected.owner, false, false,
				variable, binding, parsed.type, 0, 0, 0, 0, 0,
				HasConstantInitializerFact(initializer));
		}
	}
	variable_template_instantiations_.Insert(key, binding);
	return binding;
}

bool SemanticAnalyzer::IsNonTypeTemplateParameterType(TypeId type) const
{
	type = program_->types.RemoveTopCv(type);
	const TypeRecord& record = program_->types.Get(type);
	return record.kind == TYPE_POINTER ||
		record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_MEMBER_POINTER || IsIntegral(type, true);
}

bool SemanticAnalyzer::FormNonTypeTemplateArgumentValue(
	ExpressionInfo expression, TemplateArgument* argument)
{
	if (!argument || argument->kind != TEMPLATE_ARGUMENT_INTEGRAL)
		throw std::logic_error("invalid non-type template argument destination");
	const TypeId type = program_->types.RemoveTopCv(argument->type);
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind != TYPE_POINTER && record.kind != TYPE_LVALUE_REFERENCE &&
		record.kind != TYPE_MEMBER_POINTER)
	{
		if (!expression.constant || !IsIntegral(expression.type, true))
			return false;
		argument->value = FunctionTemplateTypeIsDependent(type) ?
			expression.value : NormalizeIntegralConstant(type, expression.value);
		return true;
	}
	if (record.kind == TYPE_MEMBER_POINTER)
	{
		if (!expression.constant) return false;
		if (expression.binding != kNoBinding &&
			expression.binding >= program_->bindings.size()) return false;
		argument->value = expression.value;
		argument->value_binding = expression.binding == kNoBinding ?
			kNoBinding : program_->bindings[expression.binding].canonical;
		return expression.binding == kNoBinding ||
			argument->value_binding < program_->bindings.size();
	}
	std::uint32_t address = record.kind == TYPE_LVALUE_REFERENCE ?
		expression.constexpr_lvalue_address : ExpressionAddress(expression);
	if (address == kNoConstexprAddress &&
		record.kind == TYPE_LVALUE_REFERENCE)
		address = LvalueAddress(&expression);
	if (address == kNoConstexprAddress && record.kind == TYPE_POINTER &&
		expression.constant && expression.value == 0)
		address = NullConstexprAddress();
	const ConstexprAddressValue* value = ConstexprAddressAt(address);
	if (!value || value->offset != 0 ||
		(value->kind != CONSTEXPR_ADDRESS_NULL &&
		 value->kind != CONSTEXPR_ADDRESS_BINDING &&
		 value->kind != CONSTEXPR_ADDRESS_FUNCTION) ||
		(record.kind == TYPE_LVALUE_REFERENCE &&
		 value->kind == CONSTEXPR_ADDRESS_NULL)) return false;
	if (value->kind != CONSTEXPR_ADDRESS_NULL)
	{
		if (value->identity >= program_->bindings.size())
			throw std::logic_error("template argument address binding is invalid");
		const BindingId source = program_->bindings[
			static_cast<BindingId>(value->identity)].canonical;
		for (ScopeId owner = program_->bindings[source].owner;
			owner != kNoScope; owner = program_->ParentScope(owner))
		{
			if (program_->KindOfScope(owner) == SCOPE_FUNCTION) return false;
			if (program_->KindOfScope(owner) == SCOPE_NAMESPACE) break;
		}
		argument->value_binding = source;
		if (value->kind == CONSTEXPR_ADDRESS_FUNCTION)
			DemandFunction(source);
	}
	argument->value = 0;
	return true;
}

void SemanticAnalyzer::ParseTemplateParameters(NodeId list, ScopeId scope,
	std::vector<TemplateParameter>* parameters,
	std::vector<NameId>* names, std::vector<NodeId>* defaults,
	const std::unordered_set<NameId>* enclosing_dependent_names)
{
	std::unordered_set<NameId> visible_local_names;
	ParseTemplateParametersWithDependentNames(list, scope, parameters,
		names, defaults, &visible_local_names, enclosing_dependent_names);
}

void SemanticAnalyzer::ParseTemplateParametersWithDependentNames(
	NodeId list, ScopeId scope,
	std::vector<TemplateParameter>* parameters,
	std::vector<NameId>* names, std::vector<NodeId>* defaults,
	std::unordered_set<NameId>* visible_local_names,
	const std::unordered_set<NameId>* enclosing_dependent_names)
{
	std::vector<NameId> introduced_names;
	if (list == kNoNode) return;
	for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId parameter = arena_->EdgeChild(edge);
		TemplateParameter record;
		record.default_argument =
			FindChild(parameter, "default-template-argument");
		record.pack = FindChild(parameter, "parameter-pack") != kNoNode;
		if (arena_->IsTag(parameter, "type-parameter"))
		{
			if (FindChild(parameter, "template-template-parameter") != kNoNode)
			{
				record.kind = TEMPLATE_ARGUMENT_TEMPLATE;
				const NodeId nested_clause = FindChild(
					parameter, "template-parameter-clause");
				const NodeId nested_list = nested_clause == kNoNode ? kNoNode :
					FindChild(nested_clause, "template-parameter-list");
				std::vector<NameId> nested_names;
				std::vector<NodeId> nested_defaults;
				ParseTemplateParametersWithDependentNames(nested_list, scope,
					&record.template_parameters, &nested_names,
					&nested_defaults, visible_local_names,
					enclosing_dependent_names);
			}
			const NodeId identifier = FindChild(parameter, "identifier");
			record.name = identifier == kNoNode ? 0 :
				program_->names.Intern(arena_->Payload(identifier));
		}
		else if (arena_->IsTag(parameter, "non-type-template-parameter"))
		{
			record.kind = TEMPLATE_ARGUMENT_INTEGRAL;
			record.specifiers = FindChild(parameter, "decl-specifier-seq");
			record.declarator = FindChild(parameter, "declarator");
			record.pack = record.pack || (record.declarator != kNoNode &&
				arena_->HasDescendantTag(record.declarator, "parameter-pack"));
			record.name = record.declarator == kNoNode ? 0 :
				DeclaratorName(record.declarator);
			record.dependent_type = SyntaxUsesTemplateParameter(
				*arena_, record.specifiers, *visible_local_names,
				enclosing_dependent_names) ||
				(record.declarator != kNoNode &&
				 SyntaxUsesTemplateParameter(*arena_, record.declarator,
					*visible_local_names, enclosing_dependent_names));
			if (!record.dependent_type)
			{
				const SpecInfo spec = BuildSpecifiers(record.specifiers,
					scope, std::string(), record.declarator != kNoNode);
				record.value_type = record.declarator == kNoNode ? spec.type :
					BuildDeclarator(record.declarator, spec.type, scope).type;
				record.value_type = AdjustParameterType(record.value_type);
				if (FunctionTemplateTypeIsDependent(record.value_type))
				{
					record.dependent_type = true;
					record.value_type = kNoType;
				}
				else if (!IsNonTypeTemplateParameterType(record.value_type))
					throw std::runtime_error(
						"invalid non-type template parameter type");
			}
		}
		else throw std::runtime_error(
			"template-template parameters are outside PA20");
		parameters->push_back(record);
		names->push_back(record.name);
		defaults->push_back(record.default_argument);
		if (record.name != 0 &&
			visible_local_names->insert(record.name).second)
			introduced_names.push_back(record.name);
	}
	for (std::size_t i = 0; i < introduced_names.size(); ++i)
		visible_local_names->erase(introduced_names[i]);
}

bool SemanticAnalyzer::CollectExplicitTemplateArguments(NodeId syntax,
	NamePath* base, std::vector<NodeId>* arguments)
{
	if (syntax == kNoNode) return false;
	const NodeId structure = arena_->IsTag(syntax, "structured-type-name") ?
		syntax : FindChild(syntax, "structured-type-name");
	if (structure == kNoNode) return false;
	NodeId terminal = kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(structure); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, "name-component")) terminal = child;
	}
	if (terminal == kNoNode) return false;
	const NodeId list = FindChild(terminal, "template-type-argument-list");
	if (list == kNoNode) return false;
	*base = StructuredNamePath(structure);
	arguments->clear();
	for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		arguments->push_back(arena_->EdgeChild(edge));
	return true;
}

TypeId SemanticAnalyzer::ResolveTemplateParameterType(
	const TemplateParameter& parameter, ScopeId parameter_scope)
{
	if (parameter.kind != TEMPLATE_ARGUMENT_INTEGRAL)
		throw std::logic_error("type requested for a type template parameter");
	if (!parameter.dependent_type) return parameter.value_type;
	const SpecInfo spec = BuildSpecifiers(parameter.specifiers, parameter_scope,
		std::string(), parameter.declarator != kNoNode);
	if (CandidateSubstitutionFailed()) return kNoType;
	TypeId type = parameter.declarator == kNoNode ? spec.type :
		BuildDeclarator(parameter.declarator, spec.type, parameter_scope).type;
	if (CandidateSubstitutionFailed()) return kNoType;
	if (type == kNoType && CandidateSubstitutionActive())
	{
		RecordCandidateSubstitutionFailure();
		return kNoType;
	}
	type = AdjustParameterType(type);
	if (!IsNonTypeTemplateParameterType(type) &&
		!FunctionTemplateTypeIsDependent(type))
		throw std::runtime_error(
			"invalid non-type template parameter type");
	return type;
}

void SemanticAnalyzer::BindTemplateArgument(ScopeId scope,
	const TemplateParameter& parameter, const TemplateArgument& argument)
{
	if (parameter.name == 0) return;
	if (parameter.kind != argument.kind)
		throw std::logic_error("template parameter/argument kind mismatch");
	if (argument.kind == TEMPLATE_ARGUMENT_TYPE ||
		argument.kind == TEMPLATE_ARGUMENT_TEMPLATE)
		program_->AddBinding(scope, BIND_TYPE_ALIAS, parameter.name,
			argument.type);
	else if (argument.IsDependent())
		program_->AddBinding(scope, BIND_PARAMETER, parameter.name,
			argument.type, false, argument.dependent_parameter);
	else
	{
		const BindingId binding = program_->AddBinding(scope, BIND_PARAMETER,
			parameter.name, argument.type, true, argument.value);
		const TypeId type = program_->types.RemoveTopCv(argument.type);
		const TypeRecord& shape = program_->types.Get(type);
		if (shape.kind == TYPE_MEMBER_POINTER)
		{
			PublishBindingScalar(binding, ConstexprScalarValue(
				argument.value_binding, argument.value));
			return;
		}
		if (shape.kind != TYPE_POINTER &&
			shape.kind != TYPE_LVALUE_REFERENCE) return;
		std::uint32_t address = kNoConstexprAddress;
		if (argument.value_binding == kNoBinding)
			address = NullConstexprAddress();
		else
		{
			if (argument.value_binding >= program_->bindings.size())
				throw std::logic_error(
					"template argument binding identity is invalid");
			const BindingRecord& source =
				program_->bindings[argument.value_binding];
			const bool function = source.kind == BIND_FUNCTION;
			const TypeRecord& storage = program_->types.Get(
				program_->types.RemoveTopCv(EffectiveType(source.type)));
			const std::int64_t extent = function ||
				(storage.kind == TYPE_ARRAY && storage.bound == 0) ? 0 :
				static_cast<std::int64_t>(
					program_->SizeOf(EffectiveType(source.type)));
			address = InternConstexprAddress(ConstexprAddressValue(
				function ? CONSTEXPR_ADDRESS_FUNCTION :
					CONSTEXPR_ADDRESS_BINDING,
				argument.value_binding, 0, 0, extent));
		}
		PublishBindingAddress(binding, address);
	}
}

void SemanticAnalyzer::BindTemplateArgumentPack(ScopeId scope,
	const TemplateParameter& parameter,
	const std::vector<TemplateArgument>& arguments, std::size_t first,
	std::size_t last)
{
	if (first > last || last > arguments.size())
		throw std::logic_error("template argument pack range is invalid");
	if (parameter.name == 0) return;
	const std::uint64_t key =
		(static_cast<std::uint64_t>(scope) << 32) | parameter.name;
	CompactIndexSequence& values = template_argument_pack_bindings_.Ensure(key);
	if (values.Size() != 0)
		throw std::logic_error("template argument pack rebound in one scope");
	for (std::size_t i = first; i < last; ++i)
	{
		if (arguments[i].kind != parameter.kind)
			throw std::logic_error("template argument pack kind mismatch");
		values.Push(template_argument_pack_values_.size());
		template_argument_pack_values_.push_back(arguments[i]);
	}
}

bool SemanticAnalyzer::LookupTemplateArgumentPack(ScopeId scope, NameId name,
	std::vector<TemplateArgument>* arguments) const
{
	for (ScopeId current = scope; current != kNoScope;
		current = program_->ParentScope(current))
	{
		const std::uint64_t key =
			(static_cast<std::uint64_t>(current) << 32) | name;
		const CompactIndexSequence* values =
			template_argument_pack_bindings_.Find(key);
		if (!values) continue;
		arguments->clear();
		arguments->reserve(values->Size());
		for (std::size_t i = 0; i < values->Size(); ++i)
		{
			const std::size_t index = (*values)[i];
			if (index >= template_argument_pack_values_.size())
				throw std::logic_error("template argument pack storage is invalid");
			arguments->push_back(template_argument_pack_values_[index]);
		}
		return true;
	}
	return false;
}

bool SemanticAnalyzer::LookupFunctionParameterPack(ScopeId scope, NameId name,
	std::vector<BindingId>* bindings) const
{
	for (ScopeId current = scope; current != kNoScope;
		current = program_->ParentScope(current))
	{
		const std::uint64_t key =
			(static_cast<std::uint64_t>(current) << 32) | name;
		const CompactIndexSequence* values =
			function_parameter_pack_bindings_.Find(key);
		if (!values) continue;
		bindings->clear();
		bindings->reserve(values->Size());
		for (std::size_t i = 0; i < values->Size(); ++i)
			bindings->push_back(static_cast<BindingId>((*values)[i]));
		return true;
	}
	return false;
}

bool SemanticAnalyzer::AppendTemplateArgument(
	const std::vector<TemplateParameter>& parameters, NodeId source,
	ScopeId source_scope, ScopeId parameter_scope,
	const std::unordered_set<NameId>* source_dependent_names,
	bool has_pack, std::size_t fixed,
	std::vector<TemplateArgument>* arguments)
{
	if (arguments->size() >= fixed && !has_pack) return false;
	const TemplateParameter& parameter =
		TemplateParameterForArgument(parameters, arguments->size());
	if (source == kNoNode)
		throw std::runtime_error("empty template argument");
	TemplateArgument argument;
	argument.kind = parameter.kind;
	const bool retained_dependent = source_dependent_names != 0 &&
		(SyntaxUsesTemplateParameter(
			*arena_, source, *source_dependent_names) ||
		 (parameter.kind == TEMPLATE_ARGUMENT_INTEGRAL &&
		  (SyntaxUsesTemplateParameter(*arena_, parameter.specifiers,
			*source_dependent_names) ||
		   (parameter.declarator != kNoNode && SyntaxUsesTemplateParameter(
			*arena_, parameter.declarator, *source_dependent_names))))) &&
		!IsDirectTemplateParameterExpression(
			source, *source_dependent_names);
	if (parameter.kind == TEMPLATE_ARGUMENT_TYPE)
	{
		NodeId type_id = arena_->IsTag(source, "type-id") ? source :
			FindChild(source, "type-id");
		if (type_id != kNoNode)
			argument.type = BuildCanonicalTemplateTypeArgument(
				type_id, source_scope, source_dependent_names);
		else if (arena_->IsTag(source, "id-expression"))
		{
			const NodeId structure = FindChild(
				source, "structured-type-name");
			const LookupResult found = structure != kNoNode ?
				LookupStructuredName(source, source_scope, LOOKUP_TYPE) :
				LookupSpelling(source_scope, PayloadSource(source), LOOKUP_TYPE);
			argument.type = found.type;
		}
		else return false;
		if (argument.type == kNoType) return false;
	}
	else if (parameter.kind == TEMPLATE_ARGUMENT_TEMPLATE)
	{
		if (!BuildTemplateTemplateArgument(
			source, source_scope, parameter_scope, parameter, &argument))
		{
			if (!retained_dependent) return false;
			argument.type = ClassTemplateNondeducedTypeShape();
			argument.dependent_parameter = kNondeducedTemplateParameter;
		}
	}
	else
	{
		if (retained_dependent)
		{
			argument.type = FunctionTemplateNondeducedTypeShape();
			argument.dependent_parameter = kNondeducedTemplateParameter;
			argument.source_value_type = RetainedIntegralLiteralType(
				*arena_, source, program_, &argument.value);
			arguments->push_back(argument);
			if (arguments->size() <= fixed)
				BindTemplateArgument(parameter_scope, parameter, argument);
			return true;
		}
		argument.type = ResolveTemplateParameterType(parameter, parameter_scope);
		if (CandidateSubstitutionFailed() || argument.type == kNoType)
			return false;
		const bool dependent_target =
			FunctionTemplateTypeIsDependent(argument.type);
		ExpressionInfo expression;
		if (arena_->IsTag(source, "type-id"))
		{
			const NodeId specifiers = FindChild(source, "type-specifier-seq");
			const NodeId name = specifiers == kNoNode ? kNoNode :
				FirstSemanticChild(specifiers);
			const NodeId declarator = FindChild(source, "abstract-declarator");
			const NodeId retained_declarator = declarator != kNoNode ?
				declarator : FindChild(source, "declarator");
			const NodeId call_clause = retained_declarator == kNoNode ?
				kNoNode : FindChild(retained_declarator, "parameter-clause");
			const bool retained_pack_name = declarator != kNoNode &&
				FindChild(declarator, "parameter-pack") != kNoNode;
			if (name != kNoNode && arena_->IsTag(name, "type-name") &&
				retained_declarator == kNoNode)
			{
				const NodeId structure = FindChild(name, "structured-type-name");
				const LookupResult known_type = structure == kNoNode ?
					LookupSpelling(source_scope, PayloadSource(name), LOOKUP_TYPE) :
					LookupStructuredName(name, source_scope, LOOKUP_TYPE);
				if (known_type.type != kNoType) return false;
			}
			if (name != kNoNode && arena_->IsTag(name, "type-name") &&
				call_clause != kNoNode &&
				FirstSemanticChild(call_clause) == kNoNode)
			{
				// The parser's declaration/expression ambiguity preserves
				// `qualified_template_id()` as a type-id with an empty function
				// declarator. In a non-type argument this is a call expression.
				const std::vector<NodeId> no_argument_syntax;
				const std::vector<ExpressionInfo> no_arguments;
				++constant_expression_required_depth_;
				bool formed = false;
				try
				{
					formed = AnalyzeRetainedNamedCall(name,
						PayloadSource(name), source_scope,
						no_argument_syntax, no_arguments,
						argument.type, &expression);
				}
				catch (...)
				{
					--constant_expression_required_depth_;
					throw;
				}
				--constant_expression_required_depth_;
				if (!formed) return false;
			}
			else if (name != kNoNode && arena_->IsTag(name, "type-name") &&
				(declarator == kNoNode || retained_pack_name))
				expression = AnalyzeNamedValue(PayloadSource(name),
					source_scope,
					dependent_target ? kNoType : argument.type, name);
			else if (name != kNoNode &&
				arena_->IsTag(name, "decltype-specifier") &&
				FindChild(source, "abstract-declarator") == kNoNode)
			{
				const TypeId qualifier = program_->types.RemoveTopCv(
					EffectiveType(DecltypeType(
						FirstSemanticChild(name), source_scope)));
				EnsureClassDefinition(qualifier);
				const ScopeId carrier = program_->ScopeForType(qualifier);
				const NodeId qualified = FindChild(name, "qualified-type-name");
				const LookupResult found = carrier == kNoScope ||
					qualified == kNoNode ? LookupResult() :
					LookupStructuredName(qualified, carrier, LOOKUP_ORDINARY);
				if (found.ordinary == kNoBinding ||
					!program_->bindings[found.ordinary].constant) return false;
				expression = MakeLiteral(
					program_->bindings[found.ordinary].type,
					InternNumber(program_->bindings[found.ordinary].value));
				expression.constant = true;
				expression.value = program_->bindings[found.ordinary].value;
			}
			else return false;
		}
		else
		{
			++constant_expression_required_depth_;
			try
			{
				expression = AnalyzeExpression(source, source_scope,
					dependent_target ? kNoType : argument.type);
			}
			catch (...)
			{
				--constant_expression_required_depth_;
				throw;
			}
			--constant_expression_required_depth_;
		}
		if (CandidateSubstitutionFailed()) return false;
		if (!expression.constant && expression.binding != kNoBinding &&
			expression.binding < program_->bindings.size() &&
			program_->bindings[expression.binding].kind == BIND_PARAMETER &&
			!program_->bindings[expression.binding].constant &&
			program_->KindOfScope(
				program_->bindings[expression.binding].owner) ==
				SCOPE_TEMPLATE_PARAMETERS)
		{
			const std::int64_t parameter_index =
				program_->bindings[expression.binding].value;
			if (parameter_index < 0 ||
				static_cast<std::uint64_t>(parameter_index) >=
				kNoTemplateParameter)
				throw std::logic_error(
					"dependent template argument index is invalid");
			argument.dependent_parameter =
				static_cast<std::uint32_t>(parameter_index);
		}
		else if (!FormNonTypeTemplateArgumentValue(expression, &argument))
		{
			if (CandidateSubstitutionActive())
			{
				RecordCandidateSubstitutionFailure();
				return false;
			}
			throw std::runtime_error(
				"non-type template argument is not an integral constant: " +
				PayloadSource(source));
		}
	}
	arguments->push_back(argument);
	if (arguments->size() <= fixed)
		BindTemplateArgument(parameter_scope, parameter, argument);
	return true;
}

bool SemanticAnalyzer::BuildTemplateArguments(
	const std::vector<TemplateParameter>& parameters,
	const std::vector<NodeId>& syntax, ScopeId use_scope,
	ScopeId lexical_scope, std::vector<TemplateArgument>* arguments,
	bool require_complete, const std::unordered_set<NameId>* dependent_names)
{
	const bool has_pack = HasTrailingTemplateParameterPack(parameters);
	const std::size_t fixed = FixedTemplateParameterCount(parameters);
	arguments->clear();
	arguments->reserve(std::max(parameters.size(), syntax.size()));
	const ScopeId parameter_scope = NewScope(lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(lexical_scope));
	const auto forwarded_symbolic_elements = [&](NodeId source,
		ScopeId source_scope, std::size_t count,
		std::vector<std::uint8_t>* symbolic)
	{
		symbolic->assign(count, 0);
		std::vector<NameId> names;
		CollectPackExpansionNames(source, source_scope, &names);
		for (std::size_t name = 0; name < names.size(); ++name)
		{
			std::vector<TemplateArgument> pack;
			if (!LookupTemplateArgumentPack(
				source_scope, names[name], &pack)) continue;
			if (pack.size() != count)
				throw std::logic_error(
					"forwarded template pack length changed during expansion");
			for (std::size_t element = 0; element < count; ++element)
				(*symbolic)[element] |= pack[element].pack_expansion ? 1U : 0U;
		}
	};
	const auto append_argument = [&](NodeId source, ScopeId source_scope,
		const std::unordered_set<NameId>* source_dependent_names) -> bool
	{
		return AppendTemplateArgument(parameters, source, source_scope,
			parameter_scope, source_dependent_names, has_pack, fixed, arguments);
	};
	for (std::size_t i = 0; i < syntax.size(); ++i)
	{
		if (arena_->IsTag(syntax[i], "pack-expansion-expression"))
		{
			const NodeId operand = FirstSemanticChild(syntax[i]);
			if (operand == kNoNode)
				throw std::runtime_error("empty template argument pack expansion");
			std::vector<ScopeId> element_scopes;
			if (!ExpandPackElementScopes(
				operand, use_scope, &element_scopes))
			{
				if (CandidateSubstitutionFailed()) return false;
				if (!append_argument(
					operand, use_scope, dependent_names)) return false;
				arguments->back().pack_expansion = true;
				continue;
			}
			std::vector<std::uint8_t> symbolic;
			forwarded_symbolic_elements(
				operand, use_scope, element_scopes.size(), &symbolic);
			for (std::size_t element = 0;
				element < element_scopes.size(); ++element)
			{
				if (arguments->size() >= fixed && !has_pack) return false;
				if (!append_argument(operand, element_scopes[element],
					dependent_names)) return false;
				arguments->back().pack_expansion = symbolic[element] != 0;
			}
			continue;
		}
		NodeId type_id = arena_->IsTag(syntax[i], "type-id") ? syntax[i] :
			FindChild(syntax[i], "type-id");
		NodeId declarator = type_id == kNoNode ? kNoNode :
			FindChild(type_id, "abstract-declarator");
		if (declarator == kNoNode && type_id != kNoNode)
			declarator = FindChild(type_id, "declarator");
		const bool expansion = declarator != kNoNode &&
			FindChild(declarator, "parameter-pack") != kNoNode;
		if (!expansion)
		{
			if (!append_argument(
				syntax[i], use_scope, dependent_names)) return false;
			continue;
		}
		const TemplateArgumentKind destination_kind =
			TemplateParameterForArgument(parameters, arguments->size()).kind;
		std::vector<ScopeId> element_scopes;
		if (!ExpandPackElementScopes(type_id, use_scope, &element_scopes))
		{
			if (CandidateSubstitutionFailed()) return false;
			// A retained pack has one symbolic exemplar. The concrete
			// specialization later replays the same syntax against its ordered
			// pack environment. This also covers a non-type pack parsed as the
			// ambiguous type-id spelling `Name...`.
			if (!append_argument(
				syntax[i], use_scope, dependent_names)) return false;
			arguments->back().pack_expansion = true;
			continue;
		}
		std::vector<std::uint8_t> symbolic;
		forwarded_symbolic_elements(
			type_id, use_scope, element_scopes.size(), &symbolic);
		for (std::size_t element = 0; element < element_scopes.size(); ++element)
		{
			if (arguments->size() >= fixed && !has_pack) return false;
			const TemplateParameter& destination =
				TemplateParameterForArgument(parameters, arguments->size());
			if (destination.kind != destination_kind) return false;
			if (destination_kind == TEMPLATE_ARGUMENT_TYPE)
			{
				TemplateArgument argument(TEMPLATE_ARGUMENT_TYPE,
					BuildTypeId(type_id, element_scopes[element]));
				if (argument.type == kNoType) return false;
				argument.pack_expansion = symbolic[element] != 0;
				arguments->push_back(argument);
				if (arguments->size() <= fixed)
					BindTemplateArgument(parameter_scope, destination, argument);
			}
			else
			{
				if (!append_argument(syntax[i], element_scopes[element],
					dependent_names))
					return false;
				arguments->back().pack_expansion = symbolic[element] != 0;
			}
		}
	}
	std::unordered_set<NameId> default_dependent_names;
	if (dependent_names)
		default_dependent_names.insert(
			dependent_names->begin(), dependent_names->end());
	for (std::size_t prior = 0; prior < arguments->size() &&
		prior < parameters.size(); ++prior)
	{
		const TemplateArgument& prior_argument = (*arguments)[prior];
		const bool dependent = prior_argument.IsDependent() ||
			((prior_argument.kind == TEMPLATE_ARGUMENT_TYPE ||
			  prior_argument.kind == TEMPLATE_ARGUMENT_TEMPLATE) &&
			 prior_argument.type != kNoType &&
			 FunctionTemplateTypeIsDependent(prior_argument.type));
		if (dependent && parameters[prior].name != 0)
			default_dependent_names.insert(parameters[prior].name);
	}
	while (require_complete && arguments->size() < fixed)
	{
		const std::size_t parameter_index = arguments->size();
		const TemplateParameter& parameter = parameters[parameter_index];
		NodeId source = parameter.default_argument;
		if (source == kNoNode) return false;
		source = FirstSemanticChild(source);
		const std::unordered_set<NameId>* default_names =
			default_dependent_names.empty() ? 0 : &default_dependent_names;
		if (!append_argument(
			source, parameter_scope, default_names)) return false;
		const TemplateArgument& appended = (*arguments)[parameter_index];
		const bool dependent = appended.IsDependent() ||
			((appended.kind == TEMPLATE_ARGUMENT_TYPE ||
			  appended.kind == TEMPLATE_ARGUMENT_TEMPLATE) &&
			 appended.type != kNoType &&
			 FunctionTemplateTypeIsDependent(appended.type));
		if (dependent && parameter.name != 0)
			default_dependent_names.insert(parameter.name);
	}
	if (require_complete && !has_pack &&
		arguments->size() != parameters.size()) return false;
	if (has_pack && arguments->size() >= fixed)
		BindTemplateArgumentPack(parameter_scope, parameters.back(),
			*arguments, fixed, arguments->size());
	return true;
}

TypeId SemanticAnalyzer::BuildCanonicalTemplateTypeArgument(NodeId type_id,
	ScopeId source_scope,
	const std::unordered_set<NameId>* dependent_names)
{
	const bool source_dependent = dependent_names != 0 &&
		SyntaxUsesTemplateParameter(*arena_, type_id, *dependent_names);
	const bool nondeduced = source_dependent &&
		TypeIdNamesDependentQualifiedType(
			*arena_, type_id, *dependent_names);
	if (!nondeduced)
	{
		if (source_dependent) candidate_substitution_failures_.push_back(0);
		TypeId result = kNoType;
		try
		{
			// A template argument needs canonical type identity, not the layout
			// of every class specialization named inside that identity.
			++class_template_completion_suppressed_depth_;
			try
			{
				result = BuildTypeId(type_id, source_scope);
			}
			catch (...)
			{
				--class_template_completion_suppressed_depth_;
				throw;
			}
			--class_template_completion_suppressed_depth_;
		}
		catch (...)
		{
			if (source_dependent)
				candidate_substitution_failures_.pop_back();
			throw;
		}
		if (!source_dependent) return result;
		const bool substitution_failed = CandidateSubstitutionFailed();
		candidate_substitution_failures_.pop_back();
		if (!substitution_failed && result != kNoType) return result;
	}
	// A dependent partial argument is only a shape at declaration time.
	// Failure to form it against synthetic shape parameters does not reject the
	// declaration; selection replays the retained syntax after deduction.
	return ClassTemplateNondeducedTypeShape();
}

TypeId SemanticAnalyzer::ClassTemplateNondeducedTypeShape()
{
	if (class_template_nondeduced_type_shape_ == kNoType)
	{
		const NameId name = program_->names.Intern(
			"__class_template_nondeduced_type_shape");
		const EntityId entity = program_->NewEntity(name,
			NAMED_TYPENAME_PARAMETER, false, kNoType,
			program_->GlobalScope(), name);
		class_template_nondeduced_type_shape_ = program_->types.Named(entity);
	}
	return class_template_nondeduced_type_shape_;
}

std::vector<TemplateArgument> SemanticAnalyzer::TypeTemplateArguments(
	const std::vector<TypeId>& arguments) const
{
	std::vector<TemplateArgument> result;
	result.reserve(arguments.size());
	for (std::size_t i = 0; i < arguments.size(); ++i)
		result.push_back(TemplateArgument(
			TEMPLATE_ARGUMENT_TYPE, arguments[i]));
	return result;
}

std::vector<TemplateArgument> SemanticAnalyzer::StoredTemplateArguments(
	std::size_t first, std::size_t count) const
{
	if (first > program_->template_arguments.size() ||
		count > program_->template_arguments.size() - first)
		throw std::logic_error("stored template argument range is invalid");
	std::vector<TemplateArgument> result;
	result.reserve(count);
	for (std::size_t i = 0; i < count; ++i)
		result.push_back(StoredTemplateArgument(first + i));
	return result;
}

TemplateArgument SemanticAnalyzer::StoredTemplateArgument(
	std::size_t index) const
{
	if (index >= program_->template_arguments.size())
		throw std::logic_error("stored template argument index is invalid");
	return index < program_->canonical_template_arguments.size() ?
		program_->canonical_template_arguments[index] : TemplateArgument(
			TEMPLATE_ARGUMENT_TYPE, program_->template_arguments[index]);
}

TemplateSpecializationKey SemanticAnalyzer::CanonicalTemplateSpecializationKey(
	std::size_t pattern, const std::vector<TemplateArgument>& arguments)
{
	return TemplateSpecializationKey(pattern,
		program_->InternTemplateArgumentList(arguments));
}

TemplateSpecializationKey SemanticAnalyzer::CanonicalTemplateSpecializationKey(
	std::size_t pattern, const std::vector<TemplateArgument>& arguments,
	const std::vector<std::uint32_t>& parameter_offsets)
{
	return TemplateSpecializationKey(pattern,
		program_->InternTemplateArgumentList(arguments),
		template_argument_partitions_.Intern(parameter_offsets));
}

void SemanticAnalyzer::StoreTemplateArguments(
	const std::vector<TemplateArgument>& arguments,
	TemplateArgumentListId* identity, std::uint32_t* first,
	std::uint32_t* count)
{
	if (!identity || !first || !count)
		throw std::logic_error("template argument owner is incomplete");
	*identity = program_->InternTemplateArgumentList(arguments, first, count);
}

bool SemanticAnalyzer::ClassTemplateHasNonTypeParameter(EntityId entity) const
{
	if (entity >= class_template_pattern_by_entity_.size()) return false;
	const std::uint32_t pattern = class_template_pattern_by_entity_[entity];
	if (pattern == kNoDumpEdge || pattern >= class_templates_.size()) return false;
	for (std::size_t i = 0; i < class_templates_[pattern].parameters.size(); ++i)
		if (class_templates_[pattern].parameters[i].kind ==
			TEMPLATE_ARGUMENT_INTEGRAL)
			return true;
	return false;
}

}
}
