#pragma once

#include "semantic/model/graph.h"
#include "lowering/ir/model.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"

#include <algorithm>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

template <class Derived>
class RegionLowering
{
protected:
	void LowerStatement(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const std::size_t boundary = derived.statement_tasks_.size();
		if (derived.stats_)
		{
			++derived.stats_->statement_scheduler_entries;
			derived.stats_->statement_scheduler_nested_entries += boundary != 0;
		}
		derived.PushStatementNode(node);
		while (derived.statement_tasks_.size() > boundary)
		{
			if (derived.stats_)
			{
				++derived.stats_->statement_scheduler_tasks;
				derived.stats_->statement_scheduler_peak_tasks = std::max(
					derived.stats_->statement_scheduler_peak_tasks,
					derived.statement_tasks_.size());
			}
			const typename Derived::StatementTask task =
				derived.statement_tasks_.back();
			derived.statement_tasks_.pop_back();
			derived.RunStatementTask(task);
		}
		if (derived.statement_tasks_.size() != boundary)
			ThrowLoweringInternal(
				"PA15 statement scheduler crossed its frame");
	}

	bool RegionStatementCanResume(const DumpNode& record) const
	{
		return record.kind == DUMP_CASE_STATEMENT ||
			record.kind == DUMP_DEFAULT_STATEMENT ||
			record.kind == DUMP_LABELED_STATEMENT;
	}

	void LowerRegionStatement(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.LowerStatement(node);
	}

	void LowerRegionConstructorBody(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.LowerConstructorBody(node);
	}

	void LowerRegionDestructorBody(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		derived.LowerDestructorBody(node);
	}

	Operand LowerStatementExpressionValue(std::uint32_t,
		const DumpNode& record, const NodeChildren& children)
	{
		Derived& derived = static_cast<Derived&>(*this);
		Operand value(0, derived.LowerExpressionType(record.type));
		bool found = false;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& child = derived.arena_.nodes[children[i]];
			if (child.kind == DUMP_STATEMENT_EXPRESSION_RESULT)
			{
				found = true;
				if (derived.CurrentBlock().terminated) continue;
				const NodeChildren result = derived.Children(children[i]);
				if (result.size() != 1)
					ThrowLoweringInternal(
						"statement expression has invalid result");
				value = derived.LowerValue(result[0]);
			}
			else if (!derived.CurrentBlock().terminated ||
				RegionStatementCanResume(child))
				LowerRegionStatement(children[i]);
		}
		if (!found && value.type.kind != LOW_VOID)
			ThrowLoweringInternal("non-void statement expression has no result");
		return value;
	}

	void LowerStatementExpressionDestination(std::uint32_t node,
		const Operand& destination)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const NodeChildren children = derived.Children(node);
		bool found = false;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& child = derived.arena_.nodes[children[i]];
			if (child.kind == DUMP_STATEMENT_EXPRESSION_RESULT)
			{
				found = true;
				if (derived.CurrentBlock().terminated) continue;
				const NodeChildren result = derived.Children(children[i]);
				if (result.size() != 1)
					ThrowLoweringInternal(
						"statement expression has invalid class result");
				derived.LowerClassDestination(result[0], destination);
			}
			else if (!derived.CurrentBlock().terminated ||
				RegionStatementCanResume(child))
				LowerRegionStatement(children[i]);
		}
		if (!found)
			ThrowLoweringInternal("class statement expression has no result");
	}

	Operand LowerStatementExpressionStorage(std::uint32_t node,
		const DumpNode& record)
	{
		Derived& derived = static_cast<Derived&>(*this);
		const LowType type = derived.LowerStorageType(record.type);
		const Operand slot(
			derived.EnsureGeneratedSlot(node, "stmtobj", type), type);
		LowerStatementExpressionDestination(
			node, derived.AddressOfStorage(slot));
		return slot;
	}
};

}
}
