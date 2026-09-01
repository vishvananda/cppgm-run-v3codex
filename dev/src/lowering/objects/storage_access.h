#pragma once

#include "lowering/ir/model.h"
#include "lowering/objects/storage_facts.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "semantic/model/graph.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace cppgm
{
namespace lowering
{

template <class Derived>
class StorageAccessLowering
{
protected:
	lowering::ir::Operand StorageFor(semantic::BindingId binding,
		const lowering::ir::LowType& type,
		semantic::NameId expression_name = 0)
	{
		using namespace semantic;
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.stats_) ++derived.stats_->binding_index_probes;
		if (binding < derived.binding_indirect_parameters_.size() &&
			derived.binding_indirect_parameters_[binding] != kNoLowId)
			return Operand(
				derived.binding_indirect_parameters_[binding], LowPtr());
		if (binding < derived.binding_slots_.size() &&
			derived.binding_slots_[binding] != kNoLowId)
			return Operand(derived.binding_slots_[binding], type);
		if (binding < derived.program_.bindings.size())
		{
			derived.RegisterAddressableStaticDataMember(
				binding, expression_name);
			binding = derived.program_.bindings[binding].canonical;
		}
		if (binding < derived.global_symbols_.size() &&
			derived.global_symbols_[binding] != kNoLowId)
		{
			const SymbolId global = derived.global_symbols_[binding];
			derived.output_.symbols[global].referenced = true;
			if (derived.output_.host_object_emission &&
				derived.output_.symbols[global].thread_local_storage &&
				global != derived.lowering_thread_local_initializer_object_)
			{
				if (global >= derived.tls_access_wrapper_symbols_.size() ||
					derived.tls_access_wrapper_symbols_[global] == kNoLowId)
					ThrowLoweringInternal(
						"thread-local storage has no access wrapper");
				const SymbolId wrapper =
					derived.tls_access_wrapper_symbols_[global];
				Instruction call =
					derived.DirectCallInstruction(wrapper, LowPtr());
				const Operand address = derived.Temp(LowPtr());
				call.dest = address.id;
				derived.Emit(call);
				return address;
			}
			return Operand(Operand::GLOBAL, global, type);
		}
		ThrowLoweringInternal("PA15 binding has no lowered storage: " +
			MissingStorageBindingDetail(derived.program_, binding));
	}

	bool BindingIsReference(semantic::BindingId binding) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		return binding < derived.program_.bindings.size() &&
			derived.IsReferenceType(derived.program_.bindings[binding].type);
	}

	bool TypeIsVolatile(semantic::TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		return type != semantic::kNoType &&
			(derived.program_.types.Get(type).cv & semantic::CV_VOLATILE) != 0;
	}

	lowering::ir::Operand LoadStorage(
		const lowering::ir::Operand& storage,
		const lowering::ir::LowType& type,
		bool volatile_access = false)
	{
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		const Operand result = derived.Temp(type);
		Instruction load(Instruction::LOAD);
		load.dest = result.id;
		load.type = type;
		load.first = storage;
		load.volatile_access = volatile_access;
		derived.Emit(load);
		return result;
	}

	lowering::ir::Operand AddressOfStorage(
		const lowering::ir::Operand& storage)
	{
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		if (storage.kind == Operand::INTEGER && storage.type.kind == LOW_PTR)
			return storage;
		if (storage.kind == Operand::TEMP ||
			(storage.kind == Operand::PARAMETER &&
			 storage.type.kind == LOW_PTR))
		{
			if (storage.type.kind != LOW_PTR)
				ThrowLoweringInternal(
					"PA15 indirect storage is not a pointer");
			return storage;
		}
		if (storage.kind == Operand::GLOBAL ||
			storage.kind == Operand::FUNCTION)
			derived.output_.symbols[storage.id].referenced = true;
		const Operand result = derived.Temp(LowPtr());
		Instruction address(Instruction::ADDR);
		address.dest = result.id;
		address.first = storage;
		derived.Emit(address);
		return result;
	}

	lowering::ir::Operand DecayAddress(
		const lowering::ir::Operand& address)
	{
		return address;
	}

	lowering::ir::Operand IndexAddress(
		const lowering::ir::LowType& element,
		const lowering::ir::Operand& base,
		const lowering::ir::Operand& offset,
		bool array_projection)
	{
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		const Operand result = derived.Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = result.id;
		index.type = element;
		index.first = base;
		index.second = offset;
		index.projection = array_projection ?
			INDEX_PROJECTION_ARRAY_ELEMENT : INDEX_PROJECTION_NONE;
		derived.Emit(index);
		return result;
	}

	lowering::ir::Operand LoadBlockInvoke(
		const lowering::ir::Operand& block)
	{
		using namespace lowering::ir;
		return LoadStorage(IndexAddress(LowI8(), block,
			Operand(16, LowI64()), false), LowPtr());
	}

	lowering::ir::Operand LowerArrayPointer(std::uint32_t node)
	{
		using namespace semantic;
		using namespace lowering::ir;
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[node];
		if (derived.IsArrayType(record.type))
		{
			if (record.kind == DUMP_LITERAL)
				return AddressOfStorage(derived.LowerStorage(node));
			return record.kind == DUMP_CONDITIONAL_EXPRESSION ||
				record.kind == DUMP_SUBSCRIPT_EXPRESSION ||
				record.kind == DUMP_CALL_EXPRESSION ?
				derived.LowerStorage(node) :
				AddressOfStorage(derived.LowerStorage(node));
		}
		return derived.LowerValue(node, LowPtr());
	}

	lowering::ir::Operand LowerStorage(std::uint32_t node)
	{
		using namespace semantic;
		using namespace lowering::ir;
		using namespace lowering::support;
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& record = derived.arena_.nodes[node];
		const NodeChildren children = derived.Children(node);
		Operand complex_storage;
		if (derived.TryLowerComplexStorage(
			node, record, children, &complex_storage))
			return complex_storage;
		if (record.kind == DUMP_TYPEID_EXPRESSION)
			return derived.LowerTypeid(record, children);
		if (record.kind == DUMP_DYNAMIC_CAST_EXPRESSION)
			return derived.LowerDynamicCast(node, record, children);
		if (record.kind == DUMP_SPECIAL_MEMBER_CONSTRUCTION_ACTION)
			return derived.LowerSpecialMemberConstruction(node);
		if (record.kind == DUMP_SPECIAL_MEMBER_ASSIGNMENT_ACTION)
			return derived.LowerSpecialMemberAssignment(node);
		if (record.kind == DUMP_STATEMENT_EXPRESSION)
			return derived.LowerStatementExpressionStorage(node, record);
		if (record.kind == DUMP_ID_EXPRESSION && record.binding != kNoBinding)
		{
			if (record.binding < derived.function_symbols_.size() &&
				derived.function_symbols_[record.binding] != kNoLowId)
				return Operand(Operand::FUNCTION,
					derived.function_symbols_[record.binding], LowPtr());
			if (record.binding <
					derived.binding_indirect_parameters_.size() &&
				derived.binding_indirect_parameters_[record.binding] != kNoLowId)
				return Operand(derived.binding_indirect_parameters_[record.binding],
					LowPtr());
			const Operand storage = StorageFor(record.binding,
				derived.LowerStorageType(
					derived.program_.bindings[record.binding].type),
				record.text);
			return BindingIsReference(record.binding) ?
				LoadStorage(storage, LowPtr()) : storage;
		}
		if (record.kind == DUMP_LITERAL && derived.IsArrayType(record.type))
			return Operand(Operand::GLOBAL,
				derived.static_initializers_.EnsureStringLiteral(node), LowPtr());
		if (record.kind == DUMP_TEMPORARY_OBJECT)
		{
			SmallSequence<BindingId, 8> path;
			return derived.LowerTemporaryObjectStorage(node, children, &path);
		}
		if (record.kind == DUMP_UNARY_EXPRESSION && children.size() == 1 &&
			record.OperationIs(OP_STAR))
			return derived.LowerValue(children[0], LowPtr());
		if (record.kind == DUMP_UNARY_EXPRESSION && children.size() == 1 &&
			(record.OperationIs(OP_INC) || record.OperationIs(OP_DEC)))
			return derived.LowerIncrement(record, children[0], true);
		if (record.kind == DUMP_SUBSCRIPT_EXPRESSION && children.size() == 2)
		{
			const Operand base = LowerArrayPointer(children[0]);
			Operand offset = derived.LowerValue(children[1]);
			if (derived.IsClassObjectType(record.type) ||
				derived.IsArrayType(record.type))
			{
				const std::size_t element_size =
					derived.program_.SizeOf(record.type);
				if (element_size != 1)
				{
					const Operand scaled = derived.Temp(LowI64());
					Instruction multiply(Instruction::BINARY);
					multiply.dest = scaled.id;
					multiply.op = LOW_OP_MUL;
					multiply.type = LowI64();
					multiply.first = offset;
					multiply.second = Operand(
						static_cast<std::int64_t>(element_size), LowI64());
					derived.Emit(multiply);
					offset = scaled;
				}
				return IndexAddress(LowI8(), base, offset, true);
			}
			return IndexAddress(
				derived.LowerExpressionType(record.type), base, offset, true);
		}
		if (record.kind == DUMP_MEMBER_EXPRESSION)
			return derived.MemberAddress(record, children);
		if (record.kind == DUMP_BINARY_EXPRESSION && children.size() == 2 &&
			record.OperationIs(OP_COMMA))
		{
			derived.LowerDiscardedValue(children[0]);
			return derived.LowerStorage(children[1]);
		}
		if (derived.IsMemberPointerApplication(record))
			return derived.LowerMemberPointerStorage(record, children);
		if (record.kind == DUMP_CONDITIONAL_EXPRESSION &&
			(record.category == VALUE_LVALUE ||
			 record.category == VALUE_XVALUE))
			return derived.LowerConditionalAddress(node, children);
		if (record.kind == DUMP_ASSIGNMENT_EXPRESSION)
			return derived.LowerAssignmentCore(record, children, true);
		if (record.kind == DUMP_CALL_EXPRESSION &&
			(derived.IsReferenceType(record.type) ||
			 derived.UsesIndirectClassResult(record.type, record.binding)))
			return derived.LowerCall(node, record, children);
		if (record.kind == DUMP_CALL_EXPRESSION &&
			derived.IsClassObjectType(record.type))
			return derived.MaterializeDirectClassCallStorage(
				node, record, children);
		if (record.kind == DUMP_CAST_EXPRESSION && children.size() == 1 &&
			(record.category == VALUE_LVALUE ||
			 record.category == VALUE_XVALUE ||
			 derived.arena_.nodes[children[0]].kind == DUMP_TEMPORARY_OBJECT))
		{
			if (record.base_projection_count != 0)
				return derived.LowerProjectedClassPointer(children[0],
					record.base_projection_count,
					record.base_projection_offset,
					record.has_base_projection_offset,
					derived.BaseEntityForType(record.type),
					record.inverse_base_projection);
			const Operand source =
				AddressOfStorage(derived.LowerStorage(children[0]));
			return derived.ProjectBaseSubobjects(source, 0,
				derived.arena_.nodes[children[0]].type);
		}
		ThrowLoweringSource("expression kind " +
			std::to_string(static_cast<unsigned>(record.kind)) +
			" does not designate scalar storage");
	}
};

}  // namespace lowering
}  // namespace cppgm
