#include "pa12_semantic_detail.h"

#include <cstdint>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

const std::size_t kMaxConstexprDepth = 1024;
const std::size_t kMaxConstexprSteps = 1000000;

}

bool SemanticAnalyzer::ConsumeConstexprStep()
{
	if (constexpr_evaluation_steps_ >= kMaxConstexprSteps) return false;
	++constexpr_evaluation_steps_;
	++constexpr_step_visits_;
	return true;
}

ExpressionInfo SemanticAnalyzer::AnalyzeConstantAwareVariableInitializer(
	NodeId initializer, ScopeId scope, TypeId type, bool local,
	bool require_constant)
{
	if (require_constant) ++constant_expression_required_depth_;
	try
	{
		ExpressionInfo result = AnalyzeVariableInitializer(
			initializer, scope, type, local);
		if (require_constant) --constant_expression_required_depth_;
		return result;
	}
	catch (...)
	{
		if (require_constant) --constant_expression_required_depth_;
		throw;
	}
}

void SemanticAnalyzer::PublishConstantVariableInitializer(BindingId binding,
	TypeId type, const SpecInfo& spec, const ExpressionInfo& initializer)
{
	if (spec.is_constexpr && !program_->types.IsReference(type) &&
		IsIntegral(type, true) && !initializer.constant)
		throw std::runtime_error(
			"constexpr scalar initializer is not constant");
	if (!initializer.constant ||
		(!spec.is_constexpr &&
		 !(IsConst(type) && IsIntegral(type, true)) &&
		 !(constexpr_evaluation_depth_ != 0 && IsIntegral(type, true))))
		return;
	program_->bindings[binding].constant = true;
	program_->bindings[binding].value = initializer.value;
	if (spec.is_constexpr && !IsPointer(type))
		dump_.nodes[initializer.node].type = type;
}

bool SemanticAnalyzer::EvaluateConstexprDeclaration(NodeId node, ScopeId scope)
{
	if (!ConsumeConstexprStep()) return false;
	const std::size_t first_binding = program_->bindings.size();
	const std::uint32_t detached = MakeDump(DUMP_COMPOUND_STATEMENT);
	AnalyzeDeclaration(node, scope, detached, true);
	for (std::size_t i = first_binding; i < program_->bindings.size(); ++i)
	{
		const BindingRecord& binding = program_->bindings[i];
		if (binding.owner != scope || binding.kind != BIND_VARIABLE) continue;
		if (!binding.constant || !IsIntegral(binding.type, true)) return false;
	}
	return true;
}

bool SemanticAnalyzer::EvaluateConstexprCondition(
	NodeId node, ScopeId scope, bool* value)
{
	if (!ConsumeConstexprStep()) return false;
	const std::size_t first_binding = program_->bindings.size();
	const std::uint32_t detached = MakeDump(DUMP_COMPOUND_STATEMENT);
	AnalyzeCondition(node, scope, detached, false);
	for (std::size_t i = first_binding; i < program_->bindings.size(); ++i)
	{
		const BindingRecord& binding = program_->bindings[i];
		if (binding.owner != scope || binding.kind != BIND_VARIABLE) continue;
		if (!binding.constant || !IsIntegral(binding.type, true)) return false;
		*value = binding.value != 0;
		return true;
	}
	const std::uint32_t condition_edge = dump_.nodes[detached].first_edge;
	if (condition_edge == kNoDumpEdge) return false;
	const std::uint32_t condition = dump_.edges[condition_edge].child;
	const std::uint32_t value_edge = dump_.nodes[condition].first_edge;
	if (value_edge == kNoDumpEdge) return false;
	const DumpNode& expression = dump_.nodes[dump_.edges[value_edge].child];
	if (!expression.constant || !IsIntegral(expression.type, true)) return false;
	*value = expression.constant_value != 0;
	return true;
}

ConstexprFlow SemanticAnalyzer::EvaluateConstexprCompound(
	NodeId node, ScopeId scope, TypeId result_type, std::int64_t* result)
{
	const ScopeId block = NewScope(scope, SCOPE_BLOCK, 0, ScopePrefixId(scope));
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		const ConstexprFlow flow = IsDeclaration(child) ?
			(EvaluateConstexprDeclaration(child, block) ?
				CONSTEXPR_FLOW_NORMAL : CONSTEXPR_FLOW_INVALID) :
			EvaluateConstexprStatement(child, block, result_type, result);
		if (flow != CONSTEXPR_FLOW_NORMAL) return flow;
	}
	return CONSTEXPR_FLOW_NORMAL;
}

ConstexprFlow SemanticAnalyzer::EvaluateConstexprStatement(
	NodeId node, ScopeId scope, TypeId result_type, std::int64_t* result)
{
	if (!ConsumeConstexprStep()) return CONSTEXPR_FLOW_INVALID;
	if (arena_->IsTag(node, "compound-statement"))
		return EvaluateConstexprCompound(node, scope, result_type, result);
	if (arena_->IsTag(node, "return-statement"))
	{
		const NodeId expression = FirstSemanticChild(node);
		if (expression == kNoNode) return CONSTEXPR_FLOW_INVALID;
		const ExpressionInfo value = AnalyzeExpression(
			expression, scope, result_type);
		if (!value.constant || !IsIntegral(value.type, true))
			return CONSTEXPR_FLOW_INVALID;
		*result = NormalizeIntegralConstant(result_type, value.value);
		return CONSTEXPR_FLOW_RETURN;
	}
	if (arena_->IsTag(node, "expression-statement"))
	{
		const NodeId expression = FirstSemanticChild(node);
		if (expression == kNoNode) return CONSTEXPR_FLOW_NORMAL;
		return AnalyzeExpression(expression, scope).constant ?
			CONSTEXPR_FLOW_NORMAL : CONSTEXPR_FLOW_INVALID;
	}
	if (arena_->IsTag(node, "if-statement"))
	{
		const ScopeId control = NewScope(
			scope, SCOPE_BLOCK, 0, ScopePrefixId(scope));
		NodeId condition = kNoNode;
		NodeId then_branch = kNoNode;
		NodeId else_branch = kNoNode;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "condition")) condition = child;
			else if (arena_->IsTag(child, "then"))
				then_branch = FirstSemanticChild(child);
			else if (arena_->IsTag(child, "else"))
				else_branch = FirstSemanticChild(child);
		}
		bool selected = false;
		if (condition == kNoNode ||
			!EvaluateConstexprCondition(condition, control, &selected))
			return CONSTEXPR_FLOW_INVALID;
		const NodeId branch = selected ? then_branch : else_branch;
		return branch == kNoNode ? CONSTEXPR_FLOW_NORMAL :
			EvaluateConstexprStatement(branch, control, result_type, result);
	}
	if (arena_->IsTag(node, "while-statement") ||
		arena_->IsTag(node, "do-statement"))
	{
		const bool is_do = arena_->IsTag(node, "do-statement");
		const ScopeId control = NewScope(
			scope, SCOPE_BLOCK, 0, ScopePrefixId(scope));
		NodeId condition = kNoNode;
		NodeId body = kNoNode;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "condition")) condition = child;
			else body = child;
		}
		for (;;)
		{
			const ScopeId iteration = NewScope(
				control, SCOPE_BLOCK, 0, ScopePrefixId(control));
			bool active = true;
			if (!is_do)
			{
				if (condition == kNoNode ||
					!EvaluateConstexprCondition(condition, iteration, &active))
					return CONSTEXPR_FLOW_INVALID;
				if (!active) return CONSTEXPR_FLOW_NORMAL;
			}
			if (body == kNoNode) return CONSTEXPR_FLOW_INVALID;
			const ConstexprFlow flow = EvaluateConstexprStatement(
				body, iteration, result_type, result);
			if (flow == CONSTEXPR_FLOW_RETURN ||
				flow == CONSTEXPR_FLOW_INVALID) return flow;
			if (flow == CONSTEXPR_FLOW_BREAK) return CONSTEXPR_FLOW_NORMAL;
			if (is_do)
			{
				if (condition == kNoNode ||
					!EvaluateConstexprCondition(condition, iteration, &active))
					return CONSTEXPR_FLOW_INVALID;
				if (!active) return CONSTEXPR_FLOW_NORMAL;
			}
		}
	}
	if (arena_->IsTag(node, "for-statement"))
	{
		const ScopeId control = NewScope(
			scope, SCOPE_BLOCK, 0, ScopePrefixId(scope));
		NodeId condition = kNoNode;
		NodeId iteration_expression = kNoNode;
		NodeId body = kNoNode;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "for-init-statement"))
			{
				const NodeId initializer = FirstSemanticChild(child);
				if (initializer != kNoNode)
				{
					if (IsDeclaration(initializer))
					{
						if (!EvaluateConstexprDeclaration(initializer, control))
							return CONSTEXPR_FLOW_INVALID;
					}
					else if (!AnalyzeExpression(initializer, control).constant)
						return CONSTEXPR_FLOW_INVALID;
				}
			}
			else if (arena_->IsTag(child, "condition")) condition = child;
			else if (arena_->IsTag(child, "iteration"))
				iteration_expression = FirstSemanticChild(child);
			else body = child;
		}
		for (;;)
		{
			const ScopeId iteration = NewScope(
				control, SCOPE_BLOCK, 0, ScopePrefixId(control));
			bool active = true;
			if (condition != kNoNode &&
				!EvaluateConstexprCondition(condition, iteration, &active))
				return CONSTEXPR_FLOW_INVALID;
			if (!active) return CONSTEXPR_FLOW_NORMAL;
			if (body == kNoNode) return CONSTEXPR_FLOW_INVALID;
			const ConstexprFlow flow = EvaluateConstexprStatement(
				body, iteration, result_type, result);
			if (flow == CONSTEXPR_FLOW_RETURN ||
				flow == CONSTEXPR_FLOW_INVALID) return flow;
			if (flow == CONSTEXPR_FLOW_BREAK) return CONSTEXPR_FLOW_NORMAL;
			if (iteration_expression != kNoNode &&
				!AnalyzeExpression(iteration_expression, iteration).constant)
				return CONSTEXPR_FLOW_INVALID;
		}
	}
	if (arena_->IsTag(node, "break-statement"))
		return CONSTEXPR_FLOW_BREAK;
	if (arena_->IsTag(node, "continue-statement"))
		return CONSTEXPR_FLOW_CONTINUE;
	if (IsDeclaration(node))
		return EvaluateConstexprDeclaration(node, scope) ?
			CONSTEXPR_FLOW_NORMAL : CONSTEXPR_FLOW_INVALID;
	return CONSTEXPR_FLOW_INVALID;
}

bool SemanticAnalyzer::TryEvaluateConstexprFunction(BindingId function,
	const std::vector<ExpressionInfo>& arguments, std::int64_t* value)
{
	function = program_->bindings[function].canonical;
	const FunctionInfo info = GetFunction(function);
	const TypeId result_type = program_->types.Get(info.type).child;
	if (!info.constexpr_function || info.definition_body == kNoNode ||
		!IsIntegral(result_type, true) ||
		arguments.size() != info.parameters.size() ||
		(info.member_owner != kNoType &&
		 !program_->bindings[function].static_member_function))
		return false;
	++constexpr_call_requests_;

	ConstexprCallKey key;
	key.function = function;
	key.parameter_types.reserve(arguments.size());
	key.parameter_values.reserve(arguments.size());
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		const TypeId type = ParameterBindingType(info.parameters[i]);
		if (!arguments[i].constant || !IsIntegral(type, true)) return false;
		key.parameter_types.push_back(
			program_->types.RemoveTopCv(EffectiveType(type)));
		key.parameter_values.push_back(
			NormalizeIntegralConstant(type, arguments[i].value));
	}

	std::unordered_map<ConstexprCallKey, ConstexprCallFact,
		ConstexprCallKeyHash>::iterator cached = constexpr_call_facts_.find(key);
	if (cached != constexpr_call_facts_.end())
	{
		++constexpr_call_cache_hits_;
		if (cached->second.state == 2)
		{
			*value = cached->second.value;
			return true;
		}
		return false;
	}
	constexpr_call_facts_.insert(std::make_pair(key, ConstexprCallFact()));
	if (constexpr_evaluation_depth_ == 0) constexpr_evaluation_steps_ = 0;
	if (constexpr_evaluation_depth_ >= kMaxConstexprDepth ||
		!ConsumeConstexprStep())
	{
		constexpr_call_facts_.find(key)->second.state = 3;
		return false;
	}

	++constexpr_evaluation_depth_;
	if (constexpr_evaluation_depth_ > constexpr_max_depth_)
		constexpr_max_depth_ = constexpr_evaluation_depth_;
	constexpr_evaluation_stack_.push_back(function);
	const ScopeId function_scope = NewScope(info.lexical_scope, SCOPE_FUNCTION,
		program_->bindings[function].name, ScopePrefixId(info.owner));
	BindFunctionParameterPackElement(
		function_scope, info.parameter_pack_name, kNoBinding);
	for (std::size_t i = 0; i < info.parameters.size(); ++i)
	{
		const ParameterInfo& parameter = info.parameters[i];
		const TypeId type = ParameterBindingType(parameter);
		const BindingId binding = program_->AddBinding(function_scope,
			BIND_PARAMETER, parameter.name, type);
		program_->bindings[binding].constant = true;
		program_->bindings[binding].value = key.parameter_values[i];
		BindFunctionParameterPackElement(
			function_scope, parameter.pack_name, binding);
	}

	const TypeId previous_return = current_return_type_;
	const EntityId previous_class = current_class_context_;
	const BindingId previous_function = current_function_context_;
	current_return_type_ = result_type;
	current_class_context_ = program_->bindings[function].member_owner;
	current_function_context_ = function;
	std::int64_t evaluated = 0;
	ConstexprFlow flow = CONSTEXPR_FLOW_INVALID;
	try
	{
		flow = EvaluateConstexprCompound(
			info.definition_body, function_scope, result_type, &evaluated);
	}
	catch (...)
	{
		flow = CONSTEXPR_FLOW_INVALID;
	}
	current_return_type_ = previous_return;
	current_class_context_ = previous_class;
	current_function_context_ = previous_function;
	constexpr_evaluation_stack_.pop_back();
	--constexpr_evaluation_depth_;

	ConstexprCallFact& fact = constexpr_call_facts_.find(key)->second;
	if (flow != CONSTEXPR_FLOW_RETURN)
	{
		fact.state = 3;
		return false;
	}
	fact.state = 2;
	fact.value = NormalizeIntegralConstant(result_type, evaluated);
	*value = fact.value;
	return true;
}

}
}
