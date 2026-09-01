#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <vector>

namespace cppgm
{
namespace semantic
{

bool Analyzer::SyntaxNamesUnboundTemplateParameter(
	NodeId syntax, ScopeId scope)
{
	if (syntax == kNoNode) return false;
	if (arena_->IsTag(syntax, ::cppgm::syntax::STAG_ID_EXPRESSION) &&
		FindChild(syntax, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME) == kNoNode)
	{
		const NameId name =
			program_->names.UseInterned(arena_->SemanticPayloadId(syntax));
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

bool Analyzer::TryExpandBuiltinIntegerPack(NodeId operand,
	ScopeId scope, const TemplateParameter& destination,
	ScopeId parameter_scope, std::vector<TemplateArgument>* arguments)
{
	if (!arena_->IsTag(operand, ::cppgm::syntax::STAG_CALL_EXPRESSION)) return false;
	const NodeId callee = FirstSemanticChild(operand);
	if (callee == kNoNode || !arena_->IsTag(callee, ::cppgm::syntax::STAG_ID_EXPRESSION) ||
		PayloadSource(callee) != "__integer_pack") return false;
	const NodeId list = FindChild(operand, ::cppgm::syntax::STAG_ARGUMENT_LIST);
	NodeId count_syntax = kNoNode;
	for (std::uint32_t edge = list == kNoNode ? kNoEdge :
		arena_->FirstEdge(list); edge != kNoEdge; edge = arena_->NextEdge(edge))
	{
		if (count_syntax != kNoNode)
			ThrowSemanticError("__integer_pack requires one operand");
		count_syntax = arena_->EdgeChild(edge);
	}
	if (count_syntax == kNoNode)
		ThrowSemanticError("__integer_pack requires one operand");
	if (destination.kind != TEMPLATE_ARGUMENT_INTEGRAL)
		ThrowSemanticError("__integer_pack requires an integral pack");
	const TypeId value_type = ResolveTemplateParameterType(
		destination, parameter_scope);
	if (value_type == kNoType || CandidateSubstitutionFailed()) return true;
	const ExpressionInfo count = AnalyzeExpression(count_syntax, scope);
	if (CandidateSubstitutionFailed()) return true;
	if (!count.constant)
	{
		if (!SyntaxNamesUnboundTemplateParameter(count_syntax, scope))
			ThrowSemanticError(
				"__integer_pack operand is not an integral constant");
		TemplateArgument symbolic(TEMPLATE_ARGUMENT_INTEGRAL, value_type, 0,
			kNondeducedTemplateParameter, true);
		arguments->push_back(symbolic);
		return true;
	}
	if (!IsIntegral(count.type, true) || count.value < 0)
		ThrowSemanticError("__integer_pack operand is not nonnegative");
	const std::size_t size = static_cast<std::size_t>(count.value);
	if (size > arguments->max_size() - arguments->size())
		ThrowSemanticResourceLimit("__integer_pack result is too large");
	arguments->reserve(arguments->size() + size);
	for (std::size_t value = 0; value < size; ++value)
		arguments->push_back(TemplateArgument(TEMPLATE_ARGUMENT_INTEGRAL,
			value_type, NormalizeIntegralConstant(value_type,
				static_cast<std::int64_t>(value))));
	return true;
}

bool Analyzer::TryResolveBuiltinTypePackElement(
	NodeId syntax, ScopeId scope, TypeId* type)
{
	NamePath path;
	std::vector<NodeId> argument_syntax;
	if (!CollectExplicitTemplateArguments(
		syntax, &path, &argument_syntax) || path.Size() != 1 ||
		program_->names.Get(path.Last()) != "__type_pack_element") return false;
	if (argument_syntax.size() < 2)
		ThrowSemanticError("__type_pack_element requires a type pack");
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
		ThrowSemanticError("__type_pack_element index is out of range");
	*type = arguments[static_cast<std::size_t>(arguments[0].value) + 1].type;
	return true;
}

bool Analyzer::TryResolveBuiltinMakeIntegerSequence(
	NodeId syntax, ScopeId scope, TypeId* type)
{
	NamePath path;
	std::vector<NodeId> source;
	if (!CollectExplicitTemplateArguments(syntax, &path, &source) ||
		path.Size() != 1 ||
		program_->names.Get(path.Last()) != "__make_integer_seq") return false;
	if (source.size() != 3)
		ThrowSemanticError("__make_integer_seq requires three arguments");
	const NodeId type_id = arena_->IsTag(source[0], ::cppgm::syntax::STAG_TYPE_ID) ? source[0] :
		FindChild(source[0], ::cppgm::syntax::STAG_TYPE_ID);
	const NodeId specifiers = type_id == kNoNode ? kNoNode :
		FindChild(type_id, ::cppgm::syntax::STAG_TYPE_SPECIFIER_SEQ);
	const NodeId name = specifiers == kNoNode ? kNoNode :
		FirstSemanticChild(specifiers);
	if (name == kNoNode)
		ThrowSemanticError("__make_integer_seq target is not a class template");
	const NodeId structured = name == kNoNode ? kNoNode :
		FindChild(name, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
	const NamePath target_path = structured == kNoNode ? NamePath() :
		StructuredNamePath(structured);
	const std::size_t target = structured == kNoNode ?
		FindClassTemplate(scope, PayloadSource(name)) :
		FindClassTemplate(scope, target_path);
	if (target >= class_templates_.size())
		ThrowSemanticError("__make_integer_seq target is not a class template");
	const std::vector<TemplateParameter>& target_parameters =
		class_templates_[target].parameters;
	if (target_parameters.size() != 2 ||
		target_parameters[0].kind != TEMPLATE_ARGUMENT_TYPE ||
		target_parameters[1].kind != TEMPLATE_ARGUMENT_INTEGRAL ||
		!target_parameters[1].pack)
		ThrowSemanticError("__make_integer_seq target has invalid parameters");
	std::vector<TemplateParameter> parameters(1);
	parameters[0].kind = TEMPLATE_ARGUMENT_TYPE;
	std::vector<NodeId> one_source(1, source[1]);
	std::vector<TemplateArgument> element;
	if (!BuildTemplateArguments(parameters, one_source, scope, scope, &element))
	{
		*type = kNoType;
		return true;
	}
	if (element[0].IsDependent() ||
		FunctionTemplateTypeIsDependent(element[0].type))
	{
		*type = FunctionTemplateNondeducedTypeShape();
		return true;
	}
	if (!IsIntegral(element[0].type, true))
		ThrowSemanticError("__make_integer_seq element type is not integral");
	parameters[0].kind = TEMPLATE_ARGUMENT_INTEGRAL;
	parameters[0].value_type = element[0].type;
	one_source[0] = source[2];
	std::vector<TemplateArgument> count;
	if (!BuildTemplateArguments(parameters, one_source, scope, scope, &count))
	{
		*type = kNoType;
		return true;
	}
	if (count[0].IsDependent())
	{
		*type = FunctionTemplateNondeducedTypeShape();
		return true;
	}
	if (count[0].value < 0 || count[0].value > 1048576)
		ThrowSemanticResourceLimit("__make_integer_seq result is too large");
	const std::size_t size = static_cast<std::size_t>(count[0].value);
	std::vector<TemplateArgument> arguments;
	arguments.reserve(size + 1);
	arguments.push_back(element[0]);
	for (std::size_t value = 0; value < size; ++value)
		arguments.push_back(TemplateArgument(TEMPLATE_ARGUMENT_INTEGRAL,
			element[0].type, NormalizeIntegralConstant(element[0].type,
				static_cast<std::int64_t>(value))));
	const BindingId binding = InstantiateClassTemplate(target, arguments);
	if (binding == kNoBinding)
		ThrowSemanticError("__make_integer_seq specialization is invalid");
	*type = program_->bindings[binding].type;
	return true;
}

}
}
