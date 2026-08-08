#include "pa12_semantic_detail.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::FunctionTemplateTypeIsDependent(TypeId type) const
{
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
	case TYPE_ARRAY:
		dependent = FunctionTemplateTypeIsDependent(record.child);
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
		const std::size_t count =
			class_templates_[template_index].type_parameters.size();
		if (entity.template_argument_count != count ||
			first > program_->template_arguments.size() ||
			count > program_->template_arguments.size() - first)
			throw std::logic_error(
				"invalid dependent class template argument range");
		for (std::size_t i = 0; i < count && !dependent; ++i)
			if (FunctionTemplateTypeIsDependent(
				program_->template_arguments[first + i])) dependent = true;
		break;
	}
	case TYPE_FUNDAMENTAL:
	case TYPE_INVALID:
		break;
	}
	function_template_dependency_cache_[type] = dependent ? 2 : 1;
	return dependent;
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
		const std::size_t count =
			class_templates_[pattern_template].type_parameters.size();
		if (pattern_owner.template_argument_count != count ||
			argument_owner.template_argument_count != count ||
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

void SemanticAnalyzer::DeduceFunctionTemplatePatterns(
	const std::vector<std::size_t>& patterns,
	const std::vector<ExpressionInfo>& arguments,
	std::vector<BindingId>* specializations,
	const std::vector<TypeId>* explicit_arguments)
{
	for (std::size_t p = 0; p < patterns.size(); ++p)
	{
		if (patterns[p] >= function_templates_.size())
			throw std::logic_error("invalid function template candidate");
		const FunctionTemplatePattern& pattern =
			function_templates_[patterns[p]];
		const TypeRecord& function_type =
			program_->types.Get(pattern.shape_type);
		if (function_type.kind != TYPE_FUNCTION ||
			function_type.parameter_count != arguments.size()) continue;
		const TypeId* parameters =
			program_->types.Parameters(pattern.shape_type);
		if (explicit_arguments &&
			explicit_arguments->size() > pattern.type_parameters.size())
			continue;
		std::vector<TypeId> deduced(pattern.type_parameters.size(), kNoType);
		if (explicit_arguments)
			std::copy(explicit_arguments->begin(), explicit_arguments->end(),
				deduced.begin());
		bool valid = true;
		for (std::size_t a = 0; a < arguments.size() && valid; ++a)
		{
			if (arguments[a].type == kNoType) continue;
			TypeId parameter = parameters[a];
			TypeId argument = EffectiveType(arguments[a].type);
			const TypeRecord& parameter_record =
				program_->types.Get(parameter);
			if (parameter_record.kind == TYPE_LVALUE_REFERENCE ||
				parameter_record.kind == TYPE_RVALUE_REFERENCE)
				parameter = parameter_record.child;
			else
			{
				parameter = program_->types.RemoveTopCv(parameter);
				argument = program_->types.RemoveTopCv(Decay(argument));
			}
			valid = DeduceFunctionTemplateType(parameter, argument, &deduced);
		}
		for (std::size_t t = 0; t < deduced.size(); ++t)
			if (deduced[t] == kNoType) valid = false;
		if (valid)
		{
			const BindingId specialization =
				InstantiateFunctionTemplate(patterns[p], deduced);
			if (specializations && specialization != kNoBinding)
				specializations->push_back(specialization);
		}
	}
}

void SemanticAnalyzer::DeduceFunctionTemplates(ScopeId scope,
	const std::string& spelling,
	const std::vector<ExpressionInfo>& arguments)
{
	std::string base = spelling;
	std::vector<TypeId> explicit_arguments;
	const bool explicit_id = ParseExplicitTemplateArguments(scope, spelling,
		&base, &explicit_arguments);
	const NamePath path = ParseNamePath(base);
	const NameId name = path.Last();
	if (name == 0) return;
	const std::vector<ScopeId> visible_owners =
		FindFunctionTemplateOwners(scope, base);
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
		DeduceFunctionTemplatePatterns(patterns, arguments, &specializations,
			explicit_id ? &explicit_arguments : 0);
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
			using_function_declarations_.Insert(signature_key, alias);
		}
	}
}

std::vector<BindingId> SemanticAnalyzer::FunctionTemplateTargetCandidates(
	ScopeId scope, const std::string& spelling, TypeId target)
{
	std::string base = spelling;
	std::vector<TypeId> explicit_arguments;
	const bool explicit_id = ParseExplicitTemplateArguments(scope, spelling,
		&base, &explicit_arguments);
	const std::vector<std::size_t> patterns =
		FindFunctionTemplates(scope, base);
	std::vector<BindingId> result;
	for (std::size_t i = 0; i < patterns.size(); ++i)
	{
		if (patterns[i] >= function_templates_.size())
			throw std::logic_error("invalid target function template candidate");
		const FunctionTemplatePattern& pattern = function_templates_[patterns[i]];
		if (explicit_id &&
			explicit_arguments.size() > pattern.type_parameters.size())
			continue;
		std::vector<TypeId> deduced(pattern.type_parameters.size(), kNoType);
		if (explicit_id)
			std::copy(explicit_arguments.begin(), explicit_arguments.end(),
				deduced.begin());
		if (!DeduceFunctionTemplateType(pattern.shape_type, target, &deduced))
			continue;
		bool complete = true;
		for (std::size_t argument = 0; argument < deduced.size(); ++argument)
			if (deduced[argument] == kNoType) complete = false;
		if (!complete) continue;
		const BindingId candidate =
			InstantiateFunctionTemplate(patterns[i], deduced);
		if (candidate != kNoBinding &&
			std::find(result.begin(), result.end(), candidate) == result.end())
			result.push_back(candidate);
	}
	return result;
}

bool SemanticAnalyzer::AnalyzeFunctionId(NodeId node, ScopeId scope,
	TypeId target, ExpressionInfo* result)
{
	const std::string spelling = arena_->Payload(node);
	EntityId naming_class = kNoEntity;
	std::vector<BindingId> candidates =
		FunctionCandidates(scope, spelling, &naming_class);
	const std::vector<std::size_t> template_patterns =
		FindFunctionTemplates(scope, spelling);
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
			FunctionTemplateTargetCandidates(scope, spelling, desired);
		for (std::size_t i = 0; i < target_templates.size(); ++i)
			if (std::find(candidates.begin(), candidates.end(),
				target_templates[i]) == candidates.end())
				candidates.push_back(target_templates[i]);
	}
	if (desired == kNoType && !template_patterns.empty() &&
		spelling.find('<') == std::string::npos)
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
				throw std::runtime_error("ambiguous overloaded function id");
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
	DemandFunction(selected);
	++expression_count_;
	return true;
}

}
}
