#pragma once

#include "syntax/model/arena.h"
#include "semantic/model/program.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppgm
{
namespace semantic
{

class LambdaCaptureUseTable
{
public:
	struct Fact
	{
		syntax::NodeId syntax;
		std::uint32_t name_begin, name_count;
		bool captures_this;

		Fact();
	};

	LambdaCaptureUseTable();
	const Fact& FindOrBuild(const syntax::SyntaxArena& arena,
		syntax::NodeId lambda);
	semantic::NameId NameAt(const Fact& fact, std::size_t index) const;
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
		semantic::NameId name;
		std::uint32_t previous, depth;

		BoundName(semantic::NameId name_value, std::uint32_t previous_value,
			std::uint32_t depth_value)
			: name(name_value), previous(previous_value), depth(depth_value) {}
	};

	std::uint32_t FindFact(syntax::NodeId lambda) const;
	std::uint32_t EnsureFact(syntax::NodeId lambda);
	void Rehash(std::size_t capacity);
	void Build(std::uint32_t fact,
		const syntax::SyntaxArena& arena);
	void Walk(syntax::NodeId node,
		const syntax::SyntaxArena& arena,
		std::vector<semantic::NameId>* free_names, bool* captures_this);
	void WalkSimpleDeclaration(syntax::NodeId node,
		const syntax::SyntaxArena& arena,
		std::vector<semantic::NameId>* free_names, bool* captures_this);
	void WalkDeclaratorDeclaration(syntax::NodeId node,
		const syntax::SyntaxArena& arena,
		std::vector<semantic::NameId>* free_names, bool* captures_this);
	void WalkRangeFor(syntax::NodeId node,
		const syntax::SyntaxArena& arena,
		std::vector<semantic::NameId>* free_names, bool* captures_this);
	semantic::NameId DeclaratorName(
		const syntax::SyntaxArena& arena,
		syntax::NodeId node) const;
	semantic::NameId SimpleExpressionName(
		const syntax::SyntaxArena& arena,
		syntax::NodeId node) const;
	void AddBound(semantic::NameId name);
	bool IsBound(semantic::NameId name) const;
	void RestoreBounds(std::size_t mark);

	std::vector<Fact> facts_;
	std::vector<std::uint8_t> states_;
	std::vector<std::uint32_t> slots_;
	std::vector<semantic::NameId> names_;
	std::vector<std::uint8_t> explicit_names_;
	std::vector<std::uint8_t> reference_names_;
	std::vector<std::uint32_t> bound_heads_;
	std::vector<BoundName> bound_stack_;
	std::vector<std::uint32_t> dedup_marks_;
	std::uint32_t depth_, dedup_generation_;
	ObservationCounter requests_, cache_hits_, syntax_visits_, name_uses_;
};

}
}
