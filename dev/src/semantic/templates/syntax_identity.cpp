#include "semantic/analysis/analyzer.h"

#include <utility>
#include <vector>

namespace cppgm
{
namespace semantic
{
namespace
{

std::size_t TemplateParameterOrdinal(
	const std::vector<TemplateParameter>& parameters, NameId name)
{
	if (name == 0) return parameters.size();
	for (std::size_t i = 0; i < parameters.size(); ++i)
		if (parameters[i].name == name) return i;
	return parameters.size();
}

}

std::uint32_t NextComparableTemplateSyntaxEdge(const SyntaxArena& arena,
	std::uint32_t edge, bool ignore_global_qualifier)
{
	while (edge != kNoEdge && ignore_global_qualifier &&
		arena.IsTag(arena.EdgeChild(edge), ::cppgm::syntax::STAG_GLOBAL_QUALIFIER))
		edge = arena.NextEdge(edge);
	return edge;
}

bool EquivalentNormalizedTemplateSyntax(const SyntaxArena& arena,
	NodeId left, NodeId right,
	const std::vector<TemplateParameter>& left_parameters,
	const std::vector<TemplateParameter>& right_parameters,
	NodeId left_global_owner, NodeId right_global_owner,
	Program* program, ScopeId left_scope, ScopeId right_scope)
{
	// Compare retained structure without demanding incomplete dependent types;
	// parameter spelling is normalized to its template-clause ordinal.
	if (left == kNoNode || right == kNoNode) return left == right;
	std::vector<std::pair<NodeId, NodeId> > pending(
		1, std::make_pair(left, right));
	while (!pending.empty())
	{
		const NodeId left_node = pending.back().first;
		const NodeId right_node = pending.back().second;
		pending.pop_back();
		if (arena.TagId(left_node) != arena.TagId(right_node)) return false;
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
			for (std::uint32_t edge = arena.FirstEdge(left_node);
				edge != kNoEdge; edge = arena.NextEdge(edge))
				if (arena.IsTag(
					arena.EdgeChild(edge), "structured-type-name"))
				{
					structured_wrapper = true;
					break;
				}
			const bool spelling_differs = left_name != right_name ||
				(left_name == 0 && right_name == 0 &&
				 arena.PayloadId(left_node) != arena.PayloadId(right_node));
			bool equivalent_owner_type = false;
			if (!structured_wrapper && spelling_differs && program &&
				left_name != 0 && right_name != 0 &&
				left_scope != kNoScope && right_scope != kNoScope)
			{
				const LookupResult left_lookup = program->LookupName(
					left_scope, left_name, LOOKUP_TYPE);
				const LookupResult right_lookup = program->LookupName(
					right_scope, right_name, LOOKUP_TYPE);
				equivalent_owner_type = left_lookup.type != kNoType &&
					left_lookup.type == right_lookup.type;
			}
			if (!structured_wrapper && spelling_differs &&
				!equivalent_owner_type) return false;
		}
		const bool ignore_global_qualifier =
			left_node == left_global_owner && right_node == right_global_owner;
		std::uint32_t left_edge = NextComparableTemplateSyntaxEdge(arena,
			arena.FirstEdge(left_node), ignore_global_qualifier);
		std::uint32_t right_edge = NextComparableTemplateSyntaxEdge(arena,
			arena.FirstEdge(right_node), ignore_global_qualifier);
		while (left_edge != kNoEdge && right_edge != kNoEdge)
		{
			pending.push_back(std::make_pair(
				arena.EdgeChild(left_edge), arena.EdgeChild(right_edge)));
			left_edge = NextComparableTemplateSyntaxEdge(
				arena, arena.NextEdge(left_edge), ignore_global_qualifier);
			right_edge = NextComparableTemplateSyntaxEdge(
				arena, arena.NextEdge(right_edge), ignore_global_qualifier);
		}
		if (left_edge != right_edge) return false;
	}
	return true;
}

}
}
