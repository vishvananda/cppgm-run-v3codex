#pragma once

#include "semantic/model/program.h"
#include "semantic/model/graph.h"
#include "lowering/abi/emission_policy.h"
#include "lowering/abi/symbol_metadata.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "lowering/ir/types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cppgm
{
namespace lowering
{

template <typename Derived>
class ValueBoundaryLowering
{
protected:
	bool IsClassValueBoundaryType(semantic::TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const semantic::TypeRecord& top = derived.program_.types.Get(type);
		return top.kind != semantic::TYPE_LVALUE_REFERENCE &&
			top.kind != semantic::TYPE_RVALUE_REFERENCE &&
			derived.IsClassObjectType(type);
	}

	bool FunctionHasClassValueBoundary(semantic::TypeId type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::uint32_t cached = 0;
		if (derived.class_value_boundary_types_.Find(type, &cached))
			return cached == 2;
		const semantic::TypeRecord& function = derived.program_.types.Get(type);
		bool boundary = function.kind == semantic::TYPE_FUNCTION &&
			IsClassValueBoundaryType(function.child);
		if (function.kind == semantic::TYPE_FUNCTION)
		{
			const semantic::TypeId* parameters =
				derived.program_.types.Parameters(type);
			for (std::size_t i = 0; !boundary &&
				i < function.parameter_count; ++i)
				boundary = IsClassValueBoundaryType(parameters[i]);
		}
		derived.class_value_boundary_types_.Insert(type, boundary ? 2 : 1);
		return boundary;
	}

	bool CallHasClassValueBoundary(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.arena_.nodes[node].kind !=
			semantic::DUMP_CALL_EXPRESSION) return false;
		const lowering::support::NodeChildren children =
			derived.Children(node);
		return !children.empty() &&
			derived.arena_.nodes[children[0]].kind ==
				semantic::DUMP_CALLEE &&
			FunctionHasClassValueBoundary(
				derived.arena_.nodes[children[0]].type);
	}

	bool UsesIndirectClassResult(semantic::TypeId type,
		semantic::BindingId function = semantic::kNoBinding) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const semantic::TypeRecord& top = derived.program_.types.Get(type);
		if (top.kind == semantic::TYPE_LVALUE_REFERENCE ||
			top.kind == semantic::TYPE_RVALUE_REFERENCE)
			return false;
		if (derived.IsComplexObjectType(type)) return true;
		if (!derived.IsClassObjectType(type)) return false;
		const semantic::TypeId object_type = derived.ExpressionObjectType(type);
		const semantic::TypeRecord& object = derived.program_.types.Get(object_type);
		if (object.kind != semantic::TYPE_NAMED ||
			!derived.program_.entities[object.entity].complete)
			return false;
		const semantic::EntityRecord& entity =
			derived.program_.entities[object.entity];
		if (function != semantic::kNoBinding)
		{
			if (function >= derived.program_.bindings.size())
				ThrowLoweringInternal(
					"class-result boundary has an invalid callable owner");
			function = derived.program_.bindings[function].canonical;
		}
		const bool forced_indirect = function != semantic::kNoBinding &&
			derived.program_.bindings[function].force_indirect_class_result_abi;
		return forced_indirect || entity.indirect_class_result_abi;
	}

	bool UsesIndirectClassParameter(semantic::TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const semantic::TypeRecord& top = derived.program_.types.Get(type);
		if (top.kind == semantic::TYPE_LVALUE_REFERENCE ||
			top.kind == semantic::TYPE_RVALUE_REFERENCE)
			return false;
		if (derived.IsComplexObjectType(type)) return true;
		if (!derived.IsClassObjectType(type)) return false;
		const semantic::TypeId object_type = derived.ExpressionObjectType(type);
		const semantic::TypeRecord& object =
			derived.program_.types.Get(object_type);
		if (object.kind != semantic::TYPE_NAMED ||
			!derived.program_.entities[object.entity].complete)
			return false;
		const semantic::EntityRecord& entity =
			derived.program_.entities[object.entity];
		const bool aggregate_parameter = object.entity <
			derived.aggregate_parameter_entities_.size() &&
			derived.aggregate_parameter_entities_[object.entity] != 0;
		return entity.indirect_class_parameter_abi ||
			(aggregate_parameter && entity.indirect_class_value_abi);
	}

	std::uint8_t BoundaryCallPassing(semantic::TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (derived.IsReferenceType(type))
			return lowering::ir::Instruction::CALL_PASS_REFERENCE;
		return UsesIndirectClassParameter(type) ?
			lowering::ir::Instruction::CALL_PASS_BY_ADDRESS :
			lowering::ir::Instruction::CALL_PASS_VALUE;
	}

	std::size_t BoundaryObjectBytes(semantic::TypeId type,
		semantic::BindingId binding, std::size_t parameter_index,
		bool reference, bool by_address) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		semantic::TypeId object = semantic::kNoType;
		if (reference)
			object = derived.ExpressionObjectType(type);
		else if (by_address)
			object = type;
		else if (parameter_index == 0 && binding != semantic::kNoBinding)
		{
			const semantic::BindingRecord& function =
				derived.program_.bindings[binding];
			if (function.member_owner != semantic::kNoEntity &&
				!function.static_member_function)
			{
				const semantic::TypeId unqualified =
					derived.program_.types.RemoveTopCv(type);
				const semantic::TypeRecord& pointer =
					derived.program_.types.Get(unqualified);
				if (pointer.kind == semantic::TYPE_POINTER)
					object = pointer.child;
			}
		}
		if (object == semantic::kNoType ||
			!abi::IsCompleteBoundaryObject(derived.program_, object)) return 0;
		const lowering::ir::LowType storage = derived.LowerStorageType(object);
		return storage.kind == lowering::ir::LOW_INVALID ||
			storage.kind == lowering::ir::LOW_VOID ? 0 :
			static_cast<std::size_t>(storage.width / 8);
	}

	void FillBoundary(std::uint32_t node,
		std::vector<lowering::ir::Parameter>* parameters,
		lowering::ir::LowType* result, bool* variadic,
		bool declaration = false) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const semantic::DumpNode& record =
			derived.arena_.nodes[node];
		const semantic::TypeRecord& function_type =
			derived.program_.types.Get(record.type);
		const bool indirect_result =
			UsesIndirectClassResult(function_type.child, record.binding);
		*result = indirect_result ? lowering::ir::LowVoid() :
			derived.LowerBoundaryResult(function_type.child);
		*variadic = function_type.variadic;
		if (indirect_result)
		{
			lowering::ir::Parameter parameter;
			parameter.name = lowering::ir::InternLocalName(
				derived.output_, "ret");
			parameter.type = lowering::ir::LowPtr();
			parameter.indirect_result = true;
			const lowering::ir::LowType storage =
				derived.LowerStorageType(function_type.child);
			if (storage.kind != lowering::ir::LOW_INVALID &&
				storage.kind != lowering::ir::LOW_VOID)
				parameter.object_bytes =
					static_cast<std::size_t>(storage.width / 8);
			parameters->push_back(parameter);
		}
		const semantic::BuiltinFunctionKind builtin =
			record.binding == semantic::kNoBinding ?
			semantic::BUILTIN_FUNCTION_NONE :
				derived.program_.bindings[record.binding].builtin_function;
		const hosted_builtin::MemoryIntrinsicKind memory_builtin =
			record.binding == semantic::kNoBinding ?
				hosted_builtin::MEMORY_INTRINSIC_NONE :
				derived.program_.bindings[record.binding].
					hosted_memory_intrinsic;
		const semantic::TypeId* source_parameters =
			derived.program_.types.Parameters(record.type);
		bool copy_or_move_constructor = false;
		if (record.binding != semantic::kNoBinding &&
			function_type.parameter_count == 2)
		{
			const semantic::BindingRecord& binding =
				derived.program_.bindings[record.binding];
			if (binding.constructor && binding.member_owner != semantic::kNoEntity &&
				derived.IsReferenceType(source_parameters[1]))
			{
				const semantic::TypeRecord& source_object = derived.program_.types.Get(
					derived.ExpressionObjectType(source_parameters[1]));
				copy_or_move_constructor =
					source_object.kind == semantic::TYPE_NAMED &&
					source_object.entity == binding.member_owner;
			}
		}
		const lowering::support::NodeChildren children =
			derived.Children(node);
		std::size_t parameter_index = 0;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const semantic::DumpNode& child =
				derived.arena_.nodes[children[i]];
			if (child.kind != semantic::DUMP_PARAMETER) continue;
			lowering::ir::Parameter parameter;
			if (derived.output_.retain_local_names)
			{
				std::string name = child.text == 0 ? std::string() :
					derived.program_.names.Get(child.text);
				if (name.empty()) name = (declaration || record.kind ==
					semantic::DUMP_FUNCTION_DECLARATION ?
					"arg" : "__param") + std::to_string(parameter_index);
				parameter.name = lowering::ir::InternLocalName(
					derived.output_, name);
			}
			const bool by_address =
				parameter_index < function_type.parameter_count &&
				UsesIndirectClassParameter(source_parameters[parameter_index]);
			parameter.type = by_address ? lowering::ir::LowPtr() :
				derived.LowerType(child.type);
			parameter.reference = parameter_index < function_type.parameter_count &&
				derived.IsReferenceType(source_parameters[parameter_index]);
			parameter.by_address = by_address;
			if (parameter_index < function_type.parameter_count)
				parameter.object_bytes = BoundaryObjectBytes(
					source_parameters[parameter_index], record.binding,
					parameter_index, parameter.reference, by_address);
			lowering::abi::ApplyBuiltinParameterAliasMetadata(
				&parameter, builtin, memory_builtin, parameter_index);
			if (copy_or_move_constructor && parameter_index < 2)
				parameter.alias = lowering::ir::Parameter::ALIAS_NOALIAS;
			parameters->push_back(parameter);
			++parameter_index;
		}
		while (parameter_index < function_type.parameter_count)
		{
			lowering::ir::Parameter parameter;
			if (derived.output_.retain_local_names)
			{
				const std::string name =
					(declaration || record.kind ==
						semantic::DUMP_FUNCTION_DECLARATION ?
						"arg" : "__param") + std::to_string(parameter_index);
				parameter.name = lowering::ir::InternLocalName(
					derived.output_, name);
			}
			const bool by_address =
				UsesIndirectClassParameter(source_parameters[parameter_index]);
			parameter.type = by_address ? lowering::ir::LowPtr() :
				derived.LowerType(source_parameters[parameter_index]);
			parameter.reference =
				derived.IsReferenceType(source_parameters[parameter_index]);
			parameter.by_address = by_address;
			parameter.object_bytes = BoundaryObjectBytes(
				source_parameters[parameter_index], record.binding,
				parameter_index, parameter.reference, by_address);
			lowering::abi::ApplyBuiltinParameterAliasMetadata(
				&parameter, builtin, memory_builtin, parameter_index);
			if (copy_or_move_constructor && parameter_index < 2)
				parameter.alias = lowering::ir::Parameter::ALIAS_NOALIAS;
			parameters->push_back(parameter);
			++parameter_index;
		}
		derived.AppendVirtualBaseBoundaryParameters(node, parameters);
	}

	void MaterializeBoundaryParameter(
		const semantic::DumpNode& source,
		std::size_t parameter_index)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const lowering::ir::Parameter& parameter =
			derived.function_->parameters[parameter_index];
		if (parameter.by_address)
		{
			derived.binding_indirect_parameters_[source.binding] =
				lowering::ir::ParameterId(parameter_index);
			derived.function_slot_bindings_.push_back(source.binding);
			return;
		}
		if (derived.IsClassValueType(source.type))
		{
			const semantic::TypeRecord& object = derived.program_.types.Get(
				derived.ExpressionObjectType(source.type));
			if (derived.program_.entities[object.entity].empty_class) return;
			const lowering::ir::Operand value(
				lowering::ir::ParameterId(parameter_index), parameter.type);
			const lowering::ir::Operand destination =
				derived.AddressOfStorage(lowering::ir::Operand(
					derived.binding_slots_[source.binding],
					derived.LowerStorageType(source.type)));
			derived.EmitClassObjectCopy(source.type, value, destination);
			return;
		}
		lowering::ir::Instruction store(
			lowering::ir::Instruction::STORE);
		store.type = parameter.type;
		store.first = lowering::ir::Operand(
			lowering::ir::ParameterId(parameter_index), store.type);
		store.second = lowering::ir::Operand(
			derived.binding_slots_[source.binding], store.type);
		derived.Emit(store);
	}
};

}
}
