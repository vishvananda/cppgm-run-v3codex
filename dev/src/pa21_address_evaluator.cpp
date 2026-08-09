#include "pa12_semantic_detail.h"

#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

std::uint32_t SemanticAnalyzer::InternConstexprAddress(
	const ConstexprAddressValue& address)
{
	std::unordered_map<ConstexprAddressValue, std::uint32_t,
		ConstexprAddressValueHash>::const_iterator found =
		constexpr_address_index_.find(address);
	if (found != constexpr_address_index_.end()) return found->second;
	if (constexpr_addresses_.size() >= kNoConstexprAddress)
		throw std::runtime_error("too many constexpr address facts");
	const std::uint32_t result =
		static_cast<std::uint32_t>(constexpr_addresses_.size());
	constexpr_addresses_.push_back(address);
	constexpr_address_index_.insert(std::make_pair(address, result));
	return result;
}

const ConstexprAddressValue* SemanticAnalyzer::ConstexprAddressAt(
	std::uint32_t address) const
{
	return address == kNoConstexprAddress ||
		address >= constexpr_addresses_.size() ? 0 :
		&constexpr_addresses_[address];
}

std::uint32_t SemanticAnalyzer::NullConstexprAddress()
{
	return InternConstexprAddress(ConstexprAddressValue());
}

void SemanticAnalyzer::SetExpressionAddress(ExpressionInfo* expression,
	std::uint32_t address) const
{
	if (!ConstexprAddressAt(address))
		throw std::logic_error("invalid constexpr address identity");
	const ConstexprAddressValue* value = ConstexprAddressAt(address);
	expression->constant = value->kind == CONSTEXPR_ADDRESS_NULL ||
		constant_expression_required_depth_ != 0 ||
		constexpr_evaluation_depth_ != 0;
	expression->floating_constant = false;
	expression->constexpr_object = kNoConstexprObject;
	expression->constexpr_address = address;
}

void SemanticAnalyzer::SetExpressionLvalueAddress(ExpressionInfo* expression,
	std::uint32_t address) const
{
	if (!ConstexprAddressAt(address))
		throw std::logic_error("invalid constexpr lvalue address identity");
	expression->constexpr_lvalue_address = address;
}

std::uint32_t SemanticAnalyzer::ExpressionAddress(
	const ExpressionInfo& expression) const
{
	return ConstexprAddressAt(expression.constexpr_address) ?
		expression.constexpr_address : kNoConstexprAddress;
}

std::uint32_t SemanticAnalyzer::BindingAddress(BindingId binding) const
{
	if (binding == kNoBinding || binding >= program_->bindings.size())
		return kNoConstexprAddress;
	BindingId owner = binding;
	if ((owner >= constexpr_address_by_binding_.size() ||
		constexpr_address_by_binding_[owner] == kNoConstexprAddress) &&
		program_->bindings[owner].canonical != owner)
		owner = program_->bindings[owner].canonical;
	if (owner >= constexpr_address_by_binding_.size())
		return kNoConstexprAddress;
	const std::uint32_t address = constexpr_address_by_binding_[owner];
	return ConstexprAddressAt(address) ? address : kNoConstexprAddress;
}

void SemanticAnalyzer::PublishBindingAddress(BindingId binding,
	std::uint32_t address)
{
	if (binding == kNoBinding || binding >= program_->bindings.size() ||
		!ConstexprAddressAt(address))
		throw std::logic_error("invalid constexpr address publication");
	program_->bindings[binding].constant = true;
	if (constexpr_address_by_binding_.size() <= binding)
		constexpr_address_by_binding_.resize(
			static_cast<std::size_t>(binding) + 1, kNoConstexprAddress);
	constexpr_address_by_binding_[binding] = address;
}

std::uint32_t SemanticAnalyzer::LvalueAddress(ExpressionInfo* expression)
{
	if (ConstexprAddressAt(expression->constexpr_lvalue_address))
		return expression->constexpr_lvalue_address;
	if (expression->constexpr_local < constexpr_locals_.size())
	{
		ConstexprLocalValue& local =
			constexpr_locals_[expression->constexpr_local];
		const TypeRecord top = program_->types.Get(local.type);
		if ((top.kind == TYPE_LVALUE_REFERENCE ||
			top.kind == TYPE_RVALUE_REFERENCE) &&
			ConstexprAddressAt(local.address))
		{
			SetExpressionLvalueAddress(expression, local.address);
			return local.address;
		}
		if (local.storage_identity == 0)
			local.storage_identity = next_constexpr_storage_identity_++;
		const std::int64_t extent = static_cast<std::int64_t>(
			program_->SizeOf(EffectiveType(local.type)));
		const std::uint32_t address = InternConstexprAddress(
			ConstexprAddressValue(CONSTEXPR_ADDRESS_LOCAL,
				local.storage_identity, 0, 0, extent));
		SetExpressionLvalueAddress(expression, address);
		return address;
	}
	if (expression->binding != kNoBinding &&
		expression->binding < program_->bindings.size())
	{
		const BindingRecord& binding =
			program_->bindings[expression->binding];
		if (program_->types.IsReference(binding.type))
		{
			const std::uint32_t referred = BindingAddress(expression->binding);
			if (referred != kNoConstexprAddress)
			{
				SetExpressionLvalueAddress(expression, referred);
				return referred;
			}
		}
		const BindingId canonical = binding.canonical;
		const bool function = binding.kind == BIND_FUNCTION;
		const TypeRecord storage = program_->types.Get(
			program_->types.RemoveTopCv(EffectiveType(binding.type)));
		if (!function && storage.kind == TYPE_ARRAY && storage.bound == 0)
			return kNoConstexprAddress;
		const std::int64_t extent = function ? 0 :
			static_cast<std::int64_t>(program_->SizeOf(
				EffectiveType(binding.type)));
		const std::uint32_t address = InternConstexprAddress(
			ConstexprAddressValue(function ? CONSTEXPR_ADDRESS_FUNCTION :
				CONSTEXPR_ADDRESS_BINDING, canonical, 0, 0, extent));
		SetExpressionLvalueAddress(expression, address);
		return address;
	}
	if (expression->string_unit_begin != kNoDumpEdge &&
		expression->string_unit_count != 0)
	{
		const TypeRecord array = program_->types.Get(
			program_->types.RemoveTopCv(expression->type));
		if (array.kind != TYPE_ARRAY) return kNoConstexprAddress;
		const std::int64_t extent = static_cast<std::int64_t>(
			expression->string_unit_count * program_->SizeOf(array.child));
		const std::uint32_t address = InternConstexprAddress(
			ConstexprAddressValue(CONSTEXPR_ADDRESS_STRING,
				dump_.nodes[expression->node].text, 0, 0, extent));
		SetExpressionLvalueAddress(expression, address);
		return address;
	}
	return kNoConstexprAddress;
}

std::uint32_t SemanticAnalyzer::OffsetConstexprAddress(
	std::uint32_t address, std::int64_t byte_offset, bool narrow,
	std::int64_t extent)
{
	const ConstexprAddressValue* source = ConstexprAddressAt(address);
	if (!source) return kNoConstexprAddress;
	if (source->kind == CONSTEXPR_ADDRESS_NULL)
		return byte_offset == 0 ? address : kNoConstexprAddress;
	if ((byte_offset > 0 && source->offset >
		std::numeric_limits<std::int64_t>::max() - byte_offset) ||
		(byte_offset < 0 && source->offset <
		std::numeric_limits<std::int64_t>::min() - byte_offset))
		return kNoConstexprAddress;
	const std::int64_t offset = source->offset + byte_offset;
	if (offset < source->lower_bound || offset > source->upper_bound)
		return kNoConstexprAddress;
	std::int64_t lower = source->lower_bound;
	std::int64_t upper = source->upper_bound;
	if (narrow)
	{
		if (extent < 0 || offset >
			std::numeric_limits<std::int64_t>::max() - extent ||
			offset + extent > source->upper_bound)
			return kNoConstexprAddress;
		lower = offset;
		upper = offset + extent;
	}
	return InternConstexprAddress(ConstexprAddressValue(source->kind,
		source->identity, offset, lower, upper));
}

bool SemanticAnalyzer::ExpressionTruth(
	const ExpressionInfo& expression) const
{
	const ConstexprAddressValue* address =
		ConstexprAddressAt(ExpressionAddress(expression));
	return address ? address->kind != CONSTEXPR_ADDRESS_NULL :
		ScalarTruth(ExpressionScalar(expression));
}

bool SemanticAnalyzer::TryAnalyzeConstexprIndirectCall(ExpressionInfo* callee,
	ScopeId scope, const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>& arguments, TypeId target,
	ExpressionInfo* result)
{
	std::uint32_t callable_address = ExpressionAddress(*callee);
	if (callable_address == kNoConstexprAddress)
		callable_address = callee->constexpr_lvalue_address;
	if (callable_address == kNoConstexprAddress &&
		callee->binding != kNoBinding &&
		callee->binding < program_->bindings.size() &&
		program_->bindings[callee->binding].kind == BIND_FUNCTION &&
		program_->types.Get(program_->types.RemoveTopCv(
			EffectiveType(callee->type))).kind == TYPE_FUNCTION)
		callable_address = LvalueAddress(callee);
	const ConstexprAddressValue* address =
		ConstexprAddressAt(callable_address);
	if (!address || address->kind != CONSTEXPR_ADDRESS_FUNCTION ||
		address->offset != 0 || address->identity >= program_->bindings.size())
		return false;
	const BindingId function = static_cast<BindingId>(address->identity);
	if (program_->bindings[function].kind != BIND_FUNCTION) return false;
	*result = BuildResolvedCall(function, scope, argument_syntax,
		arguments, 0, target);
	return true;
}

ConstexprFlow SemanticAnalyzer::EvaluateConstexprReturn(NodeId expression,
	ScopeId scope, TypeId result_type, ConstexprScalarValue* result,
	std::uint32_t* result_address)
{
	ExpressionInfo value;
	if (!AnalyzeConstexprExpression(expression, scope, result_type, &value))
		return CONSTEXPR_FLOW_INVALID;
	const TypeRecord returned = program_->types.Get(result_type);
	if (returned.kind == TYPE_LVALUE_REFERENCE ||
		returned.kind == TYPE_RVALUE_REFERENCE)
	{
		const std::uint32_t address = LvalueAddress(&value);
		if (address == kNoConstexprAddress) return CONSTEXPR_FLOW_INVALID;
		*result_address = address;
	}
	else if (IsPointer(EffectiveType(result_type)))
	{
		const std::uint32_t address = ExpressionAddress(value);
		if (address == kNoConstexprAddress) return CONSTEXPR_FLOW_INVALID;
		*result_address = address;
	}
	else
	{
		if (!value.constant ||
			(!IsIntegral(value.type, true) && !IsFloating(value.type)))
			return CONSTEXPR_FLOW_INVALID;
		*result = ConvertScalarConstant(
			value.type, result_type, ExpressionScalar(value));
	}
	return CONSTEXPR_FLOW_RETURN;
}

ExpressionInfo SemanticAnalyzer::MaterializeConstexprAddress(
	std::uint32_t address_id, TypeId type)
{
	const ConstexprAddressValue* address = ConstexprAddressAt(address_id);
	if (!address) throw std::logic_error("invalid constexpr address materialization");
	if (address->offset != 0)
		throw std::runtime_error(
			"offset constexpr address materialization is unsupported");
	if (address->kind == CONSTEXPR_ADDRESS_NULL)
	{
		ExpressionInfo result = MakeLiteral(EffectiveType(type),
			program_->names.Intern("0"));
		result.integer_literal_zero = true;
		SetExpressionAddress(&result, address_id);
		RecordExpressionFacts(result);
		return result;
	}
	if (address->kind == CONSTEXPR_ADDRESS_STRING)
	{
		if (address->identity > std::numeric_limits<NameId>::max())
			throw std::logic_error("invalid constexpr string spelling identity");
		ExpressionInfo result = MakeStringLiteral(program_->names.Get(
			static_cast<NameId>(address->identity)));
		return ApplyTarget(result, type);
	}
	if ((address->kind != CONSTEXPR_ADDRESS_BINDING &&
		 address->kind != CONSTEXPR_ADDRESS_FUNCTION) ||
		address->identity >= program_->bindings.size())
		throw std::runtime_error(
			"transient constexpr address cannot escape evaluation");
	const BindingId binding = static_cast<BindingId>(address->identity);
	const BindingRecord& record = program_->bindings[binding];
	ExpressionInfo base;
	base.node = MakeDump(DUMP_ID_EXPRESSION, EffectiveType(record.type),
		VALUE_LVALUE, record.name, binding);
	base.type = EffectiveType(record.type);
	base.category = VALUE_LVALUE;
	base.binding = binding;
	SetExpressionLvalueAddress(&base, address_id);
	++expression_count_;
	const TypeRecord shape = program_->types.Get(
		program_->types.RemoveTopCv(base.type));
	if (program_->types.IsReference(type)) return ApplyTarget(base, type);
	if (shape.kind == TYPE_ARRAY || shape.kind == TYPE_FUNCTION)
		return ApplyTarget(base, type);
	ExpressionInfo result;
	result.node = MakeDump(DUMP_UNARY_EXPRESSION, EffectiveType(type),
		VALUE_PRVALUE, program_->names.Intern("OP_AMP:&"));
	dump_.Add(result.node, base.node);
	result.type = EffectiveType(type);
	result.category = VALUE_PRVALUE;
	SetExpressionAddress(&result, address_id);
	++expression_count_;
	return result;
}

}
}
