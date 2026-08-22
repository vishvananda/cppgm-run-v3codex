#pragma once

#include "macro_processor.h"

namespace cppgm
{

// Add immutable build-host metadata to one translation-unit preprocessing
// configuration. The host compiler is probed only while building cppgm++.
// The host snapshot is taken without -O, so the level-dependent __OPTIMIZE__
// predefine follows the current compile's effective optimization level.
void ConfigureHostedPreprocessing(PreprocessingOptions* options,
	bool add_standard_include_paths = true, bool optimizing = false);

const char* HostedCompilerCommand();
const char* HostedCompilerTarget();
const char* HostedCompilerVersion();
const char* HostedCompilerSearchDirs();

}
