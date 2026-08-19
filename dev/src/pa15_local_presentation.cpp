#include "pa15_local_presentation.h"
#include "decimal_spelling.h"
#include "pa12_semantic_model.h"

#include <algorithm>
#include <cstddef>

namespace cppgm
{
namespace pa15_local_presentation
{

lowir_model::StringId InternOrdinalName(pa15_lowir_detail::TypedProgram& program,
	const char* prefix, std::size_t prefix_size, std::uint32_t ordinal)
{
	if (!program.retain_local_names) return lowir_model::StringId();
	return program.strings.intern(
		detail::PrefixedUnsignedDecimal(prefix, prefix_size, ordinal));
}

LocalPresentationState::LocalPresentationState()
	: retain_names_(true), generated_slot_ordinal_(0),
	  generated_block_ordinal_(0)
{
}

void LocalPresentationState::Reset(bool retain_names)
{
	retain_names_ = retain_names;
	generated_slot_ordinal_ = 0;
	generated_block_ordinal_ = 0;
	used_names_.Clear();
	assigned_names_.Clear();
	slot_name_counts_.Clear();
	temporaries_.clear();
}

void LocalPresentationState::CollectSourceNames(const pa11::Program& program,
	const pa12_semantic_detail::DumpArena& arena, std::uint32_t root,
	lowir_model::GeneratedNameReservations* generated)
{
	std::vector<std::uint32_t> pending(1, root);
	while (!pending.empty())
	{
		const std::uint32_t current = pending.back();
		pending.pop_back();
		const pa12_semantic_detail::DumpNode& record = arena.nodes[current];
		if ((record.kind == pa12_semantic_detail::DUMP_PARAMETER ||
			 record.kind == pa12_semantic_detail::DUMP_VARIABLE) &&
			record.text != 0)
		{
			const std::string& name = program.names.Get(record.text);
			if (retain_names_) used_names_[name] = true;
			else RecordSourceName(name, generated);
		}
		for (std::uint32_t edge = record.first_edge;
			edge != pa12_semantic_detail::kNoDumpEdge;
			edge = arena.edges[edge].next)
			pending.push_back(arena.edges[edge].child);
	}
	FinalizeSourceNames(generated);
}

void LocalPresentationState::RecordSourceName(const std::string& name,
	lowir_model::GeneratedNameReservations* generated)
{
	lowir_model::collect_o1_site_reservations(name, generated);
	struct Pattern
	{
		const char* prefix;
		lowir_model::GeneratedNameReservationKind kind;
	};
	static const Pattern patterns[] = {
		{"__force_inline_parameter_", lowir_model::GNR_FORCE_PARAMETER},
		{"__force_inline_temporary_", lowir_model::GNR_FORCE_TEMPORARY},
		{"__force_inline_slot_", lowir_model::GNR_TYPED_FORCE_SLOT},
		{"__force_inline_local_", lowir_model::GNR_FORCE_LOCAL},
		{"__force_inline_result_", lowir_model::GNR_FORCE_RESULT},
		{"retmerge__", lowir_model::GNR_O1_SCALAR_MERGE_SUFFIX},
		{"retmergeobj__", lowir_model::GNR_O1_OBJECT_MERGE_SUFFIX},
		{"__force_inline_block_", lowir_model::GNR_TYPED_FORCE_BLOCK},
		{"__force_inline_prologue_", lowir_model::GNR_FORCE_PROLOGUE},
		{"__force_inline_continuation_", lowir_model::GNR_FORCE_CONTINUATION}
	};
	for (std::size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i)
	{
		std::uint32_t ordinal = 0;
		if (lowir_model::parse_generated_name_ordinal(
			name, patterns[i].prefix, &ordinal))
			generated->reserve(patterns[i].kind, ordinal);
	}
	std::uint32_t temporary = 0;
	if (lowir_model::parse_generated_name_ordinal(name, "t", &temporary))
		temporaries_.push_back(temporary);
}

void LocalPresentationState::FinalizeSourceNames(
	lowir_model::GeneratedNameReservations* generated)
{
	if (retain_names_) return;
	std::sort(temporaries_.begin(), temporaries_.end());
	temporaries_.erase(std::unique(temporaries_.begin(), temporaries_.end()),
		temporaries_.end());
	generated->normalize();
}

std::string LocalPresentationState::UniqueSlotName(
	const std::string& requested)
{
	if (!retain_names_) return std::string();
	const std::string base = requested.empty() ? "__slot" : requested;
	std::size_t& count = slot_name_counts_[base];
	++count;
	std::string candidate = count == 1 ? base :
		base + "__shadow" + std::to_string(count);
	while (assigned_names_[candidate])
	{
		++count;
		candidate = base + "__shadow" + std::to_string(count);
	}
	assigned_names_[candidate] = true;
	used_names_[candidate] = true;
	return candidate;
}

std::string LocalPresentationState::GeneratedSlotName(
	const std::string& prefix)
{
	if (!retain_names_) return std::string();
	while (true)
	{
		const std::string candidate = prefix + "__" +
			std::to_string(++generated_slot_ordinal_);
		if (!used_names_[candidate])
		{
			used_names_[candidate] = true;
			return candidate;
		}
	}
}

std::string LocalPresentationState::GeneratedBlockName(
	const std::string& prefix)
{
	return prefix + "_" + std::to_string(++generated_block_ordinal_);
}

bool LocalPresentationState::ReservesTemporary(std::uint32_t ordinal)
{
	if (retain_names_)
		return used_names_[detail::PrefixedUnsignedDecimal("t", 1, ordinal)] != 0;
	return std::binary_search(temporaries_.begin(), temporaries_.end(), ordinal);
}

}
}
