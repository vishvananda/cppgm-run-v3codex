#pragma once

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>

enum class CompilerErrorDisposition : std::uint8_t
{
	INVOCATION,
	INPUT_OUTPUT,
	SOURCE,
	SYNTAX,
	SEMANTIC,
	HARD_SEMANTIC,
	SERIALIZED_INPUT,
	RESOURCE_LIMIT,
	INTERNAL
};

enum class CompilerErrorDomain : std::uint8_t
{
	GENERAL,
	DRIVER,
	LEXICAL,
	PREPROCESSING,
	RECOGNITION,
	SYNTAX,
	SEMANTIC,
	ABI,
	LOWIR,
	COMPILER_OBJECT,
	LOWERING,
	OPTIMIZER,
	CY86,
	NATIVE
};

enum class SerializedInputFormat : std::uint8_t
{
	ABI_FACT,
	LOWIR,
	COMPILER_OBJECT,
	CY86,
	MIR
};

class CompilerError : public std::exception
{
public:
	CompilerError(CompilerErrorDisposition disposition,
		CompilerErrorDomain domain, const std::string& message,
		std::uint16_t code = 0)
		: message_(message), disposition_(disposition),
		  domain_(domain), code_(code) {}

	const char* what() const noexcept { return message_.c_str(); }
	CompilerErrorDisposition Disposition() const { return disposition_; }
	CompilerErrorDomain Domain() const { return domain_; }
	std::uint16_t Code() const { return code_; }

private:
	std::string message_;
	CompilerErrorDisposition disposition_;
	CompilerErrorDomain domain_;
	std::uint16_t code_;
};

class InvocationError : public CompilerError
{
public:
	explicit InvocationError(const std::string& message,
		std::uint16_t code = 0)
		: CompilerError(CompilerErrorDisposition::INVOCATION,
			CompilerErrorDomain::DRIVER, message, code) {}
};

class InputOutputError : public CompilerError
{
public:
	explicit InputOutputError(const std::string& message,
		CompilerErrorDomain domain = CompilerErrorDomain::GENERAL,
		std::uint16_t code = 0)
		: CompilerError(CompilerErrorDisposition::INPUT_OUTPUT,
			domain, message, code) {}
};

class SourceError : public CompilerError
{
public:
	explicit SourceError(const std::string& message,
		CompilerErrorDomain domain, std::uint16_t code = 0)
		: CompilerError(CompilerErrorDisposition::SOURCE,
			domain, message, code) {}
};

class SyntaxError : public CompilerError
{
public:
	explicit SyntaxError(const std::string& message,
		std::uint16_t code = 0)
		: CompilerError(CompilerErrorDisposition::SYNTAX,
			CompilerErrorDomain::SYNTAX, message, code) {}
};

class SemanticError : public CompilerError
{
public:
	explicit SemanticError(const std::string& message,
		std::uint16_t code = 0)
		: CompilerError(CompilerErrorDisposition::SEMANTIC,
			CompilerErrorDomain::SEMANTIC, message, code) {}
};

class HardSemanticError : public CompilerError
{
public:
	explicit HardSemanticError(const std::string& message,
		std::uint16_t code = 0)
		: CompilerError(CompilerErrorDisposition::HARD_SEMANTIC,
			CompilerErrorDomain::SEMANTIC, message, code) {}
};

class SerializedInputError : public CompilerError
{
public:
	SerializedInputError(SerializedInputFormat format,
		const std::string& message, std::uint16_t code = 0)
		: CompilerError(CompilerErrorDisposition::SERIALIZED_INPUT,
			DomainFor(format), message, code), format_(format) {}

	SerializedInputFormat Format() const { return format_; }

private:
	static CompilerErrorDomain DomainFor(SerializedInputFormat format)
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

	SerializedInputFormat format_;
};

class ResourceLimitError : public CompilerError
{
public:
	explicit ResourceLimitError(const std::string& message,
		CompilerErrorDomain domain = CompilerErrorDomain::GENERAL,
		std::uint16_t code = 0)
		: CompilerError(CompilerErrorDisposition::RESOURCE_LIMIT,
			domain, message, code) {}
};

class InternalCompilerError : public CompilerError
{
public:
	explicit InternalCompilerError(const std::string& message,
		CompilerErrorDomain domain = CompilerErrorDomain::GENERAL,
		std::uint16_t code = 0)
		: CompilerError(CompilerErrorDisposition::INTERNAL,
			domain, message, code) {}
};

__attribute__((cold, noinline, noreturn)) inline
void ThrowSemanticError(const char* message)
{
	throw SemanticError(message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowSemanticError(const std::string& message)
{
	throw SemanticError(message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowSyntaxError(const char* message)
{
	throw SyntaxError(message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowSyntaxError(const std::string& message)
{
	throw SyntaxError(message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowInternalCompilerError(const char* message)
{
	throw InternalCompilerError(message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowInternalCompilerError(const std::string& message)
{
	throw InternalCompilerError(message);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowSemanticResourceLimit(const char* message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::SEMANTIC);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowSemanticResourceLimit(const std::string& message)
{
	throw ResourceLimitError(message, CompilerErrorDomain::SEMANTIC);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowSemanticInputOutput(const char* message)
{
	throw InputOutputError(message, CompilerErrorDomain::SEMANTIC);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowSemanticInputOutput(const std::string& message)
{
	throw InputOutputError(message, CompilerErrorDomain::SEMANTIC);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowSemanticInternal(const char* message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::SEMANTIC);
}

__attribute__((cold, noinline, noreturn)) inline
void ThrowSemanticInternal(const std::string& message)
{
	throw InternalCompilerError(message, CompilerErrorDomain::SEMANTIC);
}

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
