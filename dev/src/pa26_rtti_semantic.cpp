#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

using namespace pa11;

namespace
{

bool IsClassFlavor(NamedFlavor flavor)
{
	return flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
		flavor == NAMED_UNION;
}

}

ExpressionInfo SemanticAnalyzer::AnalyzeTypeid(NodeId node, ScopeId scope)
{
	const LookupResult type_info = LookupSpelling(
		scope, "::std::type_info", LOOKUP_TYPE);
	if (type_info.type == kNoType)
		throw std::runtime_error("typeid requires std::type_info");
	const TypeId result_type = program_->types.Qualify(
		program_->types.RemoveTopCv(type_info.type), CV_CONST);

	const NodeId type_id = FindChild(node, "type-id");
	TypeId queried = kNoType;
	ExpressionInfo operand;
	bool dynamic = false;
	if (type_id != kNoNode)
	{
		++class_template_completion_suppressed_depth_;
		try
		{
			queried = BuildTypeId(type_id, scope);
		}
		catch (...)
		{
			--class_template_completion_suppressed_depth_;
			throw;
		}
		--class_template_completion_suppressed_depth_;
	}
	else
	{
		const NodeId operand_syntax = FirstSemanticChild(node);
		if (operand_syntax == kNoNode)
			throw std::runtime_error("typeid expression has no operand");
		++unevaluated_depth_;
		try
		{
			operand = AnalyzeExpression(operand_syntax, scope);
		}
		catch (...)
		{
			--unevaluated_depth_;
			throw;
		}
		--unevaluated_depth_;
		if (CandidateSubstitutionFailed()) return ExpressionInfo();
		queried = program_->types.RemoveTopCv(EffectiveType(operand.type));
		const EntityId entity = EntityOf(queried);
		dynamic = operand.category == VALUE_LVALUE &&
			entity != kNoEntity &&
			program_->entities[entity].polymorphic_class;
		if (dynamic) MarkVtableDemand(entity);
	}
	if (CandidateSubstitutionFailed() || queried == kNoType)
		return ExpressionInfo();
	queried = program_->types.RemoveTopCv(EffectiveType(queried));
	const TypeRecord& queried_record = program_->types.Get(queried);
	if (queried_record.kind == TYPE_NAMED)
	{
		const NamedFlavor flavor =
			program_->entities[queried_record.entity].flavor;
		if (IsClassFlavor(flavor) && !program_->entities[
			queried_record.entity].lambda_closure)
			EnsureClassDefinition(queried);
	}

	ExpressionInfo result;
	result.node = MakeDump(
		DUMP_TYPEID_EXPRESSION, result_type, VALUE_LVALUE);
	dump_.nodes[result.node].operand_type = queried;
	dump_.nodes[result.node].dynamic_type_query = dynamic;
	if (dynamic) dump_.Add(result.node, operand.node);
	result.type = result_type;
	result.category = VALUE_LVALUE;
	++expression_count_;
	return result;
}

bool SemanticAnalyzer::TryAnalyzeTypeidComparison(
	const std::string& operation, const std::string& display_operation,
	NodeId left_syntax, NodeId right_syntax, const ExpressionInfo& left,
	const ExpressionInfo& right, ScopeId scope, ExpressionInfo* result)
{
	if ((operation != "==" && operation != "!=") ||
		left.node == kNoDumpEdge || right.node == kNoDumpEdge ||
		dump_.nodes[left.node].kind != DUMP_TYPEID_EXPRESSION ||
		dump_.nodes[right.node].kind != DUMP_TYPEID_EXPRESSION)
		return false;

	std::vector<NodeId> syntax;
	syntax.push_back(left_syntax);
	syntax.push_back(right_syntax);
	std::vector<ExpressionInfo> operands;
	operands.push_back(left);
	operands.push_back(right);
	ExpressionInfo validated;
	if (!TryAnalyzeOverloadedOperator(operation, scope, syntax, operands,
		false, kNoType, &validated))
		throw std::runtime_error(
			"typeid comparison requires std::type_info operator");

	const TypeId bool_type = program_->types.Fundamental(FUND_BOOL);
	const std::uint32_t expression = MakeDump(DUMP_BINARY_EXPRESSION,
		bool_type, VALUE_PRVALUE,
		program_->names.Intern(display_operation));
	dump_.nodes[expression].operand_type =
		program_->types.Pointer(left.type);
	dump_.Add(expression, left.node);
	dump_.Add(expression, right.node);
	result->node = expression;
	result->type = bool_type;
	result->category = VALUE_PRVALUE;
	++expression_count_;
	return true;
}

bool SemanticAnalyzer::TryAnalyzeDynamicCast(TypeId target,
	const ExpressionInfo& operand, ExpressionInfo* result)
{
	const TypeRecord& target_record = program_->types.Get(target);
	const bool reference = target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE;
	TypeId target_object = reference ? target_record.child :
		program_->types.RemoveTopCv(target);
	const TypeRecord& target_shape = program_->types.Get(
		program_->types.RemoveTopCv(target_object));
	if (!reference && target_shape.kind != TYPE_POINTER)
		throw std::runtime_error("dynamic_cast target is not a pointer or reference");
	if (!reference) target_object = target_shape.child;
	target_object = program_->types.RemoveTopCv(target_object);

	TypeId source_object = kNoType;
	if (reference)
		source_object = program_->types.RemoveTopCv(EffectiveType(operand.type));
	else
	{
		const TypeId source_pointer =
			program_->types.RemoveTopCv(Decay(operand.type));
		const TypeRecord& source_shape = program_->types.Get(source_pointer);
		if (source_shape.kind != TYPE_POINTER)
			throw std::runtime_error("dynamic_cast source is not a pointer");
		source_object = program_->types.RemoveTopCv(source_shape.child);
	}

	const EntityId source_entity = EntityOf(source_object);
	const EntityId target_entity = EntityOf(target_object);
	if (source_entity == kNoEntity || target_entity == kNoEntity ||
		!IsClassFlavor(program_->entities[source_entity].flavor) ||
		!IsClassFlavor(program_->entities[target_entity].flavor))
		throw std::runtime_error("dynamic_cast requires class operands");

	// Identity and derived-to-base conversions need no runtime RTTI query and
	// retain the existing cast path (including base projection facts).
	if (source_entity == target_entity ||
		program_->IsBaseOf(target_entity, source_entity))
		return false;

	EnsureClassDefinition(source_object);
	EnsureClassDefinition(target_object);
	if (!program_->entities[source_entity].polymorphic_class)
		throw std::runtime_error("dynamic_cast source is not polymorphic");
	if (program_->entities[source_entity].nonlinear_base_graph ||
		program_->entities[target_entity].nonlinear_base_graph)
		throw std::runtime_error(
			"dynamic_cast multiple inheritance is outside PA26");
	MarkVtableDemand(source_entity);
	MarkVtableDemand(target_entity);

	const ValueCategory category = reference ?
		(target_record.kind == TYPE_LVALUE_REFERENCE ?
			VALUE_LVALUE : VALUE_XVALUE) : VALUE_PRVALUE;
	result->node = MakeDump(
		DUMP_DYNAMIC_CAST_EXPRESSION, target, category);
	dump_.nodes[result->node].operand_type = source_object;
	dump_.nodes[result->node].dynamic_cast_reference = reference;
	dump_.Add(result->node, operand.node);
	result->type = target;
	result->category = category;
	++expression_count_;
	return true;
}

}
}
