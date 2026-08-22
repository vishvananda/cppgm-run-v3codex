#include "preprocessor.h"

#include <string>

#include "cppgm_builtin_host_config.h"

namespace cppgm
{
void ConfigureHostedPreprocessing(PreprocessingOptions* options,
	bool add_standard_include_paths, bool optimizing)
{
	options->hosted_predefined_source =
		cppgm_builtin_host_config::kHostPredefinedMacros;
	if (add_standard_include_paths)
		for (std::size_t i = 0;
			cppgm_builtin_host_config::kStandardIncludePaths[i]; ++i)
			options->system_include_search_paths.push_back(
				cppgm_builtin_host_config::kStandardIncludePaths[i]);
	if (optimizing)
		options->macro_actions.push_back(
			PreprocessingOptions::MacroAction(true, "__OPTIMIZE__=1"));

	const std::string predefined(options->hosted_predefined_source);
	if (predefined.find("#define __clang__ ") != std::string::npos &&
		predefined.find("#define __llvm__ ") == std::string::npos)
		options->macro_actions.push_back(
			PreprocessingOptions::MacroAction(true, "__llvm__=1"));
}

const char* HostedCompilerCommand()
{
	return cppgm_builtin_host_config::kHostCxx;
}

const char* HostedCompilerTarget()
{
	return cppgm_builtin_host_config::kTarget;
}

const char* HostedCompilerVersion()
{
	return cppgm_builtin_host_config::kVersion;
}

const char* HostedCompilerSearchDirs()
{
	return cppgm_builtin_host_config::kSearchDirs;
}

}
