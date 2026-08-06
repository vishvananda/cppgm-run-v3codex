#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <vector>

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

ExpressionInfo SemanticAnalyzer::AnalyzeBracedInit(NodeId node, ScopeId scope,
	TypeId target)
{
	if (target == kNoType) throw std::runtime_error("untyped braced-init-list");
	TypeId type = target;
	const TypeRecord array = program_->types.Get(type);
	const EntityId class_entity = EntityOf(type);
	if (IsClassEntity(*program_, class_entity))
	{
		std::vector<NodeId> elements;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
			elements.push_back(arena_->EdgeChild(edge));
		std::size_t cursor = 0;
		ExpressionInfo result = AnalyzeAggregateInit(type, scope, elements, &cursor);
		if (cursor != elements.size())
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
	ScopeId scope, const std::vector<NodeId>& elements, std::size_t* cursor)
{
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity || !program_->entities[entity].is_aggregate)
		throw std::runtime_error("class is not an aggregate");
	if (entity >= entity_data_members_.size())
		throw std::logic_error("aggregate is missing its member index");
	const std::uint32_t list = MakeDump(DUMP_BRACED_INIT_LIST,
		type, VALUE_LVALUE);
	const std::vector<BindingId>& members = entity_data_members_[entity];
	for (std::size_t i = 0; i < members.size(); ++i)
	{
		const BindingId member_id = members[i];
		const BindingRecord& member = program_->bindings[member_id];
		const std::uint32_t action = MakeDump(DUMP_INITIALIZER_ACTION,
			member.type, VALUE_NONE, member.name, member_id);
		const EntityId member_entity = EntityOf(member.type);
		const bool class_member = IsClassEntity(*program_, member_entity);
		if (*cursor < elements.size())
		{
			const NodeId source = elements[*cursor];
			ExpressionInfo value;
			if (class_member &&
				program_->entities[member_entity].is_aggregate)
			{
				if (arena_->IsTag(source, "braced-init-list"))
				{
					++*cursor;
					value = AnalyzeBracedInit(source, scope, member.type);
				}
				else
					value = AnalyzeAggregateInit(member.type, scope,
						elements, cursor);
			}
			else
			{
				++*cursor;
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
				scope, elements, cursor);
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

}
}
