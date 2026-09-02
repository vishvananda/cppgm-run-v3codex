#pragma once

#include <iosfwd>

namespace cppgm {
namespace lowering {

__attribute__((cold, noinline, noreturn))
void ThrowLoweringInvocation(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowLoweringInputOutput(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowLoweringSource(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowLoweringSource(const std::string& message);

__attribute__((cold, noinline, noreturn))
void ThrowLoweringResourceLimit(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowLoweringInternal(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowLoweringInternal(const std::string& message);

}
}
