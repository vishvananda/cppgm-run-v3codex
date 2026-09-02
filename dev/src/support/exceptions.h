#pragma once

#include <iosfwd>

__attribute__((cold, noinline, noreturn))
void ThrowSemanticError(const char* message);
__attribute__((cold, noinline, noreturn))
void ThrowSemanticError(const std::string& message);
__attribute__((cold, noinline, noreturn))
void ThrowSyntaxError(const char* message);
__attribute__((cold, noinline, noreturn))
void ThrowSyntaxError(const std::string& message);
__attribute__((cold, noinline, noreturn))
void ThrowSyntaxResourceLimit(const char* message);
__attribute__((cold, noinline, noreturn))
void ThrowSyntaxInternal(const char* message);
__attribute__((cold, noinline, noreturn))
void ThrowGeneralResourceLimit(const char* message);
__attribute__((cold, noinline, noreturn))
void ThrowInternalCompilerError(const char* message);
__attribute__((cold, noinline, noreturn))
void ThrowInternalCompilerError(const std::string& message);
__attribute__((cold, noinline, noreturn))
void ThrowSemanticResourceLimit(const char* message);
__attribute__((cold, noinline, noreturn))
void ThrowSemanticResourceLimit(const std::string& message);
__attribute__((cold, noinline, noreturn))
void ThrowSemanticInputOutput(const char* message);
__attribute__((cold, noinline, noreturn))
void ThrowSemanticInputOutput(const std::string& message);
__attribute__((cold, noinline, noreturn))
void ThrowSemanticInternal(const char* message);
__attribute__((cold, noinline, noreturn))
void ThrowSemanticInternal(const std::string& message);
