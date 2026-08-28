#include "syntax/syntax.h"

namespace cppgm
{
namespace syntax
{

InterningStats::InterningStats()
	: source_location_calls(0), token_spelling_calls(0),
	  syntax_tag_calls(0), syntax_payload_calls(0),
	  syntax_tag_query_calls(0), syntax_payload_update_calls(0),
	  syntax_tag_cache_hits(0), syntax_tag_cache_misses(0),
	  source_file_cache_hits(0), source_file_cache_misses(0)
{
}

void InterningStats::Accumulate(const InterningStats& other)
{
	table.Accumulate(other.table);
	source_location_calls += other.source_location_calls;
	token_spelling_calls += other.token_spelling_calls;
	syntax_tag_calls += other.syntax_tag_calls;
	syntax_payload_calls += other.syntax_payload_calls;
	syntax_tag_query_calls += other.syntax_tag_query_calls;
	syntax_payload_update_calls += other.syntax_payload_update_calls;
	syntax_tag_cache_hits += other.syntax_tag_cache_hits;
	syntax_tag_cache_misses += other.syntax_tag_cache_misses;
	source_file_cache_hits += other.source_file_cache_hits;
	source_file_cache_misses += other.source_file_cache_misses;
}

Stats::Stats()
	: tokens(0), interned_spellings(0), spelling_bytes(0), syntax_nodes(0),
	  syntax_edges(0), syntax_output_bytes(0), max_syntax_depth(0),
	  parser_checkpoints(0), parser_rollbacks(0),
	  template_argument_probes(0), template_argument_scans(0),
	  template_argument_cache_hits(0), template_argument_scan_tokens(0),
	  max_template_argument_scan_tokens(0), failed_template_argument_scans(0),
	  parser_fact_changes(0), token_storage_bytes(0), syntax_storage_bytes(0),
	  parser_storage_bytes(0), render_stack_storage_bytes(0),
	  peak_stage_storage_bytes(0), parse_nanoseconds(0), render_nanoseconds(0),
	  elapsed_nanoseconds(0)
{
}

}
}
