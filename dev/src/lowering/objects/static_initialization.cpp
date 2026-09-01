#include "lowering/objects/static_initialization.h"

#include "lowering/core/source_types.h"
#include "lowering/support/errors.h"
#include "lowering/support/sequences.h"
#include "preprocess/tokens/post_tokenizer.h"

#include <algorithm>

namespace cppgm
{
namespace lowering
{

using namespace semantic;
using namespace semantic;
using namespace lowering::ir;
using namespace lowering::support;

namespace
{

lowir_model::StringId DecodeTypedFloating(lowering::ir::Program& program,
	const std::string& spelling, const LowType& type,
	std::uint64_t* low, std::uint64_t* high)
{
	if (!DecodeFloatingLiteral(spelling, type, low, high))
		ThrowLoweringInternal("invalid typed floating initializer");
	return program.retain_local_names ? program.strings.intern(spelling) :
		lowir_model::StringId();
}

}

StaticInitializerLowering::StaticInitializerLowering(
	const semantic::Program& program, const DumpArena& arena,
	lowering::ir::Program& output,
	lowering::Stats* stats, const std::vector<SymbolId>& function_symbols,
	const std::vector<SymbolId>& global_symbols,
	std::vector<SymbolId>& literal_symbols,
	const std::vector<std::uint32_t>& function_definitions,
	const std::vector<SymbolId>& class_vtable_symbols)
	: program_(program), arena_(arena), output_(output), stats_(stats),
	  function_symbols_(function_symbols), global_symbols_(global_symbols),
	  literal_symbols_(literal_symbols),
	  function_definitions_(function_definitions),
	  class_vtable_symbols_(class_vtable_symbols), types_(program)
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

bool StaticInitializerLowering::IsEmptyConstructionTransferRecipe(
	std::uint32_t node) const
{
	std::vector<std::uint32_t> pending(1, node);
	std::size_t constructor_actions = 0;
	while (!pending.empty())
	{
		const std::uint32_t current = pending.back();
		pending.pop_back();
		const DumpNode& record = arena_.nodes[current];
		const NodeChildren children = Children(current);
		if (record.kind == DUMP_CONSTRUCTOR_ACTION ||
			record.kind == DUMP_CLASS_VALUE_TRANSFER)
		{
			if (record.kind == DUMP_CONSTRUCTOR_ACTION)
				++constructor_actions;
			const TypeId type = record.kind == DUMP_CONSTRUCTOR_ACTION ?
				record.operand_type : record.type;
			if (type == kNoType) return false;
			const TypeRecord& top = program_.types.Get(type);
			if ((top.cv & CV_VOLATILE) != 0) return false;
			const TypeRecord& object = program_.types.Get(
				types_.ExpressionObject(type));
			if (object.kind != TYPE_NAMED ||
				!program_.entities[object.entity].empty_class)
				return false;
		}
		else if (record.kind != DUMP_TEMPORARY_OBJECT &&
			record.kind != DUMP_BRACED_INIT_LIST)
			return false;
		for (std::size_t i = 0; i < children.size(); ++i)
			pending.push_back(children[i]);
	}
	return constructor_actions > 1;
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
		ThrowLoweringInternal("invalid PA15 literal node");
	if (literal_symbols_[node] != kNoLowId) return literal_symbols_[node];
	const std::string spelling = program_.names.Get(arena_.nodes[node].text);
	literal_symbols_[node] = EnsureStringLiteralSpelling(spelling);
	return literal_symbols_[node];
}

SymbolId StaticInitializerLowering::EnsureStringLiteralSpelling(
	const std::string& spelling)
{
	const lowir_model::StringId literal = output_.strings.intern(spelling);
	if (output_.string_literal_symbols.size() <= literal)
		output_.string_literal_symbols.resize(
			static_cast<std::size_t>(literal) + 1, kNoLowId);
	if (output_.string_literal_symbols[literal] != kNoLowId)
		return output_.string_literal_symbols[literal];
	const std::string name = "__strlit__" +
		std::to_string(++output_.string_literal_count);
	const SymbolId symbol = static_cast<SymbolId>(output_.symbols.size());
	output_.symbols.push_back(Symbol(Symbol::GLOBAL_SYMBOL,
		output_.strings.intern(name), lowir_model::StringId(),
		false, true, false));
	output_.symbols.back().definition_emitted = true;
	output_.symbols.back().referenced = true;
	output_.string_literal_symbols[literal] = symbol;
	FundamentalType decoded_type = FT_CHAR;
	std::vector<std::uint32_t> units;
	if (!DecodeStringLiteralCodeUnits(spelling, &decoded_type, &units) ||
		units.empty())
		ThrowLoweringSource("invalid PA16 string literal spelling");
	LowType element;
	std::size_t alignment = 1;
	if (decoded_type == FT_CHAR) element = LowI8();
	else if (decoded_type == FT_WCHAR_T)
	{
		element = LowI32();
		alignment = 4;
	}
	else if (decoded_type == FT_CHAR16_T)
	{
		element = LowU16();
		alignment = 2;
	}
	else if (decoded_type == FT_CHAR32_T)
	{
		element = LowU32();
		alignment = 4;
	}
	else ThrowLoweringInternal("unsupported string literal element type");
	Global global;
	global.symbol = symbol;
	global.type = LowObject(units.size() * alignment, alignment);
	// A C++ string literal is an array of const code units.  Preserve that
	// immutability at the serialized LowIR boundary so later passes need no
	// frontend-only knowledge to reason about its contents.
	global.storage = Global::STORAGE_READONLY;
	global.initializer_kind = Global::STRUCTURED_VALUE;
	for (std::size_t i = 0; i < units.size(); ++i)
	{
		Global::DataItem item;
		item.kind = Global::DataItem::INTEGER_ITEM;
		item.type = element;
		item.integer_value = units[i];
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
		if ((record.OperationIs(OP_PLUS) || record.OperationIs(OP_MINUS)) &&
			arena_.nodes[children[1]].constant &&
			ResolveConstantAddress(children[0], symbol, offset))
		{
			const std::int64_t scale = static_cast<std::int64_t>(
				program_.SizeOf(types_.Pointee(arena_.nodes[children[0]].type)));
			const std::int64_t delta =
				arena_.nodes[children[1]].constant_value * scale;
			*offset += record.OperationIs(OP_PLUS) ? delta : -delta;
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
		record.OperationIs(OP_AMP) &&
		arena_.nodes[children[0]].kind == DUMP_SUBSCRIPT_EXPRESSION;
}

bool StaticInitializerLowering::HasConstantAddress(std::uint32_t node)
{
	SymbolId symbol = kNoLowId;
	std::int64_t offset = 0;
	return node != kNoDumpEdge && !RequiresDynamicAddress(node) &&
		ResolveConstantAddress(node, &symbol, &offset);
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
	const std::vector<std::pair<BindingId, std::uint32_t> >* substitutions,
	bool allow_constructor)
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
					substitutions, allow_constructor)) return false;
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
			arena_.nodes[node].kind == DUMP_CLASS_VALUE_TRANSFER)
		{
			const NodeChildren values = Children(node);
			const TypeRecord& object = program_.types.Get(
				types_.ExpressionObject(type));
			return values.size() == 1 && object.kind == TYPE_NAMED &&
				program_.entities[object.entity].empty_class &&
				IsEmptyConstructionTransferRecipe(node) &&
				AppendValue(type, values[0], items, substitutions,
					allow_constructor);
		}
		if (node != kNoDumpEdge &&
			arena_.nodes[node].kind == DUMP_CONSTRUCTOR_ACTION)
			return allow_constructor && AppendConstructorValue(type, node, items);
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
			const BindingLayoutFact& layout = program_.BindingLayout(member);
			if (layout.member_offset < cursor) return false;
			AppendZero(static_cast<std::size_t>(layout.member_offset) - cursor,
				items);
			const NodeChildren values = Children(actions[i]);
			if (values.empty())
			{
				const LowType member_type =
					types_.LowerExpression(action.type);
				if (IsFloating(member_type))
				{
					Global::DataItem zero;
					zero.kind = Global::DataItem::FLOATING_ITEM;
					zero.type = member_type;
					const std::string spelling =
						member_type.kind == LOW_F32 ? "0.0F" :
						member_type.kind == LOW_F80 ? "0.0L" : "0.0";
					zero.floating_spelling = DecodeTypedFloating(output_,
						spelling, member_type, &zero.floating_low,
						&zero.integer_high);
					items->push_back(zero);
				}
				else AppendZero(program_.SizeOf(action.type), items);
			}
			else if (values.size() != 1 ||
				!AppendValue(action.type, values[0], items, substitutions, false))
				return false;
			cursor = static_cast<std::size_t>(layout.member_offset) +
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
	if (substitutions && value.kind == DUMP_CALL_EXPRESSION)
		return false;
	const LowType low_type = types_.LowerExpression(type);
	Global::DataItem item;
	item.type = low_type;
	if (type_record.kind == TYPE_MEMBER_POINTER &&
		program_.types.IsFunction(type_record.child) &&
		value.binding != kNoBinding &&
		SymbolForBinding(value.binding, &item.symbol))
	{
		item.kind = Global::DataItem::ADDRESS_ITEM;
		item.type = LowPtr();
		items->push_back(item);
		item = Global::DataItem();
		item.kind = Global::DataItem::INTEGER_ITEM;
		item.type = LowI64();
		item.integer_value = value.constant_value;
		items->push_back(item);
		return true;
	}
	if (IsFloating(low_type) && value.kind == DUMP_LITERAL)
	{
		item.kind = Global::DataItem::FLOATING_ITEM;
		const std::string& spelling = program_.names.Get(value.text);
		item.floating_spelling = DecodeTypedFloating(output_, spelling,
			low_type, &item.floating_low, &item.integer_high);
		items->push_back(item);
		return true;
	}
	if (value.constant && low_type.kind == LOW_PTR &&
		value.constant_value == 0 && !RequiresDynamicAddress(node) &&
		ResolveConstantAddress(node, &item.symbol, &item.offset))
	{
		item.kind = Global::DataItem::ADDRESS_ITEM;
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
			item.integer_high = value.constant_high;
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
	std::uint32_t action_node, std::vector<Global::DataItem>* items,
	bool require_vptr)
{
	const DumpNode& action = arena_.nodes[action_node];
	const TypeRecord& object = program_.types.Get(
		types_.ExpressionObject(type));
	if (!require_vptr && object.kind == TYPE_NAMED &&
		program_.entities[object.entity].empty_class &&
		(action.trivial_special_member_action || action.elide_empty_constructor))
	{
		AppendZero(program_.SizeOf(type), items);
		return true;
	}
	if (action.binding == kNoBinding ||
		action.binding >= function_definitions_.size() ||
		function_definitions_[action.binding] == kNoDumpEdge)
		return false;
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
	bool saw_vptr = false;
	const std::size_t item_begin = items->size();
	for (std::size_t i = 0; i < actions.size(); ++i)
	{
		const DumpNode& member_action = arena_.nodes[actions[i]];
		if (member_action.kind == DUMP_BASE_INITIALIZER_ACTION)
		{
			const NodeChildren values = Children(actions[i]);
			if (values.size() != 1 ||
				arena_.nodes[values[0]].kind != DUMP_CONSTRUCTOR_ACTION ||
				member_action.direct_base_offset != cursor ||
				!AppendConstructorValue(member_action.type, values[0], items, false))
				return false;
			cursor += program_.SizeOf(member_action.type);
			continue;
		}
		if (member_action.kind == DUMP_VPTR_INITIALIZATION_ACTION)
		{
			const TypeRecord& object = program_.types.Get(
				types_.ExpressionObject(member_action.type));
			const EntityId entity = object.kind == TYPE_NAMED ?
				object.entity : kNoEntity;
			if (entity == kNoEntity ||
				entity >= class_vtable_symbols_.size() ||
				class_vtable_symbols_[entity] == kNoLowId)
				return false;
			Global::DataItem vptr;
			vptr.kind = Global::DataItem::ADDRESS_ITEM;
			vptr.type = LowPtr();
			vptr.symbol = class_vtable_symbols_[entity];
			vptr.offset = 16;
			if (cursor == 0) items->push_back(vptr);
			else if (item_begin < items->size() &&
				(*items)[item_begin].kind == Global::DataItem::ADDRESS_ITEM &&
				(*items)[item_begin].type.kind == LOW_PTR)
				(*items)[item_begin] = vptr;
			else return false;
			output_.symbols[vptr.symbol].referenced = true;
			if (cursor < 8) cursor = 8;
			saw_vptr = true;
			continue;
		}
		if (member_action.kind == DUMP_COMPOUND_STATEMENT)
		{
			if (!Children(actions[i]).empty()) return false;
			continue;
		}
		if (member_action.kind != DUMP_INITIALIZER_ACTION ||
			member_action.binding == kNoBinding ||
			member_action.binding >= program_.bindings.size()) return false;
		const BindingRecord& member = program_.bindings[member_action.binding];
		const BindingLayoutFact& layout = program_.BindingLayout(member);
		if (layout.member_offset < cursor) return false;
		AppendZero(static_cast<std::size_t>(layout.member_offset) - cursor, items);
		const NodeChildren values = Children(actions[i]);
		if (values.empty()) AppendZero(program_.SizeOf(member_action.type), items);
		else if (values.size() != 1 ||
			!AppendValue(member_action.type, values[0], items, &substitutions))
			return false;
		cursor = static_cast<std::size_t>(layout.member_offset) +
			program_.SizeOf(member_action.type);
	}
	if (require_vptr && !saw_vptr) return false;
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

bool StaticInitializerLowering::LowerConstantObject(TypeId type,
	std::uint32_t initializer, Global* global)
{
	if (!global || initializer == kNoDumpEdge ||
		initializer >= arena_.nodes.size() || types_.IsReference(type))
		return false;
	const Global::InitializerKind old_kind = global->initializer_kind;
	const std::size_t old_size = global->items.size();
	global->initializer_kind = Global::STRUCTURED_VALUE;
	if (AppendValue(type, initializer, &global->items)) return true;
	global->items.resize(old_size);
	global->initializer_kind = old_kind;
	return false;
}

bool StaticInitializerLowering::Lower(const NamespaceObjectAction& action,
	bool thread_local_object, Global* global,
	bool* needs_global_class_initializer, bool* keep_global_class_address)
{
	if (keep_global_class_address) *keep_global_class_address = false;
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
		const TypeRecord& object = program_.types.Get(
			types_.ExpressionObject(action.type));
		const EntityId entity = object.kind == TYPE_NAMED ?
			object.entity : kNoEntity;
		if (!thread_local_object && entity != kNoEntity &&
			!program_.entities[entity].empty_class &&
			!program_.entities[entity].deferred_template_completion &&
			!program_.bindings[action.object].unnamed_namespace_linkage)
			*needs_global_class_initializer = true;
		return true;
	}
	const TypeRecord& source_type = program_.types.Get(
		types_.ExpressionObject(action.type));
	if (source_type.kind == TYPE_MEMBER_POINTER)
	{
		global->initializer_kind = Global::STRUCTURED_VALUE;
		const std::size_t old_size = global->items.size();
		if (AppendValue(action.type, action.initializer, &global->items))
			return true;
		global->items.resize(old_size);
		return false;
	}
	if (types_.IsClassObject(action.type) &&
		initializer.kind == DUMP_CONSTRUCTOR_ACTION)
	{
		global->initializer_kind = Global::STRUCTURED_VALUE;
		const std::size_t old_size = global->items.size();
		const TypeRecord& object = program_.types.Get(
			types_.ExpressionObject(action.type));
		const bool require_vptr = object.kind != TYPE_NAMED ||
			program_.entities[object.entity].flavor != NAMED_UNION ||
			program_.entities[object.entity].polymorphic_class;
		if (AppendConstructorValue(action.type, action.initializer,
			&global->items, require_vptr)) return true;
		global->items.resize(old_size);
		return false;
	}
	if (initializer.kind == DUMP_CONSTRUCTOR_ACTION ||
		initializer.kind == DUMP_CONSTRUCTOR_ARRAY_ACTION) return false;
	if (source_type.kind == TYPE_ARRAY || types_.IsClassObject(action.type))
	{
		global->initializer_kind = Global::STRUCTURED_VALUE;
		const std::size_t old_size = global->items.size();
		const TypeRecord& object = program_.types.Get(
			types_.ExpressionObject(action.type));
		const bool empty_lambda_recipe =
			initializer.kind == DUMP_BRACED_INIT_LIST &&
			object.kind == TYPE_NAMED &&
			program_.entities[object.entity].lambda_closure &&
			Children(action.initializer).empty();
		const bool empty_recipe = empty_lambda_recipe ||
			(initializer.kind == DUMP_CLASS_VALUE_TRANSFER &&
			 object.kind == TYPE_NAMED &&
			 program_.entities[object.entity].empty_class &&
			 IsEmptyConstructionTransferRecipe(action.initializer));
		if (AppendValue(action.type, action.initializer, &global->items))
		{
			const BindingRecord& binding = program_.bindings[action.object];
			if (!thread_local_object && types_.IsClassObject(action.type) &&
				binding.variable_template_specialization)
				return false;
			if (empty_recipe && keep_global_class_address &&
				!thread_local_object &&
				!program_.entities[object.entity].deferred_template_completion &&
				!binding.unnamed_namespace_linkage)
			{
				*needs_global_class_initializer = true;
				*keep_global_class_address = true;
			}
			return true;
		}
		global->items.resize(old_size);
		return false;
	}
	if (IsFloating(global->type) && initializer.kind == DUMP_LITERAL)
	{
		global->initializer_kind = Global::FLOATING_VALUE;
		const std::string& spelling = program_.names.Get(initializer.text);
		global->floating_initializer = DecodeTypedFloating(output_, spelling,
			global->type, &global->floating_initializer_low,
			&global->initializer_high);
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
	if (initializer.constant)
	{
		global->initializer_kind = Global::INTEGER_VALUE;
		global->initializer = initializer.constant_value;
		global->initializer_high = initializer.constant_high;
		return true;
	}
	return false;
}

}
}
