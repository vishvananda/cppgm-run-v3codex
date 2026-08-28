#pragma once

#include "native/object/relocatable.h"

#include <cstddef>
#include <string>

namespace cppgm
{
namespace compiler_object
{

lowir_native::RelocatableObject ImportElfRelocatable(
	const std::string& path, std::size_t object_ordinal);

}
}
