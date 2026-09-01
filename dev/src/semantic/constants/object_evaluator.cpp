#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <limits>

namespace cppgm
{
namespace semantic
{

TypeId Analyzer::CompleteQualifiedStaticArrayType(
	BindingId prior, TypeId declared) const
{
	if (prior == kNoBinding || prior >= program_->bindings.size() ||
		program_->bindings[prior].kind != BIND_VARIABLE)
		return declared;
	const TypeId prior_type = program_->bindings[prior].type;
	const TypeRecord completed = program_->types.Get(prior_type);
	const TypeRecord candidate = program_->types.Get(declared);
	return completed.kind == TYPE_ARRAY && candidate.kind == TYPE_ARRAY &&
		completed.child == candidate.child && !completed.IsIncompleteArray() &&
		candidate.IsIncompleteArray() ? prior_type : declared;
}

bool Analyzer::IsStaticConstantDefinition(
	BindingId binding, NodeId initializer) const
{
	const BindingRecord& declared = program_->bindings[binding];
	const BindingRecord& canonical =
		program_->bindings[declared.canonical];
	const bool definition = declared.canonical != binding &&
		canonical.member_owner != kNoEntity &&
		!canonical.non_static_data_member && canonical.constant;
	const StaticConstantInitializerFact* recorded =
		FindStaticConstantInitializer(declared.canonical);
	const bool already_initialized =
		recorded && recorded->initializer != kNoDumpEdge;
	if (definition && already_initialized && initializer != kNoNode)
		ThrowSemanticError(
			"static constant definition must not have an initializer");
	return definition;
}

ExpressionInfo Analyzer::AnalyzeStringArrayInitializer(
	const ExpressionInfo& source, TypeId type, bool local)
{
	const TypeRecord declared = program_->types.Get(type);
	const TypeRecord source_array = program_->types.Get(source.type);
	if (source_array.kind != TYPE_ARRAY ||
		program_->types.RemoveTopCv(source_array.child) !=
			program_->types.RemoveTopCv(declared.child) ||
		source.string_unit_begin == kNoDumpEdge ||
		source.string_unit_count == 0)
		ThrowSemanticError(
			"string literal initializes an incompatible array");
	if (declared.bound != 0 && source.string_unit_count > declared.bound)
		ThrowSemanticError("string literal is too long for array");
	const std::size_t count = declared.bound == 0 ?
		source.string_unit_count : declared.bound;
	const TypeId initialized_type = declared.bound == 0 ?
		program_->types.Array(declared.child, count) : type;
	const std::uint32_t list = MakeDump(
		DUMP_BRACED_INIT_LIST, initialized_type, VALUE_LVALUE);
	std::vector<ConstexprObjectElement> constant_elements;
	constant_elements.reserve(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		const std::size_t unit = source.string_unit_begin + i;
		if (i < source.string_unit_count && unit >= string_literal_units_.size())
			ThrowInternalCompilerError(
				"string literal initializer range is invalid");
		const std::int64_t code_unit = i < source.string_unit_count ?
			NormalizeIntegralConstant(
				declared.child, string_literal_units_[unit]) : 0;
		ExpressionInfo value = MakeLiteral(
			declared.child, InternNumber(code_unit));
		SetExpressionScalar(&value, NormalizeScalarConstant(
			declared.child, ConstexprScalarValue(code_unit)));
		RecordExpressionFacts(value);
		dump_.Add(list, value.node);
		constant_elements.push_back(ConstexprObjectElement(
			kNoBinding, ExpressionScalar(value)));
	}
	ExpressionInfo result;
	result.node = list;
	result.type = initialized_type;
	result.category = VALUE_LVALUE;
	SetExpressionObject(&result,
		InternConstexprObject(initialized_type, constant_elements));
	++expression_count_;
	return local ? BuildLocalAggregateArrayActions(result) : result;
}

ExpressionInfo Analyzer::AnalyzeArrayAggregateInit(TypeId type,
	ScopeId scope, std::uint32_t* element_edge)
{
	const TypeRecord array = program_->types.Get(
		program_->types.RemoveTopCv(type));
	if (array.kind != TYPE_ARRAY)
		ThrowInternalCompilerError("array initialization has non-array type");
	const std::uint32_t list = MakeDump(DUMP_BRACED_INIT_LIST,
		type, VALUE_LVALUE);
	std::vector<ConstexprObjectElement> constant_elements;
	if (!array.IsIncompleteArray() && array.bound <=
		std::numeric_limits<std::size_t>::max())
		constant_elements.reserve(static_cast<std::size_t>(array.bound));
	bool constant_object = true;
	std::size_t count = 0;
	while (*element_edge != kNoEdge &&
		(array.IsIncompleteArray() || count < array.bound))
	{
		const std::uint32_t before = *element_edge;
		const ExpressionInfo value = AnalyzeAggregateElement(
			array.child, scope, element_edge);
		if (value.node == kNoDumpEdge || *element_edge == before)
			ThrowInternalCompilerError("array initializer made no progress");
		dump_.Add(list, value.node);
		ConstexprObjectElement element(
			kNoBinding, ConstexprScalarValue(static_cast<std::int64_t>(0)));
		if (constant_object && BuildConstexprObjectElement(
			array.child, kNoBinding, value, &element))
			constant_elements.push_back(element);
		else constant_object = false;
		++count;
	}
	if (!array.IsIncompleteArray())
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

ExpressionInfo Analyzer::AnalyzeAggregateInit(TypeId type,
	ScopeId scope, std::uint32_t* element_edge)
{
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity || !program_->entities[entity].is_aggregate)
		ThrowSemanticError("class is not an aggregate");
	if (entity >= entity_data_members_.size())
		ThrowInternalCompilerError("aggregate is missing its member index");
	if (element_edge && *element_edge != kNoEdge && arena_->IsTag(
		arena_->EdgeChild(*element_edge), "designated-initializer"))
		return AnalyzeDesignatedAggregateInit(type, scope, element_edge);
	const std::uint32_t list = MakeDump(DUMP_BRACED_INIT_LIST,
		type, VALUE_LVALUE);
	const std::size_t member_count =
		program_->entities[entity].flavor == NAMED_UNION ?
			(entity_data_members_[entity].empty() ? 0 : 1) :
			entity_data_members_[entity].size();
	std::vector<ConstexprObjectElement> constant_elements;
	constant_elements.reserve(member_count);
	bool constant_object = true;
	for (std::size_t i = 0; i < member_count; ++i)
	{
		// Element analysis may instantiate a class and grow both owner vectors.
		// Retain compact member facts, not references into either vector.
		const BindingId member_id = entity_data_members_[entity][i];
		const TypeId member_type = program_->bindings[member_id].type;
		const NameId member_name = program_->bindings[member_id].name;
		const std::uint32_t action = MakeDump(DUMP_INITIALIZER_ACTION,
			member_type, VALUE_NONE, member_name, member_id);
		const bool omitted_initializer = *element_edge == kNoEdge;
		const ExpressionInfo value = AnalyzeAggregateElement(
			member_type, scope, element_edge);
		dump_.nodes[action].value_initialization = omitted_initializer;
		if (value.node != kNoDumpEdge) dump_.Add(action, value.node);
		ConstexprObjectElement element(
			member_id, ConstexprScalarValue(static_cast<std::int64_t>(0)));
		if (constant_object && BuildConstexprObjectElement(
			member_type, member_id, value, &element))
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

ExpressionInfo Analyzer::BuildLocalAggregateArrayActions(
	const ExpressionInfo& initializer)
{
	const TypeRecord array = program_->types.Get(
		program_->types.RemoveTopCv(initializer.type));
	const EntityId element = array.kind == TYPE_ARRAY ?
		EntityOf(array.child) : kNoEntity;
	if (array.kind != TYPE_ARRAY || element == kNoEntity ||
		!program_->entities[element].is_aggregate ||
		dump_.nodes[initializer.node].kind != DUMP_BRACED_INIT_LIST)
		return initializer;
	bool has_array_member = false;
	if (element < entity_data_members_.size())
		for (std::size_t i = 0; i < entity_data_members_[element].size(); ++i)
			has_array_member = has_array_member || program_->types.Get(
				program_->types.RemoveTopCv(program_->bindings[
					entity_data_members_[element][i]].type)).kind == TYPE_ARRAY;
	if (!has_array_member)
		for (std::uint32_t edge = dump_.nodes[initializer.node].first_edge;
			edge != kNoDumpEdge; edge = dump_.edges[edge].next)
			for (std::uint32_t member = dump_.nodes[
				dump_.edges[edge].child].first_edge; member != kNoDumpEdge;
				member = dump_.edges[member].next)
				if (dump_.nodes[dump_.edges[member].child].first_edge == kNoDumpEdge)
					return initializer;
	for (std::uint32_t edge = dump_.nodes[initializer.node].first_edge;
		edge != kNoDumpEdge; edge = dump_.edges[edge].next)
	{
		const std::uint32_t element_node = dump_.edges[edge].child;
		if (dump_.nodes[element_node].kind != DUMP_BRACED_INIT_LIST)
			continue;
		const std::uint32_t replacement =
			BuildAggregateConstructionAction(array.child, element_node, true);
		dump_.edges[edge].child = replacement;
	}
	return initializer;
}

bool Analyzer::MaterializeConstantDefinitionInitializer(
	BindingId binding, TypeId* type, ExpressionInfo* initializer)
{
	if (!program_->bindings[binding].constant) return false;
	const BindingId canonical = program_->bindings[binding].canonical;
	const std::uint32_t object = BindingObject(binding);
	const std::uint32_t address = BindingAddress(binding);
	const bool prefer_materialized =
		PreferMaterializedConstantDefinition(canonical) &&
		(address != kNoConstexprAddress || object != kNoConstexprObject);
	const StaticConstantInitializerFact* recorded =
		FindStaticConstantInitializer(canonical);
	if (recorded && recorded->initializer != kNoDumpEdge &&
		!prefer_materialized)
	{
		const std::uint32_t node = recorded->initializer;
		if (node >= dump_.nodes.size())
			ThrowInternalCompilerError(
				"static constant initializer fact is out of range");
		DemandStaticConstantInitializerDependencies(binding);
		initializer->node = node;
		initializer->type = *type;
		initializer->category = VALUE_NONE;
		SetExpressionDumpObject(initializer);
		return true;
	}
	if (address != kNoConstexprAddress)
	{
		*initializer = MaterializeConstexprAddress(address, *type);
		return true;
	}
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
	{
		dump_.nodes[initializer->node].constant_value = initializer->value;
		dump_.nodes[initializer->node].constant_high =
			ExpressionScalar(*initializer).integral_high;
	}
	++expression_count_;
	return true;
}

}
}
