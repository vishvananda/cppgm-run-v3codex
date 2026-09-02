#include "semantic/extensions/lambda_capture.h"
#include "support/exceptions.h"
#include "support/scoped_state.h"

#include <algorithm>
#include <limits>
#include <string>

namespace cppgm
{
namespace semantic
{

using namespace syntax;

namespace
{

const std::uint32_t kNoIndex = std::numeric_limits<std::uint32_t>::max();

bool IsControlScope(const SyntaxArena& arena, NodeId node)
{
	return arena.IsTag(node, ::cppgm::syntax::STAG_IF_STATEMENT) ||
		arena.IsTag(node, ::cppgm::syntax::STAG_SWITCH_STATEMENT) ||
		arena.IsTag(node, ::cppgm::syntax::STAG_WHILE_STATEMENT) ||
		arena.IsTag(node, ::cppgm::syntax::STAG_DO_STATEMENT) ||
		arena.IsTag(node, ::cppgm::syntax::STAG_FOR_STATEMENT) ||
		arena.IsTag(node, ::cppgm::syntax::STAG_HANDLER);
}

}

LambdaCaptureUseTable::Fact::Fact()
	: syntax(kNoNode), name_begin(0), name_count(0), captures_this(false)
{
}

LambdaCaptureUseTable::LambdaCaptureUseTable()
	: slots_(32, 0), depth_(0), dedup_generation_(0),
	  requests_(0), cache_hits_(0),
	  syntax_visits_(0), name_uses_(0)
{
}

std::uint32_t LambdaCaptureUseTable::FindFact(NodeId lambda) const
{
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = MixHash(0, lambda) & mask;
	while (slots_[slot] != 0)
	{
		const std::uint32_t fact = slots_[slot] - 1;
		if (facts_[fact].syntax == lambda) return fact;
		slot = (slot + 1) & mask;
	}
	return kNoIndex;
}

void LambdaCaptureUseTable::Rehash(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < facts_.size(); ++i)
	{
		std::size_t slot = MixHash(0, facts_[i].syntax) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	slots_.swap(replacement);
}

std::uint32_t LambdaCaptureUseTable::EnsureFact(NodeId lambda)
{
	const std::uint32_t existing = FindFact(lambda);
	if (existing != kNoIndex) return existing;
	if ((facts_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	if (facts_.size() >= std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit("too many lambda capture syntax facts");
	const std::uint32_t fact = static_cast<std::uint32_t>(facts_.size());
	facts_.push_back(Fact());
	facts_.back().syntax = lambda;
	states_.push_back(0);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = MixHash(0, lambda) & mask;
	while (slots_[slot] != 0) slot = (slot + 1) & mask;
	slots_[slot] = fact + 1;
	return fact;
}

const LambdaCaptureUseTable::Fact& LambdaCaptureUseTable::FindOrBuild(
	const SyntaxArena& arena, NodeId lambda)
{
	++requests_;
	const std::uint32_t found = FindFact(lambda);
	if (found != kNoIndex)
	{
		if (states_[found] == 2)
		{
			++cache_hits_;
			return facts_[found];
		}
		if (states_[found] == 1)
			ThrowInternalCompilerError("recursive lambda capture syntax");
		if (states_[found] == 3)
			ThrowSemanticError(
				"lambda capture syntax analysis previously failed");
	}
	const std::uint32_t fact = found == kNoIndex ? EnsureFact(lambda) : found;
	Build(fact, arena);
	return facts_[fact];
}

NameId LambdaCaptureUseTable::NameAt(
	const Fact& fact, std::size_t index) const
{
	if (index >= fact.name_count ||
		static_cast<std::size_t>(fact.name_begin) + index >= names_.size())
		ThrowInternalCompilerError("lambda capture name index is invalid");
	return names_[static_cast<std::size_t>(fact.name_begin) + index];
}

bool LambdaCaptureUseTable::IsExplicitAt(
	const Fact& fact, std::size_t index) const
{
	if (index >= fact.name_count ||
		static_cast<std::size_t>(fact.name_begin) + index >=
			explicit_names_.size())
		ThrowInternalCompilerError("lambda capture flag index is invalid");
	return explicit_names_[static_cast<std::size_t>(fact.name_begin) + index] != 0;
}

bool LambdaCaptureUseTable::IsReferenceAt(
	const Fact& fact, std::size_t index) const
{
	if (index >= fact.name_count ||
		static_cast<std::size_t>(fact.name_begin) + index >=
			reference_names_.size())
		ThrowInternalCompilerError("lambda capture mode index is invalid");
	return reference_names_[static_cast<std::size_t>(fact.name_begin) + index] != 0;
}

void LambdaCaptureUseTable::AddBound(NameId name)
{
	if (name == 0) return;
	if (bound_heads_.size() <= name)
		bound_heads_.resize(static_cast<std::size_t>(name) + 1, 0);
	bound_stack_.push_back(BoundName(name, bound_heads_[name], depth_));
	bound_heads_[name] = static_cast<std::uint32_t>(bound_stack_.size());
}

bool LambdaCaptureUseTable::IsBound(NameId name) const
{
	if (name == 0 || name >= bound_heads_.size() ||
		bound_heads_[name] == 0) return false;
	return bound_stack_[bound_heads_[name] - 1].depth == depth_;
}

void LambdaCaptureUseTable::RestoreBounds(std::size_t mark)
{
	while (bound_stack_.size() > mark)
	{
		const BoundName bound = bound_stack_.back();
		bound_stack_.pop_back();
		bound_heads_[bound.name] = bound.previous;
	}
}

NameId LambdaCaptureUseTable::DeclaratorName(
	const SyntaxArena& arena, NodeId node) const
{
	if (arena.IsTag(node, ::cppgm::syntax::STAG_IDENTIFIER))
	{
		const NameId identity = arena.SemanticPayloadId(node);
		if (identity != 0) return identity;
		const std::string& spelling = arena.Payload(node);
		if (spelling.empty() || spelling.find("::") != std::string::npos)
			return 0;
		return arena.SharedStrings().Intern(spelling);
	}
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NameId name = DeclaratorName(arena, arena.EdgeChild(edge));
		if (name != 0) return name;
	}
	return 0;
}

NameId LambdaCaptureUseTable::SimpleExpressionName(
	const SyntaxArena& arena, NodeId node) const
{
	NodeId structured = kNoNode;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if (arena.IsTag(child, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME)) structured = child;
	}
	if (structured != kNoNode)
	{
		NameId only = 0;
		std::size_t components = 0;
		for (std::uint32_t edge = arena.FirstEdge(structured);
			edge != kNoEdge; edge = arena.NextEdge(edge))
		{
			const NodeId child = arena.EdgeChild(edge);
			if (arena.IsTag(child, ::cppgm::syntax::STAG_GLOBAL_QUALIFIER)) return 0;
			if (!arena.IsTag(child, ::cppgm::syntax::STAG_NAME_COMPONENT)) continue;
			only = arena.SemanticPayloadId(child);
			++components;
		}
		return components == 1 ? only : 0;
	}
	const NameId identity = arena.SemanticPayloadId(node);
	if (identity != 0) return identity;
	const std::string& spelling = arena.Payload(node);
	if (spelling.empty() || spelling.find("::") != std::string::npos ||
		spelling.find(' ') != std::string::npos ||
		spelling.compare(0, 8, "operator") == 0) return 0;
	return arena.SharedStrings().Intern(spelling);
}

void LambdaCaptureUseTable::WalkSimpleDeclaration(NodeId node,
	const SyntaxArena& arena, std::vector<NameId>* free_names,
	bool* captures_this)
{
	NodeId list = kNoNode;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if (arena.IsTag(child, ::cppgm::syntax::STAG_INIT_DECLARATOR_LIST)) list = child;
	}
	if (list == kNoNode) return;
	for (std::uint32_t edge = arena.FirstEdge(list); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId item = arena.EdgeChild(edge);
		NodeId declarator = kNoNode;
		for (std::uint32_t item_edge = arena.FirstEdge(item);
			item_edge != kNoEdge; item_edge = arena.NextEdge(item_edge))
		{
			const NodeId child = arena.EdgeChild(item_edge);
			if (arena.IsTag(child, ::cppgm::syntax::STAG_DECLARATOR)) declarator = child;
		}
		if (declarator != kNoNode)
		{
			AddBound(DeclaratorName(arena, declarator));
			Walk(declarator, arena, free_names, captures_this);
		}
		for (std::uint32_t item_edge = arena.FirstEdge(item);
			item_edge != kNoEdge; item_edge = arena.NextEdge(item_edge))
		{
			const NodeId child = arena.EdgeChild(item_edge);
			if (child != declarator)
				Walk(child, arena, free_names, captures_this);
		}
	}
}

void LambdaCaptureUseTable::WalkDeclaratorDeclaration(NodeId node,
	const SyntaxArena& arena, std::vector<NameId>* free_names,
	bool* captures_this)
{
	NodeId declarator = kNoNode;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if (arena.IsTag(child, ::cppgm::syntax::STAG_DECLARATOR)) declarator = child;
	}
	if (declarator != kNoNode)
	{
		AddBound(DeclaratorName(arena, declarator));
		Walk(declarator, arena, free_names, captures_this);
	}
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if (child != declarator && !arena.IsTag(child, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ))
			Walk(child, arena, free_names, captures_this);
	}
}

void LambdaCaptureUseTable::WalkRangeFor(NodeId node,
	const SyntaxArena& arena, std::vector<NameId>* free_names,
	bool* captures_this)
{
	const std::size_t mark = bound_stack_.size();
	NodeId declaration = kNoNode;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if (arena.IsTag(child, ::cppgm::syntax::STAG_RANGE_INITIALIZER))
			Walk(child, arena, free_names, captures_this);
		else if (arena.IsTag(child, ::cppgm::syntax::STAG_RANGE_DECLARATION))
			declaration = child;
	}
	if (declaration != kNoNode)
		WalkDeclaratorDeclaration(
			declaration, arena, free_names, captures_this);
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if (child != declaration &&
			!arena.IsTag(child, ::cppgm::syntax::STAG_RANGE_INITIALIZER))
			Walk(child, arena, free_names, captures_this);
	}
	RestoreBounds(mark);
}

void LambdaCaptureUseTable::Walk(NodeId node, const SyntaxArena& arena,
	std::vector<NameId>* free_names, bool* captures_this)
{
	++syntax_visits_;
	if (arena.IsTag(node, ::cppgm::syntax::STAG_LAMBDA_EXPRESSION))
	{
		const Fact& nested = FindOrBuild(arena, node);
		for (std::size_t i = 0; i < nested.name_count; ++i)
		{
			const NameId name = NameAt(nested, i);
			if (!IsBound(name)) free_names->push_back(name);
		}
		*captures_this = *captures_this || nested.captures_this;
		return;
	}
	if (arena.IsTag(node, ::cppgm::syntax::STAG_ID_EXPRESSION))
	{
		const NameId name = SimpleExpressionName(arena, node);
		if (name != 0 && !IsBound(name)) free_names->push_back(name);
		return;
	}
	if (arena.IsTag(node, ::cppgm::syntax::STAG_KEYWORD_LITERAL) &&
		arena.SemanticPayload(node) == "this")
	{
		*captures_this = true;
		return;
	}
	if (arena.IsTag(node, ::cppgm::syntax::STAG_SIMPLE_DECLARATION))
	{
		WalkSimpleDeclaration(node, arena, free_names, captures_this);
		return;
	}
	if (arena.IsTag(node, ::cppgm::syntax::STAG_CONDITION_DECLARATION) ||
		arena.IsTag(node, ::cppgm::syntax::STAG_EXCEPTION_DECLARATION))
	{
		WalkDeclaratorDeclaration(node, arena, free_names, captures_this);
		return;
	}
	if (arena.IsTag(node, ::cppgm::syntax::STAG_RANGE_FOR_STATEMENT))
	{
		WalkRangeFor(node, arena, free_names, captures_this);
		return;
	}
	const bool scoped = arena.IsTag(node, ::cppgm::syntax::STAG_COMPOUND_STATEMENT) ||
		IsControlScope(arena, node);
	const std::size_t mark = bound_stack_.size();
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
		Walk(arena.EdgeChild(edge), arena, free_names, captures_this);
	if (scoped) RestoreBounds(mark);
}

void LambdaCaptureUseTable::Build(std::uint32_t fact,
	const SyntaxArena& arena)
{
	states_[fact] = 1;
	const std::size_t mark = bound_stack_.size();
	ScopedValueRestore<std::uint32_t> depth(&depth_, depth_ + 1);
	const auto restore_bounds = [this, mark]() { RestoreBounds(mark); };
	ScopedCleanup<decltype(restore_bounds)> bounds(restore_bounds);
	const auto fail_fact = [this, fact]() { states_[fact] = 3; };
	ScopedCleanup<decltype(fail_fact)> failure(fail_fact);
	std::vector<NameId> free_names;
	std::vector<NameId> explicit_names;
	std::vector<std::uint8_t> explicit_reference;
	bool captures_this = false;
	bool default_reference = false;
	bool has_default = false;
	{
		const NodeId lambda = facts_[fact].syntax;
		NodeId body = kNoNode;
		for (std::uint32_t edge = arena.FirstEdge(lambda); edge != kNoEdge;
			edge = arena.NextEdge(edge))
		{
			const NodeId child = arena.EdgeChild(edge);
			if (arena.IsTag(child, ::cppgm::syntax::STAG_LAMBDA_INTRODUCER))
			{
				for (std::uint32_t capture_edge = arena.FirstEdge(child);
					capture_edge != kNoEdge;
					capture_edge = arena.NextEdge(capture_edge))
				{
					const NodeId capture = arena.EdgeChild(capture_edge);
					if (arena.IsTag(capture,
						"lambda-capture-default-reference"))
					{
						default_reference = true;
						has_default = true;
					}
					else if (arena.IsTag(capture,
						"lambda-capture-default-copy"))
						has_default = true;
					else if (arena.IsTag(capture, ::cppgm::syntax::STAG_LAMBDA_CAPTURE_THIS))
					{
						if (captures_this)
							ThrowSemanticError(
								"duplicate explicit this capture");
						captures_this = true;
					}
					else if (arena.IsTag(capture,
							"lambda-capture-reference") ||
						arena.IsTag(capture,
							"lambda-capture-reference-pack"))
					{
						const NameId name = arena.SemanticPayloadId(capture);
						free_names.push_back(name);
						explicit_names.push_back(name);
						explicit_reference.push_back(1);
					}
					else if (arena.IsTag(capture, ::cppgm::syntax::STAG_LAMBDA_CAPTURE_COPY) ||
						arena.IsTag(capture, ::cppgm::syntax::STAG_LAMBDA_CAPTURE_COPY_PACK))
					{
						const NameId name = arena.SemanticPayloadId(capture);
						free_names.push_back(name);
						explicit_names.push_back(name);
						explicit_reference.push_back(0);
					}
				}
			}
			else if (arena.IsTag(child, ::cppgm::syntax::STAG_LAMBDA_DECLARATOR))
			{
				for (std::uint32_t declaration_edge = arena.FirstEdge(child);
					declaration_edge != kNoEdge;
					declaration_edge = arena.NextEdge(declaration_edge))
				{
					const NodeId clause = arena.EdgeChild(declaration_edge);
					if (arena.IsTag(clause, ::cppgm::syntax::STAG_TEMPLATE_PARAMETER_CLAUSE))
					{
						const NodeId list = arena.FirstEdge(clause) == kNoEdge ?
							kNoNode : arena.EdgeChild(arena.FirstEdge(clause));
						for (std::uint32_t parameter_edge =
							list == kNoNode ? kNoEdge : arena.FirstEdge(list);
							parameter_edge != kNoEdge;
							parameter_edge = arena.NextEdge(parameter_edge))
							AddBound(DeclaratorName(
								arena, arena.EdgeChild(parameter_edge)));
						continue;
					}
					if (!arena.IsTag(clause, ::cppgm::syntax::STAG_PARAMETER_CLAUSE)) continue;
					for (std::uint32_t parameter_edge = arena.FirstEdge(clause);
						parameter_edge != kNoEdge;
						parameter_edge = arena.NextEdge(parameter_edge))
					{
						const NodeId parameter = arena.EdgeChild(parameter_edge);
						if (arena.IsTag(parameter, ::cppgm::syntax::STAG_PARAMETER_DECLARATION))
							AddBound(DeclaratorName(arena, parameter));
					}
				}
			}
			else if (arena.IsTag(child, ::cppgm::syntax::STAG_COMPOUND_STATEMENT)) body = child;
		}
		if (has_default && body != kNoNode)
			Walk(body, arena, &free_names, &captures_this);
		++dedup_generation_;
		if (dedup_generation_ == 0)
		{
			std::fill(dedup_marks_.begin(), dedup_marks_.end(), 0);
			++dedup_generation_;
		}
		std::size_t retained = 0;
		for (std::size_t i = 0; i < free_names.size(); ++i)
		{
			const NameId name = free_names[i];
			if (dedup_marks_.size() <= name)
				dedup_marks_.resize(static_cast<std::size_t>(name) + 1, 0);
			if (dedup_marks_[name] == dedup_generation_)
			{
				if (i < explicit_names.size())
					ThrowSemanticError("duplicate explicit lambda capture");
				continue;
			}
			dedup_marks_[name] = dedup_generation_;
			free_names[retained++] = name;
		}
		free_names.resize(retained);
		if (names_.size() + free_names.size() >
			std::numeric_limits<std::uint32_t>::max())
			ThrowSemanticResourceLimit("too many lambda capture name uses");
		Fact& completed = facts_[fact];
		completed.name_begin = static_cast<std::uint32_t>(names_.size());
		completed.name_count = static_cast<std::uint32_t>(free_names.size());
		completed.captures_this = captures_this;
		names_.insert(names_.end(), free_names.begin(), free_names.end());
		for (std::size_t i = 0; i < free_names.size(); ++i)
		{
			explicit_names_.push_back(i < explicit_names.size() ? 1 : 0);
			reference_names_.push_back(i < explicit_reference.size() ?
				explicit_reference[i] : default_reference ? 1 : 0);
		}
		name_uses_ += free_names.size();
		states_[fact] = 2;
	}
	failure.Release();
}

std::size_t LambdaCaptureUseTable::Requests() const { return requests_; }
std::size_t LambdaCaptureUseTable::CacheHits() const { return cache_hits_; }
std::size_t LambdaCaptureUseTable::SyntaxVisits() const
	{ return syntax_visits_; }
std::size_t LambdaCaptureUseTable::NameUses() const { return name_uses_; }

std::size_t LambdaCaptureUseTable::StorageBytes() const
{
	return facts_.capacity() * sizeof(Fact) +
		states_.capacity() * sizeof(std::uint8_t) +
		slots_.capacity() * sizeof(std::uint32_t) +
		names_.capacity() * sizeof(NameId) +
		explicit_names_.capacity() * sizeof(std::uint8_t) +
		reference_names_.capacity() * sizeof(std::uint8_t) +
		bound_heads_.capacity() * sizeof(std::uint32_t) +
		bound_stack_.capacity() * sizeof(BoundName) +
		dedup_marks_.capacity() * sizeof(std::uint32_t);
}

}
}
