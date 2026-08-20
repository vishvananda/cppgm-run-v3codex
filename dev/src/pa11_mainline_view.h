#pragma once

#include "pa11_semantic.h"

#include <iosfwd>
#include <string>

namespace cppgm
{

// Render the PA11 scope/type view from the canonical mainline semantic graph.
// The hidden driver census uses this entry point until --emit-types switches
// over and the parallel analyzer can be removed.
void WriteMainlineTypeTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, TypeAnalysisStats* stats = 0);

}
