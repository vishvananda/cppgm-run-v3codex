#include "pa12_semantic_detail.h"

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
				else rank = Conversion(arguments[a], parameters[a]);
			}
			ranks[c * arity + a] = rank;
			if (rank == CONVERSION_INVALID) viable[c] = false;
		}
	}
	const auto better = [this, &ranks, arity](std::size_t left,
		std::size_t right) -> bool
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
		return no_worse && strictly_better;
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
	if (constructor.deleted_constructor)
		throw std::runtime_error("selected constructor is deleted");
	if (copy_initialization && constructor.explicit_constructor)
		throw std::runtime_error(
			"copy initialization selected an explicit constructor");
	if (!CanAccessMember(selected))
		throw std::runtime_error("inaccessible constructor");
	(void)scope;
	return selected;
}

std::uint32_t SemanticAnalyzer::BuildConstructorAction(TypeId type,
	ScopeId scope, const std::vector<NodeId>& argument_syntax,
	bool copy_initialization, bool list_initialization)
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
	const BindingId selected = SelectConstructor(scope, argument_syntax,
		arguments, candidates, copy_initialization, list_initialization);
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
	for (std::size_t a = 0; a < argument_syntax.size(); ++a)
	{
		ExpressionInfo argument = arguments[a];
		if (a < function_type.parameter_count)
		{
			if (argument.type == kNoType)
				argument = AnalyzeExpression(argument_syntax[a], scope,
					parameters[a]);
			else argument = ApplyTarget(argument, parameters[a]);
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
	if (!(constructor.implicit_constructor &&
		program_->entities[entity].trivial_default_constructor))
		DemandFunction(selected);
	++expression_count_;
	return action;
}

void SemanticAnalyzer::AddMemberInitializationAction(BindingId member_id,
	NodeId initializer, ScopeId scope, std::uint32_t body)
{
	const BindingRecord& member = program_->bindings[member_id];
	const EntityId member_entity = EntityOf(member.type);
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
		if (initializer != kNoNode && arena_->IsTag(initializer, "braced-init-list") &&
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
	if (constructor.constructor_initializer != kNoNode)
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
	if (program_->entities[entity].direct_base != kNoEntity)
		AddBaseInitializationAction(entity, base_initializer,
			function_scope, body);
	for (std::size_t i = 0; i < members.size(); ++i)
	{
		++constructor_member_action_visits_;
		const BindingId member = members[i];
		NodeId initializer = constructor_initializer_scratch_[i];
		if (initializer == kNoNode &&
			member < member_initializer_by_binding_.size())
			initializer = member_initializer_by_binding_[member];
		AddMemberInitializationAction(member, initializer, function_scope, body);
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
		scope, arguments, false, list_initialization);
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
	const TypeRecord array = program_->types.Get(type);
	const EntityId class_entity = EntityOf(type);
	if (IsClassEntity(*program_, class_entity))
	{
		std::uint32_t element_edge = arena_->FirstEdge(node);
		ExpressionInfo result = AnalyzeAggregateInit(type, scope, &element_edge);
		if (element_edge != kNoEdge)
			throw std::runtime_error("excess aggregate initializer elements");
		return result;
	}
	TypeId element = type;
	if (array.kind == TYPE_ARRAY) element = array.child;
	std::vector<ExpressionInfo> values;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		values.push_back(AnalyzeExpression(arena_->EdgeChild(edge), scope, element));
	if (array.kind == TYPE_ARRAY && array.bound != 0 && values.size() > array.bound)
		throw std::runtime_error("excess array initializer elements");
	if (array.kind == TYPE_ARRAY && array.bound == 0)
		type = program_->types.Array(array.child, values.size());
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
			(*element_edge == kNoEdge || members.empty() ? 0 : 1) :
			members.size();
	for (std::size_t i = 0; i < member_count; ++i)
	{
		const BindingId member_id = members[i];
		const BindingRecord& member = program_->bindings[member_id];
		const std::uint32_t action = MakeDump(DUMP_INITIALIZER_ACTION,
			member.type, VALUE_NONE, member.name, member_id);
		const EntityId member_entity = EntityOf(member.type);
		const bool class_member = IsClassEntity(*program_, member_entity);
		if (*element_edge != kNoEdge)
		{
			const std::uint32_t source_edge = *element_edge;
			const NodeId source = arena_->EdgeChild(source_edge);
			ExpressionInfo value;
			if (class_member &&
				program_->entities[member_entity].is_aggregate)
			{
				if (arena_->IsTag(source, "braced-init-list"))
				{
					*element_edge = arena_->NextEdge(source_edge);
					value = AnalyzeBracedInit(source, scope, member.type);
				}
				else
					value = AnalyzeAggregateInit(member.type, scope,
						element_edge);
			}
			else
			{
				*element_edge = arena_->NextEdge(source_edge);
				value = AnalyzeExpression(source, scope, member.type);
			}
			dump_.Add(action, value.node);
		}
		else if (class_member)
		{
			if (!program_->entities[member_entity].is_aggregate)
				throw std::runtime_error(
					"omitted aggregate member requires construction");
			const ExpressionInfo value = AnalyzeAggregateInit(member.type,
				scope, element_edge);
			dump_.Add(action, value.node);
		}
		else
		{
			const TypeRecord member_type = program_->types.Get(member.type);
			if (member_type.kind == TYPE_LVALUE_REFERENCE ||
				member_type.kind == TYPE_RVALUE_REFERENCE)
				throw std::runtime_error(
					"omitted aggregate reference member");
		}
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

void SemanticAnalyzer::AddDefaultConstructor(std::uint32_t variable,
	BindingId binding, TypeId type)
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity) return;
	const NamedFlavor flavor = program_->entities[entity].flavor;
	if (flavor != NAMED_STRUCT && flavor != NAMED_CLASS &&
		flavor != NAMED_UNION) return;
	const EntityRecord& class_record = program_->entities[entity];
	if (!class_record.default_constructible)
		throw std::runtime_error("class has no usable default constructor");
	const std::vector<NodeId> arguments;
	const std::uint32_t action = BuildConstructorAction(type,
		program_->bindings[binding].owner, arguments, false, false);
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

}
}
