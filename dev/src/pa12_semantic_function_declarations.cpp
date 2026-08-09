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
	if (spec.is_constexpr)
		ValidateConstexprCallableType(parsed.type, false);
	const BindingId function = DeclareFunction(declaration_scope, parsed.name,
		parsed.type, parsed.parameters, false, false, spec.storage_class,
		current_language_linkage_, IsNonthrowing(declarator, syntax_scope));
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
	if (special != kNoNode && arena_->Payload(special) == "delete") return;
	const std::uint32_t declaration = MakeDump(DUMP_FUNCTION_DECLARATION,
		parsed.type, VALUE_NONE, GetFunction(function).display_name, function);
	dump_.Add(output_parent, declaration);
}

}
}
