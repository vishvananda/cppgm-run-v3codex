#include "semantic/extensions/hosted_extensions.h"

namespace cppgm
{
namespace hosted_extension
{

bool HasGnuAttribute(const syntax::SyntaxArena& arena,
	syntax::NodeId node, const std::string& name)
{
	using namespace syntax;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if (arena.IsTag(child, ::cppgm::syntax::STAG_GNU_ATTRIBUTE) &&
			arena.SemanticPayload(child) == name) return true;
	}
	return false;
}

bool DeferArtificialFunction(const syntax::SyntaxArena& arena,
	syntax::NodeId declaration, bool force_inline)
{
	using namespace syntax;
	if (!force_inline) return false;
	const auto artificial = [&arena](NodeId owner) {
		return HasGnuAttribute(arena, owner, "artificial") ||
			HasGnuAttribute(arena, owner, "__artificial__");
	};
	if (artificial(declaration)) return true;
	for (std::uint32_t edge = arena.FirstEdge(declaration); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if ((arena.IsTag(child, ::cppgm::syntax::STAG_DECLARATOR) ||
			 arena.IsTag(child, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ)) && artificial(child))
			return true;
	}
	return false;
}

bool HasStandardAttribute(const syntax::SyntaxArena& arena,
	syntax::NodeId node, const std::string& name)
{
	using namespace syntax;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if (arena.IsTag(child, ::cppgm::syntax::STAG_STANDARD_ATTRIBUTE) &&
			arena.SemanticPayload(child) == name) return true;
	}
	return false;
}

semantic::TypeId ApplyIntegerSignedness(semantic::TypeTable& types,
	semantic::TypeId type, bool is_unsigned)
{
	using namespace semantic;
	if (type == kNoType || !is_unsigned) return type;
	const TypeRecord& explicit_type = types.Get(type);
	return explicit_type.kind == TYPE_FUNDAMENTAL &&
		explicit_type.fundamental == FUND_INT128 ?
		types.Fundamental(FUND_UINT128) : type;
}

}
}
