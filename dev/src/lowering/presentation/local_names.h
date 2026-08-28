#pragma once

#include "lowering/ir/model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cppgm
{
namespace semantic
{
class DumpArena;
struct Stats;
}
namespace lowering
{
namespace presentation
{

class EmissionNameMap
{
public:
	EmissionNameMap(const semantic::Program& program,
		semantic::Stats* stats);
	std::string Apply(const semantic::BindingRecord& binding) const;

private:
	const std::string& ClassTemplatePresentation(
		std::uint32_t presentation) const;
	const semantic::Program& program_;
	semantic::Stats* stats_;
	std::vector<std::uint32_t> replacement_presentations_;
	std::vector<semantic::EntityId> presentation_entities_;
	mutable std::vector<std::uint32_t> rendered_indices_;
	mutable std::vector<std::string> rendered_presentations_;
	mutable std::vector<semantic::NameId> path_;
};

lowir_model::StringId InternOrdinalName(lowering::ir::Program& program,
	const char* prefix, std::size_t prefix_size, std::uint32_t ordinal);
lowering::ir::BlockPresentationName ExactBlockPresentation(
	lowering::ir::Program& program, const std::string& name);
lowering::ir::BlockPresentationName GeneratedBlockPresentation(
	lowering::ir::Program& program, const std::string& prefix,
	std::uint32_t ordinal);
lowering::ir::Block MakePresentedBlock(
	lowering::ir::Program& program,
	lowering::ir::Function* function,
	const lowering::ir::BlockPresentationName& presentation);

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

void FinalizeBlockPresentation(lowering::ir::Program* program,
	LocalPresentationCounters* counters);

class LocalPresentationState
{
public:
	LocalPresentationState();

	void Reset(bool retain_names,
		LocalPresentationCounters* counters = 0);
	void CollectSourceNames(const semantic::Program& program,
		const semantic::DumpArena& arena, std::uint32_t root,
		lowir_model::GeneratedNameReservations* generated);
	std::string UniqueSlotName(const std::string& requested);
	std::string GeneratedSlotName(const std::string& prefix);
	lowering::ir::BlockPresentationName GeneratedBlockName(
		lowering::ir::Program& program, const std::string& prefix);
	bool ReservesTemporary(std::uint32_t ordinal);

private:
	void RecordSourceName(const std::string& name,
		lowir_model::GeneratedNameReservations* generated);
	void FinalizeSourceNames(lowir_model::GeneratedNameReservations* generated);

	bool retain_names_;
	LocalPresentationCounters* counters_;
	std::size_t generated_slot_ordinal_;
	std::size_t generated_block_ordinal_;
	lowering::ir::StringCounterTable used_names_;
	lowering::ir::StringCounterTable assigned_names_;
	lowering::ir::StringCounterTable slot_name_counts_;
	std::vector<std::uint32_t> temporaries_;
};

}  // namespace presentation
}  // namespace lowering
}  // namespace cppgm
