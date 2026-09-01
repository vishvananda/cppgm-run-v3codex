#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <limits>

namespace cppgm
{
namespace semantic
{

bool IsExtendedFloatingFundamental(FundamentalKind kind)
{
	return kind == FUND_FLOAT || kind == FUND_DOUBLE ||
		kind == FUND_LONG_DOUBLE || kind == FUND_FLOAT16 ||
		kind == FUND_FLOAT32 || kind == FUND_FLOAT32X ||
		kind == FUND_FLOAT64 || kind == FUND_FLOAT64X ||
		kind == FUND_STDFLOAT128 || kind == FUND_FLOAT128;
}

int FloatingConversionRank(FundamentalKind kind)
{
	switch (kind)
	{
	case FUND_FLOAT16: return 0;
	case FUND_FLOAT: case FUND_FLOAT32: return 1;
	case FUND_DOUBLE: case FUND_FLOAT32X: case FUND_FLOAT64: return 2;
	case FUND_LONG_DOUBLE: case FUND_FLOAT64X: return 3;
	case FUND_STDFLOAT128: case FUND_FLOAT128: return 4;
	default: return -1;
	}
}

TypeId Analyzer::HostedSpecifierType(
	const std::string& spelling) const
{
	FundamentalKind kind = FUND_VOID;
	if (spelling == "__int128_t") kind = FUND_INT128;
	else if (spelling == "__uint128_t") kind = FUND_UINT128;
	else if (spelling == "_Float16") kind = FUND_FLOAT16;
	else if (spelling == "_Float32") kind = FUND_FLOAT32;
	else if (spelling == "_Float32x") kind = FUND_FLOAT32X;
	else if (spelling == "_Float64") kind = FUND_FLOAT64;
	else if (spelling == "_Float64x") kind = FUND_FLOAT64X;
	else if (spelling == "_Float128") kind = FUND_STDFLOAT128;
	else if (spelling == "__float128") kind = FUND_FLOAT128;
	else if (spelling == "__builtin_va_list")
		return program_->types.Array(
			program_->types.Fundamental(FUND_UNSIGNED_LONG_INT), 3);
	else return kNoType;
	return program_->types.Fundamental(kind);
}

TypeId Analyzer::ApplyGnuVectorAttributes(
	NodeId node, TypeId type, ScopeId scope)
{
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId attribute = arena_->EdgeChild(edge);
		if (!arena_->IsTag(attribute, ::cppgm::syntax::STAG_GNU_ATTRIBUTE)) continue;
		const std::string name = arena_->SemanticPayload(attribute);
		const bool byte_width =
			name == "vector_size" || name == "__vector_size__";
		const bool lane_count =
			name == "ext_vector_type" || name == "__ext_vector_type__";
		if (!byte_width && !lane_count) continue;
		NodeId argument = kNoNode;
		bool identifier_argument = false;
		bool nonliteral_argument = false;
		for (std::uint32_t argument_edge = arena_->FirstEdge(attribute);
			argument_edge != kNoEdge; argument_edge = arena_->NextEdge(argument_edge))
		{
			const NodeId child = arena_->EdgeChild(argument_edge);
			if (arena_->IsTag(child, ::cppgm::syntax::STAG_GNU_ATTRIBUTE_NONLITERAL_ARGUMENT))
			{
				nonliteral_argument = true;
				continue;
			}
			const bool literal = arena_->IsTag(child, ::cppgm::syntax::STAG_GNU_ATTRIBUTE_ARGUMENT);
			const bool identifier = arena_->IsTag(
				child, "gnu-attribute-identifier-argument");
			if (!literal && !identifier) continue;
			if (argument != kNoNode)
				ThrowSemanticError(
					"GNU vector attribute requires one integral argument");
			argument = child;
			identifier_argument = identifier;
		}
		if (argument == kNoNode)
			ThrowSemanticError(
				"GNU vector attribute requires one integral argument");
		if (nonliteral_argument && !identifier_argument)
			ThrowSemanticError(
				"GNU vector attribute requires one integral argument");
		std::int64_t count = 0;
		TypeId dependent_type = kNoType;
		std::uint32_t dependent_parameter = kNoTemplateParameter;
		if (!identifier_argument)
			count = ParseInteger(arena_->SemanticPayload(argument));
		else
		{
			if (byte_width)
				ThrowSemanticError(
					"GNU vector_size requires an integer literal");
			const NameId argument_name = program_->names.UseInterned(
				arena_->SemanticPayloadId(argument));
			const LookupResult found =
				LookupName(scope, argument_name, LOOKUP_ORDINARY);
			if (found.ordinary == kNoBinding ||
				found.ordinary >= program_->bindings.size())
				ThrowSemanticError(
					"unknown GNU vector lane-count parameter");
			const BindingRecord& binding = program_->bindings[found.ordinary];
			if (binding.kind != BIND_PARAMETER || !IsIntegral(binding.type))
				ThrowSemanticError(
					"GNU vector lane count is not integral");
			if (binding.constant) count = binding.value;
			else if (program_->KindOfScope(binding.owner) ==
					SCOPE_TEMPLATE_PARAMETERS && binding.value >= 0 &&
				static_cast<std::uint64_t>(binding.value) < kNoTemplateParameter)
			{
				dependent_type = program_->types.RemoveTopCv(binding.type);
				dependent_parameter = static_cast<std::uint32_t>(binding.value);
			}
			else ThrowSemanticError(
				"GNU vector lane count is not a constant or template parameter");
		}
		if (dependent_parameter == kNoTemplateParameter && count <= 0)
			ThrowSemanticError("GNU vector width must be positive");
		const TypeRecord& top = program_->types.Get(type);
		const std::uint8_t cv = top.kind == TYPE_QUALIFIED ? top.cv : CV_NONE;
		const TypeId element = program_->types.RemoveTopCv(type);
		TypeId vector = kNoType;
		if (dependent_parameter != kNoTemplateParameter)
			vector = program_->types.TryDependentVector(
				element, dependent_type, dependent_parameter);
		else
		{
			std::uint64_t width = static_cast<std::uint64_t>(count);
			if (lane_count)
			{
				const std::size_t lane_bytes = program_->SizeOf(element);
				if (width > std::numeric_limits<std::uint64_t>::max() /
					lane_bytes)
					ThrowSemanticResourceLimit("GNU vector byte width overflows");
				width *= lane_bytes;
			}
			vector = program_->types.TryVector(element, width);
		}
		if (vector == kNoType)
			ThrowSemanticError(
				"invalid GNU vector element type or width");
		type = program_->types.Qualify(vector, cv);
	}
	return type;
}

}
}
