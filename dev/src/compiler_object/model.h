#pragma once

#include "lowir_model.h"

#include <string>

namespace cppgm
{
namespace compiler_object
{

struct Object
{
	std::string target;
	lowir_model::LowirProgram lowir;
};

}
}
