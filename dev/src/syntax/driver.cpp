#include "syntax/driver_detail.h"

namespace cppgm
{
namespace syntax
{

void WriteTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, Stats* stats)
{
	RunTranslationUnit(
		path, source, options, &output, 0, stats);
}

void ConsumeTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	SyntaxTreeConsumer& consumer, Stats* stats)
{
	RunTranslationUnit(
		path, source, options, 0, &consumer, stats);
}
}

}
