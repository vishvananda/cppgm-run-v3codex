#ifndef CPPGM_PA16_CALL_ARGUMENT_LOWERING_H
#define CPPGM_PA16_CALL_ARGUMENT_LOWERING_H

#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"
#include "pa12_semantic_model.h"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa16_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

template <class Derived>
class CallArgumentLowering
{
protected:
	Operand LowerBooleanConversion(std::uint32_t node, const LowType& target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand value = derived.LowerValue(node);
		Operand truth_value = value;
		const TypeId source_type = derived.program_.types.RemoveTopCv(
			derived.arena_.nodes[node].type);
		const TypeRecord& source = derived.program_.types.Get(source_type);
		if (source.kind == TYPE_MEMBER_POINTER &&
			derived.program_.types.IsFunction(source.child))
			truth_value = derived.Convert(value, LowU64(), false);
		const Operand boolean = derived.Temp(LowU8());
		Instruction compare(Instruction::CMP);
		compare.dest = boolean.id;
		compare.op = LOW_OP_NE;
		compare.type = truth_value.type;
		compare.first = truth_value;
		compare.second = IsFloating(truth_value.type) ?
			derived.FloatingOperand("0.0", truth_value.type) :
			Operand(0, truth_value.type);
		derived.Emit(compare);
		const Operand result = derived.Temp(target);
		Instruction copy(Instruction::COPY);
		copy.dest = result.id;
		copy.type = target;
		copy.first = boolean;
		derived.Emit(copy);
		return result;
	}

	bool IsClassValueType(TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		type = derived.program_.types.RemoveTopCv(type);
		const TypeRecord& record = derived.program_.types.Get(type);
		if (record.kind != TYPE_NAMED) return false;
		const NamedFlavor flavor =
			derived.program_.entities[record.entity].flavor;
		return flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
			flavor == NAMED_UNION;
	}

	bool CanonicalizeOperatorLiteral(
		std::uint32_t node, const DumpNode& callee) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (derived.arena_.nodes[node].kind != DUMP_LITERAL ||
			callee.kind != DUMP_CALLEE || callee.binding == kNoBinding ||
			callee.binding >= derived.program_.bindings.size()) return false;
		const OperatorKind kind =
			derived.program_.bindings[callee.binding].operator_kind;
		return kind > OPERATOR_NONE && kind < OPERATOR_NEW;
	}

	bool CanonicalizeInitializerImmediate(std::uint32_t node,
		const LowType& target) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& source_node = derived.arena_.nodes[node];
		if (CanonicalizeNullPointerImmediate(node, target)) return true;
		if (source_node.integer_narrowing_conversion) return true;
		if (source_node.kind != DUMP_LITERAL ||
			source_node.enum_arithmetic_conversion) return false;
		const LowType source = derived.LowerExpressionType(source_node.type);
		return IsInteger(source) && IsInteger(target) &&
			source.is_signed == target.is_signed &&
			(source.is_signed || source.width >= target.width);
	}

	bool CanonicalizeNullPointerImmediate(std::uint32_t node,
		const LowType& target) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& cast = derived.arena_.nodes[node];
		if (target.kind != LOW_PTR || cast.kind != DUMP_CAST_EXPRESSION)
			return false;
		const NodeChildren children = derived.Children(node);
		return children.size() == 1 && IsNullPointerLiteralCast(
			derived.program_, derived.arena_.nodes[children[0]], cast.type);
	}

	bool CanonicalizeBinaryImmediate(std::uint32_t node,
		const LowType& target, bool selected, bool comparison,
		bool constant_expression, bool template_layout_constant) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& source_node = derived.arena_.nodes[node];
		if (source_node.template_parameter_constant) return false;
		if (!selected || source_node.kind != DUMP_LITERAL) return selected;
		const LowType source = derived.LowerExpressionType(source_node.type);
		if (!IsInteger(source) || !IsInteger(target)) return selected;
		const TypeId source_type =
			derived.program_.types.RemoveTopCv(source_node.type);
		const TypeRecord& source_record =
			derived.program_.types.Get(source_type);
		if (source_record.kind == TYPE_FUNDAMENTAL &&
			source_record.fundamental == FUND_BOOL) return true;
		if (source.is_signed != target.is_signed)
			return !comparison && constant_expression &&
				!template_layout_constant;
		return !comparison || source.is_signed || source.width >= target.width;
	}

	Operand LowerInitializerConvertedValue(std::uint32_t node,
		const LowType& target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		return derived.LowerConvertedValue(node, target,
			CanonicalizeInitializerImmediate(node, target));
	}

	Operand LowerClassArgumentStaging(std::uint32_t node, TypeId target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const LowType type = derived.LowerType(target);
		const Operand slot(derived.EnsureGeneratedSlot(
			node, derived.UsesIndirectClassParameter(target) ?
				"arg" : "argobj", type), type);
		if (derived.arena_.nodes[node].kind == DUMP_TEMPORARY_OBJECT)
		{
			const Operand temporary = derived.LowerStorage(node);
			return derived.UsesIndirectClassParameter(target) ? temporary : slot;
		}
		const Operand destination = derived.AddressOfStorage(slot);
		if (derived.arena_.nodes[node].kind == DUMP_INITIALIZER_LIST)
			derived.LowerInitializerListObject(node, destination);
		else if (derived.arena_.nodes[node].kind == DUMP_CLASS_VALUE_TRANSFER)
			derived.LowerClassValueTransfer(node, destination);
		else if (derived.arena_.nodes[node].kind == DUMP_CONSTRUCTOR_ACTION)
		{
			const EntityId entity = derived.ClassEntity(target);
			if (derived.arena_.nodes[node].value_initialization &&
				entity != kNoEntity && entity < derived.program_.entities.size() &&
				!derived.program_.entities[entity].empty_class)
				derived.EmitZeroInitialization(target, destination);
			derived.LowerConstructorAction(node, destination);
		}
		else (void)derived.AddressOfStorage(derived.LowerStorage(node));
		return derived.UsesIndirectClassParameter(target) ? destination : slot;
	}

	bool IsProjectedClassReference(std::uint32_t node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const DumpNode& argument = derived.arena_.nodes[node];
		return argument.kind == DUMP_CAST_EXPRESSION &&
			argument.base_projection_count != 0 &&
			derived.IsClassObjectType(argument.type);
	}

	Operand LowerProjectedClassPointer(std::uint32_t child,
		std::uint32_t projection_count, std::uint64_t projection_offset,
		bool has_projection_offset, EntityId target_entity = kNoEntity,
		bool inverse_projection = false)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Operand inherited;
		if (target_entity != kNoEntity &&
			derived.CurrentVirtualBaseAddressForExpression(
				child, target_entity, &inherited))
			return derived.ProjectBaseSubobjectOffset(inherited, 0);
		const DumpNode& source = derived.arena_.nodes[child];
		const Operand address = derived.IsClassObjectType(source.type) ?
			derived.AddressOfStorage(derived.LowerStorage(child)) :
			derived.LowerValue(child, LowPtr());
		TypeId source_shape = derived.program_.types.RemoveTopCv(source.type);
		const bool pointer_source =
			derived.program_.types.Get(source_shape).kind == TYPE_POINTER;
		const bool nonnull_this = source.kind == DUMP_ID_EXPRESSION &&
			source.binding != kNoBinding &&
			source.binding == derived.current_this_binding_;
		const bool known_nonnull_address =
			source.kind == DUMP_UNARY_EXPRESSION &&
			StripOperationPrefix(derived.program_.names.Get(source.text)) == "&";
		EntityId entity = derived.BaseEntityForType(source.type);
		bool adjusted = has_projection_offset && projection_offset != 0;
		for (std::uint32_t i = 0; !has_projection_offset &&
			i < projection_count && entity != kNoEntity; ++i)
		{
			adjusted = adjusted ||
				derived.program_.entities[entity].direct_base_offset != 0;
			entity = derived.program_.entities[entity].direct_base;
		}
		if (!pointer_source || !adjusted || nonnull_this || known_nonnull_address)
		{
			if (inverse_projection)
				return derived.ProjectBaseSubobjectAdjustment(address,
					-static_cast<std::int64_t>(projection_offset));
			return derived.ProjectBaseSubobjects(address, projection_count,
				source.type, projection_offset, has_projection_offset);
		}

		const Operand result(derived.EnsureGeneratedSlot(
			child, "basecast", LowPtr()), LowPtr());
		const BlockId null_block = derived.AddBlock(
			derived.NewLabel("basecast_null"));
		const BlockId adjust_block = derived.AddBlock(
			derived.NewLabel("basecast_adjust"));
		const BlockId end_block = derived.AddBlock(
			derived.NewLabel("basecast_end"));
		const Operand is_null = derived.Temp(LowU8());
		Instruction compare(Instruction::CMP);
		compare.dest = is_null.id;
		compare.op = LOW_OP_EQ;
		compare.type = LowPtr();
		compare.first = address;
		compare.second = Operand(0, LowPtr());
		derived.Emit(compare);
		derived.EmitBranch(is_null, null_block, adjust_block);
		derived.SelectBlock(null_block);
		Instruction store_null(Instruction::STORE);
		store_null.type = LowPtr();
		store_null.first = Operand(0, LowPtr());
		store_null.second = result;
		derived.Emit(store_null);
		derived.EmitJump(end_block);
		derived.SelectBlock(adjust_block);
		const Operand projected = inverse_projection ?
			derived.ProjectBaseSubobjectAdjustment(address,
				-static_cast<std::int64_t>(projection_offset)) :
			derived.ProjectBaseSubobjects(address, projection_count, source.type,
				projection_offset, has_projection_offset);
		Instruction store_adjusted(Instruction::STORE);
		store_adjusted.type = LowPtr();
		store_adjusted.first = projected;
		store_adjusted.second = result;
		derived.Emit(store_adjusted);
		derived.EmitJump(end_block);
		derived.SelectBlock(end_block);
		return derived.LoadStorage(result, LowPtr());
	}

	Operand LowerReferenceCallArgument(std::uint32_t node, TypeId target)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& argument = derived.arena_.nodes[node];
		if (argument.category == VALUE_LVALUE ||
			argument.category == VALUE_XVALUE)
		{
			if (argument.kind == DUMP_TEMPORARY_OBJECT)
				return derived.LowerStorage(node);
			return derived.AddressOfStorage(derived.LowerStorage(node));
		}
		if (IsProjectedClassReference(node))
			return derived.LowerValue(node, LowPtr());
		if (argument.kind == DUMP_CALL_EXPRESSION &&
			derived.IsClassObjectType(argument.type) &&
			derived.UsesIndirectClassResult(argument.type, argument.binding))
		{
			const LowType type = derived.LowerStorageType(argument.type);
			const Operand slot(derived.EnsureGeneratedSlot(
				node, "arg", type), type);
			const Operand destination = derived.AddressOfStorage(slot);
			return derived.LowerCall(node, argument,
				derived.Children(node), destination);
		}
		const LowType type = derived.LowerExpressionType(target);
		const Operand slot(derived.EnsureGeneratedSlot(
			node, "refarg", type), type);
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = derived.LowerConvertedValue(node, type, false);
		store.second = slot;
		derived.Emit(store);
		return derived.AddressOfStorage(slot);
	}

	Operand LowerMemberValue(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const BindingId binding = record.binding;
		if (binding != kNoBinding &&
			derived.program_.bindings[binding].kind == BIND_FUNCTION)
		{
			if (children.size() != 1 ||
				binding >= derived.function_symbols_.size() ||
				derived.function_symbols_[binding] == kNoLowId)
				throw std::runtime_error(
					"invalid static member function expression");
			const std::string spelling =
				derived.program_.names.Get(record.text);
			if (spelling.compare(0, 8, "OP_ARROW") == 0)
				(void)derived.LowerValue(children[0], LowPtr());
			else derived.LowerDiscardedValue(children[0]);
			return derived.DecayAddress(derived.AddressOfStorage(Operand(
				Operand::FUNCTION, derived.function_symbols_[binding], LowPtr())));
		}
		const LowType type = derived.LowerExpressionType(record.type);
		if (record.constant) return Operand(record.constant_value, type);
		if (binding != kNoBinding &&
			derived.program_.bindings[binding].bit_field)
			return derived.LoadBitField(binding, derived.LowerStorage(node));
		return derived.LoadStorage(derived.LowerStorage(node), type);
	}
};

}
}

#endif
