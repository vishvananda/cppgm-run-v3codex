#include "pa12_semantic_detail.h"

#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::AnalyzeSimpleFunctionDeclaration(NodeId item,
	NodeId declarator, ScopeId syntax_scope, ScopeId declaration_scope,
	std::uint32_t output_parent, const NamePath& declared_path,
	const SpecInfo& spec, DeclaratorInfo parsed)
{
	if (FindChild(declarator, "virt-specifier") != kNoNode)
		throw std::runtime_error(
			"virt-specifier is only allowed in a class definition");
	if (spec.thread_local_storage)
		throw std::runtime_error("thread_local function");
	const EntityId function_owner =
		program_->EntityForScope(declaration_scope);
	if (spec.is_constexpr)
		parsed.type = ApplyConstexprDeclaredFunctionType(parsed.type,
			declaration_scope, parsed.name, function_owner);
	if (spec.is_constexpr && parsed.placeholder_return_kind ==
		PLACEHOLDER_DECLARATOR_NONE)
		ValidateConstexprCallableType(parsed.type, false);
	const BindingId function = DeclareFunction(declaration_scope, parsed.name,
		parsed.type, parsed.parameters, false, false, spec.storage_class,
		current_language_linkage_, IsNonthrowing(declarator, syntax_scope));
	ConfigureFunctionExceptionSpecification(function, declarator, syntax_scope);
	ConfigurePlaceholderFunctionReturn(function, parsed, spec.placeholder_cv);
	ApplyFunctionAsmLabel(declarator, function);
	ApplyFunctionAbiTagAttributes(item, function);
	PublishInlineFunctionFacts(
		function, spec.inline_specifier || spec.is_constexpr);
	GetMutableFunction(function).constexpr_function =
		GetFunction(function).constexpr_function || spec.is_constexpr;
	ValidateFunctionRefQualifier(function);
	ValidateNonmemberOperator(function);
	const NodeId function_initializer = FindChild(item, "initializer");
	ConfigureAssignmentSpecialMember(function, function_initializer,
		!declared_path.global && declared_path.Size() <= 1);
	const NodeId special = function_initializer == kNoNode ? kNoNode :
		FindChild(function_initializer, "special-initializer");
	if (special != kNoNode && arena_->Payload(special) == "delete")
	{
		GetMutableFunction(function).deleted_function = true;
		return;
	}
	const std::uint32_t declaration = MakeDump(DUMP_FUNCTION_DECLARATION,
		parsed.type, VALUE_NONE, GetFunction(function).display_name, function);
	dump_.Add(output_parent, declaration);
}

void SemanticAnalyzer::QueueFunctionDefinitionValidation(BindingId binding)
{
	if (binding == kNoBinding) return;
	binding = program_->bindings[binding].canonical;
	EnsureFunctionExceptionSpecification(binding);
	DemandClassTemplateMemberDefinitions(
		program_->bindings[binding].member_owner);
	if (binding >= function_fact_by_binding_.size() ||
		function_fact_by_binding_[binding] == kNoDumpEdge) return;
	FunctionInfo& function = GetMutableFunction(binding);
	if (!function.deferred || function.demand_state != 0) return;
	function.demand_state = 1;
	demanded_functions_.push_back(binding);
	++demand_worklist_pushes_;
}

}
}
