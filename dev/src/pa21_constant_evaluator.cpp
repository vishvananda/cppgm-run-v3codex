#include "pa12_semantic_detail.h"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
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

ConstexprScalarValue SemanticAnalyzer::ExpressionScalar(
	const ExpressionInfo& value) const
{
	return value.floating_constant ?
		ConstexprScalarValue(value.floating_value) :
		ConstexprScalarValue(value.value);
}

ConstexprScalarValue SemanticAnalyzer::ConvertScalarConstant(
	TypeId source_type, TypeId target_type,
	const ConstexprScalarValue& value) const
{
	source_type = program_->types.RemoveTopCv(EffectiveType(source_type));
	target_type = program_->types.RemoveTopCv(EffectiveType(target_type));
	if ((!IsIntegral(source_type, true) && !IsFloating(source_type)) ||
		(!IsIntegral(target_type, true) && !IsFloating(target_type)))
		throw std::logic_error("non-arithmetic scalar constant conversion");
	const TypeRecord& target_record = program_->types.Get(target_type);
	if (target_record.kind == TYPE_FUNDAMENTAL &&
		target_record.fundamental == FUND_BOOL)
		return ConstexprScalarValue(
			static_cast<std::int64_t>(ScalarTruth(value)));
	if (IsFloating(target_type))
	{
		long double converted = 0.0L;
		if (value.kind == CONSTEXPR_SCALAR_FLOATING)
			converted = value.floating;
		else if (IsUnsignedIntegral(source_type))
			converted = static_cast<long double>(
				static_cast<std::uint64_t>(value.integral));
		else converted = static_cast<long double>(value.integral);
		switch (FundamentalOf(target_type))
		{
		case FUND_FLOAT:
			converted = static_cast<long double>(
				static_cast<float>(converted));
			break;
		case FUND_DOUBLE:
			converted = static_cast<long double>(
				static_cast<double>(converted));
			break;
		case FUND_LONG_DOUBLE: break;
		default: throw std::logic_error("invalid floating constant target");
		}
		if (!std::isfinite(converted))
			throw std::runtime_error("non-finite floating constant");
		return ConstexprScalarValue(converted);
	}
	if (value.kind == CONSTEXPR_SCALAR_FLOATING)
	{
		if (!std::isfinite(value.floating))
			throw std::runtime_error("non-finite floating to integral conversion");
		const std::size_t width = IntegralWidth(target_type);
		const long double minimum = IsUnsignedIntegral(target_type) ? 0.0L :
			width == 64 ?
				static_cast<long double>(std::numeric_limits<std::int64_t>::min()) :
				-std::ldexp(1.0L, static_cast<int>(width - 1));
		const long double maximum = IsUnsignedIntegral(target_type) ?
			width == 64 ?
				static_cast<long double>(
					std::numeric_limits<std::uint64_t>::max()) :
				std::ldexp(1.0L, static_cast<int>(width)) - 1.0L :
			width == 64 ?
				static_cast<long double>(std::numeric_limits<std::int64_t>::max()) :
				std::ldexp(1.0L, static_cast<int>(width - 1)) - 1.0L;
		if (value.floating < minimum || value.floating > maximum)
			throw std::runtime_error("floating constant outside integral range");
		const std::int64_t converted = IsUnsignedIntegral(target_type) ?
			static_cast<std::int64_t>(
				static_cast<std::uint64_t>(value.floating)) :
			static_cast<std::int64_t>(value.floating);
		return ConstexprScalarValue(
			NormalizeIntegralConstant(target_type, converted));
	}
	return ConstexprScalarValue(
		NormalizeIntegralConstant(target_type, value.integral));
}

ConstexprScalarValue SemanticAnalyzer::NormalizeScalarConstant(
	TypeId type, const ConstexprScalarValue& value) const
{
	return ConvertScalarConstant(type, type, value);
}

void SemanticAnalyzer::SetExpressionScalar(ExpressionInfo* expression,
	const ConstexprScalarValue& value) const
{
	expression->constant = true;
	expression->floating_constant =
		value.kind == CONSTEXPR_SCALAR_FLOATING;
	if (expression->floating_constant)
		expression->floating_value = value.floating;
	else expression->value = value.integral;
}

ConstexprScalarValue SemanticAnalyzer::BindingScalar(BindingId binding) const
{
	if (binding == kNoBinding || binding >= program_->bindings.size())
		throw std::logic_error("invalid constant binding identity");
	const BindingRecord& record = program_->bindings[binding];
	if (!record.constant)
		throw std::logic_error("binding has no constant value");
	BindingId owner = binding;
	if ((owner >= floating_constant_fact_by_binding_.size() ||
		floating_constant_fact_by_binding_[owner] == 0) &&
		record.canonical != binding)
		owner = record.canonical;
	if (owner < floating_constant_fact_by_binding_.size())
	{
		const std::uint32_t fact = floating_constant_fact_by_binding_[owner];
		if (fact != 0)
		{
			if (fact > floating_constant_values_.size())
				throw std::logic_error("floating constant fact is out of range");
			return ConstexprScalarValue(floating_constant_values_[fact - 1]);
		}
	}
	return ConstexprScalarValue(record.value);
}

void SemanticAnalyzer::PublishBindingScalar(BindingId binding,
	const ConstexprScalarValue& value)
{
	if (binding == kNoBinding || binding >= program_->bindings.size())
		throw std::logic_error("invalid constant binding publication");
	BindingRecord& record = program_->bindings[binding];
	record.constant = true;
	if (value.kind == CONSTEXPR_SCALAR_INTEGRAL)
	{
		record.value = value.integral;
		return;
	}
	if (floating_constant_fact_by_binding_.size() <= binding)
		floating_constant_fact_by_binding_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	std::uint32_t& fact = floating_constant_fact_by_binding_[binding];
	if (fact == 0)
	{
		if (floating_constant_values_.size() >=
			std::numeric_limits<std::uint32_t>::max())
			throw std::runtime_error("too many floating constant facts");
		floating_constant_values_.push_back(value.floating);
		fact = static_cast<std::uint32_t>(floating_constant_values_.size());
	}
	else
	{
		if (fact > floating_constant_values_.size())
			throw std::logic_error("floating constant fact is out of range");
		floating_constant_values_[fact - 1] = value.floating;
	}
}

bool SemanticAnalyzer::ScalarTruth(const ConstexprScalarValue& value) const
{
	return value.kind == CONSTEXPR_SCALAR_FLOATING ?
		value.floating != 0.0L : value.integral != 0;
}

ConstexprScalarValue SemanticAnalyzer::ApplyConstantScalarBinary(
	const std::string& operation, const ConstexprScalarValue& left,
	const ConstexprScalarValue& right, TypeId operand_type) const
{
	if (operation == "&&")
		return ConstexprScalarValue(static_cast<std::int64_t>(
			ScalarTruth(left) && ScalarTruth(right)));
	if (operation == "||")
		return ConstexprScalarValue(static_cast<std::int64_t>(
			ScalarTruth(left) || ScalarTruth(right)));
	if (operation == ",") return right;
	if (operand_type != kNoType && IsFloating(operand_type))
	{
		if (left.kind != CONSTEXPR_SCALAR_FLOATING ||
			right.kind != CONSTEXPR_SCALAR_FLOATING)
			throw std::logic_error("unnormalized floating constant operands");
		const long double l = left.floating;
		const long double r = right.floating;
		if (operation == "==" || operation == "!=" || operation == "<" ||
			operation == ">" || operation == "<=" || operation == ">=")
		{
			const bool compared = operation == "==" ? l == r :
				operation == "!=" ? l != r : operation == "<" ? l < r :
				operation == ">" ? l > r : operation == "<=" ? l <= r : l >= r;
			return ConstexprScalarValue(static_cast<std::int64_t>(compared));
		}
		if (operation == "/" && r == 0.0L)
			throw std::runtime_error("floating constant division by zero");
		long double calculated = operation == "+" ? l + r :
			operation == "-" ? l - r : operation == "*" ? l * r :
			operation == "/" ? l / r :
			throw std::runtime_error("unsupported floating constant operator");
		return NormalizeScalarConstant(
			operand_type, ConstexprScalarValue(calculated));
	}
	return ConstexprScalarValue(ApplyConstantBinary(operation,
		left.integral, right.integral, operand_type));
}

NameId SemanticAnalyzer::InternScalar(TypeId type,
	const ConstexprScalarValue& value)
{
	if (value.kind == CONSTEXPR_SCALAR_INTEGRAL)
		return InternNumber(value.integral);
	std::ostringstream spelling;
	spelling.imbue(std::locale::classic());
	const FundamentalKind kind = FundamentalOf(type);
	const int precision = kind == FUND_FLOAT ?
		std::numeric_limits<float>::max_digits10 : kind == FUND_DOUBLE ?
		std::numeric_limits<double>::max_digits10 :
		std::numeric_limits<long double>::max_digits10;
	spelling << std::setprecision(precision) << value.floating;
	if (kind == FUND_FLOAT) spelling << 'f';
	else if (kind == FUND_LONG_DOUBLE) spelling << 'L';
	return program_->names.Intern(spelling.str());
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
	TypeId type, const ConstexprScalarValue& value, std::size_t* local)
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
		name, pack_name, type, NormalizeScalarConstant(type, value)));
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
	SetExpressionScalar(result, value.value);
	result->node = MakeDump(DUMP_ID_EXPRESSION, result->type,
		VALUE_LVALUE, name);
	dump_.nodes[result->node].constant = true;
	if (!result->floating_constant)
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
		(IsIntegral(type, true) || IsFloating(type)) && !initializer.constant)
		throw std::runtime_error(
			"constexpr scalar initializer is not constant");
	if (!initializer.constant ||
		(!spec.is_constexpr &&
		 !(IsConst(type) && (IsIntegral(type, true) || IsFloating(type))) &&
		 !(constexpr_evaluation_depth_ != 0 &&
			(IsIntegral(type, true) || IsFloating(type)))))
		return;
	const bool scalar_type = IsIntegral(type, true) || IsFloating(type);
	const ConstexprScalarValue converted = scalar_type ?
		ConvertScalarConstant(initializer.type, type,
			ExpressionScalar(initializer)) :
		ConstexprScalarValue(initializer.value);
	PublishBindingScalar(binding, converted);
	if (spec.is_constexpr && !IsPointer(type))
	{
		dump_.nodes[initializer.node].type = type;
		if (converted.kind == CONSTEXPR_SCALAR_FLOATING &&
			dump_.nodes[initializer.node].kind == DUMP_LITERAL)
			dump_.nodes[initializer.node].text = InternScalar(type, converted);
	}
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
						(IsIntegral(parsed.type, true) || IsFloating(parsed.type)) &&
						AnalyzeConstexprInitializer(initializer, scope,
							parsed.type, &value) &&
						AddConstexprLocal(parsed.name, 0, parsed.type,
							ExpressionScalar(value));
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
			(!IsIntegral(parsed.type, true) && !IsFloating(parsed.type)) ||
			!AnalyzeConstexprInitializer(
				initializer, scope, parsed.type, &evaluated) ||
			!AddConstexprLocal(
				parsed.name, 0, parsed.type,
				ExpressionScalar(evaluated))) return false;
		*value = ScalarTruth(ExpressionScalar(evaluated));
		return true;
	}
	ExpressionInfo expression;
	if (first == kNoNode ||
		!AnalyzeConstexprExpression(first, scope, kNoType, &expression) ||
		(!IsIntegral(expression.type, true) &&
		 !IsFloating(expression.type))) return false;
	*value = ScalarTruth(ExpressionScalar(expression));
	return true;
}

ConstexprFlow SemanticAnalyzer::EvaluateConstexprCompound(
	NodeId node, ScopeId scope, TypeId result_type,
	ConstexprScalarValue* result)
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
	NodeId node, ScopeId scope, TypeId result_type,
	ConstexprScalarValue* result)
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
		if (!value.constant ||
			(!IsIntegral(value.type, true) && !IsFloating(value.type)))
			return CONSTEXPR_FLOW_INVALID;
		*result = ConvertScalarConstant(
			value.type, result_type, ExpressionScalar(value));
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
	const std::vector<ExpressionInfo>& arguments,
	ConstexprScalarValue* value)
{
	function = program_->bindings[function].canonical;
	const FunctionInfo info = GetFunction(function);
	const TypeId result_type = program_->types.Get(info.type).child;
	if (!info.constexpr_function || info.definition_body == kNoNode ||
		(!IsIntegral(result_type, true) && !IsFloating(result_type)) ||
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
		if (!arguments[i].constant ||
			(!IsIntegral(type, true) && !IsFloating(type))) return false;
		key.parameter_types.push_back(
			program_->types.RemoveTopCv(EffectiveType(type)));
		key.parameter_values.push_back(
			ConvertScalarConstant(arguments[i].type, type,
				ExpressionScalar(arguments[i])));
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
	ConstexprScalarValue evaluated;
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
	fact.value = NormalizeScalarConstant(result_type, evaluated);
	*value = fact.value;
	return true;
}

}
}
