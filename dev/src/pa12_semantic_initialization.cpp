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

BindingId SemanticAnalyzer::SelectConstructor(ScopeId scope,
	const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>& arguments,
	const std::vector<BindingId>& candidates, bool copy_initialization,
	bool list_initialization)
{
	const std::size_t arity = argument_syntax.size();
	if (arity != 0 && candidates.size() >
		std::numeric_limits<std::size_t>::max() / arity)
		throw std::runtime_error("constructor conversion table is too large");
	std::vector<ConversionRank> ranks(candidates.size() * arity,
		CONVERSION_ELLIPSIS);
	CallConversionTable conversion_cache;
	std::vector<bool> viable(candidates.size(), true);
	for (std::size_t c = 0; c < candidates.size(); ++c)
	{
		++overload_candidates_;
		const FunctionInfo& constructor = GetFunction(candidates[c]);
		const TypeRecord function_type = program_->types.Get(constructor.type);
		if (!constructor.constructor ||
			(copy_initialization && !list_initialization &&
			 constructor.explicit_constructor))
		{
			viable[c] = false;
			continue;
		}
		std::size_t required = function_type.parameter_count;
		while (required != 0 && required <= constructor.parameters.size() &&
			constructor.parameters[required - 1].default_argument != kNoNode)
			--required;
		if (arity < required || (!function_type.variadic &&
			arity > function_type.parameter_count))
		{
			viable[c] = false;
			continue;
		}
		const TypeId* parameters = program_->types.Parameters(constructor.type);
		for (std::size_t a = 0; a < arity; ++a)
		{
			ConversionRank rank = CONVERSION_ELLIPSIS;
			if (a < function_type.parameter_count)
			{
				if (arguments[a].type == kNoType)
					rank = CONVERSION_INVALID;
				else rank = CallConversion(arguments[a], parameters[a],
					&conversion_cache, a).rank;
			}
			ranks[c * arity + a] = rank;
			if (rank == CONVERSION_INVALID) viable[c] = false;
		}
	}
	const auto better = [this, &ranks, &arguments, &candidates, arity](
		std::size_t left, std::size_t right) -> bool
	{
		++overload_order_comparisons_;
		bool no_worse = true;
		bool strictly_better = false;
		for (std::size_t a = 0; a < arity; ++a)
		{
			if (ranks[left * arity + a] > ranks[right * arity + a])
				no_worse = false;
			if (ranks[left * arity + a] < ranks[right * arity + a])
				strictly_better = true;
		}
		if (!no_worse) return false;
		if (strictly_better) return true;
		const TypeRecord& left_type =
			program_->types.Get(GetFunction(candidates[left]).type);
		const TypeRecord& right_type =
			program_->types.Get(GetFunction(candidates[right]).type);
		const TypeId* left_parameters =
			program_->types.Parameters(GetFunction(candidates[left]).type);
		const TypeId* right_parameters =
			program_->types.Parameters(GetFunction(candidates[right]).type);
		for (std::size_t a = 0; a < arity; ++a)
		{
			if (a >= left_type.parameter_count ||
				a >= right_type.parameter_count)
				continue;
			const int preference = CompareReferenceBindings(
				arguments[a], left_parameters[a], right_parameters[a]);
			if (preference != 0) return preference > 0;
		}
		return false;
	};
	std::size_t champion = candidates.size();
	std::size_t viable_count = 0;
	for (std::size_t c = 0; c < candidates.size(); ++c)
	{
		if (!viable[c]) continue;
		++viable_count;
		if (champion == candidates.size() || better(c, champion)) champion = c;
	}
	if (viable_count == 0) throw std::runtime_error("no viable constructor");
	if (viable_count != 1)
		for (std::size_t c = 0; c < candidates.size(); ++c)
			if (c != champion && viable[c] && !better(champion, c))
				throw std::runtime_error("ambiguous constructor");
	const BindingId selected = candidates[champion];
	const FunctionInfo& constructor = GetFunction(selected);
	if (constructor.deleted_constructor || constructor.deleted_special_member)
		throw std::runtime_error("selected constructor is deleted");
	if (copy_initialization && constructor.explicit_constructor)
		throw std::runtime_error(
			"copy initialization selected an explicit constructor");
	if (!CanAccessMember(selected))
		throw std::runtime_error("inaccessible constructor");
	(void)scope;
	return selected;
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

std::uint32_t SemanticAnalyzer::BuildConstructorAction(TypeId type,
	ScopeId scope, const std::vector<NodeId>& argument_syntax,
	bool copy_initialization, bool list_initialization, bool base_subobject,
	bool demand)
{
	const EntityId entity = EntityOf(type);
	if (!IsClassEntity(*program_, entity))
		throw std::logic_error("constructor action has non-class type");
	std::vector<ExpressionInfo> arguments;
	arguments.reserve(argument_syntax.size());
	for (std::size_t i = 0; i < argument_syntax.size(); ++i)
	{
		if (arena_->IsTag(argument_syntax[i], "braced-init-list"))
			arguments.push_back(ExpressionInfo());
		else arguments.push_back(AnalyzeExpression(argument_syntax[i], scope));
	}
	const std::vector<BindingId>& candidates = ConstructorCandidates(entity);
	BindingId selected = SelectConstructor(scope, argument_syntax,
		arguments, candidates, copy_initialization, list_initialization);
	if (base_subobject) selected = EnsureConstructorBaseEntry(selected);
	const FunctionInfo constructor = GetFunction(selected);
	const TypeRecord function_type = program_->types.Get(constructor.type);
	const TypeId* parameter_data = program_->types.Parameters(constructor.type);
	std::vector<TypeId> parameters;
	if (function_type.parameter_count != 0)
		parameters.assign(parameter_data,
			parameter_data + function_type.parameter_count);
	const std::uint32_t action = MakeDump(DUMP_CONSTRUCTOR_ACTION,
		AdaptMemberFunctionType(selected), VALUE_NONE,
		constructor.display_name, selected);
	dump_.nodes[action].operand_type =
		program_->types.RemoveTopCv(EffectiveType(type));
	dump_.nodes[action].trivial_special_member_action =
		constructor.trivial_special_member;
	std::vector<BindingId> empty_base_entries;
	if (((constructor.defaulted_constructor &&
		  program_->entities[entity].empty_class) ||
		 (constructor.implicit_constructor &&
		  program_->entities[entity].direct_base != kNoEntity &&
		  !program_->entities[
			program_->entities[entity].direct_base].trivial_default_constructor)) &&
		argument_syntax.empty() &&
		EmptyDefaultConstructorChain(selected, &empty_base_entries))
	{
		dump_.nodes[action].elide_empty_constructor = true;
		for (std::size_t i = 0; i < empty_base_entries.size(); ++i)
			DemandFunction(empty_base_entries[i]);
	}
	for (std::size_t a = 0; a < argument_syntax.size(); ++a)
	{
		ExpressionInfo argument = arguments[a];
		if (a < function_type.parameter_count)
		{
			if (argument.type == kNoType)
				argument = AnalyzeExpression(argument_syntax[a], scope,
					parameters[a]);
			else argument = ApplyCallArgument(argument, parameters[a]);
		}
		dump_.Add(action, argument.node);
	}
	for (std::size_t a = argument_syntax.size();
		a < function_type.parameter_count; ++a)
	{
		if (a >= constructor.parameters.size() ||
			constructor.parameters[a].default_argument == kNoNode)
			throw std::logic_error("selected constructor lacks a default argument");
		const ExpressionInfo argument = AnalyzeExpression(
			constructor.parameters[a].default_argument,
			constructor.parameters[a].default_scope, parameters[a]);
		dump_.Add(action, argument.node);
	}
	if (demand && !dump_.nodes[action].elide_empty_constructor &&
		!dump_.nodes[action].trivial_special_member_action &&
		!(constructor.implicit_constructor &&
		program_->entities[entity].trivial_default_constructor))
		DemandFunction(selected);
	++expression_count_;
	return action;
}

void SemanticAnalyzer::ValidateClassValueConstruction(TypeId type,
	const ExpressionInfo& source, bool copy_initialization)
{
	const EntityId entity = EntityOf(type);
	if (!IsClassEntity(*program_, entity))
		throw std::logic_error("class-value construction has non-class type");
	std::vector<NodeId> argument_syntax(1, kNoNode);
	std::vector<ExpressionInfo> arguments(1, source);
	(void)SelectConstructor(kNoScope, argument_syntax,
		arguments, ConstructorCandidates(entity), copy_initialization, false);
}

std::uint32_t SemanticAnalyzer::BuildClassValueConstructorAction(TypeId type,
	const ExpressionInfo& source, bool copy_initialization, bool demand)
{
	const EntityId entity = EntityOf(type);
	if (!IsClassEntity(*program_, entity))
		throw std::logic_error("class-value construction has non-class type");
	std::vector<NodeId> argument_syntax(1, kNoNode);
	std::vector<ExpressionInfo> arguments(1, source);
	const BindingId selected = SelectConstructor(kNoScope, argument_syntax,
		arguments, ConstructorCandidates(entity), copy_initialization, false);
	const FunctionInfo constructor = GetFunction(selected);
	const TypeRecord function_type = program_->types.Get(constructor.type);
	const TypeId* parameter_data = program_->types.Parameters(constructor.type);
	if (function_type.parameter_count == 0)
		throw std::logic_error("class-value constructor has no source parameter");
	const std::vector<TypeId> parameters(parameter_data,
		parameter_data + function_type.parameter_count);
	const std::uint32_t action = MakeDump(DUMP_CONSTRUCTOR_ACTION,
		AdaptMemberFunctionType(selected), VALUE_NONE,
		constructor.display_name, selected);
	bool conversion_result_materialization = false;
	if (dump_.nodes[source.node].kind == DUMP_TEMPORARY_OBJECT &&
		dump_.nodes[source.node].first_edge != kNoDumpEdge)
	{
		const std::uint32_t child =
			dump_.edges[dump_.nodes[source.node].first_edge].child;
		conversion_result_materialization =
			dump_.nodes[child].kind == DUMP_CALL_EXPRESSION &&
			dump_.nodes[child].binding != kNoBinding &&
			program_->bindings[dump_.nodes[child].binding].conversion_function;
	}
	dump_.nodes[action].operand_type =
		program_->types.RemoveTopCv(EffectiveType(type));
	dump_.nodes[action].trivial_special_member_action =
		constructor.trivial_special_member &&
		!conversion_result_materialization;
	dump_.Add(action, ApplyCallArgument(
		source, parameters[0]).node);
	for (std::size_t a = 1; a < function_type.parameter_count; ++a)
	{
		if (a >= constructor.parameters.size() ||
			constructor.parameters[a].default_argument == kNoNode)
			throw std::logic_error(
				"selected class-value constructor lacks a default argument");
		const ExpressionInfo argument = AnalyzeExpression(
			constructor.parameters[a].default_argument,
			constructor.parameters[a].default_scope, parameters[a]);
		dump_.Add(action, argument.node);
	}
	if ((demand || conversion_result_materialization) &&
		!dump_.nodes[action].trivial_special_member_action)
		DemandFunction(selected);
	++expression_count_;
	return action;
}

void SemanticAnalyzer::FinalizeNamedReturnSlot(std::uint32_t function)
{
	const TypeId result = program_->types.Get(dump_.nodes[function].type).child;
	const EntityId entity = EntityOf(result);
	if (!IsClassEntity(*program_, entity)) return;
	const EntityRecord& class_record = program_->entities[entity];
	const std::size_t size = program_->SizeOf(result);
	const bool indirect = size > 16 ||
		(size < 16 && class_record.indirect_class_value_abi);

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
	const ExpressionInfo& source, TypeId target)
{
	if (!graph_consumer_) return source;
	if ((!IsDirectTrivialClassValueType(target) &&
		 dump_.nodes[source.node].kind != DUMP_CALL_EXPRESSION) ||
		program_->types.RemoveTopCv(EffectiveType(source.type)) !=
			program_->types.RemoveTopCv(EffectiveType(target)))
		throw std::logic_error("invalid direct class-value transfer");
	const std::uint32_t action = MakeDump(DUMP_CLASS_VALUE_TRANSFER,
		program_->types.RemoveTopCv(EffectiveType(target)), VALUE_PRVALUE);
	dump_.Add(action, source.node);
	ExpressionInfo result;
	result.node = action;
	result.type = program_->types.RemoveTopCv(EffectiveType(target));
	result.category = VALUE_PRVALUE;
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
		ExpressionInfo value = AnalyzeExpression(expression, scope,
			IsVoid(current_return_type_) ? kNoType : current_return_type_);
		if (IsVoid(current_return_type_) && !IsVoid(value.type))
			throw std::runtime_error("void function returns a value");
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
		if (class_return &&
			program_->types.RemoveTopCv(EffectiveType(value.type)) ==
				returned_object &&
			dump_.nodes[value.node].kind != DUMP_CONSTRUCTOR_ACTION &&
			dump_.nodes[value.node].kind != DUMP_BRACED_INIT_LIST)
		{
			if (value.category == VALUE_PRVALUE &&
				dump_.nodes[value.node].kind == DUMP_CALL_EXPRESSION)
			{
				ValidateClassValueConstruction(current_return_type_, value);
				value = BuildDirectClassValueTransfer(
					value, current_return_type_);
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
		dump_.Add(statement, value.node);
	}
	AppendScopeDestructionActions(scope, statement);
}

ExpressionInfo SemanticAnalyzer::AnalyzeVariableInitializer(
	NodeId initializer_node, ScopeId scope, TypeId type, bool local)
{
	NodeId expression = FirstSemanticChild(initializer_node);
	const EntityId class_entity = EntityOf(type);
	const TypeKind declared_kind = program_->types.Get(type).kind;
	ExpressionInfo initializer;
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
			initializer.node = BuildConstructorAction(
				type, scope, arguments, false, false);
		}
		else if (expression != kNoNode &&
			arena_->IsTag(expression, "braced-init-list") &&
			!program_->entities[class_entity].is_aggregate)
		{
			for (std::uint32_t argument = arena_->FirstEdge(expression);
				argument != kNoEdge; argument = arena_->NextEdge(argument))
				arguments.push_back(arena_->EdgeChild(argument));
			initializer.node = BuildConstructorAction(type, scope, arguments,
				PayloadSource(initializer_node) == "copy", true);
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
					throw std::runtime_error("invalid class copy initializer");
				if (initializer.category == VALUE_PRVALUE &&
					dump_.nodes[initializer.node].kind == DUMP_CALL_EXPRESSION)
				{
					ValidateClassValueConstruction(type, initializer);
					initializer = BuildDirectClassValueTransfer(initializer, type);
				}
				else initializer.node =
					BuildClassValueConstructorAction(type, initializer);
			}
			else
			{
				const NodeId argument_list = FindChild(expression, "argument-list");
				if (argument_list != kNoNode)
					for (std::uint32_t argument = arena_->FirstEdge(argument_list);
						argument != kNoEdge; argument = arena_->NextEdge(argument))
						arguments.push_back(arena_->EdgeChild(argument));
				initializer.node = BuildConstructorAction(
					type, scope, arguments, false, false);
			}
		}
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
				if (initializer.category == VALUE_PRVALUE &&
					dump_.nodes[initializer.node].kind == DUMP_CALL_EXPRESSION)
				{
					ValidateClassValueConstruction(type, initializer);
					initializer = BuildDirectClassValueTransfer(initializer, type);
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
	}
	return local ? BuildLocalAggregateArrayActions(initializer) : initializer;
}

void SemanticAnalyzer::AddMemberInitializationAction(BindingId member_id,
	NodeId initializer, ScopeId scope, std::uint32_t body)
{
	const BindingRecord& member = program_->bindings[member_id];
	const EntityId member_entity = DestructedEntity(member.type);
	const TypeKind member_kind = program_->types.Get(member.type).kind;
	const bool class_member = member_kind != TYPE_LVALUE_REFERENCE &&
		member_kind != TYPE_RVALUE_REFERENCE &&
		IsClassEntity(*program_, member_entity);
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
					throw std::runtime_error(
						"unsupported class default member initializer");
				const NodeId list = FindChild(initializer, "argument-list");
				if (list != kNoNode)
					for (std::uint32_t edge = arena_->FirstEdge(list);
						edge != kNoEdge; edge = arena_->NextEdge(edge))
						arguments.push_back(arena_->EdgeChild(edge));
				constructor_copy = false;
			}
			else if (initializer != kNoNode)
				arguments.push_back(initializer);
			value = BuildConstructorAction(member.type, scope, arguments,
				constructor_copy, constructor_list);
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
			std::size_t count = 0;
			for (std::uint32_t edge = arena_->FirstEdge(initializer);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
			{
				if (++count > 1)
					throw std::runtime_error(
						"scalar member has multiple initializers");
				const ExpressionInfo value = AnalyzeExpression(
					arena_->EdgeChild(edge), scope, member.type);
				dump_.Add(list, value.node);
			}
			dump_.Add(action, list);
		}
	}
	else if (initializer != kNoNode)
		dump_.Add(action, AnalyzeExpression(initializer, scope, member.type).node);
	dump_.Add(body, action);
	++expression_count_;
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
	NodeId base_initializer = kNoNode;
	bool base_initializer_seen = false;
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
		base_initializer_seen = true;
		++constructor_base_action_visits_;
		++expression_count_;
	}
	else if (constructor.constructor_initializer != kNoNode)
	{
		for (std::uint32_t edge = arena_->FirstEdge(
			constructor.constructor_initializer); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId initializer = arena_->EdgeChild(edge);
			if (!arena_->IsTag(initializer, "mem-initializer")) continue;
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
				if (child != id) value = child;
			}
			if (value == kNoNode)
				throw std::runtime_error("member initializer has no value");
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
			const LookupResult type = LookupSpelling(function_scope,
				arena_->Payload(id), LOOKUP_TYPE);
			if (program_->entities[entity].direct_base == kNoEntity ||
				EntityOf(type.type) != program_->entities[entity].direct_base)
				throw std::runtime_error(
					"unknown constructor member initializer");
			if (type.type_declaration != kNoBinding &&
				!CanAccessMember(type.type_declaration, type.naming_class))
				throw std::runtime_error("inaccessible base initializer type");
			if (base_initializer_seen)
				throw std::runtime_error("duplicate base initializer");
			base_initializer = value;
			base_initializer_seen = true;
		}
	}
	if (program_->entities[entity].direct_base != kNoEntity &&
		constructor.inherited_constructor_source == kNoBinding)
		AddBaseInitializationAction(entity, base_initializer,
			function_scope, body);
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
	NodeId initializer, ScopeId scope, std::uint32_t body)
{
	const EntityId base = program_->entities[entity].direct_base;
	if (base == kNoEntity)
		throw std::logic_error("base initialization has no direct base");
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
	const std::uint32_t constructor = BuildConstructorAction(base_type,
		scope, arguments, false, list_initialization, true);
	dump_.Add(action, constructor);
	dump_.Add(body, action);
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
		const DumpNode& record = dump_.nodes[node];
		if (record.kind == DUMP_CONSTRUCTOR_ACTION)
		{
			if (record.binding == kNoBinding ||
				!program_->bindings[record.binding].nonthrowing)
				return false;
		}
		else if (record.kind == DUMP_CALL_EXPRESSION)
		{
			bool known_nonthrowing = false;
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
				false, true);
			result.type = type;
			result.category = VALUE_NONE;
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

ExpressionInfo SemanticAnalyzer::AnalyzeArrayAggregateInit(TypeId type,
	ScopeId scope, std::uint32_t* element_edge)
{
	const TypeRecord array = program_->types.Get(
		program_->types.RemoveTopCv(type));
	if (array.kind != TYPE_ARRAY)
		throw std::logic_error("array initialization has non-array type");
	const std::uint32_t list = MakeDump(DUMP_BRACED_INIT_LIST,
		type, VALUE_LVALUE);
	std::size_t count = 0;
	while (*element_edge != kNoEdge &&
		(array.bound == 0 || count < array.bound))
	{
		const std::uint32_t before = *element_edge;
		const ExpressionInfo value = AnalyzeAggregateElement(
			array.child, scope, element_edge);
		if (value.node == kNoDumpEdge || *element_edge == before)
			throw std::logic_error("array initializer made no progress");
		dump_.Add(list, value.node);
		++count;
	}
	if (array.bound != 0)
	{
		while (count < array.bound)
		{
			std::uint32_t omitted = kNoEdge;
			const ExpressionInfo value = AnalyzeAggregateElement(
				array.child, scope, &omitted);
			if (value.node != kNoDumpEdge) dump_.Add(list, value.node);
			++count;
		}
	}
	else
	{
		type = program_->types.Array(array.child, count);
		dump_.nodes[list].type = type;
	}
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
	const bool class_type = IsClassEntity(*program_, entity);
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
			for (std::size_t i = 0; i < bytes.size(); ++i)
			{
				ExpressionInfo value = MakeLiteral(record.child,
					InternNumber(bytes[i]));
				value.constant = true;
				value.value = bytes[i];
				dump_.nodes[value.node].constant = true;
				dump_.nodes[value.node].constant_value = bytes[i];
				dump_.Add(list, value.node);
			}
			*element_edge = arena_->NextEdge(source_edge);
			ExpressionInfo result;
			result.node = list;
			result.type = initialized_type;
			result.category = VALUE_LVALUE;
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
			return AnalyzeAggregateInit(type, scope, element_edge);
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

ExpressionInfo SemanticAnalyzer::AnalyzeAggregateInit(TypeId type,
	ScopeId scope, std::uint32_t* element_edge)
{
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity || !program_->entities[entity].is_aggregate)
		throw std::runtime_error("class is not an aggregate");
	if (entity >= entity_data_members_.size())
		throw std::logic_error("aggregate is missing its member index");
	const std::uint32_t list = MakeDump(DUMP_BRACED_INIT_LIST,
		type, VALUE_LVALUE);
	const std::vector<BindingId>& members = entity_data_members_[entity];
	const std::size_t member_count =
		program_->entities[entity].flavor == NAMED_UNION ?
			(members.empty() ? 0 : 1) :
			members.size();
	for (std::size_t i = 0; i < member_count; ++i)
	{
		const BindingId member_id = members[i];
		const BindingRecord& member = program_->bindings[member_id];
		const std::uint32_t action = MakeDump(DUMP_INITIALIZER_ACTION,
			member.type, VALUE_NONE, member.name, member_id);
		const ExpressionInfo value = AnalyzeAggregateElement(
			member.type, scope, element_edge);
		if (value.node != kNoDumpEdge) dump_.Add(action, value.node);
		dump_.Add(list, action);
		++expression_count_;
	}
	ExpressionInfo result;
	result.node = list;
	result.type = type;
	result.category = VALUE_LVALUE;
	++expression_count_;
	return result;
}

std::uint32_t SemanticAnalyzer::BuildAggregateConstructionAction(TypeId type,
	std::uint32_t aggregate_list)
{
	const EntityId entity = EntityOf(type);
	if (!IsClassEntity(*program_, entity) ||
		!program_->entities[entity].is_aggregate)
		throw std::logic_error("aggregate helper has non-aggregate type");
	std::vector<std::uint32_t> values;
	std::vector<BindingId> members;
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
		if (kind == TYPE_ARRAY || kind == TYPE_LVALUE_REFERENCE ||
			kind == TYPE_RVALUE_REFERENCE ||
			IsClassEntity(*program_, EntityOf(action.type)))
			return aggregate_list;
		values.push_back(value);
		members.push_back(action.binding);
		const TypeId adjusted = AdjustParameterType(action.type);
		parameter_types.push_back(adjusted);
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
			entity, type, function_type, members));
		aggregate_helper_index_.Insert(key,
			static_cast<BindingId>(helper));
	}
	else
	{
		helper = static_cast<std::uint32_t>(encoded);
		if (helper >= aggregate_helpers_.size() ||
			aggregate_helpers_[helper].members != members)
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
			const bool list = initializer != kNoNode &&
				arena_->IsTag(initializer, "braced-init-list");
			if (initializer != kNoNode &&
				(arena_->IsTag(initializer, "paren-initializer") || list))
				for (std::uint32_t edge = arena_->FirstEdge(initializer);
					edge != kNoEdge; edge = arena_->NextEdge(edge))
					constructor_arguments.push_back(arena_->EdgeChild(edge));
			construction = BuildConstructorAction(object_type, scope,
				constructor_arguments, false, list);
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
	if (!IsClassEntity(*program_, EntityOf(initializer.type)))
		return initializer;
	const std::uint32_t temporary = MakeDump(DUMP_TEMPORARY_OBJECT,
		initializer.type, VALUE_XVALUE);
	dump_.Add(temporary, initializer.node);
	const DumpNode& action = dump_.nodes[initializer.node];
	if (action.kind == DUMP_CONSTRUCTOR_ACTION &&
		action.binding != kNoBinding)
		DemandFunction(action.binding);
	ExpressionInfo result = initializer;
	result.node = temporary;
	result.category = VALUE_XVALUE;
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeClassFunctionalCast(TypeId cast_type,
	ScopeId scope, const std::vector<NodeId>& argument_syntax,
	NodeId arguments_node, TypeId target)
{
	const EntityId cast_entity = EntityOf(cast_type);
	if (argument_syntax.size() == 1)
	{
		const ExpressionInfo operand =
			AnalyzeExpression(argument_syntax[0], scope);
		if (EntityOf(operand.type) != kNoEntity &&
			ConvertingFunction(operand, cast_type, true).rank !=
				CONVERSION_INVALID)
		{
			ExpressionInfo converted =
				ApplyExplicitConversion(operand, cast_type);
			converted = MaterializeTemporary(converted);
			return target == kNoType ? converted : ApplyTarget(converted, target);
		}
	}
	if (program_->entities[cast_entity].is_aggregate &&
		arguments_node != kNoNode &&
		arena_->IsTag(arguments_node, "braced-init-list"))
	{
		ExpressionInfo result = AnalyzeBracedInit(
			arguments_node, scope, cast_type);
		return target == kNoType ? MaterializeTemporary(result) : result;
	}
	if (program_->entities[cast_entity].is_aggregate && argument_syntax.empty())
	{
		if (target == kNoType)
		{
			const std::vector<NodeId> no_arguments;
			ExpressionInfo initialized;
			initialized.node = BuildConstructorAction(
				cast_type, scope, no_arguments, false, false);
			initialized.type = cast_type;
			initialized.category = VALUE_PRVALUE;
			dump_.nodes[initialized.node].value_initialization = true;
			return MaterializeTemporary(initialized);
		}
		std::uint32_t empty = kNoEdge;
		ExpressionInfo result = AnalyzeAggregateInit(cast_type, scope, &empty);
		dump_.nodes[result.node].value_initialization = true;
		return result;
	}
	ExpressionInfo result;
	result.node = BuildConstructorAction(cast_type, scope, argument_syntax,
		false, arguments_node != kNoNode &&
		arena_->IsTag(arguments_node, "braced-init-list"));
	result.type = cast_type;
	result.category = VALUE_PRVALUE;
	if (argument_syntax.empty() &&
		!program_->entities[cast_entity].has_user_provided_constructor)
		dump_.nodes[result.node].value_initialization = true;
	return target == kNoType ? MaterializeTemporary(result) :
		ApplyTarget(result, target);
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
		if (default_constructor_demand_states_.size() <= entity)
			default_constructor_demand_states_.resize(
				static_cast<std::size_t>(entity) + 1, 0);
		if (default_constructor_demand_states_[entity] == 0)
		{
			default_constructor_demand_states_[entity] = 1;
			demanded_default_constructor_entities_.push_back(entity);
			++demand_worklist_pushes_;
		}
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

bool SemanticAnalyzer::IsEmptyUnionDestructor(BindingId destructor) const
{
	if (destructor == kNoBinding || destructor >= program_->bindings.size())
		return false;
	const BindingRecord& binding = program_->bindings[destructor];
	if (binding.member_owner == kNoEntity ||
		program_->entities[binding.member_owner].flavor != NAMED_UNION)
		return false;
	const FunctionInfo& info = GetFunction(destructor);
	if (info.definition_body == kNoNode ||
		FirstSemanticChild(info.definition_body) != kNoNode)
		return false;
	const EntityId entity = binding.member_owner;
	if (entity >= entity_data_members_.size()) return true;
	const std::vector<BindingId>& variants = entity_data_members_[entity];
	for (std::size_t i = 0; i < variants.size(); ++i)
	{
		const EntityId variant =
			DestructedEntity(program_->bindings[variants[i]].type);
		if (variant != kNoEntity &&
			!program_->entities[variant].trivial_destructor)
			return false;
	}
	return true;
}

void SemanticAnalyzer::AddLifetimeObligation(ScopeId scope,
	BindingId object, TypeId type)
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
	if (program_->entities[entity].trivial_destructor ||
		IsEmptyUnionDestructor(destructor)) return;
	if (scope_lifetimes_.size() <= scope)
		scope_lifetimes_.resize(static_cast<std::size_t>(scope) + 1);
	scope_lifetimes_[scope].push_back(
		LifetimeObligation(object, destructor, type));
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
			dump_.Add(output_parent, MakeDestructorAction(obligation.type,
				obligation.destructor, obligation.object));
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
		const BindingId destructor = DestructorForType(
			program_->entities[base].type);
		if (destructor == kNoBinding)
			throw std::logic_error("base has no destructor identity");
		if (!CanAccessMember(destructor, base))
			throw std::runtime_error("inaccessible base destructor");
		dump_.Add(body, MakeDestructorAction(program_->entities[base].type,
			destructor, kNoBinding, 1));
		++destructor_subobject_action_visits_;
	}
}

}
}
