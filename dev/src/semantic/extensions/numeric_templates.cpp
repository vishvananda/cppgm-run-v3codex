#include "semantic/analysis/analyzer.h"

namespace cppgm
{
namespace semantic
{

std::size_t Analyzer::TemplatePartialBitIntPackParameter(
	const TypeRecord& type,
	const std::vector<TemplateParameter>& parameters) const
{
	return type.dependent_bound_parameter < parameters.size() &&
		parameters[type.dependent_bound_parameter].pack ?
		type.dependent_bound_parameter : parameters.size();
}

bool Analyzer::DeduceTemplatePartialBitIntType(
	const TypeRecord& pattern, const TypeRecord& argument,
	const std::vector<TemplateParameter>& parameters,
	FunctionTemplateDeduction* deduced) const
{
	if (pattern.bitint_unsigned != argument.bitint_unsigned) return false;
	if (pattern.dependent_bound_parameter == kNoTemplateParameter)
		return argument.dependent_bound_parameter == kNoTemplateParameter &&
			pattern.bound == argument.bound;
	const std::size_t dependent = pattern.dependent_bound_parameter;
	if (dependent >= parameters.size() ||
		parameters[dependent].kind != TEMPLATE_ARGUMENT_INTEGRAL ||
		argument.bound == 0) return false;
	const TemplateArgument pattern_width(TEMPLATE_ARGUMENT_INTEGRAL,
		pattern.dependent_bound_type, 0,
		static_cast<std::uint32_t>(dependent));
	const TemplateArgument argument_width(TEMPLATE_ARGUMENT_INTEGRAL,
		argument.dependent_bound_parameter == kNoTemplateParameter ?
			pattern.dependent_bound_type : argument.dependent_bound_type,
		static_cast<std::int64_t>(argument.bound),
		argument.dependent_bound_parameter);
	return DeduceTemplatePartialArgument(
		pattern_width, argument_width, parameters, deduced);
}

bool Analyzer::DeduceTemplatePartialVectorType(
	const TypeRecord& pattern, const TypeRecord& argument,
	const std::vector<TemplateParameter>& parameters,
	FunctionTemplateDeduction* deduced) const
{
	if (pattern.dependent_bound_parameter != kNoTemplateParameter)
	{
		const std::size_t dependent = pattern.dependent_bound_parameter;
		if (dependent >= parameters.size() ||
			parameters[dependent].kind != TEMPLATE_ARGUMENT_INTEGRAL ||
			argument.dependent_bound_parameter != kNoTemplateParameter ||
			argument.bound == 0) return false;
		const std::size_t lane_bytes = program_->SizeOf(argument.child);
		if (lane_bytes == 0 || argument.bound % lane_bytes != 0) return false;
		const TemplateArgument pattern_lanes(TEMPLATE_ARGUMENT_INTEGRAL,
			pattern.dependent_bound_type, 0,
			static_cast<std::uint32_t>(dependent));
		const TemplateArgument argument_lanes(TEMPLATE_ARGUMENT_INTEGRAL,
			pattern.dependent_bound_type,
			static_cast<std::int64_t>(argument.bound / lane_bytes));
		if (!DeduceTemplatePartialArgument(
			pattern_lanes, argument_lanes, parameters, deduced)) return false;
	}
	else if (argument.dependent_bound_parameter != kNoTemplateParameter ||
		pattern.bound != argument.bound) return false;
	return DeduceTemplatePartialType(
		pattern.child, argument.child, parameters, deduced);
}

}
}
