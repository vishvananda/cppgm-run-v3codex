#include "semantic/analysis/analyzer.h"

#include <algorithm>
#include <stdexcept>

namespace cppgm
{
namespace semantic
{

void Analyzer::AnalyzeConversionFunction(NodeId node, ScopeId scope,
	TypeId owner_type, AccessKind access)
{
	const NodeId declarator = FindChild(node, ::cppgm::syntax::STAG_DECLARATOR);
	const NodeId target_node = declarator == kNoNode ? kNoNode :
		FindChild(declarator, ::cppgm::syntax::STAG_CONVERSION_TYPE_ID);
	if (target_node == kNoNode)
		throw std::logic_error("conversion function has no target type");
	const TypeId target = BuildTypeId(target_node, scope);
	bool explicit_specifier = false;
	bool constexpr_specifier = false;
	bool inline_specifier = false;
	const NodeId specifiers = FindChild(node, ::cppgm::syntax::STAG_MEMBER_SPECIFIERS);
	if (specifiers != kNoNode)
		for (std::uint32_t edge = arena_->FirstEdge(specifiers);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const std::string value = PayloadSource(arena_->EdgeChild(edge));
			if (value == "explicit") explicit_specifier = true;
			if (value == "constexpr") constexpr_specifier = true;
			if (value == "static")
				throw std::runtime_error("conversion function cannot be static");
			if (value == "inline") inline_specifier = true;
		}
	const EntityId entity = EntityOf(owner_type);
	DeclaratorInfo parsed = BuildDeclarator(
		declarator, target, scope, false, true);
	if (!program_->types.IsFunction(parsed.type) || !parsed.parameters.empty() ||
		program_->types.Get(parsed.type).child != target)
		throw std::runtime_error("invalid conversion function declarator");
	if (constexpr_specifier)
		parsed.type = ApplyConstexprMemberFunctionType(
			parsed.type, entity, false);
	const NodeId initializer = FindChild(node, ::cppgm::syntax::STAG_INITIALIZER);
	const NodeId special = initializer == kNoNode ? kNoNode :
		FindChild(initializer, ::cppgm::syntax::STAG_SPECIAL_INITIALIZER);
	if (special != kNoNode && arena_->Payload(special) == "default")
		throw std::runtime_error("conversion function cannot be defaulted");
	const bool deleted = special != kNoNode &&
		arena_->Payload(special) == "delete";
	const bool definition = arena_->IsTag(node, ::cppgm::syntax::STAG_SPECIAL_MEMBER_DEFINITION);
	const NameId conversion_name = DeclaratorNamePath(declarator).Last();
	const BindingId function = DeclareFunction(scope, conversion_name,
		parsed.type, parsed.parameters, definition, false, STORAGE_CLASS_NONE,
		current_language_linkage_,
		IsNonthrowing(declarator, parsed.parameter_scope));
	ConfigureFunctionExceptionSpecification(
		function, declarator, parsed.parameter_scope);
	BindingRecord& binding = program_->bindings[function];
	binding.member_owner = entity;
	binding.access = access;
	binding.inline_function = binding.inline_function || definition ||
		inline_specifier;
	binding.conversion_function = true;
	binding.conversion_target = target;
	FunctionInfo& info = GetMutableFunction(function);
	info.member_owner = owner_type;
	info.conversion_function = true;
	info.conversion_target = target;
	info.explicit_conversion = info.explicit_conversion || explicit_specifier;
	info.constexpr_function = info.constexpr_function || constexpr_specifier;
	info.deleted_special_member = info.deleted_special_member || deleted;
	info.deferred = !info.deleted_special_member;
	if (definition) {
		info.definition_body = FunctionDefinitionPart(node, "compound-statement");
		info.function_try_block = FindChild(node, ::cppgm::syntax::STAG_FUNCTION_TRY_BLOCK);
	}
	if (info.constexpr_function)
	{
		if (IsClassTemplateSpecializationContext(entity) &&
			!IsConstexprCallableType(info.type, false))
			info.constexpr_function = false;
		else ValidateConstexprCallableType(info.type, false);
	}
	PublishInlineFunctionFacts(
		function, definition || info.constexpr_function || binding.inline_function);
	ValidateFunctionRefQualifier(function);
	if (entity_conversion_functions_.size() <= entity)
		entity_conversion_functions_.resize(
			static_cast<std::size_t>(entity) + 1);
	std::vector<BindingId>& functions = entity_conversion_functions_[entity];
	if (std::find(functions.begin(), functions.end(), function) ==
		functions.end()) functions.push_back(function);
}

}
}
