#pragma once

#include "support/exceptions.h"

#include <string>

namespace cppgm
{
namespace compiler_object
{

__attribute__((cold, noinline, noreturn)) inline
void ThrowCompilerObjectInputError(const char* message)
{
	throw SerializedInputError(SerializedInputFormat::COMPILER_OBJECT, message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowCompilerObjectInputError(const std::string& message)
{
	throw SerializedInputError(SerializedInputFormat::COMPILER_OBJECT, message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowCompilerObjectInputOutputError(const std::string& message)
{
	throw InputOutputError(message, CompilerErrorDomain::COMPILER_OBJECT);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowCompilerObjectResourceLimit(const char* message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::COMPILER_OBJECT);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowCompilerObjectInternalError(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::COMPILER_OBJECT);
}

}
}
