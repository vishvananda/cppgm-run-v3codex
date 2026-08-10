#include "pa12_semantic_detail.h"

#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::SyntaxUsesAnyTemplateParameter(NodeId node,
	const std::unordered_set<NameId>& names) const
{
	if (node == kNoNode) return false;
	if (names.count(arena_->SemanticPayloadId(node)) != 0) return true;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (SyntaxUsesAnyTemplateParameter(
			arena_->EdgeChild(edge), names)) return true;
	return false;
}

bool SemanticAnalyzer::IsDirectTemplateParameterExpression(NodeId node,
	const std::unordered_set<NameId>& names) const
{
	while (node != kNoNode &&
		arena_->IsTag(node, "parenthesized-expression"))
	{
		const std::uint32_t edge = arena_->FirstEdge(node);
		if (edge == kNoEdge || arena_->NextEdge(edge) != kNoEdge) return false;
		node = arena_->EdgeChild(edge);
	}
	return node != kNoNode && arena_->IsTag(node, "id-expression") &&
		names.count(arena_->SemanticPayloadId(node)) != 0;
}

bool SemanticAnalyzer::HasDependentQualifiedType(NodeId node,
	const std::unordered_set<NameId>& names) const
{
	if (node == kNoNode) return false;
	if (arena_->IsTag(node, "decltype-specifier") &&
		SyntaxUsesAnyTemplateParameter(node, names)) return true;
	if (arena_->IsTag(node, "decl-specifier") &&
		arena_->HasDirectChildTag(node, "structured-type-name") &&
		(arena_->Flags(node) & SYNTAX_FLAG_TYPENAME) == 0) return false;
	if (arena_->IsTag(node, "structured-type-name"))
	{
		std::vector<NodeId> components;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
			if (arena_->IsTag(arena_->EdgeChild(edge), "name-component"))
				components.push_back(arena_->EdgeChild(edge));
		for (std::size_t i = 0; i + 1 < components.size(); ++i)
			if (SyntaxUsesAnyTemplateParameter(components[i], names)) return true;
		return false;
	}
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (HasDependentQualifiedType(arena_->EdgeChild(edge), names)) return true;
	return false;
}

TypeId SemanticAnalyzer::FunctionTemplateNondeducedTypeShape()
{
	if (function_template_nondeduced_type_shape_ == kNoType)
	{
		const NameId name = program_->names.Intern(
			"__function_template_nondeduced_type_shape");
		const EntityId entity = program_->NewEntity(name,
			NAMED_TYPENAME_PARAMETER, false, kNoType,
			program_->GlobalScope(), name);
		function_template_nondeduced_type_shape_ =
			program_->types.Named(entity);
	}
	return function_template_nondeduced_type_shape_;
}

}
}
