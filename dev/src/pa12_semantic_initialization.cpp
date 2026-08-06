#include "pa12_semantic_detail.h"

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

}
}
