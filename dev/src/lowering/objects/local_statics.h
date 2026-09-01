#ifndef CPPGM_LOWERING_LIFETIME_LOCAL_STATICS_H
#define CPPGM_LOWERING_LIFETIME_LOCAL_STATICS_H

#include "semantic/model/graph.h"
#include "lowering/ir/model.h"
#include "lowering/abi/symbol_names.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"

#include <string>
#include <vector>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

inline std::string HexLocalStaticSymbolComponent(const std::string& value)
{
	static const char digits[] = "0123456789abcdef";
	std::string result;
	result.reserve(value.size() * 2);
	for (std::size_t i = 0; i < value.size(); ++i)
	{
		const unsigned char byte = static_cast<unsigned char>(value[i]);
		result.push_back(digits[byte >> 4]);
		result.push_back(digits[byte & 15]);
	}
	return result;
}

template <class Derived>
class LocalStaticLowering
{
protected:
	bool HasMemberFunctionPointerRelocation(std::uint32_t node) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		std::vector<std::uint32_t> pending(1, node);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const DumpNode& record = derived.arena_.nodes[current];
			const TypeRecord& type = derived.program_.types.Get(
				derived.program_.types.RemoveTopCv(record.type));
			if (type.kind == TYPE_MEMBER_POINTER &&
				derived.program_.types.IsFunction(type.child) &&
				record.binding != kNoBinding) return true;
			const NodeChildren children = derived.Children(current);
			for (std::size_t i = 0; i < children.size(); ++i)
				pending.push_back(children[i]);
		}
		return false;
	}

	std::string LocalStaticSymbolName(const LocalStaticObjectAction& action,
		bool weak, bool presentation) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (action.function >= derived.function_symbols_.size() ||
			derived.function_symbols_[action.function] == kNoLowId)
			ThrowLoweringInternal(
				"local static function has no emission symbol");
		const Symbol& owner = derived.output_.symbols[
			derived.function_symbols_[action.function]];
		std::string function_identity =
			derived.output_.strings.get(owner.name);
		if (weak)
		{
			const std::string& object = derived.output_.strings.get(
				owner.object_name.valid() ? owner.object_name : owner.name);
			function_identity = "function_symbol_" +
				HexLocalStaticSymbolComponent(object);
		}
		const std::string object_name = abi::NormalizeSymbolName(
			derived.program_.names.Get(
				derived.program_.bindings[action.object].name));
		std::string name = "__local_static__" + function_identity + "__";
		if (presentation && weak && action.source_identity_presentation &&
			action.source_file != 0 && action.source_line != 0 &&
			action.source_column != 0)
		{
			const std::string source = " at " +
				derived.program_.names.Get(action.source_file) + ":" +
				std::to_string(action.source_line) + ":" +
				std::to_string(action.source_column);
			name += object_name + "__source" +
				HexLocalStaticSymbolComponent(source);
		}
		else if (presentation && !weak && action.initializer != kNoDumpEdge &&
			(derived.arena_.nodes[action.initializer].contains_temporary_object ||
			 HasMemberFunctionPointerRelocation(action.initializer)) &&
			action.source_token_last >= action.source_token_first)
			name += object_name + "__tokens" +
				std::to_string(action.source_token_first) + "_" +
				std::to_string(action.source_token_last);
		else name += "decl" + std::to_string(action.declaration_ordinal) +
			"__" + object_name;
		return name;
	}

	void RegisterLocalStaticObjects()
	{
		Derived& derived = static_cast<Derived&>(*this);
		for (std::size_t i = 0;
			i < derived.graph_.local_static_objects.size(); ++i)
		{
			const LocalStaticObjectAction& action =
				derived.graph_.local_static_objects[i];
			if (action.function >= derived.function_symbols_.size() ||
				derived.function_symbols_[action.function] == kNoLowId ||
				action.function >= derived.function_definition_.size() ||
				derived.function_definition_[action.function] == kNoDumpEdge)
				continue;
			const BindingRecord& function =
				derived.program_.bindings[action.function];
			const Symbol& owner = derived.output_.symbols[
				derived.function_symbols_[action.function]];
			const bool weak = !owner.internal_linkage &&
				(function.weak_odr || function.template_argument_count != 0);
			const std::string name =
				LocalStaticSymbolName(action, weak, true);
			const std::string object_name = weak ?
				(derived.output_.host_object_emission ? std::string() : "@") +
					LocalStaticSymbolName(action, weak, false) : std::string();
			const SymbolId symbol = derived.AddSyntheticSymbol(
				Symbol::GLOBAL_SYMBOL, name,
				object_name, !weak);
			Symbol& record = derived.output_.symbols[symbol];
			record.weak_linkage = weak;
			record.definition_emitted = true;
			record.thread_local_storage = derived.output_.host_object_emission &&
				derived.program_.bindings[action.object].thread_local_storage;
			derived.global_symbols_[action.object] = symbol;
			derived.local_static_emitted_[i] = 1;
		}
	}

	void EmitLocalStaticGlobals()
	{
		Derived& derived = static_cast<Derived&>(*this);
		for (std::size_t i = 0;
			i < derived.graph_.local_static_objects.size(); ++i)
		{
			if (!derived.local_static_emitted_[i]) continue;
			const LocalStaticObjectAction& action =
				derived.graph_.local_static_objects[i];
			const SymbolId symbol = derived.global_symbols_[action.object];
			Global global;
			global.symbol = symbol;
			const DumpNode& variable = derived.arena_.nodes[action.variable];
			global.type = derived.LowerVariableStorage(variable);
			const NamespaceObjectAction initializer(action.object, action.type,
				action.variable, action.initializer, action.destructor);
			bool static_initialized =
				derived.SetExplicitVariableZero(variable, &global);
			if (!static_initialized && derived.IsReferenceType(action.type))
			{
				derived.static_initializers_.SetZero(action.type, &global);
				if (derived.static_initializers_.HasConstantAddress(
					action.initializer))
					derived.local_static_eager_initializers_.push_back(
						static_cast<std::uint32_t>(i));
			}
			else if (!static_initialized &&
				(!derived.IsClassObjectType(action.type) ||
				(action.constant_initialized &&
				 !action.specialization_owned_recipe)))
				static_initialized = derived.static_initializers_.Lower(
					initializer, false, &global,
					&derived.needs_global_class_initializer_);
			if (!static_initialized && !derived.IsReferenceType(action.type))
				derived.static_initializers_.SetZero(action.type, &global);
			const bool eager =
				!derived.local_static_eager_initializers_.empty() &&
				derived.local_static_eager_initializers_.back() == i;
			const bool dynamic = !static_initialized && !eager;
			const bool thread_local_object =
				derived.output_.host_object_emission &&
				derived.program_.bindings[action.object].thread_local_storage;
			if (thread_local_object)
				derived.AddThreadLocalWrapper(
					"__cppgm_tls_wrapper__" +
						derived.output_.strings.get(
							derived.output_.symbols[symbol].name),
					std::string(), symbol, true);
			if (dynamic)
			{
				derived.local_static_dynamic_[i] = 1;
				const bool weak = derived.output_.symbols[symbol].weak_linkage;
				const std::string guard_name = derived.output_.strings.get(
					derived.output_.symbols[symbol].name) + "__guard";
				const lowir_model::StringId object_name_id =
					derived.output_.symbols[symbol].object_name;
				const std::string guard_object_name = weak &&
					object_name_id.valid() ? derived.output_.strings.get(
						object_name_id) + "__guard" :
					std::string();
				const SymbolId guard_symbol = derived.AddSyntheticSymbol(
					Symbol::GLOBAL_SYMBOL, guard_name,
					guard_object_name, !weak);
				Symbol& guard_record = derived.output_.symbols[guard_symbol];
				guard_record.weak_linkage = weak;
				guard_record.definition_emitted = true;
				guard_record.referenced = true;
				guard_record.thread_local_storage = thread_local_object;
				derived.local_static_guard_symbols_[i] = guard_symbol;
				if (thread_local_object)
					derived.AddThreadLocalWrapper(
						"__cppgm_tls_wrapper__" + guard_name,
						std::string(), guard_symbol, true);
			}
			derived.output_.globals.push_back(global);
			if (derived.output_.host_object_emission && dynamic &&
				action.destructor != kNoDumpEdge)
				derived.local_static_destructor_symbols_[i] =
					derived.EmitStaticDestructorWrapper(
						"__cppgm_local_static_destructor__" +
							derived.output_.strings.get(
								derived.output_.symbols[symbol].name),
						derived.arena_.nodes[action.destructor]);
			else if (action.destructor != kNoDumpEdge)
				derived.local_static_finalizers_.push_back(
					static_cast<std::uint32_t>(i));
			if (derived.stats_) ++derived.stats_->globals;
			if (dynamic)
			{
				Global guard;
				guard.symbol = derived.local_static_guard_symbols_[i];
				guard.type = LowI64();
				guard.initializer_kind = Global::ZERO;
				derived.output_.globals.push_back(guard);
				if (derived.stats_) ++derived.stats_->globals;
			}
		}
	}

	void EmitDynamicInitializer()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.namespace_initializers_.empty() &&
			derived.local_static_eager_initializers_.empty() &&
			!derived.needs_global_class_initializer_) return;
		const std::string proposed = "__cppgm_init";
		const SymbolId symbol =
			static_cast<SymbolId>(derived.output_.symbols.size());
		derived.output_.symbols.push_back(Symbol(Symbol::FUNCTION_SYMBOL,
			derived.output_.InternUniqueSymbolName(proposed),
			lowir_model::StringId(),
			false, true, false));
		derived.output_.symbols.back().definition_emitted = true;

		Function result;
		result.symbol = symbol;
		result.result = LowVoid();
		result.initializer = true;
		derived.BeginSyntheticFunction(&result);
		derived.lowering_namespace_object_ = true;
		for (std::size_t i = 0; i < derived.namespace_initializers_.size(); ++i)
		{
			const std::uint32_t action_index =
				derived.namespace_initializers_[i].first;
			const NamespaceObjectAction& action =
				derived.graph_.namespace_objects[action_index];
			if (derived.namespace_initializers_[i].second)
				derived.LowerStatementNode(action.variable);
			else
			{
				const DumpNode& variable = derived.arena_.nodes[action.variable];
				derived.AddressOfStorage(derived.StorageFor(action.object,
					derived.LowerVariableStorage(variable)));
			}
		}
		for (std::size_t i = 0;
			i < derived.local_static_eager_initializers_.size(); ++i)
		{
			const LocalStaticObjectAction& action =
				derived.graph_.local_static_objects[
					derived.local_static_eager_initializers_[i]];
			derived.LowerVariableInitializationCore(
				derived.arena_.nodes[action.variable],
				derived.Children(action.variable));
		}
		derived.lowering_namespace_object_ = false;
		derived.Emit(Instruction(Instruction::RETURN_VOID));
		derived.EndSyntheticFunction(result);
		derived.output_.functions.push_back(result);
	}

	void EmitDynamicFinalizer()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.dynamic_finalizers_.empty() &&
			derived.local_static_finalizers_.empty()) return;
		const std::string proposed = "__cppgm_fini";
		const SymbolId symbol =
			static_cast<SymbolId>(derived.output_.symbols.size());
		derived.output_.symbols.push_back(Symbol(Symbol::FUNCTION_SYMBOL,
			derived.output_.InternUniqueSymbolName(proposed),
			lowir_model::StringId(),
			false, true, false));
		derived.output_.symbols.back().definition_emitted = true;

		Function result;
		result.symbol = symbol;
		result.result = LowVoid();
		result.finalizer = true;
		derived.BeginSyntheticFunction(&result);
		for (std::size_t i = derived.local_static_finalizers_.size();
			i != 0; --i)
		{
			const std::uint32_t action_index =
				derived.local_static_finalizers_[i - 1];
			if (!derived.local_static_dynamic_[action_index]) continue;
			const LocalStaticObjectAction& action =
				derived.graph_.local_static_objects[action_index];
			const SymbolId guard_symbol =
				derived.local_static_guard_symbols_[action_index];
			if (guard_symbol == kNoLowId)
				ThrowLoweringInternal(
					"dynamic local static finalizer has no guard symbol");
			const BlockId destroy =
				derived.AddBlock(derived.NewLabel("local_static_destroy"));
			const BlockId next =
				derived.AddBlock(derived.NewLabel("local_static_fini_next"));
			const Operand guard = derived.LoadStorage(
				Operand(Operand::GLOBAL, guard_symbol, LowI64()), LowI64());
			const Operand initialized = derived.Temp(LowI64());
			Instruction compare(Instruction::CMP);
			compare.dest = initialized.id;
			compare.op = LOW_OP_NE;
			compare.type = LowI64();
			compare.first = guard;
			compare.second = Operand(0, LowI64());
			derived.Emit(compare);
			derived.EmitBranch(initialized, destroy, next);
			derived.SelectBlock(destroy);
			derived.LowerDestructorAction(
				derived.arena_.nodes[action.destructor]);
			derived.EmitJump(next);
			derived.SelectBlock(next);
		}
		for (std::size_t i = derived.dynamic_finalizers_.size(); i != 0; --i)
		{
			const NamespaceObjectAction& action =
				derived.graph_.namespace_objects[
					derived.dynamic_finalizers_[i - 1]];
			if (action.initializer_list_backing != kNoDumpEdge)
				derived.LowerNamespaceInitializerListBackingDestructor(action);
			else derived.LowerDestructorAction(
				derived.arena_.nodes[action.destructor]);
		}
		for (std::size_t i = derived.local_static_finalizers_.size();
			i != 0; --i)
		{
			const std::uint32_t action_index =
				derived.local_static_finalizers_[i - 1];
			if (derived.local_static_dynamic_[action_index]) continue;
			const LocalStaticObjectAction& action =
				derived.graph_.local_static_objects[action_index];
			derived.LowerDestructorAction(
				derived.arena_.nodes[action.destructor]);
		}
		derived.Emit(Instruction(Instruction::RETURN_VOID));
		derived.EndSyntheticFunction(result);
		derived.output_.functions.push_back(result);
	}

	void LowerLocalStaticVariable(std::uint32_t action_index,
		const DumpNode& record, const NodeChildren& children,
		const NodeChildren* declaration_children = 0)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (action_index >= derived.graph_.local_static_objects.size() ||
			!derived.local_static_emitted_[action_index])
			ThrowLoweringInternal("invalid emitted local static action");
		if (!derived.local_static_dynamic_[action_index]) return;
		const SymbolId guard_symbol =
			derived.local_static_guard_symbols_[action_index];
		if (guard_symbol == kNoLowId)
			ThrowLoweringInternal("dynamic local static has no guard symbol");
		const BlockId ready =
			derived.AddBlock(derived.NewLabel("local_static_ready"));
		const BlockId initialize =
			derived.AddBlock(derived.NewLabel("local_static_init"));
		if (derived.full_expression_cleanup_active_)
			derived.PauseFullExpressionCleanupSegment();
		const Operand guard = derived.LoadStorage(
			Operand(Operand::GLOBAL, guard_symbol, LowI64()), LowI64());
		const Operand initialized = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = initialized.id;
		compare.op = LOW_OP_NE;
		compare.type = LowI64();
		compare.first = guard;
		compare.second = Operand(0, LowI64());
		derived.Emit(compare);
		derived.EmitBranch(initialized, ready, initialize);
		derived.SelectBlock(initialize);
		const bool previous_namespace_object =
			derived.lowering_namespace_object_;
		derived.lowering_namespace_object_ = false;
		Operand retained_destination;
		if (derived.IsReferenceType(record.type) ||
			(!derived.IsClassObjectType(record.type) &&
			 !derived.IsArrayType(record.type)))
			retained_destination = derived.AddressOfStorage(derived.StorageFor(
				record.binding, derived.LowerStorageType(record.type)));
		if (declaration_children == 0 || declaration_children->size() <= 1)
			derived.LowerVariableInitializationCore(
				record, children, retained_destination);
		else
		{
			derived.BeginFullExpressionCleanup(*declaration_children, 1, true);
			derived.LowerVariableInitializationCore(
				record, children, retained_destination);
			derived.CompleteFullExpressionCleanup();
		}
		derived.lowering_namespace_object_ = previous_namespace_object;
		const SymbolId destructor =
			derived.local_static_destructor_symbols_[action_index];
		if (destructor != kNoLowId)
		{
			const LocalStaticObjectAction& action =
				derived.graph_.local_static_objects[action_index];
			const bool thread_local_object =
				derived.program_.bindings[action.object].thread_local_storage;
			derived.EmitStaticDestructorRegistration(thread_local_object,
				destructor, thread_local_object ?
					derived.AddressOfStorage(derived.StorageFor(action.object,
						derived.LowerStorageType(action.type))) : Operand());
		}
		Instruction mark(Instruction::STORE);
		mark.type = LowI64();
		mark.first = Operand(1, LowI64());
		mark.second = Operand(Operand::GLOBAL, guard_symbol, LowI64());
		derived.Emit(mark);
		derived.EmitJump(ready);
		derived.SelectBlock(ready);
	}
};

}
}

#endif
