#pragma once

#include "lowir/model/program.h"

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
