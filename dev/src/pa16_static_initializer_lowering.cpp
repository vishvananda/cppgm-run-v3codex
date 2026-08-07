#include "pa16_static_initializer_lowering.h"

#include "pa15_lowering_support.h"

#include <algorithm>
#include <stdexcept>

namespace cppgm
{
namespace pa16_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

StaticInitializerLowering::StaticInitializerLowering(
	const Program& program, const DumpArena& arena, TypedProgram& output,
	LowIRLoweringStats* stats, const std::vector<SymbolId>& function_symbols,
	const std::vector<SymbolId>& global_symbols,
	std::vector<SymbolId>& literal_symbols,
	const std::vector<std::uint32_t>& function_definitions)
	: program_(program), arena_(arena), output_(output), stats_(stats),
	  function_symbols_(function_symbols), global_symbols_(global_symbols),
	  literal_symbols_(literal_symbols),
	  function_definitions_(function_definitions), types_(program)
{
}

NodeChildren StaticInitializerLowering::Children(std::uint32_t node) const
{
	NodeChildren result;
	for (std::uint32_t edge = arena_.nodes[node].first_edge;
		edge != kNoDumpEdge; edge = arena_.edges[edge].next)
		result.Push(arena_.edges[edge].child);
	return result;
}

bool StaticInitializerLowering::IsTrivialConstructorAction(TypeId type,
	const NodeChildren& children) const
{
	if (!types_.IsClassObject(type) || children.size() != 1 ||
		arena_.nodes[children[0]].kind != DUMP_CONSTRUCTOR_ACTION)
		return false;
	const TypeRecord& record = program_.types.Get(types_.ExpressionObject(type));
	return program_.entities[record.entity].trivial_default_constructor;
}

bool StaticInitializerLowering::SymbolForBinding(BindingId binding,
	SymbolId* symbol)
{
	if (binding < function_symbols_.size() &&
		function_symbols_[binding] != kNoLowId)
	{
		*symbol = function_symbols_[binding];
		output_.symbols[*symbol].referenced = true;
		return true;
	}
	if (binding >= program_.bindings.size()) return false;
	const BindingId canonical = program_.bindings[binding].canonical;
	if (canonical < global_symbols_.size() &&
		global_symbols_[canonical] != kNoLowId)
	{
		*symbol = global_symbols_[canonical];
		output_.symbols[*symbol].referenced = true;
		return true;
	}
	return false;
}

SymbolId StaticInitializerLowering::EnsureStringLiteral(std::uint32_t node)
{
	if (node >= literal_symbols_.size())
		throw std::logic_error("invalid PA15 literal node");
	if (literal_symbols_[node] != kNoLowId) return literal_symbols_[node];
	const std::string spelling = program_.names.Get(arena_.nodes[node].text);
	const InternedStringId literal = output_.literals.Intern(spelling);
	if (output_.string_literal_symbols.size() <= literal)
		output_.string_literal_symbols.resize(
			static_cast<std::size_t>(literal) + 1, kNoLowId);
	if (output_.string_literal_symbols[literal] != kNoLowId)
	{
		literal_symbols_[node] = output_.string_literal_symbols[literal];
		return literal_symbols_[node];
	}
	const std::string name = "__strlit__" +
		std::to_string(++output_.string_literal_count);
	const SymbolId symbol = static_cast<SymbolId>(output_.symbols.size());
	output_.symbols.push_back(Symbol(Symbol::GLOBAL_SYMBOL, name,
		std::string(), false, true, false));
	output_.symbols.back().definition_emitted = true;
	output_.symbols.back().referenced = true;
	literal_symbols_[node] = symbol;
	output_.string_literal_symbols[literal] = symbol;
	const std::vector<unsigned char> bytes = DecodeStringLiteral(spelling);
	Global global;
	global.symbol = symbol;
	global.type = LowObject(bytes.size(), 1);
	global.initializer_kind = Global::STRUCTURED_VALUE;
	for (std::size_t i = 0; i < bytes.size(); ++i)
	{
		Global::DataItem item;
		item.kind = Global::DataItem::INTEGER_ITEM;
		item.type = LowI8();
		item.integer_value = bytes[i];
		global.items.push_back(item);
	}
	output_.globals.push_back(global);
	if (stats_) ++stats_->globals;
	return symbol;
}

bool StaticInitializerLowering::ResolveConstantAddress(std::uint32_t node,
	SymbolId* symbol, std::int64_t* offset)
{
	const DumpNode& record = arena_.nodes[node];
	const NodeChildren children = Children(node);
	if (record.kind == DUMP_LITERAL && types_.IsArray(record.type))
	{
		*symbol = EnsureStringLiteral(node);
		*offset = 0;
		return true;
	}
	if (record.kind == DUMP_ID_EXPRESSION && record.binding != kNoBinding)
	{
		*offset = 0;
		return SymbolForBinding(record.binding, symbol);
	}
	if ((record.kind == DUMP_CAST_EXPRESSION ||
		record.kind == DUMP_UNARY_EXPRESSION) && children.size() == 1)
		return ResolveConstantAddress(children[0], symbol, offset);
	if (record.kind == DUMP_SUBSCRIPT_EXPRESSION && children.size() == 2 &&
		arena_.nodes[children[1]].constant &&
		ResolveConstantAddress(children[0], symbol, offset))
	{
		*offset += arena_.nodes[children[1]].constant_value *
			static_cast<std::int64_t>(program_.SizeOf(record.type));
		return true;
	}
	if (record.kind == DUMP_BINARY_EXPRESSION && children.size() == 2)
	{
		const std::string operation =
			StripOperationPrefix(program_.names.Get(record.text));
		if ((operation == "+" || operation == "-") &&
			arena_.nodes[children[1]].constant &&
			ResolveConstantAddress(children[0], symbol, offset))
		{
			const std::int64_t scale = static_cast<std::int64_t>(
				program_.SizeOf(types_.Pointee(arena_.nodes[children[0]].type)));
			const std::int64_t delta =
				arena_.nodes[children[1]].constant_value * scale;
			*offset += operation == "+" ? delta : -delta;
			return true;
		}
	}
	if (record.kind == DUMP_CONDITIONAL_EXPRESSION && children.size() == 3 &&
		arena_.nodes[children[0]].constant)
		return ResolveConstantAddress(children[
			arena_.nodes[children[0]].constant_value ? 1 : 2], symbol, offset);
	return false;
}

bool StaticInitializerLowering::RequiresDynamicAddress(std::uint32_t node) const
{
	const DumpNode& record = arena_.nodes[node];
	const NodeChildren children = Children(node);
	if (record.kind == DUMP_CAST_EXPRESSION && children.size() == 1)
		return RequiresDynamicAddress(children[0]);
	return record.kind == DUMP_UNARY_EXPRESSION && children.size() == 1 &&
		StripOperationPrefix(program_.names.Get(record.text)) == "&" &&
		arena_.nodes[children[0]].kind == DUMP_SUBSCRIPT_EXPRESSION;
}

void StaticInitializerLowering::AppendZero(std::size_t bytes,
	std::vector<Global::DataItem>* items)
{
	if (bytes == 0) return;
	if (!items->empty() && items->back().kind == Global::DataItem::ZERO_ITEM)
	{
		items->back().zero_bytes += bytes;
		return;
	}
	Global::DataItem zero;
	zero.kind = Global::DataItem::ZERO_ITEM;
	zero.zero_bytes = bytes;
	items->push_back(zero);
}

bool StaticInitializerLowering::AppendValue(TypeId type, std::uint32_t node,
	std::vector<Global::DataItem>* items,
	const std::vector<std::pair<BindingId, std::uint32_t> >* substitutions)
{
	type = types_.RemoveTopQualifiers(type);
	const TypeRecord& type_record = program_.types.Get(type);
	if (type_record.kind == TYPE_ARRAY)
	{
		if (type_record.bound == 0 || node == kNoDumpEdge ||
			arena_.nodes[node].kind != DUMP_BRACED_INIT_LIST)
			return false;
		const NodeChildren values = Children(node);
		if (values.size() > type_record.bound) return false;
		for (std::size_t i = 0;
			i < static_cast<std::size_t>(type_record.bound); ++i)
		{
			if (i < values.size())
			{
				if (!AppendValue(type_record.child, values[i], items,
					substitutions)) return false;
			}
			else
			{
				const LowType element = types_.LowerExpression(type_record.child);
				const TypeRecord& child = program_.types.Get(
					types_.RemoveTopQualifiers(type_record.child));
				if (child.kind == TYPE_ARRAY ||
					types_.IsClassObject(type_record.child) || element.kind == LOW_PTR)
					AppendZero(program_.SizeOf(type_record.child), items);
				else
				{
					Global::DataItem zero;
					zero.kind = Global::DataItem::INTEGER_ITEM;
					zero.type = element;
					zero.integer_value = 0;
					items->push_back(zero);
				}
			}
		}
		return true;
	}
	if (types_.IsClassObject(type))
	{
		if (node != kNoDumpEdge &&
			arena_.nodes[node].kind == DUMP_CONSTRUCTOR_ACTION)
			return AppendConstructorValue(type, node, items);
		if (node == kNoDumpEdge ||
			arena_.nodes[node].kind != DUMP_BRACED_INIT_LIST)
			return false;
		const NodeChildren actions = Children(node);
		std::size_t cursor = 0;
		for (std::size_t i = 0; i < actions.size(); ++i)
		{
			const DumpNode& action = arena_.nodes[actions[i]];
			if (action.kind != DUMP_INITIALIZER_ACTION ||
				action.binding == kNoBinding ||
				action.binding >= program_.bindings.size()) return false;
			const BindingRecord& member = program_.bindings[action.binding];
			if (member.member_offset < cursor) return false;
			AppendZero(static_cast<std::size_t>(member.member_offset) - cursor,
				items);
			const NodeChildren values = Children(actions[i]);
			if (values.empty()) AppendZero(program_.SizeOf(action.type), items);
			else if (values.size() != 1 ||
				!AppendValue(action.type, values[0], items, substitutions))
				return false;
			cursor = static_cast<std::size_t>(member.member_offset) +
				program_.SizeOf(action.type);
		}
		const std::size_t object_size = program_.SizeOf(type);
		if (cursor > object_size) return false;
		AppendZero(object_size - cursor, items);
		return true;
	}

	while (node != kNoDumpEdge &&
		arena_.nodes[node].kind == DUMP_BRACED_INIT_LIST)
	{
		const NodeChildren values = Children(node);
		if (values.empty())
		{
			AppendZero(program_.SizeOf(type), items);
			return true;
		}
		if (values.size() != 1) return false;
		node = values[0];
	}
	if (node == kNoDumpEdge) return false;
	if (substitutions && arena_.nodes[node].kind == DUMP_ID_EXPRESSION)
	{
		const BindingId binding = arena_.nodes[node].binding;
		const std::vector<std::pair<BindingId, std::uint32_t> >::const_iterator
			found = std::lower_bound(substitutions->begin(), substitutions->end(),
				std::make_pair(binding, std::uint32_t(0)));
		if (found != substitutions->end() && found->first == binding)
			node = found->second;
	}
	const DumpNode& value = arena_.nodes[node];
	const LowType low_type = types_.LowerExpression(type);
	Global::DataItem item;
	item.type = low_type;
	if (IsFloating(low_type) && value.kind == DUMP_LITERAL)
	{
		item.kind = Global::DataItem::FLOATING_ITEM;
		item.floating_spelling = output_.literals.Intern(
			program_.names.Get(value.text));
		items->push_back(item);
		return true;
	}
	if (value.constant)
	{
		if (low_type.kind == LOW_PTR && value.constant_value == 0)
			AppendZero(program_.SizeOf(type), items);
		else
		{
			item.kind = Global::DataItem::INTEGER_ITEM;
			item.integer_value = value.constant_value;
			items->push_back(item);
		}
		return true;
	}
	if (RequiresDynamicAddress(node)) return false;
	if (ResolveConstantAddress(node, &item.symbol, &item.offset))
	{
		item.kind = Global::DataItem::ADDRESS_ITEM;
		items->push_back(item);
		return true;
	}
	return false;
}

bool StaticInitializerLowering::AppendConstructorValue(TypeId type,
	std::uint32_t action_node, std::vector<Global::DataItem>* items)
{
	const DumpNode& action = arena_.nodes[action_node];
	if (action.binding == kNoBinding ||
		action.binding >= function_definitions_.size() ||
		function_definitions_[action.binding] == kNoDumpEdge) return false;
	const std::uint32_t function_node = function_definitions_[action.binding];
	const NodeChildren function_children = Children(function_node);
	const NodeChildren arguments = Children(action_node);
	SmallSequence<std::uint32_t, 8> parameters;
	std::uint32_t body = kNoDumpEdge;
	for (std::size_t i = 0; i < function_children.size(); ++i)
	{
		const DumpNode& child = arena_.nodes[function_children[i]];
		if (child.kind == DUMP_PARAMETER) parameters.Push(function_children[i]);
		else if (child.kind == DUMP_COMPOUND_STATEMENT)
			body = function_children[i];
	}
	if (body == kNoDumpEdge || parameters.size() != arguments.size() + 1)
		return false;
	std::vector<std::pair<BindingId, std::uint32_t> > substitutions;
	substitutions.reserve(arguments.size());
	for (std::size_t i = 0; i < arguments.size(); ++i)
		substitutions.push_back(std::make_pair(
			arena_.nodes[parameters[i + 1]].binding, arguments[i]));
	std::sort(substitutions.begin(), substitutions.end());

	const NodeChildren actions = Children(body);
	std::size_t cursor = 0;
	for (std::size_t i = 0; i < actions.size(); ++i)
	{
		const DumpNode& member_action = arena_.nodes[actions[i]];
		if (member_action.kind == DUMP_COMPOUND_STATEMENT)
		{
			if (!Children(actions[i]).empty()) return false;
			continue;
		}
		if (member_action.kind != DUMP_INITIALIZER_ACTION ||
			member_action.binding == kNoBinding ||
			member_action.binding >= program_.bindings.size()) return false;
		const BindingRecord& member = program_.bindings[member_action.binding];
		if (member.member_offset < cursor) return false;
		AppendZero(static_cast<std::size_t>(member.member_offset) - cursor, items);
		const NodeChildren values = Children(actions[i]);
		if (values.empty()) AppendZero(program_.SizeOf(member_action.type), items);
		else if (values.size() != 1 ||
			!AppendValue(member_action.type, values[0], items, &substitutions))
			return false;
		cursor = static_cast<std::size_t>(member.member_offset) +
			program_.SizeOf(member_action.type);
	}
	const std::size_t object_size = program_.SizeOf(type);
	if (cursor > object_size) return false;
	AppendZero(object_size - cursor, items);
	return true;
}

void StaticInitializerLowering::SetZero(TypeId type, Global* global)
{
	if (types_.IsReference(type))
	{
		global->initializer_kind = Global::ZERO;
		return;
	}
	const TypeRecord& record = program_.types.Get(types_.ExpressionObject(type));
	if (record.kind != TYPE_ARRAY && !types_.IsClassObject(type))
	{
		global->initializer_kind = Global::ZERO;
		return;
	}
	global->initializer_kind = Global::STRUCTURED_VALUE;
	global->items.clear();
	AppendZero(program_.SizeOf(type), &global->items);
}

bool StaticInitializerLowering::Lower(const NamespaceObjectAction& action,
	bool thread_local_object, Global* global,
	bool* needs_global_class_initializer)
{
	if (types_.IsReference(action.type)) return false;
	if (action.initializer == kNoDumpEdge)
	{
		SetZero(action.type, global);
		return true;
	}
	const DumpNode& initializer = arena_.nodes[action.initializer];
	const NodeChildren variable_children = Children(action.variable);
	if (types_.IsClassObject(action.type) &&
		IsTrivialConstructorAction(action.type, variable_children))
	{
		SetZero(action.type, global);
		if (!thread_local_object &&
			!program_.bindings[action.object].unnamed_namespace_linkage)
			*needs_global_class_initializer = true;
		return true;
	}
	if (initializer.kind == DUMP_CONSTRUCTOR_ACTION ||
		initializer.kind == DUMP_CONSTRUCTOR_ARRAY_ACTION) return false;
	const TypeRecord& source_type = program_.types.Get(
		types_.ExpressionObject(action.type));
	if (source_type.kind == TYPE_ARRAY || types_.IsClassObject(action.type))
	{
		global->initializer_kind = Global::STRUCTURED_VALUE;
		const std::size_t old_size = global->items.size();
		if (AppendValue(action.type, action.initializer, &global->items))
			return true;
		global->items.resize(old_size);
		return false;
	}
	if (IsFloating(global->type) && initializer.kind == DUMP_LITERAL)
	{
		global->initializer_kind = Global::FLOATING_VALUE;
		global->floating_initializer = output_.literals.Intern(
			program_.names.Get(initializer.text));
		return true;
	}
	if (initializer.constant)
	{
		global->initializer_kind = Global::INTEGER_VALUE;
		global->initializer = initializer.constant_value;
		return true;
	}
	SymbolId symbol = kNoLowId;
	std::int64_t offset = 0;
	if (global->type.kind == LOW_PTR &&
		!RequiresDynamicAddress(action.initializer) &&
		ResolveConstantAddress(action.initializer, &symbol, &offset))
	{
		global->initializer_kind = Global::ADDRESS_VALUE;
		global->address_symbol = symbol;
		global->address_offset = offset;
		return true;
	}
	return false;
}

}
}
