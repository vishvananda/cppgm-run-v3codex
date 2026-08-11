#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

ScopeId SemanticAnalyzer::ResolveStructuredDeclaratorOwner(
	NodeId declarator, ScopeId scope)
{
	const NamePath path = DeclaratorNamePath(declarator);
	const NodeId structure = DeclaratorNameStructure(declarator);
	if (structure == kNoNode || (!path.global && path.Size() <= 1))
		return kNoScope;
	ScopeId owner = kNoScope;
	(void)LookupStructuredName(
		structure, scope, LOOKUP_ORDINARY, &owner);
	return owner;
}

void SemanticAnalyzer::MergeFunctionRedeclarationParameters(
	FunctionInfo* function, const std::vector<ParameterInfo>& parameters,
	bool definition)
{
	if (!function)
		throw std::logic_error("missing function redeclaration fact");
	const EntityId member =
		program_->bindings[function->binding].member_owner;
	const bool definition_owns_parameters = definition &&
		member != kNoEntity &&
		program_->entities[member].explicit_template_specialization;
	if (!definition_owns_parameters || parameters.empty())
	{
		if (function->parameters.empty() && !parameters.empty())
			function->parameters = parameters;
		return;
	}
	std::vector<ParameterInfo> replacement = parameters;
	if (replacement.size() == function->parameters.size())
		for (std::size_t i = 0; i < replacement.size(); ++i)
			if (replacement[i].default_argument == kNoNode)
			{
				replacement[i].default_argument =
					function->parameters[i].default_argument;
				replacement[i].default_scope =
					function->parameters[i].default_scope;
			}
	function->parameters.swap(replacement);
}

}
}
