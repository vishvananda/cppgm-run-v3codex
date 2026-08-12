#ifndef CPPGM_PA26_EXCEPTION_LOWERING_H
#define CPPGM_PA26_EXCEPTION_LOWERING_H

#include "pa12_semantic_model.h"
#include "pa15_lowir_model.h"
#include "pa15_lowering_support.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa26_lowering_detail
{

using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

template <class Derived>
class ExceptionLowering
{
protected:
	ExceptionLowering()
		: handler_selector_epoch_(0), next_handler_selector_(1) {}

	enum ExceptionRegion : std::uint8_t
	{
		EXCEPTION_TRY_REGION,
		EXCEPTION_HANDLER_REGION
	};
	struct ExceptionRegionState
	{
		ExceptionRegion kind;
		std::uint32_t handler;
		BlockId entry;
		ExceptionRegionState(ExceptionRegion kind_value,
			std::uint32_t handler_value = kNoDumpEdge,
			BlockId entry_value = kNoLowId)
			: kind(kind_value), handler(handler_value), entry(entry_value) {}
	};

	void ResetExceptionFunctionState()
	{
		Derived& derived = static_cast<Derived&>(*this);
		active_exception_regions_.clear();
		const std::size_t required = derived.arena_.nodes.size();
		if (handler_selectors_.size() < required)
		{
			if (derived.stats_)
				derived.stats_->exception_selector_table_growth +=
					required - handler_selectors_.size();
			handler_selectors_.resize(required, 0);
			handler_selector_epochs_.resize(required, 0);
		}
		if (++handler_selector_epoch_ == 0)
		{
			// Epoch wrap is the only full-table reset. It cannot occur during any
			// practical translation unit, but retaining the path keeps identity
			// reuse correct without imposing TU-sized work on every function.
			handler_selector_epochs_.assign(required, 0);
			handler_selector_epoch_ = 1;
		}
		next_handler_selector_ = 1;
		if (derived.stats_) ++derived.stats_->exception_selector_resets;
	}

	std::uint32_t ExceptionCleanupContext() const
	{
		if (active_exception_regions_.empty()) return 0;
		const ExceptionRegionState& region = active_exception_regions_.back();
		if (region.handler >= (kNoDumpEdge - 2) / 2)
			throw std::logic_error(
				"exception cleanup context identity is out of range");
		return (region.handler + 1) * 2 +
			(region.kind == EXCEPTION_HANDLER_REGION ? 1 : 0);
	}

	bool BeginExceptionTryCleanupDispatch()
	{
		if (active_exception_regions_.empty() ||
			active_exception_regions_.back().kind != EXCEPTION_TRY_REGION)
			return false;
		Derived& derived = static_cast<Derived&>(*this);
		EmitHandlerClause(active_exception_regions_.back().handler);
		derived.Emit(Instruction(Instruction::EH_CLEANUP));
		return true;
	}

	void FinishExceptionCleanupDispatch(bool routes_to_try,
		bool closes_cleanup_region = true)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!routes_to_try)
		{
			derived.Emit(Instruction(Instruction::RESUME));
			return;
		}
		if (active_exception_regions_.empty() ||
			active_exception_regions_.back().kind != EXCEPTION_TRY_REGION)
			throw std::logic_error(
				"exception cleanup lost its source try region");
		if (closes_cleanup_region)
			derived.Emit(Instruction(Instruction::EH_END));
		derived.Emit(Instruction(Instruction::EH_END));
		derived.EmitJump(active_exception_regions_.back().entry);
	}

	void FinishExceptionUnwindCleanupPrefix()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (active_exception_regions_.empty() ||
			active_exception_regions_.back().kind != EXCEPTION_HANDLER_REGION)
			return;
		for (std::size_t i = 0;
			i < derived.full_expression_segment_actions_.size(); ++i)
			if (derived.arena_.nodes[
				derived.full_expression_segment_actions_[i]].exception_handler_exit)
				return;
		derived.Emit(Instruction(Instruction::EH_END));
		CallArguments none;
		(void)EmitExceptionRuntimeCall(
			derived.polymorphism_.eh_end_catch_symbol, LowVoid(), none);
		derived.Emit(Instruction(Instruction::EH_END));
	}

	void FinishExceptionHandlerUnwindBoundary(bool closes_cleanup_region)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (closes_cleanup_region)
			derived.Emit(Instruction(Instruction::EH_END));
		CallArguments none;
		(void)EmitExceptionRuntimeCall(
			derived.polymorphism_.eh_end_catch_symbol, LowVoid(), none);
		derived.Emit(Instruction(Instruction::EH_END));
	}

	std::size_t BeginExceptionControlExit()
	{
		Derived& derived = static_cast<Derived&>(*this);
		CallArguments none;
		std::size_t closed = 0;
		if (!active_exception_regions_.empty() &&
			active_exception_regions_.back().kind == EXCEPTION_HANDLER_REGION)
		{
			derived.Emit(Instruction(Instruction::EH_END));
			(void)EmitExceptionRuntimeCall(
				derived.polymorphism_.eh_end_catch_symbol, LowVoid(), none);
			++closed;
		}
		return closed;
	}

	template <class Task>
	bool RunExceptionStatementTask(const Task& task)
	{
		if (task.kind == Derived::STATEMENT_TRY_AFTER_BODY)
		{
			FinishTryBody(task.node, task.auxiliary,
				task.first, task.second, task.third);
			return true;
		}
		if (task.kind != Derived::STATEMENT_HANDLER_AFTER_BODY) return false;
		FinishHandlerBody(task.node, task.auxiliary,
			task.first, task.second, task.third);
		return true;
	}

	bool TryLowerExceptionStatement(std::uint32_t node,
		const DumpNode& record, const NodeChildren& children)
	{
		if (record.kind == DUMP_TRY_STATEMENT)
		{
			StartTryStatement(node, children);
			return true;
		}
		if (record.kind != DUMP_THROW_EXPRESSION) return false;
		(void)LowerThrowExpression(node, record, children);
		return true;
	}

	Operand EmitExceptionRuntimeCall(SymbolId symbol, const LowType& result,
		const CallArguments& arguments)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (symbol == kNoLowId)
			throw std::logic_error("exception runtime support was not emitted");
		derived.output_.symbols[symbol].referenced = true;
		Instruction call(Instruction::CALL);
		call.type = result;
		call.first = Operand(Operand::FUNCTION, symbol, LowPtr());
		CallArgumentFlags passing;
		for (std::size_t i = 0; i < arguments.size(); ++i)
			passing.Push(Instruction::CALL_PASS_VALUE);
		derived.AttachCallArguments(&call, arguments, passing);
		if (result.kind == LOW_VOID)
		{
			derived.Emit(call);
			return Operand(0, LowVoid());
		}
		const Operand value = derived.Temp(result);
		call.dest = value.id;
		derived.Emit(call);
		return value;
	}

	SymbolId ExceptionRttiSymbol(TypeId requested) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		const TypeId type = derived.CanonicalRttiType(requested);
		if (type >= derived.polymorphism_.type_rtti_symbols.size())
			throw std::logic_error("exception RTTI type is out of range");
		const SymbolId external =
			type < derived.polymorphism_.exception_rtti_symbols.size() ?
			derived.polymorphism_.exception_rtti_symbols[type] : SymbolId(kNoLowId);
		const SymbolId symbol = external != kNoLowId ? external :
			derived.polymorphism_.type_rtti_symbols[type];
		if (symbol == kNoLowId)
			throw std::logic_error("exception RTTI fact has no symbol");
		return symbol;
	}

	Operand ExceptionRttiAddress(TypeId type)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const SymbolId symbol = ExceptionRttiSymbol(type);
		derived.output_.symbols[symbol].referenced = true;
		const Operand result = derived.Temp(LowPtr());
		Instruction address(Instruction::ADDR);
		address.dest = result.id;
		address.first = Operand(Operand::GLOBAL, symbol, LowPtr());
		derived.Emit(address);
		return result;
	}

	Operand LowerThrowExpression(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		CallArguments arguments;
		if (children.empty())
		{
			(void)EmitExceptionRuntimeCall(
				derived.polymorphism_.eh_rethrow_symbol, LowVoid(), arguments);
			derived.EmitNoreturnFallback();
			return Operand(0, LowVoid());
		}
		if (children.empty() || record.operand_type == kNoType)
			throw std::logic_error("invalid typed throw expression");
		const bool owns_cleanup = children.size() != 1 &&
			!derived.full_expression_cleanup_active_;
		if (owns_cleanup)
			derived.BeginFullExpressionCleanup(children, 1, true);
		if (owns_cleanup) derived.EnsureFullExpressionCleanupSegment();
		arguments.Push(Operand(static_cast<std::int64_t>(
			derived.program_.SizeOf(record.operand_type)), LowI64()));
		Operand object = EmitExceptionRuntimeCall(
			derived.polymorphism_.eh_allocate_exception_symbol,
			LowPtr(), arguments);
		if (owns_cleanup)
		{
			const Operand retained(derived.EnsureGeneratedSlot(
				node, "throw_alloc", LowPtr()), LowPtr());
			Instruction store(Instruction::STORE);
			store.type = LowPtr();
			store.first = object;
			store.second = retained;
			derived.Emit(store);
			derived.PauseFullExpressionCleanupSegment(
				"throw_alloc_unwind_end");
			object = derived.LoadStorage(retained, LowPtr());
		}
		if (derived.IsClassObjectType(record.operand_type))
			LowerExceptionObjectInitializer(children[0], object);
		else
		{
			const LowType type = derived.LowerExpressionType(record.operand_type);
			Instruction store(Instruction::STORE);
			store.type = type;
			store.first = derived.LowerValue(children[0], type);
			store.second = object;
			derived.Emit(store);
		}
		if (owns_cleanup)
			(void)derived.RetireFullExpressionNormalActionsBeforeNoreturn();
		if (owns_cleanup) derived.EnsureFullExpressionCleanupSegment();
		CallArguments throw_arguments;
		throw_arguments.Push(object);
		throw_arguments.Push(ExceptionRttiAddress(record.operand_type));
		Operand destructor(0, LowPtr());
		if (record.selected_binding != kNoBinding)
		{
			if (record.selected_binding >= derived.function_symbols_.size() ||
				derived.function_symbols_[record.selected_binding] == kNoLowId)
				throw std::logic_error(
					"exception destructor has no emitted binding");
			destructor = derived.AddressOfStorage(Operand(Operand::FUNCTION,
				derived.function_symbols_[record.selected_binding], LowPtr()));
		}
		throw_arguments.Push(destructor);
		(void)EmitExceptionRuntimeCall(
			derived.polymorphism_.eh_throw_symbol, LowVoid(), throw_arguments);
		if (owns_cleanup)
			derived.FinishNoreturnFullExpressionCleanup();
		if (!active_exception_regions_.empty() &&
			active_exception_regions_.back().kind == EXCEPTION_TRY_REGION)
			derived.Emit(Instruction(Instruction::EH_END));
		derived.EmitNoreturnFallback();
		return Operand(0, LowVoid());
	}

	void LowerExceptionObjectInitializer(std::uint32_t node,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& initializer = derived.arena_.nodes[node];
		if (initializer.kind == DUMP_CONSTRUCTOR_ACTION &&
			initializer.binding != kNoBinding)
		{
			derived.LowerConstructorAction(node, destination, true);
			return;
		}
		const NodeChildren children = derived.Children(node);
		if ((initializer.kind == DUMP_CLASS_VALUE_TRANSFER ||
			 initializer.kind == DUMP_TEMPORARY_OBJECT) &&
			children.size() == 1)
		{
			LowerExceptionObjectInitializer(children[0], destination);
			return;
		}
		derived.LowerClassDestination(node, destination);
	}

	void FinishExceptionControlExit(std::size_t closed_handlers)
	{
		Derived& derived = static_cast<Derived&>(*this);
		CallArguments none;
		for (std::size_t i = active_exception_regions_.size() - closed_handlers;
			i != 0; --i)
		{
			derived.Emit(Instruction(Instruction::EH_END));
			if (active_exception_regions_[i - 1].kind == EXCEPTION_HANDLER_REGION)
				(void)EmitExceptionRuntimeCall(
					derived.polymorphism_.eh_end_catch_symbol, LowVoid(), none);
		}
	}

	std::uint32_t HandlerSelector(std::uint32_t handler)
	{
		if (handler >= handler_selectors_.size())
			throw std::logic_error("exception handler selector is out of range");
		if (handler_selector_epochs_[handler] != handler_selector_epoch_)
		{
			handler_selector_epochs_[handler] = handler_selector_epoch_;
			handler_selectors_[handler] = next_handler_selector_++;
			Derived& derived = static_cast<Derived&>(*this);
			if (derived.stats_) ++derived.stats_->exception_selector_assignments;
		}
		return handler_selectors_[handler];
	}

	void EmitHandlerClause(std::uint32_t handler_node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& handler = derived.arena_.nodes[handler_node];
		const std::uint32_t selector = HandlerSelector(handler_node);
		if (handler.operand_type == kNoType)
		{
			Instruction clause(Instruction::EH_CATCH_ALL);
			clause.first = Operand(selector, LowI32());
			derived.Emit(clause);
			return;
		}
		const SymbolId symbol = ExceptionRttiSymbol(handler.operand_type);
		derived.output_.symbols[symbol].referenced = true;
		Instruction clause(Instruction::EH_CATCH);
		clause.first = Operand(Operand::GLOBAL, symbol, LowPtr());
		clause.second = Operand(selector, LowI32());
		derived.Emit(clause);
	}

	void StartTryStatement(std::uint32_t node, const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() < 2 ||
			derived.arena_.nodes[children[0]].kind != DUMP_COMPOUND_STATEMENT ||
			derived.arena_.nodes[children[1]].kind != DUMP_HANDLER)
			throw std::runtime_error(
				"multiple source handlers are outside this checkpoint");
		for (std::size_t i = 2; i < children.size(); ++i)
			if (derived.arena_.nodes[children[i]].kind !=
					DUMP_DESTRUCTOR_ACTION ||
				!derived.arena_.nodes[children[i]].unwind_only)
				throw std::runtime_error(
					"multiple source handlers are outside this checkpoint");
		const BlockId dispatch = derived.AddBlock(
			derived.NewLabel("catch_dispatch"));
		const BlockId entry = derived.AddBlock(derived.NewLabel("catch_entry"));
		const BlockId end = derived.AddBlock(derived.NewLabel("try_end"));
		derived.EmitEhTarget(Instruction::EH_TRY, dispatch);
		active_exception_regions_.push_back(ExceptionRegionState(
			EXCEPTION_TRY_REGION, children[1], entry));
		typename Derived::StatementTask after(Derived::STATEMENT_TRY_AFTER_BODY);
		after.node = node;
		after.auxiliary = children[1];
		after.first = dispatch;
		after.second = entry;
		after.third = end;
		derived.statement_tasks_.push_back(after);
		derived.PushStatementNode(children[0]);
	}

	void FinishTryBody(std::uint32_t try_node, std::uint32_t handler_node,
		BlockId dispatch,
		BlockId entry, BlockId end)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.CurrentBlock().terminated)
		{
			derived.Emit(Instruction(Instruction::EH_END));
			derived.EmitJump(end);
		}
		if (active_exception_regions_.empty() ||
			active_exception_regions_.back().kind != EXCEPTION_TRY_REGION)
			throw std::logic_error("source try region stack mismatch");
		active_exception_regions_.pop_back();
		derived.SelectBlock(dispatch);
		const DumpNode& handler = derived.arena_.nodes[handler_node];
		EmitHandlerClause(handler_node);
		const ExceptionRegionState* dispatch_parent = EnclosingTryRegion();
		if (dispatch_parent && HasInterveningHandler(dispatch_parent))
		{
			derived.Emit(Instruction(Instruction::EH_CLEANUP));
			EmitHandlerClause(dispatch_parent->handler);
		}
		derived.EmitJump(entry);
		derived.SelectBlock(entry);
		const Operand exception = derived.Temp(LowPtr());
		Instruction read_exception(Instruction::EXCEPTION);
		read_exception.dest = exception.id;
		read_exception.type = LowPtr();
		derived.Emit(read_exception);
		const Operand selector = derived.Temp(LowI32());
		Instruction read_selector(Instruction::EXCEPTION_SELECTOR);
		read_selector.dest = selector.id;
		read_selector.type = LowI32();
		derived.Emit(read_selector);
		const Operand selected = derived.Temp(LowU8());
		Instruction compare(Instruction::CMP);
		compare.dest = selected.id;
		compare.op = LOW_OP_EQ;
		compare.type = LowI32();
		compare.first = selector;
		compare.second = Operand(HandlerSelector(handler_node), LowI32());
		derived.Emit(compare);
		const BlockId body = derived.AddBlock(derived.NewLabel("catch_body"));
		const BlockId next = derived.AddBlock(derived.NewLabel("catch_next"));
		derived.EmitBranch(selected, body, next);
		derived.SelectBlock(body);
		CallArguments arguments;
		arguments.Push(exception);
		const Operand caught = EmitExceptionRuntimeCall(
			derived.polymorphism_.eh_begin_catch_symbol, LowPtr(), arguments);
		if (handler.binding != kNoBinding)
		{
			const Operand catch_slot(derived.EnsureGeneratedSlot(
				handler_node, "catch", LowPtr()), LowPtr());
			Instruction retain(Instruction::STORE);
			retain.type = LowPtr();
			retain.first = caught;
			retain.second = catch_slot;
			derived.Emit(retain);
		}
		const BlockId cleanup = derived.AddBlock(
			derived.NewLabel("catch_cleanup"));
		derived.EmitEhTarget(Instruction::EH_CLEANUP, cleanup);
		InitializeCatchVariable(handler_node, handler, caught);
		active_exception_regions_.push_back(ExceptionRegionState(
			EXCEPTION_HANDLER_REGION, handler_node));
		typename Derived::StatementTask after(Derived::STATEMENT_HANDLER_AFTER_BODY);
		after.node = try_node;
		after.auxiliary = handler_node;
		after.first = cleanup;
		after.second = next;
		after.third = end;
		derived.statement_tasks_.push_back(after);
		const NodeChildren children = derived.Children(handler_node);
		for (std::size_t i = 0; i < children.size(); ++i)
			if (derived.arena_.nodes[children[i]].kind == DUMP_COMPOUND_STATEMENT)
			{
				derived.PushStatementNode(children[i]);
				return;
			}
		throw std::logic_error("typed exception handler has no body");
	}

	void InitializeCatchVariable(std::uint32_t handler_node,
		const DumpNode& handler,
		const Operand& caught)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (handler.binding == kNoBinding &&
			handler.selected_binding == kNoBinding) return;
		const LowType type = derived.LowerStorageType(handler.type);
		const Operand destination = handler.binding != kNoBinding ?
			derived.StorageFor(handler.binding, type) :
			Operand(derived.EnsureGeneratedSlot(
				handler_node, "catch_value", type), type);
		if (handler.selected_binding != kNoBinding)
		{
			if (handler.trivial_special_member_action)
			{
				derived.EmitClassObjectCopy(
					handler.operand_type, caught, destination);
				return;
			}
			if (handler.selected_binding >= derived.function_symbols_.size() ||
				derived.function_symbols_[handler.selected_binding] == kNoLowId)
				throw std::logic_error(
					"catch copy constructor has no emitted binding");
			Instruction call(Instruction::CALL);
			call.type = LowVoid();
			call.first = Operand(Operand::FUNCTION,
				derived.function_symbols_[handler.selected_binding], LowPtr());
			CallArguments arguments;
			arguments.Push(derived.AddressOfStorage(destination));
			arguments.Push(caught);
			CallArgumentFlags passing;
			passing.Push(Instruction::CALL_PASS_VALUE);
			passing.Push(Instruction::CALL_PASS_REFERENCE);
			derived.AttachCallArguments(&call, arguments, passing);
			derived.output_.symbols[call.first.id].referenced = true;
			derived.Emit(call);
			return;
		}
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = type.kind == LOW_PTR ? caught :
			derived.LoadStorage(caught, type);
		store.second = destination;
		derived.Emit(store);
	}

	void DestroyUnnamedCatch(std::uint32_t handler_node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& handler = derived.arena_.nodes[handler_node];
		if (handler.binding != kNoBinding ||
			handler.object_binding == kNoBinding) return;
		if (handler.object_binding >= derived.function_symbols_.size() ||
			derived.function_symbols_[handler.object_binding] == kNoLowId)
			throw std::logic_error(
				"catch destructor has no emitted binding");
		const LowType type = derived.LowerStorageType(handler.type);
		const Operand storage(derived.EnsureGeneratedSlot(
			handler_node, "catch_value", type), type);
		Instruction call(Instruction::CALL);
		call.type = LowVoid();
		call.first = Operand(Operand::FUNCTION,
			derived.function_symbols_[handler.object_binding], LowPtr());
		CallArguments arguments;
		arguments.Push(derived.AddressOfStorage(storage));
		CallArgumentFlags passing;
		passing.Push(Instruction::CALL_PASS_VALUE);
		derived.AttachCallArguments(&call, arguments, passing);
		derived.output_.symbols[call.first.id].referenced = true;
		derived.Emit(call);
	}

	void FinishHandlerBody(std::uint32_t try_node,
		std::uint32_t handler_node, BlockId cleanup,
		BlockId next, BlockId end)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (active_exception_regions_.empty() ||
			active_exception_regions_.back().kind != EXCEPTION_HANDLER_REGION)
			throw std::logic_error("source handler region stack mismatch");
		active_exception_regions_.pop_back();
		CallArguments none;
		if (!derived.CurrentBlock().terminated)
		{
			DestroyUnnamedCatch(handler_node);
			derived.Emit(Instruction(Instruction::EH_END));
			(void)EmitExceptionRuntimeCall(
				derived.polymorphism_.eh_end_catch_symbol, LowVoid(), none);
			derived.EmitJump(end);
		}
		derived.SelectBlock(cleanup);
		DestroyUnnamedCatch(handler_node);
		const ExceptionRegionState* parent = EnclosingTryRegion();
		if (parent) EmitHandlerClause(parent->handler);
		(void)EmitExceptionRuntimeCall(
			derived.polymorphism_.eh_end_catch_symbol, LowVoid(), none);
		if (parent)
		{
			if (HasInterveningHandler(parent))
				LowerTryUnwindActions(try_node);
			derived.Emit(Instruction(Instruction::EH_END));
			if (HasInterveningHandler(parent))
				CloseInterveningHandlers(parent);
			else derived.Emit(Instruction(Instruction::EH_END));
			derived.EmitJump(parent->entry);
		}
		else
		{
			derived.Emit(Instruction(Instruction::EH_END));
			derived.Emit(Instruction(Instruction::RESUME));
		}
		derived.SelectBlock(next);
		LowerTryUnwindActions(try_node);
		if (parent)
		{
			if (HasInterveningHandler(parent))
				CloseInterveningHandlers(parent);
			derived.EmitJump(parent->entry);
		}
		else derived.Emit(Instruction(Instruction::RESUME));
		derived.SelectBlock(end);
	}

	const ExceptionRegionState* EnclosingTryRegion() const
	{
		for (std::size_t i = active_exception_regions_.size(); i != 0; --i)
			if (active_exception_regions_[i - 1].kind == EXCEPTION_TRY_REGION)
				return &active_exception_regions_[i - 1];
		return 0;
	}

	bool HasInterveningHandler(const ExceptionRegionState* parent) const
	{
		for (std::size_t i = active_exception_regions_.size(); i != 0; --i)
		{
			if (&active_exception_regions_[i - 1] == parent) return false;
			if (active_exception_regions_[i - 1].kind ==
				EXCEPTION_HANDLER_REGION) return true;
		}
		return false;
	}

	void LowerTryUnwindActions(std::uint32_t try_node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const NodeChildren children = derived.Children(try_node);
		for (std::size_t i = 0; i < children.size(); ++i)
			if (derived.arena_.nodes[children[i]].kind ==
					DUMP_DESTRUCTOR_ACTION &&
				derived.arena_.nodes[children[i]].unwind_only)
				derived.LowerDestructorAction(derived.arena_.nodes[children[i]]);
	}

	void CloseInterveningHandlers(const ExceptionRegionState* parent)
	{
		Derived& derived = static_cast<Derived&>(*this);
		CallArguments none;
		for (std::size_t i = active_exception_regions_.size(); i != 0; --i)
		{
			if (&active_exception_regions_[i - 1] == parent) return;
			if (active_exception_regions_[i - 1].kind !=
				EXCEPTION_HANDLER_REGION) continue;
			(void)EmitExceptionRuntimeCall(
				derived.polymorphism_.eh_end_catch_symbol, LowVoid(), none);
			derived.Emit(Instruction(Instruction::EH_END));
		}
	}

private:
	std::vector<ExceptionRegionState> active_exception_regions_;
	std::vector<std::uint32_t> handler_selectors_;
	std::vector<std::uint32_t> handler_selector_epochs_;
	std::uint32_t handler_selector_epoch_, next_handler_selector_;
};

}
}

#endif
