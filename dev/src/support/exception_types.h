#pragma once

#include "support/exceptions.h"

#include <cstdint>
#include <exception>
#include <string>

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
		std::uint16_t code = 0);
	~CompilerError() noexcept override;

	const char* what() const noexcept override;
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
		std::uint16_t code = 0);
};

class InputOutputError : public CompilerError
{
public:
	explicit InputOutputError(const std::string& message,
		CompilerErrorDomain domain = CompilerErrorDomain::GENERAL,
		std::uint16_t code = 0);
};

class SourceError : public CompilerError
{
public:
	explicit SourceError(const std::string& message,
		CompilerErrorDomain domain, std::uint16_t code = 0);
};

class SyntaxError : public CompilerError
{
public:
	explicit SyntaxError(const std::string& message,
		std::uint16_t code = 0);
};

class SemanticError : public CompilerError
{
public:
	explicit SemanticError(const std::string& message,
		std::uint16_t code = 0);
};

class HardSemanticError : public CompilerError
{
public:
	explicit HardSemanticError(const std::string& message,
		std::uint16_t code = 0);
};

class SerializedInputError : public CompilerError
{
public:
	SerializedInputError(SerializedInputFormat format,
		const std::string& message, std::uint16_t code = 0);

	SerializedInputFormat Format() const { return format_; }

private:
	static CompilerErrorDomain DomainFor(SerializedInputFormat format);

	SerializedInputFormat format_;
};

class ResourceLimitError : public CompilerError
{
public:
	explicit ResourceLimitError(const std::string& message,
		CompilerErrorDomain domain = CompilerErrorDomain::GENERAL,
		std::uint16_t code = 0);
};

class InternalCompilerError : public CompilerError
{
public:
	explicit InternalCompilerError(const char* message,
		CompilerErrorDomain domain = CompilerErrorDomain::GENERAL,
		std::uint16_t code = 0);
	explicit InternalCompilerError(const std::string& message,
		CompilerErrorDomain domain = CompilerErrorDomain::GENERAL,
		std::uint16_t code = 0);
};
