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
	else found = program_->LookupName(
		scope, path.Last(), LOOKUP_FUNCTION_TEMPLATE);
	std::vector<ScopeId> result;
	result.reserve(found.FunctionTemplateOwnerCount());
	for (std::size_t i = 0; i < found.FunctionTemplateOwnerCount(); ++i)
		result.push_back(found.FunctionTemplateOwnerAt(i));
	return result;
}

ScopeId SemanticAnalyzer::BindFunctionTemplateArguments(
	const FunctionTemplatePattern& pattern,
	const std::vector<TypeId>& arguments)
{
	const ScopeId template_scope = NewScope(pattern.lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(pattern.lexical_scope));
	for (std::size_t i = 0; i < arguments.size(); ++i)
		program_->AddBinding(template_scope, BIND_TYPE_ALIAS,
			pattern.type_parameters[i], arguments[i]);
	return template_scope;
}

void SemanticAnalyzer::UpgradeFunctionTemplateSpecializations(
	std::size_t index)
{
	if (index >= function_templates_.size())
		throw std::logic_error("invalid function template upgrade");
	const FunctionTemplatePattern& pattern = function_templates_[index];
	const std::size_t parameter_count = pattern.type_parameters.size();
	const std::vector<BindingId> specializations =
		pattern.specialization_bindings;
	const std::vector<TypeId> specialization_arguments =
		pattern.specialization_arguments;
	for (std::size_t specialization = 0;
		specialization < specializations.size(); ++specialization)
	{
		std::vector<TypeId> arguments;
		arguments.reserve(parameter_count);
		for (std::size_t p = 0; p < parameter_count; ++p)
			arguments.push_back(specialization_arguments[
				specialization * parameter_count + p]);
		const ScopeId template_scope =
			BindFunctionTemplateArguments(pattern, arguments);
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
	if (arguments.size() != pattern.type_parameters.size()) return kNoBinding;
	++template_specialization_requests_;
	const TemplateSpecializationKey request_key(index, arguments);
	BindingId old = template_instantiations_.Find(request_key);
	if (old != kNoBinding)
	{
		++template_specialization_cache_hits_;
		return old;
	}

	std::vector<TypeId> completed = arguments;
	ScopeId default_scope = kNoScope;
	for (std::size_t i = 0; i < completed.size(); ++i)
	{
		if (completed[i] != kNoType)
		{
			if (default_scope != kNoScope)
				program_->AddBinding(default_scope, BIND_TYPE_ALIAS,
					pattern.type_parameters[i], completed[i]);
			continue;
		}
		if (i >= pattern.default_arguments.size() ||
			pattern.default_arguments[i] == kNoNode)
			return kNoBinding;
		if (default_scope == kNoScope)
		{
			default_scope = NewScope(pattern.lexical_scope,
				SCOPE_TEMPLATE_PARAMETERS, 0,
				ScopePrefixId(pattern.lexical_scope));
			for (std::size_t prior = 0; prior < i; ++prior)
				program_->AddBinding(default_scope, BIND_TYPE_ALIAS,
					pattern.type_parameters[prior], completed[prior]);
		}
		NodeId type_id = FindChild(pattern.default_arguments[i], "type-id");
		if (type_id == kNoNode)
			type_id = FirstSemanticChild(pattern.default_arguments[i]);
		if (type_id == kNoNode)
			throw std::runtime_error(
				"empty function template default argument");
		completed[i] = BuildTypeId(type_id, default_scope);
		if (completed[i] == kNoType) return kNoBinding;
		program_->AddBinding(default_scope, BIND_TYPE_ALIAS,
			pattern.type_parameters[i], completed[i]);
	}
	const TemplateSpecializationKey cache_key(index, completed);
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
		BindFunctionTemplateArguments(pattern, completed);
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
	const BindingId binding = DeclareFunction(pattern.owner, pattern.name,
		parsed.type, parsed.parameters, pattern.defined, true,
		member_owner == kNoEntity ? spec.storage_class : STORAGE_CLASS_NONE,
		pattern.language_linkage, pattern.nonthrowing);
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
	{
		if (program_->template_arguments.size() >
			std::numeric_limits<std::uint32_t>::max() - completed.size())
			throw std::runtime_error("too many function template arguments");
		binding_record.template_argument_begin = static_cast<std::uint32_t>(
			program_->template_arguments.size());
		binding_record.template_argument_count =
			static_cast<std::uint32_t>(completed.size());
		program_->template_arguments.insert(
			program_->template_arguments.end(),
			completed.begin(), completed.end());
	}
	ValidateFunctionRefQualifier(binding);
	ValidateNonmemberOperator(binding);
	FunctionInfo& function = GetMutableFunction(binding);
	function.template_pattern = static_cast<std::uint32_t>(index);
	function.deferred = true;
	function.lexical_scope = template_scope;
	if (pattern.defined) function.definition_body = pattern.definition_body;
	template_instantiations_.Insert(cache_key, binding);
	if (completed != arguments)
		template_instantiations_.Insert(request_key, binding);
	FunctionTemplatePattern& mutable_pattern = function_templates_[index];
	mutable_pattern.specialization_bindings.push_back(binding);
	mutable_pattern.specialization_arguments.insert(
		mutable_pattern.specialization_arguments.end(),
		completed.begin(), completed.end());
	return binding;
}

}
}
