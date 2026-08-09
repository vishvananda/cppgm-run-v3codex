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

namespace
{

bool FunctionTemplateNeedsPartitionIdentity(
	const std::vector<TemplateParameter>& parameters)
{
	std::size_t packs = 0;
	for (std::size_t i = 0; i < parameters.size(); ++i)
		if (parameters[i].pack)
		{
			++packs;
			if (i + 1 != parameters.size()) return true;
		}
	return packs > 1;
}

}

std::vector<std::size_t> SemanticAnalyzer::FindFunctionTemplates(
	ScopeId scope, const std::string& spelling)
{
	NamePath path = ParseNamePath(spelling);
	if (path.Empty()) return std::vector<std::size_t>();
	return FindFunctionTemplates(scope, path);
}

std::vector<std::size_t> SemanticAnalyzer::FindFunctionTemplates(
	ScopeId scope, const NamePath& path)
{
	if (path.Empty()) return std::vector<std::size_t>();
	const std::vector<ScopeId> owners =
		FindFunctionTemplateOwners(scope, path);
	const NameId name = path.Last();
	std::vector<std::size_t> result;
	for (std::size_t owner = 0; owner < owners.size(); ++owner)
	{
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owners[owner]) << 32) | name;
		const CompactIndexSequence* found =
			template_function_sets_.Find(key);
		if (!found) continue;
		for (std::size_t i = 0; i < found->Size(); ++i)
			result.push_back((*found)[i]);
	}
	return result;
}

std::vector<ScopeId> SemanticAnalyzer::FindFunctionTemplateOwners(
	ScopeId scope, const std::string& spelling)
{
	NamePath path = ParseNamePath(spelling);
	if (path.Empty()) return std::vector<ScopeId>();
	return FindFunctionTemplateOwners(scope, path);
}

std::vector<ScopeId> SemanticAnalyzer::FindFunctionTemplateOwners(
	ScopeId scope, const NamePath& path)
{
	if (path.Empty()) return std::vector<ScopeId>();
	LookupResult found;
	if (path.global || path.Size() > 1)
	{
		const ScopeId owner = ResolveOwner(scope, path);
		if (owner == kNoScope) return std::vector<ScopeId>();
		NamePath name;
		name.Push(path.Last());
		found = program_->LookupQualified(
			owner, name, LOOKUP_FUNCTION_TEMPLATE);
	}
	else found = LookupPath(scope, path, LOOKUP_FUNCTION_TEMPLATE);
	std::vector<ScopeId> result;
	result.reserve(found.FunctionTemplateOwnerCount());
	for (std::size_t i = 0; i < found.FunctionTemplateOwnerCount(); ++i)
		result.push_back(found.FunctionTemplateOwnerAt(i));
	return result;
}

bool SemanticAnalyzer::BuildFunctionTemplateArgumentOffsets(
	const std::vector<TemplateParameter>& parameters,
	std::size_t argument_count, std::vector<std::uint32_t>* offsets) const
{
	offsets->clear();
	offsets->reserve(parameters.size() + 1);
	std::size_t cursor = 0;
	for (std::size_t parameter = 0; parameter < parameters.size(); ++parameter)
	{
		if (cursor > std::numeric_limits<std::uint32_t>::max()) return false;
		offsets->push_back(static_cast<std::uint32_t>(cursor));
		if (parameters[parameter].pack)
		{
			if (parameter + 1 != parameters.size()) return false;
			cursor = argument_count;
		}
		else
		{
			if (cursor >= argument_count) return false;
			++cursor;
		}
	}
	if (cursor != argument_count ||
		cursor > std::numeric_limits<std::uint32_t>::max()) return false;
	offsets->push_back(static_cast<std::uint32_t>(cursor));
	return true;
}

ScopeId SemanticAnalyzer::BindFunctionTemplateArguments(
	const FunctionTemplatePattern& pattern,
	const std::vector<TemplateArgument>& arguments,
	const std::vector<std::uint32_t>& parameter_offsets)
{
	if (parameter_offsets.size() != pattern.parameters.size() + 1 ||
		parameter_offsets.empty() || parameter_offsets.front() != 0 ||
		parameter_offsets.back() != arguments.size())
		throw std::logic_error("function template argument offsets are invalid");
	const ScopeId template_scope = NewScope(pattern.lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(pattern.lexical_scope));
	for (std::size_t parameter = 0;
		parameter < pattern.parameters.size(); ++parameter)
	{
		const std::size_t first = parameter_offsets[parameter];
		const std::size_t last = parameter_offsets[parameter + 1];
		if (first > last || last > arguments.size())
			throw std::logic_error(
				"function template argument offset range is invalid");
		if (pattern.parameters[parameter].pack)
			BindTemplateArgumentPack(template_scope,
				pattern.parameters[parameter], arguments, first, last);
		else
		{
			if (last != first + 1)
				throw std::logic_error(
					"fixed function template parameter has invalid arity");
			BindTemplateArgument(template_scope, pattern.parameters[parameter],
				arguments[first]);
		}
	}
	return template_scope;
}

void SemanticAnalyzer::UpgradeFunctionTemplateSpecializations(
	std::size_t index)
{
	if (index >= function_templates_.size())
		throw std::logic_error("invalid function template upgrade");
	const FunctionTemplatePattern& pattern = function_templates_[index];
	const std::vector<BindingId> specializations =
		pattern.specialization_bindings;
	const std::vector<TemplateArgument> specialization_arguments =
		pattern.specialization_arguments;
	const std::vector<std::uint32_t> specialization_offsets =
		pattern.specialization_argument_offsets;
	const std::vector<std::uint32_t> parameter_offsets =
		pattern.specialization_parameter_offsets;
	if (specialization_offsets.size() != specializations.size())
		throw std::logic_error(
			"function template specialization offsets are invalid");
	for (std::size_t specialization = 0;
		specialization < specializations.size(); ++specialization)
	{
		const std::size_t first = specialization_offsets[specialization];
		const std::size_t last = specialization + 1 < specialization_offsets.size() ?
			specialization_offsets[specialization + 1] :
			specialization_arguments.size();
		if (first > last || last > specialization_arguments.size())
			throw std::logic_error(
				"function template specialization argument range is invalid");
		std::vector<TemplateArgument> arguments;
		arguments.reserve(last - first);
		for (std::size_t p = first; p < last; ++p)
			arguments.push_back(specialization_arguments[p]);
		std::vector<std::uint32_t> partitions;
		if (FunctionTemplateNeedsPartitionIdentity(pattern.parameters))
		{
			const std::size_t partition_first = specialization *
				(pattern.parameters.size() + 1);
			if (partition_first > parameter_offsets.size() ||
				pattern.parameters.size() + 1 >
					parameter_offsets.size() - partition_first)
				throw std::logic_error(
					"function template specialization partitions are invalid");
			partitions.assign(parameter_offsets.begin() + partition_first,
				parameter_offsets.begin() + partition_first +
					pattern.parameters.size() + 1);
		}
		else if (!BuildFunctionTemplateArgumentOffsets(
			pattern.parameters, arguments.size(), &partitions))
			throw std::logic_error(
				"function template specialization shape is invalid");
		const ScopeId template_scope =
			BindFunctionTemplateArguments(pattern, arguments, partitions);
		const SpecInfo spec = BuildSpecifiers(pattern.specifiers, template_scope,
			std::string(), true);
		const EntityId member_owner = program_->EntityForScope(pattern.owner);
		const EntityId previous_class = current_class_context_;
		if (member_owner != kNoEntity) current_class_context_ = member_owner;
		const DeclaratorInfo parsed = BuildDeclarator(pattern.declarator,
			spec.type, template_scope, false,
			member_owner != kNoEntity &&
				spec.storage_class != STORAGE_CLASS_STATIC);
		current_class_context_ = previous_class;
		FunctionInfo& function = GetMutableFunction(
			specializations[specialization]);
		if (function.type != parsed.type)
			throw std::runtime_error(
				"function template definition does not match declaration");
		function.parameters = parsed.parameters;
		function.constexpr_function =
			function.constexpr_function || spec.is_constexpr;
		function.defined = true;
		function.deferred = true;
		function.definition_body = pattern.definition_body;
		function.lexical_scope = template_scope;
	}
}

BindingId SemanticAnalyzer::InstantiateFunctionTemplate(std::size_t index,
	const std::vector<TypeId>& arguments)
{
	if (index >= function_templates_.size())
		throw std::logic_error("invalid PA12 function template pattern");
	const FunctionTemplatePattern& pattern = function_templates_[index];
	std::vector<std::uint32_t> offsets;
	if (!BuildFunctionTemplateArgumentOffsets(
		pattern.parameters, arguments.size(), &offsets)) return kNoBinding;
	std::vector<TemplateArgument> canonical;
	canonical.reserve(arguments.size());
	for (std::size_t parameter = 0;
		parameter < pattern.parameters.size(); ++parameter)
		for (std::size_t argument = offsets[parameter];
			argument < offsets[parameter + 1]; ++argument)
		{
			if (pattern.parameters[parameter].kind != TEMPLATE_ARGUMENT_TYPE)
				return kNoBinding;
			canonical.push_back(TemplateArgument(
				TEMPLATE_ARGUMENT_TYPE, arguments[argument]));
		}
	return InstantiateFunctionTemplate(index, canonical, offsets);
}

BindingId SemanticAnalyzer::InstantiateFunctionTemplate(std::size_t index,
	const std::vector<TemplateArgument>& arguments)
{
	if (index >= function_templates_.size())
		throw std::logic_error("invalid PA12 function template pattern");
	const FunctionTemplatePattern& pattern = function_templates_[index];
	std::vector<std::uint32_t> offsets;
	if (!BuildFunctionTemplateArgumentOffsets(
		pattern.parameters, arguments.size(), &offsets)) return kNoBinding;
	return InstantiateFunctionTemplate(index, arguments, offsets);
}

BindingId SemanticAnalyzer::InstantiateFunctionTemplate(std::size_t index,
	const std::vector<TemplateArgument>& arguments,
	const std::vector<std::uint32_t>& parameter_offsets)
{
	if (index >= function_templates_.size())
		throw std::logic_error("invalid PA12 function template pattern");
	const FunctionTemplatePattern& pattern = function_templates_[index];
	if (parameter_offsets.size() != pattern.parameters.size() + 1 ||
		parameter_offsets.empty() || parameter_offsets.front() != 0 ||
		parameter_offsets.back() != arguments.size()) return kNoBinding;
	for (std::size_t parameter = 0;
		parameter < pattern.parameters.size(); ++parameter)
	{
		const std::size_t first = parameter_offsets[parameter];
		const std::size_t last = parameter_offsets[parameter + 1];
		if (first > last || last > arguments.size() ||
			(!pattern.parameters[parameter].pack && last != first + 1))
			return kNoBinding;
		for (std::size_t argument = first; argument < last; ++argument)
			if (arguments[argument].kind != pattern.parameters[parameter].kind ||
				arguments[argument].IsDependent()) return kNoBinding;
	}
	++template_specialization_requests_;
	const std::vector<std::uint32_t> no_identity_offsets;
	const std::vector<std::uint32_t>& identity_offsets =
		FunctionTemplateNeedsPartitionIdentity(pattern.parameters) ?
			parameter_offsets : no_identity_offsets;
	const TemplateSpecializationKey request_key(
		index, arguments, identity_offsets);
	BindingId old = template_instantiations_.Find(request_key);
	if (old != kNoBinding)
	{
		++template_specialization_cache_hits_;
		return old;
	}

	std::vector<TemplateArgument> completed = arguments;
	ScopeId default_scope = kNoScope;
	for (std::size_t parameter_index = 0;
		parameter_index < pattern.parameters.size(); ++parameter_index)
	{
		const TemplateParameter& parameter =
			pattern.parameters[parameter_index];
		const std::size_t first = parameter_offsets[parameter_index];
		const std::size_t last = parameter_offsets[parameter_index + 1];
		if (parameter.pack)
		{
			for (std::size_t argument = first; argument < last; ++argument)
				if (completed[argument].type == kNoType) return kNoBinding;
			if (default_scope != kNoScope)
				BindTemplateArgumentPack(default_scope, parameter,
					completed, first, last);
			continue;
		}
		TemplateArgument& argument = completed[first];
		if (argument.type != kNoType)
		{
			if (default_scope != kNoScope)
				BindTemplateArgument(default_scope, parameter, argument);
			continue;
		}
		if (parameter.default_argument == kNoNode) return kNoBinding;
		if (default_scope == kNoScope)
		{
			default_scope = NewScope(pattern.lexical_scope,
				SCOPE_TEMPLATE_PARAMETERS, 0,
				ScopePrefixId(pattern.lexical_scope));
			for (std::size_t prior = 0; prior < parameter_index; ++prior)
			{
				const std::size_t prior_first = parameter_offsets[prior];
				const std::size_t prior_last = parameter_offsets[prior + 1];
				if (pattern.parameters[prior].pack)
					BindTemplateArgumentPack(default_scope,
						pattern.parameters[prior], completed,
						prior_first, prior_last);
				else BindTemplateArgument(default_scope,
					pattern.parameters[prior], completed[prior_first]);
			}
		}
		NodeId source = FirstSemanticChild(parameter.default_argument);
		if (source == kNoNode)
			throw std::runtime_error(
				"empty function template default argument");
		if (parameter.kind == TEMPLATE_ARGUMENT_TYPE)
		{
			NodeId type_id = arena_->IsTag(source, "type-id") ? source :
				FindChild(source, "type-id");
			if (type_id == kNoNode) return kNoBinding;
			argument.type = BuildTypeId(type_id, default_scope);
			if (argument.type == kNoType) return kNoBinding;
		}
		else
		{
			argument.type = ResolveTemplateParameterType(
				parameter, default_scope);
			++constant_expression_required_depth_;
			ExpressionInfo value;
			try
			{
				value = AnalyzeExpression(
					source, default_scope, argument.type);
			}
			catch (...)
			{
				--constant_expression_required_depth_;
				throw;
			}
			--constant_expression_required_depth_;
			if (!value.constant || !IsIntegral(value.type, true))
				throw std::runtime_error(
					"default non-type function template argument is not constant");
			argument.value = NormalizeIntegralConstant(
				argument.type, value.value);
		}
		BindTemplateArgument(default_scope, parameter, argument);
	}
	const TemplateSpecializationKey cache_key(
		index, completed, identity_offsets);
	if (completed != arguments)
	{
		old = template_instantiations_.Find(cache_key);
		if (old != kNoBinding)
		{
			++template_specialization_cache_hits_;
			template_instantiations_.Insert(request_key, old);
			return old;
		}
	}

	const ScopeId template_scope =
		BindFunctionTemplateArguments(pattern, completed, parameter_offsets);
	const SpecInfo spec = BuildSpecifiers(pattern.specifiers, template_scope,
		std::string(), true);
	const EntityId member_owner = program_->EntityForScope(pattern.owner);
	const EntityId previous_class = current_class_context_;
	if (member_owner != kNoEntity) current_class_context_ = member_owner;
	const DeclaratorInfo parsed = BuildDeclarator(pattern.declarator,
		spec.type, template_scope, false,
		member_owner != kNoEntity &&
			spec.storage_class != STORAGE_CLASS_STATIC);
	current_class_context_ = previous_class;
	const bool nonthrowing = pattern.dependent_exception_specification ?
		IsNonthrowing(pattern.declarator, template_scope) :
		pattern.nonthrowing;
	const BindingId binding = DeclareFunction(pattern.owner, pattern.name,
		parsed.type, parsed.parameters, pattern.defined, true,
		member_owner == kNoEntity ? spec.storage_class : STORAGE_CLASS_NONE,
		pattern.language_linkage, nonthrowing);
	BindingRecord& binding_record = program_->bindings[binding];
	if (member_owner != kNoEntity)
	{
		binding_record.member_owner = member_owner;
		binding_record.access = pattern.member_access;
		binding_record.static_member_function =
			spec.storage_class == STORAGE_CLASS_STATIC ||
			binding_record.operator_kind == OPERATOR_NEW ||
			binding_record.operator_kind == OPERATOR_NEW_ARRAY ||
			binding_record.operator_kind == OPERATOR_DELETE ||
			binding_record.operator_kind == OPERATOR_DELETE_ARRAY;
		FunctionInfo& member_function = GetMutableFunction(binding);
		if (!binding_record.static_member_function)
			member_function.member_owner =
				program_->entities[member_owner].type;
		RegisterClassMemberFunction(member_owner, binding);
	}
	if (binding_record.template_argument_count == 0)
		StoreTemplateArguments(completed,
			&binding_record.template_argument_begin,
			&binding_record.template_argument_count);
	ValidateFunctionRefQualifier(binding);
	ValidateNonmemberOperator(binding);
	FunctionInfo& function = GetMutableFunction(binding);
	function.constexpr_function =
		function.constexpr_function || spec.is_constexpr;
	function.template_pattern = static_cast<std::uint32_t>(index);
	function.parameter_pack_name = FunctionParameterPackName(pattern.declarator);
	function.deferred = true;
	function.definition_in_class = member_owner != kNoEntity &&
		pattern.lexical_scope == pattern.owner;
	function.lexical_scope = template_scope;
	if (pattern.defined) function.definition_body = pattern.definition_body;
	template_instantiations_.Insert(cache_key, binding);
	if (completed != arguments)
		template_instantiations_.Insert(request_key, binding);
	FunctionTemplatePattern& mutable_pattern = function_templates_[index];
	if (mutable_pattern.specialization_arguments.size() >
		std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error(
			"too many function template specialization arguments");
	mutable_pattern.specialization_bindings.push_back(binding);
	mutable_pattern.specialization_argument_offsets.push_back(
		static_cast<std::uint32_t>(
			mutable_pattern.specialization_arguments.size()));
	mutable_pattern.specialization_arguments.insert(
		mutable_pattern.specialization_arguments.end(),
		completed.begin(), completed.end());
	if (!identity_offsets.empty())
	{
		if (mutable_pattern.specialization_parameter_offsets.size() >
			std::numeric_limits<std::uint32_t>::max() - parameter_offsets.size())
			throw std::runtime_error(
				"too many function template specialization partitions");
		mutable_pattern.specialization_parameter_offsets.insert(
			mutable_pattern.specialization_parameter_offsets.end(),
			parameter_offsets.begin(), parameter_offsets.end());
	}
	return binding;
}

}
}
