#include "pa12_semantic_detail.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::FunctionalCastPrecedesFunctions(
	const std::string& spelling, ScopeId scope, TypeId cast_type,
	NodeId syntax, const std::vector<BindingId>& candidates)
{
	if (cast_type == kNoType) return false;
	if (candidates.empty()) return true;
	for (std::size_t i = 0; i < candidates.size(); ++i)
		if (GetFunction(candidates[i]).member_owner != kNoType)
			return false;
	const NodeId structure = syntax == kNoNode ? kNoNode :
		FindChild(syntax, "structured-type-name");
	const LookupResult type_lookup = structure != kNoNode ?
		LookupStructuredName(syntax, scope, LOOKUP_TYPE) :
		LookupSpelling(scope, spelling, LOOKUP_TYPE);
	ScopeId type_owner = kNoScope;
	if (type_lookup.type_declaration != kNoBinding)
		type_owner = program_->bindings[type_lookup.type_declaration].owner;
	std::size_t type_distance = 0;
	ScopeId current = scope;
	while (current != kNoScope && current != type_owner)
	{
		current = program_->ParentScope(current);
		++type_distance;
	}
	if (current != type_owner) return false;
	std::size_t function_distance =
		std::numeric_limits<std::size_t>::max();
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		std::size_t distance = 0;
		ScopeId owner = scope;
		const ScopeId candidate_owner =
			program_->bindings[candidates[i]].owner;
		while (owner != kNoScope && owner != candidate_owner)
		{
			owner = program_->ParentScope(owner);
			++distance;
		}
		if (owner == candidate_owner && distance < function_distance)
			function_distance = distance;
	}
	return type_distance < function_distance;
}

bool SemanticAnalyzer::AnalyzeRetainedNamedCall(
	NodeId name_syntax, const std::string& spelling, ScopeId scope,
	const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>& arguments, TypeId target,
	ExpressionInfo* result)
{
	EntityId naming_class = kNoEntity;
	std::vector<BindingId> candidates =
		FunctionCallCandidates(scope, spelling, &naming_class, name_syntax);
	NamePath structured_base;
	std::vector<TypeId> explicit_arguments;
	const bool structured_explicit = ParseExplicitTemplateArguments(
		name_syntax, scope, &structured_base, &explicit_arguments);
	if (!structured_explicit)
		structured_base = StructuredNamePath(name_syntax);
	const bool has_templates = !structured_base.Empty() ?
		!FindFunctionTemplates(scope, structured_base).empty() :
		!FindFunctionTemplates(scope, spelling).empty();
	if (has_templates)
	{
		DeduceFunctionTemplates(scope, spelling, arguments, name_syntax);
		candidates = FunctionCallCandidates(
			scope, spelling, &naming_class, name_syntax);
	}
	if (candidates.empty()) return false;
	bool has_member_candidate = false;
	for (std::size_t i = 0; i < candidates.size(); ++i)
		if (GetFunction(candidates[i]).member_owner != kNoType)
			has_member_candidate = true;
	ExpressionInfo implicit_object;
	const ExpressionInfo* object = 0;
	if (has_member_candidate)
	{
		const NameId this_name = program_->names.Intern("this");
		const LookupResult found_this =
			program_->LookupName(scope, this_name, LOOKUP_ORDINARY);
		if (found_this.ordinary == kNoBinding) return false;
		const BindingRecord& this_binding =
			program_->bindings[found_this.ordinary];
		implicit_object.type = EffectiveType(this_binding.type);
		implicit_object.category = VALUE_LVALUE;
		implicit_object.binding = found_this.ordinary;
		implicit_object.node = MakeDump(DUMP_ID_EXPRESSION,
			implicit_object.type, VALUE_LVALUE, this_name,
			found_this.ordinary);
		object = &implicit_object;
		++expression_count_;
	}
	ObjectConversionFact object_conversion;
	std::vector<CallConversionFact> argument_conversions;
	const BindingId selected = SelectOverload(scope, argument_syntax,
		arguments, candidates, object, object ? &object_conversion : 0,
		&argument_conversions);
	*result = BuildResolvedCall(selected, scope, argument_syntax,
		arguments, object, target, naming_class,
		object ? &object_conversion : 0, &argument_conversions);
	return true;
}

bool SemanticAnalyzer::AnalyzeAmbiguousCallStatement(
	NodeId node, ScopeId scope, std::uint32_t output_parent)
{
	const NodeId specifiers = FindChild(node, "decl-specifier-seq");
	const NodeId list = FindChild(node, "init-declarator-list");
	const std::uint32_t specifier_edge = specifiers == kNoNode ? kNoEdge :
		arena_->FirstEdge(specifiers);
	const std::uint32_t item_edge = list == kNoNode ? kNoEdge :
		arena_->FirstEdge(list);
	if (specifier_edge == kNoEdge ||
		arena_->NextEdge(specifier_edge) != kNoEdge || item_edge == kNoEdge ||
		arena_->NextEdge(item_edge) != kNoEdge)
		return false;
	const NodeId specifier_node = arena_->EdgeChild(specifier_edge);
	const NodeId specifier_structure = FirstSemanticChild(specifier_node);
	if (specifier_structure != kNoNode &&
		!arena_->IsTag(specifier_structure, "structured-type-name"))
		return false;
	const std::string spelling = PayloadSource(specifier_node);
	const NodeId item = arena_->EdgeChild(item_edge);
	if (FindChild(item, "initializer") != kNoNode) return false;
	const NodeId declarator = FindChild(item, "declarator");
	const NodeId nested = declarator == kNoNode ? kNoNode :
		FindChild(declarator, "nested-declarator");
	const NodeId argument_declarator = nested == kNoNode ? kNoNode :
		FirstSemanticChild(nested);
	const NodeId argument_name = argument_declarator == kNoNode ? kNoNode :
		FindChild(argument_declarator, "identifier");
	if (argument_name == kNoNode ||
		FindChild(argument_declarator, "parameter-clause") != kNoNode ||
		FindChild(argument_declarator, "array-suffix") != kNoNode)
		return false;
	const NamePath structured = StructuredNamePath(specifier_node);
	if (FunctionCallCandidates(scope, spelling, 0, specifier_node).empty() &&
		(structured.Empty() ? FindFunctionTemplates(scope, spelling) :
		 FindFunctionTemplates(scope, structured)).empty())
		return false;
	const std::string argument_spelling = PayloadSource(argument_name);
	std::vector<NodeId> argument_syntax(1, argument_name);
	std::vector<ExpressionInfo> arguments(1,
		AnalyzeNamedValue(argument_spelling, scope, kNoType, argument_name));
	ExpressionInfo call;
	if (!AnalyzeRetainedNamedCall(specifier_node, spelling, scope,
		argument_syntax,
		arguments, kNoType, &call))
		return false;
	call = MaterializeDiscardedClassResult(call);
	const std::uint32_t statement = MakeDump(DUMP_EXPRESSION_STATEMENT);
	dump_.Add(output_parent, statement);
	dump_.Add(statement, call.node);
	AppendFullExpressionDestructionActions(call.node, statement);
	return true;
}

bool SemanticAnalyzer::AnalyzeAmbiguousDirectInitializer(
	NodeId node, ScopeId scope, std::uint32_t output_parent)
{
	const NodeId specifiers = FindChild(node, "decl-specifier-seq");
	const NodeId list = FindChild(node, "init-declarator-list");
	const std::uint32_t item_edge = list == kNoNode ? kNoEdge :
		arena_->FirstEdge(list);
	if (specifiers == kNoNode || item_edge == kNoEdge ||
		arena_->NextEdge(item_edge) != kNoEdge)
		return false;
	const NodeId item = arena_->EdgeChild(item_edge);
	if (FindChild(item, "initializer") != kNoNode) return false;
	const NodeId declarator = FindChild(item, "declarator");
	const NodeId clause = declarator == kNoNode ? kNoNode :
		FindChild(declarator, "parameter-clause");
	const NameId variable_name = declarator == kNoNode ? 0 :
		DeclaratorName(declarator);
	const std::uint32_t parameter_edge = clause == kNoNode ? kNoEdge :
		arena_->FirstEdge(clause);
	if (variable_name == 0 || parameter_edge == kNoEdge ||
		arena_->NextEdge(parameter_edge) != kNoEdge)
		return false;
	const NodeId provisional = arena_->EdgeChild(parameter_edge);
	if (!arena_->IsTag(provisional, "parameter-declaration")) return false;
	const NodeId call_specifiers =
		FindChild(provisional, "decl-specifier-seq");
	const std::uint32_t call_edge = call_specifiers == kNoNode ? kNoEdge :
		arena_->FirstEdge(call_specifiers);
	if (call_edge == kNoEdge || arena_->NextEdge(call_edge) != kNoEdge)
		return false;
	const NodeId call_name = arena_->EdgeChild(call_edge);
	const std::string call_spelling = PayloadSource(call_name);
	const NamePath structured = StructuredNamePath(call_name);
	const NodeId call_declarator = FindChild(provisional, "declarator");
	if (call_declarator == kNoNode)
	{
		const LookupResult value = structured.Empty() ?
			LookupSpelling(scope, call_spelling, LOOKUP_ORDINARY) :
			LookupStructuredName(call_name, scope, LOOKUP_ORDINARY);
		if (value.ordinary == kNoBinding) return false;
		const SpecInfo spec = BuildSpecifiers(specifiers, scope,
			program_->names.Get(variable_name), true);
		const LookupResult occupied =
			program_->LookupDirect(scope, variable_name, LOOKUP_ORDINARY);
		if (occupied.ordinary != kNoBinding)
			throw std::runtime_error("duplicate local variable");
		const BindingId binding = program_->AddBinding(scope, BIND_VARIABLE,
			variable_name, spec.type);
		PublishVariableDeclarationFacts(binding, scope, variable_name,
			spec.type, spec, true);
		std::vector<NodeId> argument_syntax(1, call_name);
		std::vector<ExpressionInfo> arguments(1,
			AnalyzeNamedValue(call_spelling, scope, kNoType, call_name));
		ExpressionInfo initializer;
		if (IsClassObjectType(spec.type))
		{
			initializer.node = BuildConstructorAction(spec.type, scope,
				argument_syntax, false, false, false, true, kNoNode,
				&arguments);
			initializer.type = spec.type;
			initializer.category = VALUE_PRVALUE;
		}
		else initializer = ApplyTarget(arguments[0], spec.type);
		const std::uint32_t owner = MakeDump(DUMP_SIMPLE_DECLARATION);
		const std::uint32_t variable = MakeDump(DUMP_VARIABLE, spec.type,
			VALUE_NONE, variable_name, binding);
		dump_.Add(variable, initializer.node);
		dump_.Add(owner, variable);
		dump_.Add(output_parent, owner);
		AddLifetimeObligation(scope, binding, spec.type);
		AppendFullExpressionDestructionActions(initializer.node, owner);
		return true;
	}
	if (FunctionCallCandidates(scope, call_spelling, 0, call_name).empty() &&
		(structured.Empty() ? FindFunctionTemplates(scope, call_spelling) :
		 FindFunctionTemplates(scope, structured)).empty())
		return false;
	const NodeId call_clause = call_declarator == kNoNode ? kNoNode :
		FindChild(call_declarator, "parameter-clause");
	const std::uint32_t argument_edge = call_clause == kNoNode ? kNoEdge :
		arena_->FirstEdge(call_clause);
	if (argument_edge == kNoEdge || arena_->NextEdge(argument_edge) != kNoEdge)
		return false;
	const NodeId argument_parameter = arena_->EdgeChild(argument_edge);
	const NodeId argument_specifiers =
		FindChild(argument_parameter, "decl-specifier-seq");
	const std::uint32_t argument_name_edge =
		argument_specifiers == kNoNode ? kNoEdge :
		arena_->FirstEdge(argument_specifiers);
	if (argument_name_edge == kNoEdge ||
		arena_->NextEdge(argument_name_edge) != kNoEdge ||
		FindChild(argument_parameter, "declarator") != kNoNode)
		return false;
	const NodeId argument_name = arena_->EdgeChild(argument_name_edge);
	const std::string argument_spelling = PayloadSource(argument_name);
	const SpecInfo spec = BuildSpecifiers(specifiers, scope,
		program_->names.Get(variable_name), true);
	std::vector<NodeId> argument_syntax(1, argument_name);
	std::vector<ExpressionInfo> arguments(1,
		AnalyzeNamedValue(argument_spelling, scope, kNoType, argument_name));
	ExpressionInfo call;
	if (!AnalyzeRetainedNamedCall(call_name, call_spelling, scope,
		argument_syntax,
		arguments, kNoType, &call))
		return false;
	const LookupResult occupied =
		program_->LookupDirect(scope, variable_name, LOOKUP_ORDINARY);
	if (occupied.ordinary != kNoBinding)
		throw std::runtime_error("duplicate local variable");
	const BindingId binding = program_->AddBinding(scope, BIND_VARIABLE,
		variable_name, spec.type);
	PublishVariableDeclarationFacts(binding, scope, variable_name,
		spec.type, spec, true);
	ExpressionInfo initializer;
	if (IsClassObjectType(spec.type))
	{
		initializer.node = BuildClassValueConstructorAction(
			spec.type, call, false, true);
		initializer.type = spec.type;
		initializer.category = VALUE_PRVALUE;
	}
	else initializer = ApplyTarget(call, spec.type);
	const std::uint32_t owner = MakeDump(DUMP_SIMPLE_DECLARATION);
	const std::uint32_t variable = MakeDump(DUMP_VARIABLE, spec.type,
		VALUE_NONE, variable_name, binding);
	dump_.Add(variable, initializer.node);
	dump_.Add(owner, variable);
	dump_.Add(output_parent, owner);
	AddLifetimeObligation(scope, binding, spec.type);
	AppendFullExpressionDestructionActions(initializer.node, owner);
	return true;
}

}
}
