#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>

#include "macro_processor.h"

namespace cppgm
{

struct InitializationStats
{
	std::size_t source_bytes;
	std::size_t tokens;
	std::size_t literal_bytes;
	std::size_t identifiers;
	std::size_t canonical_types;
	std::size_t scopes;
	std::size_t declarations;
	std::size_t lookup_queries;
	std::size_t lookup_scope_visits;
	std::size_t lookup_edge_visits;
	std::size_t linkage_candidates;
	std::size_t temporaries;
	std::size_t strings;
	std::size_t image_bytes;
	std::uint64_t elapsed_nanoseconds;

	InitializationStats();
};

// Owns one PA8 program graph. Translation-unit scopes are isolated while
// identifiers, types, linkage identities, initializers, and emission order are
// canonical for the whole command-line program.
class InitializationProgram
{
public:
	explicit InitializationProgram(InitializationStats* stats = 0);
	~InitializationProgram();

	void AddTranslationUnit(const std::string& path,
		const std::string& source, const PreprocessingOptions& options);
	void WriteImage(std::ostream& output);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;

	InitializationProgram(const InitializationProgram&);
	InitializationProgram& operator=(const InitializationProgram&);
};

}
