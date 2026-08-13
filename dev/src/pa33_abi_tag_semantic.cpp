#include "pa12_semantic_detail.h"
#include "post_tokenizer.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{
namespace
{

using namespace pa10_syntax_detail;
using namespace pa11;

void CollectDirectAbiTags(const SyntaxArena& arena, Program* program,
	NodeId owner, std::vector<NameId>* tags)
{
	if (owner == kNoNode) return;
	for (std::uint32_t edge = arena.FirstEdge(owner); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId attribute = arena.EdgeChild(edge);
		if (!arena.IsTag(attribute, "gnu-attribute")) continue;
		const std::string name = arena.SemanticPayload(attribute);
		if (name != "abi_tag" && name != "__abi_tag__") continue;
		NodeId argument = kNoNode;
		for (std::uint32_t child_edge = arena.FirstEdge(attribute);
			child_edge != kNoEdge; child_edge = arena.NextEdge(child_edge))
		{
			const NodeId child = arena.EdgeChild(child_edge);
			if (arena.IsTag(child, "gnu-attribute-argument"))
			{
				argument = child;
				break;
			}
		}
		if (argument == kNoNode)
			throw std::runtime_error("abi_tag attribute requires a string");
		std::string tag;
		if (!DecodeNarrowStringLiteral(arena.SemanticPayload(argument), &tag) ||
			tag.empty())
			throw std::runtime_error("invalid abi_tag attribute");
		tags->push_back(program->names.Intern(tag));
	}
}

void CollectFunctionAbiTags(const SyntaxArena& arena, Program* program,
	NodeId declaration, std::vector<NameId>* tags)
{
	CollectDirectAbiTags(arena, program, declaration, tags);
	NodeId declarator = kNoNode;
	for (std::uint32_t edge = arena.FirstEdge(declaration); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if (arena.IsTag(child, "declarator"))
		{
			declarator = child;
			break;
		}
	}
	CollectDirectAbiTags(arena, program, declarator, tags);
}

void MergeAbiTags(Program* program, const std::vector<NameId>& additions,
	std::uint32_t* begin, std::uint32_t* count)
{
	if (additions.empty()) return;
	if (*begin > program->abi_tags.size() ||
		*count > program->abi_tags.size() - *begin)
		throw std::logic_error("invalid semantic ABI tag range");
	std::vector<NameId> merged;
	merged.reserve(static_cast<std::size_t>(*count) + additions.size());
	merged.insert(merged.end(), program->abi_tags.begin() + *begin,
		program->abi_tags.begin() + *begin + *count);
	merged.insert(merged.end(), additions.begin(), additions.end());
	std::sort(merged.begin(), merged.end());
	merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
	if (merged.size() == *count && std::equal(merged.begin(), merged.end(),
		program->abi_tags.begin() + *begin))
		return;
	if (merged.size() > static_cast<std::size_t>(
			std::numeric_limits<std::uint32_t>::max()) ||
		program->abi_tags.size() >
			std::numeric_limits<std::uint32_t>::max() - merged.size())
		throw std::runtime_error("too many semantic ABI tags");
	*begin = static_cast<std::uint32_t>(program->abi_tags.size());
	*count = static_cast<std::uint32_t>(merged.size());
	program->abi_tags.insert(program->abi_tags.end(),
		merged.begin(), merged.end());
}

}

void SemanticAnalyzer::ApplyClassAbiTagAttributes(
	NodeId declaration, EntityId entity)
{
	if (entity == kNoEntity || entity >= program_->entities.size())
		throw std::logic_error("ABI-tagged class has no semantic entity");
	std::vector<NameId> tags;
	CollectDirectAbiTags(*arena_, program_, declaration, &tags);
	EntityRecord& record = program_->entities[entity];
	MergeAbiTags(program_, tags, &record.abi_tag_begin, &record.abi_tag_count);
}

void SemanticAnalyzer::ApplyFunctionAbiTagAttributes(
	NodeId declaration, BindingId binding)
{
	if (binding == kNoBinding || binding >= program_->bindings.size())
		throw std::logic_error("ABI-tagged function has no semantic binding");
	std::vector<NameId> tags;
	CollectFunctionAbiTags(*arena_, program_, declaration, &tags);
	if (tags.empty()) return;
	BindingRecord& record = program_->bindings[binding];
	if (record.canonical == kNoBinding ||
		record.canonical >= program_->bindings.size())
		throw std::logic_error("ABI-tagged function has no canonical binding");
	BindingRecord& canonical = program_->bindings[record.canonical];
	MergeAbiTags(program_, tags,
		&canonical.abi_tag_begin, &canonical.abi_tag_count);
	record.abi_tag_begin = canonical.abi_tag_begin;
	record.abi_tag_count = canonical.abi_tag_count;
}

}
}
