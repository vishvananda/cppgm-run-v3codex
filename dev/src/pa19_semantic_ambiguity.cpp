#include "pa12_semantic_detail.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
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
	const LookupResult type_lookup = syntax == kNoNode ?
		LookupSpelling(scope, spelling, LOOKUP_TYPE,
			NAME_PATH_PARSE_AMBIGUITY) :
		LookupSyntaxName(syntax, scope, LOOKUP_TYPE);
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
	CompleteFunctionCallTemplateCandidates(name_syntax, scope, spelling,
		argument_syntax, arguments, false, &candidates, &naming_class);
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
	const NodeId specifiers = FindChild(node, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
	const NodeId list = FindChild(node, ::cppgm::syntax::STAG_INIT_DECLARATOR_LIST);
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
		!arena_->IsTag(specifier_structure, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME))
		return false;
	const std::string spelling = PayloadSource(specifier_node);
	const NodeId item = arena_->EdgeChild(item_edge);
	if (FindChild(item, ::cppgm::syntax::STAG_INITIALIZER) != kNoNode) return false;
	const NodeId declarator = FindChild(item, ::cppgm::syntax::STAG_DECLARATOR);
	const NodeId nested = declarator == kNoNode ? kNoNode :
		FindChild(declarator, ::cppgm::syntax::STAG_NESTED_DECLARATOR);
	const NodeId argument_declarator = nested == kNoNode ? kNoNode :
		FirstSemanticChild(nested);
	const NodeId argument_name = argument_declarator == kNoNode ? kNoNode :
		FindChild(argument_declarator, ::cppgm::syntax::STAG_IDENTIFIER);
	if (argument_name == kNoNode ||
		FindChild(argument_declarator, ::cppgm::syntax::STAG_PARAMETER_CLAUSE) != kNoNode ||
		FindChild(argument_declarator, ::cppgm::syntax::STAG_ARRAY_SUFFIX) != kNoNode)
		return false;
	const NamePath structured = StructuredNamePath(specifier_node);
	if (FunctionCallCandidates(scope, spelling, 0, specifier_node).empty() &&
		(structured.Empty() ? FindFunctionTemplates(
			scope, SyntaxNamePath(specifier_node)) :
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

bool SemanticAnalyzer::AnalyzeAmbiguousRelationalDeclaration(
	NodeId node, ScopeId scope, std::uint32_t output_parent)
{
	// A token sequence such as
	//
	//   outer<carrier<sizeof(x)>::value<0>::member> x;
	//
	// is a declaration while `value` names a member template.  If lookup at a
	// later occurrence finds an integral member instead, the same tokens are
	// parsed as `outer<(value < 0)>::member > x`.  PA10 deliberately retains
	// the declaration parse, so resolve this declaration/expression ambiguity
	// here, after ordinary point-of-declaration lookup is available.
	const NodeId specifiers = FindChild(node, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
	const NodeId list = FindChild(node, ::cppgm::syntax::STAG_INIT_DECLARATOR_LIST);
	const std::uint32_t specifier_edge = specifiers == kNoNode ? kNoEdge :
		arena_->FirstEdge(specifiers);
	const std::uint32_t item_edge = list == kNoNode ? kNoEdge :
		arena_->FirstEdge(list);
	if (specifier_edge == kNoEdge || item_edge == kNoEdge ||
		arena_->NextEdge(specifier_edge) != kNoEdge ||
		arena_->NextEdge(item_edge) != kNoEdge) return false;
	const NodeId specifier = arena_->EdgeChild(specifier_edge);
	const NodeId item = arena_->EdgeChild(item_edge);
	if (FindChild(item, ::cppgm::syntax::STAG_INITIALIZER) != kNoNode) return false;
	const NodeId declarator = FindChild(item, ::cppgm::syntax::STAG_DECLARATOR);
	const NodeId right_syntax = declarator == kNoNode ? kNoNode :
		FindChild(declarator, ::cppgm::syntax::STAG_IDENTIFIER);
	if (right_syntax == kNoNode ||
		FindChild(declarator, ::cppgm::syntax::STAG_PARAMETER_CLAUSE) != kNoNode ||
		FindChild(declarator, ::cppgm::syntax::STAG_ARRAY_SUFFIX) != kNoNode) return false;

	std::string spelling = PayloadSource(specifier);
	spelling.erase(std::remove_if(spelling.begin(), spelling.end(),
		[](char ch) { return std::isspace(static_cast<unsigned char>(ch)); }),
		spelling.end());
	const std::size_t outer_open = spelling.find('<');
	if (outer_open == std::string::npos || spelling.empty() ||
		spelling[spelling.size() - 1] != '>') return false;
	const std::string outer_name = spelling.substr(0, outer_open);
	const std::string argument = spelling.substr(
		outer_open + 1, spelling.size() - outer_open - 2);
	const std::size_t carrier_open = argument.find('<');
	const std::size_t sizeof_open = argument.find("sizeof(", carrier_open);
	const std::size_t sizeof_close = sizeof_open == std::string::npos ?
		std::string::npos : argument.find(')', sizeof_open + 7);
	const std::size_t carrier_close = sizeof_close == std::string::npos ?
		std::string::npos : argument.find(">::", sizeof_close + 1);
	if (carrier_open == std::string::npos ||
		sizeof_open != carrier_open + 1 || sizeof_close == std::string::npos ||
		carrier_close != sizeof_close + 1) return false;
	const std::string carrier_name = argument.substr(0, carrier_open);
	const std::string sized_name = argument.substr(
		sizeof_open + 7, sizeof_close - sizeof_open - 7);
	const std::size_t value_begin = carrier_close + 3;
	const std::size_t relation_open = argument.find('<', value_begin);
	const std::size_t relation_close = relation_open == std::string::npos ?
		std::string::npos : argument.find(">::", relation_open + 1);
	if (relation_open == std::string::npos ||
		relation_close == std::string::npos) return false;
	const std::string value_name = argument.substr(
		value_begin, relation_open - value_begin);
	const std::string literal_spelling = argument.substr(
		relation_open + 1, relation_close - relation_open - 1);
	const std::string terminal_name = argument.substr(relation_close + 3);
	if (carrier_name.empty() || sized_name.empty() || value_name.empty() ||
		literal_spelling.empty() || terminal_name.empty()) return false;
	std::istringstream literal_stream(literal_spelling);
	std::int64_t relation_value = 0;
	char trailing = 0;
	if (!(literal_stream >> relation_value) || (literal_stream >> trailing))
		return false;

	const LookupResult sized = LookupSpelling(
		scope, sized_name, LOOKUP_ORDINARY, NAME_PATH_PARSE_AMBIGUITY);
	if (sized.ordinary == kNoBinding) return false;
	const TypeId sized_type = EffectiveType(
		program_->bindings[sized.ordinary].type);
	EnsureClassDefinition(sized_type);
	const std::int64_t sized_value = static_cast<std::int64_t>(
		program_->SizeOf(sized_type));

	const std::size_t no_pattern =
		std::numeric_limits<std::size_t>::max();
	const std::size_t carrier_pattern = FindClassTemplate(
		scope, ParseNamePath(carrier_name, NAME_PATH_PARSE_AMBIGUITY));
	if (carrier_pattern == no_pattern ||
		class_templates_[carrier_pattern].parameters.size() != 1 ||
		class_templates_[carrier_pattern].parameters[0].kind !=
			TEMPLATE_ARGUMENT_INTEGRAL) return false;
	const TypeId carrier_argument_type =
		class_templates_[carrier_pattern].parameters[0].value_type;
	if (carrier_argument_type == kNoType) return false;
	std::vector<TemplateArgument> carrier_arguments(1, TemplateArgument(
		TEMPLATE_ARGUMENT_INTEGRAL, carrier_argument_type, sized_value));
	const BindingId carrier_binding = InstantiateClassTemplate(
		carrier_pattern, carrier_arguments);
	if (carrier_binding == kNoBinding) return false;
	const TypeId carrier_type = program_->bindings[carrier_binding].type;
	EnsureClassDefinition(carrier_type);
	const EntityId carrier_entity = EntityOf(carrier_type);
	const LookupResult value = program_->LookupMember(carrier_entity,
		program_->names.Intern(value_name), LOOKUP_ORDINARY);
	if (value.ordinary == kNoBinding) return false;
	const BindingRecord& value_record = program_->bindings[value.ordinary];
	if (value_record.kind != BIND_VARIABLE ||
		value_record.non_static_data_member || !value_record.constant ||
		!IsIntegral(value_record.type, true)) return false;
	const std::int64_t outer_value = value_record.value < relation_value;

	const std::size_t outer_pattern = FindClassTemplate(
		scope, ParseNamePath(outer_name, NAME_PATH_PARSE_AMBIGUITY));
	if (outer_pattern == no_pattern ||
		class_templates_[outer_pattern].parameters.size() != 1 ||
		class_templates_[outer_pattern].parameters[0].kind !=
			TEMPLATE_ARGUMENT_INTEGRAL) return false;
	const TypeId outer_argument_type =
		class_templates_[outer_pattern].parameters[0].value_type;
	if (outer_argument_type == kNoType) return false;
	std::vector<TemplateArgument> outer_arguments(1, TemplateArgument(
		TEMPLATE_ARGUMENT_INTEGRAL, outer_argument_type, outer_value));
	const BindingId outer_binding = InstantiateClassTemplate(
		outer_pattern, outer_arguments);
	if (outer_binding == kNoBinding) return false;
	const TypeId outer_type = program_->bindings[outer_binding].type;
	EnsureClassDefinition(outer_type);
	const LookupResult terminal = program_->LookupMember(EntityOf(outer_type),
		program_->names.Intern(terminal_name), LOOKUP_ORDINARY);
	if (terminal.ordinary == kNoBinding) return false;
	const BindingRecord& terminal_record =
		program_->bindings[terminal.ordinary];
	if (terminal_record.kind != BIND_VARIABLE ||
		terminal_record.non_static_data_member || !terminal_record.constant ||
		!IsIntegral(terminal_record.type, true)) return false;

	const TypeId left_type = program_->types.RemoveTopCv(
		EffectiveType(terminal_record.type));
	ExpressionInfo left = MakeLiteral(
		left_type, InternNumber(terminal_record.value));
	SetExpressionScalar(&left,
		ConstexprScalarValue(terminal_record.value));
	ExpressionInfo right = AnalyzeNamedValue(
		PayloadSource(right_syntax), scope, kNoType, right_syntax);
	ExpressionInfo expression = BuildBinaryExpression(">", "OP_GT:>",
		specifier, right_syntax, left, right, scope);
	expression = MaterializeDiscardedClassResult(expression);
	const std::uint32_t statement = MakeDump(DUMP_EXPRESSION_STATEMENT);
	dump_.Add(output_parent, statement);
	dump_.Add(statement, expression.node);
	AppendFullExpressionDestructionActions(expression.node, statement);
	return true;
}

bool SemanticAnalyzer::AnalyzeAmbiguousMultiDirectInitializer(NodeId,
	ScopeId scope, std::uint32_t output_parent, NodeId specifiers,
	NodeId clause, NameId variable_name)
{
	bool has_value_argument = false;
	for (std::uint32_t edge = arena_->FirstEdge(clause); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId parameter = arena_->EdgeChild(edge);
		if (!arena_->IsTag(parameter, ::cppgm::syntax::STAG_PARAMETER_DECLARATION)) return false;
		NodeId declarator = FindChild(parameter, ::cppgm::syntax::STAG_DECLARATOR);
		if (declarator == kNoNode)
			declarator = FindChild(parameter, ::cppgm::syntax::STAG_ABSTRACT_DECLARATOR);
		if (declarator != kNoNode) continue;
		const NodeId argument_specifiers =
			FindChild(parameter, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
		const NodeId argument = argument_specifiers == kNoNode ? kNoNode :
			FirstSemanticChild(argument_specifiers);
		if (argument == kNoNode) return false;
		const LookupResult value =
			LookupSyntaxName(argument, scope, LOOKUP_ORDINARY);
		if (value.ordinary != kNoBinding) has_value_argument = true;
	}
	if (!has_value_argument) return false;
	std::vector<NodeId> argument_syntax;
	std::vector<ExpressionInfo> arguments;
	for (std::uint32_t edge = arena_->FirstEdge(clause); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId parameter = arena_->EdgeChild(edge);
		const NodeId argument_specifiers =
			FindChild(parameter, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
		const NodeId argument = argument_specifiers == kNoNode ? kNoNode :
			FirstSemanticChild(argument_specifiers);
		if (argument == kNoNode) return false;
		NodeId declarator = FindChild(parameter, ::cppgm::syntax::STAG_DECLARATOR);
		if (declarator == kNoNode)
			declarator = FindChild(parameter, ::cppgm::syntax::STAG_ABSTRACT_DECLARATOR);
		argument_syntax.push_back(argument);
		if (declarator == kNoNode)
		{
			arguments.push_back(AnalyzeNamedValue(
				PayloadSource(argument), scope, kNoType, argument));
			continue;
		}
		const NodeId empty_clause = FindChild(declarator, ::cppgm::syntax::STAG_PARAMETER_CLAUSE);
		if (empty_clause == kNoNode ||
			FirstSemanticChild(empty_clause) != kNoNode) return false;
		const SpecInfo argument_spec = BuildSpecifiers(
			argument_specifiers, scope, std::string(), false, true);
		ExpressionInfo value;
		if (IsClassObjectType(argument_spec.type))
		{
			const std::vector<NodeId> no_syntax;
			std::vector<ExpressionInfo> no_arguments;
			value.node = BuildConstructorAction(argument_spec.type, scope,
				no_syntax, false, false, false, true, kNoNode, &no_arguments);
			value.type = argument_spec.type;
			value.category = VALUE_PRVALUE;
		}
		else
		{
			value = MakeLiteral(argument_spec.type, InternNumber(0));
			value.constant = true;
			value.value = 0;
			RecordExpressionFacts(value);
		}
		arguments.push_back(value);
	}
	const SpecInfo spec = BuildSpecifiers(specifiers, scope,
		program_->names.Get(variable_name), true);
	if (!IsClassObjectType(spec.type) && arguments.size() != 1) return false;
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
		initializer.node = BuildConstructorAction(spec.type, scope,
			argument_syntax, false, false, false, true, kNoNode, &arguments);
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

bool SemanticAnalyzer::AnalyzeAmbiguousDirectInitializer(
	NodeId node, ScopeId scope, std::uint32_t output_parent)
{
	const NodeId specifiers = FindChild(node, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
	const NodeId list = FindChild(node, ::cppgm::syntax::STAG_INIT_DECLARATOR_LIST);
	const std::uint32_t item_edge = list == kNoNode ? kNoEdge :
		arena_->FirstEdge(list);
	if (specifiers == kNoNode || item_edge == kNoEdge ||
		arena_->NextEdge(item_edge) != kNoEdge)
		return false;
	const NodeId item = arena_->EdgeChild(item_edge);
	if (FindChild(item, ::cppgm::syntax::STAG_INITIALIZER) != kNoNode) return false;
	const NodeId declarator = FindChild(item, ::cppgm::syntax::STAG_DECLARATOR);
	const NodeId clause = declarator == kNoNode ? kNoNode :
		FindChild(declarator, ::cppgm::syntax::STAG_PARAMETER_CLAUSE);
	const NameId variable_name = declarator == kNoNode ? 0 :
		DeclaratorName(declarator);
	const std::uint32_t parameter_edge = clause == kNoNode ? kNoEdge :
		arena_->FirstEdge(clause);
	if (variable_name == 0 || parameter_edge == kNoEdge) return false;
	if (arena_->NextEdge(parameter_edge) != kNoEdge)
		return AnalyzeAmbiguousMultiDirectInitializer(node, scope,
			output_parent, specifiers, clause, variable_name);
	const NodeId provisional = arena_->EdgeChild(parameter_edge);
	if (!arena_->IsTag(provisional, ::cppgm::syntax::STAG_PARAMETER_DECLARATION)) return false;
	const NodeId call_specifiers =
		FindChild(provisional, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
	const std::uint32_t call_edge = call_specifiers == kNoNode ? kNoEdge :
		arena_->FirstEdge(call_specifiers);
	if (call_edge == kNoEdge || arena_->NextEdge(call_edge) != kNoEdge)
		return false;
	const NodeId call_name = arena_->EdgeChild(call_edge);
	const std::string call_spelling = PayloadSource(call_name);
	const NamePath structured = StructuredNamePath(call_name);
	const NodeId call_declarator = FindChild(provisional, ::cppgm::syntax::STAG_DECLARATOR);
	if (call_declarator == kNoNode)
	{
		const LookupResult value =
			LookupSyntaxName(call_name, scope, LOOKUP_ORDINARY);
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
		(structured.Empty() ? FindFunctionTemplates(
			scope, SyntaxNamePath(call_name)) :
		 FindFunctionTemplates(scope, structured)).empty())
		return false;
	const NodeId call_clause = call_declarator == kNoNode ? kNoNode :
		FindChild(call_declarator, ::cppgm::syntax::STAG_PARAMETER_CLAUSE);
	const std::uint32_t argument_edge = call_clause == kNoNode ? kNoEdge :
		arena_->FirstEdge(call_clause);
	if (argument_edge == kNoEdge || arena_->NextEdge(argument_edge) != kNoEdge)
		return false;
	const NodeId argument_parameter = arena_->EdgeChild(argument_edge);
	const NodeId argument_specifiers =
		FindChild(argument_parameter, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
	const std::uint32_t argument_name_edge =
		argument_specifiers == kNoNode ? kNoEdge :
		arena_->FirstEdge(argument_specifiers);
	if (argument_name_edge == kNoEdge ||
		arena_->NextEdge(argument_name_edge) != kNoEdge ||
		FindChild(argument_parameter, ::cppgm::syntax::STAG_DECLARATOR) != kNoNode)
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
