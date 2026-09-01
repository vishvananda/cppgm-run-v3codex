#pragma once

#include "support/exceptions.h"

#include <string>

namespace native_errors
{

__attribute__((cold, noinline, noreturn)) inline
void ThrowInvocation(const std::string& message)
{
	throw InvocationError(message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowInputOutput(const std::string& message)
{
	throw InputOutputError(message, CompilerErrorDomain::NATIVE);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowLowirInput(const char* message)
{
	throw SerializedInputError(SerializedInputFormat::LOWIR, message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowLowirInput(const std::string& message)
{
	throw SerializedInputError(SerializedInputFormat::LOWIR, message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowSource(const char* message)
{
	throw SourceError(message, CompilerErrorDomain::NATIVE);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowSource(const std::string& message)
{
	throw SourceError(message, CompilerErrorDomain::NATIVE);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowResourceLimit(const char* message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::NATIVE);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowResourceLimit(const std::string& message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::NATIVE);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowInternal(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::NATIVE);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowInternal(const std::string& message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::NATIVE);
}

}
