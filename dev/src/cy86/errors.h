#pragma once

#include <string>

namespace cppgm
{
namespace cy86_errors
{

__attribute__((cold, noinline, noreturn))
void ThrowSource(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowSource(const std::string& message);

__attribute__((cold, noinline, noreturn))
void ThrowInputOutput(const std::string& message);

__attribute__((cold, noinline, noreturn))
void ThrowResourceLimit(const char* message);

__attribute__((cold, noinline, noreturn))
void ThrowInternal(const char* message);

}
}
