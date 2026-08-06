#pragma once

#include "pa11_model.h"
#include "pa12_semantic_model.h"

#include <string>

namespace cppgm
{
namespace pa15_lowering_abi
{

std::string MangleFunction(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node);
std::string MangleVariable(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node);

}
}
