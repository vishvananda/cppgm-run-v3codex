#pragma once

#include <iosfwd>

namespace cppgm
{
namespace compiler_object
{

__attribute__((cold, noinline, noreturn))
void ThrowCompilerObjectInputError(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowCompilerObjectInputError(const std::string& message);

__attribute__((cold, noinline, noreturn))
void ThrowCompilerObjectInputOutputError(const std::string& message);

__attribute__((cold, noinline, noreturn))
void ThrowCompilerObjectResourceLimit(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowCompilerObjectInternalError(const char* message);

}
}
