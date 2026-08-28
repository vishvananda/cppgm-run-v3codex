#include "lowering/presentation/local_names.h"
#include "support/numeric/decimal_spelling.h"
#include "semantic/model/graph.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace cppgm
{
namespace lowering
{
namespace presentation
{

using namespace lowering::ir;

lowir_model::StringId InternOrdinalName(lowering::ir::TypedProgram& program,
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

// Renders one block presentation into the shared key buffer exactly as the
// retained lexical spelling would read: label text, then an underscore and
// the decimal ordinal for generated names.
void AppendBlockOrderKey(const BlockPresentationName& name,
	const lowir_model::StringPool& strings, std::string* bytes)
{
	const std::string& text = strings.get(name.text);
	bytes->append(text);
	if (name.generated())
	{
		char digits[10];
		std::size_t begin = sizeof(digits);
		std::uint32_t value = name.ordinal;
		do
		{
			digits[--begin] = static_cast<char>('0' + value % 10);
			value /= 10;
		}
		while (value != 0);
		bytes->push_back('_');
		bytes->append(digits + begin, sizeof(digits) - begin);
	}
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
	std::string key_bytes;
	std::vector<std::pair<std::uint32_t, std::uint32_t> > key_spans;
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
		// Render each presentation's exact lexical bytes once, then sort by
		// flat byte spans instead of reconstructing characters per compare.
		key_bytes.clear();
		key_spans.assign(function.blocks.size(),
			std::pair<std::uint32_t, std::uint32_t>(0, 0));
		for (std::size_t b = 0; b < order.size(); ++b)
		{
			const std::uint32_t begin =
				static_cast<std::uint32_t>(key_bytes.size());
			AppendBlockOrderKey(function.block_presentations[order[b]],
				program->strings, &key_bytes);
			key_spans[order[b]] = std::pair<std::uint32_t, std::uint32_t>(
				begin, static_cast<std::uint32_t>(key_bytes.size()));
		}
		if (counters) counters->block_order_characters += key_bytes.size();
		const char* keys = key_bytes.data();
		std::sort(order.begin(), order.end(),
			[&key_spans, keys, counters](BlockId left, BlockId right) {
				if (counters) ++counters->block_order_comparisons;
				const std::pair<std::uint32_t, std::uint32_t>& lhs =
					key_spans[left];
				const std::pair<std::uint32_t, std::uint32_t>& rhs =
					key_spans[right];
				return std::lexicographical_compare(
					keys + lhs.first, keys + lhs.second,
					keys + rhs.first, keys + rhs.second);
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

void LocalPresentationState::CollectSourceNames(const semantic::Program& program,
	const semantic::DumpArena& arena, std::uint32_t root,
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
		const semantic::DumpNode& record = arena.nodes[current];
		if ((record.kind == semantic::DUMP_PARAMETER ||
			 record.kind == semantic::DUMP_VARIABLE) &&
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
			edge != semantic::kNoDumpEdge;
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

}  // namespace presentation
}  // namespace lowering
}  // namespace cppgm
