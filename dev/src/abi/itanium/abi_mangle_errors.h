#pragma once

#include <iosfwd>

namespace abi_mangle
{

__attribute__((cold, noinline, noreturn))
void ThrowAbiFactInput(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowAbiFactInput(const std::string& message);

__attribute__((cold, noinline, noreturn))
void ThrowAbiInputOutput(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowAbiResourceLimit(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowAbiInternal(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowAbiInternal(const std::string& message);

}
