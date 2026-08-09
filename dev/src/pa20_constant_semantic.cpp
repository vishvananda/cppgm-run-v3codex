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

bool SemanticAnalyzer::TryFoldConstantClassConversion(
	const ExpressionInfo& value, BindingId conversion, TypeId target,
	std::int64_t* result)
{
	const EntityId entity = EntityOf(value.type);
	if (entity == kNoEntity || !IsIntegral(target, true)) return false;
	const EntityRecord& object = program_->entities[entity];
	if (!object.empty_class || !object.trivial_default_constructor ||
		!object.trivial_destructor) return false;
	const FunctionInfo& function = GetFunction(conversion);
	if (!function.conversion_function || !function.constexpr_function ||
		function.definition_body == kNoNode ||
		program_->types.RemoveTopCv(EffectiveType(function.conversion_target)) !=
		program_->types.RemoveTopCv(EffectiveType(target))) return false;
	NodeId statement = kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(function.definition_body);
		edge != kNoEdge; edge = arena_->NextEdge(edge))
	{
		if (statement != kNoNode) return false;
		statement = arena_->EdgeChild(edge);
	}
	if (statement == kNoNode || !arena_->IsTag(statement, "return-statement"))
		return false;
	const NodeId expression = FirstSemanticChild(statement);
	if (expression == kNoNode || !arena_->IsTag(expression, "id-expression"))
		return false;
	const NamePath path = StructuredNamePath(expression);
	const NameId name = path.Empty() ?
		program_->names.Intern(PayloadSource(expression)) : path.Last();
	const LookupResult found = program_->LookupMember(
		entity, name, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding) return false;
	const BindingRecord& member = program_->bindings[
		program_->bindings[found.ordinary].canonical];
	if (member.kind != BIND_VARIABLE || member.non_static_data_member ||
		!member.constant || !IsIntegral(member.type, true)) return false;
	*result = NormalizeIntegralConstant(target, member.value);
	return true;
}

ExpressionInfo SemanticAnalyzer::ApplyClassObjectTarget(
	ExpressionInfo value, TypeId target)
{
	const CallConversionFact conversion =
		ConvertingFunction(value, target, false);
	if (conversion.rank == CONVERSION_INVALID)
		return ApplyTarget(value, target);
	std::int64_t constant = 0;
	if (conversion.conversion_function != kNoBinding &&
		TryFoldConstantClassConversion(
			value, conversion.conversion_function, target, &constant))
	{
		ExpressionInfo folded = MakeLiteral(target, InternNumber(constant));
		folded.constant = true;
		folded.value = constant;
		RecordExpressionFacts(folded);
		return folded;
	}
	return ApplyCallArgument(value, target, &conversion);
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
