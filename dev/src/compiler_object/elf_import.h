#pragma once

#include "lowir_native.h"

#include <cstddef>
#include <string>

namespace cppgm
{
namespace compiler_object
{

lowir_native::RelocatableObject ReadElfRelocatableObject(
	const std::string& path, std::size_t object_ordinal);

}
}
