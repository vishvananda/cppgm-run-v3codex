#include "pa12_semantic_detail.h"

#include <cstdint>
#include <unordered_set>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::HasDeclSpecifier(
	NodeId specifiers, const char* spelling) const
{
	for (std::uint32_t edge = specifiers == kNoNode ? kNoEdge :
		arena_->FirstEdge(specifiers); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (PayloadSource(arena_->EdgeChild(edge)) == spelling) return true;
	return false;
}

SpecInfo SemanticAnalyzer::BuildIdentityOnlySpecifiers(
	NodeId node, ScopeId scope, const std::string& hint, bool has_declarators)
{
	++class_template_completion_suppressed_depth_;
	try
	{
		const SpecInfo result = BuildSpecifiers(
			node, scope, hint, has_declarators);
		--class_template_completion_suppressed_depth_;
		return result;
	}
	catch (...)
	{
		--class_template_completion_suppressed_depth_;
		throw;
	}
}

TypeId SemanticAnalyzer::BuildIdentityOnlyTypeId(NodeId node, ScopeId scope)
{
	++class_template_completion_suppressed_depth_;
	try
	{
		const TypeId result = BuildTypeId(node, scope);
		--class_template_completion_suppressed_depth_;
		return result;
	}
	catch (...)
	{
		--class_template_completion_suppressed_depth_;
		throw;
	}
}

TypeId SemanticAnalyzer::BuildArrayDeclaratorType(NodeId suffix,
	TypeId element, ScopeId scope,
	const std::unordered_set<NameId>* template_parameter_names)
{
	const NodeId bound_node = FirstSemanticChild(suffix);
	if (bound_node == kNoNode)
		return CandidateTypeFormation(program_->types.TryArray(element, 0),
			"invalid array element type");
	if (template_parameter_names != 0 &&
		SyntaxUsesAnyTemplateParameter(bound_node, *template_parameter_names) &&
		!IsDirectTemplateParameterExpression(
			bound_node, *template_parameter_names))
		return CandidateTypeFormation(program_->types.TryArray(element, 0),
			"invalid array element type");

	++constant_expression_required_depth_;
	ExpressionInfo expression;
	try
	{
		expression = AnalyzeExpression(bound_node, scope);
		--constant_expression_required_depth_;
	}
	catch (...)
	{
		--constant_expression_required_depth_;
		throw;
	}
	if (CandidateSubstitutionFailed()) return kNoType;
	if (expression.constant)
	{
		if (expression.value <= 0)
			return CandidateTypeFormation(kNoType, "invalid array bound");
		return CandidateTypeFormation(program_->types.TryArray(element,
			static_cast<std::uint64_t>(expression.value)),
			"invalid array element type");
	}
	if (expression.binding == kNoBinding ||
		expression.binding >= program_->bindings.size())
		return CandidateTypeFormation(kNoType, "invalid array bound");
	const BindingRecord& binding = program_->bindings[expression.binding];
	if (binding.kind != BIND_PARAMETER || binding.constant ||
		program_->KindOfScope(binding.owner) != SCOPE_TEMPLATE_PARAMETERS ||
		binding.value < 0 || static_cast<std::uint64_t>(binding.value) >=
			kNoTemplateParameter)
		return CandidateTypeFormation(kNoType, "invalid array bound");
	return CandidateTypeFormation(program_->types.TryDependentArray(element,
		program_->types.RemoveTopCv(expression.type),
		static_cast<std::uint32_t>(binding.value)),
		"invalid dependent array element type");
}

}
}
