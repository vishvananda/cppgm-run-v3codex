#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::DeduceFunctionTemplateType(TypeId pattern,
	TypeId argument, std::vector<TypeId>* deduced) const
{
	for (std::size_t i = 0;
		i < function_template_shape_parameters_.size() && i < deduced->size(); ++i)
		if (pattern == function_template_shape_parameters_[i])
		{
			argument = program_->types.RemoveTopCv(argument);
			if ((*deduced)[i] != kNoType && (*deduced)[i] != argument)
				return false;
			(*deduced)[i] = argument;
			return true;
		}
	const TypeRecord& pattern_record = program_->types.Get(pattern);
	if (pattern_record.kind == TYPE_QUALIFIED)
		return DeduceFunctionTemplateType(pattern_record.child,
			program_->types.RemoveTopCv(argument), deduced);
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
			pattern_template >= class_templates_.size() ||
			pattern_entity >= class_template_argument_begin_by_entity_.size() ||
			argument_entity >= class_template_argument_begin_by_entity_.size())
			return false;
		const std::size_t pattern_first =
			class_template_argument_begin_by_entity_[pattern_entity];
		const std::size_t argument_first =
			class_template_argument_begin_by_entity_[argument_entity];
		const std::size_t count =
			class_templates_[pattern_template].type_parameters.size();
		if (pattern_first > class_template_entity_arguments_.size() ||
			argument_first > class_template_entity_arguments_.size() ||
			count > class_template_entity_arguments_.size() - pattern_first ||
			count > class_template_entity_arguments_.size() - argument_first)
			throw std::logic_error(
				"invalid class template deduction argument range");
		for (std::size_t i = 0; i < count; ++i)
			if (!DeduceFunctionTemplateType(
				class_template_entity_arguments_[pattern_first + i],
				class_template_entity_arguments_[argument_first + i], deduced))
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
	std::vector<BindingId>* specializations)
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
		std::vector<TypeId> deduced(pattern.type_parameters.size(), kNoType);
		bool valid = true;
		for (std::size_t a = 0; a < arguments.size() && valid; ++a)
		{
			if (arguments[a].type == kNoType)
			{
				valid = false;
				continue;
			}
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
	if (spelling.find('<') != std::string::npos) return;
	const NamePath path = ParseNamePath(spelling);
	const NameId name = path.Last();
	if (name == 0) return;
	ScopeId visible_owner = kNoScope;
	if (path.global || path.Size() > 1)
		visible_owner = ResolveOwner(scope, path);
	else
		for (ScopeId current = scope; current != kNoScope; )
		{
			const std::uint64_t key =
				(static_cast<std::uint64_t>(current) << 32) | name;
			if (template_function_sets_.Find(key))
			{
				visible_owner = current;
				break;
			}
			current = current < scope_parents_.size() ?
				scope_parents_[current] : kNoScope;
		}
	if (visible_owner == kNoScope) return;
	const std::uint64_t visible_key =
		(static_cast<std::uint64_t>(visible_owner) << 32) | name;
	const CompactIndexSequence* found =
		template_function_sets_.Find(visible_key);
	if (!found) return;
	const std::vector<std::size_t> patterns = found->Copy();
	std::vector<BindingId> specializations;
	DeduceFunctionTemplatePatterns(patterns, arguments, &specializations);
	for (std::size_t i = 0; i < specializations.size(); ++i)
	{
		const BindingId source = specializations[i];
		const BindingRecord& source_record = program_->bindings[source];
		if (source_record.owner == visible_owner) continue;
		const FunctionInfo& function = GetFunction(source);
		const FunctionSignatureKey signature_key(visible_owner, name,
			function.signature);
		++function_signature_lookups_;
		if (function_declarations_.Find(signature_key) != kNoBinding) continue;
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
}
