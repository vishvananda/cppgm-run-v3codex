#pragma once

#include "support/exceptions.h"

#include <string>

namespace cppgm {
namespace lowering {

__attribute__((cold, noinline, noreturn)) inline
void ThrowLoweringInvocation(const char* message)
{
	throw InvocationError(message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowLoweringInputOutput(const char* message)
{
	throw InputOutputError(message, CompilerErrorDomain::LOWERING);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowLoweringSource(const char* message)
{
	throw SourceError(message, CompilerErrorDomain::LOWERING);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowLoweringSource(const std::string& message)
{
	throw SourceError(message, CompilerErrorDomain::LOWERING);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowLoweringResourceLimit(const char* message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::LOWERING);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowLoweringInternal(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::LOWERING);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowLoweringInternal(const std::string& message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::LOWERING);
}

}
}
