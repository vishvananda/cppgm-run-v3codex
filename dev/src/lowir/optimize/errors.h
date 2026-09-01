#pragma once

#include "support/exceptions.h"

#include <string>

namespace lowir_opt {

__attribute__((cold, noinline, noreturn)) inline
void ThrowOptimizerInvocationError(const std::string & message)
{
  throw InvocationError(message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowOptimizerInternalError(const char * message)
{
  throw InternalCompilerError(message, CompilerErrorDomain::OPTIMIZER);
}

}  // namespace lowir_opt
