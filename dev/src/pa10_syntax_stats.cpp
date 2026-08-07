#include "pa10_syntax.h"

namespace cppgm
{

SyntaxStats::SyntaxStats()
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
