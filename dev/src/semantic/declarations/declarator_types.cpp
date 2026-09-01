#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"
#include "support/scoped_state.h"

#include <cstdint>
#include <unordered_set>

namespace cppgm
{
namespace semantic
{

bool Analyzer::HasDeclSpecifier(
	NodeId specifiers, const char* spelling) const
{
	for (std::uint32_t edge = specifiers == kNoNode ? kNoEdge :
		arena_->FirstEdge(specifiers); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (PayloadSource(arena_->EdgeChild(edge)) == spelling) return true;
	return false;
}

SpecInfo Analyzer::BuildIdentityOnlySpecifiers(
	NodeId node, ScopeId scope, const std::string& hint, bool has_declarators)
{
	ScopedCounterIncrement suppressed(
		&class_template_completion_suppressed_depth_);
	return BuildSpecifiers(node, scope, hint, has_declarators);
}

TypeId Analyzer::BuildIdentityOnlyTypeId(NodeId node, ScopeId scope)
{
	ScopedCounterIncrement suppressed(
		&class_template_completion_suppressed_depth_);
	return BuildTypeId(node, scope);
}

void Analyzer::BindDeclaratorImplicitObject(
	ScopeId scope, std::uint8_t function_cv, bool enabled)
{
	if (!enabled || current_class_context_ == kNoEntity) return;
	TypeId object = program_->entities[current_class_context_].type;
	if (function_cv != CV_NONE)
		object = program_->types.Qualify(object, function_cv);
	const BindingId binding = program_->AddBinding(scope, BIND_PARAMETER,
		program_->names.Intern("this"), program_->types.Pointer(object));
	program_->bindings[binding].compiler_generated = true;
}

TypeId Analyzer::BuildArrayDeclaratorType(NodeId suffix,
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

	ExpressionInfo expression;
	{
		ScopedCounterIncrement required(&constant_expression_required_depth_);
		expression = AnalyzeExpression(bound_node, scope);
	}
	if (CandidateSubstitutionFailed()) return kNoType;
	if (expression.constant)
	{
		if (expression.value < 0)
			return CandidateTypeFormation(kNoType, "invalid array bound");
		if (expression.value == 0)
		{
			if (source_type_view_)
				ThrowSemanticError("zero-length array is outside PA11");
			return CandidateTypeFormation(
				program_->types.TryZeroLengthArray(element),
				"invalid zero-length array element type");
		}
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

TypeId Analyzer::BuildBitIntSpecifierType(
	NodeId specifier, ScopeId scope, bool is_unsigned)
{
	const NodeId width_node = FirstSemanticChild(specifier);
	if (width_node == kNoNode)
		return CandidateTypeFormation(kNoType, "_BitInt has no width");
	ExpressionInfo width;
	{
		ScopedCounterIncrement required(&constant_expression_required_depth_);
		width = AnalyzeExpression(width_node, scope);
	}
	if (CandidateSubstitutionFailed()) return kNoType;
	if (width.constant)
	{
		if (!IsIntegral(width.type) || width.value <= 0)
			return CandidateTypeFormation(kNoType, "invalid _BitInt width");
		return CandidateTypeFormation(program_->types.TryBitInt(is_unsigned,
			static_cast<std::uint64_t>(width.value)), "unsupported _BitInt width");
	}
	if (width.binding == kNoBinding ||
		width.binding >= program_->bindings.size())
		return CandidateTypeFormation(kNoType, "invalid dependent _BitInt width");
	const BindingRecord& binding = program_->bindings[width.binding];
	if (binding.kind != BIND_PARAMETER || binding.constant ||
		program_->KindOfScope(binding.owner) != SCOPE_TEMPLATE_PARAMETERS ||
		binding.value < 0 || static_cast<std::uint64_t>(binding.value) >=
			kNoTemplateParameter)
		return CandidateTypeFormation(kNoType, "invalid dependent _BitInt width");
	return CandidateTypeFormation(program_->types.TryDependentBitInt(is_unsigned,
		program_->types.RemoveTopCv(width.type),
		static_cast<std::uint32_t>(binding.value)),
		"invalid dependent _BitInt width");
}

EntityId Analyzer::EntityOf(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	return record.kind == TYPE_NAMED ? record.entity : kNoEntity;
}

bool Analyzer::IsCallableDeclaration(NodeId node) const
{
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_FUNCTION_DEFINITION)) return true;
	const NodeId list = FindChild(node, ::cppgm::syntax::STAG_INIT_DECLARATOR_LIST);
	for (std::uint32_t edge = list == kNoNode ? kNoEdge : arena_->FirstEdge(list);
		edge != kNoEdge; edge = arena_->NextEdge(edge))
	{
		const NodeId declarator = FindChild(arena_->EdgeChild(edge), ::cppgm::syntax::STAG_DECLARATOR);
		if (declarator != kNoNode &&
			FindChild(declarator, ::cppgm::syntax::STAG_PARAMETER_CLAUSE) != kNoNode) return true;
	}
	return false;
}

}
}
