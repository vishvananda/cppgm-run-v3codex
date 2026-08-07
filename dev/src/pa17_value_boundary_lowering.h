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
		return object.kind == pa11::TYPE_NAMED &&
			derived.program_.entities[object.entity].complete &&
			derived.program_.SizeOf(object_type) > 16;
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
			parameter.type = derived.LowerType(child.type);
			const pa11::TypeId* source_parameters =
				derived.program_.types.Parameters(record.type);
			parameter.reference = parameter_index < function_type.parameter_count &&
				derived.IsReferenceType(source_parameters[parameter_index]);
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
			parameter.type = derived.LowerType(source_parameters[parameter_index]);
			parameter.reference =
				derived.IsReferenceType(source_parameters[parameter_index]);
			pa15_lowering_abi::ApplyBuiltinParameterMetadata(
				&parameter, builtin, parameter_index);
			parameters->push_back(parameter);
			++parameter_index;
		}
	}
};

}
}
