#include "pa12_semantic_detail.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

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

void SemanticAnalyzer::PushConstexprBlock()
{
	if (constexpr_frames_.empty())
		throw std::logic_error("constexpr block has no invocation frame");
	constexpr_block_offsets_.push_back(ConstexprBlockOffset(
		constexpr_locals_.size(), constexpr_scope_facts_.size()));
}

void SemanticAnalyzer::PopConstexprBlock()
{
	if (constexpr_frames_.empty() ||
		constexpr_block_offsets_.size() <= constexpr_frames_.back().first_block)
		throw std::logic_error("constexpr block stack is unbalanced");
	constexpr_locals_.erase(
		constexpr_locals_.begin() +
			constexpr_block_offsets_.back().first_local,
		constexpr_locals_.end());
	constexpr_scope_facts_.erase(
		constexpr_scope_facts_.begin() +
			constexpr_block_offsets_.back().first_scope_fact,
		constexpr_scope_facts_.end());
	constexpr_block_offsets_.pop_back();
}

bool SemanticAnalyzer::AddConstexprLocal(NameId name, NameId pack_name,
	TypeId type, std::int64_t value, std::size_t* local)
{
	if (constexpr_frames_.empty()) return false;
	const ConstexprFrame& frame = constexpr_frames_.back();
	const std::size_t first =
		constexpr_block_offsets_.size() > frame.first_block ?
		constexpr_block_offsets_.back().first_local : frame.first_local;
	if (name != 0)
		for (std::size_t i = first; i < constexpr_locals_.size(); ++i)
			if (constexpr_locals_[i].name == name &&
				(pack_name == 0 || constexpr_locals_[i].pack_name != pack_name))
				return false;
	if (local) *local = constexpr_locals_.size();
	constexpr_locals_.push_back(ConstexprLocalValue(
		name, pack_name, type, NormalizeIntegralConstant(type, value)));
	if (constexpr_locals_.size() > constexpr_peak_locals_)
		constexpr_peak_locals_ = constexpr_locals_.size();
	return true;
}

bool SemanticAnalyzer::AddConstexprTypeAlias(NameId name, TypeId type)
{
	if (name == 0 || constexpr_frames_.empty()) return false;
	const ConstexprFrame& frame = constexpr_frames_.back();
	const std::size_t first =
		constexpr_block_offsets_.size() > frame.first_block ?
		constexpr_block_offsets_.back().first_scope_fact :
		frame.first_scope_fact;
	for (std::size_t i = first; i < constexpr_scope_facts_.size(); ++i)
		if (constexpr_scope_facts_[i].name == name) return false;
	constexpr_scope_facts_.push_back(
		ConstexprScopeFact(name, type, kNoScope));
	return true;
}

void SemanticAnalyzer::AddConstexprUsingNamespace(ScopeId name_space)
{
	if (name_space == kNoScope || constexpr_frames_.empty())
		throw std::logic_error("invalid constexpr using namespace fact");
	constexpr_scope_facts_.push_back(
		ConstexprScopeFact(0, kNoType, name_space));
}

bool SemanticAnalyzer::FindConstexprTypeAlias(NameId name, TypeId* type) const
{
	if (name == 0 || constexpr_frames_.empty()) return false;
	const std::size_t first = constexpr_frames_.back().first_scope_fact;
	for (std::size_t i = constexpr_scope_facts_.size(); i > first; --i)
		if (constexpr_scope_facts_[i - 1].name == name &&
			constexpr_scope_facts_[i - 1].type != kNoType)
		{
			*type = constexpr_scope_facts_[i - 1].type;
			return true;
		}
	return false;
}

void SemanticAnalyzer::FindConstexprUsingNamespaces(
	std::vector<ScopeId>* scopes) const
{
	scopes->clear();
	if (constexpr_frames_.empty()) return;
	const std::size_t first = constexpr_frames_.back().first_scope_fact;
	for (std::size_t i = first; i < constexpr_scope_facts_.size(); ++i)
		if (constexpr_scope_facts_[i].name_space != kNoScope)
			scopes->push_back(constexpr_scope_facts_[i].name_space);
}

bool SemanticAnalyzer::FindConstexprLocal(NameId name,
	std::size_t* local) const
{
	if (name == 0 || constexpr_frames_.empty()) return false;
	const std::size_t first = constexpr_frames_.back().first_local;
	for (std::size_t i = constexpr_locals_.size(); i > first; --i)
		if (constexpr_locals_[i - 1].name == name)
		{
			*local = i - 1;
			return true;
		}
	return false;
}

bool SemanticAnalyzer::FindConstexprPack(NameId name,
	std::vector<std::size_t>* locals) const
{
	if (name == 0 || constexpr_frames_.empty()) return false;
	locals->clear();
	const std::size_t first = constexpr_frames_.back().first_local;
	for (std::size_t i = first; i < constexpr_locals_.size(); ++i)
		if (constexpr_locals_[i].pack_name == name) locals->push_back(i);
	if (!locals->empty()) return true;
	return GetFunction(constexpr_frames_.back().function).parameter_pack_name ==
		name;
}

bool SemanticAnalyzer::TryAnalyzeConstexprLocal(
	const std::string& spelling, TypeId target, ExpressionInfo* result)
{
	if (constexpr_frames_.empty()) return false;
	std::size_t local = 0;
	const NameId name = program_->names.Intern(spelling);
	if (!FindConstexprLocal(name, &local)) return false;
	const ConstexprLocalValue& value = constexpr_locals_[local];
	result->type = EffectiveType(value.type);
	result->category = VALUE_LVALUE;
	result->constexpr_local = local;
	result->constant = true;
	result->value = value.value;
	result->node = MakeDump(DUMP_ID_EXPRESSION, result->type,
		VALUE_LVALUE, name);
	dump_.nodes[result->node].constant = true;
	dump_.nodes[result->node].constant_value = result->value;
	++expression_count_;
	*result = ApplyTarget(*result, target);
	return true;
}

void SemanticAnalyzer::ReleaseConstexprScratch(
	std::size_t nodes, std::size_t edges)
{
	if (nodes > dump_.nodes.size() || edges > dump_.edges.size())
		throw std::logic_error("constexpr scratch mark is invalid");
	if (dump_.nodes.size() > constexpr_scratch_peak_nodes_)
		constexpr_scratch_peak_nodes_ = dump_.nodes.size();
	dump_.nodes.erase(dump_.nodes.begin() + nodes, dump_.nodes.end());
	dump_.edges.erase(dump_.edges.begin() + edges, dump_.edges.end());
}

bool SemanticAnalyzer::AnalyzeConstexprExpression(NodeId node, ScopeId scope,
	TypeId target, ExpressionInfo* result)
{
	const std::size_t nodes = dump_.nodes.size();
	const std::size_t edges = dump_.edges.size();
	try
	{
		*result = AnalyzeExpression(node, scope, target);
	}
	catch (...)
	{
		ReleaseConstexprScratch(nodes, edges);
		throw;
	}
	ReleaseConstexprScratch(nodes, edges);
	return result->constant;
}

bool SemanticAnalyzer::AnalyzeConstexprInitializer(NodeId node, ScopeId scope,
	TypeId target, ExpressionInfo* result)
{
	const std::size_t nodes = dump_.nodes.size();
	const std::size_t edges = dump_.edges.size();
	try
	{
		*result = AnalyzeVariableInitializer(node, scope, target, true);
	}
	catch (...)
	{
		ReleaseConstexprScratch(nodes, edges);
		throw;
	}
	ReleaseConstexprScratch(nodes, edges);
	return result->constant;
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
	const std::size_t nodes = dump_.nodes.size();
	const std::size_t edges = dump_.edges.size();
	bool valid = false;
	try
	{
		if (arena_->IsTag(node, "alias-declaration"))
		{
			const TypeId type = BuildTypeId(FindChild(node, "type-id"), scope);
			valid = AddConstexprTypeAlias(
				program_->names.Intern(arena_->Payload(node)), type);
		}
		else if (arena_->IsTag(node, "using-directive"))
		{
			const NodeId target = FindChild(node, "target");
			const ScopeId target_scope = target == kNoNode ? kNoScope :
				ResolveScopeSpelling(scope, arena_->Payload(target));
			if (target_scope == kNoScope)
				throw std::runtime_error(
					"constexpr using namespace target not found");
			AddConstexprUsingNamespace(target_scope);
			valid = true;
		}
		else if (arena_->IsTag(node, "static-assert-declaration"))
		{
			AnalyzeStaticAssert(node, scope);
			valid = true;
		}
		else if (arena_->IsTag(node, "simple-declaration"))
		{
			const NodeId specifiers = FindChild(node, "decl-specifier-seq");
			const NodeId list = FindChild(node, "init-declarator-list");
			const SpecInfo spec = BuildSpecifiers(
				specifiers, scope, std::string(), list != kNoNode);
			valid = list != kNoNode &&
				spec.storage_class == STORAGE_CLASS_NONE &&
				!spec.thread_local_storage;
			for (std::uint32_t edge = valid ? arena_->FirstEdge(list) : kNoEdge;
				edge != kNoEdge && valid; edge = arena_->NextEdge(edge))
			{
				const NodeId item = arena_->EdgeChild(edge);
				const NodeId declarator = FindChild(item, "declarator");
				DeclaratorInfo parsed = BuildDeclarator(
					declarator, spec.type, scope);
				parsed.name = DeclaratorNamePath(declarator).Last();
				if (spec.is_constexpr)
					parsed.type = program_->types.Qualify(parsed.type, CV_CONST);
				if (spec.is_typedef)
					valid = AddConstexprTypeAlias(parsed.name, parsed.type);
				else
				{
					const NodeId initializer = FindChild(item, "initializer");
					ExpressionInfo value;
					valid = parsed.name != 0 && initializer != kNoNode &&
						IsIntegral(parsed.type, true) &&
						AnalyzeConstexprInitializer(initializer, scope,
							parsed.type, &value) &&
						AddConstexprLocal(parsed.name, 0, parsed.type,
							value.value);
				}
			}
		}
	}
	catch (...)
	{
		ReleaseConstexprScratch(nodes, edges);
		throw;
	}
	ReleaseConstexprScratch(nodes, edges);
	return valid;
}

bool SemanticAnalyzer::EvaluateConstexprCondition(
	NodeId node, ScopeId scope, bool* value)
{
	if (!ConsumeConstexprStep()) return false;
	const NodeId first = FirstSemanticChild(node);
	const NodeId declaration = first != kNoNode &&
		arena_->IsTag(first, "condition-declaration") ? first : node;
	const NodeId specifiers = FindChild(declaration, "decl-specifier-seq");
	if (specifiers != kNoNode)
	{
		const SpecInfo spec = BuildSpecifiers(
			specifiers, scope, std::string(), true);
		const NodeId declarator = FindChild(declaration, "declarator");
		DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, scope);
		parsed.name = DeclaratorNamePath(declarator).Last();
		if (spec.is_constexpr)
			parsed.type = program_->types.Qualify(parsed.type, CV_CONST);
		const NodeId initializer = FindChild(declaration, "initializer");
		ExpressionInfo evaluated;
		if (parsed.name == 0 || initializer == kNoNode ||
			!IsIntegral(parsed.type, true) ||
			!AnalyzeConstexprInitializer(
				initializer, scope, parsed.type, &evaluated) ||
			!AddConstexprLocal(
				parsed.name, 0, parsed.type, evaluated.value)) return false;
		*value = evaluated.value != 0;
		return true;
	}
	ExpressionInfo expression;
	if (first == kNoNode ||
		!AnalyzeConstexprExpression(first, scope, kNoType, &expression) ||
		!IsIntegral(expression.type, true)) return false;
	*value = expression.value != 0;
	return true;
}

ConstexprFlow SemanticAnalyzer::EvaluateConstexprCompound(
	NodeId node, ScopeId scope, TypeId result_type, std::int64_t* result)
{
	PushConstexprBlock();
	ConstexprFlow result_flow = CONSTEXPR_FLOW_NORMAL;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		const ConstexprFlow flow = IsDeclaration(child) ?
			(EvaluateConstexprDeclaration(child, scope) ?
				CONSTEXPR_FLOW_NORMAL : CONSTEXPR_FLOW_INVALID) :
			EvaluateConstexprStatement(child, scope, result_type, result);
		if (flow != CONSTEXPR_FLOW_NORMAL)
		{
			result_flow = flow;
			break;
		}
	}
	PopConstexprBlock();
	return result_flow;
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
		ExpressionInfo value;
		if (!AnalyzeConstexprExpression(
			expression, scope, result_type, &value))
			return CONSTEXPR_FLOW_INVALID;
		if (!value.constant || !IsIntegral(value.type, true))
			return CONSTEXPR_FLOW_INVALID;
		*result = NormalizeIntegralConstant(result_type, value.value);
		return CONSTEXPR_FLOW_RETURN;
	}
	if (arena_->IsTag(node, "expression-statement"))
	{
		const NodeId expression = FirstSemanticChild(node);
		if (expression == kNoNode) return CONSTEXPR_FLOW_NORMAL;
		ExpressionInfo value;
		return AnalyzeConstexprExpression(
			expression, scope, kNoType, &value) ?
			CONSTEXPR_FLOW_NORMAL : CONSTEXPR_FLOW_INVALID;
	}
	if (arena_->IsTag(node, "if-statement"))
	{
		PushConstexprBlock();
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
			!EvaluateConstexprCondition(condition, scope, &selected))
		{
			PopConstexprBlock();
			return CONSTEXPR_FLOW_INVALID;
		}
		const NodeId branch = selected ? then_branch : else_branch;
		const ConstexprFlow flow = branch == kNoNode ? CONSTEXPR_FLOW_NORMAL :
			EvaluateConstexprStatement(branch, scope, result_type, result);
		PopConstexprBlock();
		return flow;
	}
	if (arena_->IsTag(node, "while-statement") ||
		arena_->IsTag(node, "do-statement"))
	{
		const bool is_do = arena_->IsTag(node, "do-statement");
		PushConstexprBlock();
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
			PushConstexprBlock();
			bool active = true;
			if (!is_do)
			{
				if (condition == kNoNode ||
					!EvaluateConstexprCondition(condition, scope, &active))
				{
					PopConstexprBlock();
					PopConstexprBlock();
					return CONSTEXPR_FLOW_INVALID;
				}
				if (!active)
				{
					PopConstexprBlock();
					PopConstexprBlock();
					return CONSTEXPR_FLOW_NORMAL;
				}
			}
			if (body == kNoNode)
			{
				PopConstexprBlock();
				PopConstexprBlock();
				return CONSTEXPR_FLOW_INVALID;
			}
			const ConstexprFlow flow = EvaluateConstexprStatement(
				body, scope, result_type, result);
			if (flow == CONSTEXPR_FLOW_RETURN ||
				flow == CONSTEXPR_FLOW_INVALID)
			{
				PopConstexprBlock();
				PopConstexprBlock();
				return flow;
			}
			if (flow == CONSTEXPR_FLOW_BREAK)
			{
				PopConstexprBlock();
				PopConstexprBlock();
				return CONSTEXPR_FLOW_NORMAL;
			}
			if (is_do)
			{
				if (condition == kNoNode ||
					!EvaluateConstexprCondition(condition, scope, &active))
				{
					PopConstexprBlock();
					PopConstexprBlock();
					return CONSTEXPR_FLOW_INVALID;
				}
				if (!active)
				{
					PopConstexprBlock();
					PopConstexprBlock();
					return CONSTEXPR_FLOW_NORMAL;
				}
			}
			PopConstexprBlock();
		}
	}
	if (arena_->IsTag(node, "for-statement"))
	{
		PushConstexprBlock();
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
						if (!EvaluateConstexprDeclaration(initializer, scope))
						{
							PopConstexprBlock();
							return CONSTEXPR_FLOW_INVALID;
						}
					}
					else
					{
						ExpressionInfo value;
						if (!AnalyzeConstexprExpression(
							initializer, scope, kNoType, &value))
						{
							PopConstexprBlock();
							return CONSTEXPR_FLOW_INVALID;
						}
					}
				}
			}
			else if (arena_->IsTag(child, "condition")) condition = child;
			else if (arena_->IsTag(child, "iteration"))
				iteration_expression = FirstSemanticChild(child);
			else body = child;
		}
		for (;;)
		{
			PushConstexprBlock();
			bool active = true;
			if (condition != kNoNode &&
				!EvaluateConstexprCondition(condition, scope, &active))
			{
				PopConstexprBlock();
				PopConstexprBlock();
				return CONSTEXPR_FLOW_INVALID;
			}
			if (!active)
			{
				PopConstexprBlock();
				PopConstexprBlock();
				return CONSTEXPR_FLOW_NORMAL;
			}
			if (body == kNoNode)
			{
				PopConstexprBlock();
				PopConstexprBlock();
				return CONSTEXPR_FLOW_INVALID;
			}
			const ConstexprFlow flow = EvaluateConstexprStatement(
				body, scope, result_type, result);
			if (flow == CONSTEXPR_FLOW_RETURN ||
				flow == CONSTEXPR_FLOW_INVALID)
			{
				PopConstexprBlock();
				PopConstexprBlock();
				return flow;
			}
			if (flow == CONSTEXPR_FLOW_BREAK)
			{
				PopConstexprBlock();
				PopConstexprBlock();
				return CONSTEXPR_FLOW_NORMAL;
			}
			if (iteration_expression != kNoNode)
			{
				ExpressionInfo value;
				if (!AnalyzeConstexprExpression(
					iteration_expression, scope, kNoType, &value))
				{
					PopConstexprBlock();
					PopConstexprBlock();
					return CONSTEXPR_FLOW_INVALID;
				}
			}
			PopConstexprBlock();
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

	const bool outermost = constexpr_evaluation_depth_ == 0;
	if (outermost)
	{
		constexpr_scratch_dump_.nodes.clear();
		constexpr_scratch_dump_.edges.clear();
		std::swap(dump_, constexpr_scratch_dump_);
	}
	++constexpr_evaluation_depth_;
	if (constexpr_evaluation_depth_ > constexpr_max_depth_)
		constexpr_max_depth_ = constexpr_evaluation_depth_;
	constexpr_evaluation_stack_.push_back(function);
	constexpr_frames_.push_back(ConstexprFrame(function,
		constexpr_locals_.size(), constexpr_scope_facts_.size(),
		constexpr_block_offsets_.size()));
	for (std::size_t i = 0; i < info.parameters.size(); ++i)
	{
		const ParameterInfo& parameter = info.parameters[i];
		const TypeId type = ParameterBindingType(parameter);
		if (!AddConstexprLocal(parameter.name, parameter.pack_name,
			type, key.parameter_values[i]))
			throw std::logic_error("duplicate constexpr parameter binding");
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
			info.definition_body, info.lexical_scope, result_type, &evaluated);
	}
	catch (...)
	{
		flow = CONSTEXPR_FLOW_INVALID;
	}
	current_return_type_ = previous_return;
	current_class_context_ = previous_class;
	current_function_context_ = previous_function;
	const ConstexprFrame frame = constexpr_frames_.back();
	constexpr_block_offsets_.erase(
		constexpr_block_offsets_.begin() + frame.first_block,
		constexpr_block_offsets_.end());
	constexpr_locals_.erase(
		constexpr_locals_.begin() + frame.first_local,
		constexpr_locals_.end());
	constexpr_scope_facts_.erase(
		constexpr_scope_facts_.begin() + frame.first_scope_fact,
		constexpr_scope_facts_.end());
	constexpr_frames_.pop_back();
	constexpr_evaluation_stack_.pop_back();
	--constexpr_evaluation_depth_;
	if (outermost)
	{
		dump_.nodes.clear();
		dump_.edges.clear();
		std::swap(dump_, constexpr_scratch_dump_);
	}

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
