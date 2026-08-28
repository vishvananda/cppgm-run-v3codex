#ifndef CPPGM_PA26_EXCEPTION_LOWERING_H
#define CPPGM_PA26_EXCEPTION_LOWERING_H

#include "semantic/model/graph.h"
#include "lowering/ir/model.h"
#include "pa15_lowering_support.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa26_lowering_detail
{

using namespace semantic;
using namespace lowering::ir;
using namespace pa15_lowering_support;

template <class Derived>
class ExceptionLowering
{
protected:
	ExceptionLowering()
		: handler_selector_epoch_(0), next_handler_selector_(1),
		  next_exception_cleanup_context_(1),
		  function_exception_boundary_(FUNCTION_EXCEPTION_BOUNDARY_NONE),
		  function_exception_landing_(kNoLowId),
		  function_exception_action_(kNoLowId),
		  function_exception_object_slot_(kNoLowId),
		  function_exception_type_begin_(0), function_exception_type_count_(0),
		  function_exception_filter_selector_(-1) {}

	enum ExceptionRegion : std::uint8_t
	{
		EXCEPTION_TRY_REGION,
		EXCEPTION_HANDLER_REGION
	};
	struct ExceptionRegionState
	{
		ExceptionRegion kind;
		std::uint32_t node;
		std::uint32_t handler;
		BlockId entry;
		ExceptionRegionState(ExceptionRegion kind_value,
			std::uint32_t node_value = kNoDumpEdge,
			std::uint32_t handler_value = kNoDumpEdge,
			BlockId entry_value = kNoLowId)
			: kind(kind_value), node(node_value), handler(handler_value),
			  entry(entry_value) {}
	};
	struct ExceptionControlTarget
	{
		BlockId block;
		std::size_t region_depth;
		ExceptionControlTarget(BlockId block_value, std::size_t depth_value)
			: block(block_value), region_depth(depth_value) {}
	};

	void ResetExceptionFunctionState()
	{
		Derived& derived = static_cast<Derived&>(*this);
		active_exception_regions_.clear();
		active_exception_contexts_.clear();
		next_exception_cleanup_context_ = 1;
		function_exception_boundary_ = FUNCTION_EXCEPTION_BOUNDARY_NONE;
		function_exception_landing_ = kNoLowId;
		function_exception_action_ = kNoLowId;
		function_exception_object_slot_ = kNoLowId;
		function_exception_type_begin_ = 0;
		function_exception_type_count_ = 0;
		function_exception_clause_blocks_.clear();
		function_exception_filter_selector_ = -1;
		const std::size_t required = derived.arena_.nodes.size();
		if (handler_selectors_.size() < required)
		{
			if (derived.stats_)
				derived.stats_->exception_selector_table_growth +=
					required - handler_selectors_.size();
			handler_selectors_.resize(required, 0);
			handler_selector_epochs_.resize(required, 0);
			handler_next_.resize(required, kNoDumpEdge);
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

	void PrepareFunctionExceptionPolicyRuntime()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.output_.host_object_emission) return;
		bool need_terminate = false;
		bool need_unexpected = false;
		function_exception_boundary_needed_.assign(
			derived.program_.bindings.size(), 0);
		for (BindingId binding = 0;
			binding < derived.program_.bindings.size(); ++binding)
		{
			const BindingRecord& record = derived.program_.bindings[binding];
			if (record.canonical != binding ||
				binding >= derived.function_definition_.size() ||
				derived.function_definition_[binding] == kNoDumpEdge ||
				binding >= derived.function_symbols_.size() ||
				derived.function_symbols_[binding] == kNoLowId) continue;
			if (!FunctionDefinitionMayEscape(
				derived.function_definition_[binding])) continue;
			function_exception_boundary_needed_[binding] = 1;
			need_terminate = need_terminate || record.exception_boundary ==
				FUNCTION_EXCEPTION_BOUNDARY_TERMINATE;
			need_unexpected = need_unexpected || record.exception_boundary ==
				FUNCTION_EXCEPTION_BOUNDARY_UNEXPECTED;
		}
		if (need_terminate) EnsureTerminatePolicyRuntime();
		if (need_unexpected) EnsureUnexpectedPolicyRuntime();
	}

	void BeginFunctionExceptionBoundary(std::uint32_t node, BindingId binding)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!derived.output_.host_object_emission || binding == kNoBinding) return;
		binding = derived.program_.bindings[binding].canonical;
		if (binding >= derived.program_.bindings.size())
			throw std::logic_error("function exception binding is out of range");
		if (binding >= function_exception_boundary_needed_.size() ||
			!function_exception_boundary_needed_[binding]) return;
		const BindingRecord& record = derived.program_.bindings[binding];
		if (record.exception_boundary == FUNCTION_EXCEPTION_BOUNDARY_NONE) return;
		if (derived.stats_)
		{
			if (record.exception_boundary ==
				FUNCTION_EXCEPTION_BOUNDARY_UNEXPECTED)
				++derived.stats_->unexpected_boundaries;
			else if (record.builtin_function != BUILTIN_FUNCTION_NONE)
				++derived.stats_->terminate_boundaries_builtin_runtime;
			else if (record.template_argument_count != 0 ||
				record.explicit_function_specialization ||
				record.closure_template_specialization)
				++derived.stats_->terminate_boundaries_template_specialization;
			else if (record.compiler_generated &&
				(record.constructor || record.destructor))
				++derived.stats_->terminate_boundaries_derived_special_member;
			else
				++derived.stats_->terminate_boundaries_explicit;
		}
		if (record.exception_type_begin >
			derived.program_.function_exception_types.size() ||
			record.exception_type_count >
			derived.program_.function_exception_types.size() -
				record.exception_type_begin)
			throw std::logic_error("function exception type slice is invalid");
		function_exception_boundary_ = record.exception_boundary;
		function_exception_type_begin_ = record.exception_type_begin;
		function_exception_type_count_ = record.exception_type_count;
		function_exception_landing_ = derived.AddBlock(
			derived.NewLabel("function_exception_landing"));
		function_exception_action_ = derived.AddBlock(
			derived.NewLabel("function_exception_action"));
		function_exception_object_slot_ = static_cast<SlotId>(
			derived.function_->slots.size());
		Slot slot;
		slot.name = InternLocalName(derived.output_,
			derived.GeneratedSlotName("function_exception"));
		slot.type = LowPtr();
		derived.function_->slots.push_back(slot);
		if (function_exception_boundary_ == FUNCTION_EXCEPTION_BOUNDARY_TERMINATE)
			function_exception_filter_selector_ = next_handler_selector_++;
		derived.EmitEhTarget(Instruction::EH_TRY,
			function_exception_landing_);
		(void)node;
	}

	void RecordPotentiallyThrowingBinding(BindingId binding) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (!derived.stats_) return;
		if (binding == kNoBinding ||
			binding >= derived.program_.bindings.size())
		{
			++derived.stats_->potentially_throwing_indirect_calls;
			return;
		}
		const BindingRecord& record = derived.program_.bindings[binding];
		if (record.builtin_function != BUILTIN_FUNCTION_NONE)
			++derived.stats_->potentially_throwing_builtin_runtime_calls;
		else if (record.template_argument_count != 0 ||
			record.explicit_function_specialization ||
			record.closure_template_specialization)
			++derived.stats_->potentially_throwing_template_calls;
		else if (record.compiler_generated &&
			(record.constructor || record.destructor))
			++derived.stats_->potentially_throwing_special_member_calls;
		else
			++derived.stats_->potentially_throwing_ordinary_calls;
	}

	bool FunctionDefinitionMayEscape(std::uint32_t root) const
	{
		const Derived& derived = static_cast<const Derived&>(*this);
		if (root == kNoDumpEdge || root >= derived.arena_.nodes.size())
			return false;
		bool may_escape = false;
		std::vector<std::uint32_t> pending(1, root);
		while (!pending.empty())
		{
			const std::uint32_t node = pending.back();
			pending.pop_back();
			const DumpNode& record = derived.arena_.nodes[node];
			if (node != root && record.kind == DUMP_FUNCTION_DEFINITION) continue;
			if (record.kind == DUMP_THROW_EXPRESSION ||
				record.kind == DUMP_NEW_EXPRESSION ||
				record.kind == DUMP_DELETE_EXPRESSION ||
				(record.kind == DUMP_TYPEID_EXPRESSION &&
				 record.dynamic_type_query) ||
				(record.kind == DUMP_DYNAMIC_CAST_EXPRESSION &&
				 record.dynamic_cast_reference))
			{
				may_escape = true;
				if (!derived.stats_) return true;
				++derived.stats_->potentially_throwing_explicit_operations;
			}
			if (record.kind == DUMP_CALL_EXPRESSION &&
				record.compiler_intrinsic == COMPILER_INTRINSIC_NONE &&
				record.hosted_vector_intrinsic ==
					hosted_builtin::VECTOR_INTRINSIC_NONE &&
				record.hosted_atomic_intrinsic ==
					hosted_builtin::ATOMIC_INTRINSIC_NONE)
			{
				const NodeChildren children = derived.Children(node);
				if (children.empty())
				{
					may_escape = true;
					if (!derived.stats_) return true;
					RecordPotentiallyThrowingBinding(kNoBinding);
				}
				else
				{
				const DumpNode& callee = derived.arena_.nodes[children[0]];
				if (callee.kind != DUMP_CALLEE ||
					callee.binding == kNoBinding ||
					callee.binding >= derived.program_.bindings.size() ||
					!derived.program_.bindings[callee.binding].nonthrowing)
				{
					may_escape = true;
					if (!derived.stats_) return true;
					RecordPotentiallyThrowingBinding(
						callee.kind == DUMP_CALLEE ? callee.binding : kNoBinding);
				}
				}
			}
			if (record.kind == DUMP_CONSTRUCTOR_ACTION ||
				record.kind == DUMP_SPECIAL_MEMBER_CONSTRUCTION_ACTION ||
				record.kind == DUMP_DESTRUCTOR_ACTION)
			{
				const BindingId action = record.binding != kNoBinding ?
					record.binding : record.object_binding;
				if (action != kNoBinding &&
					(action >= derived.program_.bindings.size() ||
					 !derived.program_.bindings[action].nonthrowing))
				{
					may_escape = true;
					if (!derived.stats_) return true;
					RecordPotentiallyThrowingBinding(action);
				}
			}
			for (std::uint32_t edge = record.first_edge; edge != kNoDumpEdge;
				edge = derived.arena_.edges[edge].next)
				pending.push_back(derived.arena_.edges[edge].child);
		}
		return may_escape;
	}

	void FinishFunctionExceptionBoundaryNormalExit()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (function_exception_boundary_ != FUNCTION_EXCEPTION_BOUNDARY_NONE)
			derived.Emit(Instruction(Instruction::EH_END));
	}

	void FinishFunctionExceptionBoundary()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (function_exception_boundary_ == FUNCTION_EXCEPTION_BOUNDARY_NONE)
			return;
		derived.SelectBlock(function_exception_landing_);
		EmitFunctionExceptionBoundaryClause();
		RetainFunctionExceptionObject();
		derived.EmitJump(function_exception_action_);
		derived.SelectBlock(function_exception_action_);
		EmitFunctionExceptionAction();
	}

	void EmitFunctionExceptionAction()
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand object = derived.LoadStorage(
			Operand(function_exception_object_slot_, LowPtr()), LowPtr());
		CallArguments arguments;
		arguments.Push(object);
		if (function_exception_boundary_ ==
			FUNCTION_EXCEPTION_BOUNDARY_TERMINATE)
		{
			(void)EmitExceptionRuntimeCall(
				derived.output_.terminate_helper_symbol, LowVoid(), arguments);
		}
		else
			(void)EmitExceptionRuntimeCall(
				derived.output_.call_unexpected_symbol, LowVoid(), arguments);
		derived.EmitNoreturnFallback();
	}

	void RetainFunctionExceptionObject()
	{
		Derived& derived = static_cast<Derived&>(*this);
		const Operand exception = derived.Temp(LowPtr());
		Instruction read(Instruction::EXCEPTION);
		read.dest = exception.id;
		read.type = LowPtr();
		derived.Emit(read);
		Instruction retain(Instruction::STORE);
		retain.type = LowPtr();
		retain.first = exception;
		retain.second = Operand(function_exception_object_slot_, LowPtr());
		derived.Emit(retain);
	}

	void EmitExceptionResume()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (function_exception_boundary_ == FUNCTION_EXCEPTION_BOUNDARY_NONE)
		{
			derived.Emit(Instruction(Instruction::RESUME));
			return;
		}
		EmitFunctionExceptionBoundaryClause();
		RetainFunctionExceptionObject();
		if (function_exception_boundary_ == FUNCTION_EXCEPTION_BOUNDARY_TERMINATE)
		{
			EmitFunctionExceptionAction();
			return;
		}
		const Operand selector = derived.Temp(LowI32());
		Instruction read(Instruction::EXCEPTION_SELECTOR);
		read.dest = selector.id;
		read.type = LowI32();
		derived.Emit(read);
		const Operand disallowed = derived.Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = disallowed.id;
		compare.op = LOW_OP_EQ;
		compare.type = LowI32();
		compare.first = selector;
		compare.second = Operand(function_exception_filter_selector_, LowI32());
		derived.Emit(compare);
		const BlockId action = derived.AddBlock(
			derived.NewLabel("function_exception_local_action"));
		const BlockId resume = derived.AddBlock(
			derived.NewLabel("function_exception_resume"));
		derived.EmitBranch(disallowed, action, resume);
		derived.SelectBlock(action);
		EmitFunctionExceptionAction();
		derived.SelectBlock(resume);
		derived.Emit(Instruction(Instruction::RESUME));
	}

	void EmitFunctionExceptionBoundaryClause()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (function_exception_boundary_ == FUNCTION_EXCEPTION_BOUNDARY_NONE)
			return;
		if (derived.current_block_ >= function_exception_clause_blocks_.size())
			function_exception_clause_blocks_.resize(
				derived.function_->blocks.size(), 0);
		if (function_exception_clause_blocks_[derived.current_block_]) return;
		function_exception_clause_blocks_[derived.current_block_] = 1;
		if (function_exception_boundary_ ==
			FUNCTION_EXCEPTION_BOUNDARY_TERMINATE)
		{
			Instruction clause(Instruction::EH_CATCH_ALL);
			clause.first = Operand(function_exception_filter_selector_, LowI32());
			derived.Emit(clause);
			return;
		}
		Instruction clause(Instruction::EH_FILTER);
		clause.first = Operand(function_exception_filter_selector_, LowI32());
		clause.extra_first = static_cast<std::uint32_t>(
			derived.output_.exception_filter_types.size());
		clause.extra_count = function_exception_type_count_;
		for (std::size_t i = 0; i < function_exception_type_count_; ++i)
		{
			const TypeId type = derived.program_.function_exception_types[
				function_exception_type_begin_ + i];
			const SymbolId symbol = ExceptionRttiSymbol(type);
			derived.output_.symbols[symbol].referenced = true;
			derived.output_.exception_filter_types.push_back(symbol);
		}
		derived.Emit(clause);
	}

	void EnsureTerminatePolicyRuntime()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.output_.terminate_runtime_symbol == kNoLowId)
		{
			const SymbolId symbol = derived.AddSyntheticSymbol(
				Symbol::FUNCTION_SYMBOL, "std_terminate", "_ZSt9terminatev", false);
			Symbol& record = derived.output_.symbols[symbol];
			record.runtime_role = Symbol::RUNTIME_ROLE_TERMINATE;
			record.nonthrowing = true;
			record.noreturn = true;
			record.declaration_emitted = true;
			FunctionDeclaration declaration;
			declaration.symbol = symbol;
			declaration.result = LowVoid();
			derived.output_.declarations.push_back(declaration);
			derived.output_.terminate_runtime_symbol = symbol;
		}
		if (derived.output_.terminate_helper_symbol != kNoLowId) return;
		const SymbolId helper = derived.AddSyntheticSymbol(
			Symbol::FUNCTION_SYMBOL, "cppgm_call_terminate", std::string(), true);
		Symbol& helper_record = derived.output_.symbols[helper];
		helper_record.nonthrowing = true;
		helper_record.noreturn = true;
		helper_record.no_inline = true;
		helper_record.definition_emitted = true;
		derived.output_.terminate_helper_symbol = helper;
		Function function;
		function.symbol = helper;
		function.result = LowVoid();
		Parameter exception;
		exception.name = InternLocalName(
			derived.output_, "exception_object");
		exception.type = LowPtr();
		function.parameters.push_back(exception);
		derived.BeginSyntheticFunction(&function);
		CallArguments begin_arguments;
		begin_arguments.Push(Operand(ParameterId(0), LowPtr()));
		(void)EmitExceptionRuntimeCall(
			derived.polymorphism_.eh_begin_catch_symbol,
			LowPtr(), begin_arguments);
		CallArguments none;
		(void)EmitExceptionRuntimeCall(
			derived.output_.terminate_runtime_symbol, LowVoid(), none);
		derived.EmitNoreturnFallback();
		derived.EndSyntheticFunction(function);
		derived.output_.functions.push_back(function);
	}

	void EnsureUnexpectedPolicyRuntime()
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (derived.output_.call_unexpected_symbol != kNoLowId) return;
		const SymbolId symbol = derived.AddSyntheticSymbol(Symbol::FUNCTION_SYMBOL,
			"__cxa_call_unexpected", "__cxa_call_unexpected", false);
		Symbol& record = derived.output_.symbols[symbol];
		record.noreturn = true;
		record.declaration_emitted = true;
		FunctionDeclaration declaration;
		declaration.symbol = symbol;
		declaration.result = LowVoid();
		Parameter exception;
		exception.name = InternLocalName(
			derived.output_, "exception_object");
		exception.type = LowPtr();
		declaration.parameters.push_back(exception);
		derived.output_.declarations.push_back(declaration);
		derived.output_.call_unexpected_symbol = symbol;
	}

	std::uint32_t ExceptionCleanupContext() const
	{
		if (active_exception_regions_.size() != active_exception_contexts_.size())
			throw std::logic_error("exception cleanup context stack mismatch");
		return active_exception_contexts_.empty() ? 0 :
			active_exception_contexts_.back();
	}

	bool ExceptionCleanupRoutesToTry(std::uint32_t context) const
	{
		if (context == 0) return false;
		if (active_exception_regions_.empty() ||
			active_exception_contexts_.empty() ||
			active_exception_contexts_.back() != context)
			throw std::logic_error("exception cleanup context changed");
		return active_exception_regions_.back().kind == EXCEPTION_TRY_REGION;
	}

	void PushExceptionRegion(const ExceptionRegionState& region)
	{
		if (next_exception_cleanup_context_ == 0)
			throw std::logic_error("exception cleanup context identity overflow");
		active_exception_regions_.push_back(region);
		active_exception_contexts_.push_back(next_exception_cleanup_context_++);
	}

	void PopExceptionRegion()
	{
		if (active_exception_regions_.empty() ||
			active_exception_contexts_.empty())
			throw std::logic_error("exception cleanup context stack underflow");
		active_exception_regions_.pop_back();
		active_exception_contexts_.pop_back();
	}

	bool BeginExceptionTryCleanupDispatch()
	{
		if (active_exception_regions_.empty() ||
			active_exception_regions_.back().kind != EXCEPTION_TRY_REGION)
			return false;
		Derived& derived = static_cast<Derived&>(*this);
		EmitTryHandlerClauses(active_exception_regions_.back().node);
		derived.Emit(Instruction(Instruction::EH_CLEANUP));
		return true;
	}

	void FinishExceptionCleanupDispatch(bool routes_to_try,
		bool closes_cleanup_region = true)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (!routes_to_try)
		{
			EmitExceptionResume();
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

	std::size_t ActiveExceptionRegionCount() const
	{
		return active_exception_regions_.size();
	}

	std::size_t BeginExceptionControlExit(std::size_t exit_count)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (exit_count > active_exception_regions_.size())
			throw std::logic_error(
				"control exit exceeds active exception regions");
		CallArguments none;
		std::size_t closed = 0;
		if (exit_count != 0 && !active_exception_regions_.empty() &&
			active_exception_regions_.back().kind == EXCEPTION_HANDLER_REGION)
		{
			derived.Emit(Instruction(Instruction::EH_END));
			(void)EmitExceptionRuntimeCall(
				derived.polymorphism_.eh_end_catch_symbol, LowVoid(), none);
			++closed;
		}
		return closed;
	}

	void LowerGotoControlExit(const DumpNode& record,
		const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const std::size_t exit_count = record.exception_control_exit_count;
		const std::size_t closed = BeginExceptionControlExit(exit_count);
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			if (derived.arena_.nodes[children[i]].kind != DUMP_DESTRUCTOR_ACTION)
				throw std::logic_error("invalid goto cleanup action");
			derived.LowerDestructorAction(derived.arena_.nodes[children[i]]);
		}
		FinishExceptionControlExit(closed, exit_count);
	}

	void LowerStructuredControlExit(const NodeChildren& children,
		std::size_t target_depth)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const std::size_t active = ActiveExceptionRegionCount();
		if (target_depth > active)
			throw std::logic_error(
				"structured control transfer enters an exception region");
		const std::size_t exits = active - target_depth;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			if (derived.arena_.nodes[children[i]].kind !=
				DUMP_DESTRUCTOR_ACTION)
				throw std::logic_error(
					"invalid structured control cleanup action");
			derived.LowerDestructorAction(derived.arena_.nodes[children[i]]);
		}
		const std::size_t closed = BeginExceptionControlExit(exits);
		FinishExceptionControlExit(closed, exits);
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
		Instruction call = derived.DirectCallInstruction(symbol, result);
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
		if (record.operand_type == kNoType)
		{
			const bool owns_cleanup = !children.empty() &&
				!derived.full_expression_cleanup_active_;
			if (owns_cleanup)
			{
				derived.BeginFullExpressionCleanup(children, 0, true);
				derived.EnsureFullExpressionCleanupSegment();
			}
			(void)EmitExceptionRuntimeCall(
				derived.polymorphism_.eh_rethrow_symbol, LowVoid(), arguments);
			if (owns_cleanup)
				derived.FinishNoreturnFullExpressionCleanup();
			derived.EmitNoreturnFallback();
			return Operand(0, LowVoid());
		}
		if (children.empty())
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

	void FinishExceptionControlExit(std::size_t closed_regions,
		std::size_t exit_count)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (exit_count > active_exception_regions_.size() ||
			closed_regions > exit_count)
			throw std::logic_error("invalid exception control exit count");
		CallArguments none;
		for (std::size_t offset = closed_regions;
			offset < exit_count; ++offset)
		{
			const std::size_t i = active_exception_regions_.size() - offset - 1;
			derived.Emit(Instruction(Instruction::EH_END));
			if (active_exception_regions_[i].kind == EXCEPTION_HANDLER_REGION)
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

	void EmitTryHandlerClauses(std::uint32_t try_node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const NodeChildren children = derived.Children(try_node);
		for (std::size_t i = 0; i < children.size(); ++i)
			if (derived.arena_.nodes[children[i]].kind == DUMP_HANDLER)
				EmitHandlerClause(children[i]);
	}

	void StartTryStatement(std::uint32_t node, const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		if (children.size() < 2 ||
			derived.arena_.nodes[children[0]].kind != DUMP_COMPOUND_STATEMENT)
			throw std::runtime_error(
				"invalid source try region");
		std::uint32_t first_handler = kNoDumpEdge;
		std::uint32_t previous_handler = kNoDumpEdge;
		for (std::size_t i = 1; i < children.size(); ++i)
		{
			const DumpNode& child = derived.arena_.nodes[children[i]];
			if (child.kind == DUMP_HANDLER)
			{
				if (first_handler == kNoDumpEdge) first_handler = children[i];
				if (previous_handler != kNoDumpEdge)
					handler_next_[previous_handler] = children[i];
				previous_handler = children[i];
			}
			else if (child.kind != DUMP_DESTRUCTOR_ACTION || !child.unwind_only)
				throw std::runtime_error(
					"invalid source try suffix");
		}
		if (first_handler == kNoDumpEdge)
			throw std::runtime_error("source try region has no handler");
		if (previous_handler != kNoDumpEdge)
			handler_next_[previous_handler] = kNoDumpEdge;
		const BlockId dispatch = derived.AddBlock(
			derived.NewLabel("catch_dispatch"));
		const BlockId entry = derived.AddBlock(derived.NewLabel("catch_entry"));
		const BlockId end = derived.AddBlock(derived.NewLabel("try_end"));
		derived.EmitEhTarget(Instruction::EH_TRY, dispatch);
		PushExceptionRegion(ExceptionRegionState(
			EXCEPTION_TRY_REGION, node, first_handler, entry));
		typename Derived::StatementTask after(Derived::STATEMENT_TRY_AFTER_BODY);
		after.node = node;
		after.auxiliary = first_handler;
		after.first = dispatch;
		after.second = entry;
		after.third = end;
		derived.statement_tasks_.push_back(after);
		if (derived.arena_.nodes[node].function_try_body ==
			FUNCTION_TRY_BODY_CONSTRUCTOR)
			derived.LowerRegionConstructorBody(children[0]);
		else if (derived.arena_.nodes[node].function_try_body ==
			FUNCTION_TRY_BODY_DESTRUCTOR)
			derived.LowerRegionDestructorBody(children[0]);
		else derived.PushStatementNode(children[0]);
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
		PopExceptionRegion();
		derived.SelectBlock(dispatch);
		EmitTryHandlerClauses(try_node);
		const ExceptionRegionState* dispatch_parent = EnclosingTryRegion();
		if (dispatch_parent && HasInterveningHandler(dispatch_parent))
		{
			derived.Emit(Instruction(Instruction::EH_CLEANUP));
			EmitTryHandlerClauses(dispatch_parent->node);
		}
		if (!dispatch_parent) EmitFunctionExceptionBoundaryClause();
		derived.EmitJump(entry);
		derived.SelectBlock(entry);
		StartHandlerBody(try_node, handler_node, end);
	}

	void StartHandlerBody(std::uint32_t try_node,
		std::uint32_t handler_node, BlockId end)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const DumpNode& handler = derived.arena_.nodes[handler_node];
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
		const Operand selected = derived.Temp(LowI64());
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
		PushExceptionRegion(ExceptionRegionState(
			EXCEPTION_HANDLER_REGION, handler_node, handler_node));
		typename Derived::StatementTask after(Derived::STATEMENT_HANDLER_AFTER_BODY);
		after.node = try_node;
		after.auxiliary = handler_node;
		after.last = handler_next_[handler_node];
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
			CallArguments arguments;
			arguments.Push(derived.AddressOfStorage(destination));
			arguments.Push(caught);
			CallArgumentFlags passing;
			passing.Push(Instruction::CALL_PASS_VALUE);
			passing.Push(Instruction::CALL_PASS_REFERENCE);
			Instruction call = derived.DirectCallInstruction(
				derived.function_symbols_[handler.selected_binding], LowVoid());
			derived.AttachCallArguments(&call, arguments, passing);
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
		CallArguments arguments;
		arguments.Push(derived.AddressOfStorage(storage));
		CallArgumentFlags passing;
		passing.Push(Instruction::CALL_PASS_VALUE);
		Instruction call = derived.DirectCallInstruction(
			derived.function_symbols_[handler.object_binding], LowVoid());
		derived.AttachCallArguments(&call, arguments, passing);
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
		PopExceptionRegion();
		CallArguments none;
		if (!derived.CurrentBlock().terminated)
		{
			DestroyUnnamedCatch(handler_node);
			if (derived.arena_.nodes[try_node].function_try_body ==
					FUNCTION_TRY_BODY_CONSTRUCTOR ||
				derived.arena_.nodes[try_node].function_try_body ==
					FUNCTION_TRY_BODY_DESTRUCTOR)
			{
				(void)EmitExceptionRuntimeCall(
					derived.polymorphism_.eh_rethrow_symbol, LowVoid(), none);
				derived.EmitNoreturnFallback();
			}
			else
			{
			derived.Emit(Instruction(Instruction::EH_END));
			(void)EmitExceptionRuntimeCall(
				derived.polymorphism_.eh_end_catch_symbol, LowVoid(), none);
			derived.EmitJump(end);
			}
		}
		derived.SelectBlock(cleanup);
		DestroyUnnamedCatch(handler_node);
		const ExceptionRegionState* parent = EnclosingTryRegion();
		if (parent) EmitTryHandlerClauses(parent->node);
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
			EmitExceptionResume();
		}
		derived.SelectBlock(next);
		if (handler_next_[handler_node] != kNoDumpEdge)
		{
			StartHandlerBody(try_node, handler_next_[handler_node], end);
			return;
		}
		LowerTryUnwindActions(try_node);
		if (parent)
		{
			if (HasInterveningHandler(parent))
				CloseInterveningHandlers(parent);
			derived.EmitJump(parent->entry);
		}
		else EmitExceptionResume();
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
	std::vector<std::uint32_t> active_exception_contexts_;
	std::vector<std::uint32_t> handler_selectors_;
	std::vector<std::uint32_t> handler_selector_epochs_;
	std::vector<std::uint32_t> handler_next_;
	std::uint32_t handler_selector_epoch_, next_handler_selector_;
	std::uint32_t next_exception_cleanup_context_;
	FunctionExceptionBoundaryKind function_exception_boundary_;
	BlockId function_exception_landing_, function_exception_action_;
	SlotId function_exception_object_slot_;
	std::uint32_t function_exception_type_begin_, function_exception_type_count_;
	std::vector<std::uint8_t> function_exception_clause_blocks_;
	std::vector<std::uint8_t> function_exception_boundary_needed_;
	std::int64_t function_exception_filter_selector_;
};

}
}

#endif
