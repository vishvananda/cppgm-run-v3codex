#ifndef CPPGM_LOWERING_LIFETIME_STATIC_STORAGE_H
#define CPPGM_LOWERING_LIFETIME_STATIC_STORAGE_H

#include "semantic/model/graph.h"
#include "lowering/ir/model.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"

#include <string>
#include <vector>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

template <class Derived>
class StaticLifecycleLowering
{
protected:
	void BindThreadLocalWrapper(SymbolId target, SymbolId wrapper)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.tls_access_wrapper_symbols_.size() <= target)
			derived.tls_access_wrapper_symbols_.resize(
				static_cast<std::size_t>(target) + 1, kNoLowId);
		if (derived.tls_access_wrapper_symbols_[target] != kNoLowId &&
			derived.tls_access_wrapper_symbols_[target] != wrapper)
			ThrowLoweringInternal(
				"thread-local storage has multiple access wrappers");
		derived.tls_access_wrapper_symbols_[target] = wrapper;
	}

	SymbolId AddExternalLifecycleFunction(const std::string& proposed,
		const std::string& object_name, const LowType& result,
		const std::vector<LowType>& parameters)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const SymbolId symbol = derived.AddSyntheticSymbol(
			Symbol::FUNCTION_SYMBOL, proposed, object_name, false);
		Symbol& record = derived.output_.symbols[symbol];
		record.c_linkage = true;
		record.declaration_emitted = true;
		record.referenced = true;
		FunctionDeclaration declaration;
		declaration.symbol = symbol;
		declaration.result = result;
		for (std::size_t i = 0; i < parameters.size(); ++i)
		{
			Parameter parameter;
			parameter.name = derived.output_.strings.intern(
				"arg" + std::to_string(i));
			parameter.type = parameters[i];
			declaration.parameters.push_back(parameter);
		}
		derived.output_.declarations.push_back(declaration);
		return symbol;
	}

	SymbolId EnsureLifecycleRegistrationRuntime(bool thread_local_object)
	{
		Derived& derived = static_cast<Derived&>(*this);
		SymbolId& symbol = thread_local_object ?
			derived.thread_atexit_runtime_symbol_ :
			derived.process_atexit_runtime_symbol_;
		if (symbol != kNoLowId) return symbol;
		std::vector<LowType> parameters;
		parameters.push_back(LowPtr());
		if (thread_local_object)
		{
			parameters.push_back(LowPtr());
			parameters.push_back(LowPtr());
		}
		symbol = AddExternalLifecycleFunction(thread_local_object ?
			"__cppgm_runtime_thread_atexit" : "__cppgm_runtime_atexit",
			thread_local_object ? "__cxa_thread_atexit" : "atexit",
			LowI32(), parameters);
		return symbol;
	}

	SymbolId EnsureDsoHandle()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.dso_handle_symbol_ != kNoLowId)
			return derived.dso_handle_symbol_;
		derived.dso_handle_symbol_ = derived.AddSyntheticSymbol(
			Symbol::GLOBAL_SYMBOL, "__cppgm_dso_handle", "__dso_handle", false);
		Symbol& record = derived.output_.symbols[derived.dso_handle_symbol_];
		record.declaration_emitted = true;
		record.referenced = true;
		GlobalDeclaration declaration;
		declaration.symbol = derived.dso_handle_symbol_;
		declaration.typed = false;
		derived.output_.global_declarations.push_back(declaration);
		return derived.dso_handle_symbol_;
	}

	SymbolId EmitStaticDestructorWrapper(const std::string& proposed,
		const DumpNode& action)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const SymbolId symbol = derived.AddSyntheticSymbol(
			Symbol::FUNCTION_SYMBOL, proposed, std::string(), true);
		Symbol& record = derived.output_.symbols[symbol];
		record.definition_emitted = true;
		record.nonthrowing = true;
		Function function;
		function.symbol = symbol;
		function.result = LowVoid();
		derived.BeginSyntheticFunction(&function);
		derived.LowerDestructorAction(action);
		derived.Emit(Instruction(Instruction::RETURN_VOID));
		derived.EndSyntheticFunction(function);
		derived.output_.functions.push_back(function);
		return symbol;
	}

	void EmitStaticDestructorRegistration(bool thread_local_object,
		SymbolId destructor, const Operand& object_address = Operand())
	{
		Derived& derived = static_cast<Derived&>(*this);
		const SymbolId runtime =
			EnsureLifecycleRegistrationRuntime(thread_local_object);
		derived.output_.symbols[destructor].referenced = true;
		CallArguments arguments;
		CallArgumentFlags passing;
		arguments.Push(Operand(Operand::FUNCTION, destructor, LowPtr()));
		passing.Push(Instruction::CALL_PASS_VALUE);
		if (thread_local_object)
		{
			if (object_address.kind == Operand::NONE)
				ThrowLoweringInternal(
					"thread-local destructor registration has no object");
			arguments.Push(object_address);
			passing.Push(Instruction::CALL_PASS_VALUE);
			const SymbolId dso = EnsureDsoHandle();
			arguments.Push(derived.AddressOfStorage(
				Operand(Operand::GLOBAL, dso, LowI8())));
			passing.Push(Instruction::CALL_PASS_VALUE);
		}
		Instruction call = derived.DirectCallInstruction(runtime, LowI32());
		const Operand ignored = derived.Temp(LowI32());
		call.dest = ignored.id;
		derived.AttachCallArguments(&call, arguments, passing);
		derived.Emit(call);
	}

	SymbolId AddThreadLocalWrapper(const std::string& proposed,
		const std::string& object_name, SymbolId target, bool internal)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const SymbolId symbol = derived.AddSyntheticSymbol(
			Symbol::FUNCTION_SYMBOL, proposed, object_name, internal);
		Symbol& wrapper_symbol = derived.output_.symbols[symbol];
		wrapper_symbol.declaration_emitted = true;
		wrapper_symbol.referenced = true;
		wrapper_symbol.weak_linkage = !internal;
		wrapper_symbol.tls_for_symbol = target;
		BindThreadLocalWrapper(target, symbol);
		FunctionDeclaration wrapper;
		wrapper.symbol = symbol;
		wrapper.result = LowPtr();
		derived.output_.declarations.push_back(wrapper);
		return symbol;
	}

	void EmitPresentedThreadLocalInitializers()
	{
		Derived& derived = static_cast<Derived&>(*this);
		for (std::size_t i = 0;
			i < derived.thread_local_declarations_.size(); ++i)
		{
			const SymbolId object = derived.thread_local_declarations_[i].first;
			AddThreadLocalWrapper("__cppgm_tls_wrapper__" +
				derived.output_.strings.get(
					derived.output_.symbols[object].name),
				derived.thread_local_declarations_[i].second, object,
				derived.output_.symbols[object].internal_linkage);
		}
		for (std::size_t i = 0; i < derived.thread_local_objects_.size(); ++i)
		{
			const std::uint32_t action_index =
				derived.thread_local_objects_[i].first;
			const NamespaceObjectAction& action =
				derived.graph_.namespace_objects[action_index];
			const bool dynamic =
				action_index < derived.thread_local_dynamic_.size() &&
				derived.thread_local_dynamic_[action_index] != 0;
			const SymbolId object = derived.global_symbols_[
				derived.program_.bindings[action.object].canonical];
			const std::string& internal = derived.output_.strings.get(
				derived.output_.symbols[object].name);
			SymbolId guard = kNoLowId;
			if (dynamic)
			{
				const std::string guard_name = "__cppgm_tls_guard__" + internal;
				guard = derived.AddSyntheticSymbol(Symbol::GLOBAL_SYMBOL,
					guard_name, std::string(), true);
				Symbol& record = derived.output_.symbols[guard];
				record.definition_emitted = true;
				record.referenced = true;
				record.thread_local_storage = true;
				Global definition;
				definition.symbol = guard;
				definition.type = LowI64();
				definition.initializer_kind = Global::ZERO;
				derived.output_.globals.push_back(definition);
				if (derived.stats_) ++derived.stats_->globals;
				AddThreadLocalWrapper("__cppgm_tls_wrapper__" + guard_name,
					std::string(), guard, true);
			}
			AddThreadLocalWrapper("__cppgm_tls_wrapper__" + internal,
				derived.thread_local_objects_[i].second, object,
				derived.output_.symbols[object].internal_linkage);
			if (!dynamic) continue;

			const SymbolId initializer = derived.AddSyntheticSymbol(
				Symbol::FUNCTION_SYMBOL, "__cppgm_tls_init__" + internal,
				std::string(), true);
			derived.output_.symbols[initializer].definition_emitted = true;
			Function function;
			function.symbol = initializer;
			function.result = LowVoid();
			derived.BeginSyntheticFunction(&function);
			const BlockId run = derived.AddBlock(
				derived.NewLabel("local_static_ctor_run"));
			const BlockId done = derived.AddBlock(
				derived.NewLabel("local_static_ctor_done"));
			const Operand value = derived.LoadStorage(
				Operand(Operand::GLOBAL, guard, LowI64()), LowI64());
			const Operand initialized = derived.Temp(LowI64());
			Instruction compare(Instruction::CMP);
			compare.dest = initialized.id;
			compare.op = LOW_OP_NE;
			compare.type = LowI64();
			compare.first = value;
			compare.second = Operand(0, LowI64());
			derived.Emit(compare);
			derived.EmitBranch(initialized, done, run);
			derived.SelectBlock(run);
			derived.lowering_namespace_object_ = true;
			derived.LowerStatementNode(action.variable);
			derived.lowering_namespace_object_ = false;
			Instruction mark(Instruction::STORE);
			mark.type = LowI64();
			mark.first = Operand(1, LowI64());
			mark.second = Operand(Operand::GLOBAL, guard, LowI64());
			derived.Emit(mark);
			derived.EmitJump(done);
			derived.SelectBlock(done);
			derived.Emit(Instruction(Instruction::RETURN_VOID));
			derived.EndSyntheticFunction(function);
			derived.output_.functions.push_back(function);
		}
	}

	void EmitThreadLocalInitializers()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.output_.host_object_emission)
		{
			EmitPresentedThreadLocalInitializers();
			return;
		}
		for (std::size_t i = 0;
			i < derived.thread_local_declarations_.size(); ++i)
		{
			const SymbolId object_symbol =
				derived.thread_local_declarations_[i].first;
			const std::string& wrapper_object =
				derived.thread_local_declarations_[i].second;
			const std::string& object_internal = derived.output_.strings.get(
				derived.output_.symbols[object_symbol].name);
			AddThreadLocalWrapper("__cppgm_tls_wrapper__" + object_internal,
				wrapper_object, object_symbol,
				derived.output_.symbols[object_symbol].internal_linkage);
		}
		for (std::size_t i = 0; i < derived.thread_local_objects_.size(); ++i)
		{
			const std::uint32_t action_index =
				derived.thread_local_objects_[i].first;
			const std::string& wrapper_object =
				derived.thread_local_objects_[i].second;
			const NamespaceObjectAction& action =
				derived.graph_.namespace_objects[action_index];
			const bool dynamic =
				action_index < derived.thread_local_dynamic_.size() &&
				derived.thread_local_dynamic_[action_index] != 0;
			const bool lifecycle = dynamic || action.destructor != kNoDumpEdge;
			const SymbolId object_symbol = derived.global_symbols_[
				derived.program_.bindings[action.object].canonical];
			const std::string& object_internal = derived.output_.strings.get(
				derived.output_.symbols[object_symbol].name);
			SymbolId guard_symbol = kNoLowId;
			if (lifecycle)
			{
				const std::string guard_name =
					"__cppgm_tls_guard__" + object_internal;
				guard_symbol = derived.AddSyntheticSymbol(Symbol::GLOBAL_SYMBOL,
					guard_name, std::string(), true);
				Symbol& guard_record = derived.output_.symbols[guard_symbol];
				guard_record.definition_emitted = true;
				guard_record.referenced = true;
				guard_record.thread_local_storage = true;
				Global guard;
				guard.symbol = guard_symbol;
				guard.type = LowI64();
				guard.initializer_kind = Global::ZERO;
				derived.output_.globals.push_back(guard);
				if (derived.stats_) ++derived.stats_->globals;
				AddThreadLocalWrapper("__cppgm_tls_wrapper__" + guard_name,
					std::string(), guard_symbol, true);
			}
			if (!lifecycle)
			{
				AddThreadLocalWrapper("__cppgm_tls_wrapper__" + object_internal,
					wrapper_object, object_symbol,
					derived.output_.symbols[object_symbol].internal_linkage);
				continue;
			}

			const SymbolId initializer_symbol = derived.AddSyntheticSymbol(
				Symbol::FUNCTION_SYMBOL, "__cppgm_tls_init__" + object_internal,
				std::string(), true);
			derived.output_.symbols[initializer_symbol].definition_emitted = true;
			const SymbolId wrapper_symbol = derived.AddSyntheticSymbol(
				Symbol::FUNCTION_SYMBOL, "__cppgm_tls_wrapper__" + object_internal,
				wrapper_object,
				derived.output_.symbols[object_symbol].internal_linkage);
			Symbol& wrapper_record = derived.output_.symbols[wrapper_symbol];
			wrapper_record.definition_emitted = true;
			wrapper_record.referenced = true;
			wrapper_record.weak_linkage = !wrapper_record.internal_linkage;
			wrapper_record.tls_for_symbol = object_symbol;
			BindThreadLocalWrapper(object_symbol, wrapper_symbol);
			SymbolId destructor_symbol = kNoLowId;
			if (action.destructor != kNoDumpEdge)
				destructor_symbol = EmitStaticDestructorWrapper(
					"__cppgm_tls_destructor__" + object_internal,
					derived.arena_.nodes[action.destructor]);

			Function initializer;
			initializer.symbol = initializer_symbol;
			initializer.result = LowVoid();
			derived.BeginSyntheticFunction(&initializer);
			const BlockId run = derived.AddBlock(
				derived.NewLabel("local_static_ctor_run"));
			const BlockId done = derived.AddBlock(
				derived.NewLabel("local_static_ctor_done"));
			const Operand guard_value = derived.LoadStorage(Operand(
				Operand::GLOBAL, guard_symbol, LowI64()), LowI64());
			const Operand initialized = derived.Temp(LowI64());
			Instruction compare(Instruction::CMP);
			compare.dest = initialized.id;
			compare.op = LOW_OP_NE;
			compare.type = LowI64();
			compare.first = guard_value;
			compare.second = Operand(0, LowI64());
			derived.Emit(compare);
			derived.EmitBranch(initialized, done, run);
			derived.SelectBlock(run);
			const SymbolId previous_tls_initializer =
				derived.lowering_thread_local_initializer_object_;
			derived.lowering_thread_local_initializer_object_ = object_symbol;
			if (dynamic)
			{
				derived.lowering_namespace_object_ = true;
				derived.LowerStatementNode(action.variable);
				derived.lowering_namespace_object_ = false;
			}
			if (destructor_symbol != kNoLowId)
				EmitStaticDestructorRegistration(true, destructor_symbol,
					derived.AddressOfStorage(Operand(Operand::GLOBAL, object_symbol,
						derived.LowerStorageType(action.type))));
			derived.lowering_thread_local_initializer_object_ =
				previous_tls_initializer;
			Instruction mark(Instruction::STORE);
			mark.type = LowI64();
			mark.first = Operand(1, LowI64());
			mark.second = Operand(Operand::GLOBAL, guard_symbol, LowI64());
			derived.Emit(mark);
			derived.EmitJump(done);
			derived.SelectBlock(done);
			derived.Emit(Instruction(Instruction::RETURN_VOID));
			derived.EndSyntheticFunction(initializer);
			derived.output_.functions.push_back(initializer);

			Function wrapper;
			wrapper.symbol = wrapper_symbol;
			wrapper.result = LowPtr();
			derived.BeginSyntheticFunction(&wrapper);
			Instruction initialize = derived.DirectCallInstruction(
				initializer_symbol, LowVoid());
			derived.Emit(initialize);
			const Operand address = derived.AddressOfStorage(Operand(
				Operand::GLOBAL, object_symbol,
				derived.LowerStorageType(action.type)));
			Instruction return_address(Instruction::RETURN_VALUE);
			return_address.type = LowPtr();
			return_address.first = address;
			derived.Emit(return_address);
			derived.EndSyntheticFunction(wrapper);
			derived.output_.functions.push_back(wrapper);
		}
	}
};

}
}

#endif
