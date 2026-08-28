#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "preprocess/macros/macro_processor.h"

namespace cppgm
{

struct Cy86Stats
{
	std::size_t source_bytes;
	std::size_t tokens;
	std::size_t peak_statement_tokens;
	std::size_t identifiers;
	std::size_t identifier_bytes;
	std::size_t literal_bytes;
	std::size_t opcode_identities;
	std::size_t statements;
	std::size_t operands;
	std::size_t labels;
	std::size_t fixups;
	std::size_t instruction_bytes;
	std::size_t image_bytes;
	std::size_t peak_live_source_bytes;
	std::uint64_t frontend_nanoseconds;
	std::uint64_t lowering_nanoseconds;
	std::uint64_t writing_nanoseconds;
	std::uint64_t elapsed_nanoseconds;

	Cy86Stats();
};

class Cy86Program
{
public:
	explicit Cy86Program(Cy86Stats* stats = 0);
	~Cy86Program();

	void AddTranslationUnit(const std::string& path,
		const std::string& source, const PreprocessingOptions& options);
	void WriteExecutable(const std::string& path);

private:
	Cy86Program(const Cy86Program&);
	Cy86Program& operator=(const Cy86Program&);

	struct Impl;
	Impl* impl_;
};

}
