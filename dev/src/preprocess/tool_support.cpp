// (C) 2013 CPPGM Foundation www.cppgm.org. All rights reserved.

#include "preprocess/tool_support.h"

#include <ctime>
#include <fstream>
#include <iterator>

#include "support/exception_types.h"

namespace cppgm
{

std::string ReadPreprocessingSource(const std::string& path)
{
	std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
	if (!input)
		throw InputOutputError("unable to open source file: " + path,
			CompilerErrorDomain::PREPROCESSING);
	return std::string(std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

PreprocessingOptions BuildPreprocessingOptions()
{
	const std::time_t now = std::time(0);
	const std::tm* local = std::localtime(&now);
	if (!local)
		throw InternalCompilerError("unable to determine build time",
			CompilerErrorDomain::PREPROCESSING);
	const char* text = std::asctime(local);
	if (!text)
		throw InternalCompilerError("unable to format build time",
			CompilerErrorDomain::PREPROCESSING);
	const std::string formatted(text);
	if (formatted.size() < 24)
		throw InternalCompilerError("invalid asctime result",
			CompilerErrorDomain::PREPROCESSING);

	PreprocessingOptions options;
	options.build_date = formatted.substr(4, 7) + formatted.substr(20, 4);
	options.build_time = formatted.substr(11, 8);
	options.author = "Vishvananda Abrams";
	return options;
}

}
