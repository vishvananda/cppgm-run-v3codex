#pragma once

#include "syntax/model/arena.h"
#include "pa11_model.h"

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
pa11::TypeId ApplyIntegerSignedness(pa11::TypeTable& types,
	pa11::TypeId type, bool is_unsigned);

}
}
