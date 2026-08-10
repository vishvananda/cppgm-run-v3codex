#include "pa12_semantic_detail.h"
#include "post_tokenizer.h"

#include <limits>
#include <stdexcept>
namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

const std::size_t kDestructorArrayInlineLimit = 8;

bool IsClassEntity(const Program& program, EntityId entity)
{
	if (entity == kNoEntity) return false;
	const NamedFlavor flavor = program.entities[entity].flavor;
	return flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
		flavor == NAMED_UNION;
}

std::vector<unsigned char> DecodeStringInitializer(
	const std::string& spelling)
{
	std::string decoded;
	if (!DecodeNarrowStringLiteral(spelling, &decoded))
		throw std::runtime_error("invalid string array initializer");
	std::vector<unsigned char> bytes(decoded.begin(), decoded.end());
	bytes.push_back(0);
	return bytes;
}

}

bool SemanticAnalyzer::IsClassObjectType(TypeId type) const
{
	return IsClassEntity(*program_, EntityOf(type));
}

bool SemanticAnalyzer::EmptyDefaultConstructorChain(BindingId constructor,
	std::vector<BindingId>* base_entries)
{
	for (std::size_t depth = 0; depth <= program_->entities.size(); ++depth)
	{
		const FunctionInfo& info = GetFunction(constructor);
		const BindingRecord& binding = program_->bindings[constructor];
		if (!info.constructor || !info.parameters.empty() ||
			info.constructor_initializer != kNoNode ||
			(info.definition_body != kNoNode &&
			 FirstSemanticChild(info.definition_body) != kNoNode))
			return false;
		const EntityId entity = binding.member_owner;
		if (entity == kNoEntity || entity >= entity_data_members_.size())
			return false;
		if (program_->entities[entity].polymorphic_class) return false;
		const std::vector<BindingId>& members = entity_data_members_[entity];
		for (std::size_t i = 0; i < members.size(); ++i)
		{
			const BindingRecord& member = program_->bindings[members[i]];
			if (member.has_default_member_initializer) return false;
			TypeId member_type = member.type;
			const TypeRecord* member_record =
				&program_->types.Get(member_type);
			while (member_record->kind == TYPE_ARRAY ||
				member_record->kind == TYPE_QUALIFIED)
			{
				member_type = member_record->child;
				member_record = &program_->types.Get(member_type);
			}
			if (member_record->kind == TYPE_NAMED)
			{
				const EntityRecord& subobject =
					program_->entities[member_record->entity];
				if ((subobject.flavor == NAMED_STRUCT ||
					subobject.flavor == NAMED_CLASS ||
					subobject.flavor == NAMED_UNION) &&
					!subobject.trivial_default_constructor)
					return false;
			}
		}
		const EntityId base = program_->entities[entity].direct_base;
		if (base == kNoEntity) return true;
		if (base >= entity_constructors_.size()) return false;
		BindingId next = kNoBinding;
		const std::vector<BindingId>& candidates = entity_constructors_[base];
		for (std::size_t i = 0; i < candidates.size(); ++i)
		{
			const FunctionInfo& candidate = GetFunction(candidates[i]);
			std::size_t required = candidate.parameters.size();
			while (required != 0 &&
				candidate.parameters[required - 1].default_argument != kNoNode)
				--required;
			if (!candidate.constructor || candidate.deleted_constructor ||
				required != 0)
				continue;
			if (next != kNoBinding) return false;
			next = candidates[i];
		}
		if (next == kNoBinding) return false;
		const FunctionInfo& next_info = GetFunction(next);
		if (!(next_info.implicit_constructor &&
			program_->entities[base].trivial_default_constructor))
			base_entries->push_back(EnsureConstructorBaseEntry(next));
		constructor = next;
	}
	throw std::logic_error("cyclic default-constructor chain");
}

BindingId SemanticAnalyzer::ValidateClassValueConstruction(TypeId type,
	const ExpressionInfo& source, bool copy_initialization)
{
	const EntityId entity = EntityOf(type);
	if (!IsClassEntity(*program_, entity))
		throw std::logic_error("class-value construction has non-class type");
	if (program_->entities[entity].abstract_class)
		throw std::runtime_error("cannot construct an abstract class value");
	std::vector<NodeId> argument_syntax(1, kNoNode);
	std::vector<ExpressionInfo> arguments(1, source);
	return SelectConstructor(kNoScope, argument_syntax, arguments,
		ConstructorCandidates(entity), copy_initialization, false, 0, false, kNoNode, type);
}

std::uint32_t SemanticAnalyzer::BuildClassValueConstructorAction(TypeId type,
	const ExpressionInfo& source, bool copy_initialization, bool demand)
{
	const EntityId entity = EntityOf(type);
	if (!IsClassEntity(*program_, entity))
		throw std::logic_error("class-value construction has non-class type");
	std::vector<NodeId> argument_syntax(1, kNoNode);
	std::vector<ExpressionInfo> arguments(1, source);
	std::vector<CallConversionFact> selected_conversions;
	const BindingId selected = SelectConstructor(kNoScope, argument_syntax,
		arguments, ConstructorCandidates(entity), copy_initialization, false,
		&selected_conversions, false, kNoNode, type);
	const FunctionInfo constructor = GetFunction(selected);
	const TypeRecord function_type = program_->types.Get(constructor.type);
	const TypeId* parameter_data = program_->types.Parameters(constructor.type);
	if (function_type.parameter_count == 0)
		throw std::logic_error("class-value constructor has no source parameter");
	const std::vector<TypeId> parameters(parameter_data,
		parameter_data + function_type.parameter_count);
	const ExpressionInfo converted_source = ApplyCallArgument(
		source, parameters[0],
		selected_conversions.empty() ? 0 : &selected_conversions[0]);
	ExpressionInfo direct;
	if (TryBuildElidedClassValueTransfer(
		type, converted_source, selected, &direct))
		return direct.node;
	bool materialized_conversion_result = false;
	if (dump_.nodes[converted_source.node].kind == DUMP_TEMPORARY_OBJECT &&
		dump_.nodes[converted_source.node].first_edge != kNoDumpEdge &&
		dump_.edges[dump_.nodes[converted_source.node].first_edge].next ==
			kNoDumpEdge)
	{
		const DumpNode& recipe = dump_.nodes[dump_.edges[
			dump_.nodes[converted_source.node].first_edge].child];
		materialized_conversion_result =
			recipe.kind == DUMP_CALL_EXPRESSION && recipe.user_conversion_call;
	}
	const std::uint32_t action = MakeDump(DUMP_CONSTRUCTOR_ACTION,
		AdaptMemberFunctionType(selected), VALUE_NONE,
		constructor.display_name, selected);
	dump_.nodes[action].operand_type =
		program_->types.RemoveTopCv(EffectiveType(type));
	dump_.nodes[action].trivial_special_member_action =
		constructor.trivial_special_member && !materialized_conversion_result;
	dump_.Add(action, converted_source.node);
	std::vector<ExpressionInfo> constexpr_arguments(1, converted_source);
	for (std::size_t a = 1; a < function_type.parameter_count; ++a)
	{
		if (a >= constructor.parameters.size() ||
			constructor.parameters[a].default_argument == kNoNode)
			throw std::logic_error("class-value constructor lacks default argument");
		ExpressionInfo argument = AnalyzeExpression(
			constructor.parameters[a].default_argument,
			constructor.parameters[a].default_scope, parameters[a]);
		argument = ApplyCallArgument(argument, parameters[a]);
		dump_.nodes[argument.node].default_argument = true;
		dump_.Add(action, argument.node);
		constexpr_arguments.push_back(argument);
	}
	std::uint32_t constexpr_object = kNoConstexprObject;
	if (constant_evaluation_suppressed_depth_ == 0 &&
		(constant_expression_required_depth_ != 0 ||
		 constexpr_evaluation_depth_ != 0) &&
		(constructor.constexpr_function || constructor.defaulted_constructor ||
		 constructor.implicit_constructor) &&
		TryEvaluateConstexprConstructor(
			selected, constexpr_arguments, &constexpr_object))
		PublishDumpObject(action, constexpr_object);
	const EntityId constructor_owner =
		program_->bindings[constructor.binding].member_owner;
	const bool explicitly_defaulted = constructor.defaulted_special_member &&
		!constructor.implicit_special_member &&
		!constructor.synthesized_memberwise_copy &&
		IsClassTemplateSpecializationEntity(constructor_owner);
	if (preserve_constant_initializer_recipe_depth_ == 0 &&
		((demand && (explicitly_defaulted ||
		!dump_.nodes[action].trivial_special_member_action)) ||
		(materialized_conversion_result &&
		 !dump_.nodes[action].trivial_special_member_action)))
		DemandFunction(selected);
	++expression_count_;
	return action;
}

ExpressionInfo SemanticAnalyzer::BuildClassConditional(
	std::uint32_t condition, const ExpressionInfo& yes,
	const ExpressionInfo& no, TypeId type, bool preserve_xvalue)
{
	const TypeId object = program_->types.RemoveTopCv(EffectiveType(type));
	if (!IsClassEntity(*program_, EntityOf(object)))
		throw std::logic_error("class conditional has non-class result");
	const ExpressionInfo operands[2] = {yes, no};
	std::uint32_t arms[2] = {kNoDumpEdge, kNoDumpEdge};
	for (std::size_t i = 0; i < 2; ++i)
	{
		ExpressionInfo source = operands[i];
		std::uint32_t recipe = source.node;
		const DumpNode& source_node = dump_.nodes[recipe];
		if (source_node.kind == DUMP_TEMPORARY_OBJECT &&
			program_->types.RemoveTopCv(EffectiveType(source_node.type)) == object &&
			source_node.first_edge != kNoDumpEdge &&
			dump_.edges[source_node.first_edge].next == kNoDumpEdge)
		{
			const std::uint32_t child = dump_.edges[source_node.first_edge].child;
			const DumpKind kind = dump_.nodes[child].kind;
			if (kind == DUMP_CONSTRUCTOR_ACTION ||
				kind == DUMP_CALL_EXPRESSION || kind == DUMP_BRACED_INIT_LIST ||
				kind == DUMP_CLASS_VALUE_TRANSFER)
				recipe = child;
		}
		const DumpKind recipe_kind = dump_.nodes[recipe].kind;
		const bool direct = recipe != source.node ||
			(source.category == VALUE_PRVALUE &&
			 (recipe_kind == DUMP_CALL_EXPRESSION ||
			  recipe_kind == DUMP_CONSTRUCTOR_ACTION ||
			  recipe_kind == DUMP_BRACED_INIT_LIST ||
			  recipe_kind == DUMP_CLASS_VALUE_TRANSFER));
		if (!direct)
		{
			if (!preserve_xvalue && source.category == VALUE_XVALUE)
				source.category = VALUE_LVALUE;
			recipe = BuildClassValueConstructorAction(
				object, source, true, true);
		}
		const std::uint32_t arm = MakeDump(
			DUMP_CONDITIONAL_ARM, object, VALUE_NONE);
		dump_.Add(arm, recipe);
		std::vector<std::uint32_t> temporaries;
		CollectTemporaryObjects(recipe, &temporaries);
		if (!temporaries.empty()) MarkFullExpressionCalls(recipe);
		for (std::size_t t = temporaries.size(); t != 0; --t)
		{
			const std::uint32_t action =
				MakeTemporaryDestructorAction(temporaries[t - 1]);
			if (action != kNoDumpEdge)
			{
				dump_.nodes[action].full_expression_staging = true;
				dump_.Add(arm, action);
			}
		}
		arms[i] = arm;
	}
	const std::uint32_t expression = MakeDump(
		DUMP_CONDITIONAL_EXPRESSION, object, VALUE_PRVALUE);
	dump_.Add(expression, condition);
	dump_.Add(expression, arms[0]);
	dump_.Add(expression, arms[1]);
	ExpressionInfo result;
	result.node = expression;
	result.type = object;
	result.category = VALUE_PRVALUE;
	if ((constant_expression_required_depth_ != 0 ||
		constexpr_evaluation_depth_ != 0) &&
		condition < dump_.nodes.size() && dump_.nodes[condition].constant)
	{
		const ExpressionInfo& selected =
			dump_.nodes[condition].constant_value != 0 ? yes : no;
		const std::uint32_t selected_object = ExpressionObject(selected);
		if (selected_object != kNoConstexprObject)
			SetExpressionSubobject(&result, selected_object,
				ExpressionCompleteObject(selected));
	}
	expression_count_ += 3;
	return result;
}

ExpressionInfo SemanticAnalyzer::RetargetClassConditional(
	const ExpressionInfo& value, TypeId type)
{
	if (dump_.nodes[value.node].kind != DUMP_CONDITIONAL_EXPRESSION)
		throw std::logic_error("retargeted class value is not conditional");
	std::vector<std::uint32_t> children;
	for (std::uint32_t edge = dump_.nodes[value.node].first_edge;
		edge != kNoDumpEdge; edge = dump_.edges[edge].next)
		children.push_back(dump_.edges[edge].child);
	if (children.size() != 3)
		throw std::logic_error("invalid class conditional shape");
	if (value.category == VALUE_PRVALUE &&
		dump_.nodes[children[1]].kind == DUMP_CONDITIONAL_ARM &&
		dump_.nodes[children[2]].kind == DUMP_CONDITIONAL_ARM)
		return value;
	ExpressionInfo yes;
	yes.node = children[1];
	yes.type = dump_.nodes[children[1]].type;
	yes.category = dump_.nodes[children[1]].category;
	yes.binding = dump_.nodes[children[1]].binding;
	ExpressionInfo no;
	no.node = children[2];
	no.type = dump_.nodes[children[2]].type;
	no.category = dump_.nodes[children[2]].category;
	no.binding = dump_.nodes[children[2]].binding;
	return BuildClassConditional(children[0], yes, no, type, true);
}
void SemanticAnalyzer::FinalizeNamedReturnSlot(std::uint32_t function)
{
	const TypeId result = program_->types.Get(dump_.nodes[function].type).child;
	const EntityId entity = EntityOf(result);
	if (!IsClassEntity(*program_, entity)) return;
	const EntityRecord& class_record = program_->entities[entity];
	const std::size_t size = program_->SizeOf(result);
	const bool dependent_empty_value = class_record.empty_class &&
		class_record.template_argument_count != 0 && !class_record.closure_forced_indirect_value_abi && (class_record.enclosing_class == kNoEntity ||
		 !class_record.indirect_class_value_abi);
	const bool indirect = !dependent_empty_value && (size > 16 ||
		(size < 16 && class_record.indirect_class_value_abi));
	std::vector<std::uint32_t> pending(1, function);
	std::vector<std::uint32_t> return_edges;
	std::vector<std::uint32_t> sources;
	std::vector<BindingId> deferred_constructors;
	BindingId candidate = kNoBinding;
	bool eligible = true;
	while (!pending.empty())
	{
		const std::uint32_t node = pending.back();
		pending.pop_back();
		const DumpNode& record = dump_.nodes[node];
		if (record.kind == DUMP_RETURN_STATEMENT)
		{
			const std::uint32_t edge = record.first_edge;
			if (edge == kNoDumpEdge) { eligible = false; continue; }
			const std::uint32_t action_node = dump_.edges[edge].child;
			const DumpNode& action = dump_.nodes[action_node];
			if (action.kind != DUMP_CONSTRUCTOR_ACTION)
			{
				eligible = false;
				continue;
			}
			if (!action.trivial_special_member_action)
				deferred_constructors.push_back(action.binding);
			if (action.first_edge == kNoDumpEdge)
			{
				eligible = false;
				continue;
			}
			const std::uint32_t source = dump_.edges[action.first_edge].child;
			const DumpNode& source_record = dump_.nodes[source];
			const BindingId binding = source_record.binding;
			if (source_record.kind != DUMP_ID_EXPRESSION ||
				binding == kNoBinding || binding >= program_->bindings.size())
			{
				eligible = false;
				continue;
			}
			const BindingRecord& declaration = program_->bindings[binding];
			if (declaration.kind != BIND_VARIABLE ||
				declaration.storage_class != STORAGE_CLASS_NONE ||
				program_->KindOfScope(declaration.owner) != SCOPE_BLOCK ||
				(candidate != kNoBinding && candidate != binding))
			{
				eligible = false;
				continue;
			}
			candidate = binding;
			return_edges.push_back(edge);
			sources.push_back(source);
			continue;
		}
		for (std::uint32_t edge = record.first_edge; edge != kNoDumpEdge;
			edge = dump_.edges[edge].next)
			pending.push_back(dump_.edges[edge].child);
	}
	if (!indirect || !eligible || candidate == kNoBinding ||
		return_edges.empty() ||
		candidate >= variable_node_by_binding_.size() ||
		variable_node_by_binding_[candidate] == kNoDumpEdge)
	{
		for (std::size_t i = 0; i < deferred_constructors.size(); ++i)
			DemandFunction(deferred_constructors[i]);
		return;
	}
	DumpNode& variable = dump_.nodes[variable_node_by_binding_[candidate]];
	variable.direct_return_slot = true;
	for (std::size_t i = 0; i < deferred_constructors.size(); ++i)
		DemandSynthesizedConstructorDependencies(deferred_constructors[i]);
	for (std::size_t i = 0; i < return_edges.size(); ++i)
	{
		dump_.edges[return_edges[i]].child = sources[i];
		dump_.nodes[sources[i]].direct_return_slot = true;
	}
}

std::uint32_t SemanticAnalyzer::BuildDefaultConstructorAction(TypeId type,
	ScopeId scope)
{
	TypeId object = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord& record = program_->types.Get(object);
	if (record.kind != TYPE_ARRAY)
	{
		const std::vector<NodeId> arguments;
		return BuildConstructorAction(type, scope, arguments, false, false);
	}
	if (record.bound == 0)
		throw std::runtime_error("default construction of an unbounded array");
	const std::uint32_t action = MakeDump(DUMP_CONSTRUCTOR_ARRAY_ACTION);
	dump_.nodes[action].operand_type = object;
	const BindingId destructor = DestructorForType(object);
	if (destructor != kNoBinding &&
		!program_->entities[DestructedEntity(object)].trivial_destructor)
	{
		dump_.nodes[action].binding = destructor;
		DemandFunction(destructor);
	}
	dump_.Add(action, BuildDefaultConstructorAction(record.child, scope));
	return action;
}

bool SemanticAnalyzer::IsDirectTrivialClassValueType(TypeId type) const
{
	const TypeRecord& top = program_->types.Get(type);
	if (top.kind == TYPE_LVALUE_REFERENCE || top.kind == TYPE_RVALUE_REFERENCE)
		return false;
	type = program_->types.RemoveTopCv(type);
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind != TYPE_NAMED) return false;
	const EntityRecord& entity = program_->entities[record.entity];
	return (entity.flavor == NAMED_STRUCT || entity.flavor == NAMED_CLASS ||
		entity.flavor == NAMED_UNION) && entity.is_aggregate &&
		entity.trivial_destructor && !entity.empty_class;
}

ExpressionInfo SemanticAnalyzer::BuildDirectClassValueTransfer(
	const ExpressionInfo& source, TypeId target,
	BindingId selected_constructor)
{
	if (!graph_consumer_) return source;
	bool validated_special_member = false;
	if (selected_constructor != kNoBinding)
	{
		const FunctionInfo& selected = GetFunction(selected_constructor);
		validated_special_member =
			selected.special_member == SPECIAL_MEMBER_COPY_CONSTRUCTOR ||
			selected.special_member == SPECIAL_MEMBER_MOVE_CONSTRUCTOR;
	}
	if ((!IsDirectTrivialClassValueType(target) &&
		 dump_.nodes[source.node].kind != DUMP_CALL_EXPRESSION &&
		 !validated_special_member) ||
		program_->types.RemoveTopCv(EffectiveType(source.type)) !=
			program_->types.RemoveTopCv(EffectiveType(target)))
		throw std::logic_error("invalid direct class-value transfer");
	const std::uint32_t action = MakeDump(DUMP_CLASS_VALUE_TRANSFER,
		program_->types.RemoveTopCv(EffectiveType(target)), VALUE_PRVALUE);
	dump_.nodes[action].selected_binding = selected_constructor;
	dump_.Add(action, source.node);
	ExpressionInfo result;
	result.node = action;
	result.type = program_->types.RemoveTopCv(EffectiveType(target));
	result.category = VALUE_PRVALUE;
	const std::uint32_t object = ExpressionObject(source);
	if (object != kNoConstexprObject)
	{
		SetExpressionSubobject(
			&result, object, ExpressionCompleteObject(source));
		PublishDumpObject(action, object);
	}
	++expression_count_;
	return result;
}

void SemanticAnalyzer::AnalyzeReturnStatement(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	const std::uint32_t statement = MakeDump(DUMP_RETURN_STATEMENT);
	dump_.Add(output_parent, statement);
	const NodeId expression = FirstSemanticChild(node);
	if (expression == kNoNode)
	{
		if (!IsVoid(current_return_type_))
			throw std::runtime_error("missing return value");
	}
	else
	{
		const TypeId returned_object = program_->types.RemoveTopCv(
			EffectiveType(current_return_type_));
		const TypeRecord& returned_record = program_->types.Get(returned_object);
		const TypeRecord& returned_top = program_->types.Get(current_return_type_);
		const bool class_return = !IsVoid(current_return_type_) &&
			returned_top.kind != TYPE_LVALUE_REFERENCE &&
			returned_top.kind != TYPE_RVALUE_REFERENCE &&
			returned_record.kind == TYPE_NAMED &&
			(program_->entities[returned_record.entity].flavor == NAMED_STRUCT ||
			 program_->entities[returned_record.entity].flavor == NAMED_CLASS ||
			 program_->entities[returned_record.entity].flavor == NAMED_UNION);
		const bool direct_syntax = arena_->IsTag(expression, "call-expression") ||
			arena_->IsTag(expression, "braced-init-list");
		ExpressionInfo value = AnalyzeExpression(expression, scope,
			IsVoid(current_return_type_) || (class_return && !direct_syntax) ?
			kNoType : current_return_type_);
		if (IsVoid(current_return_type_) && !IsVoid(value.type))
			throw std::runtime_error("void function returns a value");
		if (class_return && dump_.nodes[value.node].kind ==
			DUMP_TEMPORARY_OBJECT &&
			dump_.nodes[value.node].first_edge != kNoDumpEdge &&
			dump_.edges[dump_.nodes[value.node].first_edge].next == kNoDumpEdge)
		{
			const std::uint32_t recipe =
				dump_.edges[dump_.nodes[value.node].first_edge].child;
			if ((dump_.nodes[recipe].kind == DUMP_CONSTRUCTOR_ACTION ||
				dump_.nodes[recipe].kind == DUMP_BRACED_INIT_LIST ||
				dump_.nodes[recipe].kind == DUMP_CLASS_VALUE_TRANSFER) &&
				program_->types.RemoveTopCv(EffectiveType(value.type)) ==
					returned_object)
			{
				value.node = recipe;
				value.category = VALUE_PRVALUE;
			}
		}
		if (class_return && dump_.nodes[value.node].kind ==
			DUMP_CONSTRUCTOR_ACTION &&
			dump_.nodes[value.node].first_edge != kNoDumpEdge &&
			dump_.edges[dump_.nodes[value.node].first_edge].next == kNoDumpEdge &&
			dump_.nodes[value.node].binding != kNoBinding)
		{
			const FunctionInfo& outer =
				GetFunction(dump_.nodes[value.node].binding);
			const std::uint32_t temporary =
				dump_.edges[dump_.nodes[value.node].first_edge].child;
			if ((outer.special_member == SPECIAL_MEMBER_COPY_CONSTRUCTOR ||
				 outer.special_member == SPECIAL_MEMBER_MOVE_CONSTRUCTOR ||
				 dump_.nodes[value.node].trivial_special_member_action) &&
				dump_.nodes[temporary].kind == DUMP_TEMPORARY_OBJECT &&
				dump_.nodes[temporary].first_edge != kNoDumpEdge &&
				dump_.edges[dump_.nodes[temporary].first_edge].next == kNoDumpEdge)
			{
				const std::uint32_t recipe =
					dump_.edges[dump_.nodes[temporary].first_edge].child;
				if (dump_.nodes[recipe].kind == DUMP_CONSTRUCTOR_ACTION ||
					dump_.nodes[recipe].kind == DUMP_BRACED_INIT_LIST ||
					dump_.nodes[recipe].kind ==
						DUMP_AGGREGATE_CONSTRUCTION_ACTION)
					value.node = recipe;
			}
		}
		if (class_return && dump_.nodes[value.node].kind ==
			DUMP_BRACED_INIT_LIST && dump_.nodes[value.node].value_initialization)
		{
			value.node = BuildDefaultConstructorAction(returned_object, scope);
			value.type = returned_object;
			value.category = VALUE_PRVALUE;
		}
		if (class_return && dump_.nodes[value.node].kind ==
			DUMP_BRACED_INIT_LIST &&
			program_->entities[returned_record.entity].is_aggregate)
		{
			const bool retained_specialization =
				current_function_context_ != kNoBinding &&
				GetFunction(current_function_context_).template_specialization;
			bool has_boundary_member = retained_specialization;
			for (std::uint32_t edge = dump_.nodes[value.node].first_edge;
				edge != kNoDumpEdge; edge = dump_.edges[edge].next)
			{
				const DumpNode& action =
					dump_.nodes[dump_.edges[edge].child];
				const TypeKind kind = program_->types.Get(action.type).kind;
				if (kind == TYPE_LVALUE_REFERENCE ||
					kind == TYPE_RVALUE_REFERENCE ||
					IsClassEntity(*program_, EntityOf(action.type)))
				{
					has_boundary_member = true;
					break;
				}
			}
			if (has_boundary_member)
				value.node = BuildAggregateConstructionAction(
					returned_object, value.node);
		}
		if (class_return && dump_.nodes[value.node].kind ==
			DUMP_CONDITIONAL_EXPRESSION &&
			program_->types.RemoveTopCv(EffectiveType(value.type)) ==
				returned_object)
		{
			value = RetargetClassConditional(value, current_return_type_);
			const std::uint32_t first = dump_.nodes[value.node].first_edge;
			std::uint32_t arm = first == kNoDumpEdge ? kNoDumpEdge :
				dump_.edges[first].next;
			for (std::size_t i = 0; i < 2 && arm != kNoDumpEdge; ++i)
			{
				const std::uint32_t arm_node = dump_.edges[arm].child;
				const std::uint32_t recipe = dump_.nodes[arm_node].first_edge;
				if (recipe != kNoDumpEdge && dump_.edges[recipe].next != kNoDumpEdge)
					AppendUnwindDestructionActions(scope, arm_node);
				arm = dump_.edges[arm].next;
			}
		}
		else if (class_return &&
			program_->types.RemoveTopCv(EffectiveType(value.type)) ==
				returned_object &&
			dump_.nodes[value.node].kind != DUMP_CONSTRUCTOR_ACTION &&
			dump_.nodes[value.node].kind != DUMP_BRACED_INIT_LIST &&
			dump_.nodes[value.node].kind !=
				DUMP_AGGREGATE_CONSTRUCTION_ACTION)
		{
			if (value.category == VALUE_PRVALUE &&
				dump_.nodes[value.node].kind == DUMP_CALL_EXPRESSION &&
				!dump_.nodes[value.node].explicit_user_conversion_call)
			{
				const BindingId selected = ValidateClassValueConstruction(
					current_return_type_, value);
				value = BuildDirectClassValueTransfer(
					value, current_return_type_, selected);
			}
			else
			{
				ExpressionInfo source = value;
				if (source.category == VALUE_LVALUE &&
					source.binding != kNoBinding)
				{
					const BindingRecord& declaration =
						program_->bindings[source.binding];
					if ((declaration.kind == BIND_VARIABLE ||
						 declaration.kind == BIND_PARAMETER) &&
						program_->KindOfScope(declaration.owner) != SCOPE_NAMESPACE)
						source.category = VALUE_XVALUE;
				}
				value.node = BuildClassValueConstructorAction(
					current_return_type_, source, true, false);
			}
			value.type = returned_object;
			value.category = VALUE_PRVALUE;
		}
		else if (class_return &&
			program_->types.RemoveTopCv(EffectiveType(value.type)) !=
				returned_object)
		{
			value.node = BuildClassValueConstructorAction(
				current_return_type_, value, true, false);
			value.type = returned_object;
			value.category = VALUE_PRVALUE;
		}
		dump_.Add(statement, value.node);
		AppendFullExpressionDestructionActions(value.node, statement);
		StageLambdaReturnTemporaryCleanup(value.node, statement);
	}
	AppendScopeDestructionActions(scope, statement);
}

ExpressionInfo SemanticAnalyzer::AnalyzeVariableInitializer(
	NodeId initializer_node, ScopeId scope, TypeId type, bool local)
{
	NodeId expression = FirstSemanticChild(initializer_node);
	const EntityId class_entity = EntityOf(type);
	const TypeKind declared_kind = program_->types.Get(type).kind;
	const TypeRecord declared = program_->types.Get(type);
	ExpressionInfo initializer;
	if (declared_kind == TYPE_ARRAY && expression != kNoNode &&
		arena_->IsTag(expression, "literal") &&
		arena_->Payload(expression).find('"') != std::string::npos)
	{
		const ExpressionInfo source = AnalyzeExpression(expression, scope);
		const TypeRecord source_array = program_->types.Get(source.type);
		if (source_array.kind != TYPE_ARRAY ||
			program_->types.RemoveTopCv(source_array.child) !=
				program_->types.RemoveTopCv(declared.child) ||
			source.string_unit_begin == kNoDumpEdge ||
			source.string_unit_count == 0)
			throw std::runtime_error(
				"string literal initializes an incompatible array");
		if (declared.bound != 0 &&
			source.string_unit_count > declared.bound)
			throw std::runtime_error("string literal is too long for array");
		const std::size_t count = declared.bound == 0 ?
			source.string_unit_count : declared.bound;
		const TypeId initialized_type = declared.bound == 0 ?
			program_->types.Array(declared.child, count) : type;
		const std::uint32_t list = MakeDump(DUMP_BRACED_INIT_LIST,
			initialized_type, VALUE_LVALUE);
		for (std::size_t i = 0; i < count; ++i)
		{
			const std::size_t unit = source.string_unit_begin + i;
			if (i < source.string_unit_count &&
				unit >= string_literal_units_.size())
				throw std::logic_error(
					"string literal initializer range is invalid");
			const std::int64_t code_unit = i < source.string_unit_count ?
				NormalizeIntegralConstant(
					declared.child, string_literal_units_[unit]) : 0;
			ExpressionInfo value = MakeLiteral(
				declared.child, InternNumber(code_unit));
			value.constant = true;
			value.value = code_unit;
			RecordExpressionFacts(value);
			dump_.Add(list, value.node);
		}
		initializer.node = list;
		initializer.type = initialized_type;
		initializer.category = VALUE_LVALUE;
		++expression_count_;
		return local ?
			BuildLocalAggregateArrayActions(initializer) : initializer;
	}
	if (declared_kind != TYPE_LVALUE_REFERENCE &&
		declared_kind != TYPE_RVALUE_REFERENCE &&
		IsClassEntity(*program_, class_entity))
	{
		std::vector<NodeId> arguments;
		if (expression != kNoNode &&
			arena_->IsTag(expression, "paren-initializer"))
		{
			for (std::uint32_t argument = arena_->FirstEdge(expression);
				argument != kNoEdge; argument = arena_->NextEdge(argument))
				arguments.push_back(arena_->EdgeChild(argument));
			std::vector<ExpressionInfo> prepared;
			const bool expanded = ExpandCallArgumentPacks(arguments, scope, &arguments, &prepared);
			initializer.node = BuildConstructorAction(type, scope, arguments, false, false, false, true, kNoNode, expanded ? &prepared : 0);
		}
		else if (expression != kNoNode &&
			arena_->IsTag(expression, "braced-init-list") &&
			!program_->entities[class_entity].is_aggregate)
		{
			for (std::uint32_t argument = arena_->FirstEdge(expression);
				argument != kNoEdge; argument = arena_->NextEdge(argument))
				arguments.push_back(arena_->EdgeChild(argument));
			initializer.node = BuildConstructorAction(type, scope, arguments,
				PayloadSource(initializer_node) == "copy", true, false, true,
				expression);
			if (arguments.empty() &&
				!program_->entities[class_entity].has_user_provided_constructor)
				dump_.nodes[initializer.node].value_initialization = true;
		}
		else if (expression != kNoNode &&
			arena_->IsTag(expression, "call-expression") &&
			!program_->entities[class_entity].is_aggregate)
		{
			const NodeId callee = FirstSemanticChild(expression);
			const std::string expected = program_->names.Get(
				program_->entities[class_entity].identity_name);
			if (callee == kNoNode || !arena_->IsTag(callee, "id-expression") ||
				arena_->Payload(callee) != expected)
			{
				initializer = AnalyzeExpression(expression, scope);
				if (program_->types.RemoveTopCv(EffectiveType(initializer.type)) !=
					program_->types.RemoveTopCv(type))
				{
					arguments.push_back(expression);
					const std::vector<ExpressionInfo> prepared(1, initializer);
					initializer.node = BuildConstructorAction(type, scope, arguments,
						true, false, false, true, kNoNode, &prepared);
				}
				else if (initializer.category == VALUE_PRVALUE &&
					dump_.nodes[initializer.node].kind == DUMP_CALL_EXPRESSION &&
					!dump_.nodes[initializer.node].explicit_user_conversion_call)
				{
					const BindingId selected =
						ValidateClassValueConstruction(type, initializer);
					initializer = BuildDirectClassValueTransfer(
						initializer, type, selected);
				}
				else initializer.node =
					BuildClassValueConstructorAction(type, initializer);
			}
			else
			{
				NodeId argument_list = FindChild(expression, "argument-list");
				if (argument_list == kNoNode)
					argument_list = FindChild(expression, "braced-init-list");
				if (argument_list != kNoNode)
					for (std::uint32_t argument = arena_->FirstEdge(argument_list);
						argument != kNoEdge; argument = arena_->NextEdge(argument))
						arguments.push_back(arena_->EdgeChild(argument));
				std::vector<ExpressionInfo> prepared;
				const bool expanded = ExpandCallArgumentPacks(arguments, scope, &arguments, &prepared);
				const NodeId braced = argument_list != kNoNode && arena_->IsTag(argument_list, "braced-init-list") ? argument_list : kNoNode;
				initializer.node = BuildConstructorAction(type, scope, arguments, false,
					braced != kNoNode, false, true, braced, expanded ? &prepared : 0);
			}
		}
		else if (expression != kNoNode &&
			arena_->IsTag(expression, "cast-expression") &&
			!program_->entities[class_entity].is_aggregate)
		{
			initializer = AnalyzeExpression(expression, scope, type);
			if (dump_.nodes[initializer.node].kind == DUMP_TEMPORARY_OBJECT &&
				dump_.nodes[initializer.node].first_edge != kNoDumpEdge &&
				dump_.edges[dump_.nodes[initializer.node].first_edge].next ==
					kNoDumpEdge)
			{
				const std::uint32_t recipe = dump_.edges[
					dump_.nodes[initializer.node].first_edge].child;
				if (dump_.nodes[recipe].kind == DUMP_CONSTRUCTOR_ACTION &&
					dump_.nodes[recipe].operand_type ==
						program_->types.RemoveTopCv(EffectiveType(type)))
					initializer.node = recipe;
			}
		}
		else if (TryAnalyzeClassExpressionInitializer(
			expression, scope, type, &initializer)) {}
		else if (expression != kNoNode &&
			!program_->entities[class_entity].is_aggregate)
		{
			arguments.push_back(expression);
			initializer.node = BuildConstructorAction(
				type, scope, arguments, true, false);
		}
		else
		{
			initializer = AnalyzeExpression(expression, scope, type);
			if (program_->types.RemoveTopCv(EffectiveType(initializer.type)) ==
				program_->types.RemoveTopCv(type) &&
				dump_.nodes[initializer.node].kind != DUMP_BRACED_INIT_LIST)
			{
				if (dump_.nodes[initializer.node].kind ==
					DUMP_CONDITIONAL_EXPRESSION &&
					IsDirectTrivialClassValueType(type)) {}
				else if (initializer.category == VALUE_PRVALUE &&
					dump_.nodes[initializer.node].kind == DUMP_CALL_EXPRESSION &&
					!dump_.nodes[initializer.node].explicit_user_conversion_call)
				{
					const BindingId selected =
						ValidateClassValueConstruction(type, initializer);
					initializer = BuildDirectClassValueTransfer(
						initializer, type, selected);
				}
				else initializer.node =
					BuildClassValueConstructorAction(type, initializer);
			}
			else initializer = ApplyTarget(initializer, type);
		}
		initializer.type = type;
		initializer.category = VALUE_NONE;
	}
	else
	{
		const bool direct_initialization = expression != kNoNode &&
			arena_->IsTag(expression, "paren-initializer");
		if (expression != kNoNode &&
			arena_->IsTag(expression, "paren-initializer"))
			expression = FirstSemanticChild(expression);
		if (direct_initialization)
		{
			initializer = AnalyzeExpression(expression, scope);
			initializer = EntityOf(initializer.type) != kNoEntity ?
				ApplyExplicitConversion(initializer, type) :
				ApplyTarget(initializer, type);
		}
		else initializer = AnalyzeExpression(expression, scope, type);
		if ((declared_kind == TYPE_LVALUE_REFERENCE ||
			 declared_kind == TYPE_RVALUE_REFERENCE) &&
			initializer.category == VALUE_PRVALUE &&
			IsClassEntity(*program_, EntityOf(initializer.type)) &&
			dump_.nodes[initializer.node].kind == DUMP_CALL_EXPRESSION)
		{
			initializer = MaterializeTemporary(initializer);
			initializer = ApplyTarget(initializer, type);
		}
	}
	return FinalizeVariableInitializer(initializer, type, class_entity, local);
}

void SemanticAnalyzer::AddMemberInitializationAction(BindingId member_id,
	NodeId initializer, ScopeId scope, std::uint32_t body)
{
	const BindingRecord& member = program_->bindings[member_id];
	const EntityId member_entity = DestructedEntity(member.type);
	const TypeKind member_kind = program_->types.Get(member.type).kind;
	const bool class_member = member_kind != TYPE_LVALUE_REFERENCE &&
		member_kind != TYPE_RVALUE_REFERENCE && IsClassEntity(*program_, member_entity);
	if (initializer == kNoNode && member.anonymous_union_storage &&
		program_->entities[member_entity].union_default_member == kNoBinding) return;
	if (initializer == kNoNode && (member_kind == TYPE_LVALUE_REFERENCE ||
		member_kind == TYPE_RVALUE_REFERENCE))
		throw std::runtime_error("reference member is not initialized");
	if (initializer == kNoNode && member_kind == TYPE_QUALIFIED)
	{
		const TypeRecord& qualified = program_->types.Get(member.type);
		if ((qualified.cv & CV_CONST) != 0 &&
			!IsClassEntity(*program_, EntityOf(qualified.child)))
			throw std::runtime_error("const scalar member is not initialized");
	}
	if (initializer == kNoNode && !class_member) return;
	const bool copy_initialization = initializer != kNoNode &&
		arena_->IsTag(initializer, "initializer") &&
		PayloadSource(initializer) == "copy";
	while (initializer != kNoNode && arena_->IsTag(initializer, "initializer"))
		initializer = FirstSemanticChild(initializer);
	const std::uint32_t action = MakeDump(DUMP_INITIALIZER_ACTION,
		member.type, VALUE_NONE, member.name, member_id);
	if (class_member)
	{
		std::uint32_t value = kNoDumpEdge;
		if (initializer == kNoNode)
			value = BuildDefaultConstructorAction(member.type, scope);
		else if (arena_->IsTag(initializer, "braced-init-list") &&
			program_->entities[member_entity].is_aggregate)
			value = AnalyzeBracedInit(initializer, scope, member.type).node;
		else
		{
			std::vector<NodeId> arguments;
			bool constructor_copy = copy_initialization;
			bool constructor_list = false;
			if (initializer != kNoNode &&
				(arena_->IsTag(initializer, "paren-argument-list") ||
				 arena_->IsTag(initializer, "braced-init-list")))
			{
				constructor_list = arena_->IsTag(initializer,
					"braced-init-list");
				for (std::uint32_t edge = arena_->FirstEdge(initializer);
					edge != kNoEdge; edge = arena_->NextEdge(edge))
					arguments.push_back(arena_->EdgeChild(edge));
			}
			else if (initializer != kNoNode &&
				arena_->IsTag(initializer, "call-expression"))
			{
				const NodeId callee = FirstSemanticChild(initializer);
				const std::string expected = program_->names.Get(
					program_->entities[member_entity].identity_name);
				if (callee == kNoNode || !arena_->IsTag(callee, "id-expression") ||
					arena_->Payload(callee) != expected)
				{
					ExpressionInfo initialized = AnalyzeExpression(
						initializer, scope, member.type);
					if (program_->types.RemoveTopCv(
						EffectiveType(initialized.type)) !=
						program_->types.RemoveTopCv(EffectiveType(member.type)))
						throw std::runtime_error(
							"invalid class default member initializer");
					if (initialized.category == VALUE_PRVALUE &&
						dump_.nodes[initialized.node].kind == DUMP_CALL_EXPRESSION &&
						!dump_.nodes[initialized.node].explicit_user_conversion_call)
					{
						const BindingId selected = ValidateClassValueConstruction(
							member.type, initialized);
						initialized = BuildDirectClassValueTransfer(
							initialized, member.type, selected);
					}
					value = initialized.node;
				}
				else
				{
					NodeId list = FindChild(initializer, "argument-list");
					if (list == kNoNode)
						list = FindChild(initializer, "braced-init-list");
					if (list != kNoNode)
						for (std::uint32_t edge = arena_->FirstEdge(list);
							edge != kNoEdge; edge = arena_->NextEdge(edge))
							arguments.push_back(arena_->EdgeChild(edge));
					constructor_copy = false;
				}
			}
			else if (initializer != kNoNode)
				arguments.push_back(initializer);
			if (value == kNoDumpEdge)
				value = BuildConstructorAction(member.type, scope, arguments,
					constructor_copy, constructor_list, false, true,
					constructor_list ? initializer : kNoNode);
			if (initializer != kNoNode &&
				arena_->IsTag(initializer, "paren-argument-list") &&
				arguments.empty() && dump_.nodes[value].binding != kNoBinding &&
				GetFunction(dump_.nodes[value].binding).implicit_constructor)
				dump_.nodes[value].value_initialization = true;
		}
		dump_.Add(action, value);
	}
	else if (initializer != kNoNode &&
		(arena_->IsTag(initializer, "paren-argument-list") ||
		 arena_->IsTag(initializer, "braced-init-list")))
	{
		if (arena_->IsTag(initializer, "braced-init-list"))
			dump_.Add(action,
				AnalyzeBracedInit(initializer, scope, member.type).node);
		else
		{
			const std::uint32_t list = MakeDump(DUMP_BRACED_INIT_LIST,
				member.type, VALUE_LVALUE);
			std::vector<NodeId> syntax;
			for (std::uint32_t edge = arena_->FirstEdge(initializer);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
				syntax.push_back(arena_->EdgeChild(edge));
			std::vector<ExpressionInfo> values;
			if (!ExpandCallArgumentPacks(syntax, scope, &syntax, &values))
				for (std::size_t i = 0; i < syntax.size(); ++i)
					values.push_back(AnalyzeExpression(syntax[i], scope));
			if (values.size() > 1)
				throw std::runtime_error(
					"scalar member has multiple initializers");
			for (std::size_t i = 0; i < values.size(); ++i)
				dump_.Add(list, ApplyTarget(values[i], member.type).node);
			dump_.Add(action, list);
		}
	}
	else if (initializer != kNoNode)
		dump_.Add(action, AnalyzeExpression(initializer, scope, member.type).node);
	dump_.Add(body, action);
	if (dump_.nodes[action].first_edge != kNoDumpEdge)
		AppendFullExpressionDestructionActions(
			dump_.edges[dump_.nodes[action].first_edge].child, body);
	++expression_count_;
}

void SemanticAnalyzer::RecordDelegatingConstructor(BindingId source,
	BindingId selected)
{
	const auto complete_identity = [this](BindingId binding)
	{
		binding = program_->bindings[binding].canonical;
		const FunctionInfo& function = GetFunction(binding);
		return function.complete_constructor == kNoBinding ?
			binding : function.complete_constructor;
	};
	source = complete_identity(source);
	selected = complete_identity(selected);
	FunctionInfo& source_info = GetMutableFunction(source);
	if (source_info.delegated_constructor != kNoBinding)
	{
		if (complete_identity(source_info.delegated_constructor) != selected)
			throw std::logic_error(
				"constructor has conflicting delegation facts");
		return;
	}
	BindingId cursor = selected;
	for (std::size_t depth = 0; depth <= functions_.size(); ++depth)
	{
		if (cursor == source)
			throw std::runtime_error("delegating constructor cycle");
		const BindingId next = GetFunction(cursor).delegated_constructor;
		if (next == kNoBinding)
		{
			source_info.delegated_constructor = selected;
			return;
		}
		cursor = complete_identity(next);
	}
	throw std::logic_error("cyclic constructor delegation fact graph");
}

void SemanticAnalyzer::CollectConstructorInitializers(
	const FunctionInfo& constructor, EntityId entity, ScopeId function_scope,
	std::vector<NodeId>* syntax, std::vector<ScopeId>* scopes,
	std::vector<std::uint8_t>* expanded)
{
	if (constructor.constructor_initializer == kNoNode) return;
	for (std::uint32_t edge = arena_->FirstEdge(
		constructor.constructor_initializer); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId initializer = arena_->EdgeChild(edge);
		if (!arena_->IsTag(initializer, "mem-initializer")) continue;
		if (FindChild(initializer, "pack-expansion") == kNoNode)
		{
			syntax->push_back(initializer);
			scopes->push_back(function_scope);
			expanded->push_back(0);
			continue;
		}
		std::vector<ScopeId> element_scopes;
		if (!ExpandPackElementScopes(
			initializer, function_scope, &element_scopes))
			throw std::runtime_error(
				"constructor pack expansion contains no unexpanded pack");
		for (std::size_t element = 0;
			element < element_scopes.size(); ++element)
		{
			BindLexicalTypeNames(initializer,
				program_->entities[entity].owner, element_scopes[element]);
			syntax->push_back(initializer);
			scopes->push_back(element_scopes[element]);
			expanded->push_back(1);
		}
	}
}

void SemanticAnalyzer::AddConstructorMemberActions(
	const FunctionInfo& constructor, ScopeId function_scope,
	const std::vector<BindingId>& parameters,
	std::uint32_t body)
{
	const EntityId entity = program_->bindings[constructor.binding].member_owner;
	if (entity == kNoEntity || entity >= entity_data_members_.size())
		throw std::logic_error("constructor is missing its member index");
	const std::vector<BindingId>& members = entity_data_members_[entity];
	if (constructor_initializer_scratch_.size() < members.size())
		constructor_initializer_scratch_.resize(members.size(), kNoNode);
	constructor_initializer_touched_.clear();
	const std::size_t base_count =
		program_->entities[entity].direct_base_count;
	std::vector<NodeId> base_initializers(base_count, kNoNode);
	std::vector<ScopeId> base_initializer_scopes(base_count, function_scope);
	std::vector<std::uint8_t> base_initializer_seen(base_count, 0);
	std::vector<std::uint8_t> base_initializer_expanded(base_count, 0);
	std::vector<NodeId> initializer_syntax;
	std::vector<ScopeId> initializer_scopes;
	std::vector<std::uint8_t> initializer_expanded;
	CollectConstructorInitializers(constructor, entity, function_scope,
		&initializer_syntax, &initializer_scopes, &initializer_expanded);
	const std::size_t initializer_count = initializer_syntax.size();
	if (constructor.inherited_constructor_source != kNoBinding)
	{
		const EntityId base = program_->entities[entity].direct_base;
		if (base == kNoEntity)
			throw std::logic_error("inherited constructor has no direct base");
		const BindingId source = constructor.inherited_constructor_source;
		const FunctionInfo& source_info = GetFunction(source);
		const std::uint32_t base_action = MakeDump(
			DUMP_BASE_INITIALIZER_ACTION, program_->entities[base].type,
			VALUE_NONE, program_->entities[base].identity_name);
		dump_.nodes[base_action].base_projection_count = 1;
		const std::uint32_t call = MakeDump(DUMP_CONSTRUCTOR_ACTION,
			AdaptMemberFunctionType(source), VALUE_NONE,
			source_info.display_name, source);
		if (parameters.size() != source_info.parameters.size())
			throw std::logic_error(
				"inherited constructor parameter fact mismatch");
		for (std::size_t i = 0; i < parameters.size(); ++i)
		{
			const BindingRecord& parameter = program_->bindings[parameters[i]];
			const TypeId type = EffectiveType(parameter.type);
			dump_.Add(call, MakeDump(DUMP_ID_EXPRESSION, type,
				VALUE_LVALUE, parameter.name, parameters[i]));
			++expression_count_;
		}
		DemandFunction(source);
		dump_.Add(base_action, call);
		dump_.Add(body, base_action);
		++constructor_base_action_visits_;
		++expression_count_;
	}
	else if (!initializer_syntax.empty())
	{
		for (std::size_t initializer_index = 0;
			initializer_index < initializer_syntax.size(); ++initializer_index)
		{
			const NodeId initializer = initializer_syntax[initializer_index];
			const ScopeId initializer_scope =
				initializer_scopes[initializer_index];
			const NodeId id = FindChild(initializer, "mem-initializer-id");
			if (id == kNoNode)
				throw std::runtime_error("member initializer has no target");
			const NameId name = program_->names.Intern(arena_->Payload(id));
			const LookupResult found = program_->LookupDirect(
				program_->entities[entity].member_scope, name, LOOKUP_ORDINARY);
			NodeId value = kNoNode;
			for (std::uint32_t child_edge = arena_->FirstEdge(initializer);
				child_edge != kNoEdge; child_edge = arena_->NextEdge(child_edge))
			{
				const NodeId child = arena_->EdgeChild(child_edge);
				if (child != id &&
					!arena_->IsTag(child, "pack-expansion")) value = child;
			}
			if (value == kNoNode)
				throw std::runtime_error("member initializer has no value");
			LookupResult target_type;
			const NodeId structured = FindChild(id, "structured-type-name");
			if (structured != kNoNode)
			{
				const NamePath target_path = StructuredNamePath(structured);
				if (!target_path.global && target_path.Size() == 1 &&
					program_->LookupDirect(initializer_scope, target_path.Last(),
						LOOKUP_TYPE).type == kNoType)
				{
					const LookupResult lexical = program_->LookupDirect(
						program_->entities[entity].owner,
						target_path.Last(), LOOKUP_TYPE);
					if (lexical.type != kNoType)
						program_->AddBinding(initializer_scope,
							BIND_TYPE_ALIAS, target_path.Last(), lexical.type);
				}
				target_type.type = ResolveStructuredTypeName(
					structured, initializer_scope);
			}
			else
				target_type = LookupSpelling(initializer_scope,
					arena_->Payload(id), LOOKUP_TYPE);
			if (target_type.type != kNoType &&
				EntityOf(target_type.type) == entity)
			{
				if (initializer_count != 1)
					throw std::runtime_error(
						"delegating initializer must be the only initializer");
				std::vector<NodeId> arguments;
				const bool list_initialization =
					arena_->IsTag(value, "braced-init-list");
				if (arena_->IsTag(value, "paren-argument-list") ||
					list_initialization)
					for (std::uint32_t argument_edge = arena_->FirstEdge(value);
						argument_edge != kNoEdge;
						argument_edge = arena_->NextEdge(argument_edge))
						arguments.push_back(arena_->EdgeChild(argument_edge));
				else arguments.push_back(value);
				const std::vector<NodeId> original_arguments = arguments;
				std::vector<ExpressionInfo> prepared_arguments;
				const bool expanded_arguments = ExpandCallArgumentPacks(
					original_arguments, initializer_scope, &arguments,
					&prepared_arguments);
				const TypeId owner_type = program_->entities[entity].type;
				const std::uint32_t action = MakeDump(
					DUMP_DELEGATING_INITIALIZER_ACTION, owner_type,
					VALUE_NONE, program_->entities[entity].identity_name);
				const bool base_entry =
					program_->bindings[constructor.binding].constructor_base_entry;
				const std::uint32_t delegate = BuildConstructorAction(owner_type,
					initializer_scope, arguments, false, list_initialization,
					base_entry, true,
					list_initialization ? value : kNoNode,
					expanded_arguments ? &prepared_arguments : 0);
				RecordDelegatingConstructor(
					constructor.binding, dump_.nodes[delegate].binding);
				dump_.Add(action, delegate);
				AppendFullExpressionDestructionActions(delegate, action);
				dump_.Add(body, action);
				++constructor_delegation_action_visits_;
				++expression_count_;
				return;
			}
			if (found.ordinary != kNoBinding &&
				program_->bindings[found.ordinary].non_static_data_member &&
				program_->bindings[found.ordinary].member_owner == entity)
			{
				const BindingId member = found.ordinary;
				if (program_->entities[entity].flavor == NAMED_UNION &&
					!constructor_initializer_touched_.empty())
					throw std::runtime_error(
						"union constructor initializes multiple variants");
				const std::uint32_t ordinal =
					program_->bindings[member].member_ordinal;
				if (ordinal >= members.size() || members[ordinal] != member)
					throw std::logic_error(
						"constructor member has no canonical ordinal");
				if (constructor_initializer_scratch_[ordinal] != kNoNode)
					throw std::runtime_error(
						"duplicate constructor member initializer");
				constructor_initializer_scratch_[ordinal] = value;
				constructor_initializer_touched_.push_back(member);
				continue;
			}
			if (found.ordinary != kNoBinding)
				throw std::runtime_error(
					"constructor initializer target is not a data member");
			const EntityId target_base = EntityOf(target_type.type);
			std::size_t base_ordinal = base_count;
			for (std::size_t i = 0; i < base_count; ++i)
				if (program_->DirectBase(entity, i).entity == target_base)
				{
					base_ordinal = i;
					break;
				}
			if (base_ordinal == base_count)
				throw std::runtime_error(
					"unknown constructor member initializer");
			if (target_type.type_declaration != kNoBinding &&
				!CanAccessMember(target_type.type_declaration,
					target_type.naming_class))
				throw std::runtime_error("inaccessible base initializer type");
			if (base_initializer_seen[base_ordinal])
				throw std::runtime_error("duplicate base initializer");
			base_initializers[base_ordinal] = value;
			base_initializer_scopes[base_ordinal] = initializer_scope;
			base_initializer_seen[base_ordinal] = 1;
			base_initializer_expanded[base_ordinal] =
				initializer_expanded[initializer_index];
		}
	}
	if (constructor.inherited_constructor_source == kNoBinding)
		for (std::size_t base_ordinal = 0;
			base_ordinal < base_count; ++base_ordinal)
			AddBaseInitializationAction(entity, base_ordinal,
				base_initializers[base_ordinal],
				base_initializer_scopes[base_ordinal], body,
				base_initializer_expanded[base_ordinal] != 0);
	if (program_->entities[entity].polymorphic_class)
		dump_.Add(body, MakeDump(DUMP_VPTR_INITIALIZATION_ACTION,
			program_->entities[entity].type));
	if (program_->entities[entity].flavor == NAMED_UNION)
	{
		const BindingId active = constructor_initializer_touched_.empty() ?
			program_->entities[entity].union_default_member :
			constructor_initializer_touched_[0];
		if (active != kNoBinding)
		{
			const std::uint32_t ordinal =
				program_->bindings[active].member_ordinal;
			if (ordinal >= members.size() || members[ordinal] != active)
				throw std::logic_error("union active member has no canonical ordinal");
			NodeId initializer = constructor_initializer_scratch_[ordinal];
			if (initializer == kNoNode &&
				active < member_initializer_by_binding_.size())
				initializer = member_initializer_by_binding_[active];
			AddMemberInitializationAction(
				active, initializer, function_scope, body);
			++constructor_member_action_visits_;
		}
	}
	else
	{
		for (std::size_t i = 0; i < members.size(); ++i)
		{
			++constructor_member_action_visits_;
			const BindingId member = members[i];
			NodeId initializer = constructor_initializer_scratch_[i];
			if (initializer == kNoNode &&
				member < member_initializer_by_binding_.size())
				initializer = member_initializer_by_binding_[member];
			AddMemberInitializationAction(
				member, initializer, function_scope, body);
		}
	}
	for (std::size_t i = 0; i < constructor_initializer_touched_.size(); ++i)
	{
		const BindingId member = constructor_initializer_touched_[i];
		constructor_initializer_scratch_[
			program_->bindings[member].member_ordinal] = kNoNode;
	}
}

void SemanticAnalyzer::AddBaseInitializationAction(EntityId entity,
	std::size_t base_ordinal, NodeId initializer, ScopeId scope,
	std::uint32_t body, bool pack_expanded)
{
	if (base_ordinal >= program_->entities[entity].direct_base_count)
		throw std::logic_error("base initialization has no direct base");
	const EntityId base = program_->DirectBase(entity, base_ordinal).entity;
	std::vector<NodeId> arguments;
	bool list_initialization = false;
	if (initializer != kNoNode)
	{
		list_initialization = arena_->IsTag(initializer, "braced-init-list");
		if (arena_->IsTag(initializer, "paren-argument-list") ||
			list_initialization)
		{
			for (std::uint32_t edge = arena_->FirstEdge(initializer);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
				arguments.push_back(arena_->EdgeChild(edge));
		}
		else arguments.push_back(initializer);
	}
	const TypeId base_type = program_->entities[base].type;
	const std::uint32_t action = MakeDump(DUMP_BASE_INITIALIZER_ACTION,
		base_type, VALUE_NONE, program_->entities[base].identity_name);
	dump_.nodes[action].base_projection_count = 1;
	dump_.nodes[action].direct_base_offset =
		program_->DirectBase(entity, base_ordinal).offset;
	dump_.nodes[action].has_direct_base_offset = true;
	const std::uint32_t constructor = BuildConstructorAction(base_type,
		scope, arguments, false, list_initialization, true, true,
		list_initialization ? initializer : kNoNode);
	if (dump_.nodes[constructor].kind == DUMP_CONSTRUCTOR_ACTION &&
		dump_.nodes[constructor].binding != kNoBinding)
	{
		const FunctionInfo& selected =
			GetFunction(dump_.nodes[constructor].binding);
		const EntityId selected_owner =
			program_->bindings[selected.binding].member_owner;
		const bool demanded_template_base = !pack_expanded &&
			initializer != kNoNode && !arguments.empty() &&
			IsClassTemplateSpecializationEntity(selected_owner);
		const bool demanded_constexpr_vptr_base =
			selected_owner != kNoEntity && selected.constexpr_function &&
			program_->entities[selected_owner].polymorphic_class;
		if ((demanded_template_base || demanded_constexpr_vptr_base) &&
			!selected.implicit_constructor && !selected.defaulted_constructor &&
			selected.complete_constructor != kNoBinding)
			DemandFunction(selected.complete_constructor);
	}
	dump_.Add(action, constructor);
	dump_.Add(body, action);
	AppendFullExpressionDestructionActions(constructor, action);
	++constructor_base_action_visits_;
	++expression_count_;
}

bool SemanticAnalyzer::InitializationActionsAreNonthrowing(
	std::uint32_t body) const
{
	std::vector<std::uint32_t> pending(1, body);
	while (!pending.empty())
	{
		const std::uint32_t node = pending.back();
		pending.pop_back();
		++nonthrowing_action_visits_;
		const DumpNode& record = dump_.nodes[node];
		if (record.kind == DUMP_CONSTRUCTOR_ACTION)
		{
			if (record.binding == kNoBinding ||
				!program_->bindings[record.binding].nonthrowing)
				return false;
		}
		else if (record.kind == DUMP_TEMPORARY_OBJECT)
		{
			const EntityId entity = DestructedEntity(record.type);
			if (entity != kNoEntity &&
				!program_->entities[entity].trivial_destructor)
			{
				const BindingId destructor = DestructorForType(record.type);
				if (destructor == kNoBinding ||
					!program_->bindings[destructor].nonthrowing)
					return false;
			}
		}
		else if (record.kind == DUMP_DESTRUCTOR_ACTION)
		{
			if (record.binding == kNoBinding ||
				!program_->bindings[record.binding].nonthrowing)
				return false;
		}
		else if (record.kind == DUMP_CALL_EXPRESSION)
		{
			bool known_nonthrowing = record.binding != kNoBinding &&
				program_->bindings[record.binding].nonthrowing;
			for (std::uint32_t edge = record.first_edge; edge != kNoDumpEdge;
				edge = dump_.edges[edge].next)
			{
				const DumpNode& child = dump_.nodes[dump_.edges[edge].child];
				if (child.kind == DUMP_CALLEE && child.binding != kNoBinding &&
					program_->bindings[child.binding].nonthrowing)
					known_nonthrowing = true;
			}
			if (!known_nonthrowing) return false;
		}
		else if (record.kind == DUMP_DELETE_EXPRESSION)
		{
			if (record.binding == kNoBinding ||
				!program_->bindings[record.binding].nonthrowing)
				return false;
			if (record.selected_binding != kNoBinding &&
				!program_->bindings[record.selected_binding].nonthrowing)
				return false;
		}
		for (std::uint32_t edge = record.first_edge; edge != kNoDumpEdge;
			edge = dump_.edges[edge].next)
			pending.push_back(dump_.edges[edge].child);
	}
	return true;
}

ExpressionInfo SemanticAnalyzer::AnalyzeBracedInit(NodeId node, ScopeId scope,
	TypeId target)
{
	if (target == kNoType) throw std::runtime_error("untyped braced-init-list");
	ExpressionInfo expanded;
	if (TryAnalyzeExpandedBracedInit(node, scope, target, &expanded))
		return expanded;
	TypeId type = target;
	const TypeRecord array = program_->types.Get(
		program_->types.RemoveTopCv(type));
	const EntityId class_entity = EntityOf(type);
	if (IsClassEntity(*program_, class_entity))
	{
		if (!program_->entities[class_entity].is_aggregate)
		{
			std::vector<NodeId> arguments;
			for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
				edge = arena_->NextEdge(edge))
				arguments.push_back(arena_->EdgeChild(edge));
			ExpressionInfo result;
			result.node = BuildConstructorAction(type, scope, arguments,
				false, true, false, true, node);
			result.type = type;
			result.category = VALUE_NONE;
			SetExpressionDumpObject(&result);
			return result;
		}
		std::uint32_t element_edge = arena_->FirstEdge(node);
		ExpressionInfo result = AnalyzeAggregateInit(type, scope, &element_edge);
		if (element_edge != kNoEdge)
			throw std::runtime_error("excess aggregate initializer elements");
		return result;
	}
	if (array.kind == TYPE_ARRAY)
	{
		std::uint32_t element_edge = arena_->FirstEdge(node);
		ExpressionInfo result = AnalyzeArrayAggregateInit(type, scope,
			&element_edge);
		if (element_edge != kNoEdge)
			throw std::runtime_error("excess array initializer elements");
		return result;
	}
	TypeId element = type;
	std::vector<ExpressionInfo> values;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		ExpressionInfo value = AnalyzeExpression(arena_->EdgeChild(edge), scope,
			element);
		if (IsIntegral(element, true) && IsArithmetic(value.type) &&
			!IsIntegral(value.type, true))
			throw std::runtime_error("narrowing list-initialization conversion");
		values.push_back(value);
	}
	const std::uint32_t list = MakeDump(DUMP_BRACED_INIT_LIST, type,
		VALUE_LVALUE);
	for (std::size_t i = 0; i < values.size(); ++i) dump_.Add(list, values[i].node);
	ExpressionInfo result;
	result.node = list;
	result.type = type;
	result.category = VALUE_LVALUE;
	RecordExpressionFacts(result);
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeAggregateElement(TypeId type,
	ScopeId scope, std::uint32_t* element_edge)
{
	const TypeId object = program_->types.RemoveTopCv(type);
	const TypeRecord record = program_->types.Get(object);
	const EntityId entity = EntityOf(type);
	const bool class_type = record.kind == TYPE_NAMED &&
		IsClassEntity(*program_, entity);
	const bool class_aggregate = class_type &&
		program_->entities[entity].is_aggregate;
	if (*element_edge != kNoEdge)
	{
		const std::uint32_t source_edge = *element_edge;
		const NodeId source = arena_->EdgeChild(source_edge);
		if (record.kind == TYPE_ARRAY && arena_->IsTag(source, "literal") &&
			!arena_->Payload(source).empty() &&
			arena_->Payload(source).find('"') != std::string::npos)
		{
			const TypeId element = program_->types.RemoveTopCv(record.child);
			const TypeRecord element_record = program_->types.Get(element);
			if (element_record.kind != TYPE_FUNDAMENTAL ||
				element_record.fundamental != FUND_CHAR)
				throw std::runtime_error(
					"string literal initializes a non-character array");
			const std::vector<unsigned char> bytes =
				DecodeStringInitializer(arena_->Payload(source));
			if (record.bound != 0 && bytes.size() > record.bound)
				throw std::runtime_error("string literal is too long for array");
			TypeId initialized_type = type;
			if (record.bound == 0)
				initialized_type = program_->types.Array(record.child, bytes.size());
			const std::uint32_t list = MakeDump(DUMP_BRACED_INIT_LIST,
				initialized_type, VALUE_LVALUE);
			std::vector<ConstexprObjectElement> constant_elements;
			const std::size_t element_count = record.bound == 0 ? bytes.size() :
				static_cast<std::size_t>(record.bound);
			constant_elements.reserve(element_count);
			for (std::size_t i = 0; i < bytes.size(); ++i)
			{
				ExpressionInfo value = MakeLiteral(record.child,
					InternNumber(bytes[i]));
				value.constant = true;
				value.value = bytes[i];
				dump_.nodes[value.node].constant = true;
				dump_.nodes[value.node].constant_value = bytes[i];
				dump_.Add(list, value.node);
				constant_elements.push_back(ConstexprObjectElement(kNoBinding,
					NormalizeScalarConstant(record.child,
						ConstexprScalarValue(static_cast<std::int64_t>(bytes[i])))));
			}
			while (constant_elements.size() < element_count)
				constant_elements.push_back(ConstexprObjectElement(kNoBinding,
					NormalizeScalarConstant(record.child,
						ConstexprScalarValue(static_cast<std::int64_t>(0)))));
			*element_edge = arena_->NextEdge(source_edge);
			ExpressionInfo result;
			result.node = list;
			result.type = initialized_type;
			result.category = VALUE_LVALUE;
			SetExpressionObject(&result,
				InternConstexprObject(initialized_type, constant_elements));
			++expression_count_;
			return result;
		}
		if (arena_->IsTag(source, "braced-init-list"))
		{
			*element_edge = arena_->NextEdge(source_edge);
			return AnalyzeBracedInit(source, scope, type);
		}
		if (record.kind == TYPE_ARRAY)
			return AnalyzeArrayAggregateInit(type, scope, element_edge);
		if (class_aggregate)
		{
			if (!braced_initialization_context_)
				return AnalyzePreparedAggregateElement(
					type, scope, element_edge);
			const ExpressionInfo expression = AnalyzeExpression(source, scope);
			const CallConversionFact conversion =
				PreparedAggregateElementConversion(source, type, expression);
			const bool has_elements = entity < entity_data_members_.size() &&
				!entity_data_members_[entity].empty();
			if (conversion.rank == CONVERSION_INVALID && has_elements)
				return AnalyzeAggregateDescent(type, scope, element_edge);
		}
		*element_edge = arena_->NextEdge(source_edge);
		if (class_type)
		{
			if (arena_->IsTag(source, "call-expression"))
			{
				ExpressionInfo constructed =
					AnalyzeExpression(source, scope, type);
				if (program_->types.RemoveTopCv(constructed.type) == object &&
					(dump_.nodes[constructed.node].kind ==
						DUMP_CONSTRUCTOR_ACTION ||
					 dump_.nodes[constructed.node].kind ==
						DUMP_BRACED_INIT_LIST))
				{
					if (dump_.nodes[constructed.node].kind ==
						DUMP_CONSTRUCTOR_ACTION &&
						dump_.nodes[constructed.node].binding != kNoBinding)
					{
						const FunctionInfo& constructor = GetFunction(
							dump_.nodes[constructed.node].binding);
						if (program_->entities[entity].direct_base == kNoEntity &&
							entity < entity_data_members_.size() &&
							entity_data_members_[entity].empty() &&
							constructor.constructor_initializer == kNoNode &&
							constructor.definition_body != kNoNode &&
							FirstSemanticChild(constructor.definition_body) == kNoNode)
							dump_.nodes[constructed.node].elide_empty_constructor = true;
					}
					return constructed;
				}
			}
			std::vector<NodeId> arguments(1, source);
			ExpressionInfo result;
			result.node = BuildConstructorAction(type, scope, arguments,
				true, false);
			result.type = type;
			result.category = VALUE_NONE;
			return result;
		}
		return AnalyzeExpression(source, scope, type);
	}
	if (record.kind == TYPE_ARRAY)
		return AnalyzeArrayAggregateInit(type, scope, element_edge);
	if (class_type)
	{
		if (class_aggregate)
			return AnalyzeAggregateInit(type, scope, element_edge);
		ExpressionInfo result;
		result.node = BuildDefaultConstructorAction(type, scope);
		result.type = type;
		result.category = VALUE_NONE;
		return result;
	}
	if (record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE)
		throw std::runtime_error("omitted aggregate reference member");
	ExpressionInfo omitted;
	omitted.type = type;
	omitted.category = VALUE_NONE;
	return omitted;
}

std::uint32_t SemanticAnalyzer::BuildAggregateConstructionAction(TypeId type,
	std::uint32_t aggregate_list, bool allow_array_members)
{
	const EntityId entity = EntityOf(type);
	if (!IsClassEntity(*program_, entity) ||
		!program_->entities[entity].is_aggregate)
		throw std::logic_error("aggregate helper has non-aggregate type");
	std::vector<std::uint32_t> values;
	std::vector<BindingId> members;
	std::vector<BindingId> member_constructors;
	std::vector<std::uint8_t> trivial_member_constructors;
	std::vector<TypeId> parameter_types;
	for (std::uint32_t edge = dump_.nodes[aggregate_list].first_edge;
		edge != kNoDumpEdge; edge = dump_.edges[edge].next)
	{
		const std::uint32_t action_node = dump_.edges[edge].child;
		const DumpNode& action = dump_.nodes[action_node];
		if (action.kind != DUMP_INITIALIZER_ACTION ||
			action.binding == kNoBinding)
			throw std::logic_error("aggregate helper has invalid member action");
		if (action.first_edge == kNoDumpEdge) return aggregate_list;
		if (dump_.edges[action.first_edge].next != kNoDumpEdge)
			throw std::runtime_error(
				"aggregate helper member has multiple values");
		const std::uint32_t value = dump_.edges[action.first_edge].child;
		const TypeKind kind = program_->types.Get(
			program_->types.RemoveTopCv(action.type)).kind;
		if (kind == TYPE_ARRAY && !allow_array_members)
			return aggregate_list;
		if (kind == TYPE_ARRAY && allow_array_members)
		{
			std::vector<std::uint32_t> pending(1, value);
			while (!pending.empty())
			{
				const std::uint32_t current = pending.back();
				pending.pop_back();
				if (dump_.nodes[current].kind == DUMP_CONSTRUCTOR_ACTION)
					dump_.nodes[current].elide_empty_constructor = false;
				for (std::uint32_t child = dump_.nodes[current].first_edge;
					child != kNoDumpEdge; child = dump_.edges[child].next)
					pending.push_back(dump_.edges[child].child);
			}
		}
		members.push_back(action.binding);
		const TypeId adjusted = AdjustParameterType(action.type);
		parameter_types.push_back(adjusted);
		ExpressionInfo argument;
		argument.node = value;
		argument.type = dump_.nodes[value].type;
		argument.category = dump_.nodes[value].category;
		argument.binding = dump_.nodes[value].binding;
		if (IsClassEntity(*program_, EntityOf(action.type)))
		{
			argument.type = action.type;
			argument.category = VALUE_PRVALUE;
		}
		values.push_back(IsClassEntity(*program_, EntityOf(action.type)) ?
			ApplyCallArgument(argument, adjusted).node : value);
		BindingId constructor = kNoBinding;
		if (IsClassEntity(*program_, EntityOf(action.type)))
		{
			constructor = ConstructorForSubobject(
				action.type, SPECIAL_MEMBER_MOVE_CONSTRUCTOR);
			if (constructor == kNoBinding ||
				GetFunction(constructor).deleted_constructor ||
				GetFunction(constructor).deleted_special_member)
				throw std::runtime_error(
					"aggregate member has no usable value constructor");
			if (!GetFunction(constructor).trivial_special_member)
				DemandFunction(constructor);
		}
		member_constructors.push_back(constructor);
		trivial_member_constructors.push_back(constructor != kNoBinding &&
			GetFunction(constructor).trivial_special_member ? 1 : 0);
	}
	if (members.empty()) return aggregate_list;
	const EntityRecord& owner = program_->entities[entity];
	std::vector<TypeId> boundary_types;
	boundary_types.reserve(parameter_types.size() + 1);
	boundary_types.push_back(program_->types.Pointer(type));
	boundary_types.insert(boundary_types.end(), parameter_types.begin(),
		parameter_types.end());
	const TypeId function_type = program_->types.Function(
		program_->types.Fundamental(FUND_VOID), boundary_types, false);
	const FunctionSignatureKey key(owner.member_scope, owner.identity_name,
		function_type);
	BindingId encoded = aggregate_helper_index_.Find(key);
	std::uint32_t helper = kNoDumpEdge;
	if (encoded == kNoBinding)
	{
		if (aggregate_helpers_.size() >= kNoDumpEdge)
			throw std::runtime_error("too many aggregate helper identities");
		helper = static_cast<std::uint32_t>(aggregate_helpers_.size());
		aggregate_helpers_.push_back(AggregateHelperInfo(
			entity, type, function_type, members, member_constructors,
			trivial_member_constructors));
		aggregate_helper_index_.Insert(key,
			static_cast<BindingId>(helper));
	}
	else
	{
		helper = static_cast<std::uint32_t>(encoded);
		if (helper >= aggregate_helpers_.size() ||
			aggregate_helpers_[helper].members != members ||
			aggregate_helpers_[helper].member_constructors !=
				member_constructors ||
			aggregate_helpers_[helper].trivial_member_constructors !=
				trivial_member_constructors)
			throw std::logic_error("aggregate helper identity collision");
	}
	const std::uint32_t call = MakeDump(
		DUMP_AGGREGATE_CONSTRUCTION_ACTION, type, VALUE_NONE);
	dump_.nodes[call].aggregate_helper = helper;
	for (std::size_t i = 0; i < values.size(); ++i)
		dump_.Add(call, values[i]);
	++expression_count_;
	return call;
}

ExpressionInfo SemanticAnalyzer::BuildLocalAggregateArrayActions(
	const ExpressionInfo& initializer)
{
	const TypeRecord array = program_->types.Get(
		program_->types.RemoveTopCv(initializer.type));
	const EntityId element = array.kind == TYPE_ARRAY ?
		EntityOf(array.child) : kNoEntity;
	if (array.kind != TYPE_ARRAY || !IsClassEntity(*program_, element) ||
		!program_->entities[element].is_aggregate ||
		dump_.nodes[initializer.node].kind != DUMP_BRACED_INIT_LIST)
		return initializer;
	for (std::uint32_t edge = dump_.nodes[initializer.node].first_edge;
		edge != kNoDumpEdge; edge = dump_.edges[edge].next)
	{
		const std::uint32_t element_node = dump_.edges[edge].child;
		if (dump_.nodes[element_node].kind != DUMP_BRACED_INIT_LIST)
			throw std::logic_error(
				"aggregate array element has no action list");
		const std::uint32_t replacement =
			BuildAggregateConstructionAction(array.child, element_node);
		dump_.edges[edge].child = replacement;
	}
	return initializer;
}

BindingId SemanticAnalyzer::SelectUsualDeallocation(ScopeId scope,
	EntityId entity, bool explicit_global, bool array, TypeId object_type)
{
	const bool class_object = IsClassEntity(*program_, entity);
	const char* spelling = array ? "operatordelete[]" : "operatordelete";
	std::vector<BindingId> candidates;
	EntityId naming_class = kNoEntity;
	if (!explicit_global && class_object)
	{
		const LookupResult member = program_->LookupMember(entity,
			program_->names.Intern(spelling), LOOKUP_ORDINARY);
		if (member.ordinary != kNoBinding &&
			program_->bindings[member.ordinary].kind == BIND_FUNCTION)
		{
			candidates = FunctionSet(member.ordinary);
			naming_class = member.naming_class;
		}
	}
	if (candidates.empty())
	{
		(void)EnsureBuiltinFunction(array ?
			BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY :
			BUILTIN_FUNCTION_OPERATOR_DELETE);
		candidates = FunctionCandidates(program_->GlobalScope(), spelling);
	}
	std::vector<BindingId> unsized;
	std::vector<BindingId> sized;
	const TypeId size_type =
		program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		const TypeId function_type = GetFunction(candidates[i]).type;
		const TypeRecord& function = program_->types.Get(function_type);
		const TypeId* parameters = program_->types.Parameters(function_type);
		if (function.parameter_count == 1) unsized.push_back(candidates[i]);
		else if (function.parameter_count == 2 &&
			program_->types.RemoveTopCv(parameters[1]) == size_type)
			sized.push_back(candidates[i]);
	}
	std::vector<BindingId>& usual = unsized.empty() ? sized : unsized;
	if (usual.empty()) throw std::runtime_error("no usual deallocation function");
	ExpressionInfo pointer_argument;
	pointer_argument.type = program_->types.Pointer(
		program_->types.Fundamental(FUND_VOID));
	std::vector<NodeId> syntax(1, kNoNode);
	std::vector<ExpressionInfo> arguments(1, pointer_argument);
	if (unsized.empty())
	{
		ExpressionInfo size = MakeLiteral(size_type,
			InternNumber(static_cast<std::int64_t>(program_->SizeOf(object_type))));
		size.constant = true;
		size.value = static_cast<std::int64_t>(program_->SizeOf(object_type));
		dump_.nodes[size.node].constant = true;
		dump_.nodes[size.node].constant_value = size.value;
		syntax.push_back(kNoNode);
		arguments.push_back(size);
	}
	const BindingId selected = SelectOverload(scope, syntax, arguments,
		usual, 0, 0, 0);
	if (!CanAccessMember(selected, naming_class, entity))
		throw std::runtime_error("inaccessible deallocation function");
	DemandFunction(selected);
	return selected;
}

ExpressionInfo SemanticAnalyzer::AnalyzeArrayNewExpression(NodeId node,
	NodeId type_node, ScopeId scope, TypeId target)
{
	const NodeId specifiers = FindChild(type_node, "type-specifier-seq");
	const SpecInfo spec = BuildSpecifiers(
		specifiers, scope, std::string(), false);
	const NodeId declarator = FindChild(type_node, "abstract-declarator");
	if (declarator == kNoNode)
		throw std::logic_error("array new has no abstract declarator");
	TypeId leaf_type = spec.type;
	std::vector<NodeId> suffixes;
	bool value_initialization = false;
	for (std::uint32_t edge = arena_->FirstEdge(declarator); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, "array-suffix")) suffixes.push_back(child);
		else if (arena_->IsTag(child, "parameter-clause"))
		{
			if (FirstSemanticChild(child) != kNoNode)
				throw std::runtime_error("array new initializer has parameters");
			value_initialization = true;
		}
		else if (arena_->IsTag(child, "ptr-operator"))
		{
			if (PayloadSource(child) != "*")
				throw std::runtime_error("unsupported array new declarator");
			leaf_type = program_->types.Pointer(leaf_type);
		}
		else if (arena_->IsTag(child, "cv-qualifier"))
			leaf_type = program_->types.Qualify(leaf_type,
				PayloadSource(child) == "const" ? CV_CONST : CV_VOLATILE);
	}
	if (suffixes.empty())
		throw std::logic_error("array new has no array suffix");
	TypeId result_element_type = leaf_type;
	for (std::size_t i = suffixes.size(); i != 1; --i)
	{
		const NodeId bound_node = FirstSemanticChild(suffixes[i - 1]);
		const ExpressionInfo bound = AnalyzeExpression(bound_node, scope);
		if (!bound.constant || bound.value <= 0)
			throw std::runtime_error("invalid inner array bound");
		result_element_type = program_->types.Array(result_element_type,
			static_cast<std::uint64_t>(bound.value));
	}
	const NodeId extent_syntax = FirstSemanticChild(suffixes[0]);
	if (extent_syntax == kNoNode)
		throw std::runtime_error("array new has no extent");
	ExpressionInfo extent = AnalyzeExpression(extent_syntax, scope);
	if (!IsIntegral(extent.type) || (extent.constant && extent.value < 0))
		throw std::runtime_error("invalid array new extent");
	const EntityId entity = EntityOf(leaf_type);
	const bool class_elements = IsClassEntity(*program_, entity);
	const std::uint64_t cookie_size = class_elements ? 8 : 0;
	const std::uint64_t row_size = program_->SizeOf(result_element_type);
	const std::uint64_t leaf_size = program_->SizeOf(leaf_type);
	if (leaf_size == 0 || row_size % leaf_size != 0)
		throw std::logic_error("invalid array element stride");
	ExpressionInfo allocation_size = extent;
	std::uint64_t flat_count = 0;
	if (extent.constant)
	{
		const std::uint64_t count = static_cast<std::uint64_t>(extent.value);
		if (count > (std::numeric_limits<std::uint64_t>::max() - cookie_size) /
			row_size)
			throw std::runtime_error("array allocation size overflow");
		const std::uint64_t bytes = count * row_size + cookie_size;
		const std::uint64_t inner_count = row_size / leaf_size;
		if (bytes > static_cast<std::uint64_t>(
			std::numeric_limits<std::int64_t>::max()) ||
			count > std::numeric_limits<std::uint64_t>::max() / inner_count)
			throw std::runtime_error("array allocation exceeds PA17 limits");
		flat_count = count * inner_count;
		allocation_size = MakeLiteral(extent.type,
			InternNumber(static_cast<std::int64_t>(bytes)));
		allocation_size.constant = true;
		allocation_size.value = static_cast<std::int64_t>(bytes);
		dump_.nodes[allocation_size.node].constant = true;
		dump_.nodes[allocation_size.node].constant_value = allocation_size.value;
	}
	else
	{
		const auto combine = [this](const ExpressionInfo& left,
			std::uint64_t right_value, const char* operation) -> ExpressionInfo
		{
			ExpressionInfo right = MakeLiteral(
				program_->types.Fundamental(FUND_INT),
				InternNumber(static_cast<std::int64_t>(right_value)));
			right.constant = true;
			right.value = static_cast<std::int64_t>(right_value);
			dump_.nodes[right.node].constant = true;
			dump_.nodes[right.node].constant_value = right.value;
			const TypeId arithmetic = CommonArithmeticType(left.type, right.type);
			const ExpressionInfo converted_left = ApplyTarget(left, arithmetic);
			const ExpressionInfo converted_right = ApplyTarget(right, arithmetic);
			const std::uint32_t expression = MakeDump(DUMP_BINARY_EXPRESSION,
				arithmetic, VALUE_PRVALUE, program_->names.Intern(operation));
			dump_.nodes[expression].operand_type = arithmetic;
			dump_.Add(expression, converted_left.node);
			dump_.Add(expression, converted_right.node);
			ExpressionInfo result;
			result.node = expression;
			result.type = arithmetic;
			result.category = VALUE_PRVALUE;
			++expression_count_;
			return result;
		};
		if (row_size != 1)
			allocation_size = combine(allocation_size, row_size, "*");
		if (cookie_size != 0)
			allocation_size = combine(allocation_size, cookie_size, "+");
	}
	std::vector<NodeId> argument_syntax(1, kNoNode);
	std::vector<ExpressionInfo> arguments(1, allocation_size);
	const bool explicit_global = FindChild(node, "global-scope") != kNoNode;
	std::vector<BindingId> candidates;
	EntityId naming_class = kNoEntity;
	if (!explicit_global && class_elements)
	{
		const LookupResult member = program_->LookupMember(entity,
			program_->names.Intern("operatornew[]"), LOOKUP_ORDINARY);
		if (member.ordinary != kNoBinding &&
			program_->bindings[member.ordinary].kind == BIND_FUNCTION)
		{
			candidates = FunctionSet(member.ordinary);
			naming_class = member.naming_class;
		}
	}
	if (candidates.empty())
	{
		(void)EnsureBuiltinFunction(BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY);
		candidates = FunctionCandidates(
			program_->GlobalScope(), "operatornew[]");
	}
	std::vector<CallConversionFact> conversions;
	const BindingId selected = SelectOverload(scope, argument_syntax,
		arguments, candidates, 0, 0, &conversions);
	const ExpressionInfo allocation = BuildResolvedCall(selected, scope,
		argument_syntax, arguments, 0, kNoType, naming_class, 0, &conversions);
	std::uint32_t construction = kNoDumpEdge;
	BindingId destructor = kNoBinding;
	if (class_elements)
	{
		const std::vector<NodeId> no_arguments;
		const std::uint32_t action = BuildConstructorAction(
			leaf_type, scope, no_arguments, false, false, false, false);
		const FunctionInfo& constructor = GetFunction(dump_.nodes[action].binding);
		std::vector<BindingId> empty_base_entries;
		const bool empty = EmptyDefaultConstructorChain(
			dump_.nodes[action].binding, &empty_base_entries) &&
			empty_base_entries.empty();
		if (!empty && !dump_.nodes[action].elide_empty_constructor &&
			!(constructor.implicit_constructor &&
			 program_->entities[entity].trivial_default_constructor))
		{
			construction = action;
			if (!dump_.nodes[action].trivial_special_member_action)
				DemandFunction(dump_.nodes[action].binding);
		}
		if (!program_->entities[entity].trivial_destructor)
		{
			destructor = DestructorForType(leaf_type);
			if (destructor == kNoBinding ||
				GetFunction(destructor).deleted_destructor)
				throw std::runtime_error("array element is not destructible");
			DemandFunction(destructor);
		}
	}
	const BindingId cleanup = construction == kNoDumpEdge ? kNoBinding :
		SelectUsualDeallocation(
			scope, entity, explicit_global, true, leaf_type);
	const TypeId result_type = program_->types.Pointer(result_element_type);
	const std::uint32_t result_node = MakeDump(DUMP_NEW_EXPRESSION,
		result_type, VALUE_PRVALUE, 0, selected);
	DumpNode& result_record = dump_.nodes[result_node];
	result_record.operand_type = leaf_type;
	result_record.array_action = true;
	result_record.array_cookie = cookie_size != 0;
	result_record.value_initialization = value_initialization;
	result_record.selected_binding = destructor;
	result_record.object_binding = cleanup;
	result_record.array_count_constant = extent.constant;
	result_record.array_count = flat_count;
	dump_.Add(result_node, allocation.node);
	if (construction != kNoDumpEdge) dump_.Add(result_node, construction);
	ExpressionInfo result;
	result.node = result_node;
	result.type = result_type;
	result.category = VALUE_PRVALUE;
	++expression_count_;
	return ApplyTarget(result, target);
}

ExpressionInfo SemanticAnalyzer::AnalyzeNewExpression(NodeId node,
	ScopeId scope, TypeId target)
{
	const NodeId type_node = FindChild(node, "type-id");
	if (type_node == kNoNode)
		throw std::runtime_error("new-expression has no allocated type");
	const NodeId new_declarator = FindChild(type_node, "abstract-declarator");
	if (new_declarator != kNoNode &&
		FindChild(new_declarator, "array-suffix") != kNoNode)
		return AnalyzeArrayNewExpression(node, type_node, scope, target);
	TypeId object_type = BuildTypeId(type_node, scope);
	bool parsed_empty_initializer = false;
	const TypeRecord parsed_object = program_->types.Get(
		program_->types.RemoveTopCv(object_type));
	if (parsed_object.kind == TYPE_FUNCTION &&
		parsed_object.parameter_count == 0)
	{
		object_type = parsed_object.child;
		parsed_empty_initializer = true;
	}
	if (program_->types.Get(program_->types.RemoveTopCv(object_type)).kind ==
		TYPE_ARRAY)
		throw std::runtime_error("array new is outside PA16");
	const NodeId placement = FindChild(node, "placement");
	const NodeId placement_arguments = placement == kNoNode ? kNoNode :
		FindChild(placement, "paren-argument-list");
	std::vector<NodeId> argument_syntax;
	std::vector<ExpressionInfo> arguments;
	ExpressionInfo size = MakeLiteral(program_->types.Fundamental(FUND_INT),
		InternNumber(static_cast<std::int64_t>(program_->SizeOf(object_type))));
	size.constant = true;
	size.value = static_cast<std::int64_t>(program_->SizeOf(object_type));
	dump_.nodes[size.node].constant = true;
	dump_.nodes[size.node].constant_value = size.value;
	argument_syntax.push_back(kNoNode);
	arguments.push_back(size);
	if (placement_arguments != kNoNode)
		for (std::uint32_t edge = arena_->FirstEdge(placement_arguments);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const NodeId argument = arena_->EdgeChild(edge);
			argument_syntax.push_back(argument);
			arguments.push_back(AnalyzeExpression(argument, scope));
		}
	const EntityId entity = EntityOf(object_type);
	const bool explicit_global = FindChild(node, "global-scope") != kNoNode;
	std::vector<BindingId> candidates;
	EntityId naming_class = kNoEntity;
	if (!explicit_global && IsClassEntity(*program_, entity))
	{
		const LookupResult member = program_->LookupMember(entity,
			program_->names.Intern("operatornew"), LOOKUP_ORDINARY);
		if (member.ordinary != kNoBinding &&
			program_->bindings[member.ordinary].kind == BIND_FUNCTION)
		{
			candidates = FunctionSet(member.ordinary);
			naming_class = member.naming_class;
		}
	}
	if (candidates.empty())
	{
		(void)EnsureBuiltinFunction(BUILTIN_FUNCTION_OPERATOR_NEW);
		candidates = FunctionCandidates(program_->GlobalScope(), "operatornew");
	}
	if (candidates.empty())
		throw std::runtime_error("operator new was not declared");
	std::vector<CallConversionFact> argument_conversions;
	const BindingId selected = SelectOverload(scope, argument_syntax,
		arguments, candidates, 0, 0, &argument_conversions);
	ExpressionInfo allocation = BuildResolvedCall(selected, scope,
		argument_syntax, arguments, 0, kNoType, naming_class, 0,
		&argument_conversions);
	const NodeId initializer_node = FindChild(node, "initializer");
	NodeId initializer = initializer_node == kNoNode ? kNoNode :
		FirstSemanticChild(initializer_node);
	std::uint32_t construction = kNoDumpEdge;
	if (IsClassEntity(*program_, entity))
	{
		if (initializer != kNoNode &&
			arena_->IsTag(initializer, "braced-init-list") &&
			program_->entities[entity].is_aggregate)
		{
			const ExpressionInfo aggregate = AnalyzeBracedInit(
				initializer, scope, object_type);
			construction = BuildAggregateConstructionAction(
				object_type, aggregate.node);
		}
		else
		{
			std::vector<NodeId> constructor_arguments;
			std::vector<ExpressionInfo> prepared_constructor_arguments;
			const bool list = initializer != kNoNode &&
				arena_->IsTag(initializer, "braced-init-list");
			if (initializer != kNoNode &&
				(arena_->IsTag(initializer, "paren-initializer") || list))
				for (std::uint32_t edge = arena_->FirstEdge(initializer);
					edge != kNoEdge; edge = arena_->NextEdge(edge))
					constructor_arguments.push_back(arena_->EdgeChild(edge));
			const std::vector<NodeId> original_constructor_arguments =
				constructor_arguments;
			const bool expanded_constructor_arguments = ExpandCallArgumentPacks(
				original_constructor_arguments, scope, &constructor_arguments,
				&prepared_constructor_arguments);
			construction = BuildConstructorAction(object_type, scope,
				constructor_arguments, false, list, false, true,
				list ? initializer : kNoNode,
				expanded_constructor_arguments ?
					&prepared_constructor_arguments : 0);
			const DumpNode& action = dump_.nodes[construction];
			if (action.binding != kNoBinding &&
				GetFunction(action.binding).implicit_constructor &&
				program_->entities[entity].trivial_default_constructor)
				DemandFunction(action.binding);
		}
	}
	else if (initializer != kNoNode || parsed_empty_initializer)
	{
		if (initializer == kNoNode)
		{
			ExpressionInfo zero = MakeLiteral(object_type,
				program_->names.Intern("0"));
			zero.constant = true;
			zero.value = 0;
			construction = zero.node;
		}
		else if (arena_->IsTag(initializer, "paren-initializer"))
		{
			std::vector<NodeId> scalar_syntax;
			for (std::uint32_t edge = arena_->FirstEdge(initializer);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
				scalar_syntax.push_back(arena_->EdgeChild(edge));
			std::vector<NodeId> expanded_syntax;
			std::vector<ExpressionInfo> expanded_values;
			if (ExpandCallArgumentPacks(scalar_syntax, scope,
				&expanded_syntax, &expanded_values))
			{
				if (expanded_values.size() > 1)
					throw std::runtime_error(
						"scalar new has multiple initializers");
				if (expanded_values.empty())
				{
					ExpressionInfo zero = MakeLiteral(object_type,
						program_->names.Intern("0"));
					zero.constant = true;
					zero.value = 0;
					construction = zero.node;
				}
				else construction = ApplyTarget(
					expanded_values[0], object_type).node;
			}
			else
			{
			const std::uint32_t first = arena_->FirstEdge(initializer);
			if (first == kNoEdge)
			{
				ExpressionInfo zero = MakeLiteral(object_type,
					program_->names.Intern("0"));
				zero.constant = true;
				zero.value = 0;
				construction = zero.node;
			}
			else
			{
				if (arena_->NextEdge(first) != kNoEdge)
					throw std::runtime_error(
						"scalar new has multiple initializers");
				construction = AnalyzeExpression(arena_->EdgeChild(first),
					scope, object_type).node;
			}
			}
		}
		else construction = AnalyzeExpression(
			initializer, scope, object_type).node;
	}
	const TypeId result_type = program_->types.Pointer(object_type);
	const std::uint32_t result_node = MakeDump(DUMP_NEW_EXPRESSION,
		result_type, VALUE_PRVALUE, 0, selected);
	dump_.nodes[result_node].operand_type = object_type;
	const FunctionInfo& allocation_function = GetFunction(selected);
	const TypeRecord& allocation_type =
		program_->types.Get(allocation_function.type);
	bool nonallocating_placement = false;
	if (allocation_type.parameter_count == 2)
	{
		const TypeId placement_type = program_->types.RemoveTopCv(EffectiveType(
			program_->types.Parameters(allocation_function.type)[1]));
		const TypeRecord& placement = program_->types.Get(placement_type);
		nonallocating_placement = placement.kind == TYPE_POINTER &&
			IsVoid(placement.child);
	}
	dump_.nodes[result_node].allocation_may_return_null =
		program_->bindings[selected].nonthrowing && !nonallocating_placement;
	dump_.Add(result_node, allocation.node);
	if (construction != kNoDumpEdge) dump_.Add(result_node, construction);
	ExpressionInfo result;
	result.node = result_node;
	result.type = result_type;
	result.category = VALUE_PRVALUE;
	++expression_count_;
	return ApplyTarget(result, target);
}

ExpressionInfo SemanticAnalyzer::AnalyzeDeleteExpression(NodeId node,
	ScopeId scope, TypeId target)
{
	const bool array = FindChild(node, "array-delete") != kNoNode;
	NodeId operand_syntax = kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (!arena_->IsTag(child, "global-scope") &&
			!arena_->IsTag(child, "array-delete")) operand_syntax = child;
	}
	if (operand_syntax == kNoNode)
		throw std::runtime_error("delete-expression has no operand");
	ExpressionInfo operand = AnalyzeExpression(operand_syntax, scope);
	if (!IsPointer(Decay(operand.type)))
	{
		std::vector<TypeId> results;
		AppendBuiltinConversionTargets(operand, &results);
		std::vector<TypeId> pointers;
		for (std::size_t i = 0; i < results.size(); ++i)
			if (IsPointer(results[i])) pointers.push_back(results[i]);
		if (pointers.size() != 1)
			throw std::runtime_error("delete operand is not a unique pointer");
		const CallConversionFact conversion =
			ConvertingFunction(operand, pointers[0], false);
		if (conversion.rank == CONVERSION_INVALID)
			throw std::runtime_error("invalid delete pointer conversion");
		operand = ApplyCallArgument(operand, pointers[0], &conversion);
	}
	const TypeRecord pointer = program_->types.Get(Decay(operand.type));
	if (pointer.kind != TYPE_POINTER)
		throw std::runtime_error("delete operand is not a pointer");
	const TypeId object_type = program_->types.RemoveTopCv(pointer.child);
	TypeId leaf_type = object_type;
	while (array && program_->types.Get(
		program_->types.RemoveTopCv(leaf_type)).kind == TYPE_ARRAY)
		leaf_type = program_->types.Get(
			program_->types.RemoveTopCv(leaf_type)).child;
	leaf_type = program_->types.RemoveTopCv(leaf_type);
	const EntityId entity = EntityOf(leaf_type);
	const bool class_object = IsClassEntity(*program_, entity);
	BindingId destructor = kNoBinding;
	if (class_object)
	{
		const BindingId selected_destructor = DestructorForType(leaf_type);
		if (selected_destructor == kNoBinding)
			throw std::logic_error("deleted class has no destructor identity");
		if (!CanAccessMember(selected_destructor, entity))
			throw std::runtime_error("inaccessible delete destructor");
		if (GetFunction(selected_destructor).deleted_destructor)
			throw std::runtime_error("deleted destructor is required");
		if (!program_->entities[entity].trivial_destructor)
		{
			destructor = selected_destructor;
			DemandFunction(destructor);
		}
	}
	const bool explicit_global = FindChild(node, "global-scope") != kNoNode;
	const BindingId deallocation = SelectUsualDeallocation(
		scope, entity, explicit_global, array, leaf_type);
	const TypeId void_type = program_->types.Fundamental(FUND_VOID);
	const std::uint32_t expression = MakeDump(DUMP_DELETE_EXPRESSION,
		void_type, VALUE_PRVALUE, 0, deallocation);
	dump_.nodes[expression].operand_type = leaf_type;
	dump_.nodes[expression].selected_binding = destructor;
	if (destructor != kNoBinding &&
		program_->bindings[destructor].virtual_function)
	{
		dump_.nodes[expression].virtual_call = true;
		const std::uint32_t complete_slot = VirtualSlotFor(destructor);
		if (complete_slot == kNoDumpEdge)
			throw std::logic_error("virtual destructor has no slot");
		dump_.nodes[expression].virtual_slot = complete_slot + 1;
	}
	dump_.nodes[expression].array_action = array;
	dump_.nodes[expression].array_cookie = array && class_object;
	dump_.Add(expression, operand.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = void_type;
	result.category = VALUE_PRVALUE;
	++expression_count_;
	return ApplyTarget(result, target);
}

ExpressionInfo SemanticAnalyzer::MaterializeTemporary(
	const ExpressionInfo& initializer)
{
	const TypeRecord initial_type = program_->types.Get(
		program_->types.RemoveTopCv(initializer.type));
	if (!IsClassEntity(*program_, EntityOf(initializer.type)) &&
		initial_type.kind != TYPE_ARRAY)
		return initializer;
	TypeId object_type = initializer.type;
	const TypeRecord& outer = program_->types.Get(object_type);
	const bool reference_result = outer.kind == TYPE_LVALUE_REFERENCE ||
		outer.kind == TYPE_RVALUE_REFERENCE;
	if (reference_result) object_type = outer.child;
	object_type = program_->types.RemoveTopCv(object_type);
	const std::uint32_t temporary = MakeDump(DUMP_TEMPORARY_OBJECT,
		object_type, VALUE_XVALUE);
	dump_.nodes[temporary].reference_call_materialization = reference_result;
	dump_.Add(temporary, initializer.node);
	const DumpNode& action = dump_.nodes[initializer.node];
	const bool compile_time_only =
		constant_expression_required_depth_ != 0 &&
		constant_initializer_required_depth_ == 0 &&
		ExpressionObject(initializer) != kNoConstexprObject &&
		(action.binding == kNoBinding ||
		 GetFunction(action.binding).delegated_constructor == kNoBinding);
	if (action.kind == DUMP_CONSTRUCTOR_ACTION &&
		action.binding != kNoBinding &&
		!compile_time_only && unevaluated_depth_ == 0)
	{
		if (constant_expression_required_depth_ != 0 ||
			constexpr_evaluation_depth_ != 0)
			DemandFunction(action.binding);
		else dump_.nodes[temporary].pending_constructor_demand = true;
	}
	else if (action.kind == DUMP_BRACED_INIT_LIST &&
		action.value_constructor != kNoDumpEdge && unevaluated_depth_ == 0)
		dump_.nodes[temporary].pending_constructor_demand = true;
	ExpressionInfo result = initializer;
	result.node = temporary;
	result.type = object_type;
	result.category = VALUE_XVALUE;
	const std::uint32_t object = ExpressionObject(initializer);
	if (object != kNoConstexprObject)
	{
		SetExpressionSubobject(
			&result, object, ExpressionCompleteObject(initializer));
		PublishDumpObject(temporary, object);
	}
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::MaterializeDiscardedClassResult(
	ExpressionInfo value)
{
	const DumpKind kind = dump_.nodes[value.node].kind;
	const TypeRecord discarded_type = program_->types.Get(
		program_->types.RemoveTopCv(value.type));
	if (kind == DUMP_TEMPORARY_OBJECT &&
		discarded_type.kind == TYPE_ARRAY)
	{
		dump_.nodes[value.node].discarded_materialization = true;
		return value;
	}
	if (value.category == VALUE_PRVALUE &&
		IsClassEntity(*program_, EntityOf(value.type)) &&
		(kind == DUMP_CALL_EXPRESSION ||
		 kind == DUMP_CONDITIONAL_EXPRESSION))
	{
		value = MaterializeTemporary(value);
		dump_.nodes[value.node].discarded_materialization = true;
		return value;
	}
	const bool value_wrapper = kind == DUMP_CAST_EXPRESSION ||
		(kind == DUMP_BINARY_EXPRESSION &&
		 IsClassEntity(*program_, EntityOf(value.type)));
	if (!value_wrapper || dump_.nodes[value.node].first_edge == kNoDumpEdge)
		return value;
	std::uint32_t edge = dump_.nodes[value.node].first_edge;
	while (dump_.edges[edge].next != kNoDumpEdge)
		edge = dump_.edges[edge].next;
	const std::uint32_t child = dump_.edges[edge].child;
	ExpressionInfo discarded;
	discarded.node = child;
	discarded.type = dump_.nodes[child].type;
	discarded.category = dump_.nodes[child].category;
	discarded.binding = dump_.nodes[child].binding;
	discarded = MaterializeDiscardedClassResult(discarded);
	dump_.edges[edge].child = discarded.node;
	return value;
}

void SemanticAnalyzer::DemandDefaultConstructor(EntityId entity)
{
	if (entity == kNoEntity) return;
	if (default_constructor_demand_states_.size() <= entity)
		default_constructor_demand_states_.resize(
			static_cast<std::size_t>(entity) + 1, 0);
	if (default_constructor_demand_states_[entity] != 0) return;
	default_constructor_demand_states_[entity] = 1;
	demanded_default_constructor_entities_.push_back(entity);
	++demand_worklist_pushes_;
}
void SemanticAnalyzer::DemandConstructorDefinition(BindingId binding)
{
	DemandFunction(binding);
}

void SemanticAnalyzer::AddDefaultConstructor(std::uint32_t variable,
	BindingId binding, TypeId type)
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord object_type = program_->types.Get(type);
	const EntityId entity = DestructedEntity(type);
	if (entity == kNoEntity) return;
	const NamedFlavor flavor = program_->entities[entity].flavor;
	if (flavor != NAMED_STRUCT && flavor != NAMED_CLASS &&
		flavor != NAMED_UNION) return;
	const EntityRecord& class_record = program_->entities[entity];
	if (!class_record.default_constructible)
		throw std::runtime_error("class has no usable default constructor");
	const std::uint32_t action = BuildDefaultConstructorAction(type,
		program_->bindings[binding].owner);
	if (object_type.kind == TYPE_ARRAY)
	{
		dump_.nodes[action].object_binding = binding;
		dump_.Add(variable, action);
		return;
	}
	const BindingId constructor = dump_.nodes[action].binding;
	if (constructor != kNoBinding &&
		GetFunction(constructor).implicit_constructor &&
		program_->entities[entity].trivial_default_constructor)
	{
		const TypeId function_type = dump_.nodes[action].type;
		const TypeId this_type = program_->types.Parameters(function_type)[0];
		const std::uint32_t call = MakeDump(DUMP_CALL_EXPRESSION,
			program_->types.Fundamental(FUND_VOID), VALUE_PRVALUE);
		const std::uint32_t callee = MakeDump(DUMP_CALLEE, function_type,
			VALUE_NONE, dump_.nodes[action].text, constructor);
		const BindingRecord& object = program_->bindings[binding];
		const std::uint32_t identifier = MakeDump(DUMP_ID_EXPRESSION, type,
			VALUE_LVALUE, object.name, binding);
		const std::uint32_t address = MakeDump(DUMP_UNARY_EXPRESSION, this_type,
			VALUE_PRVALUE, program_->names.Intern("OP_AMP:&"));
		dump_.Add(address, identifier);
		dump_.Add(call, callee);
		dump_.Add(call, address);
		dump_.Add(action, call);
		expression_count_ += 2;
		DemandDefaultConstructor(entity);
	}
	dump_.Add(variable, action);
}

void SemanticAnalyzer::EmitDefaultConstructor(EntityId entity)
{
	if (entity >= default_constructor_demand_states_.size() ||
		default_constructor_demand_states_[entity] != 1) return;
	default_constructor_demand_states_[entity] = 2;
	const TypeId type = program_->entities[entity].type;
	const std::string owner = program_->names.Get(program_->entities[entity].name);
	const std::size_t separator = owner.rfind("::");
	const std::string leaf = separator == std::string::npos ? owner :
		owner.substr(separator + 2);
	const NameId name = program_->names.Intern(owner + "::" + leaf);
	const TypeId this_type = program_->types.Pointer(type);
	std::vector<TypeId> parameters(1, this_type);
	const TypeId function_type = program_->types.Function(
		program_->types.Fundamental(FUND_VOID), parameters, false);
	const std::uint32_t function = MakeDump(DUMP_FUNCTION_DEFINITION,
		function_type, VALUE_NONE, name);
	const std::uint32_t parameter = MakeDump(DUMP_PARAMETER, this_type,
		VALUE_NONE, program_->names.Intern("this"));
	const std::uint32_t body = MakeDump(DUMP_COMPOUND_STATEMENT);
	dump_.Add(function, parameter);
	dump_.Add(function, body);
	dump_.Add(root_, function);
	++default_constructor_emissions_;
}

std::uint32_t SemanticAnalyzer::MakeDestructorAction(TypeId type,
	BindingId destructor, BindingId object, std::uint32_t base_projections)
{
	if (destructor == kNoBinding ||
		!program_->bindings[destructor].destructor)
		throw std::logic_error("destruction action has no destructor identity");
	const FunctionInfo& info = GetFunction(destructor);
	if (info.deleted_destructor)
		throw std::runtime_error("deleted destructor is required");
	const std::uint32_t action = MakeDump(DUMP_DESTRUCTOR_ACTION,
		AdaptMemberFunctionType(destructor), VALUE_NONE,
		info.display_name, destructor);
	dump_.nodes[action].operand_type = type;
	dump_.nodes[action].object_binding = object;
	dump_.nodes[action].base_projection_count = base_projections;
	DemandFunction(destructor);
	return action;
}

std::uint32_t SemanticAnalyzer::MakeTemporaryDestructorAction(
	std::uint32_t temporary, BindingId destructor)
{
	if (temporary == kNoDumpEdge || temporary >= dump_.nodes.size() ||
		dump_.nodes[temporary].kind != DUMP_TEMPORARY_OBJECT)
		throw std::logic_error("temporary destruction has no object identity");
	const TypeId type = dump_.nodes[temporary].type;
	const EntityId entity = DestructedEntity(type);
	if (entity == kNoEntity) return kNoDumpEdge;
	if (!program_->entities[entity].destructible)
		throw std::runtime_error("temporary type is not destructible");
	if (destructor == kNoBinding) destructor = DestructorForType(type);
	if (destructor == kNoBinding)
		throw std::logic_error("temporary class has no destructor identity");
	if (!CanAccessMember(destructor, entity))
		throw std::runtime_error("inaccessible temporary destructor");
	if (program_->entities[entity].trivial_destructor) return kNoDumpEdge;
	const bool dependent_template_object =
		program_->entities[entity].template_argument_count != 0;
	if (!dependent_template_object && IsElidableAutomaticDestructor(destructor) &&
		!dump_.nodes[temporary].control_dependent_temporary)
		return kNoDumpEdge;
	const std::uint32_t action = MakeDestructorAction(
		type, destructor, kNoBinding);
	dump_.nodes[action].lifetime_object = temporary;
	return action;
}

bool SemanticAnalyzer::CollectTemporaryObjects(std::uint32_t node,
	std::vector<std::uint32_t>* temporaries, bool conditionally_evaluated)
{
	if (node == kNoDumpEdge || node >= dump_.nodes.size()) return false;
	++temporary_dependency_visits_;
	DumpNode& record = dump_.nodes[node];
	if (record.kind == DUMP_CONDITIONAL_ARM) return false;
	bool short_circuit = false;
	if (record.kind == DUMP_BINARY_EXPRESSION && record.text != 0)
	{
		const std::string& operation = program_->names.Get(record.text);
		short_circuit = operation.find("&&") != std::string::npos ||
			operation.find("||") != std::string::npos;
	}
	bool control_dependent = record.kind == DUMP_CONDITIONAL_EXPRESSION ||
		short_circuit;
	std::size_t child_index = 0;
	for (std::uint32_t edge = record.first_edge; edge != kNoDumpEdge;
		edge = dump_.edges[edge].next, ++child_index)
	{
		const bool branch_only =
			(short_circuit && child_index == 1) ||
			(record.kind == DUMP_CONDITIONAL_EXPRESSION && child_index != 0);
		control_dependent = CollectTemporaryObjects(
			dump_.edges[edge].child, temporaries,
			conditionally_evaluated || branch_only) || control_dependent;
	}
	if (record.kind == DUMP_TEMPORARY_OBJECT)
	{
		if (conditionally_evaluated) record.conditionally_constructed = true;
		if (control_dependent) record.control_dependent_temporary = true;
		temporaries->push_back(node);
	}
	return control_dependent;
}

void SemanticAnalyzer::MarkFullExpressionCalls(std::uint32_t node)
{
	if (node == kNoDumpEdge || node >= dump_.nodes.size()) return;
	DumpNode& record = dump_.nodes[node];
	record.full_expression_staging = true;
	if (record.kind == DUMP_CALL_EXPRESSION)
		record.full_expression_staging = true;
	if (record.kind == DUMP_TEMPORARY_OBJECT)
		record.full_expression_staging = true;
	for (std::uint32_t edge = record.first_edge; edge != kNoDumpEdge;
		edge = dump_.edges[edge].next)
		MarkFullExpressionCalls(dump_.edges[edge].child);
}

bool SemanticAnalyzer::HasControlDependentTemporary(std::uint32_t node)
{
	if (node == kNoDumpEdge || node >= dump_.nodes.size()) return false;
	++temporary_dependency_visits_;
	const DumpNode& record = dump_.nodes[node];
	if (record.kind == DUMP_CONDITIONAL_EXPRESSION) return true;
	if (record.kind == DUMP_BINARY_EXPRESSION && record.text != 0)
	{
		const std::string& operation = program_->names.Get(record.text);
		if (operation.find("&&") != std::string::npos ||
			operation.find("||") != std::string::npos)
			return true;
	}
	for (std::uint32_t edge = record.first_edge; edge != kNoDumpEdge;
		edge = dump_.edges[edge].next)
		if (HasControlDependentTemporary(dump_.edges[edge].child)) return true;
	return false;
}

void SemanticAnalyzer::AppendFullExpressionDestructionActions(
	std::uint32_t expression, std::uint32_t output_parent)
{
	std::vector<std::uint32_t> temporaries;
	CollectTemporaryObjects(expression, &temporaries);
	for (std::size_t i = temporaries.size(); i != 0; --i)
	{
		const std::uint32_t action =
			MakeTemporaryDestructorAction(temporaries[i - 1]);
		if (action != kNoDumpEdge)
		{
			dump_.nodes[action].full_expression_staging = true;
			dump_.Add(output_parent, action);
		}
	}
}

void SemanticAnalyzer::AppendUnwindDestructionActions(ScopeId scope,
	std::uint32_t output_parent)
{
	ScopeId current = scope < nearest_lifetime_scopes_.size() ?
		nearest_lifetime_scopes_[scope] : kNoScope;
	while (current != kNoScope)
	{
		++unwind_cleanup_scope_visits_;
		if (current >= scope_lifetimes_.size())
			throw std::logic_error("indexed lifetime scope has no obligations");
		const std::vector<LifetimeObligation>& obligations =
			scope_lifetimes_[current];
		for (std::size_t i = obligations.size(); i != 0; --i)
		{
			const LifetimeObligation& obligation = obligations[i - 1];
			std::uint32_t action = obligation.temporary == kNoDumpEdge ?
				MakeDestructorAction(obligation.type, obligation.destructor,
					obligation.object) :
				MakeTemporaryDestructorAction(obligation.temporary,
					obligation.destructor);
			if (action == kNoDumpEdge) continue;
			dump_.nodes[action].unwind_only = true;
			dump_.Add(output_parent, action);
			++unwind_cleanup_action_visits_;
		}
		const ScopeId parent = scope_parents_[current];
		current = parent != kNoScope &&
			parent < nearest_lifetime_scopes_.size() ?
			nearest_lifetime_scopes_[parent] : kNoScope;
	}
}

bool SemanticAnalyzer::CacheDestructorChainDecision(BindingId destructor,
	bool proven_empty) const
{
	if (empty_destructor_chain_cache_.size() <= destructor)
		empty_destructor_chain_cache_.resize(
			static_cast<std::size_t>(destructor) + 1, 0);
	// A conservative no-elide decision is monotonic: a later empty definition
	// may enable an optional optimization, but retaining destruction is correct.
	empty_destructor_chain_cache_[destructor] = proven_empty ? 2 : 1;
	return proven_empty;
}

bool SemanticAnalyzer::CanElideDestructorChain(BindingId destructor) const
{
	++empty_destructor_chain_visits_;
	if (destructor == kNoBinding || destructor >= program_->bindings.size())
		return false;
	if (destructor < empty_destructor_chain_cache_.size() &&
		empty_destructor_chain_cache_[destructor] != 0)
	{
		++empty_destructor_chain_cache_hits_;
		return empty_destructor_chain_cache_[destructor] == 2;
	}
	const BindingRecord& binding = program_->bindings[destructor];
	if (binding.member_owner == kNoEntity)
		return CacheDestructorChainDecision(destructor, false);
	const FunctionInfo& info = GetFunction(destructor);
	const bool empty_definition = info.definition_body != kNoNode &&
		FirstSemanticChild(info.definition_body) == kNoNode;
	if (!info.implicit_destructor && !info.defaulted_destructor &&
		!empty_definition)
		return CacheDestructorChainDecision(destructor, false);
	const EntityId entity = binding.member_owner;
	const EntityId base = program_->entities[entity].direct_base;
	if (base != kNoEntity && !program_->entities[base].trivial_destructor)
	{
		const BindingId base_destructor = DestructorForType(
			program_->entities[base].type);
		if (!CanElideDestructorChain(base_destructor))
			return CacheDestructorChainDecision(destructor, false);
	}
	if (entity >= entity_data_members_.size())
		return CacheDestructorChainDecision(destructor, true);
	const std::vector<BindingId>& members = entity_data_members_[entity];
	for (std::size_t i = 0; i < members.size(); ++i)
	{
		const EntityId member = DestructedEntity(
			program_->bindings[members[i]].type);
		if (member == kNoEntity ||
			program_->entities[member].trivial_destructor)
			continue;
		if (!CanElideDestructorChain(DestructorForType(
			program_->bindings[members[i]].type)))
			return CacheDestructorChainDecision(destructor, false);
	}
	return CacheDestructorChainDecision(destructor, true);
}

bool SemanticAnalyzer::IsElidableAutomaticDestructor(
	BindingId destructor) const
{
	if (destructor == kNoBinding || destructor >= program_->bindings.size())
		return false;
	const BindingRecord& binding = program_->bindings[destructor];
	if (binding.member_owner == kNoEntity)
		return false;
	if (program_->entities[binding.member_owner].polymorphic_class)
		return false;
	const FunctionInfo& info = GetFunction(destructor);
	const bool union_object =
		program_->entities[binding.member_owner].flavor == NAMED_UNION;
	const bool empty_definition = info.definition_body != kNoNode &&
		FirstSemanticChild(info.definition_body) == kNoNode;
	const bool elidable_definition = info.implicit_destructor ||
		empty_definition || info.defaulted_destructor;
	if ((!union_object || !empty_definition) && !elidable_definition)
		return false;
	return CanElideDestructorChain(destructor);
}

ScopeId SemanticAnalyzer::CompoundCleanupStop(ScopeId scope) const
{
	return program_->KindOfScope(scope) == SCOPE_FUNCTION ?
		scope_parents_[scope] : scope;
}

void SemanticAnalyzer::AddLifetimeObligation(ScopeId scope,
	BindingId object, TypeId type, bool allow_elision)
{
	const EntityId entity = DestructedEntity(type);
	if (entity == kNoEntity) return;
	if (!program_->entities[entity].destructible)
		throw std::runtime_error("object type is not destructible");
	const BindingId destructor = DestructorForType(type);
	if (destructor == kNoBinding)
		throw std::logic_error("class has no destructor identity");
	if (!CanAccessMember(destructor, entity))
		throw std::runtime_error("inaccessible destructor");
	const TypeKind object_kind = program_->types.Get(
		program_->types.RemoveTopCv(type)).kind;
	if (program_->entities[entity].trivial_destructor ||
		(allow_elision && object_kind != TYPE_ARRAY &&
		 IsElidableAutomaticDestructor(destructor))) return;
	if (scope_lifetimes_.size() <= scope)
		scope_lifetimes_.resize(static_cast<std::size_t>(scope) + 1);
	if (nearest_lifetime_scopes_.size() <= scope)
		nearest_lifetime_scopes_.resize(
			static_cast<std::size_t>(scope) + 1, kNoScope);
	nearest_lifetime_scopes_[scope] = scope;
	scope_lifetimes_[scope].push_back(
		LifetimeObligation(object, destructor, type));
}

void SemanticAnalyzer::AddTemporaryLifetimeObligation(ScopeId scope,
	std::uint32_t temporary)
{
	const std::uint32_t action = MakeTemporaryDestructorAction(temporary);
	if (action == kNoDumpEdge) return;
	const DumpNode& cleanup = dump_.nodes[action];
	if (scope_lifetimes_.size() <= scope)
		scope_lifetimes_.resize(static_cast<std::size_t>(scope) + 1);
	if (nearest_lifetime_scopes_.size() <= scope)
		nearest_lifetime_scopes_.resize(
			static_cast<std::size_t>(scope) + 1, kNoScope);
	nearest_lifetime_scopes_[scope] = scope;
	scope_lifetimes_[scope].push_back(LifetimeObligation(kNoBinding,
		cleanup.binding, cleanup.operand_type, temporary));
}

void SemanticAnalyzer::RegisterConditionLifetime(ScopeId scope,
	BindingId object, TypeId type, const ExpressionInfo& initializer,
	std::uint32_t condition)
{
	const std::size_t first = scope < scope_lifetimes_.size() ?
		scope_lifetimes_[scope].size() : 0;
	const TypeKind kind = program_->types.Get(type).kind;
	std::uint32_t temporary = kNoDumpEdge;
	if (kind == TYPE_LVALUE_REFERENCE || kind == TYPE_RVALUE_REFERENCE)
	{
		std::vector<std::uint32_t> temporaries;
		CollectTemporaryObjects(initializer.node, &temporaries);
		if (!temporaries.empty()) temporary = temporaries.back();
	}
	const TypeId lifetime_type = temporary == kNoDumpEdge ? type :
		dump_.nodes[temporary].type;
	const BindingId destructor = DestructorForType(lifetime_type);
	if (destructor != kNoBinding)
	{
		const FunctionInfo& info = GetFunction(destructor);
		if (info.definition_body != kNoNode &&
			FirstSemanticChild(info.definition_body) == kNoNode)
			return;
	}
	if (temporary != kNoDumpEdge)
		AddTemporaryLifetimeObligation(scope, temporary);
	else if (kind != TYPE_LVALUE_REFERENCE && kind != TYPE_RVALUE_REFERENCE)
		AddLifetimeObligation(scope, object, type);
	if (scope >= scope_lifetimes_.size()) return;
	const std::vector<LifetimeObligation>& obligations = scope_lifetimes_[scope];
	for (std::size_t i = first; i < obligations.size(); ++i)
	{
		const LifetimeObligation& obligation = obligations[i];
		std::uint32_t action = obligation.temporary == kNoDumpEdge ?
			MakeDestructorAction(obligation.type, obligation.destructor,
				obligation.object) :
			MakeTemporaryDestructorAction(obligation.temporary,
				obligation.destructor);
		if (action == kNoDumpEdge) continue;
		dump_.nodes[action].unwind_only = true;
		dump_.Add(condition, action);
	}
}

void SemanticAnalyzer::AddNamespaceObjectAction(std::uint32_t variable,
	BindingId object, TypeId type, std::uint32_t initializer)
{
	std::uint32_t destructor_action = kNoDumpEdge;
	const EntityId entity = DestructedEntity(type);
	if (entity != kNoEntity)
	{
		if (program_->bindings[object].unnamed_namespace_linkage &&
			initializer != kNoDumpEdge &&
			dump_.nodes[initializer].kind == DUMP_CONSTRUCTOR_ACTION)
		{
			const BindingId constructor = dump_.nodes[initializer].binding;
			DemandFunction(EnsureConstructorBaseEntry(constructor));
			DemandFunction(constructor);
		}
		if (!program_->entities[entity].destructible)
			throw std::runtime_error("namespace object type is not destructible");
		const BindingId destructor = DestructorForType(type);
		if (destructor == kNoBinding)
			throw std::logic_error("namespace class has no destructor identity");
		if (!CanAccessMember(destructor, entity))
			throw std::runtime_error("inaccessible namespace object destructor");
		if (!program_->entities[entity].trivial_destructor)
			destructor_action = MakeDestructorAction(type, destructor, object);
	}
	namespace_objects_.push_back(NamespaceObjectAction(object, type, variable,
		initializer, destructor_action));
}

void SemanticAnalyzer::AppendScopeDestructionActions(ScopeId scope,
	std::uint32_t output_parent, ScopeId stop_exclusive)
{
	for (ScopeId current = scope; current != kNoScope &&
		current != stop_exclusive; current = scope_parents_[current])
	{
		if (current >= scope_lifetimes_.size()) continue;
		const std::vector<LifetimeObligation>& obligations =
			scope_lifetimes_[current];
		for (std::size_t i = obligations.size(); i != 0; --i)
		{
			const LifetimeObligation& obligation = obligations[i - 1];
			std::uint32_t action = obligation.temporary == kNoDumpEdge ?
				MakeDestructorAction(obligation.type, obligation.destructor,
					obligation.object) :
				MakeTemporaryDestructorAction(obligation.temporary,
					obligation.destructor);
			if (action != kNoDumpEdge) dump_.Add(output_parent, action);
			++lexical_cleanup_action_visits_;
		}
	}
}

void SemanticAnalyzer::AddDestructorSubobjectActions(EntityId entity,
	std::uint32_t body)
{
	if (entity >= entity_data_members_.size())
		throw std::logic_error("destructor is missing its member index");
	if (program_->entities[entity].flavor == NAMED_UNION) return;
	const std::vector<BindingId>& members = entity_data_members_[entity];
	for (std::size_t i = members.size(); i != 0; --i)
	{
		const BindingId member = members[i - 1];
		const TypeId type = program_->bindings[member].type;
		const EntityId subobject = DestructedEntity(type);
		if (subobject == kNoEntity ||
			program_->entities[subobject].trivial_destructor) continue;
		const BindingId destructor = DestructorForType(type);
		if (destructor == kNoBinding)
			throw std::logic_error("member has no destructor identity");
		if (!CanAccessMember(destructor, subobject))
			throw std::runtime_error("inaccessible member destructor");
		TypeId object = program_->types.RemoveTopCv(EffectiveType(type));
		const TypeRecord& record = program_->types.Get(object);
		if (record.kind == TYPE_ARRAY)
		{
			std::size_t element_count = 1;
			TypeId element_type = object;
			for (;;)
			{
				const TypeRecord& array = program_->types.Get(
					program_->types.RemoveTopCv(element_type));
				if (array.kind != TYPE_ARRAY) break;
				if (array.bound == 0 || array.bound >
					std::numeric_limits<std::size_t>::max() / element_count)
					throw std::logic_error("invalid destructor array extent");
				element_count *= static_cast<std::size_t>(array.bound);
				element_type = array.child;
			}
			if (element_count > kDestructorArrayInlineLimit)
			{
				const std::uint32_t action = MakeDestructorAction(
					type, destructor, member);
				dump_.nodes[action].array_action = true;
				dump_.nodes[action].array_count = element_count;
				dump_.Add(body, action);
				++destructor_subobject_action_visits_;
				continue;
			}
			for (std::size_t element =
				static_cast<std::size_t>(record.bound); element != 0; --element)
			{
				const std::uint32_t action = MakeDestructorAction(
					type, destructor, member);
				dump_.nodes[action].constant = true;
				dump_.nodes[action].constant_value =
					static_cast<std::int64_t>(element - 1);
				dump_.Add(body, action);
				++destructor_subobject_action_visits_;
			}
		}
		else
		{
			dump_.Add(body, MakeDestructorAction(type, destructor, member));
			++destructor_subobject_action_visits_;
		}
	}
	const EntityId base = program_->entities[entity].direct_base;
	if (base != kNoEntity && !program_->entities[base].trivial_destructor)
	{
		BindingId destructor = DestructorForType(
			program_->entities[base].type);
		if (destructor == kNoBinding)
			throw std::logic_error("base has no destructor identity");
		if (!CanAccessMember(destructor, base))
			throw std::runtime_error("inaccessible base destructor");
		if (program_->bindings[destructor].virtual_function)
			destructor = EnsureDestructorBaseEntry(destructor);
		dump_.Add(body, MakeDestructorAction(program_->entities[base].type,
			destructor, kNoBinding, 1));
		++destructor_subobject_action_visits_;
	}
}

}
}
