#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>

#include "preprocess/macros/macro_processor.h"

namespace cppgm
{
namespace namespace_semantics
{

struct Stats
{
	PreprocessingStats preprocessing;
	std::size_t tokens;
	std::size_t token_storage_bytes;
	std::size_t declarator_frames;
	std::size_t declarator_cache_hits;
	std::size_t declarator_cache_misses;
	std::size_t declarator_memo_entries;
	std::size_t peak_parser_scratch_bytes;
	std::size_t parser_memo_storage_bytes;
	std::size_t identifiers;
	std::size_t identifier_bytes;
	std::size_t canonical_types;
	std::size_t namespaces;
	std::size_t declarations;
	std::size_t using_edges;
	std::size_t lookup_queries;
	std::size_t lookup_cache_hits;
	std::size_t lookup_cache_misses;
	std::size_t lookup_cache_invalidations;
	std::size_t lookup_cache_entries;
	std::size_t lookup_scope_visits;
	std::size_t lookup_edge_visits;
	std::size_t semantic_storage_bytes;
	std::size_t peak_stage_storage_bytes;
	std::uint64_t elapsed_nanoseconds;

	Stats();
};

// Owns the canonical PA7 semantic graph for one translation unit. Parsing is
// integrated with semantic construction; the phase-7 token buffer is released
// when construction returns.
class TranslationUnit
{
public:
	TranslationUnit(const std::string& path,
		const std::string& source, const PreprocessingOptions& options,
		Stats* stats = 0);
	~TranslationUnit();

	TranslationUnit(TranslationUnit&& other) noexcept;
	TranslationUnit& operator=(
		TranslationUnit&& other) noexcept;

	void Render(std::ostream& output) const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;

	TranslationUnit(const TranslationUnit&);
	TranslationUnit& operator=(const TranslationUnit&);
};

}
}
