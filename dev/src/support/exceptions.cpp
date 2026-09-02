#include "abi/itanium/abi_mangle_errors.h"
#include "compiler_object/errors.h"
#include "cy86/errors.h"
#include "lowering/support/errors.h"
#include "lowir/optimize/errors.h"
#include "native/errors.h"
#include "support/exception_types.h"
#include "support/driver_errors.h"

#include <stdexcept>
#include <type_traits>

static_assert(!std::is_base_of<SemanticError, HardSemanticError>::value,
	"hard semantic failures must bypass ordinary semantic recovery");
static_assert(!std::is_base_of<std::runtime_error, CompilerError>::value,
	"project failures must not enter legacy runtime-error recovery");
static_assert(!std::is_base_of<std::logic_error, CompilerError>::value,
	"project failures must not enter legacy logic-error recovery");
static_assert(!std::is_base_of<SyntaxError, ResourceLimitError>::value,
	"resource limits must not be syntax recovery");
static_assert(!std::is_base_of<SyntaxError, InternalCompilerError>::value,
	"internal failures must not be syntax recovery");

CompilerError::CompilerError(CompilerErrorDisposition disposition,
	CompilerErrorDomain domain, const std::string& message, std::uint16_t code)
	: message_(message), disposition_(disposition), domain_(domain), code_(code)
{}

CompilerError::~CompilerError() noexcept = default;

const char* CompilerError::what() const noexcept
{
	return message_.c_str();
}

InvocationError::InvocationError(const std::string& message,
	std::uint16_t code)
	: CompilerError(CompilerErrorDisposition::INVOCATION,
		CompilerErrorDomain::DRIVER, message, code)
{}

InputOutputError::InputOutputError(const std::string& message,
	CompilerErrorDomain domain, std::uint16_t code)
	: CompilerError(CompilerErrorDisposition::INPUT_OUTPUT,
		domain, message, code)
{}

SourceError::SourceError(const std::string& message,
	CompilerErrorDomain domain, std::uint16_t code)
	: CompilerError(CompilerErrorDisposition::SOURCE, domain, message, code)
{}

SyntaxError::SyntaxError(const std::string& message, std::uint16_t code)
	: CompilerError(CompilerErrorDisposition::SYNTAX,
		CompilerErrorDomain::SYNTAX, message, code)
{}

SemanticError::SemanticError(const std::string& message, std::uint16_t code)
	: CompilerError(CompilerErrorDisposition::SEMANTIC,
		CompilerErrorDomain::SEMANTIC, message, code)
{}

HardSemanticError::HardSemanticError(const std::string& message,
	std::uint16_t code)
	: CompilerError(CompilerErrorDisposition::HARD_SEMANTIC,
		CompilerErrorDomain::SEMANTIC, message, code)
{}

CompilerErrorDomain SerializedInputError::DomainFor(
	SerializedInputFormat format)
{
	switch (format)
	{
	case SerializedInputFormat::ABI_FACT:
		return CompilerErrorDomain::ABI;
	case SerializedInputFormat::LOWIR:
		return CompilerErrorDomain::LOWIR;
	case SerializedInputFormat::COMPILER_OBJECT:
		return CompilerErrorDomain::COMPILER_OBJECT;
	case SerializedInputFormat::CY86:
		return CompilerErrorDomain::CY86;
	case SerializedInputFormat::MIR:
		return CompilerErrorDomain::NATIVE;
	}
	return CompilerErrorDomain::GENERAL;
}

SerializedInputError::SerializedInputError(SerializedInputFormat format,
	const std::string& message, std::uint16_t code)
	: CompilerError(CompilerErrorDisposition::SERIALIZED_INPUT,
		DomainFor(format), message, code), format_(format)
{}

ResourceLimitError::ResourceLimitError(const std::string& message,
	CompilerErrorDomain domain, std::uint16_t code)
	: CompilerError(CompilerErrorDisposition::RESOURCE_LIMIT,
		domain, message, code)
{}

InternalCompilerError::InternalCompilerError(const std::string& message,
	CompilerErrorDomain domain, std::uint16_t code)
	: CompilerError(CompilerErrorDisposition::INTERNAL,
		domain, message, code)
{}

void ThrowSemanticError(const char* message)
{
	throw SemanticError(message);
}

void ThrowSemanticError(const std::string& message)
{
	throw SemanticError(message);
}

void ThrowSyntaxError(const char* message)
{
	throw SyntaxError(message);
}

void ThrowSyntaxError(const std::string& message)
{
	throw SyntaxError(message);
}

void ThrowSyntaxResourceLimit(const char* message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::SYNTAX);
}

void ThrowSyntaxInternal(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::SYNTAX);
}

void ThrowGeneralResourceLimit(const char* message)
{
	throw ResourceLimitError(message);
}

void ThrowInternalCompilerError(const char* message)
{
	throw InternalCompilerError(message);
}

void ThrowInternalCompilerError(const std::string& message)
{
	throw InternalCompilerError(message);
}

void ThrowSemanticResourceLimit(const char* message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::SEMANTIC);
}

void ThrowSemanticResourceLimit(const std::string& message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::SEMANTIC);
}

void ThrowSemanticInputOutput(const char* message)
{
	throw InputOutputError(message, CompilerErrorDomain::SEMANTIC);
}

void ThrowSemanticInputOutput(const std::string& message)
{
	throw InputOutputError(message, CompilerErrorDomain::SEMANTIC);
}

void ThrowSemanticInternal(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::SEMANTIC);
}

void ThrowSemanticInternal(const std::string& message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::SEMANTIC);
}

namespace cppgm {
namespace driver_errors {

void ThrowInvocation(const char* message)
{
	throw InvocationError(message);
}

void ThrowInvocation(const std::string& message)
{
	throw InvocationError(message);
}

void ThrowInputOutput(const char* message)
{
	throw InputOutputError(message, CompilerErrorDomain::DRIVER);
}

void ThrowInputOutput(const std::string& message)
{
	throw InputOutputError(message, CompilerErrorDomain::DRIVER);
}

void ThrowInternal(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::DRIVER);
}

void ThrowLexicalSource(const std::string& message)
{
	throw SourceError(message, CompilerErrorDomain::LEXICAL);
}

}
}

namespace abi_mangle {

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

namespace native_errors {

void ThrowInvocation(const std::string& message)
{
	throw InvocationError(message);
}

void ThrowInputOutput(const std::string& message)
{
	throw InputOutputError(message, CompilerErrorDomain::NATIVE);
}

void ThrowLowirInput(const char* message)
{
	throw SerializedInputError(SerializedInputFormat::LOWIR, message);
}

void ThrowLowirInput(const std::string& message)
{
	throw SerializedInputError(SerializedInputFormat::LOWIR, message);
}

void ThrowSource(const char* message)
{
	throw SourceError(message, CompilerErrorDomain::NATIVE);
}

void ThrowSource(const std::string& message)
{
	throw SourceError(message, CompilerErrorDomain::NATIVE);
}

void ThrowResourceLimit(const char* message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::NATIVE);
}

void ThrowResourceLimit(const std::string& message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::NATIVE);
}

void ThrowInternal(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::NATIVE);
}

void ThrowInternal(const std::string& message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::NATIVE);
}

}

namespace cppgm {
namespace lowering {

void ThrowLoweringInvocation(const char* message)
{
	throw InvocationError(message);
}

void ThrowLoweringInputOutput(const char* message)
{
	throw InputOutputError(message, CompilerErrorDomain::LOWERING);
}

void ThrowLoweringSource(const char* message)
{
	throw SourceError(message, CompilerErrorDomain::LOWERING);
}

void ThrowLoweringSource(const std::string& message)
{
	throw SourceError(message, CompilerErrorDomain::LOWERING);
}

void ThrowLoweringResourceLimit(const char* message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::LOWERING);
}

void ThrowLoweringInternal(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::LOWERING);
}

void ThrowLoweringInternal(const std::string& message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::LOWERING);
}

}
}

namespace lowir_opt {

void ThrowOptimizerInvocationError(const std::string& message)
{
	throw InvocationError(message);
}

void ThrowOptimizerInternalError(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::OPTIMIZER);
}

}

namespace cppgm {
namespace compiler_object {

void ThrowCompilerObjectInputError(const char* message)
{
	throw SerializedInputError(SerializedInputFormat::COMPILER_OBJECT, message);
}

void ThrowCompilerObjectInputError(const std::string& message)
{
	throw SerializedInputError(SerializedInputFormat::COMPILER_OBJECT, message);
}

void ThrowCompilerObjectInputOutputError(const std::string& message)
{
	throw InputOutputError(message, CompilerErrorDomain::COMPILER_OBJECT);
}

void ThrowCompilerObjectResourceLimit(const char* message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::COMPILER_OBJECT);
}

void ThrowCompilerObjectInternalError(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::COMPILER_OBJECT);
}

}
}

namespace cppgm {
namespace cy86_errors {

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
