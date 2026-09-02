#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>

#include "preprocess/tokens/post_tokenizer.h"
#include "support/exception_types.h"

namespace
{

char HexDigit(unsigned int value)
{
	return value < 10 ? static_cast<char>('0' + value) :
		static_cast<char>('A' + value - 10);
}

std::string HexDump(const void* data, std::size_t size)
{
	const unsigned char* bytes = static_cast<const unsigned char*>(data);
	std::string result(size * 2, '0');
	for (std::size_t i = 0; i < size; ++i)
	{
		result[i * 2] = HexDigit(bytes[i] >> 4);
		result[i * 2 + 1] = HexDigit(bytes[i] & 0xF);
	}
	return result;
}

class DebugPostTokenStream : public cppgm::IPostTokenStream
{
public:
	explicit DebugPostTokenStream(std::ostream& output) : output_(output) {}

	void EmitInvalid(const std::string& source)
	{
		output_ << "invalid " << source << '\n';
	}

	void EmitSimple(const std::string& source,
		cppgm::SimpleTokenKind kind)
	{
		output_ << "simple " << source << ' ' <<
			cppgm::SimpleTokenKindName(kind) << '\n';
	}

	void EmitIdentifier(const std::string& source)
	{
		output_ << "identifier " << source << '\n';
	}

	void EmitLiteral(const std::string& source,
		cppgm::FundamentalType type, const void* data, std::size_t size)
	{
		output_ << "literal " << source << ' ' <<
			cppgm::FundamentalTypeName(type) << ' ' <<
			HexDump(data, size) << '\n';
	}

	void EmitLiteralArray(const std::string& source, std::size_t elements,
		cppgm::FundamentalType type, const void* data, std::size_t size)
	{
		output_ << "literal " << source << " array of " << elements << ' ' <<
			cppgm::FundamentalTypeName(type) << ' ' <<
			HexDump(data, size) << '\n';
	}

	void EmitUserDefinedCharacter(const std::string& source,
		const std::string& suffix, cppgm::FundamentalType type,
		const void* data, std::size_t size)
	{
		output_ << "user-defined-literal " << source << ' ' << suffix <<
			" character " << cppgm::FundamentalTypeName(type) << ' ' <<
			HexDump(data, size) << '\n';
	}

	void EmitUserDefinedString(const std::string& source,
		const std::string& suffix, std::size_t elements,
		cppgm::FundamentalType type, const void* data, std::size_t size)
	{
		output_ << "user-defined-literal " << source << ' ' << suffix <<
			" string array of " << elements << ' ' <<
			cppgm::FundamentalTypeName(type) << ' ' <<
			HexDump(data, size) << '\n';
	}

	void EmitUserDefinedInteger(const std::string& source,
		const std::string& suffix, const std::string& prefix)
	{
		output_ << "user-defined-literal " << source << ' ' << suffix <<
			" integer " << prefix << '\n';
	}

	void EmitUserDefinedFloating(const std::string& source,
		const std::string& suffix, const std::string& prefix)
	{
		output_ << "user-defined-literal " << source << ' ' << suffix <<
			" floating " << prefix << '\n';
	}

	void EmitEof()
	{
		output_ << "eof\n";
	}

private:
	std::ostream& output_;
};

}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	try
	{
		std::ios_base::sync_with_stdio(false);
		std::cin.tie(0);
		const std::string source((std::istreambuf_iterator<char>(std::cin)),
			std::istreambuf_iterator<char>());
		DebugPostTokenStream output(std::cout);
		cppgm::TokenizePostTokens(source, output);
		return EXIT_SUCCESS;
	}
	catch (const CompilerError& error)
	{
		std::cerr << "ERROR: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
