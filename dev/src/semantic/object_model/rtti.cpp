#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"
#include "support/scoped_state.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace cppgm
{
namespace semantic
{


namespace
{

std::uint8_t TopCv(const Program& program, TypeId type)
{
	const TypeRecord& record = program.types.Get(type);
	return record.kind == TYPE_QUALIFIED ? record.cv : CV_NONE;
}

}

ExpressionInfo Analyzer::AnalyzeTypeid(NodeId node, ScopeId scope)
{
	const bool enclosing_unevaluated = unevaluated_depth_ != 0;
	const LookupResult type_info = LookupPath(scope,
		GeneratedLibraryPath(GENERATED_LIBRARY_TYPE_INFO), LOOKUP_TYPE);
	if (type_info.type == kNoType)
		ThrowSemanticError("typeid requires std::type_info");
	const TypeId result_type = program_->types.Qualify(
		program_->types.RemoveTopCv(type_info.type), CV_CONST);

	const NodeId type_id = FindChild(node, ::cppgm::syntax::STAG_TYPE_ID);
	TypeId queried = kNoType;
	ExpressionInfo operand;
	bool dynamic = false;
	if (type_id != kNoNode)
	{
		{
			ScopedCounterIncrement suppressed(
				&class_template_completion_suppressed_depth_);
			queried = BuildTypeId(type_id, scope);
		}
	}
	else
	{
		const NodeId operand_syntax = FirstSemanticChild(node);
		if (operand_syntax == kNoNode)
			ThrowSemanticError("typeid expression has no operand");
		{
			ScopedCounterIncrement unevaluated(&unevaluated_depth_);
			ScopedCounterIncrement conditional(
				&conditionally_evaluated_operand_depth_);
			ScopedCounterIncrement suppressed(
				&resolved_call_demand_suppressed_depth_);
			operand = AnalyzeExpression(operand_syntax, scope);
		}
		if (CandidateSubstitutionFailed()) return ExpressionInfo();
		queried = program_->types.RemoveTopCv(EffectiveType(operand.type));
		const EntityId entity = EntityOf(queried);
		dynamic = !enclosing_unevaluated &&
			operand.category == VALUE_LVALUE &&
			entity != kNoEntity &&
			program_->entities[entity].polymorphic_class;
		if (dynamic)
		{
			// The operand is potentially evaluated only after its static type is
			// known. Calls were retained with deferred demand while that fact was
			// established; publish their runtime demand exactly for this branch.
			DemandRetainedRuntimeCalls(operand.node);
			DemandMaterializedConstructorActions(operand.node);
			DemandConditionallyEvaluatedConstructors(operand.node);
			MarkVtableDemand(entity);
		}
	}
	if (CandidateSubstitutionFailed() || queried == kNoType)
		return ExpressionInfo();
	queried = program_->types.RemoveTopCv(EffectiveType(queried));
	const TypeRecord& queried_record = program_->types.Get(queried);
	if (queried_record.kind == TYPE_NAMED)
	{
		const NamedFlavor flavor =
			program_->entities[queried_record.entity].flavor;
		if (IsClassNamedFlavor(flavor) && !program_->entities[
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

bool Analyzer::TryAnalyzeTypeidComparison(
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
		ThrowSemanticError(
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

void Analyzer::DemandConditionallyEvaluatedConstructors(
	std::uint32_t root)
{
	if (root >= dump_.nodes.size())
		ThrowInternalCompilerError("invalid conditionally evaluated demand root");
	std::vector<std::uint32_t> pending(1, root);
	while (!pending.empty())
	{
		const std::uint32_t node = pending.back();
		pending.pop_back();
		const DumpNode& record = dump_.nodes[node];
		if ((record.kind == DUMP_CONSTRUCTOR_ACTION ||
			 record.kind == DUMP_SPECIAL_MEMBER_CONSTRUCTION_ACTION) &&
			record.binding != kNoBinding &&
			!record.trivial_special_member_action)
			DemandConstructorDefinition(record.binding);
		for (std::uint32_t edge = record.first_edge;
			edge != kNoDumpEdge; edge = dump_.edges[edge].next)
			pending.push_back(dump_.edges[edge].child);
	}
}

bool Analyzer::TryAnalyzeDynamicCast(TypeId target,
	const ExpressionInfo& operand, ExpressionInfo* result)
{
	const TypeRecord& target_record = program_->types.Get(target);
	const bool reference = target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE;
	TypeId target_object_with_cv = reference ? target_record.child :
		program_->types.RemoveTopCv(target);
	const TypeRecord& target_shape = program_->types.Get(
		program_->types.RemoveTopCv(target_object_with_cv));
	if (!reference && target_shape.kind != TYPE_POINTER)
		ThrowSemanticError("dynamic_cast target is not a pointer or reference");
	if (!reference) target_object_with_cv = target_shape.child;
	TypeId target_object = target_object_with_cv;
	target_object = program_->types.RemoveTopCv(target_object);

	TypeId source_object_with_cv = kNoType;
	if (reference)
		source_object_with_cv = EffectiveType(operand.type);
	else
	{
		const TypeId source_pointer =
			program_->types.RemoveTopCv(Decay(operand.type));
		const TypeRecord& source_shape = program_->types.Get(source_pointer);
		if (source_shape.kind != TYPE_POINTER)
			ThrowSemanticError("dynamic_cast source is not a pointer");
		source_object_with_cv = source_shape.child;
	}
	const TypeId source_object =
		program_->types.RemoveTopCv(source_object_with_cv);
	const TypeRecord& target_object_record = program_->types.Get(target_object);
	const bool target_void = !reference &&
		target_object_record.kind == TYPE_FUNDAMENTAL &&
		target_object_record.fundamental == FUND_VOID;

	const EntityId source_entity = EntityOf(source_object);
	const EntityId target_entity = EntityOf(target_object);
	if (source_entity == kNoEntity || (!target_void && target_entity == kNoEntity) ||
		!IsClassNamedFlavor(program_->entities[source_entity].flavor) ||
		(!target_void &&
		 !IsClassNamedFlavor(program_->entities[target_entity].flavor)))
		ThrowSemanticError("dynamic_cast requires class operands");
	if ((TopCv(*program_, source_object_with_cv) &
		~TopCv(*program_, target_object_with_cv)) != 0)
		ThrowSemanticError("dynamic_cast removes cv-qualification");
	EnsureClassDefinition(source_object);
	if (!target_void) EnsureClassDefinition(target_object);

	// Identity and derived-to-base conversions need no runtime RTTI query and
	// retain the existing cast path (including base projection facts).
	if (!target_void && source_entity == target_entity)
		return false;
	if (!target_void && program_->IsBaseOf(target_entity, source_entity))
	{
		if (!BaseConversionAllowed(source_entity, target_entity))
			ThrowSemanticError(
				"dynamic_cast names an inaccessible base");
		return false;
	}

	if (!program_->entities[source_entity].polymorphic_class)
		ThrowSemanticError("dynamic_cast source is not polymorphic");
	MarkVtableDemand(source_entity);
	if (!target_void) MarkVtableDemand(target_entity);

	const ValueCategory category = reference ?
		(target_record.kind == TYPE_LVALUE_REFERENCE ?
			VALUE_LVALUE : VALUE_XVALUE) : VALUE_PRVALUE;
	result->node = MakeDump(
		DUMP_DYNAMIC_CAST_EXPRESSION, target, category);
	dump_.nodes[result->node].operand_type = source_object;
	dump_.nodes[result->node].dynamic_cast_reference = reference;
	std::int64_t hint = -2;
	if (!target_void)
	{
		std::uint64_t offset = 0;
		bool all_public = false;
		bool ambiguous = false;
		if (program_->QueryBasePath(target_entity, source_entity, 0,
			&all_public, &offset, &ambiguous))
		{
			if (!all_public) hint = -2;
			else if (program_->HasVirtualBasePath(target_entity, source_entity))
				hint = -1;
			else if (ambiguous) hint = -3;
			else if (offset <= static_cast<std::uint64_t>(
				std::numeric_limits<std::int64_t>::max()))
				hint = static_cast<std::int64_t>(offset);
			else ThrowSemanticResourceLimit(
				"dynamic_cast base offset exceeds runtime ABI");
		}
	}
	dump_.nodes[result->node].dynamic_cast_hint = hint;
	dump_.Add(result->node, operand.node);
	result->type = target;
	result->category = category;
	++expression_count_;
	return true;
}

}
}
