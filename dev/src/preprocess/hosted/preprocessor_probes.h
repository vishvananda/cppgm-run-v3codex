#pragma once

#include <string>

namespace cppgm
{

bool IsSupportedFeatureProbe(const std::string& value);
bool IsSupportedBuiltinProbe(const std::string& value);
bool IsSupportedAttributeProbe(const std::string& value);
bool IsCompilerIdentifier(const std::string& value);

}
