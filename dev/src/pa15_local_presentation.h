#pragma once

#include "pa15_lowir_model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{
class DumpArena;
}
namespace pa15_local_presentation
{

lowir_model::StringId InternOrdinalName(pa15_lowir_detail::TypedProgram& program,
	const char* prefix, std::size_t prefix_size, std::uint32_t ordinal);
pa15_lowir_detail::BlockPresentationName ExactBlockPresentation(
	pa15_lowir_detail::TypedProgram& program, const std::string& name);
pa15_lowir_detail::BlockPresentationName GeneratedBlockPresentation(
	pa15_lowir_detail::TypedProgram& program, const std::string& prefix,
	std::uint32_t ordinal);
pa15_lowir_detail::Block MakePresentedBlock(
	pa15_lowir_detail::TypedProgram& program,
	pa15_lowir_detail::Function* function,
	const pa15_lowir_detail::BlockPresentationName& presentation);

// Stats-only counters for the T5 presentation audit; null disables them.
struct LocalPresentationCounters
{
	std::size_t source_names_scanned;
	std::size_t source_name_bytes;
	std::size_t reservation_matches;
	std::size_t temporary_reservations;
	std::size_t temporary_probes;
	std::size_t temporary_hits;
	std::size_t block_order_functions;
	std::size_t block_order_comparisons;
	std::size_t block_order_characters;

	LocalPresentationCounters()
		: source_names_scanned(0), source_name_bytes(0),
		  reservation_matches(0), temporary_reservations(0),
		  temporary_probes(0), temporary_hits(0), block_order_functions(0),
		  block_order_comparisons(0), block_order_characters(0) {}
};

void FinalizeBlockPresentation(pa15_lowir_detail::TypedProgram* program,
	LocalPresentationCounters* counters);

class LocalPresentationState
{
public:
	LocalPresentationState();

	void Reset(bool retain_names,
		LocalPresentationCounters* counters = 0);
	void CollectSourceNames(const pa11::Program& program,
		const pa12_semantic_detail::DumpArena& arena, std::uint32_t root,
		lowir_model::GeneratedNameReservations* generated);
	std::string UniqueSlotName(const std::string& requested);
	std::string GeneratedSlotName(const std::string& prefix);
	pa15_lowir_detail::BlockPresentationName GeneratedBlockName(
		pa15_lowir_detail::TypedProgram& program, const std::string& prefix);
	bool ReservesTemporary(std::uint32_t ordinal);

private:
	void RecordSourceName(const std::string& name,
		lowir_model::GeneratedNameReservations* generated);
	void FinalizeSourceNames(lowir_model::GeneratedNameReservations* generated);

	bool retain_names_;
	LocalPresentationCounters* counters_;
	std::size_t generated_slot_ordinal_;
	std::size_t generated_block_ordinal_;
	pa15_lowir_detail::StringCounterTable used_names_;
	pa15_lowir_detail::StringCounterTable assigned_names_;
	pa15_lowir_detail::StringCounterTable slot_name_counts_;
	std::vector<std::uint32_t> temporaries_;
};

}
}
