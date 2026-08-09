#include "pa12_semantic_detail.h"

#include <cstdint>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::RecordExpressionFacts(const ExpressionInfo& value)
{
	if (value.node == kNoDumpEdge) return;
	DumpNode& node = dump_.nodes[value.node];
	node.constant = value.constant;
	node.constant_value = value.value;
}

bool SemanticAnalyzer::IsUnsignedIntegral(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind == TYPE_NAMED)
	{
		const EntityRecord& entity = program_->entities[record.entity];
		return entity.underlying != kNoType &&
			IsUnsignedIntegral(entity.underlying);
	}
	if (record.kind != TYPE_FUNDAMENTAL) return false;
	switch (record.fundamental)
	{
	case FUND_UNSIGNED_CHAR:
	case FUND_UNSIGNED_SHORT_INT:
	case FUND_UNSIGNED_INT:
	case FUND_UNSIGNED_LONG_INT:
	case FUND_UNSIGNED_LONG_LONG_INT:
	case FUND_CHAR16_T:
	case FUND_CHAR32_T:
		return true;
	default:
		return false;
	}
}

std::size_t SemanticAnalyzer::IntegralWidth(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind == TYPE_NAMED)
	{
		const EntityRecord& entity = program_->entities[record.entity];
		if (entity.underlying == kNoType)
			throw std::logic_error("integral named type has no underlying type");
		return IntegralWidth(entity.underlying);
	}
	if (record.kind != TYPE_FUNDAMENTAL || !IsIntegral(type))
		throw std::logic_error("integral width requested for non-integral type");
	if (record.fundamental == FUND_BOOL) return 1;
	return program_->SizeOf(type) * 8;
}

std::int64_t SemanticAnalyzer::NormalizeIntegralConstant(TypeId type,
	std::int64_t value) const
{
	const std::size_t width = IntegralWidth(type);
	if (width == 1) return value != 0;
	if (width > 64 || width == 0)
		throw std::logic_error("unsupported integral constant width");
	std::uint64_t bits = static_cast<std::uint64_t>(value);
	if (width < 64)
	{
		const std::uint64_t mask = (std::uint64_t(1) << width) - 1;
		bits &= mask;
		if (!IsUnsignedIntegral(type) &&
			(bits & (std::uint64_t(1) << (width - 1))) != 0)
			bits |= ~mask;
	}
	return static_cast<std::int64_t>(bits);
}

void SemanticAnalyzer::AnalyzeStaticAssert(NodeId node, ScopeId scope)
{
	const NodeId condition_syntax = FirstSemanticChild(node);
	if (condition_syntax == kNoNode)
		throw std::runtime_error("static_assert has no condition");
	const ExpressionInfo condition = AnalyzeExpression(condition_syntax, scope);
	if (!IsIntegral(condition.type, true) || !condition.constant)
		throw std::runtime_error(
			"static_assert requires an integral constant expression");
	if (condition.value == 0)
		throw std::runtime_error("static assertion failed");
}

}
}
