#include "hosted_extension_semantic.h"

namespace cppgm
{
namespace hosted_extension
{

bool HasGnuAttribute(const pa10_syntax_detail::SyntaxArena& arena,
	pa10_syntax_detail::NodeId node, const std::string& name)
{
	using namespace pa10_syntax_detail;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if (arena.IsTag(child, "gnu-attribute") &&
			arena.Payload(child) == name) return true;
	}
	return false;
}

pa11::TypeId ApplyIntegerSignedness(pa11::TypeTable& types,
	pa11::TypeId type, bool is_unsigned)
{
	using namespace pa11;
	if (type == kNoType || !is_unsigned) return type;
	const TypeRecord& explicit_type = types.Get(type);
	return explicit_type.kind == TYPE_FUNDAMENTAL &&
		explicit_type.fundamental == FUND_INT128 ?
		types.Fundamental(FUND_UINT128) : type;
}

}
}
