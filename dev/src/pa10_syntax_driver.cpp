#include "pa10_syntax_driver_detail.h"

namespace cppgm
{

void WriteSyntaxTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, SyntaxStats* stats)
{
	pa10_syntax_detail::RunSyntaxTranslationUnit(
		path, source, options, &output, 0, stats);
}

void ConsumeSyntaxTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	pa10_syntax_detail::SyntaxTreeConsumer& consumer, SyntaxStats* stats)
{
	pa10_syntax_detail::RunSyntaxTranslationUnit(
		path, source, options, 0, &consumer, stats);
}

}
