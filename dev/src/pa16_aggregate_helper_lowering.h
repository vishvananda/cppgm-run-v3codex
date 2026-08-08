#ifndef CPPGM_PA16_AGGREGATE_HELPER_LOWERING_H
#define CPPGM_PA16_AGGREGATE_HELPER_LOWERING_H

#include "pa15_lowering_support.h"
#include "pa15_lowir_model.h"
#include "pa12_semantic_model.h"

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
class AggregateHelperLowering
{
protected:
	void RegisterAggregateHelpers()
	{
		Derived& derived = static_cast<Derived&>(*this);
		for (std::size_t i = 0; i < derived.graph_.aggregate_helpers.size(); ++i)
		{
			const AggregateHelperInfo& helper = derived.graph_.aggregate_helpers[i];
			if (helper.entity >= derived.program_.entities.size())
				throw std::logic_error("aggregate helper has invalid entity");
			const EntityRecord& entity = derived.program_.entities[helper.entity];
			const TypeRecord& object = derived.program_.types.Get(
				derived.program_.types.RemoveTopCv(helper.object_type));
			if (object.kind != TYPE_NAMED || object.entity != helper.entity)
				throw std::logic_error("aggregate helper has invalid object type");
			const std::string proposed = SanitizeSymbol(
				derived.program_.names.Get(entity.name)) + "__" + SanitizeSymbol(
				derived.program_.names.Get(entity.identity_name)) + "__aggregate";
			derived.aggregate_helper_symbols_[i] = derived.AddSyntheticSymbol(
				Symbol::FUNCTION_SYMBOL, proposed, std::string(), false);
			derived.output_.symbols[
				derived.aggregate_helper_symbols_[i]].nonthrowing = true;
		}
	}

	void EmitAggregateHelpers()
	{
		Derived& derived = static_cast<Derived&>(*this);
		for (std::size_t i = 0; i < derived.graph_.aggregate_helpers.size(); ++i)
		{
			const AggregateHelperInfo& helper = derived.graph_.aggregate_helpers[i];
			const TypeRecord& function_type =
				derived.program_.types.Get(helper.function_type);
			const TypeId* source_parameters =
				derived.program_.types.Parameters(helper.function_type);
			if (function_type.kind != TYPE_FUNCTION ||
				function_type.parameter_count != helper.members.size() + 1 ||
				helper.member_constructors.size() != helper.members.size() ||
				helper.trivial_member_constructors.size() != helper.members.size())
				throw std::logic_error("aggregate helper has invalid function type");
			Function result;
			result.symbol = derived.aggregate_helper_symbols_[i];
			result.result = LowVoid();
			for (std::size_t p = 0; p < function_type.parameter_count; ++p)
			{
				Parameter parameter;
				parameter.by_address = p != 0 &&
					derived.UsesIndirectClassParameter(source_parameters[p]);
				parameter.type = parameter.by_address ? LowPtr() :
					derived.LowerType(source_parameters[p]);
				parameter.reference =
					derived.IsReferenceType(source_parameters[p]);
				parameter.name = p == 0 ? "this" : derived.program_.names.Get(
					derived.program_.bindings[helper.members[p - 1]].name);
				result.parameters.push_back(parameter);
				Slot slot;
				slot.name = parameter.name;
				slot.type = parameter.by_address ?
					derived.LowerStorageType(source_parameters[p]) :
					parameter.type;
				result.slots.push_back(slot);
			}
			derived.BeginSyntheticFunction(&result);
			for (std::size_t p = 0; p < result.parameters.size(); ++p)
			{
				if (result.parameters[p].by_address) continue;
				Instruction store(Instruction::STORE);
				store.type = result.parameters[p].type;
				store.first = Operand(static_cast<ParameterId>(p), store.type);
				store.second = Operand(static_cast<SlotId>(p), store.type);
				derived.Emit(store);
			}
			for (std::size_t m = 0; m < helper.members.size(); ++m)
			{
				const BindingId member = helper.members[m];
				const BindingId constructor = helper.member_constructors[m];
				if (constructor != kNoBinding)
				{
					const Operand object = derived.LoadStorage(
						Operand(static_cast<SlotId>(0), LowPtr()), LowPtr());
					const Operand destination =
						derived.ProjectAggregateMember(object, member);
					const Parameter& parameter = result.parameters[m + 1];
					const Operand source = parameter.by_address ?
						Operand(static_cast<ParameterId>(m + 1), LowPtr()) :
						derived.AddressOfStorage(Operand(
							static_cast<SlotId>(m + 1),
							result.slots[m + 1].type));
					if (helper.trivial_member_constructors[m])
						derived.EmitClassObjectCopy(
							derived.program_.bindings[member].type,
							source, destination);
					else
					{
						Instruction call(Instruction::CALL);
						call.type = LowVoid();
						call.first = Operand(Operand::FUNCTION,
							derived.function_symbols_[constructor], LowPtr());
						CallArguments arguments;
						CallArgumentFlags references;
						arguments.Push(destination);
						references.Push(0);
						arguments.Push(source);
						references.Push(1);
						derived.output_.symbols[
							derived.function_symbols_[constructor]].referenced = true;
						derived.AttachCallArguments(&call, arguments, references);
						derived.Emit(call);
					}
					continue;
				}
				const LowType value_type = result.parameters[m + 1].type;
				const Operand value = derived.LoadStorage(
					Operand(static_cast<SlotId>(m + 1), value_type), value_type);
				const Operand object = derived.LoadStorage(
					Operand(static_cast<SlotId>(0), LowPtr()), LowPtr());
				const Operand destination =
					derived.ProjectAggregateMember(object, member);
				if (derived.program_.bindings[member].bit_field)
					derived.InitializeBitField(member, value, destination,
						derived.LowerExpressionType(
							derived.program_.bindings[member].type));
				else
				{
					Instruction store(Instruction::STORE);
					store.type = value_type;
					store.first = value;
					store.second = destination;
					derived.Emit(store);
				}
			}
			derived.Emit(Instruction(Instruction::RETURN_VOID));
			derived.EndSyntheticFunction(result);
			derived.output_.functions.push_back(result);
			derived.output_.symbols[
				derived.aggregate_helper_symbols_[i]].definition_emitted = true;
		}
	}

	void LowerAggregateConstructionAction(std::uint32_t node,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& action = derived.arena_.nodes[node];
		if (action.kind != DUMP_AGGREGATE_CONSTRUCTION_ACTION ||
			action.aggregate_helper >= derived.graph_.aggregate_helpers.size() ||
			action.aggregate_helper >= derived.aggregate_helper_symbols_.size())
			throw std::logic_error("invalid aggregate helper action identity");
		const AggregateHelperInfo& helper =
			derived.graph_.aggregate_helpers[action.aggregate_helper];
		const TypeRecord& function_type =
			derived.program_.types.Get(helper.function_type);
		const TypeId* parameters =
			derived.program_.types.Parameters(helper.function_type);
		const NodeChildren children = derived.Children(node);
		if (function_type.kind != TYPE_FUNCTION ||
			function_type.parameter_count != children.size() + 1 ||
			children.size() != helper.members.size())
			throw std::logic_error("aggregate helper boundary mismatch");
		Instruction call(Instruction::CALL);
		call.type = LowVoid();
		call.first = Operand(Operand::FUNCTION,
			derived.aggregate_helper_symbols_[action.aggregate_helper], LowPtr());
		CallArguments arguments;
		CallArgumentFlags references;
		arguments.Push(destination);
		references.Push(0);
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const LowType expected = derived.LowerType(parameters[i + 1]);
			const bool reference =
				derived.IsReferenceType(parameters[i + 1]);
			if (!reference &&
				derived.IsClassValueType(parameters[i + 1]))
				arguments.Push(derived.LowerClassArgumentStaging(
					children[i], parameters[i + 1]));
			else if (reference)
				arguments.Push(derived.AddressOfStorage(
					derived.LowerStorage(children[i])));
			else arguments.Push(derived.Convert(
				derived.LowerValue(children[i]), expected));
			references.Push(reference ? 1 : 0);
		}
		derived.output_.symbols[
			derived.aggregate_helper_symbols_[action.aggregate_helper]].referenced = true;
		derived.AttachCallArguments(&call, arguments, references);
		derived.Emit(call);
	}
};

}
}

#endif
