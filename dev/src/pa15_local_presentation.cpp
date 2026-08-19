#include "pa15_local_presentation.h"
#include "decimal_spelling.h"
#include "pa12_semantic_model.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace cppgm
{
namespace pa15_local_presentation
{

using namespace pa15_lowir_detail;

lowir_model::StringId InternOrdinalName(pa15_lowir_detail::TypedProgram& program,
	const char* prefix, std::size_t prefix_size, std::uint32_t ordinal)
{
	if (!program.retain_local_names) return lowir_model::StringId();
	return program.strings.intern(
		detail::PrefixedUnsignedDecimal(prefix, prefix_size, ordinal));
}

BlockPresentationName ExactBlockPresentation(TypedProgram& program,
	const std::string& name)
{
	return BlockPresentationName(program.strings.intern(name));
}

BlockPresentationName GeneratedBlockPresentation(TypedProgram& program,
	const std::string& prefix, std::uint32_t ordinal)
{
	if (!program.retain_local_names)
		return BlockPresentationName(program.strings.intern(prefix), ordinal);
	std::string name = prefix;
	name += "_";
	name += std::to_string(ordinal);
	return BlockPresentationName(program.strings.intern(name));
}

Block MakePresentedBlock(TypedProgram& program, Function* function,
	const BlockPresentationName& presentation)
{
	if (!function)
		throw std::logic_error("block presentation has no function");
	if (program.retain_local_names)
	{
		if (presentation.generated() || !presentation.text.valid())
			throw std::logic_error("serializable block has no exact label");
		return Block(presentation.text);
	}
	if (!presentation.text.valid() ||
		function->block_presentations.size() != function->blocks.size())
		throw std::logic_error("invalid object-only block presentation");
	function->block_presentations.push_back(presentation);
	return Block(lowir_model::StringId());
}

namespace
{

struct BlockNameView
{
	const std::string& text;
	char digits[10];
	std::size_t digit_begin;
	bool generated;

	BlockNameView(const BlockPresentationName& name,
		const lowir_model::StringPool& strings)
		: text(strings.get(name.text)), digit_begin(sizeof(digits)),
		  generated(name.generated())
	{
		if (!generated) return;
		std::uint32_t value = name.ordinal;
		do
		{
			digits[--digit_begin] = static_cast<char>('0' + value % 10);
			value /= 10;
		}
		while (value != 0);
	}

	std::size_t size() const
	{
		return text.size() + (generated ? 1 + sizeof(digits) - digit_begin : 0);
	}

	char at(std::size_t index) const
	{
		if (index < text.size()) return text[index];
		if (index == text.size()) return '_';
		return digits[digit_begin + index - text.size() - 1];
	}
};

bool PresentationLess(const BlockPresentationName& left,
	const BlockPresentationName& right,
	const lowir_model::StringPool& strings,
	LocalPresentationCounters* counters)
{
	const BlockNameView lhs(left, strings);
	const BlockNameView rhs(right, strings);
	const std::size_t common = std::min(lhs.size(), rhs.size());
	if (counters) ++counters->block_order_comparisons;
	for (std::size_t i = 0; i < common; ++i)
	{
		if (counters) ++counters->block_order_characters;
		if (lhs.at(i) != rhs.at(i)) return lhs.at(i) < rhs.at(i);
	}
	return lhs.size() < rhs.size();
}

bool RequiresBlockPresentationOrder(const Function& function)
{
	for (std::size_t block = 0; block < function.blocks.size(); ++block)
		for (std::size_t instruction = 0;
			instruction < function.blocks[block].instructions.size(); ++instruction)
		{
			const Instruction& item =
				function.blocks[block].instructions[instruction];
			if (item.kind == Instruction::EH_TRY ||
				(item.kind == Instruction::EH_CLEANUP &&
				 item.target != kNoLowId))
				return true;
		}
	return false;
}

}

void FinalizeBlockPresentation(TypedProgram* program,
	LocalPresentationCounters* counters)
{
	if (!program || program->retain_local_names) return;
	for (std::size_t f = 0; f < program->functions.size(); ++f)
	{
		Function& function = program->functions[f];
		if (function.block_presentations.size() != function.blocks.size())
			throw std::logic_error(
				"object-only function has incomplete block presentation");
		if (!RequiresBlockPresentationOrder(function))
		{
			std::vector<BlockPresentationName>().swap(
				function.block_presentations);
			continue;
		}
		if (counters) ++counters->block_order_functions;
		std::vector<BlockId> order = function.block_order;
		for (std::size_t b = 0; b < order.size(); ++b)
			if (order[b] >= function.blocks.size())
				throw std::logic_error(
					"object-only function has invalid block order");
		std::sort(order.begin(), order.end(),
			[&function, program, counters](BlockId left, BlockId right) {
				return PresentationLess(function.block_presentations[left],
					function.block_presentations[right], program->strings,
					counters);
			});
		function.block_presentation_order.assign(function.blocks.size(), 0);
		for (std::size_t rank = 0; rank < order.size(); ++rank)
			function.block_presentation_order[order[rank]] =
				static_cast<std::uint32_t>(rank);
		std::vector<BlockPresentationName>().swap(
			function.block_presentations);
	}
}

LocalPresentationState::LocalPresentationState()
	: retain_names_(true), counters_(0), generated_slot_ordinal_(0),
	  generated_block_ordinal_(0)
{
}

void LocalPresentationState::Reset(bool retain_names,
	LocalPresentationCounters* counters)
{
	retain_names_ = retain_names;
	counters_ = counters;
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
	// Object-only lowering discards local spellings, generated value and
	// block names never depend on the reservations, and exact machine
	// behavior was byte-identical with the scan disabled, so only the
	// serializable renderer inspects source names for collisions.
	if (!retain_names_)
	{
		FinalizeSourceNames(generated);
		return;
	}
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
			if (counters_)
			{
				++counters_->source_names_scanned;
				counters_->source_name_bytes += name.size();
			}
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
		{
			if (counters_) ++counters_->reservation_matches;
			generated->reserve(patterns[i].kind, ordinal);
		}
	}
	std::uint32_t temporary = 0;
	if (lowir_model::parse_generated_name_ordinal(name, "t", &temporary))
	{
		if (counters_) ++counters_->temporary_reservations;
		temporaries_.push_back(temporary);
	}
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

BlockPresentationName LocalPresentationState::GeneratedBlockName(
	TypedProgram& program, const std::string& prefix)
{
	return GeneratedBlockPresentation(
		program, prefix, static_cast<std::uint32_t>(++generated_block_ordinal_));
}

bool LocalPresentationState::ReservesTemporary(std::uint32_t ordinal)
{
	if (counters_) ++counters_->temporary_probes;
	const bool reserved = retain_names_ ?
		used_names_[detail::PrefixedUnsignedDecimal("t", 1, ordinal)] != 0 :
		std::binary_search(temporaries_.begin(), temporaries_.end(), ordinal);
	if (reserved && counters_) ++counters_->temporary_hits;
	return reserved;
}

}
}
