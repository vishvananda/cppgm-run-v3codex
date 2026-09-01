#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <limits>
#include <string>
#include <vector>

namespace cppgm
{
namespace semantic
{

NameId Analyzer::NextRangeForHiddenName(const char* prefix)
{
	if (current_function_context_ == kNoBinding)
		ThrowInternalCompilerError("range-for is not owned by a function");
	if (range_for_hidden_count_by_function_.size() <= current_function_context_)
		range_for_hidden_count_by_function_.resize(
			static_cast<std::size_t>(current_function_context_) + 1, 0);
	std::uint32_t& count =
		range_for_hidden_count_by_function_[current_function_context_];
	if (count == std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit("too many range-for hidden objects");
	const std::string generated =
		std::string(prefix) + std::to_string(++count);
	if (stats_)
		RecordGeneratedIdentityRender(SEMANTIC_GENERATED_RANGE_FOR_HIDDEN,
			generated, 1);
	return program_->names.Intern(generated);
}

ExpressionInfo Analyzer::MakeRangeForBindingExpression(
	BindingId binding)
{
	if (binding >= program_->bindings.size())
		ThrowInternalCompilerError("invalid range-for binding");
	const BindingRecord& record = program_->bindings[binding];
	ExpressionInfo result;
	result.type = EffectiveType(record.type);
	result.category = VALUE_LVALUE;
	result.binding = binding;
	result.node = MakeDump(DUMP_ID_EXPRESSION, result.type,
		VALUE_LVALUE, record.name, binding);
	++expression_count_;
	return result;
}

BindingId Analyzer::AddRangeForLocal(ScopeId scope,
	std::uint32_t output_parent, NameId name, TypeId type,
	ExpressionInfo initializer, bool array_initializer)
{
	EnsureClassDefinition(type);
	const TypeKind declared_kind = program_->types.Get(type).kind;
	const bool reference = declared_kind == TYPE_LVALUE_REFERENCE ||
		declared_kind == TYPE_RVALUE_REFERENCE;
	if (!array_initializer)
	{
		const DumpKind kind = dump_.nodes[initializer.node].kind;
		if (!reference && IsClassObjectType(type) &&
			kind == DUMP_TEMPORARY_OBJECT &&
			dump_.nodes[initializer.node].first_edge != kNoDumpEdge &&
			dump_.edges[dump_.nodes[initializer.node].first_edge].next ==
				kNoDumpEdge)
		{
			const std::uint32_t recipe = dump_.edges[
				dump_.nodes[initializer.node].first_edge].child;
			if (dump_.nodes[recipe].kind == DUMP_CONSTRUCTOR_ACTION &&
				dump_.nodes[recipe].operand_type ==
					program_->types.RemoveTopCv(EffectiveType(type)))
			{
				initializer.node = recipe;
				initializer.type = type;
				initializer.category = VALUE_NONE;
			}
		}
		else if (!reference && IsClassObjectType(type) &&
			initializer.category == VALUE_PRVALUE &&
			kind == DUMP_CALL_EXPRESSION &&
			!dump_.nodes[initializer.node].explicit_user_conversion_call)
		{
			const BindingId selected =
				ValidateClassValueConstruction(type, initializer);
			initializer = BuildDirectClassValueTransfer(
				initializer, type, selected);
		}
		else if (!reference && IsClassObjectType(type) &&
			program_->types.RemoveTopCv(EffectiveType(initializer.type)) ==
				program_->types.RemoveTopCv(EffectiveType(type)))
		{
			initializer.node = BuildClassValueConstructorAction(type, initializer);
			initializer.type = type;
			initializer.category = VALUE_NONE;
		}
		else initializer = ApplyTarget(initializer, type);
		initializer = FinalizeVariableInitializer(
			initializer, type, EntityOf(type), true);
	}
	// Hidden range-for locals are exposition-only and stay out of lookup.
	const BindingId binding = program_->AddUnindexedBinding(
		scope, BIND_VARIABLE, name, type, kNoBinding);
	SpecInfo spec;
	PublishVariableDeclarationFacts(binding, scope, name, type, spec, true);
	PublishVariableInitializer(binding, type, spec, initializer, false);
	const std::uint32_t declaration = MakeDump(DUMP_SIMPLE_DECLARATION);
	const std::uint32_t variable = MakeDump(
		DUMP_VARIABLE, type, VALUE_NONE, name, binding);
	dump_.nodes[variable].storage_size =
		dump_.nodes[initializer.node].storage_size;
	dump_.nodes[variable].storage_alignment =
		dump_.nodes[initializer.node].storage_alignment;
	dump_.Add(variable, initializer.node);
	dump_.Add(declaration, variable);
	dump_.Add(output_parent, declaration);
	RegisterVariableLifetimeAndStorage(scope, true, false, variable,
		binding, type, 0, 0, 0, 0, 0,
		true, HasConstantInitializerFact(initializer));
	FinishRangeForLocalInitializer(scope, declaration, type, initializer);
	return binding;
}

void Analyzer::FinishRangeForLocalInitializer(ScopeId scope,
	std::uint32_t declaration, TypeId type,
	const ExpressionInfo& initializer)
{
	const TypeKind kind = program_->types.Get(type).kind;
	if (kind != TYPE_LVALUE_REFERENCE && kind != TYPE_RVALUE_REFERENCE)
	{
		AppendFullExpressionDestructionActions(initializer.node, declaration);
		return;
	}
	std::vector<std::uint32_t> temporaries;
	CollectTemporaryObjects(initializer.node, &temporaries);
	if (temporaries.empty()) return;
	AddTemporaryLifetimeObligation(scope, temporaries.back());
	for (std::size_t i = temporaries.size() - 1; i != 0; --i)
	{
		const std::uint32_t action =
			MakeTemporaryDestructorAction(temporaries[i - 1]);
		if (action != kNoDumpEdge) dump_.Add(declaration, action);
	}
}

void Analyzer::FinishRangeForFullExpression(ScopeId scope,
	std::uint32_t owner, const ExpressionInfo& expression)
{
	const std::size_t edge_count = dump_.edges.size();
	AppendFullExpressionDestructionActions(expression.node, owner);
	if (dump_.edges.size() == edge_count) return;
	MarkFullExpressionCalls(expression.node);
	AppendUnwindDestructionActions(scope, owner);
}

ExpressionInfo Analyzer::AnalyzeRangeForUnary(const char* operation,
	const char* display, ExpressionInfo operand, ScopeId scope)
{
	std::vector<NodeId> syntax(1, kNoNode);
	std::vector<ExpressionInfo> operands(1, operand);
	ExpressionInfo overloaded;
	if (TryAnalyzeOverloadedOperator(operation, scope, syntax, operands,
		false, kNoType, &overloaded)) return overloaded;
	(void)ApplyBuiltinUnaryConversion(operation, &operand);
	TypeId result_type = EffectiveType(operand.type);
	ValueCategory category = VALUE_PRVALUE;
	if (std::string(operation) == "*")
	{
		const TypeId pointer_type = Decay(result_type);
		const TypeRecord& pointer = program_->types.Get(pointer_type);
		if (pointer.kind != TYPE_POINTER)
			ThrowSemanticError("range iterator is not dereferenceable");
		result_type = pointer.child;
		category = VALUE_LVALUE;
	}
	else if (std::string(operation) == "++")
	{
		if (!IsModifiableLvalue(operand) ||
			(!IsArithmetic(result_type) && !IsPointer(result_type)) ||
			(IsPointer(result_type) && !IsPointerToCompleteObject(result_type)))
			ThrowSemanticError("range iterator is not incrementable");
		category = VALUE_LVALUE;
	}
	else ThrowInternalCompilerError("invalid range-for unary operation");
	const std::uint32_t expression = MakeDump(DUMP_UNARY_EXPRESSION,
		result_type, category, program_->names.Intern(display));
	dump_.Add(expression, operand.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = result_type;
	result.category = category;
	++expression_count_;
	return result;
}

ExpressionInfo Analyzer::AnalyzeRangeForSubscript(
	ExpressionInfo range, ExpressionInfo index, ScopeId)
{
	(void)ApplyBuiltinBinaryConversions("[]", &range, &index);
	if (!IsPointer(Decay(range.type)) && IsPointer(Decay(index.type)))
		std::swap(range, index);
	const TypeId pointer_type = Decay(range.type);
	const TypeRecord& pointer = program_->types.Get(pointer_type);
	if (pointer.kind != TYPE_POINTER || !IsIntegral(index.type) ||
		!IsPointerToCompleteObject(pointer_type))
		ThrowSemanticError("invalid bounded range subscript");
	const std::uint32_t expression = MakeDump(DUMP_SUBSCRIPT_EXPRESSION,
		pointer.child, VALUE_LVALUE);
	dump_.Add(expression, range.node);
	dump_.Add(expression, index.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = pointer.child;
	result.category = VALUE_LVALUE;
	++expression_count_;
	return result;
}

ExpressionInfo Analyzer::AnalyzeRangeForMemberCall(
	ExpressionInfo object, ScopeId scope, const LookupResult& found)
{
	if (found.ordinary == kNoBinding ||
		program_->bindings[found.ordinary].kind != BIND_FUNCTION)
		ThrowSemanticError("range member does not name a function");
	std::vector<BindingId> candidates = FunctionSet(found.ordinary);
	if (candidates.empty())
		ThrowSemanticError("range member has no callable candidates");
	if (object.category == VALUE_PRVALUE &&
		dump_.nodes[object.node].kind != DUMP_TEMPORARY_OBJECT)
		object = MaterializeTemporary(object);
	ExpressionInfo object_pointer = MakeImplicitObjectPointer(object);
	const std::vector<NodeId> argument_syntax;
	const std::vector<ExpressionInfo> arguments;
	ObjectConversionFact object_conversion;
	std::vector<CallConversionFact> argument_conversions;
	const BindingId selected = SelectOverload(scope, argument_syntax,
		arguments, candidates, &object_pointer, &object_conversion,
		&argument_conversions);
	if (selected == kNoBinding)
		ThrowSemanticError("range member call is ambiguous");
	DemandRetainedRuntimeCalls(object.node);
	return BuildResolvedCall(selected, scope, argument_syntax, arguments,
		&object_pointer, kNoType, found.naming_class, &object_conversion,
		&argument_conversions);
}

ExpressionInfo Analyzer::AnalyzeRangeForAdlCall(
	ExpressionInfo object, ScopeId scope, NameId name)
{
	std::vector<NodeId> argument_syntax(1, kNoNode);
	std::vector<ExpressionInfo> arguments(1, object);
	std::vector<BindingId> candidates;
	CompleteArgumentDependentCallCandidates(name, 0, scope,
		argument_syntax, arguments, false, &candidates);
	if (candidates.empty())
		ThrowSemanticError("range ADL call has no candidates");
	std::vector<CallConversionFact> argument_conversions;
	const BindingId selected = SelectOverload(scope, argument_syntax,
		arguments, candidates, 0, 0, &argument_conversions);
	if (selected == kNoBinding)
		ThrowSemanticError("range ADL call is ambiguous");
	return BuildResolvedCall(selected, scope, argument_syntax, arguments,
		0, kNoType, kNoEntity, 0, &argument_conversions);
}

void Analyzer::AddRangeForLoopVariable(NodeId declaration,
	ExpressionInfo initializer, ScopeId scope, std::uint32_t output_parent)
{
	const NodeId specifiers = FindChild(declaration, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
	const NodeId declarator = FindChild(declaration, ::cppgm::syntax::STAG_DECLARATOR);
	if (specifiers == kNoNode || declarator == kNoNode)
		ThrowSemanticError("invalid range declaration");
	const SpecInfo spec = BuildSpecifiers(
		specifiers, scope, std::string(), true);
	DeclaratorInfo parsed;
	if (!spec.placeholder_auto)
		parsed = BuildDeclarator(declarator, spec.type, scope);
	else
	{
		std::string pointer_operator;
		for (std::uint32_t edge = arena_->FirstEdge(declarator);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (!arena_->IsTag(child, ::cppgm::syntax::STAG_PTR_OPERATOR)) continue;
			if (!pointer_operator.empty())
				ThrowSemanticError(
					"compound placeholder range declarator");
			pointer_operator = PayloadSource(child);
		}
		TypeId base = EffectiveType(initializer.type);
		if (pointer_operator.empty()) base = Decay(initializer.type);
		else if (pointer_operator == "&")
		{
			if (initializer.category != VALUE_LVALUE &&
				(spec.placeholder_cv & CV_CONST) == 0)
				ThrowSemanticError("auto& range variable needs an lvalue");
		}
		else if (pointer_operator == "&&")
		{
			if (initializer.category == VALUE_LVALUE &&
				spec.placeholder_cv == CV_NONE)
				base = program_->types.Reference(TYPE_LVALUE_REFERENCE, base);
		}
		else if (pointer_operator == "*")
		{
			const TypeRecord& pointer = program_->types.Get(
				Decay(initializer.type));
			if (pointer.kind != TYPE_POINTER)
				ThrowSemanticError("auto* range variable needs a pointer");
			base = pointer.child;
		}
		else ThrowSemanticError(
			"unsupported placeholder range declarator");
		base = program_->types.Qualify(base, spec.placeholder_cv);
		parsed = BuildDeclarator(declarator, base, scope);
	}
	if (IsStructuredBindingDeclarator(declarator))
	{
		const TypeKind kind = program_->types.Get(parsed.type).kind;
		if ((kind == TYPE_LVALUE_REFERENCE || kind == TYPE_RVALUE_REFERENCE) &&
			initializer.category == VALUE_PRVALUE &&
			dump_.nodes[initializer.node].kind == DUMP_CALL_EXPRESSION &&
			!IsClassObjectType(initializer.type))
			dump_.nodes[initializer.node].reference_call_materialization = true;
		initializer = ApplyTarget(initializer, parsed.type);
		initializer = FinalizeVariableInitializer(
			initializer, parsed.type, EntityOf(parsed.type), true);
		EmitStructuredBindingStorage(declaration, declarator, spec, parsed,
			initializer, scope, output_parent, true, true);
		return;
	}
	if (parsed.name == 0)
		ThrowSemanticError("unnamed range variable");
	const TypeKind declared_kind = program_->types.Get(parsed.type).kind;
	if ((declared_kind == TYPE_LVALUE_REFERENCE ||
		 declared_kind == TYPE_RVALUE_REFERENCE) &&
		initializer.category == VALUE_PRVALUE &&
		dump_.nodes[initializer.node].kind == DUMP_CALL_EXPRESSION &&
		!IsClassObjectType(initializer.type))
		dump_.nodes[initializer.node].reference_call_materialization = true;
	initializer = ApplyTarget(initializer, parsed.type);
	initializer = FinalizeVariableInitializer(
		initializer, parsed.type, EntityOf(parsed.type), true);
	const BindingId binding = program_->AddBinding(
		scope, BIND_VARIABLE, parsed.name, parsed.type);
	PublishVariableDeclarationFacts(
		binding, scope, parsed.name, parsed.type, spec, true);
	PublishVariableInitializer(binding, parsed.type, spec, initializer, false);
	const std::uint32_t simple = MakeDump(DUMP_SIMPLE_DECLARATION);
	const std::uint32_t variable = MakeDump(DUMP_VARIABLE, parsed.type,
		VALUE_NONE, parsed.name, binding);
	dump_.Add(variable, initializer.node);
	dump_.Add(simple, variable);
	dump_.Add(output_parent, simple);
	RegisterVariableLifetimeAndStorage(scope, true, false, variable,
		binding, parsed.type, 0, 0, 0, 0, 0,
		true, HasConstantInitializerFact(initializer));
	FinishRangeForLocalInitializer(scope, simple, parsed.type, initializer);
}

void Analyzer::AnalyzeRangeFor(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	const NodeId declaration = FindChild(node, ::cppgm::syntax::STAG_RANGE_DECLARATION);
	const NodeId initializer_node = FindChild(node, ::cppgm::syntax::STAG_RANGE_INITIALIZER);
	const NodeId initializer_syntax = FirstSemanticChild(initializer_node);
	if (declaration == kNoNode || initializer_syntax == kNoNode)
		ThrowSemanticError("invalid range-for statement");
	NodeId body_syntax = kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (child != declaration && child != initializer_node)
			body_syntax = child;
	}
	if (body_syntax == kNoNode)
		ThrowSemanticError("range-for statement has no body");

	const ScopeId control = NewScope(
		scope, SCOPE_BLOCK, 0, ScopePrefixId(scope));
	const std::uint32_t statement = MakeDump(DUMP_FOR_STATEMENT);
	const std::uint32_t init = MakeDump(DUMP_FOR_INIT_STATEMENT);
	dump_.Add(statement, init);
	dump_.Add(output_parent, statement);

	ExpressionInfo range;
	TypeId range_type = kNoType;
	const bool braced = arena_->IsTag(initializer_syntax, ::cppgm::syntax::STAG_BRACED_INIT_LIST);
	if (braced)
	{
		std::vector<ExpressionInfo> elements;
		for (std::uint32_t edge = arena_->FirstEdge(initializer_syntax);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
			elements.push_back(AnalyzeExpression(
				arena_->EdgeChild(edge), control));
		if (elements.empty())
			ThrowSemanticError("empty braced range has no element type");
		const TypeId element_type = Decay(elements[0].type);
		range_type = program_->types.Array(element_type, elements.size());
		const std::uint32_t list = MakeDump(
			DUMP_BRACED_INIT_LIST, range_type, VALUE_LVALUE);
		for (std::size_t i = 0; i < elements.size(); ++i)
			dump_.Add(list, ApplyTarget(elements[i], element_type).node);
		ExpressionInfo list_value;
		list_value.node = list;
		list_value.type = range_type;
		list_value.category = VALUE_LVALUE;
		++expression_count_;
		const NameId name = NextRangeForHiddenName("__range");
		const BindingId binding = AddRangeForLocal(
			control, init, name, range_type, list_value, true);
		range = MakeRangeForBindingExpression(binding);
	}
	else
	{
		range = AnalyzeExpression(initializer_syntax, control);
		range_type = program_->types.RemoveTopCv(EffectiveType(range.type));
		const TypeKind range_kind = program_->types.Get(range_type).kind;
		if (range.category == VALUE_PRVALUE &&
			range_kind == TYPE_ARRAY)
			range = MaterializeTemporary(range);
		if (range_kind == TYPE_ARRAY &&
			dump_.nodes[range.node].kind == DUMP_TEMPORARY_OBJECT)
			dump_.nodes[range.node].range_for_materialization = true;
		const bool stable_lvalue = range.category == VALUE_LVALUE &&
			dump_.nodes[range.node].kind == DUMP_ID_EXPRESSION;
		if (!stable_lvalue)
		{
			const NameId name = NextRangeForHiddenName("__range");
			TypeId storage_type = EffectiveType(range.type);
			if (range.category == VALUE_LVALUE)
				storage_type = program_->types.Reference(
					TYPE_LVALUE_REFERENCE, storage_type);
			else if (range.category == VALUE_XVALUE &&
				(range_kind == TYPE_ARRAY ||
				 dump_.nodes[range.node].kind != DUMP_TEMPORARY_OBJECT))
				storage_type = program_->types.Reference(
					TYPE_RVALUE_REFERENCE, storage_type);
			const BindingId binding = AddRangeForLocal(
				control, init, name, storage_type, range);
			range = MakeRangeForBindingExpression(binding);
			range_type = program_->types.RemoveTopCv(
				EffectiveType(storage_type));
		}
	}

	ExpressionInfo element;
	const TypeRecord& range_shape = program_->types.Get(range_type);
	TypeId initializer_list_element = kNoType;
	if (IsInitializerListType(range_type, &initializer_list_element))
	{
		ExpressionInfo zero = MakeLiteral(
			program_->types.Fundamental(FUND_INT),
			program_->names.Intern("0"));
		zero.constant = true;
		zero.value = 0;
		RecordExpressionFacts(zero);
		const NameId index_name = NextRangeForHiddenName("__idx");
		const BindingId index_binding = AddRangeForLocal(control, init,
			index_name, program_->types.Fundamental(FUND_INT), zero);

		ExpressionInfo size;
		// The retained count is lowered as the ABI's signed machine-width index;
		// this keeps the established PA25 range comparison conversion path.
		size.type = program_->types.Fundamental(FUND_LONG_INT);
		size.category = VALUE_PRVALUE;
		size.node = MakeDump(DUMP_INITIALIZER_LIST_SIZE,
			size.type, size.category);
		dump_.Add(size.node, range.node);
		RecordExpressionFacts(size);
		++expression_count_;
		ExpressionInfo condition_value = BuildBinaryExpression("<", "OP_LT:<",
			kNoNode, kNoNode, MakeRangeForBindingExpression(index_binding),
			size, control);
		const std::uint32_t condition = MakeDump(DUMP_CONDITION);
		dump_.nodes[condition].full_expression_staging = true;
		dump_.Add(condition, condition_value.node);
		FinishRangeForFullExpression(control, condition, condition_value);
		dump_.Add(statement, condition);

		ExpressionInfo begin;
		begin.type = program_->types.Pointer(program_->types.Qualify(
			initializer_list_element, CV_CONST));
		begin.category = VALUE_PRVALUE;
		begin.node = MakeDump(DUMP_INITIALIZER_LIST_BEGIN,
			begin.type, begin.category);
		dump_.Add(begin.node, range.node);
		RecordExpressionFacts(begin);
		++expression_count_;
		element = AnalyzeRangeForSubscript(begin,
			MakeRangeForBindingExpression(index_binding), control);
		const ExpressionInfo increment = MaterializeDiscardedClassResult(
			AnalyzeRangeForUnary("++", "OP_INC:++",
				MakeRangeForBindingExpression(index_binding), control));
		const std::uint32_t iteration = MakeDump(DUMP_ITERATION);
		dump_.Add(iteration, increment.node);
		FinishRangeForFullExpression(control, iteration, increment);
		dump_.Add(statement, iteration);
	}
	else if (range_shape.kind == TYPE_ARRAY)
	{
		ExpressionInfo zero = MakeLiteral(
			program_->types.Fundamental(FUND_INT),
			program_->names.Intern("0"));
		zero.constant = true;
		zero.value = 0;
		RecordExpressionFacts(zero);
		const NameId index_name = NextRangeForHiddenName("__idx");
		const BindingId index_binding = AddRangeForLocal(control, init,
			index_name, program_->types.Fundamental(FUND_INT), zero);
		ExpressionInfo index_for_condition =
			MakeRangeForBindingExpression(index_binding);
		ExpressionInfo bound = MakeLiteral(
			program_->types.Fundamental(FUND_INT),
			program_->names.Intern(std::to_string(range_shape.bound)));
		bound.constant = true;
		bound.value = static_cast<std::int64_t>(range_shape.bound);
		RecordExpressionFacts(bound);
		ExpressionInfo condition_value = BuildBinaryExpression("<", "OP_LT:<",
			kNoNode, kNoNode, index_for_condition, bound, control);
		const std::uint32_t condition = MakeDump(DUMP_CONDITION);
		dump_.nodes[condition].full_expression_staging = true;
		dump_.Add(condition, condition_value.node);
		FinishRangeForFullExpression(control, condition, condition_value);
		dump_.Add(statement, condition);

		ExpressionInfo index_for_element =
			MakeRangeForBindingExpression(index_binding);
		element = AnalyzeRangeForSubscript(
			range, index_for_element, control);
		ExpressionInfo index_for_iteration =
			MakeRangeForBindingExpression(index_binding);
		const ExpressionInfo increment = MaterializeDiscardedClassResult(
			AnalyzeRangeForUnary(
				"++", "OP_INC:++", index_for_iteration, control));
		const std::uint32_t iteration = MakeDump(DUMP_ITERATION);
		dump_.Add(iteration, increment.node);
		FinishRangeForFullExpression(control, iteration, increment);
		dump_.Add(statement, iteration);
	}
	else
	{
		const EntityId entity = EntityOf(range_type);
		if (entity == kNoEntity)
			ThrowSemanticError("range expression is neither array nor class");
		EnsureClassDefinition(range_type);
		const NameId begin_name = program_->names.Intern("begin");
		const NameId end_name = program_->names.Intern("end");
		const LookupResult member_begin = program_->LookupMember(
			entity, begin_name, LOOKUP_ORDINARY);
		const LookupResult member_end = program_->LookupMember(
			entity, end_name, LOOKUP_ORDINARY);
		const bool member_range = member_begin.ordinary != kNoBinding &&
			member_end.ordinary != kNoBinding;
		ExpressionInfo begin_value = member_range ?
			AnalyzeRangeForMemberCall(range, control, member_begin) :
			AnalyzeRangeForAdlCall(range, control, begin_name);
		const NameId begin_hidden = NextRangeForHiddenName("__begin");
		const BindingId begin_binding = AddRangeForLocal(control, init,
			begin_hidden, Decay(begin_value.type), begin_value);
		ExpressionInfo end_value = member_range ?
			AnalyzeRangeForMemberCall(range, control, member_end) :
			AnalyzeRangeForAdlCall(range, control, end_name);
		const NameId end_hidden = NextRangeForHiddenName("__end");
		const BindingId end_binding = AddRangeForLocal(control, init,
			end_hidden, Decay(end_value.type), end_value);

		ExpressionInfo condition_value = BuildBinaryExpression("!=", "OP_NE:!=",
			kNoNode, kNoNode, MakeRangeForBindingExpression(begin_binding),
			MakeRangeForBindingExpression(end_binding), control);
		const std::uint32_t condition = MakeDump(DUMP_CONDITION);
		dump_.nodes[condition].full_expression_staging = true;
		dump_.Add(condition, condition_value.node);
		FinishRangeForFullExpression(control, condition, condition_value);
		dump_.Add(statement, condition);
		element = AnalyzeRangeForUnary("*", "OP_STAR:*",
			MakeRangeForBindingExpression(begin_binding), control);
		const ExpressionInfo increment = MaterializeDiscardedClassResult(
			AnalyzeRangeForUnary("++", "OP_INC:++",
				MakeRangeForBindingExpression(begin_binding), control));
		const std::uint32_t iteration = MakeDump(DUMP_ITERATION);
		dump_.Add(iteration, increment.node);
		FinishRangeForFullExpression(control, iteration, increment);
		dump_.Add(statement, iteration);
	}

	const ScopeId body_scope = NewScope(
		control, SCOPE_BLOCK, 0, ScopePrefixId(control));
	const std::uint32_t body = MakeDump(DUMP_COMPOUND_STATEMENT);
	dump_.Add(statement, body);
	AddRangeForLoopVariable(declaration, element, body_scope, body);
	++loop_depth_;
	break_cleanup_stops_.push_back(control);
	continue_cleanup_stops_.push_back(control);
	if (arena_->IsTag(body_syntax, ::cppgm::syntax::STAG_COMPOUND_STATEMENT))
	{
		for (std::uint32_t edge = arena_->FirstEdge(body_syntax);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (IsDeclaration(child))
				AnalyzeDeclaration(child, body_scope, body, true);
			else AnalyzeStatement(child, body_scope, body);
		}
	}
	else if (IsDeclaration(body_syntax))
		AnalyzeDeclaration(body_syntax, body_scope, body, true);
	else AnalyzeStatement(body_syntax, body_scope, body);
	continue_cleanup_stops_.pop_back();
	break_cleanup_stops_.pop_back();
	--loop_depth_;
	AppendScopeDestructionActions(body_scope, body, control);
	AppendScopeDestructionActions(control, output_parent, scope);
}

}
}
