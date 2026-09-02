#include "semantic/analysis/analyzer.h"
#include "semantic/constants/wide_integer.h"
#include "support/exception_types.h"
#include "support/scoped_state.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace cppgm
{
namespace semantic
{

namespace
{

const std::size_t kMaxConstexprDepth = 1024;
const std::size_t kMaxConstexprSteps = 1000000;

std::size_t MixConstexprHash(std::size_t seed, std::size_t value)
{
	return seed ^ (value + static_cast<std::size_t>(0x9e3779b9U) +
		(seed << 6) + (seed >> 2));
}

bool IsConstexprClassEntity(const Program& program, EntityId entity)
{
	if (entity == kNoEntity) return false;
	return IsClassNamedFlavor(program.entities[entity].flavor);
}

}

ConstexprScalarValue Analyzer::ExpressionScalar(
	const ExpressionInfo& value) const
{
	if (IsMemberPointer(value.type))
		return ConstexprScalarValue(value.binding, value.value);
	if (value.floating_constant)
		return ConstexprScalarValue(value.floating_value);
	if (value.integral_high_valid)
		return ConstexprScalarValue::IntegralBits(
			static_cast<std::uint64_t>(value.value), value.integral_high);
	return ConstexprScalarValue::IntegralBits(
		static_cast<std::uint64_t>(value.value),
		IsIntegral(value.type, true) && IsUnsignedIntegral(value.type) ? 0 :
		value.value < 0 ? ~std::uint64_t(0) : 0);
}

ConstexprScalarValue Analyzer::ConvertScalarConstant(
	TypeId source_type, TypeId target_type,
	const ConstexprScalarValue& value) const
{
	source_type = program_->types.RemoveTopCv(EffectiveType(source_type));
	target_type = program_->types.RemoveTopCv(EffectiveType(target_type));
	const bool source_member_pointer = IsMemberPointer(source_type);
	const bool target_member_pointer = IsMemberPointer(target_type);
	const TypeRecord& target_record = program_->types.Get(target_type);
	if (source_member_pointer && target_record.kind == TYPE_FUNDAMENTAL &&
		target_record.fundamental == FUND_BOOL)
		return ConstexprScalarValue(
			static_cast<std::int64_t>(ScalarTruth(value)));
	if (source_member_pointer && target_member_pointer)
	{
		if (value.kind != CONSTEXPR_SCALAR_MEMBER_POINTER)
			ThrowInternalCompilerError("invalid member pointer constant");
		return value;
	}
	if ((!IsIntegral(source_type, true) && !IsFloating(source_type)) ||
		(!IsIntegral(target_type, true) && !IsFloating(target_type)))
		ThrowInternalCompilerError(
			"non-arithmetic scalar constant conversion");
	if (target_record.kind == TYPE_FUNDAMENTAL &&
		target_record.fundamental == FUND_BOOL)
		return ConstexprScalarValue(
			static_cast<std::int64_t>(ScalarTruth(value)));
	if (IsFloating(target_type))
	{
		long double converted = 0.0L;
		if (value.kind == CONSTEXPR_SCALAR_FLOATING)
			converted = value.floating;
		else if (IsIntegral(source_type, true))
		{
			const std::size_t source_width = IntegralWidth(source_type);
			const ConstexprScalarValue source = NormalizeWideConstant(value,
				source_width, IsUnsignedIntegral(source_type) || source_width == 1);
			const bool negative = !IsUnsignedIntegral(source_type) &&
				source.integral_high >> 63 != 0;
			ConstexprScalarValue magnitude = source;
			if (negative) magnitude = NegateWideConstant(source, 128, true);
			converted = std::ldexp(
				static_cast<long double>(magnitude.integral_high), 64) +
				static_cast<long double>(
					static_cast<std::uint64_t>(magnitude.integral));
			if (negative) converted = -converted;
		}
		else converted = static_cast<long double>(value.integral);
		switch (FundamentalOf(target_type))
		{
		case FUND_FLOAT16:
		case FUND_FLOAT:
		case FUND_FLOAT32:
			converted = static_cast<long double>(
				static_cast<float>(converted));
			break;
		case FUND_DOUBLE:
		case FUND_FLOAT32X:
		case FUND_FLOAT64:
			converted = static_cast<long double>(
				static_cast<double>(converted));
			break;
		case FUND_LONG_DOUBLE:
		case FUND_FLOAT64X:
		case FUND_STDFLOAT128:
		case FUND_FLOAT128: break;
		default: ThrowInternalCompilerError(
			"invalid floating constant target");
		}
		if (!std::isfinite(converted))
			ThrowSemanticError("non-finite floating constant");
		return ConstexprScalarValue(converted);
	}
	if (value.kind == CONSTEXPR_SCALAR_FLOATING)
	{
		if (!std::isfinite(value.floating))
			ThrowSemanticError(
				"non-finite floating to integral conversion");
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
			ThrowSemanticError("floating constant outside integral range");
		const std::int64_t converted = IsUnsignedIntegral(target_type) ?
			static_cast<std::int64_t>(
				static_cast<std::uint64_t>(value.floating)) :
			static_cast<std::int64_t>(value.floating);
		return ConstexprScalarValue(
			NormalizeIntegralConstant(target_type, converted));
	}
	const std::size_t source_width = IntegralWidth(source_type);
	const ConstexprScalarValue source = NormalizeWideConstant(value,
		source_width, IsUnsignedIntegral(source_type) || source_width == 1);
	return NormalizeWideConstant(source, IntegralWidth(target_type),
		IsUnsignedIntegral(target_type));
}

ConstexprScalarValue Analyzer::NormalizeScalarConstant(
	TypeId type, const ConstexprScalarValue& value) const
{
	return ConvertScalarConstant(type, type, value);
}

void Analyzer::SetExpressionScalar(ExpressionInfo* expression,
	const ConstexprScalarValue& value) const
{
	expression->constant = true;
	expression->constexpr_object = kNoConstexprObject;
	expression->constexpr_complete_object = kNoConstexprObject;
	expression->constexpr_address = kNoConstexprAddress;
	expression->floating_constant = value.kind == CONSTEXPR_SCALAR_FLOATING;
	expression->integral_high_valid =
		value.kind == CONSTEXPR_SCALAR_INTEGRAL;
	if (value.kind == CONSTEXPR_SCALAR_MEMBER_POINTER)
		expression->binding = value.member_pointer;
	if (expression->floating_constant)
		expression->floating_value = value.floating;
	else
	{
		expression->value = value.integral;
		expression->integral_high = value.integral_high;
	}
}

void Analyzer::SetExpressionObject(ExpressionInfo* expression,
	std::uint32_t object) const
{
	if (object == kNoConstexprObject || object >= constexpr_objects_.size())
		ThrowInternalCompilerError("invalid constexpr object identity");
	expression->constant = true;
	expression->floating_constant = false;
	expression->constexpr_object = object;
	expression->constexpr_complete_object = object;
	expression->constexpr_address = kNoConstexprAddress;
}

void Analyzer::SetExpressionSubobject(ExpressionInfo* expression,
	std::uint32_t object, std::uint32_t complete_object) const
{
	if (object == kNoConstexprObject || object >= constexpr_objects_.size() ||
		complete_object == kNoConstexprObject ||
		complete_object >= constexpr_objects_.size())
		ThrowInternalCompilerError("invalid constexpr subobject identity");
	expression->constant = true;
	expression->floating_constant = false;
	expression->constexpr_object = object;
	expression->constexpr_complete_object = complete_object;
}

std::uint32_t Analyzer::ExpressionObject(
	const ExpressionInfo& expression) const
{
	if (expression.constexpr_object != kNoConstexprObject)
		return expression.constexpr_object;
	if (expression.node < constexpr_object_by_dump_.size())
		return constexpr_object_by_dump_[expression.node];
	return kNoConstexprObject;
}

std::uint32_t Analyzer::ExpressionCompleteObject(
	const ExpressionInfo& expression) const
{
	if (expression.constexpr_complete_object != kNoConstexprObject)
		return expression.constexpr_complete_object;
	return ExpressionObject(expression);
}

void Analyzer::SetExpressionDumpObject(
	ExpressionInfo* expression) const
{
	const std::uint32_t object = ExpressionObject(*expression);
	if (object == kNoConstexprObject) return;
	const std::uint32_t complete_object =
		ExpressionCompleteObject(*expression);
	if (ExpressionAddress(*expression) != kNoConstexprAddress &&
		complete_object != kNoConstexprObject)
		SetExpressionSubobject(expression, object, complete_object);
	else SetExpressionObject(expression, object);
}

void Analyzer::PublishDumpObject(std::uint32_t node,
	std::uint32_t object)
{
	if (node == kNoDumpEdge || node >= dump_.nodes.size() ||
		object == kNoConstexprObject || object >= constexpr_objects_.size())
		ThrowInternalCompilerError("invalid constexpr dump object publication");
	if (constexpr_object_by_dump_.size() <= node)
		constexpr_object_by_dump_.resize(
			static_cast<std::size_t>(node) + 1, kNoConstexprObject);
	constexpr_object_by_dump_[node] = object;
}

ConstexprScalarValue Analyzer::BindingScalar(BindingId binding) const
{
	if (binding == kNoBinding || binding >= program_->bindings.size())
		ThrowInternalCompilerError("invalid constant binding identity");
	const BindingRecord& record = program_->bindings[binding];
	if (!record.constant)
		ThrowInternalCompilerError("binding has no constant value");
	BindingId owner = binding;
	if (IsMemberPointer(record.type))
	{
		if ((owner >= constexpr_member_pointer_by_binding_.size() ||
			 constexpr_member_pointer_by_binding_[owner] == kNoBinding) &&
			record.canonical != binding)
			owner = record.canonical;
		const BindingId member =
			owner < constexpr_member_pointer_by_binding_.size() ?
			constexpr_member_pointer_by_binding_[owner] : kNoBinding;
		return ConstexprScalarValue(member,
			program_->bindings[owner].value);
	}
	BindingId floating_owner = binding;
	if ((floating_owner >= floating_constant_fact_by_binding_.size() ||
		floating_constant_fact_by_binding_[floating_owner] == 0) &&
		record.canonical != binding)
		floating_owner = record.canonical;
	if (floating_owner < floating_constant_fact_by_binding_.size())
	{
		const std::uint32_t fact =
			floating_constant_fact_by_binding_[floating_owner];
		if (fact != 0)
		{
			if (fact > floating_constant_values_.size())
				ThrowInternalCompilerError("floating constant fact is out of range");
			return ConstexprScalarValue(floating_constant_values_[fact - 1]);
		}
	}
	owner = binding;
	if ((owner >= integral_constant_fact_by_binding_.size() ||
		integral_constant_fact_by_binding_[owner] == 0) &&
		record.canonical != binding &&
		record.canonical < integral_constant_fact_by_binding_.size() &&
		integral_constant_fact_by_binding_[record.canonical] != 0)
		owner = record.canonical;
	const BindingRecord& integral_record = program_->bindings[owner];
	return ConstexprScalarValue::IntegralBits(
		static_cast<std::uint64_t>(integral_record.value),
		owner < integral_constant_fact_by_binding_.size() &&
			integral_constant_fact_by_binding_[owner] != 0 ?
			integral_constant_high_by_binding_[owner] :
			IsUnsignedIntegral(integral_record.type) ? 0 :
			integral_record.value < 0 ? ~std::uint64_t(0) : 0);
}

std::uint32_t Analyzer::BindingObject(BindingId binding) const
{
	if (binding == kNoBinding || binding >= program_->bindings.size())
		ThrowInternalCompilerError("invalid constant binding identity");
	const BindingRecord& record = program_->bindings[binding];
	if (!record.constant)
		ThrowInternalCompilerError("binding has no constant value");
	BindingId owner = binding;
	if ((owner >= constexpr_object_by_binding_.size() ||
		constexpr_object_by_binding_[owner] == kNoConstexprObject) &&
		record.canonical != binding)
		owner = record.canonical;
	if (owner >= constexpr_object_by_binding_.size() ||
		constexpr_object_by_binding_[owner] == kNoConstexprObject)
		return kNoConstexprObject;
	const std::uint32_t object = constexpr_object_by_binding_[owner];
	if (object >= constexpr_objects_.size())
		ThrowInternalCompilerError("constexpr object fact is out of range");
	return object;
}

void Analyzer::PublishBindingScalar(BindingId binding,
	const ConstexprScalarValue& value)
{
	if (binding == kNoBinding || binding >= program_->bindings.size())
		ThrowInternalCompilerError("invalid constant binding publication");
	BindingRecord& record = program_->bindings[binding];
	record.constant = true;
	if (value.kind == CONSTEXPR_SCALAR_MEMBER_POINTER)
	{
		record.value = value.integral;
		if (constexpr_member_pointer_by_binding_.size() <= binding)
			constexpr_member_pointer_by_binding_.resize(
				static_cast<std::size_t>(binding) + 1, kNoBinding);
		constexpr_member_pointer_by_binding_[binding] = value.member_pointer;
		return;
	}
	if (value.kind == CONSTEXPR_SCALAR_INTEGRAL)
	{
		record.value = value.integral;
		if (integral_constant_high_by_binding_.size() <= binding)
			integral_constant_high_by_binding_.resize(
				static_cast<std::size_t>(binding) + 1, 0);
		if (integral_constant_fact_by_binding_.size() <= binding)
			integral_constant_fact_by_binding_.resize(
				static_cast<std::size_t>(binding) + 1, 0);
		integral_constant_high_by_binding_[binding] = value.integral_high;
		integral_constant_fact_by_binding_[binding] = 1;
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
			ThrowSemanticResourceLimit("too many floating constant facts");
		floating_constant_values_.push_back(value.floating);
		fact = static_cast<std::uint32_t>(floating_constant_values_.size());
	}
	else
	{
		if (fact > floating_constant_values_.size())
			ThrowInternalCompilerError("floating constant fact is out of range");
		floating_constant_values_[fact - 1] = value.floating;
	}
}

void Analyzer::PublishBindingObject(BindingId binding,
	std::uint32_t object)
{
	if (binding == kNoBinding || binding >= program_->bindings.size())
		ThrowInternalCompilerError("invalid constant binding publication");
	if (object == kNoConstexprObject || object >= constexpr_objects_.size())
		ThrowInternalCompilerError("invalid constexpr object publication");
	program_->bindings[binding].constant = true;
	if (constexpr_object_by_binding_.size() <= binding)
		constexpr_object_by_binding_.resize(
			static_cast<std::size_t>(binding) + 1, kNoConstexprObject);
	constexpr_object_by_binding_[binding] = object;
}

void Analyzer::PublishBindingConstant(BindingId binding,
	const ExpressionInfo& value)
{
	if (!value.constant)
		ThrowInternalCompilerError("publishing a nonconstant expression");
	const std::uint32_t object = ExpressionObject(value);
	const std::uint32_t address = ExpressionAddress(value);
	if (address != kNoConstexprAddress)
		PublishBindingAddress(binding, address);
	else if (object != kNoConstexprObject)
		PublishBindingObject(binding, object);
	else PublishBindingScalar(binding, ExpressionScalar(value));
}

void Analyzer::PublishCanonicalBindingConstant(BindingId binding)
{
	if (binding == kNoBinding || binding >= program_->bindings.size() ||
		!program_->bindings[binding].constant) return;
	const BindingId canonical = program_->bindings[binding].canonical;
	const std::uint32_t address = BindingAddress(binding);
	const std::uint32_t object = BindingObject(binding);
	if (address != kNoConstexprAddress)
		PublishBindingAddress(canonical, address);
	else if (object != kNoConstexprObject)
		PublishBindingObject(canonical, object);
	else PublishBindingScalar(canonical, BindingScalar(binding));
}

ExpressionInfo Analyzer::AnalyzeConstantRequiredExpression(
	NodeId node, ScopeId scope, TypeId type, bool required)
{
	ScopedCounterIncrement required_context(
		&constant_expression_required_depth_, required);
	return AnalyzeExpression(node, scope, type);
}

void Analyzer::SetExpressionBindingConstant(
	ExpressionInfo* expression, BindingId binding) const
{
	const std::uint32_t address = BindingAddress(binding);
	const std::uint32_t object = BindingObject(binding);
	if (address != kNoConstexprAddress)
	{
		if (program_->types.IsReference(program_->bindings[binding].type))
			SetExpressionLvalueAddress(expression, address);
		else SetExpressionAddress(expression, address);
	}
	else if (object != kNoConstexprObject) SetExpressionObject(expression, object);
	else SetExpressionScalar(expression, BindingScalar(binding));
}

bool Analyzer::BuildConstexprObjectElement(TypeId type,
	BindingId member, const ExpressionInfo& value,
	ConstexprObjectElement* result) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	if (IsPointer(type) || program_->types.IsReference(type))
	{
		const std::uint32_t address = program_->types.IsReference(type) ?
			value.constexpr_lvalue_address : ExpressionAddress(value);
		if (address == kNoConstexprAddress) return false;
		*result = ConstexprObjectElement(member, address, true);
		return true;
	}
	if (IsIntegral(type, true) || IsFloating(type) || IsMemberPointer(type))
	{
		ConstexprScalarValue scalar = IsMemberPointer(type) ?
			ConstexprScalarValue(kNoBinding, static_cast<std::int64_t>(0)) :
			ConstexprScalarValue(static_cast<std::int64_t>(0));
		if (value.node != kNoDumpEdge)
		{
			if (!value.constant ||
				value.constexpr_object != kNoConstexprObject) return false;
			scalar = ConvertScalarConstant(
				value.type, type, ExpressionScalar(value));
		}
		else scalar = NormalizeScalarConstant(type, scalar);
		*result = ConstexprObjectElement(member, scalar);
		return true;
	}
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind != TYPE_ARRAY &&
		!IsConstexprClassEntity(*program_, EntityOf(type))) return false;
	const std::uint32_t object = ExpressionObject(value);
	if (object == kNoConstexprObject) return false;
	*result = ConstexprObjectElement(member, object);
	return true;
}

std::uint32_t Analyzer::InternConstexprObject(TypeId type,
	const std::vector<ConstexprObjectElement>& elements)
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	std::size_t hash = std::hash<TypeId>()(type);
	std::uint64_t newest_local_storage_identity = 0;
	for (std::size_t i = 0; i < elements.size(); ++i)
	{
		const ConstexprObjectElement& element = elements[i];
		hash = MixConstexprHash(hash, std::hash<BindingId>()(element.member));
		hash = MixConstexprHash(hash,
			std::hash<bool>()(element.object_value));
		hash = MixConstexprHash(hash,
			std::hash<bool>()(element.address_value));
		if (element.object_value)
		{
			hash = MixConstexprHash(hash,
				std::hash<std::uint32_t>()(element.object));
			if (element.object >= constexpr_objects_.size())
				ThrowInternalCompilerError("invalid nested constexpr object identity");
			newest_local_storage_identity = std::max(
				newest_local_storage_identity,
				constexpr_objects_[element.object].newest_local_storage_identity);
		}
		else if (element.address_value)
		{
			hash = MixConstexprHash(hash,
				std::hash<std::uint32_t>()(element.address));
			const ConstexprAddressValue* address =
				ConstexprAddressAt(element.address);
			if (!address)
				ThrowInternalCompilerError("invalid constexpr object address identity");
			if (address->kind == CONSTEXPR_ADDRESS_LOCAL)
				newest_local_storage_identity = std::max(
					newest_local_storage_identity, address->identity);
		}
		else
		{
			hash = MixConstexprHash(hash,
				std::hash<int>()(static_cast<int>(element.scalar.kind)));
				hash = MixConstexprHash(hash,
					element.scalar.kind == CONSTEXPR_SCALAR_FLOATING ?
					std::hash<long double>()(element.scalar.floating) :
					std::hash<std::int64_t>()(element.scalar.integral));
			if (element.scalar.kind != CONSTEXPR_SCALAR_FLOATING)
				hash = MixConstexprHash(hash,
					std::hash<std::uint64_t>()(element.scalar.integral_high));
			if (element.scalar.kind == CONSTEXPR_SCALAR_MEMBER_POINTER)
				hash = MixConstexprHash(hash, std::hash<BindingId>()(
					element.scalar.member_pointer));
		}
	}
	const std::pair<std::unordered_multimap<std::size_t,
		std::uint32_t>::const_iterator,
		std::unordered_multimap<std::size_t, std::uint32_t>::const_iterator>
		range = constexpr_object_index_.equal_range(hash);
	for (std::unordered_multimap<std::size_t,
		std::uint32_t>::const_iterator candidate = range.first;
		candidate != range.second; ++candidate)
	{
		const ConstexprObjectValue& object =
			constexpr_objects_[candidate->second];
		if (object.type != type || object.element_count != elements.size())
			continue;
		bool equal = true;
		for (std::size_t i = 0; i < elements.size(); ++i)
			if (!(constexpr_object_elements_[object.first_element + i] ==
				elements[i]))
			{
				equal = false;
				break;
			}
		if (equal) return candidate->second;
	}
	if (elements.size() > kNoConstexprObject ||
		constexpr_objects_.size() >= kNoConstexprObject ||
		constexpr_object_elements_.size() > kNoConstexprObject - elements.size())
		ThrowSemanticResourceLimit("too many constexpr object facts");
	const std::uint32_t first = static_cast<std::uint32_t>(
		constexpr_object_elements_.size());
	const std::uint32_t count = static_cast<std::uint32_t>(elements.size());
	constexpr_object_elements_.insert(
		constexpr_object_elements_.end(), elements.begin(), elements.end());
	const std::uint32_t result = static_cast<std::uint32_t>(
		constexpr_objects_.size());
	constexpr_objects_.push_back(
		ConstexprObjectValue(type, first, count, hash,
			newest_local_storage_identity));
	constexpr_object_index_.insert(std::make_pair(hash, result));
	return result;
}

const ConstexprObjectElement* Analyzer::ConstexprObjectElementAt(
	std::uint32_t object, std::size_t ordinal) const
{
	if (object == kNoConstexprObject || object >= constexpr_objects_.size())
		return 0;
	const ConstexprObjectValue& value = constexpr_objects_[object];
	if (ordinal >= value.element_count) return 0;
	return &constexpr_object_elements_[value.first_element + ordinal];
}

std::uint32_t Analyzer::ProjectConstexprObject(
	std::uint32_t object, TypeId target, std::uint64_t* byte_offset) const
{
	++constexpr_object_projection_visits_;
	if (object == kNoConstexprObject || object >= constexpr_objects_.size())
		return kNoConstexprObject;
	target = program_->types.RemoveTopCv(EffectiveType(target));
	const ConstexprObjectValue& value = constexpr_objects_[object];
	if (value.type == target)
	{
		if (byte_offset) *byte_offset = 0;
		return object;
	}
	const EntityId source_entity = EntityOf(value.type);
	const EntityId target_entity = EntityOf(target);
	if (source_entity == kNoEntity || target_entity == kNoEntity ||
		source_entity >= entity_data_members_.size())
		return kNoConstexprObject;
	const std::size_t member_count =
		entity_data_members_[source_entity].size();
	const std::size_t base_count =
		program_->entities[source_entity].direct_base_count;
	if (member_count > value.element_count ||
		base_count > value.element_count - member_count)
		return kNoConstexprObject;
	std::uint32_t result = kNoConstexprObject;
	std::uint64_t result_offset = 0;
	for (std::size_t i = 0; i < base_count; ++i)
	{
		const ConstexprObjectElement& element = constexpr_object_elements_[
			value.first_element + member_count + i];
		if (!element.object_value) return kNoConstexprObject;
		std::uint64_t nested_offset = 0;
		const std::uint32_t projected =
			ProjectConstexprObject(element.object, target, &nested_offset);
		if (projected == kNoConstexprObject) continue;
		if (result != kNoConstexprObject) return kNoConstexprObject;
		result = projected;
		const std::uint64_t direct_offset =
			program_->DirectBase(source_entity, i).offset;
		if (nested_offset > std::numeric_limits<std::uint64_t>::max() -
			direct_offset) return kNoConstexprObject;
		result_offset = direct_offset + nested_offset;
	}
	if (result != kNoConstexprObject && byte_offset)
		*byte_offset = result_offset;
	return result;
}

const ConstexprObjectElement* Analyzer::ConstexprClassMemberAt(
	std::uint32_t object, BindingId member) const
{
	if (member == kNoBinding || member >= program_->bindings.size()) return 0;
	const BindingRecord& binding = program_->bindings[member];
	if (!binding.non_static_data_member || binding.member_owner == kNoEntity)
		return 0;
	object = ProjectConstexprObject(
		object, program_->entities[binding.member_owner].type);
	if (object == kNoConstexprObject) return 0;
	if (program_->entities[binding.member_owner].flavor == NAMED_UNION)
	{
		const ConstexprObjectValue& value = constexpr_objects_[object];
		for (std::size_t i = 0; i < value.element_count; ++i)
		{
			const ConstexprObjectElement& element =
				constexpr_object_elements_[value.first_element + i];
			if (element.member == member) return &element;
		}
		return 0;
	}
	const ConstexprObjectElement* element =
		ConstexprObjectElementAt(
			object, program_->BindingLayout(binding).member_ordinal);
	return element && element->member == member ? element : 0;
}

void Analyzer::SetExpressionObjectElement(ExpressionInfo* expression,
	const ConstexprObjectElement& element) const
{
	if (element.object_value) SetExpressionObject(expression, element.object);
	else if (element.address_value)
	{
		if (program_->types.IsReference(expression->type))
			SetExpressionLvalueAddress(expression, element.address);
		else SetExpressionAddress(expression, element.address);
	}
	else SetExpressionScalar(expression, element.scalar);
}

ExpressionInfo Analyzer::MaterializeConstexprObjectElement(
	const ConstexprObjectElement& element, TypeId type)
{
	if (element.object_value)
		return MaterializeConstexprObject(element.object, type);
	if (element.address_value)
		return MaterializeConstexprAddress(element.address, type);
	ExpressionInfo result = MakeLiteral(type, InternScalar(type, element.scalar));
	SetExpressionScalar(&result, element.scalar);
	RecordExpressionFacts(result);
	return result;
}

ExpressionInfo Analyzer::MaterializeConstexprObject(
	std::uint32_t object, TypeId type)
{
	if (object == kNoConstexprObject || object >= constexpr_objects_.size())
		ThrowInternalCompilerError("invalid constexpr object materialization");
	const ConstexprObjectValue& value = constexpr_objects_[object];
	TypeId unqualified = program_->types.RemoveTopCv(EffectiveType(type));
	if (value.type != unqualified)
	{
		const TypeRecord& requested = program_->types.Get(unqualified);
		const TypeRecord& completed = program_->types.Get(value.type);
		if (!requested.IsIncompleteArray() ||
			completed.kind != TYPE_ARRAY ||
			requested.child != completed.child)
			ThrowInternalCompilerError(
				"constexpr object materialization type mismatch");
		type = value.type;
		unqualified = value.type;
	}
	const TypeRecord& record = program_->types.Get(unqualified);
	const std::uint32_t list = MakeDump(
		DUMP_BRACED_INIT_LIST, type, VALUE_LVALUE);
	std::size_t materialized_count = value.element_count;
	if (record.kind != TYPE_ARRAY)
	{
		const EntityId entity = EntityOf(unqualified);
		const bool union_object = entity != kNoEntity &&
			program_->entities[entity].flavor == NAMED_UNION;
		if (entity == kNoEntity || entity >= entity_data_members_.size() ||
			(!union_object &&
			 entity_data_members_[entity].size() > value.element_count))
			ThrowInternalCompilerError(
				"constexpr class object has an invalid direct-member range");
		materialized_count = union_object ? value.element_count :
			entity_data_members_[entity].size();
	}
	for (std::size_t i = 0; i < materialized_count; ++i)
	{
		const ConstexprObjectElement& element =
			constexpr_object_elements_[value.first_element + i];
		if (record.kind == TYPE_ARRAY)
		{
			const ExpressionInfo child =
				MaterializeConstexprObjectElement(element, record.child);
			dump_.Add(list, child.node);
		}
		else
		{
			if (element.member == kNoBinding ||
				element.member >= program_->bindings.size())
				ThrowInternalCompilerError(
					"constexpr class object has no member identity");
			const BindingRecord& member = program_->bindings[element.member];
			const std::uint32_t action = MakeDump(DUMP_INITIALIZER_ACTION,
				member.type, VALUE_NONE, member.name, element.member);
			const ExpressionInfo child =
				MaterializeConstexprObjectElement(element, member.type);
			dump_.Add(action, child.node);
			dump_.Add(list, action);
			++expression_count_;
		}
	}
	ExpressionInfo result;
	result.node = list;
	result.type = type;
	result.category = VALUE_LVALUE;
	SetExpressionObject(&result, object);
	RecordExpressionFacts(result);
	++expression_count_;
	return result;
}

bool Analyzer::ScalarTruth(const ConstexprScalarValue& value) const
{
	if (value.kind == CONSTEXPR_SCALAR_MEMBER_POINTER)
		return value.member_pointer != kNoBinding || value.integral != 0;
	return value.kind == CONSTEXPR_SCALAR_FLOATING ?
		value.floating != 0.0L :
		value.integral != 0 || value.integral_high != 0;
}

ConstexprScalarValue Analyzer::ApplyConstantScalarBinary(
	const std::string& operation, const ConstexprScalarValue& left,
	const ConstexprScalarValue& right, TypeId operand_type) const
{
	const int op = ClassifyOperationSpelling(operation);
	if (op == OP_LAND)
		return ConstexprScalarValue(static_cast<std::int64_t>(
			ScalarTruth(left) && ScalarTruth(right)));
	if (op == OP_LOR)
		return ConstexprScalarValue(static_cast<std::int64_t>(
			ScalarTruth(left) || ScalarTruth(right)));
	if (op == OP_COMMA) return right;
	if (left.kind == CONSTEXPR_SCALAR_MEMBER_POINTER ||
		right.kind == CONSTEXPR_SCALAR_MEMBER_POINTER)
	{
		if (left.kind != CONSTEXPR_SCALAR_MEMBER_POINTER ||
			right.kind != CONSTEXPR_SCALAR_MEMBER_POINTER ||
			(op != OP_EQ && op != OP_NE))
			ThrowInternalCompilerError(
				"unsupported member pointer constant operator");
		const bool equal = left == right;
		return ConstexprScalarValue(static_cast<std::int64_t>(
			op == OP_EQ ? equal : !equal));
	}
	if (operand_type != kNoType && IsFloating(operand_type))
	{
		if (left.kind != CONSTEXPR_SCALAR_FLOATING ||
			right.kind != CONSTEXPR_SCALAR_FLOATING)
			ThrowInternalCompilerError(
				"unnormalized floating constant operands");
		const long double l = left.floating;
		const long double r = right.floating;
		if (op == OP_EQ || op == OP_NE || op == OP_LT ||
			op == OP_GT || op == OP_LE || op == OP_GE)
		{
			const bool compared = op == OP_EQ ? l == r :
				op == OP_NE ? l != r : op == OP_LT ? l < r :
				op == OP_GT ? l > r : op == OP_LE ? l <= r : l >= r;
			return ConstexprScalarValue(static_cast<std::int64_t>(compared));
		}
		if (op == OP_DIV && r == 0.0L)
			ThrowSemanticError("floating constant division by zero");
		long double calculated = 0.0L;
		if (op == OP_PLUS) calculated = l + r;
		else if (op == OP_MINUS) calculated = l - r;
		else if (op == OP_STAR) calculated = l * r;
		else if (op == OP_DIV) calculated = l / r;
		else ThrowInternalCompilerError(
			"unsupported floating constant operator");
		return NormalizeScalarConstant(
			operand_type, ConstexprScalarValue(calculated));
	}
	if (operand_type != kNoType && IsIntegral(operand_type, true) &&
		IntegralWidth(operand_type) > 64)
		return ApplyWideConstantBinary(operation, left, right,
			IntegralWidth(operand_type), IsUnsignedIntegral(operand_type));
	return ConstexprScalarValue(ApplyConstantBinary(operation,
		left.integral, right.integral, operand_type));
}

NameId Analyzer::InternScalar(TypeId type,
	const ConstexprScalarValue& value)
{
	if (value.kind != CONSTEXPR_SCALAR_FLOATING)
	{
		if (!IsIntegral(type, true) || IntegralWidth(type) <= 64)
			return InternNumber(value.integral);
		std::ostringstream spelling;
		spelling << "0x" << std::hex << std::setfill('0')
			<< std::setw(16) << value.integral_high
			<< std::setw(16)
			<< static_cast<std::uint64_t>(value.integral);
		return program_->names.Intern(spelling.str());
	}
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

bool Analyzer::ConsumeConstexprStep()
{
	if (constexpr_evaluation_steps_ >= kMaxConstexprSteps) return false;
	++constexpr_evaluation_steps_;
	++constexpr_step_visits_;
	return true;
}

void Analyzer::PushConstexprBlock()
{
	if (constexpr_frames_.empty())
		ThrowInternalCompilerError("constexpr block has no invocation frame");
	constexpr_block_offsets_.push_back(ConstexprBlockOffset(
		constexpr_locals_.size(), constexpr_scope_facts_.size()));
}

void Analyzer::PopConstexprBlock()
{
	if (constexpr_frames_.empty() ||
		constexpr_block_offsets_.size() <= constexpr_frames_.back().first_block)
		ThrowInternalCompilerError("constexpr block stack is unbalanced");
	ReleaseConstexprLocals(
		constexpr_block_offsets_.back().first_local);
	ReleaseConstexprScopeFacts(
		constexpr_block_offsets_.back().first_scope_fact);
	constexpr_block_offsets_.pop_back();
}

void Analyzer::ReleaseConstexprLocals(std::size_t first)
{
	if (first > constexpr_locals_.size())
		ThrowInternalCompilerError("constexpr local release is out of range");
	while (constexpr_locals_.size() > first)
	{
		const std::size_t index = constexpr_locals_.size() - 1;
		const ConstexprLocalValue& value = constexpr_locals_.back();
		if (value.name != 0)
		{
			if (value.name >= constexpr_local_by_name_.size() ||
				constexpr_local_by_name_[value.name] != index)
				ThrowInternalCompilerError(
					"constexpr local name index is unbalanced");
			constexpr_local_by_name_[value.name] = value.previous_same_name;
		}
		if (value.pack_name != 0)
		{
			if (value.pack_name >= constexpr_local_by_pack_.size() ||
				constexpr_local_by_pack_[value.pack_name] != index)
				ThrowInternalCompilerError(
					"constexpr local pack index is unbalanced");
			constexpr_local_by_pack_[value.pack_name] =
				value.previous_same_pack;
		}
		constexpr_locals_.pop_back();
	}
}

void Analyzer::ReleaseConstexprScopeFacts(std::size_t first)
{
	if (first > constexpr_scope_facts_.size())
		ThrowInternalCompilerError("constexpr scope fact release is out of range");
	while (constexpr_scope_facts_.size() > first)
	{
		const std::size_t index = constexpr_scope_facts_.size() - 1;
		const ConstexprScopeFact& fact = constexpr_scope_facts_.back();
		if (fact.name != 0)
		{
			if (fact.name >= constexpr_type_alias_by_name_.size() ||
				constexpr_type_alias_by_name_[fact.name] != index)
				ThrowInternalCompilerError(
					"constexpr type alias index is unbalanced");
			constexpr_type_alias_by_name_[fact.name] =
				fact.previous_same_name;
		}
		constexpr_scope_facts_.pop_back();
	}
}

bool Analyzer::AddConstexprLocalValue(ConstexprLocalValue value,
	std::size_t* local)
{
	if (constexpr_frames_.empty()) return false;
	const ConstexprFrame& frame = constexpr_frames_.back();
	const std::size_t first =
		constexpr_block_offsets_.size() > frame.first_block ?
		constexpr_block_offsets_.back().first_local : frame.first_local;
	if (value.name != 0)
	{
		if (constexpr_local_by_name_.size() <= value.name)
			constexpr_local_by_name_.resize(
				static_cast<std::size_t>(value.name) + 1,
				kNoConstexprLocal);
		++constexpr_local_index_probes_;
		const std::size_t prior = constexpr_local_by_name_[value.name];
		if (prior != kNoConstexprLocal && prior >= first &&
			(value.pack_name == 0 ||
			 constexpr_locals_[prior].pack_name != value.pack_name))
			return false;
		value.previous_same_name = prior;
	}
	if (value.pack_name != 0)
	{
		if (constexpr_local_by_pack_.size() <= value.pack_name)
			constexpr_local_by_pack_.resize(
				static_cast<std::size_t>(value.pack_name) + 1,
				kNoConstexprLocal);
		value.previous_same_pack =
			constexpr_local_by_pack_[value.pack_name];
	}
	const std::size_t index = constexpr_locals_.size();
	if (local) *local = index;
	constexpr_locals_.push_back(value);
	if (value.name != 0) constexpr_local_by_name_[value.name] = index;
	if (value.pack_name != 0)
		constexpr_local_by_pack_[value.pack_name] = index;
	if (constexpr_locals_.size() > constexpr_peak_locals_)
		constexpr_peak_locals_ = constexpr_locals_.size();
	return true;
}

bool Analyzer::AddConstexprLocal(NameId name, NameId pack_name,
	TypeId type, const ConstexprScalarValue& value, std::size_t* local)
{
	return AddConstexprLocalValue(ConstexprLocalValue(name, pack_name, type,
		NormalizeScalarConstant(type, value)), local);
}

bool Analyzer::AddConstexprLocal(NameId name, NameId pack_name,
	TypeId type, std::uint32_t object, std::size_t* local)
{
	return AddConstexprLocal(
		name, pack_name, type, object, object, local);
}

bool Analyzer::AddConstexprLocal(NameId name, NameId pack_name,
	TypeId type, std::uint32_t object, std::uint32_t complete_object,
	std::size_t* local)
{
	if (constexpr_frames_.empty() || object == kNoConstexprObject ||
		object >= constexpr_objects_.size() ||
		complete_object == kNoConstexprObject ||
		complete_object >= constexpr_objects_.size()) return false;
	ConstexprLocalValue value(name, pack_name, type, object);
	value.complete_object = complete_object;
	return AddConstexprLocalValue(value, local);
}

bool Analyzer::AddConstexprAddressLocal(NameId name, NameId pack_name,
	TypeId type, std::uint32_t address, std::size_t* local)
{
	if (constexpr_frames_.empty() || !ConstexprAddressAt(address)) return false;
	ConstexprLocalValue value(name, pack_name, type,
		ConstexprScalarValue(static_cast<std::int64_t>(0)));
	value.address = address;
	return AddConstexprLocalValue(value, local);
}

bool Analyzer::AddConstexprTypeAlias(NameId name, TypeId type)
{
	if (name == 0 || constexpr_frames_.empty()) return false;
	const ConstexprFrame& frame = constexpr_frames_.back();
	const std::size_t first =
		constexpr_block_offsets_.size() > frame.first_block ?
		constexpr_block_offsets_.back().first_scope_fact :
		frame.first_scope_fact;
	if (constexpr_type_alias_by_name_.size() <= name)
		constexpr_type_alias_by_name_.resize(
			static_cast<std::size_t>(name) + 1, kNoConstexprLocal);
	++constexpr_scope_index_probes_;
	const std::size_t prior = constexpr_type_alias_by_name_[name];
	if (prior != kNoConstexprLocal && prior >= first) return false;
	ConstexprScopeFact fact(name, type, kNoScope);
	fact.previous_same_name = prior;
	const std::size_t index = constexpr_scope_facts_.size();
	constexpr_scope_facts_.push_back(fact);
	constexpr_type_alias_by_name_[name] = index;
	return true;
}

void Analyzer::AddConstexprUsingNamespace(ScopeId name_space)
{
	if (name_space == kNoScope || constexpr_frames_.empty())
		ThrowInternalCompilerError("invalid constexpr using namespace fact");
	constexpr_scope_facts_.push_back(
		ConstexprScopeFact(0, kNoType, name_space));
}

bool Analyzer::FindConstexprTypeAlias(NameId name, TypeId* type) const
{
	if (name == 0 || constexpr_frames_.empty()) return false;
	++constexpr_scope_index_probes_;
	const std::size_t first = constexpr_frames_.back().first_scope_fact;
	if (name >= constexpr_type_alias_by_name_.size() ||
		constexpr_type_alias_by_name_[name] == kNoConstexprLocal ||
		constexpr_type_alias_by_name_[name] < first) return false;
	*type = constexpr_scope_facts_[
		constexpr_type_alias_by_name_[name]].type;
	return true;
}

void Analyzer::FindConstexprUsingNamespaces(
	std::vector<ScopeId>* scopes) const
{
	scopes->clear();
	if (constexpr_frames_.empty()) return;
	const std::size_t first = constexpr_frames_.back().first_scope_fact;
	for (std::size_t i = first; i < constexpr_scope_facts_.size(); ++i)
		if (constexpr_scope_facts_[i].name_space != kNoScope)
			scopes->push_back(constexpr_scope_facts_[i].name_space);
}

bool Analyzer::FindConstexprLocal(NameId name,
	std::size_t* local) const
{
	if (name == 0 || constexpr_frames_.empty()) return false;
	++constexpr_local_index_probes_;
	const std::size_t first = constexpr_frames_.back().first_local;
	if (name >= constexpr_local_by_name_.size() ||
		constexpr_local_by_name_[name] == kNoConstexprLocal ||
		constexpr_local_by_name_[name] < first) return false;
	*local = constexpr_local_by_name_[name];
	return true;
}

bool Analyzer::FindConstexprPack(NameId name,
	std::vector<std::size_t>* locals) const
{
	if (name == 0 || constexpr_frames_.empty()) return false;
	locals->clear();
	++constexpr_local_index_probes_;
	const std::size_t first = constexpr_frames_.back().first_local;
	std::size_t current = name < constexpr_local_by_pack_.size() ?
		constexpr_local_by_pack_[name] : kNoConstexprLocal;
	while (current != kNoConstexprLocal && current >= first)
	{
		locals->push_back(current);
		current = constexpr_locals_[current].previous_same_pack;
	}
	std::reverse(locals->begin(), locals->end());
	if (!locals->empty()) return true;
	return GetFunction(constexpr_frames_.back().function).parameter_pack_name ==
		name;
}

bool Analyzer::TryAnalyzeConstexprLocal(
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
	if (program_->types.IsReference(value.type))
	{
		if (value.object != kNoConstexprObject)
			SetExpressionSubobject(
				result, value.object, value.complete_object);
		else if (IsIntegral(EffectiveType(value.type), true) ||
			IsFloating(EffectiveType(value.type)) ||
			IsMemberPointer(EffectiveType(value.type)))
			SetExpressionScalar(result, value.value);
		if (value.address != kNoConstexprAddress)
			SetExpressionLvalueAddress(result, value.address);
	}
	else if (value.address != kNoConstexprAddress)
	{
		SetExpressionAddress(result, value.address);
		if (value.object != kNoConstexprObject &&
			value.complete_object != kNoConstexprObject)
			SetExpressionSubobject(
				result, value.object, value.complete_object);
	}
	else if (value.object != kNoConstexprObject)
		SetExpressionSubobject(result, value.object, value.complete_object);
	else SetExpressionScalar(result, value.value);
	result->node = MakeDump(DUMP_ID_EXPRESSION, result->type,
		VALUE_LVALUE, name);
	dump_.nodes[result->node].constant = true;
	if (!result->floating_constant &&
		result->constexpr_object == kNoConstexprObject &&
		result->constexpr_address == kNoConstexprAddress)
	{
		dump_.nodes[result->node].constant_value = result->value;
		dump_.nodes[result->node].constant_high =
			ExpressionScalar(*result).integral_high;
	}
	++expression_count_;
	*result = ApplyTarget(*result, target);
	return true;
}

void Analyzer::ReleaseConstexprScratch(
	std::size_t nodes, std::size_t edges)
{
	if (nodes > dump_.nodes.size() || edges > dump_.edges.size())
		ThrowInternalCompilerError("constexpr scratch mark is invalid");
	if (dump_.nodes.size() > constexpr_scratch_peak_nodes_)
		constexpr_scratch_peak_nodes_ = dump_.nodes.size();
	if (constexpr_object_by_dump_.size() > nodes)
		constexpr_object_by_dump_.resize(nodes);
	dump_.nodes.erase(dump_.nodes.begin() + nodes, dump_.nodes.end());
	dump_.edges.erase(dump_.edges.begin() + edges, dump_.edges.end());
}

bool Analyzer::AnalyzeConstexprExpression(NodeId node, ScopeId scope,
	TypeId target, ExpressionInfo* result)
{
	const std::size_t nodes = dump_.nodes.size();
	const std::size_t edges = dump_.edges.size();
	const auto release_scratch = [this, nodes, edges]()
	{
		ReleaseConstexprScratch(nodes, edges);
	};
	ScopedCleanup<decltype(release_scratch)> scratch(release_scratch);
	*result = AnalyzeExpression(node, scope, target);
	SetExpressionDumpObject(result);
	return result->constant ||
		result->constexpr_address != kNoConstexprAddress ||
		result->constexpr_lvalue_address != kNoConstexprAddress;
}

bool Analyzer::AnalyzeConstexprInitializer(NodeId node, ScopeId scope,
	TypeId target, ExpressionInfo* result)
{
	const std::size_t nodes = dump_.nodes.size();
	const std::size_t edges = dump_.edges.size();
	const auto release_scratch = [this, nodes, edges]()
	{
		ReleaseConstexprScratch(nodes, edges);
	};
	ScopedCleanup<decltype(release_scratch)> scratch(release_scratch);
	*result = AnalyzeVariableInitializer(node, scope, target, true);
	SetExpressionDumpObject(result);
	return result->constant ||
		result->constexpr_address != kNoConstexprAddress ||
		result->constexpr_lvalue_address != kNoConstexprAddress;
}

ExpressionInfo Analyzer::AnalyzeConstantAwareVariableInitializer(
	NodeId initializer, ScopeId scope, TypeId type, bool local,
	bool require_constant, bool preserve_recipe)
{
	ScopedCounterIncrement required_expression(
		&constant_expression_required_depth_, require_constant);
	ScopedCounterIncrement required_initializer(
		&constant_initializer_required_depth_, require_constant);
	ScopedCounterIncrement local_initializer(
		&local_constant_initializer_depth_, require_constant && local);
	ScopedCounterIncrement preserved_recipe(
		&preserve_constant_initializer_recipe_depth_, preserve_recipe);
	return AnalyzeVariableInitializer(initializer, scope, type, local);
}

bool Analyzer::ShouldProbeConstantInitialization(bool local,
	const SpecInfo& spec, TypeId type) const
{
	return spec.is_constexpr || !local ||
		(!program_->types.IsReference(type) && IsConst(type) &&
		 IsIntegral(type, true)) ||
		spec.storage_class == STORAGE_CLASS_STATIC;
}

bool Analyzer::HasConstantInitializerFact(
	const ExpressionInfo& initializer) const
{
	return initializer.constant || initializer.floating_constant ||
		initializer.constexpr_object != kNoConstexprObject ||
		initializer.constexpr_address != kNoConstexprAddress ||
		initializer.constexpr_lvalue_address != kNoConstexprAddress;
}

ExpressionInfo Analyzer::AnalyzeInClassStaticInitializer(
	NodeId initializer, ScopeId scope, TypeId type)
{
	// A short-circuited arm is still semantically analyzed and may demand a
	// class specialization while looking up a static constant.  That class's
	// own initializer is an independent constant-evaluation root; inheriting the
	// arm's suppression would incorrectly publish it as nonconstant.
	ScopedValueRestore<std::size_t> suppression(
		&constant_evaluation_suppressed_depth_, 0);
	return AnalyzeConstantAwareVariableInitializer(
		initializer, scope, type, false, true, true);
}

void Analyzer::InheritVariableRedeclarationFacts(BindingId binding)
{
	BindingRecord& declared = program_->bindings[binding];
	BindingRecord& canonical = program_->bindings[declared.canonical];
	if (declared.canonical == binding) return;
	if (canonical.member_owner != kNoEntity)
	{
		declared.member_owner = canonical.member_owner;
		declared.non_static_data_member = canonical.non_static_data_member;
		declared.weak_odr = canonical.weak_odr;
	}
	if (!canonical.constant) return;
	const std::uint32_t address = BindingAddress(declared.canonical);
	const std::uint32_t object = BindingObject(declared.canonical);
	if (address != kNoConstexprAddress)
		PublishBindingAddress(binding, address);
	else if (object != kNoConstexprObject)
		PublishBindingObject(binding, object);
	else PublishBindingScalar(binding, BindingScalar(declared.canonical));
}

ExpressionInfo Analyzer::FinalizeVariableInitializer(
	ExpressionInfo initializer, TypeId type, EntityId class_entity, bool local)
{
	SetExpressionDumpObject(&initializer);
	const std::uint32_t constant_object = ExpressionObject(initializer);
	bool user_constexpr_constructor = false;
	if (constant_object != kNoConstexprObject && class_entity != kNoEntity &&
		class_entity < entity_constructors_.size())
		for (std::size_t i = 0;
			i < entity_constructors_[class_entity].size(); ++i)
		{
			const BindingId candidate = entity_constructors_[class_entity][i];
			const FunctionInfo& constructor = GetFunction(candidate);
			if (constructor.defaulted_constructor &&
				!constructor.implicit_constructor &&
				constructor.parameters.empty() &&
				constexpr_evaluation_depth_ == 0 &&
				constant_expression_required_depth_ != 0 &&
				preserve_constant_initializer_recipe_depth_ == 0)
				DemandFunction(candidate);
			if (constructor.constexpr_function &&
				!constructor.defaulted_constructor &&
				!constructor.implicit_constructor &&
				!constructor.defaulted_special_member &&
				!constructor.implicit_special_member &&
				program_->entities[class_entity].direct_base_count == 0)
				user_constexpr_constructor = true;
		}
	if (constant_expression_required_depth_ != 0 &&
		constant_object != kNoConstexprObject && user_constexpr_constructor &&
		preserve_constant_initializer_recipe_depth_ == 0 &&
		dump_.nodes[initializer.node].kind != DUMP_CLASS_VALUE_TRANSFER)
		initializer = MaterializeConstexprObject(constant_object, type);
	ExpressionInfo result = local ?
		BuildLocalAggregateArrayActions(initializer) : initializer;
	SetExpressionDumpObject(&result);
	return result;
}

ExpressionInfo Analyzer::AnalyzeDefaultConstexprObjectInitializer(
	TypeId type, ScopeId scope, bool local)
{
	ExpressionInfo initializer;
	{
		ScopedCounterIncrement required_expression(
			&constant_expression_required_depth_);
		ScopedCounterIncrement required_initializer(
			&constant_initializer_required_depth_);
		ScopedCounterIncrement local_initializer(
			&local_constant_initializer_depth_, local);
		initializer.node = BuildDefaultConstructorAction(type, scope);
		initializer.type = type;
		initializer.category = VALUE_NONE;
		SetExpressionDumpObject(&initializer);
	}
	if (!initializer.constant)
		ThrowSemanticError(
			"constexpr object default initializer is not constant");
	if (!local)
		initializer = MaterializeConstexprObject(
			ExpressionObject(initializer), type);
	return initializer;
}

void Analyzer::RecordStaticConstantInitializer(
	BindingId binding, std::uint32_t initializer)
{
	const BindingRecord& published_binding = program_->bindings[binding];
	if (published_binding.member_owner == kNoEntity ||
		published_binding.non_static_data_member || initializer == kNoDumpEdge)
		return;
	const BindingId canonical = published_binding.canonical;
	StaticConstantInitializerFact& fact =
		EnsureStaticConstantInitializer(canonical);
	if (fact.initializer != kNoDumpEdge) return;

	std::vector<std::uint32_t> pending(1, initializer);
	while (!pending.empty())
	{
		const std::uint32_t node = pending.back();
		pending.pop_back();
		++static_constant_initializer_visits_;
		if (node >= dump_.nodes.size())
			ThrowInternalCompilerError(
				"static constant initializer is out of range");
		const DumpNode& record = dump_.nodes[node];
		// Scalar constants published on the binding are consumed directly by
		// static lowering. A merely constant pointer recipe still names the
		// function whose address its demanded storage must emit.
		if (record.constant && (IsIntegral(record.type, true) ||
			IsFloating(record.type) || IsNullptr(record.type) ||
			IsMemberPointer(record.type)))
			continue;

		BindingId dependency = kNoBinding;
		if ((record.kind == DUMP_CONSTRUCTOR_ACTION ||
			record.kind == DUMP_CALL_EXPRESSION) &&
			record.binding != kNoBinding)
			dependency = program_->bindings[record.binding].canonical;
		else if (record.kind == DUMP_ID_EXPRESSION &&
			record.binding != kNoBinding &&
			program_->bindings[record.binding].kind == BIND_FUNCTION)
			dependency = program_->bindings[record.binding].canonical;
		else if (record.kind == DUMP_CLASS_VALUE_TRANSFER &&
			record.selected_binding != kNoBinding)
			dependency = program_->bindings[record.selected_binding].canonical;
		if (dependency != kNoBinding)
		{
			if (static_constant_dependency_owner_marks_.size() <= dependency)
				static_constant_dependency_owner_marks_.resize(
					static_cast<std::size_t>(dependency) + 1, kNoBinding);
			if (static_constant_dependency_owner_marks_[dependency] != canonical)
			{
				static_constant_dependency_owner_marks_[dependency] = canonical;
				fact.function_dependencies.push_back(dependency);
				++static_constant_dependency_edges_;
			}
		}
		for (std::uint32_t edge = record.first_edge;
			edge != kNoDumpEdge; edge = dump_.edges[edge].next)
			pending.push_back(dump_.edges[edge].child);
	}
	fact.initializer = initializer;
}

void Analyzer::DemandStaticConstantInitializerDependencies(
	BindingId member)
{
	member = program_->bindings[member].canonical;
	const StaticConstantInitializerFact* fact =
		FindStaticConstantInitializer(member);
	if (!fact) return;
	const std::vector<BindingId>& dependencies = fact->function_dependencies;
	for (std::size_t i = 0; i < dependencies.size(); ++i)
		if (!GetFunction(dependencies[i]).trivial_special_member)
			DemandFunction(dependencies[i]);
}

void Analyzer::PublishStaticConstantEvaluationStats() const
{
	stats_->static_constant_initializer_visits =
		static_constant_initializer_visits_;
	stats_->static_constant_dependency_edges =
		static_constant_dependency_edges_;
}

bool Analyzer::ShouldDemandResolvedCall(BindingId binding,
	bool folded, bool compile_time_only) const
{
	if (constant_evaluation_suppressed_depth_ != 0) return false;
	if (constexpr_evaluation_depth_ != 0) return false;
	if (!folded && !compile_time_only) return true;
	if (preserve_constant_initializer_recipe_depth_ == 0) return false;
	const BindingRecord& record = program_->bindings[binding];
	return record.static_member_function &&
		!GetFunction(binding).definition_in_class;
}

void Analyzer::PublishConstantVariableInitializer(BindingId binding,
	TypeId type, const SpecInfo& spec, const ExpressionInfo& initializer)
{
	program_->bindings[binding].template_parameter_constant =
		dump_.nodes[initializer.node].template_parameter_constant;
	program_->bindings[program_->bindings[binding].canonical].
		template_parameter_constant =
		program_->bindings[binding].template_parameter_constant;
	RecordStaticConstantInitializer(binding, initializer.node);
	const TypeId object_type_id = program_->types.RemoveTopCv(
		EffectiveType(type));
	const TypeRecord& object_type = program_->types.Get(object_type_id);
	const bool aggregate_object_type = object_type.kind == TYPE_ARRAY ||
		IsConstexprClassEntity(*program_, EntityOf(object_type_id));
	const std::uint32_t initializer_object = ExpressionObject(initializer);
	const std::uint32_t initializer_address = program_->types.IsReference(type) ?
		initializer.constexpr_lvalue_address : ExpressionAddress(initializer);
	if (initializer_address != kNoConstexprAddress &&
		!program_->IsStaticDataMember(binding))
	{
		const ConstexprAddressValue* address =
			ConstexprAddressAt(initializer_address);
		if (address && address->kind == CONSTEXPR_ADDRESS_FUNCTION)
		{
			if (address->identity >= program_->bindings.size())
				ThrowInternalCompilerError(
					"constant function address has invalid binding identity");
			DemandFunction(static_cast<BindingId>(address->identity));
		}
	}
	if (spec.is_constexpr &&
		(IsPointer(EffectiveType(type)) || program_->types.IsReference(type)) &&
		initializer_address == kNoConstexprAddress)
		ThrowSemanticError(
			"constexpr pointer/reference initializer is not constant");
	if (spec.is_constexpr && !program_->types.IsReference(type) &&
		(IsIntegral(type, true) || IsFloating(type) || IsMemberPointer(type)) &&
		!initializer.constant)
		ThrowSemanticError(
			"constexpr scalar initializer is not constant");
	if (spec.is_constexpr && !program_->types.IsReference(type) &&
		aggregate_object_type &&
		initializer_object == kNoConstexprObject)
		ThrowSemanticError(
			"constexpr object initializer is not constant");
	if (!initializer.constant ||
		(!spec.is_constexpr &&
		 !(IsConst(type) && (IsIntegral(type, true) || IsFloating(type) ||
			IsMemberPointer(type))) &&
		 !(constexpr_evaluation_depth_ != 0 &&
			(IsIntegral(type, true) || IsFloating(type) ||
			 IsMemberPointer(type)))))
		return;
	const bool scalar_type = IsIntegral(type, true) || IsFloating(type) ||
		IsMemberPointer(type);
	if (initializer_address != kNoConstexprAddress)
	{
		PublishBindingAddress(binding, initializer_address);
		const bool immediate_storage =
			!program_->IsStaticDataMember(binding);
		if (immediate_storage &&
			initializer.node != kNoDumpEdge &&
			dump_.nodes[initializer.node].kind == DUMP_CALL_EXPRESSION &&
			initializer.binding != kNoBinding)
			DemandFunction(initializer.binding);
		return;
	}
	if (aggregate_object_type)
	{
		PublishBindingObject(binding, initializer_object);
		if (spec.is_constexpr && initializer.node != kNoDumpEdge &&
			dump_.nodes[initializer.node].kind != DUMP_CONSTRUCTOR_ACTION)
			dump_.nodes[initializer.node].type = type;
		return;
	}
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

bool Analyzer::EvaluateConstexprDeclaration(NodeId node, ScopeId scope)
{
	if (!ConsumeConstexprStep()) return false;
	const std::size_t nodes = dump_.nodes.size();
	const std::size_t edges = dump_.edges.size();
	bool valid = false;
	const auto release_scratch = [this, nodes, edges]()
	{
		ReleaseConstexprScratch(nodes, edges);
	};
	ScopedCleanup<decltype(release_scratch)> scratch(release_scratch);
	{
		if (arena_->IsTag(node, ::cppgm::syntax::STAG_ALIAS_DECLARATION))
		{
			const TypeId type = BuildTypeId(FindChild(node, ::cppgm::syntax::STAG_TYPE_ID), scope);
			valid = AddConstexprTypeAlias(
				program_->names.UseInterned(arena_->PayloadId(node)), type);
		}
		else if (arena_->IsTag(node, ::cppgm::syntax::STAG_USING_DIRECTIVE))
		{
			const NodeId target = FindChild(node, ::cppgm::syntax::STAG_TARGET);
			const ScopeId target_scope = target == kNoNode ? kNoScope :
				ResolveScopeSpelling(scope, arena_->Payload(target),
					NAME_PATH_PARSE_LITERAL);
			if (target_scope == kNoScope)
				ThrowSemanticError(
					"constexpr using namespace target not found");
			AddConstexprUsingNamespace(target_scope);
			valid = true;
		}
		else if (arena_->IsTag(node, ::cppgm::syntax::STAG_STATIC_ASSERT_DECLARATION))
		{
			AnalyzeStaticAssert(node, scope);
			valid = true;
		}
		else if (arena_->IsTag(node, ::cppgm::syntax::STAG_SIMPLE_DECLARATION))
		{
			const NodeId specifiers = FindChild(node, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
			const NodeId list = FindChild(node, ::cppgm::syntax::STAG_INIT_DECLARATOR_LIST);
			const SpecInfo spec = BuildSpecifiers(
				specifiers, scope, std::string(), list != kNoNode);
			valid = list != kNoNode &&
				spec.storage_class == STORAGE_CLASS_NONE &&
				!spec.thread_local_storage;
			for (std::uint32_t edge = valid ? arena_->FirstEdge(list) : kNoEdge;
				edge != kNoEdge && valid; edge = arena_->NextEdge(edge))
			{
				const NodeId item = arena_->EdgeChild(edge);
				const NodeId declarator = FindChild(item, ::cppgm::syntax::STAG_DECLARATOR);
				DeclaratorInfo parsed = BuildDeclarator(
					declarator, spec.type, scope);
				parsed.name = DeclaratorNamePath(declarator).Last();
				if (spec.is_constexpr)
					parsed.type = program_->types.Qualify(parsed.type, CV_CONST);
				if (spec.is_typedef)
					valid = AddConstexprTypeAlias(parsed.name, parsed.type);
				else
				{
					const NodeId initializer = FindChild(item, ::cppgm::syntax::STAG_INITIALIZER);
					ExpressionInfo value;
					valid = parsed.name != 0 && initializer != kNoNode &&
						AnalyzeConstexprInitializer(initializer, scope,
							parsed.type, &value);
					if (valid)
					{
						const std::uint32_t address =
							program_->types.IsReference(parsed.type) ?
							value.constexpr_lvalue_address :
							value.constexpr_address;
						std::size_t local = 0;
						if (value.constexpr_object != kNoConstexprObject)
						{
							valid = AddConstexprLocal(parsed.name, 0, parsed.type,
								value.constexpr_object,
								ExpressionCompleteObject(value), &local);
							if (valid && address != kNoConstexprAddress)
								constexpr_locals_[local].address = address;
						}
						else if (address != kNoConstexprAddress)
							valid = AddConstexprAddressLocal(
								parsed.name, 0, parsed.type, address);
						else valid =
							(IsIntegral(parsed.type, true) ||
							 IsFloating(parsed.type) ||
							 IsMemberPointer(parsed.type)) &&
							AddConstexprLocal(parsed.name, 0, parsed.type,
								ExpressionScalar(value));
					}
				}
			}
		}
	}
	return valid;
}

bool Analyzer::EvaluateConstexprCondition(
	NodeId node, ScopeId scope, bool* value)
{
	if (!ConsumeConstexprStep()) return false;
	const NodeId first = FirstSemanticChild(node);
	const NodeId declaration = first != kNoNode &&
		arena_->IsTag(first, ::cppgm::syntax::STAG_CONDITION_DECLARATION) ? first : node;
	const NodeId specifiers = FindChild(declaration, ::cppgm::syntax::STAG_DECL_SPECIFIER_SEQ);
	if (specifiers != kNoNode)
	{
		const SpecInfo spec = BuildSpecifiers(
			specifiers, scope, std::string(), true);
		const NodeId declarator = FindChild(declaration, ::cppgm::syntax::STAG_DECLARATOR);
		DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, scope);
		parsed.name = DeclaratorNamePath(declarator).Last();
		if (spec.is_constexpr)
			parsed.type = program_->types.Qualify(parsed.type, CV_CONST);
		const NodeId initializer = FindChild(declaration, ::cppgm::syntax::STAG_INITIALIZER);
		ExpressionInfo evaluated;
		if (parsed.name == 0 || initializer == kNoNode ||
			(!IsIntegral(parsed.type, true) && !IsFloating(parsed.type) &&
			 !IsMemberPointer(parsed.type) &&
			 !IsPointer(EffectiveType(parsed.type))) ||
			!AnalyzeConstexprInitializer(
				initializer, scope, parsed.type, &evaluated) ||
			(IsPointer(EffectiveType(parsed.type)) ?
			 !AddConstexprAddressLocal(parsed.name, 0, parsed.type,
				 ExpressionAddress(evaluated)) :
			 !AddConstexprLocal(parsed.name, 0, parsed.type,
				 ExpressionScalar(evaluated)))) return false;
		*value = ExpressionTruth(evaluated);
		return true;
	}
	ExpressionInfo expression;
	if (first == kNoNode ||
		!AnalyzeConstexprExpression(first, scope, kNoType, &expression) ||
		(!IsIntegral(expression.type, true) &&
		 !IsFloating(expression.type) &&
		 !IsMemberPointer(expression.type) &&
		 !IsPointer(EffectiveType(expression.type)))) return false;
	*value = ExpressionTruth(expression);
	return true;
}

ConstexprFlow Analyzer::EvaluateConstexprCompound(
	NodeId node, ScopeId scope, TypeId result_type,
	ConstexprScalarValue* result, bool* result_has_scalar,
	std::uint32_t* result_address,
	std::uint32_t* result_object, std::uint32_t* result_complete_object)
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
			EvaluateConstexprStatement(child, scope, result_type, result,
				result_has_scalar, result_address, result_object,
				result_complete_object);
		if (flow != CONSTEXPR_FLOW_NORMAL)
		{
			result_flow = flow;
			break;
		}
	}
	PopConstexprBlock();
	return result_flow;
}

ConstexprFlow Analyzer::EvaluateConstexprStatement(
	NodeId node, ScopeId scope, TypeId result_type,
	ConstexprScalarValue* result, bool* result_has_scalar,
	std::uint32_t* result_address,
	std::uint32_t* result_object, std::uint32_t* result_complete_object)
{
	if (!ConsumeConstexprStep()) return CONSTEXPR_FLOW_INVALID;
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_COMPOUND_STATEMENT))
		return EvaluateConstexprCompound(node, scope, result_type, result,
			result_has_scalar, result_address, result_object,
			result_complete_object);
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_RETURN_STATEMENT))
	{
		const NodeId expression = FirstSemanticChild(node);
		if (expression == kNoNode) return CONSTEXPR_FLOW_INVALID;
		return EvaluateConstexprReturn(
			expression, scope, result_type, result, result_has_scalar,
			result_address,
			result_object, result_complete_object);
	}
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_EXPRESSION_STATEMENT))
	{
		const NodeId expression = FirstSemanticChild(node);
		if (expression == kNoNode) return CONSTEXPR_FLOW_NORMAL;
		ExpressionInfo value;
		return AnalyzeConstexprExpression(
			expression, scope, kNoType, &value) ?
			CONSTEXPR_FLOW_NORMAL : CONSTEXPR_FLOW_INVALID;
	}
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_IF_STATEMENT))
	{
		PushConstexprBlock();
		NodeId condition = kNoNode;
		NodeId then_branch = kNoNode;
		NodeId else_branch = kNoNode;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, ::cppgm::syntax::STAG_CONDITION)) condition = child;
			else if (arena_->IsTag(child, ::cppgm::syntax::STAG_THEN))
				then_branch = FirstSemanticChild(child);
			else if (arena_->IsTag(child, ::cppgm::syntax::STAG_ELSE))
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
			EvaluateConstexprStatement(branch, scope, result_type, result,
				result_has_scalar, result_address, result_object,
				result_complete_object);
		PopConstexprBlock();
		return flow;
	}
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_WHILE_STATEMENT) ||
		arena_->IsTag(node, ::cppgm::syntax::STAG_DO_STATEMENT))
	{
		const bool is_do = arena_->IsTag(node, ::cppgm::syntax::STAG_DO_STATEMENT);
		PushConstexprBlock();
		NodeId condition = kNoNode;
		NodeId body = kNoNode;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, ::cppgm::syntax::STAG_CONDITION)) condition = child;
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
				body, scope, result_type, result, result_has_scalar, result_address,
				result_object, result_complete_object);
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
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_FOR_STATEMENT))
	{
		PushConstexprBlock();
		NodeId condition = kNoNode;
		NodeId iteration_expression = kNoNode;
		NodeId body = kNoNode;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, ::cppgm::syntax::STAG_FOR_INIT_STATEMENT))
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
			else if (arena_->IsTag(child, ::cppgm::syntax::STAG_CONDITION)) condition = child;
			else if (arena_->IsTag(child, ::cppgm::syntax::STAG_ITERATION))
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
				body, scope, result_type, result, result_has_scalar, result_address,
				result_object, result_complete_object);
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
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_BREAK_STATEMENT))
		return CONSTEXPR_FLOW_BREAK;
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_CONTINUE_STATEMENT))
		return CONSTEXPR_FLOW_CONTINUE;
	if (IsDeclaration(node))
		return EvaluateConstexprDeclaration(node, scope) ?
			CONSTEXPR_FLOW_NORMAL : CONSTEXPR_FLOW_INVALID;
	return CONSTEXPR_FLOW_INVALID;
}

bool Analyzer::AddConstexprInvocationArguments(
	const FunctionInfo& function,
	const std::vector<ExpressionInfo>& arguments)
{
	if (arguments.size() != function.parameters.size()) return false;
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		const ParameterInfo& parameter = function.parameters[i];
		const TypeId type = ParameterBindingType(parameter);
		const bool reference = program_->types.IsReference(type);
		const bool pointer = IsPointer(EffectiveType(type));
		std::uint32_t address = reference ?
			arguments[i].constexpr_lvalue_address :
			ExpressionAddress(arguments[i]);
		const bool scalar_temporary = reference &&
			address == kNoConstexprAddress &&
			arguments[i].category != VALUE_LVALUE && arguments[i].constant &&
			(IsIntegral(EffectiveType(type), true) ||
			 IsFloating(EffectiveType(type)) ||
			 IsMemberPointer(EffectiveType(type)));
		if (scalar_temporary)
		{
			const TypeId object_type = program_->types.RemoveTopCv(
				EffectiveType(type));
			const std::int64_t extent = static_cast<std::int64_t>(
				program_->SizeOf(object_type));
			address = InternConstexprAddress(ConstexprAddressValue(
				CONSTEXPR_ADDRESS_LOCAL, next_constexpr_storage_identity_++,
				0, 0, extent));
		}
		if (reference && address == kNoConstexprAddress) return false;
		const std::uint32_t object = ExpressionObject(arguments[i]);
		bool added = false;
		std::size_t local = std::numeric_limits<std::size_t>::max();
		if ((pointer || reference) && address != kNoConstexprAddress)
		{
			added = AddConstexprAddressLocal(parameter.name,
				parameter.pack_name, type, address, &local);
			if (added && reference && arguments[i].constant &&
				(IsIntegral(EffectiveType(type), true) ||
				 IsFloating(EffectiveType(type)) ||
				 IsMemberPointer(EffectiveType(type))))
				constexpr_locals_[local].value = ConvertScalarConstant(
					arguments[i].type, type, ExpressionScalar(arguments[i]));
			if (added && object != kNoConstexprObject &&
				ExpressionCompleteObject(arguments[i]) != kNoConstexprObject)
			{
				constexpr_locals_[local].object = object;
				constexpr_locals_[local].complete_object =
					ExpressionCompleteObject(arguments[i]);
			}
		}
		else if (object != kNoConstexprObject)
			added = AddConstexprLocal(
				parameter.name, parameter.pack_name, type, object,
				ExpressionCompleteObject(arguments[i]), 0);
		else if (arguments[i].constant &&
			(IsIntegral(EffectiveType(type), true) ||
			 IsFloating(EffectiveType(type)) ||
			 IsMemberPointer(EffectiveType(type))))
			added = AddConstexprLocal(parameter.name, parameter.pack_name, type,
				ConvertScalarConstant(arguments[i].type, type,
					ExpressionScalar(arguments[i])));
		if (!added) return false;
	}
	return true;
}

bool Analyzer::AnalyzeConstexprMemberInitializer(
	NodeId initializer, ScopeId scope, TypeId type, ExpressionInfo* value)
{
	while (initializer != kNoNode && arena_->IsTag(initializer, ::cppgm::syntax::STAG_INITIALIZER))
		initializer = FirstSemanticChild(initializer);
	if (initializer == kNoNode) return false;
	const TypeId object_type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord& record = program_->types.Get(object_type);
	const bool argument_list =
		arena_->IsTag(initializer, ::cppgm::syntax::STAG_PAREN_ARGUMENT_LIST) ||
		arena_->IsTag(initializer, ::cppgm::syntax::STAG_BRACED_INIT_LIST);
	if (!argument_list)
		return AnalyzeConstexprExpression(initializer, scope, type, value);

	std::vector<NodeId> syntax;
	for (std::uint32_t edge = arena_->FirstEdge(initializer); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		syntax.push_back(arena_->EdgeChild(edge));
	const bool aggregate_braced = arena_->IsTag(initializer,
		::cppgm::syntax::STAG_BRACED_INIT_LIST) && (record.kind == TYPE_ARRAY ||
		(IsConstexprClassEntity(*program_, EntityOf(object_type)) &&
		 program_->entities[EntityOf(object_type)].is_aggregate));
	std::vector<ExpressionInfo> prepared;
	const bool expanded = !aggregate_braced && ExpandCallArgumentPacks(
		syntax, scope, &syntax, &prepared);
	if (IsIntegral(object_type, true) || IsFloating(object_type) ||
		IsMemberPointer(object_type))
	{
		if (syntax.empty())
		{
			value->node = kNoDumpEdge;
			value->type = type;
			value->category = VALUE_NONE;
			const ConstexprScalarValue zero = IsMemberPointer(object_type) ?
				ConstexprScalarValue(kNoBinding, static_cast<std::int64_t>(0)) :
				ConstexprScalarValue(static_cast<std::int64_t>(0));
			SetExpressionScalar(value, NormalizeScalarConstant(type, zero));
			return true;
		}
		if (syntax.size() != 1) return false;
		if (!expanded)
			return AnalyzeConstexprExpression(syntax[0], scope, type, value);
		*value = ApplyTarget(prepared[0], type);
		return value->constant;
	}
	if (IsPointer(object_type))
	{
		if (syntax.size() != 1) return false;
		if (!expanded)
			return AnalyzeConstexprExpression(syntax[0], scope, type, value);
		*value = ApplyTarget(prepared[0], type);
		return ExpressionAddress(*value) != kNoConstexprAddress;
	}
	if (record.kind == TYPE_ARRAY ||
		(IsConstexprClassEntity(*program_, EntityOf(object_type)) &&
		 program_->entities[EntityOf(object_type)].is_aggregate))
	{
		if (record.kind == TYPE_ARRAY && syntax.empty() &&
			arena_->IsTag(initializer, ::cppgm::syntax::STAG_PAREN_ARGUMENT_LIST))
		{
			std::uint32_t omitted = kNoEdge;
			*value = AnalyzeArrayAggregateInit(type, scope, &omitted);
			SetExpressionDumpObject(value);
			return value->constant;
		}
		if (!arena_->IsTag(initializer, ::cppgm::syntax::STAG_BRACED_INIT_LIST)) return false;
		*value = AnalyzeBracedInit(initializer, scope, type);
		SetExpressionDumpObject(value);
		return value->constant;
	}
	if (!IsConstexprClassEntity(*program_, EntityOf(object_type))) return false;
	value->node = BuildConstructorAction(type, scope, syntax, false,
		arena_->IsTag(initializer, ::cppgm::syntax::STAG_BRACED_INIT_LIST), false, true,
		arena_->IsTag(initializer, ::cppgm::syntax::STAG_BRACED_INIT_LIST) ? initializer : kNoNode,
		expanded ? &prepared : 0);
	value->type = type;
	value->category = VALUE_NONE;
	SetExpressionDumpObject(value);
	return value->constant;
}

struct Analyzer::ConstexprConstructorPlan
{
	ConstexprConstructorPlan(std::size_t members, std::size_t bases,
		ScopeId lexical_scope)
		: member_initializers(members, kNoNode),
		  member_scopes(members, lexical_scope),
		  base_initializers(bases, kNoNode),
		  base_scopes(bases, lexical_scope),
		  base_prepared_arguments(
			bases, std::numeric_limits<std::size_t>::max()),
		  active_union_member(kNoBinding),
		  delegating_initializer(kNoNode),
		  delegating_scope(lexical_scope)
	{}

	std::vector<NodeId> member_initializers;
	std::vector<ScopeId> member_scopes;
	std::vector<NodeId> base_initializers;
	std::vector<ScopeId> base_scopes;
	std::vector<std::size_t> base_prepared_arguments;
	BindingId active_union_member;
	NodeId delegating_initializer;
	ScopeId delegating_scope;
};

bool Analyzer::PlanConstexprConstructorInitializers(
	const FunctionInfo& constructor, EntityId entity,
	std::size_t argument_count, ConstexprConstructorPlan* plan)
{
	if (constructor.constructor_initializer == kNoNode)
	{
		if (program_->entities[entity].flavor == NAMED_UNION)
			plan->active_union_member =
				program_->entities[entity].union_default_member;
		return true;
	}
	try
	{
		std::vector<NodeId> syntax;
		std::vector<ScopeId> scopes;
		std::vector<std::uint8_t> expanded;
		std::vector<NodeId> raw_initializers;
		for (std::uint32_t edge = arena_->FirstEdge(
			constructor.constructor_initializer); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
			if (arena_->IsTag(arena_->EdgeChild(edge), ::cppgm::syntax::STAG_MEM_INITIALIZER))
				raw_initializers.push_back(arena_->EdgeChild(edge));

		const std::size_t base_count = plan->base_initializers.size();
		bool positional_base_pack = raw_initializers.size() == 1 &&
			base_count == argument_count && base_count != 0 &&
			FindChild(raw_initializers[0], ::cppgm::syntax::STAG_PACK_EXPANSION) != kNoNode;
		if (positional_base_pack)
		{
			const NodeId id = FindChild(
				raw_initializers[0], ::cppgm::syntax::STAG_MEM_INITIALIZER_ID);
			NodeId value = kNoNode;
			for (std::uint32_t child = arena_->FirstEdge(raw_initializers[0]);
				child != kNoEdge; child = arena_->NextEdge(child))
			{
				const NodeId candidate = arena_->EdgeChild(child);
				if (candidate != id &&
					!arena_->IsTag(candidate, ::cppgm::syntax::STAG_PACK_EXPANSION)) value = candidate;
			}
			positional_base_pack = id != kNoNode && value != kNoNode;
			for (std::size_t i = 0; positional_base_pack && i < base_count; ++i)
			{
				plan->base_initializers[i] = value;
				plan->base_prepared_arguments[i] = i;
			}
		}
		if (!positional_base_pack)
			CollectConstructorInitializers(constructor, entity,
				constructor.lexical_scope, &syntax, &scopes, &expanded);
		if (CandidateSubstitutionFailed()) return false;

		const std::vector<BindingId>& members = entity_data_members_[entity];
		for (std::size_t index = 0; index < syntax.size(); ++index)
		{
			const NodeId initializer = syntax[index];
			const ScopeId initializer_scope = scopes[index];
			const NodeId id = FindChild(initializer, ::cppgm::syntax::STAG_MEM_INITIALIZER_ID);
			if (id == kNoNode) return false;
			const LookupResult found = program_->LookupDirect(
				program_->entities[entity].member_scope,
				program_->names.UseInterned(arena_->PayloadId(id)), LOOKUP_ORDINARY);
			NodeId value = kNoNode;
			for (std::uint32_t child = arena_->FirstEdge(initializer);
				child != kNoEdge; child = arena_->NextEdge(child))
			{
				const NodeId candidate = arena_->EdgeChild(child);
				if (candidate != id &&
					!arena_->IsTag(candidate, ::cppgm::syntax::STAG_PACK_EXPANSION)) value = candidate;
			}
			if (value == kNoNode) return false;

			LookupResult target_type;
			const NodeId structured = FindChild(id, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
			if (structured != kNoNode)
				target_type.type = ResolveStructuredTypeName(
					structured, initializer_scope);
			else target_type =
				LookupSyntaxName(id, initializer_scope, LOOKUP_TYPE);
			if (target_type.type != kNoType &&
				EntityOf(target_type.type) == entity)
			{
				if (syntax.size() != 1 ||
					plan->delegating_initializer != kNoNode) return false;
				plan->delegating_initializer = value;
				plan->delegating_scope = initializer_scope;
				continue;
			}
			if (found.ordinary != kNoBinding &&
				program_->bindings[found.ordinary].non_static_data_member &&
				program_->bindings[found.ordinary].member_owner == entity)
			{
				if (program_->entities[entity].flavor == NAMED_UNION &&
					plan->active_union_member != kNoBinding &&
					plan->active_union_member != found.ordinary) return false;
				const std::size_t ordinal =
					program_->BindingLayout(
						program_->bindings[found.ordinary]).member_ordinal;
				if (ordinal >= members.size() ||
					members[ordinal] != found.ordinary ||
					plan->member_initializers[ordinal] != kNoNode) return false;
				plan->member_initializers[ordinal] = value;
				plan->member_scopes[ordinal] = initializer_scope;
				if (program_->entities[entity].flavor == NAMED_UNION)
					plan->active_union_member = found.ordinary;
				continue;
			}
			if (found.ordinary != kNoBinding) return false;

			const EntityId target_base = EntityOf(target_type.type);
			std::size_t base_ordinal = base_count;
			for (std::size_t i = 0; i < base_count; ++i)
				if (program_->DirectBase(entity, i).entity == target_base)
				{
					base_ordinal = i;
					break;
				}
			if (base_ordinal == base_count ||
				plan->base_initializers[base_ordinal] != kNoNode) return false;
			plan->base_initializers[base_ordinal] = value;
			plan->base_scopes[base_ordinal] = initializer_scope;
		}
	}
	catch (const SemanticError&) { return false; }
	return true;
}

bool Analyzer::EvaluateConstexprConstructorInitializers(
	const FunctionInfo& constructor, EntityId entity,
	const std::vector<ExpressionInfo>& arguments,
	const ConstexprConstructorPlan& plan, std::uint32_t* object)
{
	bool valid = true;
	const std::vector<BindingId>& members = entity_data_members_[entity];
	const std::size_t base_count = plan.base_initializers.size();
	const bool union_object =
		program_->entities[entity].flavor == NAMED_UNION;
	const BindingId active_union_member = plan.active_union_member != kNoBinding ?
		plan.active_union_member : program_->entities[entity].union_default_member;
	std::vector<ConstexprObjectElement> base_elements;
	std::vector<ConstexprObjectElement> member_elements;
	base_elements.reserve(base_count);
	member_elements.reserve(members.size());
	try
	{
		if (plan.delegating_initializer != kNoNode)
		{
			const std::size_t nodes = dump_.nodes.size();
			const std::size_t edges = dump_.edges.size();
			ExpressionInfo delegated;
			valid = AnalyzeConstexprMemberInitializer(
				plan.delegating_initializer, plan.delegating_scope,
				program_->entities[entity].type, &delegated);
			std::uint32_t delegated_action = delegated.node;
			if (valid && delegated_action < dump_.nodes.size() &&
				dump_.nodes[delegated_action].kind == DUMP_TEMPORARY_OBJECT &&
				dump_.nodes[delegated_action].first_edge != kNoDumpEdge)
				delegated_action = dump_.edges[
					dump_.nodes[delegated_action].first_edge].child;
			if (valid && delegated_action < dump_.nodes.size() &&
				dump_.nodes[delegated_action].kind == DUMP_CONSTRUCTOR_ACTION &&
				dump_.nodes[delegated_action].binding != kNoBinding)
				RecordDelegatingConstructor(constructor.binding,
					dump_.nodes[delegated_action].binding);
			const std::uint32_t delegated_object = ExpressionObject(delegated);
			if (valid && delegated_object != kNoConstexprObject &&
				constexpr_objects_[delegated_object].type ==
					program_->types.RemoveTopCv(program_->entities[entity].type))
			{
				*object = delegated_object;
				constexpr_frames_.back().receiver_object = delegated_object;
				constexpr_frames_.back().receiver_complete_object = delegated_object;
			}
			else valid = false;
			ReleaseConstexprScratch(nodes, edges);
		}
		for (std::size_t i = 0;
			valid && plan.delegating_initializer == kNoNode && i < base_count; ++i)
		{
			const EntityId base = program_->DirectBase(entity, i).entity;
			const TypeId base_type = program_->entities[base].type;
			const std::size_t nodes = dump_.nodes.size();
			const std::size_t edges = dump_.edges.size();
			ExpressionInfo initialized;
			const bool inherited_base =
				constructor.inherited_constructor_source != kNoBinding &&
				program_->bindings[
					constructor.inherited_constructor_source].member_owner == base;
			if (inherited_base)
			{
				std::uint32_t inherited_object = kNoConstexprObject;
				valid = TryEvaluateConstexprConstructor(
					constructor.inherited_constructor_source,
					arguments, &inherited_object);
				if (valid) initialized = MaterializeConstexprObject(
					inherited_object, base_type);
			}
			else if (plan.base_initializers[i] != kNoNode &&
				plan.base_prepared_arguments[i] !=
					std::numeric_limits<std::size_t>::max())
			{
				const ExpressionInfo& source =
					arguments[plan.base_prepared_arguments[i]];
				bool direct_trivial_copy = false;
				if (ExpressionObject(source) != kNoConstexprObject &&
					program_->types.RemoveTopCv(EffectiveType(source.type)) ==
						program_->types.RemoveTopCv(base_type))
				{
					const BindingId selected =
						ValidateClassValueConstruction(base_type, source, false);
					direct_trivial_copy = selected != kNoBinding &&
						GetFunction(selected).trivial_special_member;
					if (direct_trivial_copy) initialized = source;
				}
				NodeId argument_list = plan.base_initializers[i];
				while (argument_list != kNoNode &&
					arena_->IsTag(argument_list, ::cppgm::syntax::STAG_INITIALIZER))
					argument_list = FirstSemanticChild(argument_list);
				std::vector<NodeId> argument_syntax;
				if (argument_list != kNoNode &&
					(arena_->IsTag(argument_list, ::cppgm::syntax::STAG_PAREN_ARGUMENT_LIST) ||
					 arena_->IsTag(argument_list, ::cppgm::syntax::STAG_BRACED_INIT_LIST)))
					for (std::uint32_t edge = arena_->FirstEdge(argument_list);
						edge != kNoEdge; edge = arena_->NextEdge(edge))
						argument_syntax.push_back(arena_->EdgeChild(edge));
				else if (argument_list != kNoNode)
					argument_syntax.push_back(argument_list);
				const std::vector<ExpressionInfo> prepared(1, source);
				if (!direct_trivial_copy &&
					argument_syntax.size() != prepared.size()) valid = false;
				else if (!direct_trivial_copy)
				{
					initialized.node = BuildConstructorAction(base_type,
						plan.base_scopes[i], argument_syntax, false,
						arena_->IsTag(argument_list, ::cppgm::syntax::STAG_BRACED_INIT_LIST),
						true, false,
						arena_->IsTag(argument_list, ::cppgm::syntax::STAG_BRACED_INIT_LIST) ?
							argument_list : kNoNode, &prepared);
					initialized.type = base_type;
					initialized.category = VALUE_NONE;
					SetExpressionDumpObject(&initialized);
					valid = initialized.constant;
				}
			}
			else if (plan.base_initializers[i] != kNoNode)
				valid = AnalyzeConstexprMemberInitializer(
					plan.base_initializers[i], plan.base_scopes[i],
					base_type, &initialized);
			else
			{
				const std::vector<NodeId> no_arguments;
				initialized.node = BuildConstructorAction(base_type,
					constructor.lexical_scope, no_arguments,
					false, false, true, false);
				initialized.type = base_type;
				initialized.category = VALUE_NONE;
				SetExpressionDumpObject(&initialized);
				valid = initialized.constant;
			}
			ConstexprObjectElement element(kNoBinding,
				ConstexprScalarValue(static_cast<std::int64_t>(0)));
			if (valid) valid = BuildConstexprObjectElement(
				base_type, kNoBinding, initialized, &element);
			if (valid) base_elements.push_back(element);
			ReleaseConstexprScratch(nodes, edges);
		}
		for (std::size_t i = 0;
			valid && plan.delegating_initializer == kNoNode &&
			i < members.size(); ++i)
		{
			const BindingId member_id = members[i];
			if (union_object && member_id != active_union_member) continue;
			const BindingRecord& member = program_->bindings[member_id];
			NodeId initializer = plan.member_initializers[i];
			if (initializer == kNoNode &&
				member_id < member_initializer_by_binding_.size())
				initializer = member_initializer_by_binding_[member_id];
			const std::size_t nodes = dump_.nodes.size();
			const std::size_t edges = dump_.edges.size();
			ExpressionInfo initialized;
			if (initializer != kNoNode)
				valid = AnalyzeConstexprMemberInitializer(initializer,
					plan.member_scopes[i], member.type, &initialized);
			else
			{
				const TypeId member_type = program_->types.RemoveTopCv(
					EffectiveType(member.type));
				const TypeRecord& member_record = program_->types.Get(member_type);
				if (IsIntegral(member_type, true) || IsFloating(member_type) ||
					IsMemberPointer(member_type))
				{
					valid = constructor.defaulted_constructor ||
						constructor.implicit_constructor;
					initialized.node = kNoDumpEdge;
					initialized.type = member.type;
					initialized.category = VALUE_NONE;
				}
				else if (member_record.kind == TYPE_ARRAY)
				{
					std::uint32_t omitted = kNoEdge;
					initialized = AnalyzeArrayAggregateInit(
						member.type, constructor.lexical_scope, &omitted);
					valid = initialized.constant;
				}
				else if (IsConstexprClassEntity(
					*program_, EntityOf(member_type)))
				{
					const std::vector<NodeId> no_arguments;
					initialized.node = BuildConstructorAction(member.type,
						constructor.lexical_scope, no_arguments, false, false);
					initialized.type = member.type;
					initialized.category = VALUE_NONE;
					SetExpressionDumpObject(&initialized);
					valid = initialized.constant;
				}
				else valid = false;
			}
			ConstexprObjectElement element(member_id,
				ConstexprScalarValue(static_cast<std::int64_t>(0)));
			if (valid) valid = BuildConstexprObjectElement(
				member.type, member_id, initialized, &element);
			if (valid) member_elements.push_back(element);
			ReleaseConstexprScratch(nodes, edges);
		}
		const std::size_t required_member_elements = union_object ?
			(active_union_member == kNoBinding ? 0 : 1) : members.size();
		if (valid && plan.delegating_initializer == kNoNode &&
			member_elements.size() == required_member_elements &&
			base_elements.size() == base_count)
		{
			std::vector<ConstexprObjectElement> elements;
			elements.reserve(member_elements.size() + base_elements.size());
			elements.insert(elements.end(),
				member_elements.begin(), member_elements.end());
			elements.insert(elements.end(),
				base_elements.begin(), base_elements.end());
			*object = InternConstexprObject(program_->entities[entity].type, elements);
			if (constexpr_objects_[*object].newest_local_storage_identity >=
				constexpr_frames_.back().first_storage_identity)
			{
				*object = kNoConstexprObject;
				valid = false;
			}
			else
			{
				constexpr_frames_.back().receiver_object = *object;
				constexpr_frames_.back().receiver_complete_object = *object;
			}
		}
	}
	catch (const SemanticError&) { valid = false; }
	return valid;
}

bool Analyzer::TryEvaluateConstexprConstructor(BindingId function,
	const std::vector<ExpressionInfo>& arguments, std::uint32_t* object)
{
	function = program_->bindings[function].canonical;
	const FunctionInfo info = GetFunction(function);
	if (!info.constructor || info.deleted_constructor ||
		(!info.constexpr_function && !info.defaulted_constructor &&
		 !info.implicit_constructor) ||
		arguments.size() != info.parameters.size()) return false;
	EnsureFunctionExceptionSpecification(function);
	const EntityId entity = program_->bindings[function].member_owner;
	if (entity == kNoEntity || entity >= entity_data_members_.size()) return false;
	if ((info.defaulted_constructor || info.implicit_constructor) &&
		arguments.size() == 1)
	{
		const std::uint32_t copied = ExpressionObject(arguments[0]);
		if (copied != kNoConstexprObject &&
			constexpr_objects_[copied].type ==
				program_->types.RemoveTopCv(program_->entities[entity].type))
		{
			*object = copied;
			return true;
		}
	}
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		const TypeId type = ParameterBindingType(info.parameters[i]);
		if (ExpressionObject(arguments[i]) == kNoConstexprObject &&
			ExpressionAddress(arguments[i]) == kNoConstexprAddress &&
			arguments[i].constexpr_lvalue_address == kNoConstexprAddress &&
			(!arguments[i].constant ||
				 (!IsIntegral(EffectiveType(type), true) &&
				  !IsFloating(EffectiveType(type)) &&
				  !IsMemberPointer(EffectiveType(type))))) return false;
	}
	if (constexpr_evaluation_depth_ == 0) constexpr_evaluation_steps_ = 0;
	if (constexpr_evaluation_depth_ >= kMaxConstexprDepth ||
		!ConsumeConstexprStep()) return false;

	const bool outermost = constexpr_evaluation_depth_ == 0;
	if (outermost)
	{
		constexpr_scratch_dump_.nodes.clear();
		constexpr_scratch_dump_.edges.clear();
		constexpr_scratch_object_by_dump_.clear();
		std::swap(dump_, constexpr_scratch_dump_);
		std::swap(constexpr_object_by_dump_,
			constexpr_scratch_object_by_dump_);
	}
	++constexpr_evaluation_depth_;
	if (constexpr_evaluation_depth_ > constexpr_max_depth_)
		constexpr_max_depth_ = constexpr_evaluation_depth_;
	constexpr_evaluation_stack_.push_back(function);
	constexpr_frames_.push_back(ConstexprFrame(function,
		constexpr_locals_.size(), constexpr_scope_facts_.size(),
		constexpr_block_offsets_.size(), next_constexpr_storage_identity_));
	const TypeId previous_return = current_return_type_;
	const EntityId previous_class = current_class_context_;
	const BindingId previous_function = current_function_context_;
	const TypeId result_type = program_->types.Get(info.type).child;
	current_return_type_ = result_type;
	current_class_context_ = entity;
	current_function_context_ = function;
	bool valid = AddConstexprInvocationArguments(info, arguments);
	ConstexprConstructorPlan plan(entity_data_members_[entity].size(),
		program_->entities[entity].direct_base_count, info.lexical_scope);
	if (valid) valid = PlanConstexprConstructorInitializers(
		info, entity, arguments.size(), &plan);
	if (valid) valid = EvaluateConstexprConstructorInitializers(
		info, entity, arguments, plan, object);
	if (valid && info.definition_body != kNoNode)
	{
		ConstexprScalarValue ignored;
		bool ignored_has_scalar = false;
		try
		{
			std::uint32_t ignored_address = kNoConstexprAddress;
			std::uint32_t ignored_object = kNoConstexprObject;
			std::uint32_t ignored_complete_object = kNoConstexprObject;
			valid = EvaluateConstexprCompound(info.definition_body,
				info.lexical_scope, result_type, &ignored, &ignored_has_scalar,
				&ignored_address,
				&ignored_object, &ignored_complete_object) ==
				CONSTEXPR_FLOW_NORMAL;
		}
		catch (const SemanticError&) { valid = false; }
	}
	else if (valid && !info.defaulted_constructor &&
		!info.implicit_constructor) valid = false;
	current_return_type_ = previous_return;
	current_class_context_ = previous_class;
	current_function_context_ = previous_function;

	const ConstexprFrame frame = constexpr_frames_.back();
	constexpr_block_offsets_.erase(
		constexpr_block_offsets_.begin() + frame.first_block,
		constexpr_block_offsets_.end());
	ReleaseConstexprLocals(frame.first_local);
	ReleaseConstexprScopeFacts(frame.first_scope_fact);
	constexpr_frames_.pop_back();
	constexpr_evaluation_stack_.pop_back();
	--constexpr_evaluation_depth_;
	if (outermost)
	{
		dump_.nodes.clear();
		dump_.edges.clear();
		constexpr_object_by_dump_.clear();
		std::swap(dump_, constexpr_scratch_dump_);
		std::swap(constexpr_object_by_dump_,
			constexpr_scratch_object_by_dump_);
	}
	return valid;
}

bool Analyzer::TryEvaluateConstexprFunction(BindingId function,
	const std::vector<ExpressionInfo>& arguments,
	ConstexprScalarValue* value, bool* has_scalar,
	std::uint32_t* address, std::uint32_t* object,
	std::uint32_t* complete_object,
	const ExpressionInfo* receiver)
{
	function = program_->bindings[function].canonical;
	FunctionInfo info = GetFunction(function);
	if (info.constexpr_function && info.definition_body == kNoNode &&
		info.member_owner != kNoType)
	{
		const EntityId owner = EntityOf(info.member_owner);
		if (owner != kNoEntity &&
			owner < class_template_pattern_by_entity_.size() &&
			class_template_pattern_by_entity_[owner] != kNoDumpEdge)
		{
			DemandClassTemplateMemberDefinitions(owner);
			const BindingId specialization =
				program_->entities[owner].declaration;
			if (specialization != kNoBinding && specialization <
				class_template_member_definition_demand_states_.size())
				ApplyDemandedClassTemplateMemberDefinitions(specialization);
			info = GetFunction(function);
		}
	}
	const TypeId result_type = program_->types.Get(info.type).child;
	const bool nonstatic_member = info.member_owner != kNoType &&
		!program_->bindings[function].static_member_function;
	const std::uint32_t receiver_object = nonstatic_member && receiver ?
		ExpressionObject(*receiver) : kNoConstexprObject;
	const std::uint32_t receiver_complete_object =
		nonstatic_member && receiver ?
		ExpressionCompleteObject(*receiver) : kNoConstexprObject;
	std::uint32_t receiver_address = nonstatic_member && receiver ?
		ExpressionAddress(*receiver) : kNoConstexprAddress;
	if (receiver_address == kNoConstexprAddress && receiver)
		receiver_address = receiver->constexpr_lvalue_address;
	const TypeRecord returned = program_->types.Get(result_type);
	const bool address_result = IsPointer(EffectiveType(result_type)) ||
		returned.kind == TYPE_LVALUE_REFERENCE ||
		returned.kind == TYPE_RVALUE_REFERENCE;
	const TypeId result_object_type = program_->types.RemoveTopCv(
		EffectiveType(result_type));
	const TypeRecord& result_object_record =
		program_->types.Get(result_object_type);
	const bool object_result = !address_result &&
		result_object_record.kind == TYPE_NAMED &&
		IsConstexprClassEntity(*program_, result_object_record.entity);
	if (!info.constexpr_function || info.definition_body == kNoNode ||
		(!address_result && !object_result &&
		 !IsIntegral(result_type, true) && !IsFloating(result_type) &&
		 !IsMemberPointer(result_type)) ||
		arguments.size() != info.parameters.size() ||
		(nonstatic_member && receiver_object == kNoConstexprObject &&
		 receiver_address == kNoConstexprAddress))
		return false;
	EnsureFunctionExceptionSpecification(function);
	++constexpr_call_requests_;

	ConstexprCallKey key;
	key.function = function;
	key.receiver_object = receiver_object;
	key.receiver_complete_object = receiver_complete_object;
	key.receiver_address = receiver_address;
	key.arguments.reserve(arguments.size());
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		const TypeId type = ParameterBindingType(info.parameters[i]);
		const bool reference = program_->types.IsReference(type);
		ConstexprCallArgument argument;
		argument.type = type;
		argument.object = ExpressionObject(arguments[i]);
		argument.complete_object = ExpressionCompleteObject(arguments[i]);
		if (argument.object != kNoConstexprObject)
			argument.kind |= CONSTEXPR_CALL_ARGUMENT_OBJECT;
		argument.address = reference ?
			arguments[i].constexpr_lvalue_address : ExpressionAddress(arguments[i]);
		if (argument.address != kNoConstexprAddress)
			argument.kind |= CONSTEXPR_CALL_ARGUMENT_ADDRESS;
		if (arguments[i].constant &&
			(IsIntegral(EffectiveType(type), true) ||
			 IsFloating(EffectiveType(type)) ||
			 IsMemberPointer(EffectiveType(type))))
		{
			argument.kind |= CONSTEXPR_CALL_ARGUMENT_SCALAR;
			argument.scalar = ConvertScalarConstant(
				arguments[i].type, type, ExpressionScalar(arguments[i]));
		}
		const bool scalar_temporary = reference &&
			arguments[i].category != VALUE_LVALUE &&
			(argument.kind & CONSTEXPR_CALL_ARGUMENT_SCALAR);
		if (argument.kind == 0 || (reference &&
			!(argument.kind & CONSTEXPR_CALL_ARGUMENT_ADDRESS) &&
			!scalar_temporary)) return false;
		key.arguments.push_back(argument);
	}

	{
		std::unordered_map<ConstexprCallKey, ConstexprCallFact,
			ConstexprCallKeyHash>::iterator cached =
			constexpr_call_facts_.find(key);
		if (cached != constexpr_call_facts_.end())
		{
			++constexpr_call_cache_hits_;
			if (cached->second.state == 2)
			{
				*address = cached->second.address;
				*object = cached->second.object;
				*complete_object = cached->second.complete_object;
				*has_scalar = cached->second.has_scalar;
				if (cached->second.has_scalar) *value = cached->second.value;
				return true;
			}
			return false;
		}
		constexpr_call_facts_.insert(
			std::make_pair(key, ConstexprCallFact()));
	}
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
		constexpr_scratch_object_by_dump_.clear();
		std::swap(dump_, constexpr_scratch_dump_);
		std::swap(constexpr_object_by_dump_,
			constexpr_scratch_object_by_dump_);
	}
	++constexpr_evaluation_depth_;
	if (constexpr_evaluation_depth_ > constexpr_max_depth_)
		constexpr_max_depth_ = constexpr_evaluation_depth_;
	constexpr_evaluation_stack_.push_back(function);
	constexpr_frames_.push_back(ConstexprFrame(function,
		constexpr_locals_.size(), constexpr_scope_facts_.size(),
		constexpr_block_offsets_.size(), next_constexpr_storage_identity_,
		receiver_object, receiver_complete_object, receiver_address));
	bool valid = AddConstexprInvocationArguments(info, arguments);

	const TypeId previous_return = current_return_type_;
	const EntityId previous_class = current_class_context_;
	const BindingId previous_function = current_function_context_;
	current_return_type_ = result_type;
	current_class_context_ = program_->bindings[function].member_owner;
	current_function_context_ = function;
	ConstexprScalarValue evaluated;
	bool evaluated_has_scalar = false;
	std::uint32_t evaluated_address = kNoConstexprAddress;
	std::uint32_t evaluated_object = kNoConstexprObject;
	std::uint32_t evaluated_complete_object = kNoConstexprObject;
	ConstexprFlow flow = CONSTEXPR_FLOW_INVALID;
	try
	{
		if (valid) flow = EvaluateConstexprCompound(
			info.definition_body, info.lexical_scope, result_type, &evaluated,
			&evaluated_has_scalar,
			&evaluated_address, &evaluated_object,
			&evaluated_complete_object);
	}
	catch (const SemanticError&) { flow = CONSTEXPR_FLOW_INVALID; }
	current_return_type_ = previous_return;
	current_class_context_ = previous_class;
	current_function_context_ = previous_function;
	const ConstexprFrame frame = constexpr_frames_.back();
	constexpr_block_offsets_.erase(
		constexpr_block_offsets_.begin() + frame.first_block,
		constexpr_block_offsets_.end());
	ReleaseConstexprLocals(frame.first_local);
	ReleaseConstexprScopeFacts(frame.first_scope_fact);
	constexpr_frames_.pop_back();
	constexpr_evaluation_stack_.pop_back();
	--constexpr_evaluation_depth_;
	if (outermost)
	{
		dump_.nodes.clear();
		dump_.edges.clear();
		constexpr_object_by_dump_.clear();
		std::swap(dump_, constexpr_scratch_dump_);
		std::swap(constexpr_object_by_dump_,
			constexpr_scratch_object_by_dump_);
	}

	if (flow != CONSTEXPR_FLOW_RETURN)
	{
		constexpr_call_facts_.find(key)->second.state = 3;
		return false;
	}
	if (address_result)
	{
		if (evaluated_address == kNoConstexprAddress)
		{
			constexpr_call_facts_.find(key)->second.state = 3;
			return false;
		}
		ConstexprCallFact& fact = constexpr_call_facts_.find(key)->second;
		fact.state = 2;
		fact.address = evaluated_address;
		fact.object = evaluated_object;
		fact.complete_object = evaluated_complete_object;
		fact.has_scalar = evaluated_has_scalar;
		if (evaluated_has_scalar) fact.value = evaluated;
		*address = evaluated_address;
		*object = evaluated_object;
		*complete_object = evaluated_complete_object;
		if (evaluated_has_scalar) *value = evaluated;
		*has_scalar = evaluated_has_scalar;
		return true;
	}
	if (object_result)
	{
		if (evaluated_object == kNoConstexprObject ||
			constexpr_objects_[evaluated_object].type != result_object_type ||
			constexpr_objects_[evaluated_object].newest_local_storage_identity >=
				frame.first_storage_identity)
		{
			constexpr_call_facts_.find(key)->second.state = 3;
			return false;
		}
		ConstexprCallFact& fact = constexpr_call_facts_.find(key)->second;
		fact.state = 2;
		fact.object = evaluated_object;
		fact.complete_object = evaluated_complete_object;
		*object = evaluated_object;
		*complete_object = evaluated_complete_object;
		*has_scalar = false;
		return true;
	}
	const ConstexprScalarValue normalized =
		NormalizeScalarConstant(result_type, evaluated);
	ConstexprCallFact& fact = constexpr_call_facts_.find(key)->second;
	fact.state = 2;
	fact.value = normalized;
	fact.has_scalar = true;
	*value = normalized;
	*has_scalar = true;
	*complete_object = kNoConstexprObject;
	return true;
}

}
}
