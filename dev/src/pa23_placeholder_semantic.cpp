#include "pa12_semantic_detail.h"

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
	if (!spec.placeholder_auto)
		return BuildDeclarator(declarator, spec.type, scope);
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
	if (expression == kNoNode || arena_->IsTag(expression, "braced-init-list"))
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
	const FunctionInfo& initial = GetFunction(function);
	if (initial.placeholder_return_kind == PLACEHOLDER_DECLARATOR_NONE ||
		initial.retained_definition_semantics != kNoDumpEdge)
		return;
	if (!initial.defined || initial.definition_body == kNoNode)
		throw std::runtime_error(
			"placeholder function return requires a visible definition");

	const bool member = initial.member_owner != kNoType;
	const TypeId provisional_output = member ?
		AdaptMemberFunctionType(function) : initial.type;
	const std::uint32_t detached = MakeDump(DUMP_FUNCTION_DEFINITION,
		provisional_output, VALUE_NONE, initial.display_name, initial.binding);
	const ScopeId function_scope = NewScope(initial.lexical_scope,
		SCOPE_FUNCTION, program_->bindings[initial.binding].name,
		ScopePrefixId(initial.owner));
	BindFunctionParameterPackElement(
		function_scope, initial.parameter_pack_name, kNoBinding);
	if (member)
	{
		const TypeId this_type =
			program_->types.Parameters(provisional_output)[0];
		const NameId this_name = program_->names.Intern("this");
		const BindingId this_binding = program_->AddBinding(function_scope,
			BIND_PARAMETER, this_name, this_type);
		dump_.Add(detached, MakeDump(DUMP_PARAMETER, this_type,
			VALUE_NONE, this_name, this_binding));
	}
	for (std::size_t i = 0; i < initial.parameters.size(); ++i)
	{
		const ParameterInfo& parameter = initial.parameters[i];
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

	const TypeId previous_return = current_return_type_;
	const EntityId previous_class = current_class_context_;
	const BindingId previous_function = current_function_context_;
	current_return_type_ = initial.placeholder_return_deduced ?
		initial.placeholder_return_type : kNoType;
	current_class_context_ = initial.friend_of != kNoEntity ?
		initial.friend_of : program_->bindings[initial.binding].member_owner;
	current_function_context_ = function;
	try
	{
		AnalyzeCompound(initial.definition_body, function_scope, detached);
		CompletePlaceholderFunctionReturn(function);
		FinalizeNamedReturnSlot(detached);
	}
	catch (...)
	{
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
	dump_.nodes[detached].type = member ?
		AdaptMemberFunctionType(function) : completed.type;
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
	const bool dependent_result = member_owner == kNoEntity &&
		program_->entities[entity].has_user_provided_constructor &&
		(program_->entities[entity].empty_class ? nontrivial_empty_result :
		 pattern.deferred_result_formation);
	const bool conversion_result = pattern.conversion_template &&
		program_->entities[entity].template_argument_count == 0;
	if (dependent_result || conversion_result)
		program_->bindings[canonical_binding].
			force_indirect_class_result_abi = true;
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
