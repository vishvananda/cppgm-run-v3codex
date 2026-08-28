// Fixed operator and keyword vocabulary classification: one packed
// simple-token kind per distinct payload or operation name, classified once
// at the semantic input boundary and memoized by ID.
#include "pa12_semantic_detail.h"

namespace cppgm { namespace pa12_semantic_detail {

namespace
{

// Maps a rendered simple-token name such as "OP_PLUS" back to its kind once;
// the table is built from the same vocabulary that rendered the text.
std::uint8_t ClassifyOperationText(const std::string& text)
{
	const std::size_t colon = text.find(':');
	if (colon == std::string::npos)
	{
		// Synthesized operations carry the bare fixed spelling.
		SimpleTokenKind kind = OP_PLUS;
		return ClassifySimpleSpelling(text, &kind) ?
			static_cast<std::uint8_t>(kind + 1) : 0;
	}
	const std::string prefix = text.substr(0, colon);
	for (int kind = 0; kind <= OP_ARROW; ++kind)
		if (prefix == SimpleTokenKindName(static_cast<SimpleTokenKind>(kind)))
			return static_cast<std::uint8_t>(kind + 1);
	return 0;
}

}

int ClassifyOperationSpelling(const std::string& operation)
{
	SimpleTokenKind kind = OP_PLUS;
	return ClassifySimpleSpelling(operation, &kind) ?
		static_cast<int>(kind) : -1;
}

int SemanticAnalyzer::PayloadTokenKind(NodeId node)
{
	const ::cppgm::syntax::TextId payload =
		arena_->SemanticPayloadId(node);
	if (payload == 0) return -1;
	if (payload_token_kind_.size() <= payload)
		payload_token_kind_.resize(
			static_cast<std::size_t>(payload) + 1, 255);
	std::uint8_t& slot = payload_token_kind_[payload];
	if (slot == 255)
	{
		SimpleTokenKind kind = OP_PLUS;
		slot = ClassifySimpleSpelling(arena_->SemanticPayload(node), &kind) ?
			static_cast<std::uint8_t>(kind + 1) : 0;
	}
	return static_cast<int>(slot) - 1;
}

std::uint8_t SemanticAnalyzer::OperationKindForName(NameId text)
{
	if (text == 0) return 0;
	if (operation_kind_by_name_.size() <= text)
		operation_kind_by_name_.resize(
			static_cast<std::size_t>(text) + 1, 255);
	std::uint8_t& slot = operation_kind_by_name_[text];
	if (slot == 255) slot = ClassifyOperationText(program_->names.Get(text));
	return slot;
}

} }
