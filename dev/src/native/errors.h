#pragma once

#include <iosfwd>

namespace native_errors
{

__attribute__((cold, noinline, noreturn))
void ThrowInvocation(const std::string& message);

__attribute__((cold, noinline, noreturn))
void ThrowInputOutput(const std::string& message);

__attribute__((cold, noinline, noreturn))
void ThrowLowirInput(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowLowirInput(const std::string& message);

__attribute__((cold, noinline, noreturn))
void ThrowSource(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowSource(const std::string& message);

__attribute__((cold, noinline, noreturn))
void ThrowResourceLimit(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowResourceLimit(const std::string& message);

__attribute__((cold, noinline, noreturn))
void ThrowInternal(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowInternal(const std::string& message);

}
