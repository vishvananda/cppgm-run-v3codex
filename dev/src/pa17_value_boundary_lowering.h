#pragma once

#include "pa11_model.h"
#include "pa12_semantic_model.h"
#include "pa15_lowering_abi.h"
#include "pa15_lowering_support.h"
#include "pa15_lowir_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa17_lowering_detail
{

template <typename Derived>
class ValueBoundaryLowering
{
protected:
	bool IsClassValueBoundaryType(pa11::TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const pa11::TypeRecord& top = derived.program_.types.Get(type);
		return top.kind != pa11::TYPE_LVALUE_REFERENCE &&
			top.kind != pa11::TYPE_RVALUE_REFERENCE &&
			derived.IsClassObjectType(type);
	}

	bool FunctionHasClassValueBoundary(pa11::TypeId type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::uint32_t cached = 0;
		if (derived.class_value_boundary_types_.Find(type, &cached))
			return cached == 2;
		const pa11::TypeRecord& function = derived.program_.types.Get(type);
		bool boundary = function.kind == pa11::TYPE_FUNCTION &&
			IsClassValueBoundaryType(function.child);
		if (function.kind == pa11::TYPE_FUNCTION)
		{
			const pa11::TypeId* parameters =
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
			pa12_semantic_detail::DUMP_CALL_EXPRESSION) return false;
		const pa15_lowering_support::NodeChildren children =
			derived.Children(node);
		return !children.empty() &&
			derived.arena_.nodes[children[0]].kind ==
				pa12_semantic_detail::DUMP_CALLEE &&
			FunctionHasClassValueBoundary(
				derived.arena_.nodes[children[0]].type);
	}

	bool UsesIndirectClassResult(pa11::TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const pa11::TypeRecord& top = derived.program_.types.Get(type);
		if (top.kind == pa11::TYPE_LVALUE_REFERENCE ||
			top.kind == pa11::TYPE_RVALUE_REFERENCE ||
			!derived.IsClassObjectType(type))
			return false;
		const pa11::TypeId object_type = derived.ExpressionObjectType(type);
		const pa11::TypeRecord& object = derived.program_.types.Get(object_type);
		if (object.kind != pa11::TYPE_NAMED ||
			!derived.program_.entities[object.entity].complete)
			return false;
		const pa11::EntityRecord& entity =
			derived.program_.entities[object.entity];
		const std::size_t size = derived.program_.SizeOf(object_type);
		const bool dependent_empty_value = entity.empty_class &&
			entity.template_argument_count != 0 && (entity.enclosing_class ==
			pa11::kNoEntity || !entity.indirect_class_value_abi);
		return !dependent_empty_value && (size > 16 ||
			(size < 16 && entity.indirect_class_value_abi));
	}

	bool UsesIndirectClassParameter(pa11::TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const pa11::TypeRecord& top = derived.program_.types.Get(type);
		if (top.kind == pa11::TYPE_LVALUE_REFERENCE ||
			top.kind == pa11::TYPE_RVALUE_REFERENCE ||
			!derived.IsClassObjectType(type))
			return false;
		const pa11::TypeId object_type = derived.ExpressionObjectType(type);
		const pa11::TypeRecord& object =
			derived.program_.types.Get(object_type);
		if (object.kind != pa11::TYPE_NAMED ||
			!derived.program_.entities[object.entity].complete)
			return false;
		const std::size_t size = derived.program_.SizeOf(object_type);
		return size != 16 &&
			derived.program_.entities[object.entity].indirect_class_value_abi;
	}

	std::uint8_t BoundaryCallPassing(pa11::TypeId type) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (derived.IsReferenceType(type))
			return pa15_lowir_detail::Instruction::CALL_PASS_REFERENCE;
		return UsesIndirectClassParameter(type) ?
			pa15_lowir_detail::Instruction::CALL_PASS_BY_ADDRESS :
			pa15_lowir_detail::Instruction::CALL_PASS_VALUE;
	}

	void FillBoundary(std::uint32_t node,
		std::vector<pa15_lowir_detail::Parameter>* parameters,
		pa15_lowir_detail::LowType* result, bool* variadic) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const pa12_semantic_detail::DumpNode& record =
			derived.arena_.nodes[node];
		const pa11::TypeRecord& function_type =
			derived.program_.types.Get(record.type);
		const bool indirect_result =
			UsesIndirectClassResult(function_type.child);
		*result = indirect_result ? pa15_lowir_detail::LowVoid() :
			derived.LowerBoundaryResult(function_type.child);
		*variadic = function_type.variadic;
		if (indirect_result)
		{
			pa15_lowir_detail::Parameter parameter;
			parameter.name = "ret";
			parameter.type = pa15_lowir_detail::LowPtr();
			parameter.indirect_result = true;
			parameters->push_back(parameter);
		}
		const pa11::BuiltinFunctionKind builtin =
			record.binding == pa12_semantic_detail::kNoBinding ?
			pa11::BUILTIN_FUNCTION_NONE :
			derived.program_.bindings[record.binding].builtin_function;
		const pa15_lowering_support::NodeChildren children =
			derived.Children(node);
		std::size_t parameter_index = 0;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const pa12_semantic_detail::DumpNode& child =
				derived.arena_.nodes[children[i]];
			if (child.kind != pa12_semantic_detail::DUMP_PARAMETER) continue;
			pa15_lowir_detail::Parameter parameter;
			parameter.name = child.text == 0 ? std::string() :
				derived.program_.names.Get(child.text);
			if (parameter.name.empty()) parameter.name =
				(record.kind == pa12_semantic_detail::DUMP_FUNCTION_DECLARATION ?
					"arg" : "__param") + std::to_string(parameter_index);
			const pa11::TypeId* source_parameters =
				derived.program_.types.Parameters(record.type);
			const bool by_address =
				parameter_index < function_type.parameter_count &&
				UsesIndirectClassParameter(source_parameters[parameter_index]);
			parameter.type = by_address ? pa15_lowir_detail::LowPtr() :
				derived.LowerType(child.type);
			parameter.reference = parameter_index < function_type.parameter_count &&
				derived.IsReferenceType(source_parameters[parameter_index]);
			parameter.by_address = by_address;
			pa15_lowering_abi::ApplyBuiltinParameterMetadata(
				&parameter, builtin, parameter_index);
			parameters->push_back(parameter);
			++parameter_index;
		}
		const pa11::TypeId* source_parameters =
			derived.program_.types.Parameters(record.type);
		while (parameter_index < function_type.parameter_count)
		{
			pa15_lowir_detail::Parameter parameter;
			parameter.name =
				(record.kind == pa12_semantic_detail::DUMP_FUNCTION_DECLARATION ?
					"arg" : "__param") + std::to_string(parameter_index);
			const bool by_address =
				UsesIndirectClassParameter(source_parameters[parameter_index]);
			parameter.type = by_address ? pa15_lowir_detail::LowPtr() :
				derived.LowerType(source_parameters[parameter_index]);
			parameter.reference =
				derived.IsReferenceType(source_parameters[parameter_index]);
			parameter.by_address = by_address;
			pa15_lowering_abi::ApplyBuiltinParameterMetadata(
				&parameter, builtin, parameter_index);
			parameters->push_back(parameter);
			++parameter_index;
		}
	}

	void MaterializeBoundaryParameter(
		const pa12_semantic_detail::DumpNode& source,
		std::size_t parameter_index)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const pa15_lowir_detail::Parameter& parameter =
			derived.function_->parameters[parameter_index];
		if (parameter.by_address)
		{
			derived.binding_indirect_parameters_[source.binding] =
				pa15_lowir_detail::ParameterId(parameter_index);
			return;
		}
		if (derived.IsClassValueType(source.type))
		{
			const pa11::TypeRecord& object = derived.program_.types.Get(
				derived.ExpressionObjectType(source.type));
			if (derived.program_.entities[object.entity].empty_class) return;
			const pa15_lowir_detail::Operand value(
				pa15_lowir_detail::ParameterId(parameter_index), parameter.type);
			const pa15_lowir_detail::Operand destination =
				derived.AddressOfStorage(pa15_lowir_detail::Operand(
					derived.binding_slots_[source.binding],
					derived.LowerStorageType(source.type)));
			derived.EmitClassObjectCopy(source.type, value, destination);
			return;
		}
		pa15_lowir_detail::Instruction store(
			pa15_lowir_detail::Instruction::STORE);
		store.type = parameter.type;
		store.first = pa15_lowir_detail::Operand(
			pa15_lowir_detail::ParameterId(parameter_index), store.type);
		store.second = pa15_lowir_detail::Operand(
			derived.binding_slots_[source.binding], store.type);
		derived.Emit(store);
	}
};

}
}
