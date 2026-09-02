#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>

#include "preprocess/macros/macro_processor.h"
#include "support/driver_errors.h"
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
	void EmitSimple(const std::string& source, cppgm::SimpleTokenKind kind)
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
			cppgm::FundamentalTypeName(type) << ' ' << HexDump(data, size) << '\n';
	}
	void EmitLiteralArray(const std::string& source, std::size_t elements,
		cppgm::FundamentalType type, const void* data, std::size_t size)
	{
		output_ << "literal " << source << " array of " << elements << ' ' <<
			cppgm::FundamentalTypeName(type) << ' ' << HexDump(data, size) << '\n';
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
			cppgm::FundamentalTypeName(type) << ' ' << HexDump(data, size) << '\n';
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
	try
	{
		if (argc > 2 || (argc == 2 && std::string(argv[1]) != "--stats"))
			cppgm::driver_errors::ThrowInvocation("invalid usage");
		std::ios_base::sync_with_stdio(false);
		std::cin.tie(0);
		const std::string source((std::istreambuf_iterator<char>(std::cin)),
			std::istreambuf_iterator<char>());
		DebugPostTokenStream output(std::cout);
		const bool report_stats = argc == 2;
		cppgm::MacroProcessingStats stats;
		cppgm::ProcessMacros(source, output, report_stats ? &stats : 0);
		if (report_stats)
		{
			std::cerr << "macro_stats"
				<< " source_bytes=" << source.size()
				<< " pp_tokens=" << stats.tokenization.emitted_tokens
				<< " source_tokens=" << stats.source_tokens
				<< " identifiers=" << stats.interned_identifiers
				<< " identifier_bytes=" << stats.interned_identifier_bytes
				<< " lines=" << stats.logical_lines
				<< " directives=" << stats.directive_lines
				<< " definitions=" << stats.macro_definitions
				<< " undefs=" << stats.macro_undefinitions
				<< " lookups=" << stats.macro_lookups
				<< " invocations=" << stats.macro_invocations
				<< " argument_prescans=" << stats.argument_prescans
				<< " expanded_tokens=" << stats.expanded_tokens
				<< " pasted_tokens=" << stats.pasted_tokens
				<< " pasted_bytes=" << stats.pasted_spelling_bytes
				<< " post_tokens=" << stats.postprocessing.emitted_tokens
				<< " peak_line_tokens=" << stats.peak_line_tokens
				<< " peak_rescan_tokens=" << stats.peak_rescan_tokens
				<< " peak_replacement_tokens="
				<< stats.peak_retained_replacement_tokens
				<< " peak_frames=" << stats.peak_expansion_frames
				<< " peak_argument_bytes=" << stats.peak_argument_storage_bytes
				<< " paint_roots=" << stats.paint_roots
				<< " paint_nodes=" << stats.paint_nodes
				<< " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
		}
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
