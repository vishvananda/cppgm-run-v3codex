#include "pa12_semantic_detail.h"

#include <algorithm>
#include <limits>
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
	if (!IsIntegral(type, true))
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
	if (argument.kind == TEMPLATE_ARGUMENT_TYPE)
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
	if ((!has_pack && syntax.size() > parameters.size()) ||
		(parameters.empty() && !syntax.empty())) return false;
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
		else
		{
			argument.type = ResolveTemplateParameterType(
				parameter, parameter_scope);
			ExpressionInfo expression;
			if (arena_->IsTag(source, "type-id"))
			{
				const NodeId specifiers = FindChild(source,
					"type-specifier-seq");
				const NodeId name = specifiers == kNoNode ? kNoNode :
					FirstSemanticChild(specifiers);
				if (name == kNoNode || !arena_->IsTag(name, "type-name") ||
					FindChild(source, "abstract-declarator") != kNoNode)
					return false;
				expression = AnalyzeNamedValue(PayloadSource(name),
					source_scope,
					argument.type, name);
			}
			else expression = AnalyzeExpression(source,
				source_scope, argument.type);
			if (!IsIntegral(expression.type, true))
				throw std::runtime_error(
					"non-type template argument is not an integral constant");
			if (expression.constant)
				argument.value = NormalizeIntegralConstant(
					argument.type, expression.value);
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
				"non-type template argument is not an integral constant");
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
		const NodeId specifiers = FindChild(type_id, "type-specifier-seq");
		const NodeId spelling_node = specifiers == kNoNode ? kNoNode :
			FirstSemanticChild(specifiers);
		if (spelling_node == kNoNode) return false;
		const NameId source_name = program_->names.Intern(
			PayloadSource(spelling_node));
		if (arguments->size() >= fixed && !has_pack) return false;
		if (TemplateParameterForArgument(parameters, arguments->size()).kind !=
			TEMPLATE_ARGUMENT_TYPE) return false;
		std::vector<TemplateArgument> expanded;
		if (!LookupTemplateArgumentPack(use_scope, source_name, &expanded))
		{
			// While retaining a function-template shape, a pack has one
			// placeholder alias.  The concrete specialization later replays the
			// same type-id against the ordered pack environment.
			if (!append_argument(syntax[i], use_scope)) return false;
			continue;
		}
		for (std::size_t element = 0; element < expanded.size(); ++element)
		{
			if (expanded[element].kind != TEMPLATE_ARGUMENT_TYPE ||
				(arguments->size() >= fixed && !has_pack)) return false;
			const TemplateParameter& destination =
				TemplateParameterForArgument(parameters, arguments->size());
			if (destination.kind != TEMPLATE_ARGUMENT_TYPE) return false;
			const ScopeId element_scope = NewScope(use_scope,
				SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(use_scope));
			program_->AddBinding(element_scope, BIND_TYPE_ALIAS, source_name,
				expanded[element].type);
			TemplateArgument argument(TEMPLATE_ARGUMENT_TYPE,
				BuildTypeId(type_id, element_scope));
			if (argument.type == kNoType) return false;
			arguments->push_back(argument);
			if (arguments->size() <= fixed)
				BindTemplateArgument(parameter_scope, destination, argument);
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
	{
		const std::size_t index = first + i;
		result.push_back(index < program_->canonical_template_arguments.size() ?
			program_->canonical_template_arguments[index] : TemplateArgument(
				TEMPLATE_ARGUMENT_TYPE, program_->template_arguments[index]));
	}
	return result;
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
