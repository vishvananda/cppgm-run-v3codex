#pragma once

#include "support/interning/frontend_intern.h"
#include "syntax/syntax.h"

namespace cppgm
{
namespace syntax
{

void RunTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream* output, SyntaxTreeConsumer* consumer, Stats* stats,
	InternedStringTable* retained_strings = 0);

}
}
