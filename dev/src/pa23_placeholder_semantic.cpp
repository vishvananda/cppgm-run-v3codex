#include "pa12_semantic_detail.h"

#include <algorithm>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::ApplyPlaceholderDeclaratorOperator(
	const std::string& operation, DeclaratorInfo* declarator) const
{
	if (declarator->placeholder_return_kind != PLACEHOLDER_DECLARATOR_VALUE)
		throw std::runtime_error(
			"compound placeholder function result declarator");
	if (operation == "*")
		declarator->placeholder_return_kind = PLACEHOLDER_DECLARATOR_POINTER;
	else if (operation == "&")
		declarator->placeholder_return_kind =
			PLACEHOLDER_DECLARATOR_LVALUE_REFERENCE;
	else if (operation == "&&")
		declarator->placeholder_return_kind =
			PLACEHOLDER_DECLARATOR_RVALUE_REFERENCE;
	else throw std::runtime_error(
		"unsupported placeholder function result declarator");
}

DeclaratorInfo SemanticAnalyzer::BuildVariableDeclarator(
	NodeId item, NodeId declarator, const SpecInfo& spec, ScopeId scope,
	bool local, ExpressionInfo* prepared_initializer)
{
	const bool function =
		FindChild(declarator, "parameter-clause") != kNoNode;
	if (!spec.placeholder_auto || function)
		return BuildDeclarator(declarator, spec.type, scope,
			spec.placeholder_auto);
	if (!prepared_initializer)
		throw std::logic_error(
			"placeholder variable deduction has no result owner");
	NodeId initializer = FindChild(item, "initializer");
	if (initializer == kNoNode)
		throw std::runtime_error("placeholder variable requires initializer");
	NodeId expression = FirstSemanticChild(initializer);
	while (expression != kNoNode && arena_->IsTag(expression, "initializer"))
		expression = FirstSemanticChild(expression);
	if (expression != kNoNode && arena_->IsTag(expression, "paren-initializer"))
	{
		const NodeId first = FirstSemanticChild(expression);
		const std::uint32_t first_edge = arena_->FirstEdge(expression);
		if (first == kNoNode || arena_->NextEdge(first_edge) != kNoEdge)
			throw std::runtime_error(
				"placeholder direct-initializer requires one expression");
		expression = first;
	}
	if (expression != kNoNode && arena_->IsTag(expression, "braced-init-list"))
	{
		std::vector<NodeId> syntax;
		for (std::uint32_t edge = arena_->FirstEdge(expression);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
			syntax.push_back(arena_->EdgeChild(edge));
		std::vector<ExpressionInfo> values;
		std::vector<NodeId> expanded_syntax = syntax;
		if (!ExpandCallArgumentPacks(
			syntax, scope, &expanded_syntax, &values))
		{
			values.reserve(syntax.size());
			for (std::size_t i = 0; i < syntax.size(); ++i)
				values.push_back(AnalyzeExpression(syntax[i], scope));
		}
		if (values.empty())
			throw std::runtime_error("empty placeholder initializer-list");
		TypeId element = program_->types.RemoveTopCv(Decay(values[0].type));
		for (std::size_t i = 1; i < values.size(); ++i)
			if (program_->types.RemoveTopCv(Decay(values[i].type)) != element)
				throw std::runtime_error(
					"placeholder initializer-list has inconsistent element types");
		const std::size_t pattern =
			FindClassTemplate(scope, "std::initializer_list");
		if (pattern >= class_templates_.size())
			throw std::runtime_error("std::initializer_list is not declared");
		const BindingId specialization = InstantiateClassTemplate(
			pattern, std::vector<TypeId>(1, element));
		if (specialization == kNoBinding)
			throw std::runtime_error(
				"unable to form std::initializer_list specialization");
		TypeId list_type = program_->bindings[specialization].type;
		ConfigureInitializerListSpecialization(list_type);
		DeclaratorInfo parsed = BuildDeclarator(declarator,
			program_->types.Qualify(list_type, spec.placeholder_cv), scope);
		*prepared_initializer = BuildInitializerListFromValues(
			parsed.type, values);
		prepared_initializer->type = parsed.type;
		return parsed;
	}
	if (expression == kNoNode)
		throw std::runtime_error(
			"placeholder list deduction is outside the PA23 boundary");
	const bool require_constant = spec.is_constexpr || !local ||
		spec.storage_class == STORAGE_CLASS_STATIC ||
		(spec.placeholder_cv & CV_CONST) != 0;
	const bool preserve_recipe = !local && spec.is_constexpr &&
		arena_->HasDescendantTag(initializer, "conditional-expression");
	if (require_constant)
	{
		++constant_expression_required_depth_;
		++constant_initializer_required_depth_;
		if (local) ++local_constant_initializer_depth_;
	}
	if (preserve_recipe) ++preserve_constant_initializer_recipe_depth_;
	bool context_active = true;
	const auto release_context = [this, require_constant, local,
		preserve_recipe, &context_active]()
	{
		if (!context_active) return;
		if (require_constant)
		{
			if (local) --local_constant_initializer_depth_;
			--constant_initializer_required_depth_;
			--constant_expression_required_depth_;
		}
		if (preserve_recipe) --preserve_constant_initializer_recipe_depth_;
		context_active = false;
	};
	ExpressionInfo value;
	DeclaratorInfo parsed;
	try
	{
		value = AnalyzeExpression(expression, scope);
		std::string pointer_operator;
		for (std::uint32_t edge = arena_->FirstEdge(declarator);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (!arena_->IsTag(child, "ptr-operator")) continue;
			if (!pointer_operator.empty())
				throw std::runtime_error(
					"compound placeholder declarator is outside the PA23 boundary");
			pointer_operator = PayloadSource(child);
		}
		TypeId base = EffectiveType(value.type);
		if (pointer_operator.empty()) base = Decay(value.type);
		else if (pointer_operator == "&")
		{
			if (value.category != VALUE_LVALUE)
				throw std::runtime_error("auto& requires an lvalue initializer");
		}
		else if (pointer_operator == "&&")
		{
			if (value.category == VALUE_LVALUE &&
				spec.placeholder_cv == CV_NONE)
				base = program_->types.Reference(TYPE_LVALUE_REFERENCE, base);
		}
		else if (pointer_operator == "*")
		{
			const TypeRecord& pointer = program_->types.Get(Decay(value.type));
			if (pointer.kind != TYPE_POINTER)
				throw std::runtime_error("auto* requires a pointer initializer");
			base = pointer.child;
		}
		else throw std::runtime_error(
			"unsupported placeholder pointer operator in PA23");
		base = program_->types.Qualify(base, spec.placeholder_cv);
		parsed = BuildDeclarator(declarator, base, scope);
		if (spec.is_constexpr)
			parsed.type = program_->types.Qualify(parsed.type, CV_CONST);
		value = ApplyTarget(value, parsed.type);
		const TypeRecord& declared = program_->types.Get(parsed.type);
		if (declared.kind != TYPE_LVALUE_REFERENCE &&
			declared.kind != TYPE_RVALUE_REFERENCE &&
			IsClassObjectType(parsed.type) && value.category == VALUE_PRVALUE &&
			dump_.nodes[value.node].kind == DUMP_CALL_EXPRESSION &&
			!dump_.nodes[value.node].explicit_user_conversion_call &&
			program_->types.RemoveTopCv(EffectiveType(value.type)) ==
				program_->types.RemoveTopCv(EffectiveType(parsed.type)))
		{
			const BindingId selected =
				ValidateClassValueConstruction(parsed.type, value);
			value = BuildDirectClassValueTransfer(
				value, parsed.type, selected);
		}
		value = FinalizeVariableInitializer(
			value, parsed.type, EntityOf(parsed.type), local);
	}
	catch (...)
	{
		release_context();
		throw;
	}
	release_context();
	*prepared_initializer = value;
	return parsed;
}

DeclaratorInfo SemanticAnalyzer::BuildMemberDeclarator(NodeId item,
	NodeId declarator, const SpecInfo& spec, ScopeId scope, bool definition,
	ExpressionInfo* prepared_initializer)
{
	const bool function = definition ||
		FindChild(declarator, "parameter-clause") != kNoNode;
	DeclaratorInfo parsed = spec.placeholder_auto && !function ?
		BuildVariableDeclarator(item, declarator, spec, scope, false,
			prepared_initializer) :
		BuildDeclarator(declarator, spec.type, scope, spec.placeholder_auto,
			spec.storage_class != STORAGE_CLASS_STATIC && function);
	parsed.placeholder_return_cv = spec.placeholder_cv;
	return parsed;
}
void SemanticAnalyzer::ConfigurePlaceholderFunctionReturn(BindingId function,
	const DeclaratorInfo& declarator, std::uint8_t placeholder_cv)
{
	if (declarator.placeholder_return_kind == PLACEHOLDER_DECLARATOR_NONE)
		return;
	FunctionInfo& fact = GetMutableFunction(function);
	if (fact.placeholder_return_kind != PLACEHOLDER_DECLARATOR_NONE &&
		(fact.placeholder_return_kind != declarator.placeholder_return_kind ||
		 fact.placeholder_return_cv != placeholder_cv))
		throw std::runtime_error(
			"conflicting placeholder function return declarator");
	fact.placeholder_return_kind = declarator.placeholder_return_kind;
	fact.placeholder_return_cv = placeholder_cv;
}

TypeId SemanticAnalyzer::DeducePlaceholderFunctionReturnType(
	const FunctionInfo& function, const ExpressionInfo* expression)
{
	if (!expression)
	{
		if (function.placeholder_return_kind != PLACEHOLDER_DECLARATOR_VALUE)
			throw std::runtime_error(
				"reference or pointer placeholder function returns no value");
		return program_->types.Fundamental(FUND_VOID);
	}
	TypeId base = EffectiveType(expression->type);
	switch (function.placeholder_return_kind)
	{
	case PLACEHOLDER_DECLARATOR_VALUE:
		base = Decay(expression->type);
		if (function.placeholder_return_cv != CV_NONE)
			base = program_->types.Qualify(
				base, function.placeholder_return_cv);
		return base;
	case PLACEHOLDER_DECLARATOR_POINTER:
	{
		const TypeId pointer = Decay(expression->type);
		const TypeRecord& shape = program_->types.Get(pointer);
		if (shape.kind != TYPE_POINTER)
			throw std::runtime_error(
				"auto* function return requires a pointer expression");
		base = shape.child;
		if (function.placeholder_return_cv != CV_NONE)
			base = program_->types.Qualify(
				base, function.placeholder_return_cv);
		return program_->types.Pointer(base);
	}
	case PLACEHOLDER_DECLARATOR_LVALUE_REFERENCE:
		if (expression->category != VALUE_LVALUE)
			throw std::runtime_error(
				"auto& function return requires an lvalue expression");
		if (function.placeholder_return_cv != CV_NONE)
			base = program_->types.Qualify(
				base, function.placeholder_return_cv);
		return program_->types.Reference(TYPE_LVALUE_REFERENCE, base);
	case PLACEHOLDER_DECLARATOR_RVALUE_REFERENCE:
		if (function.placeholder_return_cv != CV_NONE)
			base = program_->types.Qualify(
				base, function.placeholder_return_cv);
		return program_->types.Reference(
			expression->category == VALUE_LVALUE &&
				function.placeholder_return_cv == CV_NONE ?
				TYPE_LVALUE_REFERENCE :
				TYPE_RVALUE_REFERENCE, base);
	case PLACEHOLDER_DECLARATOR_NONE:
		break;
	}
	throw std::logic_error("placeholder return deduction has no placeholder");
}

void SemanticAnalyzer::PublishPlaceholderFunctionReturn(
	BindingId function, const ExpressionInfo* expression)
{
	function = program_->bindings[function].canonical;
	FunctionInfo& fact = GetMutableFunction(function);
	if (fact.placeholder_return_kind == PLACEHOLDER_DECLARATOR_NONE)
		throw std::logic_error(
			"placeholder result published for an ordinary function");
	const TypeId deduced =
		DeducePlaceholderFunctionReturnType(fact, expression);
	if (fact.placeholder_return_deduced)
	{
		if (fact.placeholder_return_type != deduced)
			throw std::runtime_error(
				"inconsistent placeholder function return types");
		current_return_type_ = fact.placeholder_return_type;
		return;
	}
	const TypeRecord old_type = program_->types.Get(fact.type);
	const TypeId* old_parameters = program_->types.Parameters(fact.type);
	std::vector<TypeId> parameters;
	if (old_type.parameter_count != 0)
		parameters.assign(old_parameters,
			old_parameters + old_type.parameter_count);
	const TypeId completed = program_->types.Function(deduced, parameters,
		old_type.variadic, old_type.cv, old_type.ref_qualifier);
	fact.type = completed;
	fact.placeholder_return_type = deduced;
	fact.placeholder_return_deduced = true;
	program_->bindings[function].type = completed;
	current_return_type_ = deduced;
}

void SemanticAnalyzer::CompletePlaceholderFunctionReturn(BindingId function)
{
	FunctionInfo& fact = GetMutableFunction(function);
	if (fact.placeholder_return_kind != PLACEHOLDER_DECLARATOR_NONE &&
		!fact.placeholder_return_deduced)
		PublishPlaceholderFunctionReturn(function, 0);
}

void SemanticAnalyzer::AnalyzeRetainedPlaceholderFunctionBody(
	BindingId function)
{
	function = program_->bindings[function].canonical;
	FunctionInfo& requested = GetMutableFunction(function);
	if (requested.placeholder_return_kind == PLACEHOLDER_DECLARATOR_NONE)
		return;
	if (requested.placeholder_body_state == PLACEHOLDER_BODY_SUCCEEDED ||
		requested.retained_definition_semantics != kNoDumpEdge)
	{
		requested.placeholder_body_state = PLACEHOLDER_BODY_SUCCEEDED;
		return;
	}
	if (requested.placeholder_body_state == PLACEHOLDER_BODY_IN_PROGRESS)
		throw std::runtime_error(
			"recursive placeholder function return deduction");
	if (requested.placeholder_body_state == PLACEHOLDER_BODY_FAILED)
		throw std::runtime_error(
			"placeholder function return deduction previously failed");
	if (!requested.defined || requested.definition_body == kNoNode)
		throw std::runtime_error(
			"placeholder function return requires a visible definition");
	requested.placeholder_body_state = PLACEHOLDER_BODY_IN_PROGRESS;

	const bool member = requested.member_owner != kNoType;
	const TypeId provisional_output = member ?
		AdaptMemberFunctionType(function) : requested.type;
	const std::uint32_t detached = MakeDump(DUMP_FUNCTION_DEFINITION,
		provisional_output, VALUE_NONE, requested.display_name, requested.binding);
	const ScopeId function_scope = NewScope(requested.lexical_scope,
		SCOPE_FUNCTION, program_->bindings[requested.binding].name,
		ScopePrefixId(requested.owner));
	BindFunctionParameterPackElement(
		function_scope, requested.parameter_pack_name, kNoBinding);
	BindingId this_binding = kNoBinding;
	if (member)
	{
		const TypeId this_type =
			program_->types.Parameters(provisional_output)[0];
		const NameId this_name = program_->names.Intern("this");
		this_binding = program_->AddBinding(function_scope,
			BIND_PARAMETER, this_name, this_type);
		dump_.Add(detached, MakeDump(DUMP_PARAMETER, this_type,
			VALUE_NONE, this_name, this_binding));
	}
	for (std::size_t i = 0; i < requested.parameters.size(); ++i)
	{
		const ParameterInfo& parameter = requested.parameters[i];
		const BindingId parameter_binding = program_->AddBinding(function_scope,
			BIND_PARAMETER, parameter.name, ParameterBindingType(parameter));
		BindFunctionParameterPackElement(
			function_scope, parameter.pack_name, parameter_binding);
		dump_.Add(detached, MakeDump(DUMP_PARAMETER,
			parameter.function_type, VALUE_NONE,
			parameter.name, parameter_binding));
		AddLifetimeObligation(function_scope, parameter_binding,
			parameter.function_type, false);
	}
	InstallLambdaCaptureBindings(function_scope, this_binding, requested);

	const TypeId previous_return = current_return_type_;
	const EntityId previous_class = current_class_context_;
	const BindingId previous_function = current_function_context_;
	current_return_type_ = requested.placeholder_return_deduced ?
		requested.placeholder_return_type : kNoType;
	current_class_context_ = requested.friend_of != kNoEntity ?
		requested.friend_of : program_->bindings[requested.binding].member_owner;
	current_function_context_ = function;
	BeginFunctionControlFlowFacts();
	try
	{
		AnalyzeCompound(requested.definition_body, function_scope, detached);
		FinishFunctionControlFlowFacts();
		CompletePlaceholderFunctionReturn(function);
		const FunctionInfo& completed = GetFunction(function);
		const bool declared_constexpr = completed.constexpr_function ||
			(completed.template_pattern < function_templates_.size() &&
			 function_templates_[completed.template_pattern].constexpr_specifier);
		if (declared_constexpr)
			ValidateConstexprCallableType(completed.type, false);
		FinalizeNamedReturnSlot(detached);
	}
	catch (...)
	{
		GetMutableFunction(function).placeholder_body_state =
			PLACEHOLDER_BODY_FAILED;
		current_return_type_ = previous_return;
		current_class_context_ = previous_class;
		current_function_context_ = previous_function;
		throw;
	}
	current_return_type_ = previous_return;
	current_class_context_ = previous_class;
	current_function_context_ = previous_function;
	FunctionInfo& completed = GetMutableFunction(function);
	completed.retained_definition_semantics = detached;
	if (completed.template_pattern < function_templates_.size() &&
		function_templates_[completed.template_pattern].constexpr_specifier)
		completed.constexpr_function = true;
	completed.placeholder_body_state = PLACEHOLDER_BODY_SUCCEEDED;
	dump_.nodes[detached].type = member ?
		AdaptMemberFunctionType(function) : completed.type;

	const EntityId member_entity = completed.member_owner == kNoType ?
		kNoEntity : EntityOf(completed.member_owner);
	if (member_entity != kNoEntity &&
		program_->entities[member_entity].lambda_closure)
	{
		// A non-generic lambda body is semantically completed even when the call
		// operator itself is never emitted.  Retain declarations for undefined
		// functions referenced by that detached body; lowering cannot discover
		// those dependencies by walking the translation-unit root.
		std::vector<std::uint32_t> pending(1, detached);
		std::vector<BindingId> published;
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const DumpNode record = dump_.nodes[current];
			if (record.kind == DUMP_CALL_EXPRESSION &&
				record.binding != kNoBinding)
			{
				const BindingId dependency =
					program_->bindings[record.binding].canonical;
				if (std::find(published.begin(), published.end(), dependency) ==
					published.end() &&
					dependency < function_fact_by_binding_.size() &&
					function_fact_by_binding_[dependency] != kNoDumpEdge &&
					!GetFunction(dependency).defined)
				{
					const FunctionInfo& referenced = GetFunction(dependency);
					const TypeId declaration_type = referenced.member_owner == kNoType ?
						referenced.type : AdaptMemberFunctionType(dependency);
					const std::uint32_t declaration = MakeDump(
						DUMP_FUNCTION_DECLARATION,
						declaration_type, VALUE_NONE,
						referenced.display_name, dependency);
					dump_.nodes[declaration].declaration_only = true;
					dump_.Add(root_, declaration);
					published.push_back(dependency);
				}
			}
			for (std::uint32_t edge = record.first_edge;
				edge != kNoDumpEdge; edge = dump_.edges[edge].next)
				pending.push_back(dump_.edges[edge].child);
		}
	}
}

void SemanticAnalyzer::ApplyConditionalClassConversion(
	ExpressionInfo* yes, ExpressionInfo* no)
{
	const TypeId yes_type = EffectiveType(yes->type);
	const TypeId no_type = EffectiveType(no->type);
	if (program_->types.RemoveTopCv(yes_type) ==
		program_->types.RemoveTopCv(no_type)) return;
	const bool yes_class = IsClassObjectType(yes_type);
	const bool no_class = IsClassObjectType(no_type);
	if (!yes_class && !no_class) return;
	if (yes_class != no_class)
	{
		const TypeId class_target = yes_class ? yes_type : no_type;
		const ExpressionInfo& nonclass = yes_class ? *no : *yes;
		const TypeId nonclass_type = yes_class ? no_type : yes_type;
		const TypeId nonclass_target = nonclass.category == VALUE_LVALUE ?
			program_->types.Reference(TYPE_LVALUE_REFERENCE, nonclass_type) :
			nonclass.category == VALUE_XVALUE ?
			program_->types.Reference(TYPE_RVALUE_REFERENCE, nonclass_type) :
			nonclass_type;
		const CallConversionFact class_to_nonclass = CallConversion(
			yes_class ? *yes : *no, nonclass_target, 0, 0);
		const CallConversionFact nonclass_to_class = CallConversion(
			nonclass, class_target, 0, 1);
		const bool convert_class =
			class_to_nonclass.rank != CONVERSION_INVALID;
		const bool convert_nonclass =
			nonclass_to_class.rank != CONVERSION_INVALID;
		if (convert_class == convert_nonclass) return;
		if (convert_class)
		{
			if (yes_class) *yes = ApplyCallArgument(
				*yes, nonclass_target, &class_to_nonclass);
			else *no = ApplyCallArgument(
				*no, nonclass_target, &class_to_nonclass);
		}
		else if (yes_class) *no = ApplyCallArgument(
			*no, class_target, &nonclass_to_class);
		else *yes = ApplyCallArgument(
			*yes, class_target, &nonclass_to_class);
		return;
	}
	const TypeId yes_target = yes->category == VALUE_LVALUE ?
		program_->types.Reference(TYPE_LVALUE_REFERENCE, yes_type) :
		yes->category == VALUE_XVALUE ?
		program_->types.Reference(TYPE_RVALUE_REFERENCE, yes_type) : yes_type;
	const TypeId no_target = no->category == VALUE_LVALUE ?
		program_->types.Reference(TYPE_LVALUE_REFERENCE, no_type) :
		no->category == VALUE_XVALUE ?
		program_->types.Reference(TYPE_RVALUE_REFERENCE, no_type) : no_type;
	const CallConversionFact yes_to_no =
		CallConversion(*yes, no_target, 0, 0);
	const CallConversionFact no_to_yes =
		CallConversion(*no, yes_target, 0, 1);
	const bool convert_yes = yes_to_no.rank != CONVERSION_INVALID;
	const bool convert_no = no_to_yes.rank != CONVERSION_INVALID;
	if (convert_yes == convert_no) return;
	if (convert_yes) *yes = ApplyCallArgument(*yes, no_target, &yes_to_no);
	else *no = ApplyCallArgument(*no, yes_target, &no_to_yes);
}

void SemanticAnalyzer::PublishStableFunctionTemplateResultAbi(
	const FunctionTemplatePattern& pattern, TypeId function_type,
	EntityId member_owner, BindingId canonical_binding)
{
	const TypeId result = program_->types.Get(function_type).child;
	const EntityId entity = EntityOf(result);
	if (entity == kNoEntity) return;
	const bool nontrivial_empty_result =
		program_->entities[entity].empty_class &&
		entity < class_special_members_.size() &&
		class_special_members_[entity].user_copy_constructor;
	const bool deferred_nonmember_result = member_owner == kNoEntity &&
		program_->entities[entity].has_user_provided_constructor &&
		(program_->entities[entity].empty_class ? nontrivial_empty_result :
		 pattern.deferred_result_formation);
	const bool dependent_move_result = pattern.result_type_dependent &&
		!program_->entities[entity].empty_class &&
		entity < class_special_members_.size() &&
		class_special_members_[entity].user_move_constructor;
	const bool conversion_result = pattern.conversion_template &&
		program_->entities[entity].template_argument_count == 0;
	if (deferred_nonmember_result || dependent_move_result || conversion_result)
		program_->bindings[canonical_binding].
			force_indirect_class_result_abi = true;
}

void SemanticAnalyzer::CompleteFunctionTemplatePlaceholderResult(
	std::size_t pattern, BindingId binding, EntityId member_owner)
{
	if (GetFunction(binding).placeholder_return_kind ==
		PLACEHOLDER_DECLARATOR_NONE) return;
	AnalyzeRetainedPlaceholderFunctionBody(binding);
	const BindingId canonical = program_->bindings[binding].canonical;
	PublishStableFunctionTemplateResultAbi(function_templates_[pattern],
		GetFunction(binding).type, member_owner, canonical);
}

bool SemanticAnalyzer::ShouldPreserveRuntimeInitializerRecipe(bool local,
	const SpecInfo& spec, TypeId type, NodeId initializer) const
{
	if (local || !spec.is_constexpr) return false;
	if (program_->types.IsReference(type))
		return arena_->HasDescendantTag(initializer, "conditional-expression");
	if (!IsClassObjectType(type)) return false;
	const NodeId paren = FindChild(initializer, "paren-initializer");
	const NodeId expression = paren == kNoNode ? initializer : paren;
	const NodeId call = FindChild(expression, "call-expression");
	const NodeId arguments = call == kNoNode ? kNoNode :
		FindChild(call, "argument-list");
	return arguments != kNoNode && arena_->FirstEdge(arguments) != kNoEdge;
}

bool SemanticAnalyzer::PreferMaterializedConstantDefinition(
	BindingId canonical) const
{
	if (canonical >= static_constant_initializers_by_binding_.size())
		return false;
	return static_constant_initializers_by_binding_[canonical].
		prefer_materialized_definition;
}

void SemanticAnalyzer::PublishInClassStaticDefinitionPolicy(
	BindingId binding, TypeId type, const SpecInfo& spec, NodeId initializer)
{
	const BindingId canonical = program_->bindings[binding].canonical;
	if (canonical >= static_constant_initializers_by_binding_.size()) return;
	StaticConstantInitializerFact& fact =
		static_constant_initializers_by_binding_[canonical];
	if (fact.initializer == kNoDumpEdge) return;
	fact.prefer_materialized_definition =
		!ShouldPreserveRuntimeInitializerRecipe(
			false, spec, type, initializer);
}

void SemanticAnalyzer::PublishVariableInitializer(BindingId binding,
	TypeId type, const SpecInfo& spec, const ExpressionInfo& initializer,
	bool preserve_runtime_recipe)
{
	program_->bindings[binding].template_parameter_constant =
		dump_.nodes[initializer.node].template_parameter_constant;
	program_->bindings[program_->bindings[binding].canonical].
		template_parameter_constant =
		program_->bindings[binding].template_parameter_constant;
	if (!(preserve_runtime_recipe && program_->types.IsReference(type)))
		PublishConstantVariableInitializer(binding, type, spec, initializer);
	if (!preserve_runtime_recipe && spec.is_constexpr &&
		IsClassObjectType(type))
	{
		const BindingId canonical = program_->bindings[binding].canonical;
		if (canonical < static_constant_initializers_by_binding_.size() &&
			static_constant_initializers_by_binding_[canonical].initializer !=
				kNoDumpEdge)
			static_constant_initializers_by_binding_[canonical].
				prefer_materialized_definition = true;
	}
	if (preserve_runtime_recipe)
		DemandRuntimeInitializerFunctions(initializer.node);
}

}
}
