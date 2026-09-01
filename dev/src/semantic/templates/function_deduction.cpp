#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace cppgm
{
namespace semantic
{

namespace
{

TypeId RemoveFunctionParameterCv(TypeTable* types, TypeId type,
	std::uint8_t cv)
{
	const TypeRecord record = types->Get(type);
	if (record.kind == TYPE_ARRAY)
	{
		const TypeId child = RemoveFunctionParameterCv(types, record.child, cv);
		if (child == record.child) return type;
		return record.dependent_bound_parameter == kNoTemplateParameter ?
			types->Array(child, record.bound) :
			types->DependentArray(child, record.dependent_bound_type,
				record.dependent_bound_parameter);
	}
	if (record.kind != TYPE_QUALIFIED) return type;
	const std::uint8_t remaining = static_cast<std::uint8_t>(record.cv & ~cv);
	return remaining == CV_NONE ? record.child :
		types->Qualify(record.child, remaining);
}

}

bool Analyzer::FunctionTemplateTypeIsDependent(TypeId type) const
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
	if (type == function_template_nondeduced_type_shape_ &&
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
	case TYPE_BLOCK_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
	case TYPE_COMPLEX:
		dependent = FunctionTemplateTypeIsDependent(record.child);
		break;
	case TYPE_VECTOR:
		dependent = record.dependent_bound_parameter != kNoTemplateParameter ||
			FunctionTemplateTypeIsDependent(record.child);
		break;
	case TYPE_ARRAY:
		dependent = record.dependent_bound_parameter != kNoTemplateParameter ||
			FunctionTemplateTypeIsDependent(record.child);
		break;
	case TYPE_BITINT:
		dependent = record.dependent_bound_parameter != kNoTemplateParameter;
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
		dependent = FunctionTemplateTypeIsDependent(
			static_cast<TypeId>(record.bound)) ||
			FunctionTemplateTypeIsDependent(record.child);
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
		if (class_templates_[template_index].template_parameter_proxy)
		{
			dependent = true;
			break;
		}
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
			ThrowInternalCompilerError(
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

bool Analyzer::FunctionTemplateTypeUsesUnspecifiedParameter(
	TypeId type, const std::vector<TemplateParameter>& parameters,
	const std::vector<std::uint8_t>& explicitly_specified) const
{
	for (std::size_t i = 0; i < parameters.size() &&
		i < function_template_shape_parameters_.size(); ++i)
		if (type == function_template_shape_parameters_[i])
			return parameters[i].pack || i >= explicitly_specified.size() ||
				explicitly_specified[i] == 0;
	if (type == function_template_dependent_result_shape_ ||
		type == function_template_nondeduced_type_shape_)
		return false;
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind == TYPE_MEMBER_POINTER)
		return FunctionTemplateTypeUsesUnspecifiedParameter(
			static_cast<TypeId>(record.bound), parameters, explicitly_specified) ||
			FunctionTemplateTypeUsesUnspecifiedParameter(
				record.child, parameters, explicitly_specified);
	if (record.kind == TYPE_QUALIFIED || record.kind == TYPE_POINTER ||
		record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE)
		return FunctionTemplateTypeUsesUnspecifiedParameter(
			record.child, parameters, explicitly_specified);
	if (record.kind == TYPE_ARRAY || record.kind == TYPE_VECTOR)
	{
		const std::size_t bound = record.dependent_bound_parameter;
		if (bound != kNoTemplateParameter && bound < parameters.size() &&
			(parameters[bound].pack || bound >= explicitly_specified.size() ||
			 explicitly_specified[bound] == 0)) return true;
		return FunctionTemplateTypeUsesUnspecifiedParameter(
			record.child, parameters, explicitly_specified);
	}
	if (record.kind == TYPE_FUNCTION)
	{
		if (FunctionTemplateTypeUsesUnspecifiedParameter(
			record.child, parameters, explicitly_specified)) return true;
		const TypeId* function_parameters = program_->types.Parameters(type);
		for (std::size_t i = 0; i < record.parameter_count; ++i)
			if (FunctionTemplateTypeUsesUnspecifiedParameter(
				function_parameters[i], parameters, explicitly_specified))
				return true;
		return false;
	}
	if (record.kind != TYPE_NAMED) return false;
	const EntityRecord& entity = program_->entities[record.entity];
	if (entity.template_argument_begin == kNoBinding) return false;
	const std::vector<TemplateArgument> arguments = StoredTemplateArguments(
		entity.template_argument_begin, entity.template_argument_count);
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		if (arguments[i].IsDependent())
		{
			const std::size_t parameter = arguments[i].dependent_parameter;
			if (parameter < parameters.size() &&
				(parameters[parameter].pack ||
				 parameter >= explicitly_specified.size() ||
				 explicitly_specified[parameter] == 0)) return true;
		}
		if (arguments[i].kind == TEMPLATE_ARGUMENT_TYPE &&
			FunctionTemplateTypeUsesUnspecifiedParameter(
				arguments[i].type, parameters, explicitly_specified)) return true;
	}
	return false;
}

std::size_t Analyzer::FunctionTemplateShapePackParameter(TypeId type,
	const std::vector<TemplateParameter>& parameters) const
{
	for (std::size_t i = 0; i < parameters.size() &&
		i < function_template_shape_parameters_.size(); ++i)
		if (parameters[i].pack &&
			type == function_template_shape_parameters_[i]) return i;
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind == TYPE_MEMBER_POINTER)
	{
		const std::size_t owner = FunctionTemplateShapePackParameter(
			static_cast<TypeId>(record.bound), parameters);
		return owner != parameters.size() ? owner :
			FunctionTemplateShapePackParameter(record.child, parameters);
	}
	if (record.kind == TYPE_QUALIFIED || record.kind == TYPE_POINTER ||
		record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE || record.kind == TYPE_ARRAY)
		return FunctionTemplateShapePackParameter(record.child, parameters);
	if (record.kind == TYPE_FUNCTION)
	{
		const TypeId* function_parameters = program_->types.Parameters(type);
		for (std::size_t i = 0; i < record.parameter_count; ++i)
		{
			const std::size_t found = FunctionTemplateShapePackParameter(
				function_parameters[i], parameters);
			if (found != parameters.size()) return found;
		}
	}
	if (record.kind == TYPE_NAMED)
	{
		const EntityRecord& entity = program_->entities[record.entity];
		if (entity.template_argument_begin != kNoBinding)
		{
			const std::vector<TemplateArgument> arguments =
				StoredTemplateArguments(entity.template_argument_begin,
					entity.template_argument_count);
			for (std::size_t i = 0; i < arguments.size(); ++i)
			{
				if (arguments[i].IsDependent() &&
					arguments[i].dependent_parameter < parameters.size() &&
					parameters[arguments[i].dependent_parameter].pack)
					return arguments[i].dependent_parameter;
				if (arguments[i].kind == TEMPLATE_ARGUMENT_TYPE)
				{
					const std::size_t found =
						FunctionTemplateShapePackParameter(
							arguments[i].type, parameters);
					if (found != parameters.size()) return found;
				}
			}
		}
	}
	return parameters.size();
}

bool Analyzer::FunctionTemplateArgumentPatternAccepts(
	const TemplateArgument& pattern, const TemplateArgument& exemplar,
	const std::vector<TemplateParameter>& pattern_parameters,
	const std::vector<TemplateParameter>& exemplar_parameters) const
{
	if (pattern.kind != exemplar.kind) return false;
	if (pattern.IsNondeduced()) return true;
	if (pattern.kind == TEMPLATE_ARGUMENT_TYPE)
		return FunctionTemplatePatternAccepts(pattern.type, exemplar.type,
			pattern_parameters, exemplar_parameters);
	if (pattern.IsDependent() &&
		pattern.dependent_parameter < pattern_parameters.size()) return true;
	return pattern == exemplar;
}

bool Analyzer::FunctionTemplateParameterListAccepts(
	const FunctionTemplatePattern& pattern,
	const FunctionTemplatePattern& exemplar) const
{
	const TypeRecord& pattern_type = program_->types.Get(pattern.shape_type);
	const TypeRecord& exemplar_type = program_->types.Get(exemplar.shape_type);
	if (pattern_type.kind != TYPE_FUNCTION ||
		exemplar_type.kind != TYPE_FUNCTION) return false;
	const TypeId* pattern_types = program_->types.Parameters(pattern.shape_type);
	const TypeId* exemplar_types = program_->types.Parameters(exemplar.shape_type);
	const std::size_t pattern_fixed = pattern_type.parameter_count -
		(pattern.function_parameter_pack ? 1 : 0);
	const std::size_t exemplar_fixed = exemplar_type.parameter_count -
		(exemplar.function_parameter_pack ? 1 : 0);
	const auto parameter_accepts = [this, &pattern, &exemplar](
		std::size_t pattern_index, TypeId pattern_parameter,
		TypeId exemplar_parameter) -> bool
	{
		const TypeKind pattern_kind =
			program_->types.Get(pattern_parameter).kind;
		const TypeKind exemplar_kind =
			program_->types.Get(exemplar_parameter).kind;
		const bool pattern_reference =
			pattern_kind == TYPE_LVALUE_REFERENCE ||
			pattern_kind == TYPE_RVALUE_REFERENCE;
		const bool exemplar_reference =
			exemplar_kind == TYPE_LVALUE_REFERENCE ||
			exemplar_kind == TYPE_RVALUE_REFERENCE;
		if (pattern_reference)
			pattern_parameter = program_->types.Get(pattern_parameter).child;
		if (exemplar_reference)
			exemplar_parameter = program_->types.Get(exemplar_parameter).child;
		if (!pattern_reference || !exemplar_reference)
		{
			pattern_parameter = program_->types.RemoveTopCv(pattern_parameter);
			exemplar_parameter = program_->types.RemoveTopCv(exemplar_parameter);
		}
		if (pattern_index < pattern.function_parameter_nondeduced.size() &&
			pattern.function_parameter_nondeduced[pattern_index] != 0) return true;
		return FunctionTemplatePatternAccepts(pattern_parameter,
			exemplar_parameter, pattern.parameters, exemplar.parameters);
	};
	const std::size_t common = std::min(pattern_fixed, exemplar_fixed);
	for (std::size_t i = 0; i < common; ++i)
		if (!parameter_accepts(i, pattern_types[i], exemplar_types[i]))
			return false;
	if (pattern_fixed > exemplar_fixed &&
		pattern.required_parameter_count > exemplar_fixed) return false;
	if (pattern_fixed < exemplar_fixed)
	{
		if (!pattern.function_parameter_pack)
			return !exemplar.function_parameter_pack &&
				exemplar.required_parameter_count <= pattern_fixed;
		const std::size_t pack_index = pattern_fixed;
		for (std::size_t i = pattern_fixed; i < exemplar_fixed; ++i)
			if (!parameter_accepts(
				pack_index, pattern_types[pack_index], exemplar_types[i]))
				return false;
	}
	if (exemplar.function_parameter_pack)
	{
		if (!pattern.function_parameter_pack)
		{
			// The compared specializations are already viable for the same
			// use.  Exemplar pack elements fit the pattern's fixed suffix and
			// any unmatched suffix is entirely defaulted; requiring another pack
			// here loses the structured-prefix ordering relation.
			return pattern.required_parameter_count <= exemplar_fixed;
		}
		const std::size_t pack_index = pattern_fixed;
		if (!parameter_accepts(pack_index, pattern_types[pack_index],
				exemplar_types[exemplar_fixed])) return false;
	}
	return true;
}

int Analyzer::CompareFunctionTemplateConstraints(
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
	const TypeRecord& left_shape = program_->types.Get(left_pattern.shape_type);
	const TypeRecord& right_shape = program_->types.Get(right_pattern.shape_type);
	const bool left_initializer_list = left_shape.kind == TYPE_FUNCTION &&
		left_shape.parameter_count != 0 && IsInitializerListType(
			program_->types.Parameters(left_pattern.shape_type)[0]);
	const bool right_initializer_list = right_shape.kind == TYPE_FUNCTION &&
		right_shape.parameter_count != 0 && IsInitializerListType(
			program_->types.Parameters(right_pattern.shape_type)[0]);
	if (left_initializer_list != right_initializer_list)
		return left_initializer_list ? 1 : -1;
	const bool left_accepts_right = FunctionTemplateParameterListAccepts(
		left_pattern, right_pattern);
	const bool right_accepts_left = FunctionTemplateParameterListAccepts(
		right_pattern, left_pattern);
	if (right_accepts_left != left_accepts_right)
		return right_accepts_left ? 1 : -1;
	const TypeRecord& left_actual = program_->types.Get(left.type);
	const TypeRecord& right_actual = program_->types.Get(right.type);
	if (left_shape.kind == TYPE_FUNCTION && right_shape.kind == TYPE_FUNCTION &&
		left_actual.kind == TYPE_FUNCTION && right_actual.kind == TYPE_FUNCTION &&
		left_shape.parameter_count == right_shape.parameter_count &&
		left_actual.parameter_count == right_actual.parameter_count)
	{
		const TypeId* left_types = program_->types.Parameters(left_pattern.shape_type);
		const TypeId* right_types = program_->types.Parameters(right_pattern.shape_type);
		const TypeId* left_actual_types = program_->types.Parameters(left.type);
		const TypeId* right_actual_types = program_->types.Parameters(right.type);
		const auto direct_template_reference = [this](TypeId reference,
			TypeKind reference_kind,
			const FunctionTemplatePattern& pattern) -> bool
		{
			const TypeRecord& record = program_->types.Get(reference);
			if (record.kind != reference_kind) return false;
			const TypeId referred = program_->types.RemoveTopCv(record.child);
			if (referred == function_template_nondeduced_type_shape_ &&
				referred != kNoType) return true;
			for (std::size_t parameter = 0;
				parameter < pattern.parameters.size() && parameter <
					function_template_shape_parameters_.size(); ++parameter)
				if (referred == function_template_shape_parameters_[parameter])
					return pattern.parameters[parameter].kind ==
						TEMPLATE_ARGUMENT_TYPE;
			return false;
		};
		for (std::size_t i = 0; i < left_shape.parameter_count; ++i)
		{
			if (left_actual_types[i] != right_actual_types[i]) continue;
			if (direct_template_reference(left_types[i], TYPE_LVALUE_REFERENCE,
				left_pattern) && direct_template_reference(right_types[i],
				TYPE_RVALUE_REFERENCE, right_pattern))
				return 1;
			if (direct_template_reference(right_types[i], TYPE_LVALUE_REFERENCE,
				right_pattern) && direct_template_reference(left_types[i],
				TYPE_RVALUE_REFERENCE, left_pattern))
				return -1;
		}
	}
	if (!left_accepts_right) return 0;
	const std::size_t left_parameters = left_pattern.parameters.size();
	const std::size_t right_parameters = right_pattern.parameters.size();
	if (left_parameters == right_parameters) return 0;
	// Mutually accepting parameter patterns expose equality constraints: fewer
	// independent template parameters means the pattern accepted a strict
	// subset of the argument combinations accepted by the other candidate.
	return left_parameters < right_parameters ? 1 : -1;
}

bool Analyzer::FunctionTemplatePatternAccepts(
	TypeId pattern, TypeId exemplar,
	const std::vector<TemplateParameter>& pattern_parameters,
	const std::vector<TemplateParameter>& exemplar_parameters) const
{
	if (pattern == class_template_nondeduced_type_shape_ &&
		pattern != kNoType) return true;
	for (std::size_t i = 0; i < pattern_parameters.size() &&
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
			FunctionTemplatePatternAccepts(pattern_record.child,
				exemplar_record.child, pattern_parameters, exemplar_parameters);
	case TYPE_POINTER:
	case TYPE_BLOCK_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
	case TYPE_COMPLEX:
		return FunctionTemplatePatternAccepts(pattern_record.child,
			exemplar_record.child, pattern_parameters, exemplar_parameters);
	case TYPE_ARRAY:
		return pattern_record.bound == exemplar_record.bound &&
			FunctionTemplatePatternAccepts(pattern_record.child,
				exemplar_record.child, pattern_parameters, exemplar_parameters);
	case TYPE_VECTOR:
		return pattern_record.bound == exemplar_record.bound &&
			FunctionTemplatePatternAccepts(pattern_record.child,
				exemplar_record.child, pattern_parameters, exemplar_parameters);
	case TYPE_BITINT:
		return pattern_record.bitint_unsigned ==
				exemplar_record.bitint_unsigned &&
			(pattern_record.dependent_bound_parameter != kNoTemplateParameter ||
			 pattern_record.bound == exemplar_record.bound);
	case TYPE_FUNCTION:
	{
		if (pattern_record.cv != exemplar_record.cv ||
			pattern_record.ref_qualifier != exemplar_record.ref_qualifier ||
			!FunctionTemplatePatternAccepts(pattern_record.child,
				exemplar_record.child, pattern_parameters,
				exemplar_parameters)) return false;
		const TypeId* pattern_types = program_->types.Parameters(pattern);
		const TypeId* exemplar_types = program_->types.Parameters(exemplar);
		const bool pattern_pack = pattern_record.variadic &&
			pattern_record.parameter_count != 0 &&
			FunctionTemplateShapePackParameter(
				pattern_types[pattern_record.parameter_count - 1],
				pattern_parameters) != pattern_parameters.size();
		const bool exemplar_pack = exemplar_record.variadic &&
			exemplar_record.parameter_count != 0 &&
			FunctionTemplateShapePackParameter(
				exemplar_types[exemplar_record.parameter_count - 1],
				exemplar_parameters) != exemplar_parameters.size();
		if (!pattern_pack && pattern_record.variadic != exemplar_record.variadic)
			return false;
		const std::size_t pattern_fixed = pattern_record.parameter_count -
			(pattern_pack ? 1 : 0);
		const std::size_t exemplar_fixed = exemplar_record.parameter_count -
			(exemplar_pack ? 1 : 0);
		const std::size_t common = std::min(pattern_fixed, exemplar_fixed);
		for (std::size_t i = 0; i < common; ++i)
			if (!FunctionTemplatePatternAccepts(pattern_types[i],
				exemplar_types[i], pattern_parameters,
				exemplar_parameters)) return false;
		if (pattern_fixed > exemplar_fixed) return false;
		if (pattern_fixed < exemplar_fixed)
		{
			if (!pattern_pack) return false;
			for (std::size_t i = pattern_fixed; i < exemplar_fixed; ++i)
				if (!FunctionTemplatePatternAccepts(
					pattern_types[pattern_fixed], exemplar_types[i],
					pattern_parameters, exemplar_parameters)) return false;
		}
		if (exemplar_pack)
			return pattern_pack && FunctionTemplatePatternAccepts(
				pattern_types[pattern_fixed], exemplar_types[exemplar_fixed],
				pattern_parameters, exemplar_parameters);
		return true;
	}
	case TYPE_MEMBER_POINTER:
		return FunctionTemplatePatternAccepts(
				static_cast<TypeId>(pattern_record.bound),
				static_cast<TypeId>(exemplar_record.bound), pattern_parameters,
				exemplar_parameters) &&
			FunctionTemplatePatternAccepts(pattern_record.child,
				exemplar_record.child, pattern_parameters, exemplar_parameters);
	case TYPE_NAMED:
	{
		const EntityId pattern_entity = pattern_record.entity;
		const EntityId exemplar_entity = exemplar_record.entity;
		if (pattern_entity >= class_template_pattern_by_entity_.size() ||
			exemplar_entity >= class_template_pattern_by_entity_.size()) return false;
		const std::uint32_t pattern_template =
			class_template_pattern_by_entity_[pattern_entity];
		const std::uint32_t exemplar_template =
			class_template_pattern_by_entity_[exemplar_entity];
		if (pattern_template == kNoDumpEdge ||
			exemplar_template == kNoDumpEdge ||
			pattern_template >= class_templates_.size() ||
			exemplar_template >= class_templates_.size()) return false;
		const ClassTemplatePattern& pattern_template_record =
			class_templates_[pattern_template];
		const ClassTemplatePattern& exemplar_template_record =
			class_templates_[exemplar_template];
		if (pattern_template_record.template_parameter_proxy)
		{
			if (!TemplateTemplateParameterMatches(
				pattern_template_record.parameters,
				exemplar_template_record.parameters)) return false;
		}
		else if (pattern_template != exemplar_template) return false;
		const EntityRecord& pattern_owner = program_->entities[pattern_entity];
		const EntityRecord& exemplar_owner = program_->entities[exemplar_entity];
		if (pattern_owner.template_argument_begin == kNoBinding ||
			exemplar_owner.template_argument_begin == kNoBinding) return false;
		const std::vector<TemplateArgument> pattern_arguments =
			StoredTemplateArguments(pattern_owner.template_argument_begin,
				pattern_owner.template_argument_count);
		const std::vector<TemplateArgument> exemplar_arguments =
			StoredTemplateArguments(exemplar_owner.template_argument_begin,
				exemplar_owner.template_argument_count);
		std::size_t pattern_index = 0, exemplar_index = 0;
		while (pattern_index < pattern_arguments.size())
		{
			const TemplateArgument& pattern_argument =
				pattern_arguments[pattern_index];
			std::size_t expansion_parameter = pattern_parameters.size();
			if (pattern_argument.kind == TEMPLATE_ARGUMENT_TYPE)
				expansion_parameter = FunctionTemplateShapePackParameter(
					pattern_argument.type, pattern_parameters);
			else if (pattern_argument.IsDependent())
				expansion_parameter = pattern_argument.dependent_parameter;
			if (pattern_argument.pack_expansion &&
				expansion_parameter < pattern_parameters.size() &&
				pattern_parameters[expansion_parameter].pack)
			{
				const std::size_t remaining =
					pattern_arguments.size() - pattern_index - 1;
				if (exemplar_index + remaining > exemplar_arguments.size())
					return false;
				const std::size_t last = exemplar_arguments.size() - remaining;
				while (exemplar_index < last)
					if (!FunctionTemplateArgumentPatternAccepts(pattern_argument,
						exemplar_arguments[exemplar_index++], pattern_parameters,
						exemplar_parameters)) return false;
				++pattern_index;
				continue;
			}
			if (exemplar_index >= exemplar_arguments.size() ||
				!FunctionTemplateArgumentPatternAccepts(pattern_argument,
					exemplar_arguments[exemplar_index], pattern_parameters,
					exemplar_parameters)) return false;
			++pattern_index;
			++exemplar_index;
		}
		return exemplar_index == exemplar_arguments.size();
	}
	case TYPE_FUNDAMENTAL:
	case TYPE_INVALID:
		return false;
	}
	return false;
}

bool Analyzer::DeduceFunctionTemplateType(TypeId pattern,
	TypeId argument, std::vector<TypeId>* deduced) const
{
	++function_template_deduction_visits_;
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
		return DeduceFunctionTemplateType(pattern_record.child,
			RemoveFunctionParameterCv(
				&program_->types, argument, pattern_record.cv), deduced);
	}
	const TypeRecord& argument_record = program_->types.Get(argument);
	if (pattern_record.kind != argument_record.kind) return false;
	switch (pattern_record.kind)
	{
	case TYPE_POINTER:
	case TYPE_BLOCK_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
	case TYPE_COMPLEX:
		return DeduceFunctionTemplateType(pattern_record.child,
			argument_record.child, deduced);
	case TYPE_ARRAY:
		return (pattern_record.bound == 0 ||
			pattern_record.bound == argument_record.bound) &&
			DeduceFunctionTemplateType(pattern_record.child,
				argument_record.child, deduced);
	case TYPE_VECTOR:
		return pattern_record.bound == argument_record.bound &&
			DeduceFunctionTemplateType(pattern_record.child,
				argument_record.child, deduced);
	case TYPE_BITINT:
		return pattern_record.bitint_unsigned ==
				argument_record.bitint_unsigned &&
			(pattern_record.dependent_bound_parameter != kNoTemplateParameter ||
			 pattern_record.bound == argument_record.bound);
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
		return DeduceFunctionTemplateType(
				static_cast<TypeId>(pattern_record.bound),
				static_cast<TypeId>(argument_record.bound), deduced) &&
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
			ThrowInternalCompilerError(
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

bool Analyzer::DeduceFunctionTemplatePackArgument(
	const TemplateArgument& pattern, const TemplateArgument& argument,
	const std::vector<TemplateParameter>& parameters,
	FunctionTemplateDeduction* deduced) const
{
	if (pattern.kind != argument.kind) return false;
	if (pattern.IsNondeduced()) return true;
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
	if (argument.IsDependent()) return false;
	if (pattern.kind == TEMPLATE_ARGUMENT_TYPE)
		return DeduceFunctionTemplatePackType(
			pattern.type, argument.type, parameters, deduced);
	return pattern.type == argument.type && pattern.value == argument.value &&
		pattern.value_binding == argument.value_binding;
}

bool Analyzer::DeduceFunctionTemplatePackType(TypeId pattern,
	TypeId argument, const std::vector<TemplateParameter>& parameters,
	FunctionTemplateDeduction* deduced) const
{
	++function_template_deduction_visits_;
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
		return DeduceFunctionTemplatePackType(pattern_record.child,
			RemoveFunctionParameterCv(
				&program_->types, argument, pattern_record.cv), parameters, deduced);
	}
	const TypeRecord& argument_record = program_->types.Get(argument);
	if (pattern_record.kind != argument_record.kind) return false;
	switch (pattern_record.kind)
	{
	case TYPE_POINTER:
	case TYPE_BLOCK_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
	case TYPE_COMPLEX:
		return DeduceFunctionTemplatePackType(pattern_record.child,
			argument_record.child, parameters, deduced);
	case TYPE_ARRAY:
	{
		if (pattern_record.dependent_bound_parameter != kNoTemplateParameter)
		{
			const std::size_t dependent =
				pattern_record.dependent_bound_parameter;
			if (dependent >= parameters.size() ||
				parameters[dependent].kind != TEMPLATE_ARGUMENT_INTEGRAL ||
				argument_record.bound == 0 ||
				argument_record.dependent_bound_parameter !=
					kNoTemplateParameter)
				return false;
			const TemplateArgument pattern_bound(TEMPLATE_ARGUMENT_INTEGRAL,
				pattern_record.dependent_bound_type, 0,
				static_cast<std::uint32_t>(dependent));
			const TemplateArgument argument_bound(TEMPLATE_ARGUMENT_INTEGRAL,
				pattern_record.dependent_bound_type,
				NormalizeIntegralConstant(pattern_record.dependent_bound_type,
					static_cast<std::int64_t>(argument_record.bound)));
			if (!DeduceFunctionTemplatePackArgument(pattern_bound,
				argument_bound, parameters, deduced)) return false;
		}
		else if (argument_record.dependent_bound_parameter !=
			kNoTemplateParameter ||
			(pattern_record.bound != 0 &&
			 pattern_record.bound != argument_record.bound)) return false;
		return DeduceFunctionTemplatePackType(pattern_record.child,
			argument_record.child, parameters, deduced);
	}
	case TYPE_VECTOR:
	{
		if (pattern_record.dependent_bound_parameter != kNoTemplateParameter)
		{
			const std::size_t dependent =
				pattern_record.dependent_bound_parameter;
			if (dependent >= parameters.size() ||
				parameters[dependent].kind != TEMPLATE_ARGUMENT_INTEGRAL ||
				argument_record.dependent_bound_parameter != kNoTemplateParameter ||
				argument_record.bound == 0) return false;
			const std::size_t lane_bytes =
				program_->SizeOf(argument_record.child);
			if (lane_bytes == 0 || argument_record.bound % lane_bytes != 0)
				return false;
			const TemplateArgument pattern_lanes(TEMPLATE_ARGUMENT_INTEGRAL,
				pattern_record.dependent_bound_type, 0,
				static_cast<std::uint32_t>(dependent));
			const TemplateArgument argument_lanes(TEMPLATE_ARGUMENT_INTEGRAL,
				pattern_record.dependent_bound_type,
				NormalizeIntegralConstant(pattern_record.dependent_bound_type,
					static_cast<std::int64_t>(
						argument_record.bound / lane_bytes)));
			if (!DeduceFunctionTemplatePackArgument(
				pattern_lanes, argument_lanes, parameters, deduced)) return false;
		}
		else if (argument_record.dependent_bound_parameter !=
			kNoTemplateParameter || pattern_record.bound != argument_record.bound)
			return false;
		return DeduceFunctionTemplatePackType(pattern_record.child,
			argument_record.child, parameters, deduced);
	}
	case TYPE_BITINT:
	{
		if (pattern_record.bitint_unsigned != argument_record.bitint_unsigned)
			return false;
		if (pattern_record.dependent_bound_parameter == kNoTemplateParameter)
			return argument_record.dependent_bound_parameter ==
					kNoTemplateParameter &&
				pattern_record.bound == argument_record.bound;
		const std::size_t dependent =
			pattern_record.dependent_bound_parameter;
		if (dependent >= parameters.size() ||
			parameters[dependent].kind != TEMPLATE_ARGUMENT_INTEGRAL ||
			argument_record.dependent_bound_parameter != kNoTemplateParameter ||
			argument_record.bound == 0) return false;
		const TemplateArgument pattern_width(TEMPLATE_ARGUMENT_INTEGRAL,
			pattern_record.dependent_bound_type, 0,
			static_cast<std::uint32_t>(dependent));
		const TemplateArgument argument_width(TEMPLATE_ARGUMENT_INTEGRAL,
			pattern_record.dependent_bound_type,
			NormalizeIntegralConstant(pattern_record.dependent_bound_type,
				static_cast<std::int64_t>(argument_record.bound)));
		return DeduceFunctionTemplatePackArgument(
			pattern_width, argument_width, parameters, deduced);
	}
	case TYPE_FUNCTION:
	{
		if (pattern_record.cv != argument_record.cv ||
			pattern_record.ref_qualifier != argument_record.ref_qualifier ||
			!DeduceFunctionTemplatePackType(pattern_record.child,
				argument_record.child, parameters, deduced)) return false;
		const TypeId* pattern_parameters = program_->types.Parameters(pattern);
		const TypeId* argument_parameters = program_->types.Parameters(argument);
		const std::size_t pack_parameter = pattern_record.variadic &&
			pattern_record.parameter_count != 0 ?
			FunctionTemplateShapePackParameter(
				pattern_parameters[pattern_record.parameter_count - 1],
				parameters) : parameters.size();
		if (pack_parameter == parameters.size())
		{
			if (pattern_record.parameter_count != argument_record.parameter_count ||
				pattern_record.variadic != argument_record.variadic) return false;
			for (std::size_t i = 0; i < pattern_record.parameter_count; ++i)
				if (!DeduceFunctionTemplatePackType(pattern_parameters[i],
					argument_parameters[i], parameters, deduced)) return false;
			return true;
		}
		const std::size_t fixed = pattern_record.parameter_count - 1;
		if (argument_record.variadic || argument_record.parameter_count < fixed)
			return false;
		for (std::size_t i = 0; i < fixed; ++i)
			if (!DeduceFunctionTemplatePackType(pattern_parameters[i],
				argument_parameters[i], parameters, deduced)) return false;
		const std::size_t prior_size =
			deduced->pack_arguments[pack_parameter].size();
		const bool prior_started =
			deduced->pack_deduction_started[pack_parameter] != 0;
		deduced->pack_deduction_positions[pack_parameter] = 0;
		for (std::size_t i = fixed; i < argument_record.parameter_count; ++i)
			if (!DeduceFunctionTemplatePackType(pattern_parameters[fixed],
				argument_parameters[i], parameters, deduced)) return false;
		if ((prior_started &&
			deduced->pack_arguments[pack_parameter].size() != prior_size) ||
			deduced->pack_deduction_positions[pack_parameter] !=
				deduced->pack_arguments[pack_parameter].size()) return false;
		deduced->pack_deduction_started[pack_parameter] = 1;
		return true;
	}
	case TYPE_MEMBER_POINTER:
		return DeduceFunctionTemplatePackType(
				static_cast<TypeId>(pattern_record.bound),
				static_cast<TypeId>(argument_record.bound), parameters, deduced) &&
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
		const auto search_bases = [this, pattern, argument_entity,
			&parameters, deduced]() -> bool
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
		};
		FunctionTemplateDeduction direct = *deduced;
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
				parameters, &direct)) return false;
		}
		else if (class_pattern != argument_pattern)
			return search_bases();
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
				parameters[dependent].pack;
			if (expansion)
			{
				const bool destination_pack = !class_parameters.empty() &&
					TemplateParameterForArgument(
						class_parameters, pattern_index).pack;
				std::size_t last = argument_arguments.size();
				if (destination_pack)
				{
					const std::size_t remaining =
						pattern_arguments.size() - pattern_index - 1;
					if (argument_index + remaining > argument_arguments.size())
						return false;
					last = argument_arguments.size() - remaining;
				}
				else
				{
					// Canonical class arguments already contain omitted defaults.
					// Anchor that suffix once so this function pack consumes only
					// the preceding fixed-primary argument span.
					if (pattern_arguments.size() != argument_arguments.size())
						return search_bases();
					FunctionTemplateDeduction suffix = direct;
					last = pattern_arguments.size();
					while (last > pattern_index + 1)
					{
						++function_template_deduction_visits_;
						const TemplateArgument& suffix_pattern =
							pattern_arguments[last - 1];
						if (suffix_pattern.pack_expansion) break;
						FunctionTemplateDeduction trial = suffix;
						const bool dependent_suffix =
							suffix_pattern.IsDependent() ||
							(suffix_pattern.kind == TEMPLATE_ARGUMENT_TYPE &&
							 FunctionTemplateTypeIsDependent(suffix_pattern.type));
						const bool matches = dependent_suffix ?
							DeduceFunctionTemplatePackArgument(suffix_pattern,
								argument_arguments[last - 1], parameters, &trial) :
							suffix_pattern == argument_arguments[last - 1];
						if (!matches)
							break;
						suffix = trial;
						--last;
					}
					direct = suffix;
				}
				const std::size_t prior_size =
					direct.pack_arguments[dependent].size();
				const bool prior_started =
					direct.pack_deduction_started[dependent] != 0;
				direct.pack_deduction_positions[dependent] = 0;
				while (argument_index < last)
					if (!DeduceFunctionTemplatePackArgument(pattern_argument,
						argument_arguments[argument_index++], parameters,
						&direct)) return search_bases();
				if ((prior_started &&
					 direct.pack_arguments[dependent].size() != prior_size) ||
					direct.pack_deduction_positions[dependent] !=
					 direct.pack_arguments[dependent].size()) return search_bases();
				direct.pack_deduction_started[dependent] = 1;
				if (!destination_pack)
				{
					pattern_index = pattern_arguments.size();
					argument_index = argument_arguments.size();
				}
				else ++pattern_index;
				continue;
			}
			if (argument_index >= argument_arguments.size() ||
				!DeduceFunctionTemplatePackArgument(pattern_argument,
					argument_arguments[argument_index], parameters, &direct))
				return search_bases();
			++pattern_index;
			++argument_index;
		}
		if (argument_index != argument_arguments.size()) return search_bases();
		*deduced = direct;
		return true;
	}
	case TYPE_FUNDAMENTAL:
	case TYPE_INVALID:
	case TYPE_QUALIFIED:
		return pattern == argument;
	}
	return false;
}

std::size_t Analyzer::RequiredFunctionParameterCount(
	const std::vector<ParameterInfo>& parameters) const
{
	std::size_t required = parameters.size();
	while (required != 0 &&
		parameters[required - 1].default_argument != kNoNode) --required;
	return required;
}

bool Analyzer::DeduceFunctionTemplateOverloadArgument(
	TypeId parameter, NodeId syntax, ScopeId scope,
	const std::vector<TemplateParameter>& parameters,
	FunctionTemplateDeduction* deduced)
{
	TypeId list_element = kNoType;
	if (syntax != kNoNode && arena_->IsTag(syntax, ::cppgm::syntax::STAG_BRACED_INIT_LIST) &&
		IsInitializerListType(parameter, &list_element))
	{
		FunctionTemplateDeduction trial = *deduced;
		bool saw_element = false;
		for (std::uint32_t edge = arena_->FirstEdge(syntax); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId element_syntax = arena_->EdgeChild(edge);
			ExpressionInfo element;
			if (!ReusePreparedBracedExpression(
				element_syntax, kNoType, &element))
				element = AnalyzeExpression(element_syntax, scope);
			const TypeId argument = program_->types.RemoveTopCv(
				Decay(element.type));
			if (!DeduceFunctionTemplatePackType(
				list_element, argument, parameters, &trial)) return false;
			saw_element = true;
		}
		if (!saw_element) return false;
		*deduced = trial;
		return true;
	}
	while (syntax != kNoNode &&
		arena_->IsTag(syntax, ::cppgm::syntax::STAG_PARENTHESIZED_EXPRESSION))
		syntax = FirstSemanticChild(syntax);
	if (syntax != kNoNode && arena_->IsTag(syntax, ::cppgm::syntax::STAG_UNARY_EXPRESSION) &&
		PayloadSource(syntax) == "&")
		syntax = FirstSemanticChild(syntax);
	while (syntax != kNoNode &&
		arena_->IsTag(syntax, ::cppgm::syntax::STAG_PARENTHESIZED_EXPRESSION))
		syntax = FirstSemanticChild(syntax);
	if (syntax == kNoNode || !arena_->IsTag(syntax, ::cppgm::syntax::STAG_ID_EXPRESSION))
		return false;

	const std::string spelling = arena_->Payload(syntax);
	NamePath explicit_base;
	std::vector<NodeId> explicit_syntax;
	const bool explicit_id = CollectExplicitTemplateArguments(
		syntax, &explicit_base, &explicit_syntax);
	if (!explicit_id)
	{
		const NamePath structured = StructuredNamePath(syntax);
		const std::vector<std::size_t> templates = structured.Empty() ?
			FindFunctionTemplates(scope, SyntaxNamePath(syntax)) :
			FindStructuredFunctionTemplates(syntax, scope);
		if (!templates.empty()) return false;
	}
	const std::vector<BindingId> candidates =
		FunctionCandidates(scope, spelling, 0, syntax);
	FunctionTemplateDeduction selected;
	std::size_t matches = 0;
	const TypeRecord shape = program_->types.Get(
		program_->types.RemoveTopCv(parameter));
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		const FunctionInfo& function = GetFunction(candidates[i]);
		TypeId argument = kNoType;
		if (shape.kind == TYPE_POINTER && function.member_owner == kNoType)
			argument = program_->types.Pointer(function.type);
		else if (shape.kind == TYPE_FUNCTION &&
			function.member_owner == kNoType)
			argument = function.type;
		else if (shape.kind == TYPE_MEMBER_POINTER &&
			function.member_owner != kNoType)
			argument = program_->types.MemberPointer(
				function.member_owner, function.type);
		if (argument == kNoType) continue;
		FunctionTemplateDeduction trial = *deduced;
		if (!DeduceFunctionTemplatePackType(
			parameter, argument, parameters, &trial)) continue;
		selected = trial;
		if (++matches != 1) return false;
	}
	if (matches != 1) return false;
	*deduced = selected;
	return true;
}

void Analyzer::AppendConversionFunctionTemplateCandidates(
	EntityId entity, TypeId target, std::vector<BindingId>* candidates)
{
	if (!candidates || target == kNoType) return;
	std::vector<EntityId> pending(1, entity);
	std::vector<EntityId> owners;
	std::unordered_set<EntityId> visited;
	while (!pending.empty())
	{
		entity = pending.back();
		pending.pop_back();
		if (entity == kNoEntity || entity >= program_->entities.size() ||
			!visited.insert(entity).second) continue;
		owners.push_back(entity);
		const EntityRecord& record = program_->entities[entity];
		for (std::size_t base = record.direct_base_count;
			base != 0; --base)
			pending.push_back(program_->DirectBase(entity, base - 1).entity);
	}
	for (std::size_t owner = 0; owner < owners.size(); ++owner)
	{
		entity = owners[owner];
		if (entity >= entity_conversion_function_templates_.size())
			continue;
		const std::vector<std::size_t>& patterns =
			entity_conversion_function_templates_[entity];
		for (std::size_t p = 0; p < patterns.size(); ++p)
		{
			if (patterns[p] >= function_templates_.size())
				ThrowInternalCompilerError(
					"invalid conversion function template candidate");
			const FunctionTemplatePattern& pattern = function_templates_[patterns[p]];
			if (!pattern.conversion_template) continue;
			const TypeRecord& function = program_->types.Get(pattern.shape_type);
			if (function.kind != TYPE_FUNCTION) continue;
			TypeId parameter = function.child;
			TypeId argument = target;
			TypeRecord parameter_top = program_->types.Get(parameter);
			TypeRecord argument_top = program_->types.Get(argument);
			if (parameter_top.kind == TYPE_LVALUE_REFERENCE ||
				parameter_top.kind == TYPE_RVALUE_REFERENCE)
			{
				parameter = parameter_top.child;
				parameter_top = program_->types.Get(parameter);
			}
			if (argument_top.kind == TYPE_LVALUE_REFERENCE ||
				argument_top.kind == TYPE_RVALUE_REFERENCE)
				argument = argument_top.child;
			else
			{
				if (parameter_top.kind == TYPE_ARRAY ||
					parameter_top.kind == TYPE_FUNCTION)
					parameter = Decay(parameter);
				else parameter = program_->types.RemoveTopCv(parameter);
			}
			argument = program_->types.RemoveTopCv(argument);
			FunctionTemplateDeduction deduced(pattern.parameters);
			if (!DeduceFunctionTemplatePackType(parameter, argument,
				pattern.parameters, &deduced)) continue;

			std::vector<TemplateArgument> canonical;
			std::vector<std::uint32_t> offsets;
			offsets.reserve(pattern.parameters.size() + 1);
			bool valid = true;
			for (std::size_t i = 0; i < pattern.parameters.size(); ++i)
			{
				if (canonical.size() >
					std::numeric_limits<std::uint32_t>::max())
				{
					valid = false;
					break;
				}
				offsets.push_back(static_cast<std::uint32_t>(canonical.size()));
				if (pattern.parameters[i].pack)
					canonical.insert(canonical.end(),
						deduced.pack_arguments[i].begin(),
						deduced.pack_arguments[i].end());
				else
				{
					TemplateArgument value = deduced.fixed_arguments[i];
					value.kind = pattern.parameters[i].kind;
					if (value.type == kNoType &&
						pattern.parameters[i].default_argument == kNoNode)
					{
						valid = false;
						break;
					}
					canonical.push_back(value);
				}
			}
			if (!valid || canonical.size() >
				std::numeric_limits<std::uint32_t>::max()) continue;
			offsets.push_back(static_cast<std::uint32_t>(canonical.size()));
			candidate_substitution_failures_.push_back(0);
			const BindingId specialization = InstantiateFunctionTemplate(
				patterns[p], canonical, offsets);
			const bool substitution_failed = CandidateSubstitutionFailed();
			candidate_substitution_failures_.pop_back();
			if (specialization != kNoBinding && !substitution_failed &&
				std::find(candidates->begin(), candidates->end(), specialization) ==
					candidates->end())
				candidates->push_back(specialization);
		}
	}
}

void Analyzer::DeduceFunctionTemplatePatterns(
	const std::vector<std::size_t>& patterns,
	const std::vector<ExpressionInfo>& arguments,
	std::vector<BindingId>* specializations,
	const std::vector<TypeId>* explicit_arguments,
	const std::vector<TemplateArgument>* canonical_explicit_arguments,
	ScopeId argument_scope,
	const std::vector<NodeId>* argument_syntax)
{
	for (std::size_t p = 0; p < patterns.size(); ++p)
	{
		if (patterns[p] >= function_templates_.size())
			ThrowInternalCompilerError("invalid function template candidate");
		const FunctionTemplatePattern& pattern =
			function_templates_[patterns[p]];
		const TypeRecord& function_type =
			program_->types.Get(pattern.shape_type);
		const std::size_t fixed_function_parameters =
			pattern.function_parameter_pack ?
			function_type.parameter_count - 1 : function_type.parameter_count;
		if (function_type.kind != TYPE_FUNCTION ||
			arguments.size() < pattern.required_parameter_count ||
			(!pattern.function_parameter_pack && !function_type.variadic &&
			 arguments.size() > function_type.parameter_count)) continue;
		const TypeId* parameters =
			program_->types.Parameters(pattern.shape_type);
		FunctionTemplateDeduction deduced(pattern.parameters);
		std::vector<std::uint8_t> explicitly_specified(
			pattern.parameters.size(), 0);
		bool valid = true;
		if (explicit_arguments && canonical_explicit_arguments)
			ThrowInternalCompilerError(
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
				explicitly_specified[parameter_index] = 1;
			}
			if (explicit_index != explicit_count) valid = false;
		}
		std::size_t outer_pack_parameter = pattern.parameters.size();
		std::size_t outer_pack_prior_size = 0;
		bool outer_pack_prior_started = false;
		bool outer_pack_sequence_started = false;
		if (pattern.function_parameter_pack)
			outer_pack_parameter = FunctionTemplateShapePackParameter(
				parameters[fixed_function_parameters], pattern.parameters);
		for (std::size_t a = 0; a < arguments.size() && valid; ++a)
		{
			// An ellipsis accepts trailing call operands but contributes no
			// deduction constraint. Explicit template arguments may still make
			// the function-template specialization complete.
			if (!pattern.function_parameter_pack && function_type.variadic &&
				a >= function_type.parameter_count) continue;
			const bool pack_element = pattern.function_parameter_pack &&
				a >= fixed_function_parameters;
			if (arguments[a].type == kNoType)
			{
				const std::size_t function_parameter = pack_element ?
					fixed_function_parameters : a;
				if (!pack_element && argument_syntax &&
					a < argument_syntax->size() &&
					function_parameter < function_type.parameter_count &&
					(function_parameter >=
						pattern.function_parameter_nondeduced.size() ||
					 pattern.function_parameter_nondeduced[function_parameter] == 0))
				{
					TypeId parameter = parameters[function_parameter];
					const TypeRecord top = program_->types.Get(parameter);
					parameter = top.kind == TYPE_LVALUE_REFERENCE ||
						top.kind == TYPE_RVALUE_REFERENCE ? top.child :
						program_->types.RemoveTopCv(parameter);
					(void)DeduceFunctionTemplateOverloadArgument(parameter,
						(*argument_syntax)[a], argument_scope,
						pattern.parameters, &deduced);
				}
				continue;
			}
			if (pack_element && !outer_pack_sequence_started &&
				outer_pack_parameter < pattern.parameters.size())
			{
				outer_pack_prior_size =
					deduced.pack_arguments[outer_pack_parameter].size();
				outer_pack_prior_started =
					deduced.pack_deduction_started[outer_pack_parameter] != 0;
				deduced.pack_deduction_positions[outer_pack_parameter] = 0;
				outer_pack_sequence_started = true;
			}
			const std::size_t function_parameter = pack_element ?
				fixed_function_parameters : a;
			if (function_parameter <
					pattern.function_parameter_nondeduced.size() &&
				pattern.function_parameter_nondeduced[function_parameter] != 0)
				continue;
			TypeId parameter = parameters[function_parameter];
			if ((explicit_arguments || canonical_explicit_arguments) &&
				FunctionTemplateTypeIsDependent(parameter) &&
				!FunctionTemplateTypeUsesUnspecifiedParameter(parameter,
					pattern.parameters, explicitly_specified)) continue;
			TypeId argument = EffectiveType(arguments[a].type);
			const TypeRecord& parameter_record =
				program_->types.Get(parameter);
			if (parameter_record.kind == TYPE_LVALUE_REFERENCE ||
				parameter_record.kind == TYPE_RVALUE_REFERENCE)
			{
				if (parameter_record.kind == TYPE_RVALUE_REFERENCE &&
					arguments[a].category == VALUE_LVALUE &&
					(arguments[a].node == kNoDumpEdge ||
					 dump_.nodes[arguments[a].node].kind != DUMP_BRACED_INIT_LIST))
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
		if (valid && pattern.function_parameter_pack &&
			outer_pack_parameter < pattern.parameters.size())
		{
			if (!outer_pack_sequence_started)
			{
				outer_pack_prior_size =
					deduced.pack_arguments[outer_pack_parameter].size();
				outer_pack_prior_started =
					deduced.pack_deduction_started[outer_pack_parameter] != 0;
				deduced.pack_deduction_positions[outer_pack_parameter] = 0;
			}
			valid = (!outer_pack_prior_started ||
				deduced.pack_arguments[outer_pack_parameter].size() ==
					outer_pack_prior_size) &&
				deduced.pack_deduction_positions[outer_pack_parameter] ==
					deduced.pack_arguments[outer_pack_parameter].size();
			if (valid)
				deduced.pack_deduction_started[outer_pack_parameter] = 1;
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
			candidate_substitution_failures_.push_back(0);
			const BindingId specialization =
				InstantiateFunctionTemplate(patterns[p], canonical, offsets);
			const bool substitution_failed = CandidateSubstitutionFailed();
			candidate_substitution_failures_.pop_back();
			if (specializations && specialization != kNoBinding &&
				!substitution_failed)
				specializations->push_back(specialization);
		}
	}
}

void Analyzer::DeduceFunctionTemplates(ScopeId scope,
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
		syntax == kNoNode ? ParseNamePath(
			spelling, NAME_PATH_PARSE_TEMPLATE) : SyntaxNamePath(syntax);
	const NameId name = path.Last();
	if (name == 0) return;
	const std::vector<ScopeId> visible_owners =
		FindFunctionTemplateOwners(scope, path);
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

void Analyzer::DeduceFunctionTemplatePatternsWithExplicitSyntax(
	const std::vector<std::size_t>& patterns,
	const std::vector<ExpressionInfo>& arguments,
	const std::vector<NodeId>& explicit_syntax, ScopeId use_scope,
	std::vector<BindingId>* specializations,
	const std::vector<NodeId>* argument_syntax)
{
	for (std::size_t i = 0; i < patterns.size(); ++i)
	{
		if (patterns[i] >= function_templates_.size())
			ThrowInternalCompilerError("invalid function template candidate");
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
		candidate_substitution_failures_.push_back(0);
		const bool built = BuildTemplateArguments(explicit_parameters,
			explicit_syntax, use_scope, pattern.lexical_scope,
			&canonical, false);
		const bool substitution_failed = CandidateSubstitutionFailed();
		candidate_substitution_failures_.pop_back();
		if (!built || substitution_failed) continue;
		const std::vector<std::size_t> one_pattern(1, patterns[i]);
		DeduceFunctionTemplatePatterns(one_pattern, arguments,
			specializations, 0, &canonical, use_scope, argument_syntax);
	}
}

std::vector<BindingId> Analyzer::FunctionTemplateTargetCandidates(
	ScopeId scope, const std::string& spelling, TypeId target, NodeId syntax)
{
	NamePath structured_base;
	std::vector<NodeId> explicit_syntax;
	const bool structured_explicit = CollectExplicitTemplateArguments(
		syntax, &structured_base, &explicit_syntax);
	const bool explicit_id = structured_explicit;
	if (!structured_explicit) structured_base = StructuredNamePath(syntax);
	std::vector<std::size_t> patterns = !structured_base.Empty() ?
		FindStructuredFunctionTemplates(syntax, scope) :
		FindFunctionTemplates(scope, SyntaxNamePath(syntax));
	if (patterns.empty() && !structured_base.Empty())
		patterns = FindFunctionTemplates(scope, structured_base);
	std::vector<BindingId> result;
	for (std::size_t i = 0; i < patterns.size(); ++i)
	{
		if (patterns[i] >= function_templates_.size())
			ThrowInternalCompilerError("invalid target function template candidate");
		const FunctionTemplatePattern& pattern = function_templates_[patterns[i]];
		FunctionTemplateDeduction deduced(pattern.parameters);
		if (explicit_id)
		{
			std::vector<TemplateArgument> explicit_arguments;
			candidate_substitution_failures_.push_back(0);
			const bool built = BuildTemplateArguments(pattern.parameters,
				explicit_syntax, scope, pattern.lexical_scope,
				&explicit_arguments, false);
			const bool substitution_failed = CandidateSubstitutionFailed();
			candidate_substitution_failures_.pop_back();
			if (!built || substitution_failed) continue;
			std::size_t argument = 0;
			for (std::size_t parameter = 0;
				parameter < pattern.parameters.size() &&
				argument < explicit_arguments.size(); ++parameter)
			{
				if (pattern.parameters[parameter].pack)
				{
					while (argument < explicit_arguments.size())
						deduced.pack_arguments[parameter].push_back(
							explicit_arguments[argument++]);
					break;
				}
				deduced.fixed_arguments[parameter] =
					explicit_arguments[argument++];
			}
			if (argument != explicit_arguments.size()) continue;
		}
		TypeId deduction_target = target;
		const TypeRecord& shape = program_->types.Get(pattern.shape_type);
		const TypeRecord& desired = program_->types.Get(target);
		if (shape.kind == TYPE_FUNCTION && desired.kind == TYPE_FUNCTION &&
			shape.parameter_count == desired.parameter_count &&
			pattern.function_parameter_nondeduced.size() ==
				desired.parameter_count)
		{
			std::vector<TypeId> parameters(
				program_->types.Parameters(target),
				program_->types.Parameters(target) + desired.parameter_count);
			const TypeId* shape_parameters =
				program_->types.Parameters(pattern.shape_type);
			for (std::size_t parameter = 0;
				parameter < parameters.size(); ++parameter)
				if (pattern.function_parameter_nondeduced[parameter] != 0)
					parameters[parameter] = shape_parameters[parameter];
			const TypeId result = pattern.deferred_result_formation ?
				shape.child : desired.child;
			deduction_target = program_->types.Function(result,
				parameters, desired.variadic, desired.cv,
				desired.ref_qualifier);
		}
		if (!DeduceFunctionTemplatePackType(pattern.shape_type,
			deduction_target, pattern.parameters, &deduced))
			continue;
		std::vector<TemplateArgument> canonical;
		std::vector<std::uint32_t> offsets;
		offsets.reserve(pattern.parameters.size() + 1);
		bool valid = true;
		for (std::size_t parameter = 0;
			parameter < pattern.parameters.size(); ++parameter)
		{
			if (canonical.size() >
				std::numeric_limits<std::uint32_t>::max())
			{
				valid = false;
				break;
			}
			offsets.push_back(static_cast<std::uint32_t>(canonical.size()));
			if (pattern.parameters[parameter].pack)
				canonical.insert(canonical.end(),
					deduced.pack_arguments[parameter].begin(),
					deduced.pack_arguments[parameter].end());
			else
			{
				TemplateArgument argument = deduced.fixed_arguments[parameter];
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
		candidate_substitution_failures_.push_back(0);
		const BindingId candidate = InstantiateFunctionTemplate(
			patterns[i], canonical, offsets);
		const bool substitution_failed = CandidateSubstitutionFailed();
		candidate_substitution_failures_.pop_back();
		if (candidate != kNoBinding &&
			!substitution_failed &&
			std::find(result.begin(), result.end(), candidate) == result.end())
			result.push_back(candidate);
	}
	return result;
}

bool Analyzer::HasUniqueFunctionAddressTarget(
	ScopeId scope, NodeId syntax, TypeId target)
{
	while (syntax != kNoNode &&
		arena_->IsTag(syntax, ::cppgm::syntax::STAG_PARENTHESIZED_EXPRESSION))
		syntax = FirstSemanticChild(syntax);
	if (syntax != kNoNode && arena_->IsTag(syntax, ::cppgm::syntax::STAG_UNARY_EXPRESSION) &&
		PayloadSource(syntax) == "&")
		syntax = FirstSemanticChild(syntax);
	while (syntax != kNoNode &&
		arena_->IsTag(syntax, ::cppgm::syntax::STAG_PARENTHESIZED_EXPRESSION))
		syntax = FirstSemanticChild(syntax);
	if (syntax == kNoNode || !arena_->IsTag(syntax, ::cppgm::syntax::STAG_ID_EXPRESSION))
		return false;

	TypeId desired = program_->types.RemoveTopCv(target);
	TypeRecord shape = program_->types.Get(desired);
	if (shape.kind == TYPE_LVALUE_REFERENCE ||
		shape.kind == TYPE_RVALUE_REFERENCE)
	{
		desired = program_->types.RemoveTopCv(shape.child);
		shape = program_->types.Get(desired);
	}
	const TypeId member_target = shape.kind == TYPE_MEMBER_POINTER &&
		program_->types.IsFunction(shape.child) ? desired : kNoType;
	const bool function_pointer_target = shape.kind == TYPE_POINTER &&
		program_->types.IsFunction(shape.child);
	if (member_target == kNoType && !function_pointer_target) return false;
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
		const FunctionInfo& function = GetFunction(candidates[i]);
		if (function.type != desired) continue;
		if (member_target != kNoType)
		{
			const ConversionRank member_conversion =
				function.member_owner == kNoType ? CONVERSION_INVALID :
				Conversion(program_->types.MemberPointer(
					function.member_owner, function.type), VALUE_PRVALUE,
					false, member_target);
			if (member_conversion == CONVERSION_INVALID)
				continue;
		}
		else if (function.member_owner != kNoType) continue;
		const BindingId canonical = program_->bindings[candidates[i]].canonical;
		if (selected == kNoBinding || selected == canonical)
		{
			selected = canonical;
			continue;
		}
		const FunctionInfo& prior = GetFunction(selected);
		const FunctionInfo& candidate = GetFunction(canonical);
		if (prior.template_specialization != candidate.template_specialization)
		{
			if (prior.template_specialization) selected = canonical;
			continue;
		}
		const int preference =
			CompareFunctionTemplateConstraints(candidate, prior);
		if (preference > 0) selected = canonical;
		else if (preference == 0) return false;
	}
	return selected != kNoBinding;
}

bool Analyzer::AnalyzeFunctionId(NodeId node, ScopeId scope,
	TypeId target, ExpressionInfo* result)
{
	const std::string spelling = arena_->Payload(node);
	EntityId naming_class = kNoEntity;
	std::vector<BindingId> candidates =
		FunctionCandidates(scope, spelling, &naming_class, node);
	if (CandidateSubstitutionFailed()) return true;
	NamePath structured_base;
	std::vector<TypeId> explicit_arguments;
	const bool explicit_template_id = ParseExplicitTemplateArguments(
		node, scope, &structured_base, &explicit_arguments);
	if (!explicit_template_id) structured_base = StructuredNamePath(node);
	std::vector<std::size_t> template_patterns = !structured_base.Empty() ?
		FindStructuredFunctionTemplates(node, scope) :
		FindFunctionTemplates(scope, SyntaxNamePath(node));
	if (template_patterns.empty() && !structured_base.Empty())
		template_patterns = FindFunctionTemplates(scope, structured_base);
	TypeId desired = target;
	TypeId member_target = kNoType;
	bool function_pointer_target = false;
	if (desired != kNoType)
	{
		desired = program_->types.RemoveTopCv(desired);
		TypeRecord target_record = program_->types.Get(desired);
		if (target_record.kind == TYPE_LVALUE_REFERENCE ||
			target_record.kind == TYPE_RVALUE_REFERENCE)
		{
			desired = program_->types.RemoveTopCv(target_record.child);
			target_record = program_->types.Get(desired);
		}
		if (target_record.kind == TYPE_POINTER)
		{
			function_pointer_target = program_->types.IsFunction(
				target_record.child);
			desired = target_record.child;
		}
		else if (target_record.kind == TYPE_MEMBER_POINTER)
		{
			member_target = desired;
			desired = target_record.child;
		}
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
	if (member_target != kNoType &&
		FindChild(node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME) != kNoNode)
	{
		const ScopeId naming_scope = naming_class == kNoEntity ? kNoScope :
			program_->entities[naming_class].member_scope;
		const NameId member_name = StructuredNamePath(node).Last();
		const LookupResult direct_templates = naming_scope == kNoScope ?
			LookupResult() : program_->LookupDirect(
				naming_scope, member_name, LOOKUP_FUNCTION_TEMPLATE);
		const bool direct_template =
			direct_templates.FunctionTemplateOwnerCount() != 0;
		if (direct_template)
			candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
				[this, naming_scope](BindingId candidate) {
					return program_->bindings[candidate].owner != naming_scope;
				}), candidates.end());
		const LookupResult colliding_type =
			LookupStructuredName(node, scope, LOOKUP_TYPE);
		if (CandidateSubstitutionFailed()) return true;
		ScopeId candidate_owner = kNoScope;
		bool ambiguous_bases = false;
		for (std::size_t i = 0; !direct_template && i < candidates.size(); ++i)
		{
			const ScopeId owner = program_->bindings[candidates[i]].owner;
			if (candidate_owner == kNoScope) candidate_owner = owner;
			else if (candidate_owner != owner) ambiguous_bases = true;
		}
		if ((!direct_template && colliding_type.type != kNoType) ||
			ambiguous_bases)
		{
			*result = CandidateExpressionFailure(
				"ambiguous member address lookup");
			return true;
		}
	}
	if (candidates.empty()) return false;
	BindingId selected = kNoBinding;
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		const FunctionInfo& candidate_function = GetFunction(candidates[i]);
		bool target_matches = desired == kNoType ||
			candidate_function.type == desired;
		if (target_matches && member_target != kNoType)
			target_matches = candidate_function.member_owner != kNoType &&
				Conversion(program_->types.MemberPointer(
					candidate_function.member_owner, candidate_function.type),
					VALUE_PRVALUE, false, member_target) != CONVERSION_INVALID;
		else if (target_matches && function_pointer_target)
			target_matches = candidate_function.member_owner == kNoType;
		if (target_matches)
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
				*result = CandidateExpressionFailure(
					"ambiguous overloaded function id");
				return true;
			}
			selected = candidates[i];
			if (desired == kNoType && candidates.size() != 1)
			{
				result->binding = candidates[0];
				return true;
			}
		}
	}
	if (selected == kNoBinding)
	{
		*result = CandidateExpressionFailure(
			"no target-matching overloaded function");
		return true;
	}
	const EntityId member_object = member_target == kNoType ? kNoEntity :
		EntityOf(static_cast<TypeId>(
			program_->types.Get(member_target).bound));
	if (!CanAccessMember(selected, naming_class, member_object))
	{
		*result = CandidateExpressionFailure("inaccessible member function");
		return true;
	}
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
	if (constant_evaluation_suppressed_depth_ == 0 &&
		constant_expression_required_depth_ == 0 &&
		constexpr_evaluation_depth_ == 0)
	{
		if (retain_lowering_facts_ && !function.defined &&
			program_->bindings[selected].member_owner != kNoEntity)
			GetMutableFunction(selected).deferred = true;
		DemandFunction(selected);
	}
	++expression_count_;
	return true;
}

}
}
