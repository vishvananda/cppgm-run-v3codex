// (C) 2013 CPPGM Foundation www.cppgm.org. All rights reserved.

#pragma once

#include <string>

#include "preprocess/macros/macro_processor.h"

namespace cppgm
{

std::string ReadPreprocessingSource(const std::string& path);
PreprocessingOptions BuildPreprocessingOptions();

}
