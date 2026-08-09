#include "pa12_semantic_detail.h"

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
	else program_->AddBinding(scope, BIND_PARAMETER, parameter.name,
		argument.type, true, argument.value);
}

bool SemanticAnalyzer::BuildTemplateArguments(
	const std::vector<TemplateParameter>& parameters,
	const std::vector<NodeId>& syntax, ScopeId use_scope,
	ScopeId lexical_scope, std::vector<TemplateArgument>* arguments)
{
	if (syntax.size() > parameters.size()) return false;
	arguments->clear();
	arguments->reserve(parameters.size());
	const ScopeId parameter_scope = NewScope(lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(lexical_scope));
	for (std::size_t i = 0; i < parameters.size(); ++i)
	{
		const TemplateParameter& parameter = parameters[i];
		if (parameter.pack)
			throw std::runtime_error(
				"template parameter pack requires PA20 pack expansion");
		const bool supplied = i < syntax.size();
		NodeId source = supplied ? syntax[i] : parameter.default_argument;
		if (source == kNoNode) return false;
		if (!supplied) source = FirstSemanticChild(source);
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
				supplied ? use_scope : parameter_scope);
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
					supplied ? use_scope : parameter_scope,
					argument.type, name);
			}
			else expression = AnalyzeExpression(source,
				supplied ? use_scope : parameter_scope, argument.type);
			if (!expression.constant || !IsIntegral(expression.type, true))
				throw std::runtime_error(
					"non-type template argument is not an integral constant");
			argument.value = NormalizeIntegralConstant(
				argument.type, expression.value);
		}
		arguments->push_back(argument);
		BindTemplateArgument(parameter_scope, parameter, argument);
	}
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
