#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::SyntaxNamesUnboundTemplateParameter(
	NodeId syntax, ScopeId scope)
{
	if (syntax == kNoNode) return false;
	if (arena_->IsTag(syntax, "id-expression") &&
		FindChild(syntax, "structured-type-name") == kNoNode)
	{
		const NameId name = program_->names.Intern(PayloadSource(syntax));
		const LookupResult found = program_->LookupName(
			scope, name, LOOKUP_ORDINARY);
		if (found.ordinary != kNoBinding)
		{
			const BindingRecord& binding =
				program_->bindings[found.ordinary];
			if (binding.kind == BIND_PARAMETER && !binding.constant &&
				program_->KindOfScope(binding.owner) ==
					SCOPE_TEMPLATE_PARAMETERS) return true;
		}
	}
	for (std::uint32_t edge = arena_->FirstEdge(syntax); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (SyntaxNamesUnboundTemplateParameter(
			arena_->EdgeChild(edge), scope)) return true;
	return false;
}

bool SemanticAnalyzer::TryExpandBuiltinIntegerPack(NodeId operand,
	ScopeId scope, const TemplateParameter& destination,
	ScopeId parameter_scope, std::vector<TemplateArgument>* arguments)
{
	if (!arena_->IsTag(operand, "call-expression")) return false;
	const NodeId callee = FirstSemanticChild(operand);
	if (callee == kNoNode || !arena_->IsTag(callee, "id-expression") ||
		PayloadSource(callee) != "__integer_pack") return false;
	const NodeId list = FindChild(operand, "argument-list");
	NodeId count_syntax = kNoNode;
	for (std::uint32_t edge = list == kNoNode ? kNoEdge :
		arena_->FirstEdge(list); edge != kNoEdge; edge = arena_->NextEdge(edge))
	{
		if (count_syntax != kNoNode)
			throw std::runtime_error("__integer_pack requires one operand");
		count_syntax = arena_->EdgeChild(edge);
	}
	if (count_syntax == kNoNode)
		throw std::runtime_error("__integer_pack requires one operand");
	if (destination.kind != TEMPLATE_ARGUMENT_INTEGRAL)
		throw std::runtime_error("__integer_pack requires an integral pack");
	const TypeId value_type = ResolveTemplateParameterType(
		destination, parameter_scope);
	if (value_type == kNoType || CandidateSubstitutionFailed()) return true;
	const ExpressionInfo count = AnalyzeExpression(count_syntax, scope);
	if (CandidateSubstitutionFailed()) return true;
	if (!count.constant)
	{
		if (!SyntaxNamesUnboundTemplateParameter(count_syntax, scope))
			throw std::runtime_error(
				"__integer_pack operand is not an integral constant");
		TemplateArgument symbolic(TEMPLATE_ARGUMENT_INTEGRAL, value_type, 0,
			kNondeducedTemplateParameter, true);
		arguments->push_back(symbolic);
		return true;
	}
	if (!IsIntegral(count.type, true) || count.value < 0)
		throw std::runtime_error("__integer_pack operand is not nonnegative");
	const std::size_t size = static_cast<std::size_t>(count.value);
	if (size > arguments->max_size() - arguments->size())
		throw std::runtime_error("__integer_pack result is too large");
	arguments->reserve(arguments->size() + size);
	for (std::size_t value = 0; value < size; ++value)
		arguments->push_back(TemplateArgument(TEMPLATE_ARGUMENT_INTEGRAL,
			value_type, NormalizeIntegralConstant(value_type,
				static_cast<std::int64_t>(value))));
	return true;
}

bool SemanticAnalyzer::TryResolveBuiltinTypePackElement(
	NodeId syntax, ScopeId scope, TypeId* type)
{
	NamePath path;
	std::vector<NodeId> argument_syntax;
	if (!CollectExplicitTemplateArguments(
		syntax, &path, &argument_syntax) || path.Size() != 1 ||
		program_->names.Get(path.Last()) != "__type_pack_element") return false;
	if (argument_syntax.size() < 2)
		throw std::runtime_error("__type_pack_element requires a type pack");
	std::vector<TemplateParameter> parameters(2);
	parameters[0].kind = TEMPLATE_ARGUMENT_INTEGRAL;
	parameters[0].value_type =
		program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	parameters[1].kind = TEMPLATE_ARGUMENT_TYPE;
	parameters[1].pack = true;
	std::vector<TemplateArgument> arguments;
	if (!BuildTemplateArguments(parameters, argument_syntax, scope, scope,
		&arguments) || arguments.size() < 2)
	{
		*type = kNoType;
		return true;
	}
	if (arguments[0].IsDependent())
	{
		*type = FunctionTemplateNondeducedTypeShape();
		return true;
	}
	for (std::size_t i = 1; i < arguments.size(); ++i)
		if (arguments[i].IsDependent() || arguments[i].pack_expansion)
		{
			*type = FunctionTemplateNondeducedTypeShape();
			return true;
		}
	if (arguments[0].value < 0 ||
		static_cast<std::uint64_t>(arguments[0].value) >= arguments.size() - 1)
		throw std::runtime_error("__type_pack_element index is out of range");
	*type = arguments[static_cast<std::size_t>(arguments[0].value) + 1].type;
	return true;
}

}
}
