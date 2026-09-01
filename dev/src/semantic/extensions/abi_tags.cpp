#include "semantic/analysis/analyzer.h"
#include "semantic/extensions/function_control_attributes.h"
#include "preprocess/tokens/post_tokenizer.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace semantic
{
namespace
{

using namespace syntax;

void CollectDirectAbiTags(const SyntaxArena& arena, Program* program,
	NodeId owner, std::vector<NameId>* tags)
{
	if (owner == kNoNode) return;
	for (std::uint32_t edge = arena.FirstEdge(owner); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId attribute = arena.EdgeChild(edge);
		if (!arena.IsTag(attribute, ::cppgm::syntax::STAG_GNU_ATTRIBUTE)) continue;
		const std::string name = arena.SemanticPayload(attribute);
		if (name != "abi_tag" && name != "__abi_tag__") continue;
		bool has_argument = false;
		for (std::uint32_t child_edge = arena.FirstEdge(attribute);
			child_edge != kNoEdge; child_edge = arena.NextEdge(child_edge))
		{
			const NodeId child = arena.EdgeChild(child_edge);
			if (arena.IsTag(child, ::cppgm::syntax::STAG_GNU_ATTRIBUTE_NONLITERAL_ARGUMENT))
				throw std::runtime_error("invalid abi_tag attribute argument");
			if (!arena.IsTag(child, ::cppgm::syntax::STAG_GNU_ATTRIBUTE_ARGUMENT)) continue;
			has_argument = true;
			std::string tag;
			if (!DecodeNarrowStringLiteralSequence(
				arena.SemanticPayload(child), &tag) ||
				tag.empty())
				throw std::runtime_error("invalid abi_tag attribute");
			tags->push_back(program->names.Intern(tag));
		}
		if (!has_argument)
			throw std::runtime_error("abi_tag attribute requires a string");
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
		if (arena.IsTag(child, ::cppgm::syntax::STAG_DECLARATOR))
		{
			declarator = child;
			break;
		}
	}
	CollectDirectAbiTags(arena, program, declarator, tags);
}

std::uint8_t DirectFunctionControlAttributeMask(
	const SyntaxArena& arena, NodeId owner)
{
	if (owner == kNoNode) return 0;
	std::uint8_t result = 0;
	for (std::uint32_t edge = arena.FirstEdge(owner); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId attribute = arena.EdgeChild(edge);
		if (!arena.IsTag(attribute, ::cppgm::syntax::STAG_GNU_ATTRIBUTE) &&
			!arena.IsTag(attribute, ::cppgm::syntax::STAG_STANDARD_ATTRIBUTE)) continue;
		const std::string& spelling = arena.SemanticPayload(attribute);
		if (spelling == "noreturn" || spelling == "__noreturn__")
			result |= FUNCTION_CONTROL_NORETURN;
		else if (spelling == "always_inline" || spelling == "__always_inline__")
			result |= FUNCTION_CONTROL_FORCE_INLINE;
		else if (spelling == "noinline" || spelling == "__noinline__")
			result |= FUNCTION_CONTROL_NO_INLINE;
		else if (spelling == "pure" || spelling == "__pure__")
			result |= FUNCTION_CONTROL_PURE;
		else if (spelling == "const" || spelling == "__const__")
			result |= FUNCTION_CONTROL_CONST;
		else if (spelling == "cppgm_stable_prefix" ||
			spelling == "__cppgm_stable_prefix__")
		{
			if (arena.FirstEdge(attribute) != kNoEdge)
				throw std::runtime_error(
					"cppgm_stable_prefix attribute takes no arguments");
			result |= FUNCTION_CONTROL_STABLE_PREFIX_QUERY;
		}
	}
	return result;
}

std::uint8_t CollectFunctionControlAttributeMask(
	const SyntaxArena& arena, NodeId declaration)
{
	std::uint8_t result =
		DirectFunctionControlAttributeMask(arena, declaration);
	for (std::uint32_t edge = arena.FirstEdge(declaration); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if (arena.IsTag(child, ::cppgm::syntax::STAG_DECLARATOR) ||
			arena.IsTag(child,
				::cppgm::syntax::STAG_LAMBDA_DECLARATOR) ||
			arena.IsTag(child, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ))
		{
			result |= DirectFunctionControlAttributeMask(arena, child);
		}
	}
	return result;
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

bool IsStablePrefixIntegerType(const Program& program, TypeId type)
{
	type = program.types.RemoveTopCv(type);
	const TypeRecord& record = program.types.Get(type);
	if (record.kind == TYPE_BITINT) return record.bound <= 64;
	if (record.kind == TYPE_FUNDAMENTAL)
		return record.fundamental != FUND_VOID &&
			record.fundamental != FUND_NULLPTR_T &&
			record.fundamental != FUND_INT128 &&
			record.fundamental != FUND_UINT128 &&
			!IsExtendedFloatingFundamental(record.fundamental);
	if (record.kind != TYPE_NAMED || record.entity >= program.entities.size())
		return false;
	return IsEnumNamedFlavor(program.entities[record.entity].flavor);
}

bool IsStablePrefixScalarResult(const Program& program, TypeId type)
{
	type = program.types.RemoveTopCv(type);
	const TypeRecord& record = program.types.Get(type);
	if (record.kind == TYPE_POINTER || record.kind == TYPE_BLOCK_POINTER ||
		record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE)
		return true;
	if (record.kind == TYPE_BITINT) return record.bound <= 64;
	if (record.kind == TYPE_NAMED)
		return record.entity < program.entities.size() &&
			IsEnumNamedFlavor(program.entities[record.entity].flavor);
	return record.kind == TYPE_FUNDAMENTAL &&
		record.fundamental != FUND_VOID &&
		record.fundamental != FUND_LONG_DOUBLE &&
		record.fundamental != FUND_FLOAT64X &&
		record.fundamental != FUND_INT128 &&
		record.fundamental != FUND_UINT128 &&
		record.fundamental != FUND_STDFLOAT128 &&
		record.fundamental != FUND_FLOAT128;
}

void ValidateStablePrefixFunction(const Program& program, TypeId type)
{
	const TypeRecord& function = program.types.Get(type);
	if (function.kind != TYPE_FUNCTION || function.variadic ||
		function.parameter_count == 0 ||
		!IsStablePrefixIntegerType(program,
			program.types.Parameters(type)[function.parameter_count - 1]) ||
		!IsStablePrefixScalarResult(program, function.child))
		throw std::runtime_error(
			"cppgm_stable_prefix requires a fixed scalar function "
			"with a final integer parameter");
}

}

std::uint8_t FunctionControlAttributeMask(
	const SyntaxArena& arena, NodeId declaration)
{
	return CollectFunctionControlAttributeMask(arena, declaration);
}

void ApplyFunctionControlAttributes(Program* program,
	BindingId binding, std::uint8_t attributes)
{
	if (!program || binding == kNoBinding || binding >= program->bindings.size())
		throw std::logic_error("attributed function has no semantic binding");
	BindingRecord& record = program->bindings[binding];
	if (record.canonical == kNoBinding ||
		record.canonical >= program->bindings.size())
		throw std::logic_error("attributed function has no canonical binding");
	BindingRecord& canonical = program->bindings[record.canonical];
	if ((attributes & FUNCTION_CONTROL_NORETURN) != 0)
		record.noreturn_function = canonical.noreturn_function = true;
	if ((attributes & FUNCTION_CONTROL_FORCE_INLINE) != 0)
		record.force_inline = canonical.force_inline = true;
	if ((attributes & FUNCTION_CONTROL_NO_INLINE) != 0)
		record.no_inline = canonical.no_inline = true;
	if ((attributes & FUNCTION_CONTROL_STABLE_PREFIX_QUERY) != 0)
	{
		ValidateStablePrefixFunction(*program, record.type);
		record.stable_prefix_query = canonical.stable_prefix_query = true;
	}
	const FunctionMemoryEffects effects =
		(attributes & FUNCTION_CONTROL_CONST) != 0 ?
			FUNCTION_EFFECTS_READNONE :
		(attributes & FUNCTION_CONTROL_PURE) != 0 ?
			FUNCTION_EFFECTS_READONLY : FUNCTION_EFFECTS_DEFAULT;
	if (effects > record.function_effects) record.function_effects = effects;
	if (effects > canonical.function_effects)
		canonical.function_effects = effects;
}

void Analyzer::ApplyClassAbiTagAttributes(
	NodeId declaration, EntityId entity)
{
	if (entity == kNoEntity || entity >= program_->entities.size())
		throw std::logic_error("ABI-tagged class has no semantic entity");
	std::vector<NameId> tags;
	CollectDirectAbiTags(*arena_, program_, declaration, &tags);
	EntityRecord& record = program_->entities[entity];
	MergeAbiTags(program_, tags, &record.abi_tag_begin, &record.abi_tag_count);
}

void Analyzer::ApplyFunctionAbiTagAttributes(
	NodeId declaration, BindingId binding)
{
	if (binding == kNoBinding || binding >= program_->bindings.size())
		throw std::logic_error("ABI-tagged function has no semantic binding");
	BindingRecord& record = program_->bindings[binding];
	if (record.canonical == kNoBinding ||
		record.canonical >= program_->bindings.size())
		throw std::logic_error("attributed function has no canonical binding");
	BindingRecord& canonical = program_->bindings[record.canonical];
	ApplyFunctionControlAttributes(program_, binding,
		FunctionControlAttributeMask(*arena_, declaration));
	std::vector<NameId> tags;
	CollectFunctionAbiTags(*arena_, program_, declaration, &tags);
	if (tags.empty()) return;
	MergeAbiTags(program_, tags,
		&canonical.abi_tag_begin, &canonical.abi_tag_count);
	record.abi_tag_begin = canonical.abi_tag_begin;
	record.abi_tag_count = canonical.abi_tag_count;
}

}
}
