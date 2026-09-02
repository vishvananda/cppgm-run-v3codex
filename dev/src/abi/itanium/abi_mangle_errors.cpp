#include "abi/itanium/abi_mangle_errors.h"

#include "support/exceptions.h"

namespace abi_mangle
{

void ThrowAbiFactInput(const char* message)
{
	throw SerializedInputError(SerializedInputFormat::ABI_FACT, message);
}

void ThrowAbiFactInput(const std::string& message)
{
	throw SerializedInputError(SerializedInputFormat::ABI_FACT, message);
}

void ThrowAbiInputOutput(const char* message)
{
	throw InputOutputError(message, CompilerErrorDomain::ABI);
}

void ThrowAbiResourceLimit(const char* message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::ABI);
}

void ThrowAbiInternal(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::ABI);
}

void ThrowAbiInternal(const std::string& message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::ABI);
}

}
