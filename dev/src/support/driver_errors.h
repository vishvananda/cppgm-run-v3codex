#pragma once

#include <iosfwd>

namespace cppgm {
namespace driver_errors {

__attribute__((cold, noinline, noreturn))
void ThrowInvocation(const char * message);

__attribute__((cold, noinline, noreturn))
void ThrowInvocation(const std::string & message);

__attribute__((cold, noinline, noreturn))
void ThrowInputOutput(const char * message);

__attribute__((cold, noinline, noreturn))
void ThrowInputOutput(const std::string & message);

__attribute__((cold, noinline, noreturn))
void ThrowInternal(const char * message);

__attribute__((cold, noinline, noreturn))
void ThrowLexicalSource(const std::string & message);

}
}
