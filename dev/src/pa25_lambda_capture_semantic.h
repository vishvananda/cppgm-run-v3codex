#pragma once

#include "pa10_syntax_model.h"
#include "pa11_model.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppgm
{
namespace pa25_semantic_detail
{

class LambdaCaptureUseTable
{
public:
	struct Fact
	{
		pa10_syntax_detail::NodeId syntax;
		std::uint32_t name_begin, name_count;
		bool captures_this;

		Fact();
	};

	LambdaCaptureUseTable();
	const Fact& FindOrBuild(const pa10_syntax_detail::SyntaxArena& arena,
		pa10_syntax_detail::NodeId lambda);
	pa11::NameId NameAt(const Fact& fact, std::size_t index) const;
	bool IsExplicitAt(const Fact& fact, std::size_t index) const;
	bool IsReferenceAt(const Fact& fact, std::size_t index) const;
	std::size_t Requests() const;
	std::size_t CacheHits() const;
	std::size_t SyntaxVisits() const;
	std::size_t NameUses() const;
	std::size_t StorageBytes() const;

private:
	struct BoundName
	{
		pa11::NameId name;
		std::uint32_t previous, depth;

		BoundName(pa11::NameId name_value, std::uint32_t previous_value,
			std::uint32_t depth_value)
			: name(name_value), previous(previous_value), depth(depth_value) {}
	};

	std::uint32_t FindFact(pa10_syntax_detail::NodeId lambda) const;
	std::uint32_t EnsureFact(pa10_syntax_detail::NodeId lambda);
	void Rehash(std::size_t capacity);
	void Build(std::uint32_t fact,
		const pa10_syntax_detail::SyntaxArena& arena);
	void Walk(pa10_syntax_detail::NodeId node,
		const pa10_syntax_detail::SyntaxArena& arena,
		std::vector<pa11::NameId>* free_names, bool* captures_this);
	void WalkSimpleDeclaration(pa10_syntax_detail::NodeId node,
		const pa10_syntax_detail::SyntaxArena& arena,
		std::vector<pa11::NameId>* free_names, bool* captures_this);
	void WalkDeclaratorDeclaration(pa10_syntax_detail::NodeId node,
		const pa10_syntax_detail::SyntaxArena& arena,
		std::vector<pa11::NameId>* free_names, bool* captures_this);
	void WalkRangeFor(pa10_syntax_detail::NodeId node,
		const pa10_syntax_detail::SyntaxArena& arena,
		std::vector<pa11::NameId>* free_names, bool* captures_this);
	pa11::NameId DeclaratorName(
		const pa10_syntax_detail::SyntaxArena& arena,
		pa10_syntax_detail::NodeId node) const;
	pa11::NameId SimpleExpressionName(
		const pa10_syntax_detail::SyntaxArena& arena,
		pa10_syntax_detail::NodeId node) const;
	void AddBound(pa11::NameId name);
	bool IsBound(pa11::NameId name) const;
	void RestoreBounds(std::size_t mark);

	std::vector<Fact> facts_;
	std::vector<std::uint8_t> states_;
	std::vector<std::uint32_t> slots_;
	std::vector<pa11::NameId> names_;
	std::vector<std::uint8_t> explicit_names_;
	std::vector<std::uint8_t> reference_names_;
	std::vector<std::uint32_t> bound_heads_;
	std::vector<BoundName> bound_stack_;
	std::vector<std::uint32_t> dedup_marks_;
	std::uint32_t depth_, dedup_generation_;
	std::size_t requests_, cache_hits_, syntax_visits_, name_uses_;
};

}
}
