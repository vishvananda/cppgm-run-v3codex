#pragma once

#include "pa10_syntax.h"

namespace cppgm
{
namespace pa10_syntax_detail
{

void RunSyntaxTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream* output, SyntaxTreeConsumer* consumer, SyntaxStats* stats);

}
}
