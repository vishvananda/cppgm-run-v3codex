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
	if (structure == kNoNode) return false;

	// A concrete out-of-class member specialization names its class template-id
	// before the terminal member.  Complete that canonical owner once and feed
	// the declaration through the ordinary member-definition boundary.
	std::vector<NodeId> components;
	NamePath structured_path;
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
		const ScopeId definition_scope = NewScope(
			program_->entities[entity].member_scope,
			SCOPE_TEMPLATE_PARAMETERS, 0,
			ScopePrefixId(program_->entities[entity].member_scope));
		if (arena_->IsTag(target, "simple-declaration"))
			AnalyzeSimple(target, definition_scope, root_, false, true, true);
		else if (arena_->IsTag(target, "function-definition"))
			AnalyzeFunction(target, definition_scope, root_, true);
		else throw std::runtime_error(
			"unsupported explicit member specialization");
		return true;
	}

	NamePath primary;
	std::vector<NodeId> argument_syntax;
	if (!CollectExplicitTemplateArguments(
		structure, &primary, &argument_syntax)) return false;

	if (arena_->IsTag(target, "class-specifier") ||
		arena_->IsTag(target, "class-forward-declaration"))
	{
		const LookupResult found = LookupPath(scope, primary, LOOKUP_TYPE);
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
		const TemplateSpecializationKey key(pattern_index, arguments);
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
			if (program_->entities[entity].complete)
				throw std::runtime_error(
					"explicit specialization follows completed instantiation");
			const BindingRecord& declaration = program_->bindings[
				program_->entities[entity].declaration];
			const NodeId key_node = FindChild(target, "class-key");
			const std::string key_text = PayloadSource(key_node);
			const NamedFlavor flavor = key_text == "struct" ? NAMED_STRUCT :
				key_text == "class" ? NAMED_CLASS :
				key_text == "union" ? NAMED_UNION : NAMED_NONE;
			if (flavor == NAMED_NONE)
				throw std::runtime_error("invalid explicit specialization class-key");
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
			type = AnalyzeClass(target, specialization_scope, std::string(),
				false, name, pattern.owner, pattern.name, true,
				program_->names.Intern(name));
			entity = EntityOf(type);
			if (entity == kNoEntity ||
				program_->entities[entity].declaration == kNoBinding)
				throw std::logic_error(
					"explicit class specialization has no declaration");
			binding = program_->entities[entity].declaration;
		}
		if (program_->entities[entity].template_argument_begin == kNoBinding)
			StoreTemplateArguments(arguments,
				&program_->entities[entity].template_argument_begin,
				&program_->entities[entity].template_argument_count);
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
		return true;
	}

	if (declarator == kNoNode) return false;
	const NodeId specifiers = FindChild(target, "decl-specifier-seq");
	if (specifiers == kNoNode) return false;
	const SpecInfo spec = BuildSpecifiers(
		specifiers, scope, std::string(), true);
	const DeclaratorInfo parsed = BuildDeclarator(
		declarator, spec.type, scope);
	if (!program_->types.IsFunction(parsed.type)) return false;
	const std::vector<std::size_t> patterns =
		FindFunctionTemplates(scope, primary);
	BindingId selected = kNoBinding;
	for (std::size_t i = 0; i < patterns.size(); ++i)
	{
		const FunctionTemplatePattern& pattern = function_templates_[patterns[i]];
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
	if (selected == kNoBinding)
		throw std::runtime_error(
			"explicit function specialization primary was not found");
	FunctionInfo& function = GetMutableFunction(selected);
	if (spec.is_constexpr)
		ValidateConstexprCallableType(function.type, false);
	function.parameters = parsed.parameters;
	function.constexpr_function =
		function.constexpr_function || spec.is_constexpr;
	function.defined = arena_->IsTag(target, "function-definition");
	function.deferred = true;
	function.definition_body = FindChild(target, "compound-statement");
	function.lexical_scope = scope;
	program_->bindings[selected].nonthrowing =
		IsNonthrowing(declarator, scope);
	PublishInlineFunctionFacts(
		selected, spec.inline_specifier || spec.is_constexpr);
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
	return result;
}

BindingId SemanticAnalyzer::InstantiateVariableTemplate(
	NodeId syntax, ScopeId scope)
{
	NamePath path;
	std::vector<NodeId> argument_syntax;
	if (!CollectExplicitTemplateArguments(
		syntax, &path, &argument_syntax)) return kNoBinding;
	const std::vector<std::size_t> related =
		FindVariableTemplates(scope, path);
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
	const TemplateSpecializationKey key(primary_index, arguments);
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
	std::ostringstream generated;
	generated << "__variable_template_" << primary_index << '_';
	for (std::size_t i = 0; i < arguments.size(); ++i)
		generated << arguments[i].kind << '_' << arguments[i].type << '_'
			<< arguments[i].value << '_';
	const NameId name = program_->names.Intern(generated.str());
	const BindingId binding = program_->AddBinding(
		selected.owner, BIND_VARIABLE, name, parsed.type);
	if (variable_template_bindings_.size() <= binding)
		variable_template_bindings_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	variable_template_bindings_[binding] = 1;
	BindingRecord& record = program_->bindings[binding];
	record.storage_class = spec.storage_class;
	StoreTemplateArguments(arguments,
		&record.template_argument_begin, &record.template_argument_count);
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
		const ExpressionInfo initializer = AnalyzeVariableInitializer(
			selected.initializer, substitution_scope, parsed.type, false);
		if (initializer.constant &&
			(spec.is_constexpr || (IsConst(parsed.type) &&
			 IsIntegral(parsed.type, true))))
		{
			PublishBindingScalar(binding, ExpressionScalar(initializer));
		}
	}
	variable_template_instantiations_.Insert(key, binding);
	return binding;
}

void SemanticAnalyzer::ParseTemplateParameters(NodeId list, ScopeId scope,
	std::vector<TemplateParameter>* parameters,
	std::vector<NameId>* names, std::vector<NodeId>* defaults)
{
	std::unordered_set<NameId> prior_names;
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
				ParseTemplateParameters(nested_list, scope,
					&record.template_parameters, &nested_names,
					&nested_defaults);
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
			record.name = record.declarator == kNoNode ? 0 :
				DeclaratorName(record.declarator);
			record.dependent_type = SyntaxUsesTemplateParameter(
				*arena_, record.specifiers, prior_names);
			if (!record.dependent_type)
			{
				const SpecInfo spec = BuildSpecifiers(record.specifiers,
					scope, std::string(), record.declarator != kNoNode);
				record.value_type = record.declarator == kNoNode ? spec.type :
					BuildDeclarator(record.declarator, spec.type, scope).type;
				record.value_type =
					program_->types.RemoveTopCv(record.value_type);
				if (!IsIntegral(record.value_type, true))
					throw std::runtime_error(
						"non-type template parameter is not integral");
			}
		}
		else throw std::runtime_error(
			"template-template parameters are outside PA20");
		parameters->push_back(record);
		names->push_back(record.name);
		defaults->push_back(record.default_argument);
		if (record.name != 0) prior_names.insert(record.name);
	}
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
	const TypeId type = parameter.declarator == kNoNode ? spec.type :
		BuildDeclarator(parameter.declarator, spec.type, parameter_scope).type;
	if (!IsIntegral(type, true) && !FunctionTemplateTypeIsDependent(type))
		throw std::runtime_error(
			"non-type template parameter does not have integral type");
	return program_->types.RemoveTopCv(type);
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
	else program_->AddBinding(scope, BIND_PARAMETER, parameter.name,
			argument.type, true, argument.value);
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

bool SemanticAnalyzer::BuildTemplateArguments(
	const std::vector<TemplateParameter>& parameters,
	const std::vector<NodeId>& syntax, ScopeId use_scope,
	ScopeId lexical_scope, std::vector<TemplateArgument>* arguments,
	bool require_complete)
{
	const bool has_pack = HasTrailingTemplateParameterPack(parameters);
	const std::size_t fixed = FixedTemplateParameterCount(parameters);
	arguments->clear();
	arguments->reserve(std::max(parameters.size(), syntax.size()));
	const ScopeId parameter_scope = NewScope(lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(lexical_scope));
	const auto append_argument = [&](NodeId source,
		ScopeId source_scope) -> bool
	{
		if (arguments->size() >= fixed && !has_pack) return false;
		const TemplateParameter& parameter =
			TemplateParameterForArgument(parameters, arguments->size());
		if (source == kNoNode)
			throw std::runtime_error("empty template argument");
		TemplateArgument argument;
		argument.kind = parameter.kind;
		if (parameter.kind == TEMPLATE_ARGUMENT_TYPE)
		{
			NodeId type_id = arena_->IsTag(source, "type-id") ? source :
				FindChild(source, "type-id");
			if (type_id == kNoNode) return false;
			argument.type = BuildTypeId(type_id,
				source_scope);
			if (argument.type == kNoType) return false;
		}
		else if (parameter.kind == TEMPLATE_ARGUMENT_TEMPLATE)
		{
			if (!BuildTemplateTemplateArgument(
				source, source_scope, parameter, &argument)) return false;
		}
		else
		{
			argument.type = ResolveTemplateParameterType(
				parameter, parameter_scope);
			const bool dependent_target =
				FunctionTemplateTypeIsDependent(argument.type);
			ExpressionInfo expression;
			if (arena_->IsTag(source, "type-id"))
			{
				const NodeId specifiers = FindChild(source,
					"type-specifier-seq");
				const NodeId name = specifiers == kNoNode ? kNoNode :
					FirstSemanticChild(specifiers);
				const NodeId declarator = FindChild(
					source, "abstract-declarator");
				const bool retained_pack_name = declarator != kNoNode &&
					FindChild(declarator, "parameter-pack") != kNoNode;
				if (name != kNoNode && arena_->IsTag(name, "type-name") &&
					(declarator == kNoNode || retained_pack_name))
					expression = AnalyzeNamedValue(PayloadSource(name),
						source_scope, argument.type, name);
				else if (name != kNoNode &&
					arena_->IsTag(name, "decltype-specifier") &&
					FindChild(source, "abstract-declarator") == kNoNode)
				{
					const TypeId qualifier = program_->types.RemoveTopCv(
						EffectiveType(DecltypeType(
							FirstSemanticChild(name), source_scope)));
					EnsureClassDefinition(qualifier);
					const ScopeId carrier = program_->ScopeForType(qualifier);
					const NodeId qualified = FindChild(
						name, "qualified-type-name");
					const LookupResult found = carrier == kNoScope ||
						qualified == kNoNode ? LookupResult() :
						LookupStructuredName(
							qualified, carrier, LOOKUP_ORDINARY);
					if (found.ordinary == kNoBinding ||
						!program_->bindings[found.ordinary].constant)
						return false;
					expression = MakeLiteral(
						program_->bindings[found.ordinary].type,
						InternNumber(program_->bindings[found.ordinary].value));
					expression.constant = true;
					expression.value =
						program_->bindings[found.ordinary].value;
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
			if (!IsIntegral(expression.type, true))
				throw std::runtime_error(
					"non-type template argument is not an integral constant");
			if (expression.constant)
				argument.value = dependent_target ? expression.value :
					NormalizeIntegralConstant(argument.type, expression.value);
			else if (expression.binding != kNoBinding &&
				expression.binding < program_->bindings.size() &&
				program_->bindings[expression.binding].kind == BIND_PARAMETER &&
				!program_->bindings[expression.binding].constant &&
				program_->KindOfScope(
					program_->bindings[expression.binding].owner) ==
					SCOPE_TEMPLATE_PARAMETERS)
			{
				const std::int64_t parameter =
					program_->bindings[expression.binding].value;
				if (parameter < 0 || static_cast<std::uint64_t>(parameter) >=
					kNoTemplateParameter)
					throw std::logic_error(
						"dependent template argument index is invalid");
				argument.dependent_parameter =
					static_cast<std::uint32_t>(parameter);
			}
			else throw std::runtime_error(
				"non-type template argument is not an integral constant: " +
				PayloadSource(source));
		}
		arguments->push_back(argument);
		if (arguments->size() <= fixed)
			BindTemplateArgument(parameter_scope, parameter, argument);
		return true;
	};
	for (std::size_t i = 0; i < syntax.size(); ++i)
	{
		if (arena_->IsTag(syntax[i], "pack-expansion-expression"))
		{
			const NodeId operand = FirstSemanticChild(syntax[i]);
			if (operand == kNoNode)
				throw std::runtime_error("empty template argument pack expansion");
			const NameId source_name = program_->names.Intern(
				PayloadSource(operand));
			std::vector<TemplateArgument> expanded;
			if (!LookupTemplateArgumentPack(use_scope, source_name, &expanded))
			{
				if (!append_argument(operand, use_scope)) return false;
				arguments->back().pack_expansion = true;
				continue;
			}
			for (std::size_t element = 0; element < expanded.size(); ++element)
			{
				if (arguments->size() >= fixed && !has_pack) return false;
				const TemplateParameter& destination =
					TemplateParameterForArgument(parameters, arguments->size());
				if (destination.kind != expanded[element].kind) return false;
				const ScopeId element_scope = NewScope(use_scope,
					SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(use_scope));
				TemplateParameter source_parameter;
				source_parameter.name = source_name;
				source_parameter.kind = expanded[element].kind;
				BindTemplateArgument(element_scope, source_parameter,
					expanded[element]);
				if (!append_argument(operand, element_scope)) return false;
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
			if (!append_argument(syntax[i], use_scope)) return false;
			continue;
		}
		const TemplateArgumentKind destination_kind =
			TemplateParameterForArgument(parameters, arguments->size()).kind;
		std::vector<ScopeId> element_scopes;
		if (!ExpandPackElementScopes(type_id, use_scope, &element_scopes))
		{
			// A retained pack has one symbolic exemplar. The concrete
			// specialization later replays the same syntax against its ordered
			// pack environment. This also covers a non-type pack parsed as the
			// ambiguous type-id spelling `Name...`.
			if (!append_argument(syntax[i], use_scope)) return false;
			arguments->back().pack_expansion = true;
			continue;
		}
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
				arguments->push_back(argument);
				if (arguments->size() <= fixed)
					BindTemplateArgument(parameter_scope, destination, argument);
			}
			else if (!append_argument(syntax[i], element_scopes[element]))
				return false;
		}
	}
	while (require_complete && arguments->size() < fixed)
	{
		const TemplateParameter& parameter = parameters[arguments->size()];
		NodeId source = parameter.default_argument;
		if (source == kNoNode) return false;
		source = FirstSemanticChild(source);
		if (!append_argument(source, parameter_scope)) return false;
	}
	if (require_complete && !has_pack &&
		arguments->size() != parameters.size()) return false;
	if (has_pack)
		BindTemplateArgumentPack(parameter_scope, parameters.back(),
			*arguments, fixed, arguments->size());
	return true;
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

void SemanticAnalyzer::StoreTemplateArguments(
	const std::vector<TemplateArgument>& arguments, std::uint32_t* first,
	std::uint32_t* count)
{
	if (program_->template_arguments.size() !=
		program_->canonical_template_arguments.size())
		throw std::logic_error("canonical template argument storage diverged");
	if (program_->template_arguments.size() >
		std::numeric_limits<std::uint32_t>::max() - arguments.size())
		throw std::runtime_error("too many template arguments");
	*first = static_cast<std::uint32_t>(program_->template_arguments.size());
	*count = static_cast<std::uint32_t>(arguments.size());
	for (std::size_t i = 0; i < arguments.size(); ++i)
		program_->template_arguments.push_back(arguments[i].type);
	program_->canonical_template_arguments.insert(
		program_->canonical_template_arguments.end(),
		arguments.begin(), arguments.end());
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
