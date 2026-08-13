#pragma once

#include "pa12_semantic_model.h"
#include "pa15_lowir_model.h"
#include "pa15_lowering_support.h"

#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa30_lowering_detail
{

using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

template <class Derived>
class RegionLowering
{
protected:
	void LowerRegionStatement(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::vector<typename Derived::StatementTask> outer;
		outer.swap(derived.statement_tasks_);
		derived.LowerStatement(node);
		outer.swap(derived.statement_tasks_);
	}

	void LowerRegionConstructorBody(std::uint32_t node)
	{
		Derived& derived = static_cast<Derived&>(*this);
		std::vector<typename Derived::StatementTask> outer;
		outer.swap(derived.statement_tasks_);
		derived.LowerConstructorBody(node);
		outer.swap(derived.statement_tasks_);
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
				const NodeChildren result = derived.Children(children[i]);
				if (result.size() != 1)
					throw std::logic_error(
						"statement expression has invalid result");
				value = derived.LowerValue(result[0]);
				found = true;
			}
			else LowerRegionStatement(children[i]);
		}
		if (!found && value.type.kind != LOW_VOID)
			throw std::logic_error("non-void statement expression has no result");
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
				const NodeChildren result = derived.Children(children[i]);
				if (result.size() != 1)
					throw std::logic_error(
						"statement expression has invalid class result");
				derived.LowerClassDestination(result[0], destination);
				found = true;
			}
			else LowerRegionStatement(children[i]);
		}
		if (!found)
			throw std::logic_error("class statement expression has no result");
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
