#include "pa12_semantic_detail.h"

namespace cppgm
{
namespace pa12_semantic_detail
{

DeclaratorInfo SemanticAnalyzer::BuildVariableDeclarator(
	NodeId item, NodeId declarator, const SpecInfo& spec, ScopeId scope,
	bool local)
{
	if (!spec.placeholder_auto)
		return BuildDeclarator(declarator, spec.type, scope);
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
		spec.placeholder_cv != CV_NONE;
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
			if (value.category == VALUE_LVALUE)
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
	prepared_placeholder_initializers_[item] = value;
	return parsed;
}

bool SemanticAnalyzer::TakePreparedPlaceholderVariableInitializer(
	NodeId item, ExpressionInfo* initializer)
{
	std::unordered_map<NodeId, ExpressionInfo>::iterator found =
		prepared_placeholder_initializers_.find(item);
	if (found == prepared_placeholder_initializers_.end()) return false;
	*initializer = found->second;
	prepared_placeholder_initializers_.erase(found);
	return true;
}

void SemanticAnalyzer::ApplyConditionalClassConversion(
	ExpressionInfo* yes, ExpressionInfo* no)
{
	const TypeId yes_type = EffectiveType(yes->type);
	const TypeId no_type = EffectiveType(no->type);
	if (program_->types.RemoveTopCv(yes_type) ==
		program_->types.RemoveTopCv(no_type)) return;
	if (!IsClassObjectType(yes_type) || !IsClassObjectType(no_type)) return;
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

bool SemanticAnalyzer::HasConstructorTemplatePattern(EntityId entity) const
{
	const EntityRecord& owner = program_->entities[entity];
	const std::uint64_t key =
		(static_cast<std::uint64_t>(owner.member_scope) << 32) |
		owner.identity_name;
	const CompactIndexSequence* patterns = template_function_sets_.Find(key);
	for (std::size_t i = 0; patterns && i < patterns->Size(); ++i)
		if (function_templates_[(*patterns)[i]].constructor_template) return true;
	return false;
}

void SemanticAnalyzer::PublishStableFunctionTemplateResultAbi(
	const FunctionTemplatePattern& pattern, TypeId function_type,
	EntityId member_owner, BindingId canonical_binding)
{
	const TypeId result = program_->types.Get(function_type).child;
	const EntityId entity = EntityOf(result);
	if (entity == kNoEntity || program_->entities[entity].empty_class) return;
	const bool dependent_result = pattern.deferred_result_formation &&
		member_owner == kNoEntity &&
		program_->entities[entity].has_user_provided_constructor;
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
