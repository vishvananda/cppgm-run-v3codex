#include "pa12_semantic_detail.h"

#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

ExpressionInfo SemanticAnalyzer::AnalyzeArrayAggregateInit(TypeId type,
	ScopeId scope, std::uint32_t* element_edge)
{
	const TypeRecord array = program_->types.Get(
		program_->types.RemoveTopCv(type));
	if (array.kind != TYPE_ARRAY)
		throw std::logic_error("array initialization has non-array type");
	const std::uint32_t list = MakeDump(DUMP_BRACED_INIT_LIST,
		type, VALUE_LVALUE);
	std::vector<ConstexprObjectElement> constant_elements;
	if (array.bound != 0 && array.bound <=
		std::numeric_limits<std::size_t>::max())
		constant_elements.reserve(static_cast<std::size_t>(array.bound));
	bool constant_object = true;
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
		ConstexprObjectElement element(
			kNoBinding, ConstexprScalarValue(static_cast<std::int64_t>(0)));
		if (constant_object && BuildConstexprObjectElement(
			array.child, kNoBinding, value, &element))
			constant_elements.push_back(element);
		else constant_object = false;
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
			ConstexprObjectElement element(
				kNoBinding, ConstexprScalarValue(static_cast<std::int64_t>(0)));
			if (constant_object && BuildConstexprObjectElement(
				array.child, kNoBinding, value, &element))
				constant_elements.push_back(element);
			else constant_object = false;
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
	if (constant_object && constant_elements.size() == count)
		SetExpressionObject(&result,
			InternConstexprObject(type, constant_elements));
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
			(members.empty() ? 0 : 1) : members.size();
	std::vector<ConstexprObjectElement> constant_elements;
	constant_elements.reserve(member_count);
	bool constant_object = true;
	for (std::size_t i = 0; i < member_count; ++i)
	{
		const BindingId member_id = members[i];
		const BindingRecord& member = program_->bindings[member_id];
		const std::uint32_t action = MakeDump(DUMP_INITIALIZER_ACTION,
			member.type, VALUE_NONE, member.name, member_id);
		const ExpressionInfo value = AnalyzeAggregateElement(
			member.type, scope, element_edge);
		if (value.node != kNoDumpEdge) dump_.Add(action, value.node);
		ConstexprObjectElement element(
			member_id, ConstexprScalarValue(static_cast<std::int64_t>(0)));
		if (constant_object && BuildConstexprObjectElement(
			member.type, member_id, value, &element))
			constant_elements.push_back(element);
		else constant_object = false;
		dump_.Add(list, action);
		++expression_count_;
	}
	ExpressionInfo result;
	result.node = list;
	result.type = type;
	result.category = VALUE_LVALUE;
	if (constant_object && constant_elements.size() == member_count)
		SetExpressionObject(&result,
			InternConstexprObject(type, constant_elements));
	++expression_count_;
	return result;
}

bool SemanticAnalyzer::MaterializeConstantDefinitionInitializer(
	BindingId binding, TypeId* type, ExpressionInfo* initializer)
{
	if (!program_->bindings[binding].constant) return false;
	const std::uint32_t object = BindingObject(binding);
	if (object != kNoConstexprObject)
	{
		*initializer = MaterializeConstexprObject(object, *type);
		*type = initializer->type;
		program_->bindings[binding].type = *type;
		return true;
	}
	initializer->type = *type;
	initializer->category = VALUE_PRVALUE;
	SetExpressionScalar(initializer, BindingScalar(binding));
	initializer->node = MakeDump(DUMP_LITERAL, *type, VALUE_PRVALUE,
		InternScalar(*type, ExpressionScalar(*initializer)));
	dump_.nodes[initializer->node].constant = true;
	if (!initializer->floating_constant)
		dump_.nodes[initializer->node].constant_value = initializer->value;
	++expression_count_;
	return true;
}

}
}
