#include "pa12_semantic_detail.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::FunctionTemplateTypeIsDependent(TypeId type) const
{
	if (type == function_template_dependent_result_shape_ &&
		type != kNoType)
	{
		if (function_template_dependency_cache_.size() <= type)
			function_template_dependency_cache_.resize(
				static_cast<std::size_t>(type) + 1, 0);
		function_template_dependency_cache_[type] = 2;
		return true;
	}
	for (std::size_t i = 0; i < function_template_shape_parameters_.size(); ++i)
		if (type == function_template_shape_parameters_[i])
		{
			if (function_template_dependency_cache_.size() <= type)
				function_template_dependency_cache_.resize(
					static_cast<std::size_t>(type) + 1, 0);
			function_template_dependency_cache_[type] = 2;
			return true;
		}
	if (function_template_dependency_cache_.size() <= type)
		function_template_dependency_cache_.resize(
			static_cast<std::size_t>(type) + 1, 0);
	if (function_template_dependency_cache_[type] != 0)
		return function_template_dependency_cache_[type] == 2;
	// Mark the node non-dependent while descending so malformed cyclic metadata
	// cannot recurse indefinitely; a dependent child upgrades it below.
	function_template_dependency_cache_[type] = 1;
	const TypeRecord& record = program_->types.Get(type);
	bool dependent = false;
	switch (record.kind)
	{
	case TYPE_QUALIFIED:
	case TYPE_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
		dependent = FunctionTemplateTypeIsDependent(record.child);
		break;
	case TYPE_ARRAY:
		dependent = record.dependent_bound_parameter != kNoTemplateParameter ||
			FunctionTemplateTypeIsDependent(record.child);
		break;
	case TYPE_FUNCTION:
	{
		dependent = FunctionTemplateTypeIsDependent(record.child);
		const TypeId* parameters = program_->types.Parameters(type);
		for (std::size_t i = 0; i < record.parameter_count && !dependent; ++i)
			if (FunctionTemplateTypeIsDependent(parameters[i])) dependent = true;
		break;
	}
	case TYPE_MEMBER_POINTER:
		dependent = FunctionTemplateTypeIsDependent(record.child);
		break;
	case TYPE_NAMED:
	{
		const EntityRecord& entity = program_->entities[record.entity];
		if (record.entity >= class_template_pattern_by_entity_.size() ||
			class_template_pattern_by_entity_[record.entity] == kNoDumpEdge ||
			entity.template_argument_begin == kNoBinding)
			break;
		const std::uint32_t template_index =
			class_template_pattern_by_entity_[record.entity];
		if (template_index >= class_templates_.size()) break;
		const std::size_t first = entity.template_argument_begin;
		const std::size_t count = entity.template_argument_count;
		const std::vector<TemplateParameter>& template_parameters =
			class_templates_[template_index].parameters;
		if ((!HasTrailingTemplateParameterPack(template_parameters) &&
			 count != template_parameters.size()) ||
			(HasTrailingTemplateParameterPack(template_parameters) && count <
			 FixedTemplateParameterCount(template_parameters)) ||
			first > program_->template_arguments.size() ||
			count > program_->template_arguments.size() - first)
			throw std::logic_error(
				"invalid dependent class template argument range");
		for (std::size_t i = 0; i < count && !dependent; ++i)
		{
			if (first + i < program_->canonical_template_arguments.size() &&
				program_->canonical_template_arguments[first + i].IsDependent())
				dependent = true;
			else if (FunctionTemplateTypeIsDependent(
				program_->template_arguments[first + i])) dependent = true;
		}
		break;
	}
	case TYPE_FUNDAMENTAL:
	case TYPE_INVALID:
		break;
	}
	function_template_dependency_cache_[type] = dependent ? 2 : 1;
	return dependent;
}

int SemanticAnalyzer::CompareFunctionTemplateConstraints(
	const FunctionInfo& left, const FunctionInfo& right) const
{
	if (!left.template_specialization || !right.template_specialization ||
		left.template_pattern == kNoDumpEdge ||
		right.template_pattern == kNoDumpEdge ||
		left.template_pattern >= function_templates_.size() ||
		right.template_pattern >= function_templates_.size())
		return 0;
	const FunctionTemplatePattern& left_pattern =
		function_templates_[left.template_pattern];
	const FunctionTemplatePattern& right_pattern =
		function_templates_[right.template_pattern];
	const TypeRecord left_shape = program_->types.Get(left_pattern.shape_type);
	const TypeRecord right_shape = program_->types.Get(right_pattern.shape_type);
	if (left_shape.kind == TYPE_FUNCTION && right_shape.kind == TYPE_FUNCTION &&
		left_shape.parameter_count == right_shape.parameter_count)
	{
		const TypeId* left_types =
			program_->types.Parameters(left_pattern.shape_type);
		const TypeId* right_types =
			program_->types.Parameters(right_pattern.shape_type);
		bool left_accepts_right = true;
		bool right_accepts_left = true;
		for (std::size_t i = 0; i < left_shape.parameter_count; ++i)
		{
			left_accepts_right = left_accepts_right &&
				FunctionTemplatePatternAccepts(left_types[i], right_types[i]);
			right_accepts_left = right_accepts_left &&
				FunctionTemplatePatternAccepts(right_types[i], left_types[i]);
		}
		if (right_accepts_left != left_accepts_right)
			return right_accepts_left ? 1 : -1;
	}
	const std::size_t left_parameters = left_pattern.parameters.size();
	const std::size_t right_parameters = right_pattern.parameters.size();
	if (left_parameters == right_parameters) return 0;
	// Equal instantiated signatures expose equality constraints in the pattern:
	// fewer independent type parameters means the pattern accepted a strict
	// subset of the argument combinations accepted by the other candidate.
	return left_parameters < right_parameters ? 1 : -1;
}

bool SemanticAnalyzer::FunctionTemplatePatternAccepts(
	TypeId pattern, TypeId exemplar) const
{
	for (std::size_t i = 0;
		i < function_template_shape_parameters_.size(); ++i)
		if (pattern == function_template_shape_parameters_[i]) return true;
	if (pattern == exemplar) return true;
	const TypeRecord pattern_record = program_->types.Get(pattern);
	const TypeRecord exemplar_record = program_->types.Get(exemplar);
	if (pattern_record.kind != exemplar_record.kind) return false;
	switch (pattern_record.kind)
	{
	case TYPE_QUALIFIED:
		return (pattern_record.cv & ~exemplar_record.cv) == 0 &&
			FunctionTemplatePatternAccepts(
				pattern_record.child, exemplar_record.child);
	case TYPE_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
		return FunctionTemplatePatternAccepts(
			pattern_record.child, exemplar_record.child);
	case TYPE_ARRAY:
		return pattern_record.bound == exemplar_record.bound &&
			FunctionTemplatePatternAccepts(
				pattern_record.child, exemplar_record.child);
	case TYPE_FUNCTION:
	{
		if (pattern_record.parameter_count != exemplar_record.parameter_count ||
			pattern_record.variadic != exemplar_record.variadic ||
			pattern_record.cv != exemplar_record.cv ||
			pattern_record.ref_qualifier != exemplar_record.ref_qualifier ||
			!FunctionTemplatePatternAccepts(
				pattern_record.child, exemplar_record.child)) return false;
		const TypeId* pattern_parameters = program_->types.Parameters(pattern);
		const TypeId* exemplar_parameters = program_->types.Parameters(exemplar);
		for (std::size_t i = 0; i < pattern_record.parameter_count; ++i)
			if (!FunctionTemplatePatternAccepts(
				pattern_parameters[i], exemplar_parameters[i])) return false;
		return true;
	}
	case TYPE_MEMBER_POINTER:
		return pattern_record.entity == exemplar_record.entity &&
			FunctionTemplatePatternAccepts(
				pattern_record.child, exemplar_record.child);
	case TYPE_NAMED:
	case TYPE_FUNDAMENTAL:
	case TYPE_INVALID:
		return false;
	}
	return false;
}

bool SemanticAnalyzer::DeduceFunctionTemplateType(TypeId pattern,
	TypeId argument, std::vector<TypeId>* deduced) const
{
	for (std::size_t i = 0;
		i < function_template_shape_parameters_.size() && i < deduced->size(); ++i)
		if (pattern == function_template_shape_parameters_[i])
		{
			if ((*deduced)[i] != kNoType && (*deduced)[i] != argument)
				return false;
			(*deduced)[i] = argument;
			return true;
		}
	if (!FunctionTemplateTypeIsDependent(pattern)) return true;
	const TypeRecord& pattern_record = program_->types.Get(pattern);
	if (pattern_record.kind == TYPE_QUALIFIED)
	{
		const TypeRecord& argument_record = program_->types.Get(argument);
		if (argument_record.kind != TYPE_QUALIFIED)
			return DeduceFunctionTemplateType(pattern_record.child,
				argument, deduced);
		const std::uint8_t extra_cv = static_cast<std::uint8_t>(
			argument_record.cv & ~pattern_record.cv);
		TypeId adjusted = argument_record.child;
		if (extra_cv != CV_NONE)
			adjusted = program_->types.Qualify(adjusted, extra_cv);
		return DeduceFunctionTemplateType(pattern_record.child,
			adjusted, deduced);
	}
	const TypeRecord& argument_record = program_->types.Get(argument);
	if (pattern_record.kind != argument_record.kind) return false;
	switch (pattern_record.kind)
	{
	case TYPE_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
		return DeduceFunctionTemplateType(pattern_record.child,
			argument_record.child, deduced);
	case TYPE_ARRAY:
		return (pattern_record.bound == 0 ||
			pattern_record.bound == argument_record.bound) &&
			DeduceFunctionTemplateType(pattern_record.child,
				argument_record.child, deduced);
	case TYPE_FUNCTION:
	{
		if (pattern_record.parameter_count != argument_record.parameter_count ||
			pattern_record.variadic != argument_record.variadic ||
			pattern_record.cv != argument_record.cv ||
			pattern_record.ref_qualifier != argument_record.ref_qualifier ||
			!DeduceFunctionTemplateType(pattern_record.child,
				argument_record.child, deduced))
			return false;
		const TypeId* pattern_parameters = program_->types.Parameters(pattern);
		const TypeId* argument_parameters = program_->types.Parameters(argument);
		for (std::size_t i = 0; i < pattern_record.parameter_count; ++i)
			if (!DeduceFunctionTemplateType(pattern_parameters[i],
				argument_parameters[i], deduced)) return false;
		return true;
	}
	case TYPE_MEMBER_POINTER:
		return pattern_record.entity == argument_record.entity &&
			DeduceFunctionTemplateType(pattern_record.child,
				argument_record.child, deduced);
	case TYPE_NAMED:
	{
		if (pattern == argument) return true;
		const EntityId pattern_entity = pattern_record.entity;
		const EntityId argument_entity = argument_record.entity;
		if (pattern_entity >= class_template_pattern_by_entity_.size() ||
			argument_entity >= class_template_pattern_by_entity_.size())
			return false;
		const std::uint32_t pattern_template =
			class_template_pattern_by_entity_[pattern_entity];
		if (pattern_template == kNoDumpEdge ||
			pattern_template != class_template_pattern_by_entity_[argument_entity] ||
			pattern_template >= class_templates_.size())
			return false;
		const EntityRecord& pattern_owner = program_->entities[pattern_entity];
		const EntityRecord& argument_owner = program_->entities[argument_entity];
		const std::size_t pattern_first = pattern_owner.template_argument_begin;
		const std::size_t argument_first = argument_owner.template_argument_begin;
		const std::size_t count = pattern_owner.template_argument_count;
		if (argument_owner.template_argument_count != count ||
			pattern_first > program_->template_arguments.size() ||
			argument_first > program_->template_arguments.size() ||
			count > program_->template_arguments.size() - pattern_first ||
			count > program_->template_arguments.size() - argument_first)
			throw std::logic_error(
				"invalid class template deduction argument range");
		for (std::size_t i = 0; i < count; ++i)
			if (!DeduceFunctionTemplateType(
				program_->template_arguments[pattern_first + i],
				program_->template_arguments[argument_first + i], deduced))
				return false;
		return true;
	}
	case TYPE_FUNDAMENTAL:
	case TYPE_INVALID:
	case TYPE_QUALIFIED:
		return pattern == argument;
	}
	return false;
}

bool SemanticAnalyzer::DeduceFunctionTemplatePackArgument(
	const TemplateArgument& pattern, const TemplateArgument& argument,
	const std::vector<TemplateParameter>& parameters,
	FunctionTemplateDeduction* deduced) const
{
	std::size_t dependent = parameters.size();
	if (pattern.kind == TEMPLATE_ARGUMENT_TYPE)
	{
		for (std::size_t i = 0;
			i < function_template_shape_parameters_.size() &&
			i < parameters.size(); ++i)
			if (pattern.type == function_template_shape_parameters_[i])
			{
				dependent = i;
				break;
			}
	}
	else if (pattern.IsDependent())
		dependent = pattern.dependent_parameter;
	if (dependent != parameters.size())
	{
		if (dependent >= parameters.size() ||
			parameters[dependent].kind != argument.kind ||
			argument.IsDependent()) return false;
		if (parameters[dependent].pack)
		{
			std::size_t& position =
				deduced->pack_deduction_positions[dependent];
			if (position < deduced->pack_arguments[dependent].size())
			{
				if (deduced->pack_arguments[dependent][position] != argument)
					return false;
			}
			else deduced->pack_arguments[dependent].push_back(argument);
			++position;
			return true;
		}
		TemplateArgument& prior = deduced->fixed_arguments[dependent];
		if (prior.type != kNoType && prior != argument) return false;
		prior = argument;
		return true;
	}
	if (pattern.kind != argument.kind || argument.IsDependent()) return false;
	if (pattern.kind == TEMPLATE_ARGUMENT_TYPE)
		return DeduceFunctionTemplatePackType(
			pattern.type, argument.type, parameters, deduced);
	return pattern.type == argument.type && pattern.value == argument.value;
}

bool SemanticAnalyzer::DeduceFunctionTemplatePackType(TypeId pattern,
	TypeId argument, const std::vector<TemplateParameter>& parameters,
	FunctionTemplateDeduction* deduced) const
{
	for (std::size_t i = 0;
		i < function_template_shape_parameters_.size() &&
		i < parameters.size(); ++i)
		if (pattern == function_template_shape_parameters_[i])
			return DeduceFunctionTemplatePackArgument(
				TemplateArgument(TEMPLATE_ARGUMENT_TYPE, pattern),
				TemplateArgument(TEMPLATE_ARGUMENT_TYPE, argument),
				parameters, deduced);
	if (!FunctionTemplateTypeIsDependent(pattern)) return true;
	const TypeRecord& pattern_record = program_->types.Get(pattern);
	if (pattern_record.kind == TYPE_QUALIFIED)
	{
		const TypeRecord& argument_record = program_->types.Get(argument);
		if (argument_record.kind != TYPE_QUALIFIED)
			return DeduceFunctionTemplatePackType(pattern_record.child,
				argument, parameters, deduced);
		const std::uint8_t extra_cv = static_cast<std::uint8_t>(
			argument_record.cv & ~pattern_record.cv);
		TypeId adjusted = argument_record.child;
		if (extra_cv != CV_NONE)
			adjusted = program_->types.Qualify(adjusted, extra_cv);
		return DeduceFunctionTemplatePackType(pattern_record.child,
			adjusted, parameters, deduced);
	}
	const TypeRecord& argument_record = program_->types.Get(argument);
	if (pattern_record.kind != argument_record.kind) return false;
	switch (pattern_record.kind)
	{
	case TYPE_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
		return DeduceFunctionTemplatePackType(pattern_record.child,
			argument_record.child, parameters, deduced);
	case TYPE_ARRAY:
		return (pattern_record.bound == 0 ||
			pattern_record.bound == argument_record.bound) &&
			DeduceFunctionTemplatePackType(pattern_record.child,
				argument_record.child, parameters, deduced);
	case TYPE_FUNCTION:
	{
		if (pattern_record.parameter_count != argument_record.parameter_count ||
			pattern_record.variadic != argument_record.variadic ||
			pattern_record.cv != argument_record.cv ||
			pattern_record.ref_qualifier != argument_record.ref_qualifier ||
			!DeduceFunctionTemplatePackType(pattern_record.child,
				argument_record.child, parameters, deduced)) return false;
		const TypeId* pattern_parameters = program_->types.Parameters(pattern);
		const TypeId* argument_parameters = program_->types.Parameters(argument);
		for (std::size_t i = 0; i < pattern_record.parameter_count; ++i)
			if (!DeduceFunctionTemplatePackType(pattern_parameters[i],
				argument_parameters[i], parameters, deduced)) return false;
		return true;
	}
	case TYPE_MEMBER_POINTER:
		return pattern_record.entity == argument_record.entity &&
			DeduceFunctionTemplatePackType(pattern_record.child,
				argument_record.child, parameters, deduced);
	case TYPE_NAMED:
	{
		if (pattern == argument) return true;
		const EntityId pattern_entity = pattern_record.entity;
		const EntityId argument_entity = argument_record.entity;
		if (pattern_entity >= class_template_pattern_by_entity_.size())
			return false;
		const std::uint32_t class_pattern =
			class_template_pattern_by_entity_[pattern_entity];
		const std::uint32_t argument_pattern = argument_entity <
			class_template_pattern_by_entity_.size() ?
			class_template_pattern_by_entity_[argument_entity] : kNoDumpEdge;
		if (class_pattern == kNoDumpEdge ||
			class_pattern >= class_templates_.size())
			return false;
		const ClassTemplatePattern& pattern_template =
			class_templates_[class_pattern];
		if (pattern_template.template_parameter_proxy)
		{
			const std::size_t ordinal =
				pattern_template.template_parameter_ordinal;
			if (ordinal >= parameters.size() ||
				parameters[ordinal].kind != TEMPLATE_ARGUMENT_TEMPLATE ||
				argument_pattern == kNoDumpEdge ||
				argument_pattern >= class_templates_.size())
				return false;
			const ClassTemplatePattern& argument_template =
				class_templates_[argument_pattern];
			if (!TemplateTemplateParameterMatches(
				parameters[ordinal].template_parameters,
				argument_template.parameters)) return false;
			if (!DeduceFunctionTemplatePackArgument(
				TemplateArgument(TEMPLATE_ARGUMENT_TEMPLATE,
					program_->entities[pattern_template.marker_entity].type,
					0, static_cast<std::uint32_t>(ordinal)),
				TemplateArgument(TEMPLATE_ARGUMENT_TEMPLATE,
					program_->entities[argument_template.marker_entity].type),
				parameters, deduced)) return false;
		}
		else if (class_pattern != argument_pattern)
		{
			const EntityRecord& derived = program_->entities[argument_entity];
			for (std::size_t base = 0; base < derived.direct_base_count; ++base)
			{
				const EntityId base_entity =
					program_->DirectBase(argument_entity, base).entity;
				if (base_entity == kNoEntity ||
					base_entity >= program_->entities.size()) continue;
				FunctionTemplateDeduction trial = *deduced;
				if (DeduceFunctionTemplatePackType(pattern,
					program_->entities[base_entity].type, parameters, &trial))
				{
					*deduced = trial;
					return true;
				}
			}
			return false;
		}
		const EntityRecord& pattern_owner = program_->entities[pattern_entity];
		const EntityRecord& argument_owner = program_->entities[argument_entity];
		if (pattern_owner.template_argument_begin == kNoBinding ||
			argument_owner.template_argument_begin == kNoBinding) return false;
		const std::vector<TemplateArgument> pattern_arguments =
			StoredTemplateArguments(pattern_owner.template_argument_begin,
				pattern_owner.template_argument_count);
		const std::vector<TemplateArgument> argument_arguments =
			StoredTemplateArguments(argument_owner.template_argument_begin,
				argument_owner.template_argument_count);
		const std::vector<TemplateParameter>& class_parameters =
			pattern_template.parameters;
		std::size_t pattern_index = 0, argument_index = 0;
		while (pattern_index < pattern_arguments.size())
		{
			const TemplateArgument& pattern_argument =
				pattern_arguments[pattern_index];
			std::size_t dependent = parameters.size();
			if (pattern_argument.kind == TEMPLATE_ARGUMENT_TYPE)
			{
				for (std::size_t i = 0;
					i < function_template_shape_parameters_.size() &&
					i < parameters.size(); ++i)
					if (pattern_argument.type ==
						function_template_shape_parameters_[i])
					{
						dependent = i;
						break;
					}
			}
			else if (pattern_argument.IsDependent())
				dependent = pattern_argument.dependent_parameter;
			const bool expansion = pattern_argument.pack_expansion &&
				dependent < parameters.size() &&
				parameters[dependent].pack && !class_parameters.empty() &&
				TemplateParameterForArgument(class_parameters,
					pattern_index).pack;
			if (expansion)
			{
				const std::size_t remaining =
					pattern_arguments.size() - pattern_index - 1;
				if (argument_index + remaining > argument_arguments.size())
					return false;
				const std::size_t last = argument_arguments.size() - remaining;
				const std::size_t prior_size =
					deduced->pack_arguments[dependent].size();
				const bool prior_started =
					deduced->pack_deduction_started[dependent] != 0;
				deduced->pack_deduction_positions[dependent] = 0;
				while (argument_index < last)
					if (!DeduceFunctionTemplatePackArgument(pattern_argument,
						argument_arguments[argument_index++], parameters,
						deduced)) return false;
				if ((prior_started &&
					 deduced->pack_arguments[dependent].size() != prior_size) ||
					deduced->pack_deduction_positions[dependent] !=
						deduced->pack_arguments[dependent].size()) return false;
				deduced->pack_deduction_started[dependent] = 1;
				++pattern_index;
				continue;
			}
			if (argument_index >= argument_arguments.size() ||
				!DeduceFunctionTemplatePackArgument(pattern_argument,
					argument_arguments[argument_index], parameters, deduced))
				return false;
			++pattern_index;
			++argument_index;
		}
		return argument_index == argument_arguments.size();
	}
	case TYPE_FUNDAMENTAL:
	case TYPE_INVALID:
	case TYPE_QUALIFIED:
		return pattern == argument;
	}
	return false;
}

std::size_t SemanticAnalyzer::RequiredFunctionParameterCount(
	const std::vector<ParameterInfo>& parameters) const
{
	std::size_t required = parameters.size();
	while (required != 0 &&
		parameters[required - 1].default_argument != kNoNode) --required;
	return required;
}

void SemanticAnalyzer::DeduceFunctionTemplatePatterns(
	const std::vector<std::size_t>& patterns,
	const std::vector<ExpressionInfo>& arguments,
	std::vector<BindingId>* specializations,
	const std::vector<TypeId>* explicit_arguments,
	const std::vector<TemplateArgument>* canonical_explicit_arguments)
{
	for (std::size_t p = 0; p < patterns.size(); ++p)
	{
		if (patterns[p] >= function_templates_.size())
			throw std::logic_error("invalid function template candidate");
		const FunctionTemplatePattern& pattern =
			function_templates_[patterns[p]];
		const TypeRecord& function_type =
			program_->types.Get(pattern.shape_type);
		const std::size_t fixed_function_parameters =
			pattern.function_parameter_pack ?
			function_type.parameter_count - 1 : function_type.parameter_count;
		if (function_type.kind != TYPE_FUNCTION ||
			arguments.size() < pattern.required_parameter_count ||
			(!pattern.function_parameter_pack &&
			 arguments.size() > function_type.parameter_count)) continue;
		const TypeId* parameters =
			program_->types.Parameters(pattern.shape_type);
		FunctionTemplateDeduction deduced(pattern.parameters);
		bool valid = true;
		if (explicit_arguments && canonical_explicit_arguments)
			throw std::logic_error(
				"function template deduction has two explicit argument forms");
		if (explicit_arguments || canonical_explicit_arguments)
		{
			std::size_t explicit_index = 0;
			const std::size_t explicit_count = canonical_explicit_arguments ?
				canonical_explicit_arguments->size() : explicit_arguments->size();
			for (std::size_t parameter_index = 0;
				parameter_index < pattern.parameters.size() &&
				explicit_index < explicit_count; ++parameter_index)
			{
				const TemplateParameter& template_parameter =
					pattern.parameters[parameter_index];
				if (!canonical_explicit_arguments &&
					template_parameter.kind != TEMPLATE_ARGUMENT_TYPE)
				{
					valid = false;
					break;
				}
				if (template_parameter.pack)
				{
					// Explicit function-template arguments bind to a pack even
					// when later template parameters are inferred from the call.
					// There is no syntax for skipping the pack to name a later
					// parameter, so the pack owns the remaining explicit list.
					while (explicit_index < explicit_count)
					{
						const TemplateArgument explicit_argument =
							canonical_explicit_arguments ?
								(*canonical_explicit_arguments)[explicit_index++] :
								TemplateArgument(TEMPLATE_ARGUMENT_TYPE,
									(*explicit_arguments)[explicit_index++]);
						if (explicit_argument.kind != template_parameter.kind)
						{
							valid = false;
							break;
						}
						deduced.pack_arguments[parameter_index].push_back(
							explicit_argument);
					}
					break;
				}
				const TemplateArgument explicit_argument =
					canonical_explicit_arguments ?
						(*canonical_explicit_arguments)[explicit_index++] :
						TemplateArgument(TEMPLATE_ARGUMENT_TYPE,
							(*explicit_arguments)[explicit_index++]);
				if (explicit_argument.kind != template_parameter.kind)
				{
					valid = false;
					break;
				}
				deduced.fixed_arguments[parameter_index] = explicit_argument;
			}
			if (explicit_index != explicit_count) valid = false;
		}
		for (std::size_t a = 0; a < arguments.size() && valid; ++a)
		{
			if (arguments[a].type == kNoType) continue;
			const bool pack_element = pattern.function_parameter_pack &&
				a >= fixed_function_parameters;
			TypeId parameter = parameters[pack_element ?
				fixed_function_parameters : a];
			TypeId argument = EffectiveType(arguments[a].type);
			const TypeRecord& parameter_record =
				program_->types.Get(parameter);
			if (parameter_record.kind == TYPE_LVALUE_REFERENCE ||
				parameter_record.kind == TYPE_RVALUE_REFERENCE)
			{
				if (parameter_record.kind == TYPE_RVALUE_REFERENCE &&
					arguments[a].category == VALUE_LVALUE)
				{
					bool forwarding_reference = false;
					for (std::size_t t = 0;
						t < pattern.parameters.size(); ++t)
						if (parameter_record.child ==
							function_template_shape_parameters_[t])
							forwarding_reference = true;
					if (forwarding_reference)
						argument = program_->types.Reference(
							TYPE_LVALUE_REFERENCE, argument);
				}
				parameter = parameter_record.child;
			}
			else
			{
				parameter = program_->types.RemoveTopCv(parameter);
				argument = program_->types.RemoveTopCv(Decay(argument));
			}
			valid = DeduceFunctionTemplatePackType(
				parameter, argument, pattern.parameters, &deduced);
		}
		if (valid)
		{
			std::vector<TemplateArgument> canonical;
			std::vector<std::uint32_t> offsets;
			offsets.reserve(pattern.parameters.size() + 1);
			for (std::size_t parameter = 0;
				parameter < pattern.parameters.size(); ++parameter)
			{
				if (canonical.size() >
					std::numeric_limits<std::uint32_t>::max())
				{
					valid = false;
					break;
				}
				offsets.push_back(
					static_cast<std::uint32_t>(canonical.size()));
				if (pattern.parameters[parameter].pack)
					canonical.insert(canonical.end(),
						deduced.pack_arguments[parameter].begin(),
						deduced.pack_arguments[parameter].end());
				else
				{
					TemplateArgument argument =
						deduced.fixed_arguments[parameter];
					argument.kind = pattern.parameters[parameter].kind;
					if (argument.type == kNoType &&
						pattern.parameters[parameter].default_argument == kNoNode)
					{
						valid = false;
						break;
					}
					canonical.push_back(argument);
				}
			}
			if (!valid || canonical.size() >
				std::numeric_limits<std::uint32_t>::max()) continue;
			offsets.push_back(static_cast<std::uint32_t>(canonical.size()));
			const BindingId specialization =
				InstantiateFunctionTemplate(patterns[p], canonical, offsets);
			if (specializations && specialization != kNoBinding)
				specializations->push_back(specialization);
		}
	}
}

void SemanticAnalyzer::DeduceFunctionTemplates(ScopeId scope,
	const std::string& spelling,
	const std::vector<ExpressionInfo>& arguments, NodeId syntax)
{
	NamePath structured_base;
	std::vector<NodeId> explicit_syntax;
	const bool structured_explicit = CollectExplicitTemplateArguments(
		syntax, &structured_base, &explicit_syntax);
	const bool explicit_id = structured_explicit;
	if (!structured_explicit) structured_base = StructuredNamePath(syntax);
	const bool structured_name = !structured_base.Empty();
	const NamePath path = structured_name ? structured_base :
		ParseNamePath(spelling);
	const NameId name = path.Last();
	if (name == 0) return;
	const std::vector<ScopeId> visible_owners =
		structured_name ? FindFunctionTemplateOwners(scope, structured_base) :
		FindFunctionTemplateOwners(scope, spelling);
	for (std::size_t owner = 0; owner < visible_owners.size(); ++owner)
	{
		const ScopeId visible_owner = visible_owners[owner];
		const std::uint64_t visible_key =
			(static_cast<std::uint64_t>(visible_owner) << 32) | name;
		const CompactIndexSequence* found =
			template_function_sets_.Find(visible_key);
		if (!found) continue;
		const std::vector<std::size_t> patterns = found->Copy();
		std::vector<BindingId> specializations;
		if (explicit_id)
			DeduceFunctionTemplatePatternsWithExplicitSyntax(patterns,
				arguments, explicit_syntax, scope, &specializations);
		else DeduceFunctionTemplatePatterns(
			patterns, arguments, &specializations);
		for (std::size_t i = 0; i < specializations.size(); ++i)
		{
			const BindingId source = specializations[i];
			const BindingRecord& source_record = program_->bindings[source];
			if (source_record.owner == visible_owner) continue;
			const FunctionInfo& function = GetFunction(source);
			const FunctionSignatureKey signature_key(visible_owner, name,
				function.signature);
			++function_signature_lookups_;
			if (function_declarations_.Find(signature_key) != kNoBinding)
				continue;
			++function_signature_lookups_;
			if (using_function_declarations_.Find(signature_key) != kNoBinding)
				continue;
			const BindingId alias = program_->AddBinding(visible_owner,
				BIND_FUNCTION, name, function.type, false, 0, NAMED_NONE, 0,
				source);
			CompactIndexSequence& aliases = function_sets_.Ensure(visible_key);
			CompactIndexSequence& ordinary_aliases =
				ordinary_function_sets_.Ensure(visible_key);
			aliases.Push(alias);
			ordinary_aliases.Push(alias);
			IndexEnumOperatorCandidate(alias);
			using_function_declarations_.Insert(signature_key, alias);
		}
	}
}

void SemanticAnalyzer::DeduceFunctionTemplatePatternsWithExplicitSyntax(
	const std::vector<std::size_t>& patterns,
	const std::vector<ExpressionInfo>& arguments,
	const std::vector<NodeId>& explicit_syntax, ScopeId use_scope,
	std::vector<BindingId>* specializations)
{
	for (std::size_t i = 0; i < patterns.size(); ++i)
	{
		if (patterns[i] >= function_templates_.size())
			throw std::logic_error("invalid function template candidate");
		const FunctionTemplatePattern& pattern =
			function_templates_[patterns[i]];
		std::vector<TemplateParameter> explicit_parameters = pattern.parameters;
		for (std::size_t parameter = 0;
			parameter < explicit_parameters.size(); ++parameter)
			if (explicit_parameters[parameter].pack)
			{
				explicit_parameters.resize(parameter + 1);
				break;
			}
		std::vector<TemplateArgument> canonical;
		if (!BuildTemplateArguments(explicit_parameters, explicit_syntax,
			use_scope, pattern.lexical_scope, &canonical, false)) continue;
		const std::vector<std::size_t> one_pattern(1, patterns[i]);
		DeduceFunctionTemplatePatterns(one_pattern, arguments,
			specializations, 0, &canonical);
	}
}

std::vector<BindingId> SemanticAnalyzer::FunctionTemplateTargetCandidates(
	ScopeId scope, const std::string& spelling, TypeId target, NodeId syntax)
{
	NamePath structured_base;
	std::vector<TypeId> explicit_arguments;
	const bool structured_explicit = ParseExplicitTemplateArguments(
		syntax, scope, &structured_base, &explicit_arguments);
	const bool explicit_id = structured_explicit;
	if (!structured_explicit) structured_base = StructuredNamePath(syntax);
	std::vector<std::size_t> patterns = !structured_base.Empty() ?
		FindStructuredFunctionTemplates(syntax, scope) :
		FindFunctionTemplates(scope, spelling);
	if (patterns.empty() && !structured_base.Empty())
		patterns = FindFunctionTemplates(scope, structured_base);
	std::vector<BindingId> result;
	for (std::size_t i = 0; i < patterns.size(); ++i)
	{
		if (patterns[i] >= function_templates_.size())
			throw std::logic_error("invalid target function template candidate");
		const FunctionTemplatePattern& pattern = function_templates_[patterns[i]];
		if (explicit_id &&
			explicit_arguments.size() > pattern.parameters.size())
			continue;
		std::vector<TypeId> deduced(pattern.parameters.size(), kNoType);
		if (explicit_id)
			std::copy(explicit_arguments.begin(), explicit_arguments.end(),
				deduced.begin());
		if (!DeduceFunctionTemplateType(pattern.shape_type, target, &deduced))
			continue;
		const BindingId candidate =
			InstantiateFunctionTemplate(patterns[i], deduced);
		if (candidate != kNoBinding &&
			std::find(result.begin(), result.end(), candidate) == result.end())
			result.push_back(candidate);
	}
	return result;
}

bool SemanticAnalyzer::HasUniqueFunctionAddressTarget(
	ScopeId scope, NodeId syntax, TypeId target)
{
	while (syntax != kNoNode &&
		arena_->IsTag(syntax, "parenthesized-expression"))
		syntax = FirstSemanticChild(syntax);
	if (syntax == kNoNode || !arena_->IsTag(syntax, "unary-expression") ||
		PayloadSource(syntax) != "&") return false;
	syntax = FirstSemanticChild(syntax);
	while (syntax != kNoNode &&
		arena_->IsTag(syntax, "parenthesized-expression"))
		syntax = FirstSemanticChild(syntax);
	if (syntax == kNoNode || !arena_->IsTag(syntax, "id-expression"))
		return false;

	TypeId desired = program_->types.RemoveTopCv(target);
	TypeRecord shape = program_->types.Get(desired);
	if (shape.kind == TYPE_LVALUE_REFERENCE ||
		shape.kind == TYPE_RVALUE_REFERENCE)
	{
		desired = program_->types.RemoveTopCv(shape.child);
		shape = program_->types.Get(desired);
	}
	if (shape.kind != TYPE_POINTER ||
		!program_->types.IsFunction(shape.child)) return false;
	desired = shape.child;

	const std::string spelling = arena_->Payload(syntax);
	std::vector<BindingId> candidates =
		FunctionCandidates(scope, spelling, 0, syntax);
	const std::vector<BindingId> templates =
		FunctionTemplateTargetCandidates(scope, spelling, desired, syntax);
	for (std::size_t i = 0; i < templates.size(); ++i)
		if (std::find(candidates.begin(), candidates.end(), templates[i]) ==
			candidates.end())
			candidates.push_back(templates[i]);
	BindingId selected = kNoBinding;
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		if (GetFunction(candidates[i]).type != desired) continue;
		const BindingId canonical = program_->bindings[candidates[i]].canonical;
		if (selected != kNoBinding && selected != canonical) return false;
		selected = canonical;
	}
	return selected != kNoBinding;
}

bool SemanticAnalyzer::AnalyzeFunctionId(NodeId node, ScopeId scope,
	TypeId target, ExpressionInfo* result)
{
	const std::string spelling = arena_->Payload(node);
	EntityId naming_class = kNoEntity;
	std::vector<BindingId> candidates =
		FunctionCandidates(scope, spelling, &naming_class, node);
	NamePath structured_base;
	std::vector<TypeId> explicit_arguments;
	const bool explicit_template_id = ParseExplicitTemplateArguments(
		node, scope, &structured_base, &explicit_arguments);
	if (!explicit_template_id) structured_base = StructuredNamePath(node);
	std::vector<std::size_t> template_patterns = !structured_base.Empty() ?
		FindStructuredFunctionTemplates(node, scope) :
		FindFunctionTemplates(scope, spelling);
	if (template_patterns.empty() && !structured_base.Empty())
		template_patterns = FindFunctionTemplates(scope, structured_base);
	TypeId desired = target;
	if (desired != kNoType)
	{
		desired = program_->types.RemoveTopCv(desired);
		const TypeRecord target_record = program_->types.Get(desired);
		if (target_record.kind == TYPE_LVALUE_REFERENCE ||
			target_record.kind == TYPE_RVALUE_REFERENCE)
			desired = target_record.child;
		if (program_->types.Get(desired).kind == TYPE_POINTER)
			desired = program_->types.Get(desired).child;
		else if (program_->types.Get(desired).kind == TYPE_MEMBER_POINTER)
			desired = program_->types.Get(desired).child;
		const std::vector<BindingId> target_templates =
			FunctionTemplateTargetCandidates(scope, spelling, desired, node);
		for (std::size_t i = 0; i < target_templates.size(); ++i)
			if (std::find(candidates.begin(), candidates.end(),
				target_templates[i]) == candidates.end())
				candidates.push_back(target_templates[i]);
	}
	if (desired == kNoType && !template_patterns.empty() &&
		!explicit_template_id)
	{
		result->binding = candidates.empty() ? kNoBinding : candidates[0];
		return true;
	}
	if (candidates.empty()) return false;
	BindingId selected = kNoBinding;
	for (std::size_t i = 0; i < candidates.size(); ++i)
		if (desired == kNoType || GetFunction(candidates[i]).type == desired)
		{
			if (selected != kNoBinding && desired != kNoType)
			{
				const FunctionInfo& prior = GetFunction(selected);
				const FunctionInfo& candidate = GetFunction(candidates[i]);
				if (prior.template_specialization !=
					candidate.template_specialization)
				{
					if (prior.template_specialization)
						selected = candidates[i];
					continue;
				}
				const int preference =
					CompareFunctionTemplateConstraints(candidate, prior);
				if (preference > 0) { selected = candidates[i]; continue; }
				if (preference < 0) continue;
				throw std::runtime_error("ambiguous overloaded function id");
			}
			selected = candidates[i];
			if (desired == kNoType && candidates.size() != 1)
			{
				result->binding = candidates[0];
				return true;
			}
		}
	if (selected == kNoBinding)
		throw std::runtime_error("no target-matching overloaded function");
	if (!CanAccessMember(selected, naming_class))
		throw std::runtime_error("inaccessible member function");
	const FunctionInfo& function = GetFunction(selected);
	const BindingId emission_binding =
		program_->bindings[selected].canonical;
	result->type = function.type;
	if (function.member_owner != kNoType)
	{
		const TypeRecord member_type = program_->types.Get(function.type);
		TypeId object = function.member_owner;
		if ((member_type.cv & CV_CONST) != 0)
			object = program_->types.Qualify(object, CV_CONST);
		if ((member_type.cv & CV_VOLATILE) != 0)
			object = program_->types.Qualify(object, CV_VOLATILE);
		std::vector<TypeId> parameters;
		parameters.push_back(program_->types.Pointer(object));
		const TypeId* explicit_parameters =
			program_->types.Parameters(function.type);
		for (std::size_t i = 0; i < member_type.parameter_count; ++i)
			parameters.push_back(explicit_parameters[i]);
		result->type = program_->types.Function(member_type.child,
			parameters, member_type.variadic);
	}
	result->category = VALUE_LVALUE;
	result->binding = emission_binding;
	result->node = MakeDump(DUMP_ID_EXPRESSION, result->type,
		result->category, program_->names.Intern(spelling), emission_binding);
	if (constant_expression_required_depth_ == 0 &&
		constexpr_evaluation_depth_ == 0)
		DemandFunction(selected);
	++expression_count_;
	return true;
}

}
}
