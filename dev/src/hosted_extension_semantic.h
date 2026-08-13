#pragma once

#include "pa10_syntax_model.h"
#include "pa11_model.h"

#include <string>

namespace cppgm
{
namespace hosted_extension
{

bool HasGnuAttribute(const pa10_syntax_detail::SyntaxArena& arena,
	pa10_syntax_detail::NodeId node, const std::string& name);
bool HasStandardAttribute(const pa10_syntax_detail::SyntaxArena& arena,
	pa10_syntax_detail::NodeId node, const std::string& name);
pa11::TypeId ApplyIntegerSignedness(pa11::TypeTable& types,
	pa11::TypeId type, bool is_unsigned);

}
}
