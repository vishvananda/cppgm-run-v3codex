#pragma once

#include "support/exceptions.h"

#include <string>

namespace cppgm {
namespace driver_errors {

__attribute__((cold, noinline, noreturn)) inline
void ThrowInvocation(const char * message)
{
	throw InvocationError(message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowInvocation(const std::string & message)
{
	throw InvocationError(message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowInputOutput(const char * message)
{
	throw InputOutputError(message, CompilerErrorDomain::DRIVER);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowInputOutput(const std::string & message)
{
	throw InputOutputError(message, CompilerErrorDomain::DRIVER);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowInternal(const char * message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::DRIVER);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowLexicalSource(const std::string & message)
{
	throw SourceError(message, CompilerErrorDomain::LEXICAL);
}

}
}
