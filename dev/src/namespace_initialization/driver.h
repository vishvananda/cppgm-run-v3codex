#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>

#include "preprocess/macros/macro_processor.h"

namespace cppgm
{
namespace namespace_initialization
{

struct Stats
{
	std::size_t source_bytes;
	std::size_t tokens;
	std::size_t token_storage_bytes;
	std::size_t literal_bytes;
	std::size_t identifiers;
	std::size_t identifier_bytes;
	std::size_t canonical_types;
	std::size_t canonical_type_bytes;
	std::size_t scopes;
	std::size_t declarations;
	std::size_t using_edges;
	std::size_t lookup_queries;
	std::size_t lookup_cache_hits;
	std::size_t lookup_cache_misses;
	std::size_t lookup_cache_invalidations;
	std::size_t lookup_cache_entries;
	std::size_t lookup_scope_visits;
	std::size_t lookup_edge_visits;
	std::size_t linkage_candidates;
	std::size_t declarator_frames;
	std::size_t declarator_cache_hits;
	std::size_t declarator_cache_misses;
	std::size_t declarator_memo_entries;
	std::size_t peak_parser_scratch_bytes;
	std::size_t parser_memo_storage_bytes;
	std::size_t temporaries;
	std::size_t strings;
	std::size_t semantic_storage_bytes;
	std::size_t peak_stage_storage_bytes;
	std::size_t image_bytes;
	std::uint64_t elapsed_nanoseconds;

	Stats();
};

// Owns one PA8 program graph. Translation-unit scopes are isolated while
// identifiers, types, linkage identities, initializers, and emission order are
// canonical for the whole command-line program.
class Program
{
public:
	explicit Program(Stats* stats = 0);
	~Program();

	void AddTranslationUnit(const std::string& path,
		const std::string& source, const PreprocessingOptions& options);
	void WriteImage(std::ostream& output);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;

	Program(const Program&);
	Program& operator=(const Program&);
};

}
}
