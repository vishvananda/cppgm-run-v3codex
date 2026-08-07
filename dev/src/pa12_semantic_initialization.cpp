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
	bool copy_initialization, bool list_initialization, bool base_subobject)
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
		values.push_back(AnalyzeExpression(arena_->EdgeChild(edge), scope, element));
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
			(*element_edge == kNoEdge || members.empty() ? 0 : 1) :
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

ExpressionInfo SemanticAnalyzer::AnalyzeNewExpression(NodeId node,
	ScopeId scope, TypeId target)
{
	const NodeId type_node = FindChild(node, "type-id");
	if (type_node == kNoNode)
		throw std::runtime_error("new-expression has no allocated type");
	const TypeId object_type = BuildTypeId(type_node, scope);
	if (program_->types.Get(program_->types.RemoveTopCv(object_type)).kind ==
		TYPE_ARRAY)
		throw std::runtime_error("array new is outside PA16");
	const NodeId placement = FindChild(node, "placement");
	if (placement == kNoNode)
		throw std::runtime_error("ordinary new is outside PA16");
	const NodeId placement_arguments = FindChild(placement,
		"paren-argument-list");
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
	std::vector<BindingId> candidates = FunctionCandidates(scope,
		"operatornew");
	if (candidates.empty())
		throw std::runtime_error("placement operator new was not declared");
	const BindingId selected = SelectOverload(scope, argument_syntax,
		arguments, candidates);
	ExpressionInfo allocation = BuildResolvedCall(selected, scope,
		argument_syntax, arguments, 0, kNoType);
	const NodeId initializer_node = FindChild(node, "initializer");
	NodeId initializer = initializer_node == kNoNode ? kNoNode :
		FirstSemanticChild(initializer_node);
	std::uint32_t construction = kNoDumpEdge;
	const EntityId entity = EntityOf(object_type);
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
		}
	}
	else if (initializer != kNoNode)
		construction = AnalyzeExpression(initializer, scope, object_type).node;
	const TypeId result_type = program_->types.Pointer(object_type);
	const std::uint32_t result_node = MakeDump(DUMP_NEW_EXPRESSION,
		result_type, VALUE_PRVALUE);
	dump_.nodes[result_node].operand_type = object_type;
	dump_.Add(result_node, allocation.node);
	if (construction != kNoDumpEdge) dump_.Add(result_node, construction);
	ExpressionInfo result;
	result.node = result_node;
	result.type = result_type;
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
	if (argument_syntax.empty())
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
	if (program_->entities[entity].trivial_destructor) return;
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
