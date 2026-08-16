#include "pa12_semantic_detail.h"

namespace cppgm
{
namespace pa12_semantic_detail
{

void DumpArena::ReserveNodes(std::size_t count)
{
	if (count < kNoDumpEdge) nodes.reserve(count);
}

void SemanticAnalyzer::ReserveSemanticCapacity(const SyntaxArena& arena)
{
	// Scope, name, and declaration records are normally no more numerous than
	// syntax nodes. Use that known scale to avoid repeatedly moving them;
	// expansion-heavy translation units retain ordinary vector growth.
	const std::size_t syntax_nodes = arena.Nodes();
	if (syntax_nodes >= static_cast<std::size_t>(kNoBinding)) return;
	program_->ReserveSemanticStorage(syntax_nodes);

	// Grammar scaffolding accounts for roughly half the syntax arena and does
	// not become semantic dump nodes. Start at that scale because DumpNode is a
	// comparatively large retained record; denser units still grow normally.
	dump_.ReserveNodes((syntax_nodes + 1) / 2);
}

}
}
