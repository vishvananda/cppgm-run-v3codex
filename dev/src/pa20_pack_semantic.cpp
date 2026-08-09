#include "pa12_semantic_detail.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

bool HasFunctionParameterPack(const SyntaxArena& arena, NodeId node)
{
	if (node == kNoNode) return false;
	if (arena.IsTag(node, "parameter-declaration"))
	{
		for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
			edge = arena.NextEdge(edge))
		{
			const NodeId child = arena.EdgeChild(edge);
			if (!arena.IsTag(child, "declarator")) continue;
			for (std::uint32_t declarator_edge = arena.FirstEdge(child);
				declarator_edge != kNoEdge;
				declarator_edge = arena.NextEdge(declarator_edge))
				if (arena.IsTag(arena.EdgeChild(declarator_edge),
					"parameter-pack")) return true;
		}
	}
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
		if (HasFunctionParameterPack(arena, arena.EdgeChild(edge))) return true;
	return false;
}

}

ExpressionInfo SemanticAnalyzer::AnalyzeSizeofPackExpression(
	NodeId node, ScopeId scope)
{
	const NameId name = program_->names.Intern(arena_->Payload(node));
	std::vector<TemplateArgument> template_arguments;
	std::vector<BindingId> function_arguments;
	std::size_t count = 0;
	if (LookupTemplateArgumentPack(scope, name, &template_arguments))
		count = template_arguments.size();
	else if (LookupFunctionParameterPack(scope, name, &function_arguments))
		count = function_arguments.size();
	else throw std::runtime_error("sizeof names no parameter pack");
	ExpressionInfo result;
	result.type = program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	result.node = MakeDump(DUMP_SIZEOF_EXPRESSION, result.type, VALUE_PRVALUE);
	result.constant = true;
	result.value = static_cast<std::int64_t>(count);
	RecordExpressionFacts(result);
	++expression_count_;
	return result;
}

void SemanticAnalyzer::InitializeFunctionTemplatePackShape(
	FunctionTemplatePattern* pattern, const DeclaratorInfo& shape)
{
	pattern->function_parameter_pack =
		HasFunctionParameterPack(*arena_, pattern->declarator) &&
		program_->types.Get(pattern->shape_type).variadic;
	pattern->required_parameter_count =
		RequiredFunctionParameterCount(shape.parameters);
	if (pattern->function_parameter_pack &&
		pattern->required_parameter_count != 0)
		--pattern->required_parameter_count;
}

void SemanticAnalyzer::BindFunctionParameterPackElement(
	ScopeId scope, NameId pack, BindingId binding)
{
	if (pack == 0) return;
	const std::uint64_t key =
		(static_cast<std::uint64_t>(scope) << 32) | pack;
	CompactIndexSequence& elements =
		function_parameter_pack_bindings_.Ensure(key);
	if (binding != kNoBinding) elements.Push(binding);
}

NameId SemanticAnalyzer::FunctionParameterPackName(NodeId declarator)
{
	if (declarator == kNoNode) return 0;
	if (arena_->IsTag(declarator, "parameter-declaration"))
	{
		const NodeId parameter = FindChild(declarator, "declarator");
		if (parameter != kNoNode &&
			FindChild(parameter, "parameter-pack") != kNoNode)
			return DeclaratorName(parameter);
	}
	for (std::uint32_t edge = arena_->FirstEdge(declarator); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NameId name = FunctionParameterPackName(arena_->EdgeChild(edge));
		if (name != 0) return name;
	}
	return 0;
}

void SemanticAnalyzer::CollectPackExpansionNames(NodeId node, ScopeId scope,
	std::vector<NameId>* names) const
{
	if (node == kNoNode ||
		arena_->IsTag(node, "pack-expansion-expression")) return;
	const bool can_name_pack =
		arena_->IsTag(node, "id-expression") ||
		arena_->IsTag(node, "type-name") ||
		arena_->IsTag(node, "decl-specifier") ||
		arena_->IsTag(node, "name-component") ||
		arena_->IsTag(node, "sizeof-pack-expression");
	if (can_name_pack)
	{
		const std::string spelling = PayloadSource(node);
		if (!spelling.empty())
		{
			const NameId name = program_->names.Intern(spelling);
			std::vector<TemplateArgument> template_pack;
			std::vector<BindingId> function_pack;
			if ((LookupTemplateArgumentPack(scope, name, &template_pack) ||
				 LookupFunctionParameterPack(scope, name, &function_pack)) &&
				std::find(names->begin(), names->end(), name) == names->end())
				names->push_back(name);
		}
	}
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		CollectPackExpansionNames(arena_->EdgeChild(edge), scope, names);
}

bool SemanticAnalyzer::ExpandPackElementScopes(NodeId pattern, ScopeId scope,
	std::vector<ScopeId>* element_scopes)
{
	std::vector<NameId> names;
	CollectPackExpansionNames(pattern, scope, &names);
	if (names.empty()) return false;
	std::vector<std::vector<TemplateArgument> > packs(names.size());
	std::size_t length = std::numeric_limits<std::size_t>::max();
	for (std::size_t source = 0; source < names.size(); ++source)
	{
		if (!LookupTemplateArgumentPack(scope, names[source], &packs[source]))
			throw std::runtime_error(
				"declaration pack expansion requires a template parameter pack");
		if (length == std::numeric_limits<std::size_t>::max())
			length = packs[source].size();
		else if (length != packs[source].size())
			throw std::runtime_error(
				"pack expansion operands have different lengths");
	}
	element_scopes->reserve(element_scopes->size() + length);
	for (std::size_t element = 0; element < length; ++element)
	{
		const ScopeId element_scope = NewScope(scope,
			SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(scope));
		for (std::size_t source = 0; source < names.size(); ++source)
		{
			TemplateParameter parameter;
			parameter.name = names[source];
			parameter.kind = packs[source][element].kind;
			BindTemplateArgument(element_scope, parameter,
				packs[source][element]);
		}
		element_scopes->push_back(element_scope);
	}
	return true;
}

void SemanticAnalyzer::BindLexicalTypeNames(NodeId pattern,
	ScopeId lexical_owner, ScopeId target_scope)
{
	if (pattern == kNoNode) return;
	if (arena_->IsTag(pattern, "name-component"))
	{
		const NameId name = program_->names.UseInterned(
			arena_->SemanticPayloadId(pattern));
		if (name != 0 && program_->LookupDirect(
			target_scope, name, LOOKUP_TYPE).type == kNoType)
		{
			const LookupResult lexical = program_->LookupDirect(
				lexical_owner, name, LOOKUP_TYPE);
			if (lexical.type != kNoType)
				program_->AddBinding(target_scope,
					BIND_TYPE_ALIAS, name, lexical.type);
		}
	}
	for (std::uint32_t edge = arena_->FirstEdge(pattern); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		BindLexicalTypeNames(arena_->EdgeChild(edge),
			lexical_owner, target_scope);
}

void SemanticAnalyzer::ExpandExpressionPack(NodeId expansion, ScopeId scope,
	std::vector<NodeId>* syntax,
	std::vector<ExpressionInfo>* expressions)
{
	if (!arena_->IsTag(expansion, "pack-expansion-expression"))
		throw std::logic_error("expression pack expansion node is invalid");
	const NodeId operand = FirstSemanticChild(expansion);
	if (operand == kNoNode)
		throw std::runtime_error("empty pack expansion expression");
	std::vector<NameId> names;
	CollectPackExpansionNames(operand, scope, &names);
	if (names.empty())
		throw std::runtime_error("pack expansion contains no unexpanded pack");
	std::vector<std::vector<TemplateArgument> > template_packs(names.size());
	std::vector<std::vector<BindingId> > function_packs(names.size());
	std::vector<std::uint8_t> is_template(names.size(), 0);
	std::size_t length = std::numeric_limits<std::size_t>::max();
	for (std::size_t source = 0; source < names.size(); ++source)
	{
		if (LookupTemplateArgumentPack(
			scope, names[source], &template_packs[source]))
			is_template[source] = 1;
		else if (!LookupFunctionParameterPack(
			scope, names[source], &function_packs[source]))
			throw std::logic_error("collected pack binding disappeared");
		const std::size_t source_length = is_template[source] ?
			template_packs[source].size() : function_packs[source].size();
		if (length == std::numeric_limits<std::size_t>::max())
			length = source_length;
		else if (length != source_length)
			throw std::runtime_error(
				"pack expansion operands have different lengths");
	}
	for (std::size_t element = 0; element < length; ++element)
	{
		const ScopeId element_scope = NewScope(scope,
			SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(scope));
		for (std::size_t source = 0; source < names.size(); ++source)
		{
			if (is_template[source])
			{
				TemplateParameter parameter;
				parameter.name = names[source];
				parameter.kind = template_packs[source][element].kind;
				BindTemplateArgument(element_scope, parameter,
					template_packs[source][element]);
			}
			else
			{
				const BindingId binding = function_packs[source][element];
				if (binding >= program_->bindings.size())
					throw std::logic_error(
						"function parameter pack binding is invalid");
				const BindingRecord& record = program_->bindings[binding];
				program_->AddBinding(element_scope, BIND_PARAMETER, names[source],
					record.type, record.constant, record.value, NAMED_NONE, 0,
					binding, false);
			}
		}
		syntax->push_back(kNoNode);
		expressions->push_back(AnalyzeExpression(operand, element_scope));
	}
}

bool SemanticAnalyzer::TryAnalyzeExpandedBracedInit(
	NodeId node, ScopeId scope, TypeId target, ExpressionInfo* result)
{
	bool has_expansion = false;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (arena_->IsTag(
			arena_->EdgeChild(edge), "pack-expansion-expression"))
			has_expansion = true;
	if (!has_expansion) return false;
	std::vector<NodeId> syntax;
	std::vector<ExpressionInfo> values;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, "pack-expansion-expression"))
			ExpandExpressionPack(child, scope, &syntax, &values);
		else
		{
			syntax.push_back(child);
			values.push_back(AnalyzeExpression(child, scope));
		}
	}
	TypeId object = program_->types.RemoveTopCv(target);
	const TypeRecord record = program_->types.Get(object);
	const EntityId entity = EntityOf(object);
	const bool class_aggregate = record.kind == TYPE_NAMED &&
		IsClassObjectType(object) && program_->entities[entity].is_aggregate;
	if (class_aggregate)
	{
		if (entity >= entity_data_members_.size())
			throw std::logic_error("aggregate is missing its member index");
		const std::vector<BindingId>& members = entity_data_members_[entity];
		const std::size_t member_count =
			program_->entities[entity].flavor == NAMED_UNION ?
				(members.empty() ? 0 : 1) : members.size();
		if (values.size() > member_count)
			throw std::runtime_error("excess aggregate initializer elements");
		const std::uint32_t list = MakeDump(
			DUMP_BRACED_INIT_LIST, target, VALUE_LVALUE);
		for (std::size_t i = 0; i < member_count; ++i)
		{
			const BindingRecord& member = program_->bindings[members[i]];
			const std::uint32_t action = MakeDump(DUMP_INITIALIZER_ACTION,
				member.type, VALUE_NONE, member.name, members[i]);
			ExpressionInfo value;
			if (i < values.size())
			{
				if (IsBracedNarrowing(values[i], member.type))
					throw std::runtime_error(
						"narrowing aggregate initialization conversion");
				value = ApplyTarget(values[i], member.type);
			}
			else
			{
				std::uint32_t omitted = kNoEdge;
				value = AnalyzeAggregateElement(
					member.type, scope, &omitted);
			}
			if (value.node != kNoDumpEdge) dump_.Add(action, value.node);
			dump_.Add(list, action);
			++expression_count_;
		}
		result->node = list;
		result->type = target;
		result->category = VALUE_LVALUE;
		++expression_count_;
		return true;
	}
	if (record.kind == TYPE_ARRAY)
	{
		if (record.bound != 0 && values.size() > record.bound)
			throw std::runtime_error("excess array initializer elements");
		const std::size_t count = record.bound == 0 ?
			values.size() : record.bound;
		const TypeId initialized = record.bound == 0 ?
			program_->types.Array(record.child, count) : target;
		const std::uint32_t list = MakeDump(
			DUMP_BRACED_INIT_LIST, initialized, VALUE_LVALUE);
		for (std::size_t i = 0; i < count; ++i)
		{
			ExpressionInfo value;
			if (i < values.size())
			{
				if (IsBracedNarrowing(values[i], record.child))
					throw std::runtime_error(
						"narrowing array initialization conversion");
				value = ApplyTarget(values[i], record.child);
			}
			else
			{
				std::uint32_t omitted = kNoEdge;
				value = AnalyzeAggregateElement(
					record.child, scope, &omitted);
			}
			if (value.node != kNoDumpEdge) dump_.Add(list, value.node);
		}
		result->node = list;
		result->type = initialized;
		result->category = VALUE_LVALUE;
		RecordExpressionFacts(*result);
		++expression_count_;
		return true;
	}
	if (values.size() != 1)
		throw std::runtime_error("scalar pack initialization has invalid arity");
	*result = ApplyTarget(values[0], target);
	return true;
}

bool SemanticAnalyzer::ExpandCallArgumentPacks(
	const std::vector<NodeId>& original, ScopeId scope,
	std::vector<NodeId>* syntax, std::vector<ExpressionInfo>* arguments)
{
	bool has_expansion = false;
	for (std::size_t i = 0; i < original.size(); ++i)
		if (arena_->IsTag(original[i], "pack-expansion-expression"))
			has_expansion = true;
	if (!has_expansion) return false;
	const std::vector<NodeId> input = original;
	syntax->clear();
	arguments->clear();
	for (std::size_t i = 0; i < input.size(); ++i)
	{
		if (!arena_->IsTag(input[i], "pack-expansion-expression"))
		{
			syntax->push_back(input[i]);
			arguments->push_back(AnalyzeExpression(input[i], scope));
			continue;
		}
		ExpandExpressionPack(input[i], scope, syntax, arguments);
	}
	return true;
}

}
}
