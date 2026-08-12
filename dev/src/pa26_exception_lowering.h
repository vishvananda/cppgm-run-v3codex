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
	enum ExceptionRegion : std::uint8_t
	{
		EXCEPTION_TRY_REGION,
		EXCEPTION_HANDLER_REGION
	};

	void ResetExceptionFunctionState()
	{
		active_exception_regions_.clear();
	}

	template <class Task>
	bool RunExceptionStatementTask(const Task& task)
	{
		if (task.kind == Derived::STATEMENT_TRY_AFTER_BODY)
		{
			FinishTryBody(task.node, task.first, task.second, task.third);
			return true;
		}
		if (task.kind != Derived::STATEMENT_HANDLER_AFTER_BODY) return false;
		FinishHandlerBody(task.first, task.second, task.third);
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
		(void)LowerThrowExpression(record, children);
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

	Operand LowerThrowExpression(const DumpNode& record,
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
		if (children.size() != 1 || record.operand_type == kNoType)
			throw std::logic_error("invalid typed throw expression");
		arguments.Push(Operand(static_cast<std::int64_t>(
			derived.program_.SizeOf(record.operand_type)), LowI64()));
		const Operand object = EmitExceptionRuntimeCall(
			derived.polymorphism_.eh_allocate_exception_symbol,
			LowPtr(), arguments);
		if (derived.IsClassObjectType(record.operand_type))
			throw std::runtime_error(
				"class exception construction is outside this checkpoint");
		const LowType type = derived.LowerExpressionType(record.operand_type);
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = derived.LowerValue(children[0], type);
		store.second = object;
		derived.Emit(store);
		CallArguments throw_arguments;
		throw_arguments.Push(object);
		throw_arguments.Push(ExceptionRttiAddress(record.operand_type));
		throw_arguments.Push(Operand(0, LowPtr()));
		(void)EmitExceptionRuntimeCall(
			derived.polymorphism_.eh_throw_symbol, LowVoid(), throw_arguments);
		if (!active_exception_regions_.empty() &&
			active_exception_regions_.back() == EXCEPTION_TRY_REGION)
			derived.Emit(Instruction(Instruction::EH_END));
		derived.EmitNoreturnFallback();
		return Operand(0, LowVoid());
	}

	void FinishExceptionControlExit()
	{
		Derived& derived = static_cast<Derived&>(*this);
		CallArguments none;
		for (std::size_t i = active_exception_regions_.size(); i != 0; --i)
		{
			derived.Emit(Instruction(Instruction::EH_END));
			if (active_exception_regions_[i - 1] == EXCEPTION_HANDLER_REGION)
				(void)EmitExceptionRuntimeCall(
					derived.polymorphism_.eh_end_catch_symbol, LowVoid(), none);
		}
	}

	void EmitHandlerClause(const DumpNode& handler)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (handler.operand_type == kNoType)
		{
			Instruction clause(Instruction::EH_CATCH_ALL);
			clause.first = Operand(1, LowI32());
			derived.Emit(clause);
			return;
		}
		const SymbolId symbol = ExceptionRttiSymbol(handler.operand_type);
		derived.output_.symbols[symbol].referenced = true;
		Instruction clause(Instruction::EH_CATCH);
		clause.first = Operand(Operand::GLOBAL, symbol, LowPtr());
		clause.second = Operand(1, LowI32());
		derived.Emit(clause);
	}

	void StartTryStatement(std::uint32_t node, const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() != 2 ||
			derived.arena_.nodes[children[0]].kind != DUMP_COMPOUND_STATEMENT ||
			derived.arena_.nodes[children[1]].kind != DUMP_HANDLER)
			throw std::runtime_error(
				"multiple source handlers are outside this checkpoint");
		const BlockId dispatch = derived.AddBlock(
			derived.NewLabel("catch_dispatch"));
		const BlockId entry = derived.AddBlock(derived.NewLabel("catch_entry"));
		const BlockId end = derived.AddBlock(derived.NewLabel("try_end"));
		derived.EmitEhTarget(Instruction::EH_TRY, dispatch);
		active_exception_regions_.push_back(EXCEPTION_TRY_REGION);
		typename Derived::StatementTask after(Derived::STATEMENT_TRY_AFTER_BODY);
		after.node = children[1];
		after.first = dispatch;
		after.second = entry;
		after.third = end;
		derived.statement_tasks_.push_back(after);
		derived.PushStatementNode(children[0]);
	}

	void FinishTryBody(std::uint32_t handler_node, BlockId dispatch,
		BlockId entry, BlockId end)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.CurrentBlock().terminated)
		{
			derived.Emit(Instruction(Instruction::EH_END));
			derived.EmitJump(end);
		}
		if (active_exception_regions_.empty() ||
			active_exception_regions_.back() != EXCEPTION_TRY_REGION)
			throw std::logic_error("source try region stack mismatch");
		active_exception_regions_.pop_back();
		derived.SelectBlock(dispatch);
		const DumpNode& handler = derived.arena_.nodes[handler_node];
		EmitHandlerClause(handler);
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
		compare.second = Operand(1, LowI32());
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
		InitializeCatchVariable(handler, caught);
		active_exception_regions_.push_back(EXCEPTION_HANDLER_REGION);
		typename Derived::StatementTask after(Derived::STATEMENT_HANDLER_AFTER_BODY);
		after.node = handler_node;
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

	void InitializeCatchVariable(const DumpNode& handler,
		const Operand& caught)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (handler.binding == kNoBinding) return;
		const LowType type = derived.LowerStorageType(handler.type);
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = type.kind == LOW_PTR ? caught :
			derived.LoadStorage(caught, type);
		store.second = derived.StorageFor(handler.binding, type);
		derived.Emit(store);
	}

	void FinishHandlerBody(BlockId cleanup, BlockId next, BlockId end)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (active_exception_regions_.empty() ||
			active_exception_regions_.back() != EXCEPTION_HANDLER_REGION)
			throw std::logic_error("source handler region stack mismatch");
		active_exception_regions_.pop_back();
		CallArguments none;
		if (!derived.CurrentBlock().terminated)
		{
			derived.Emit(Instruction(Instruction::EH_END));
			(void)EmitExceptionRuntimeCall(
				derived.polymorphism_.eh_end_catch_symbol, LowVoid(), none);
			derived.EmitJump(end);
		}
		derived.SelectBlock(cleanup);
		(void)EmitExceptionRuntimeCall(
			derived.polymorphism_.eh_end_catch_symbol, LowVoid(), none);
		derived.Emit(Instruction(Instruction::EH_END));
		derived.Emit(Instruction(Instruction::RESUME));
		derived.SelectBlock(next);
		derived.Emit(Instruction(Instruction::RESUME));
		derived.SelectBlock(end);
	}

private:
	std::vector<std::uint8_t> active_exception_regions_;
};

}
}

#endif
