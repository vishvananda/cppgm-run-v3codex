#pragma once

#include "syntax/model/arena.h"
#include "semantic/model/program.h"

#include <string>

namespace cppgm
{
namespace hosted_extension
{

bool HasGnuAttribute(const syntax::SyntaxArena& arena,
	syntax::NodeId node, const std::string& name);
bool DeferArtificialFunction(const syntax::SyntaxArena& arena,
	syntax::NodeId declaration, bool force_inline);
bool HasStandardAttribute(const syntax::SyntaxArena& arena,
	syntax::NodeId node, const std::string& name);
semantic::TypeId ApplyIntegerSignedness(semantic::TypeTable& types,
	semantic::TypeId type, bool is_unsigned);

}
}
