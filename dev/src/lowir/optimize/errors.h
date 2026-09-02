#pragma once

#include <iosfwd>

namespace lowir_opt {

__attribute__((cold, noinline, noreturn))
void ThrowOptimizerInvocationError(const std::string & message);

__attribute__((cold, noinline, noreturn))
void ThrowOptimizerInternalError(const char * message);

}  // namespace lowir_opt
