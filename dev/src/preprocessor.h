#pragma once

#include "macro_processor.h"

namespace cppgm
{

// Add immutable build-host metadata to one translation-unit preprocessing
// configuration. The host compiler is probed only while building cppgm++.
void ConfigureHostedPreprocessing(PreprocessingOptions* options,
	bool add_standard_include_paths = true);

const char* HostedCompilerCommand();
const char* HostedCompilerTarget();
const char* HostedCompilerVersion();
const char* HostedCompilerSearchDirs();

}
