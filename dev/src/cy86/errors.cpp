#include "cy86/errors.h"

#include "support/exceptions.h"

namespace cppgm
{
namespace cy86_errors
{

void ThrowSource(const char* message)
{
	throw SourceError(message, CompilerErrorDomain::CY86);
}

void ThrowSource(const std::string& message)
{
	throw SourceError(message, CompilerErrorDomain::CY86);
}

void ThrowInputOutput(const std::string& message)
{
	throw InputOutputError(message, CompilerErrorDomain::CY86);
}

void ThrowResourceLimit(const char* message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::CY86);
}

void ThrowInternal(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::CY86);
}

}
}
