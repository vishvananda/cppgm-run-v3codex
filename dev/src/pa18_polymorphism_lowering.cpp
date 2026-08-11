#include "pa18_polymorphism_lowering.h"

#include "pa15_lowering_abi.h"
#include "pa15_lowering_support.h"
#include "pa15_source_type_lowering.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace cppgm
{
namespace pa18_lowering_detail
{

using namespace pa11;
using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail;
using namespace pa15_lowering_support;

PolymorphismLoweringState::PolymorphismLoweringState()
	: pure_virtual_symbol(kNoLowId), rtti_class_symbol(kNoLowId),
	  rtti_si_symbol(kNoLowId), rtti_vmi_symbol(kNoLowId),
	  rtti_fundamental_symbol(kNoLowId), rtti_pointer_symbol(kNoLowId),
	  rtti_enum_symbol(kNoLowId), rtti_array_symbol(kNoLowId),
	  rtti_function_symbol(kNoLowId), rtti_member_pointer_symbol(kNoLowId),
	  dynamic_cast_symbol(kNoLowId),
	  bad_cast_symbol(kNoLowId), bad_typeid_symbol(kNoLowId),
	  need_dynamic_cast(false), need_bad_cast(false), need_bad_typeid(false),
	  source_function_first(0)
{
}

bool IsFunctionLocalEntity(const Program& program, EntityId entity)
{
	if (entity == kNoEntity || entity >= program.entities.size()) return false;
	for (ScopeId scope = program.entities[entity].owner;
		scope != kNoScope; scope = program.ParentScope(scope))
	{
		if (program.KindOfScope(scope) == SCOPE_FUNCTION) return true;
		if (program.KindOfScope(scope) == SCOPE_NAMESPACE) return false;
	}
	return false;
}

namespace
{

class GlobalEmitter
{
public:
	GlobalEmitter(const SemanticGraphView& graph, TypedProgram& output,
		LowIRLoweringStats* stats, std::size_t source_ordinal,
		const std::vector<SymbolId>& function_symbols,
		PolymorphismLoweringState* state)
		: graph_(graph), program_(graph.program), output_(output), stats_(stats),
		  source_ordinal_(source_ordinal), function_symbols_(function_symbols),
		  state_(*state), source_types_(program_)
	{
	}

	void Prepare()
	{
		InitializeState();
		CollectRttiDemands();
		RegisterSymbols();
		EmitGlobals();
	}

private:
	void InitializeState()
	{
		state_.source_function_first = output_.functions.size();
		const std::size_t count = program_.entities.size();
		state_.class_vtable_symbols.assign(count, kNoLowId);
		state_.class_rtti_symbols.assign(count, kNoLowId);
		state_.class_type_name_symbols.assign(count, kNoLowId);
		state_.class_rtti_demanded.assign(count, 0);
		state_.type_rtti_symbols.assign(program_.types.Size(), kNoLowId);
		state_.type_name_symbols.assign(program_.types.Size(), kNoLowId);
		state_.type_rtti_demanded.assign(program_.types.Size(), 0);
		state_.deleting_destructor_symbols.assign(count, kNoLowId);
		state_.deallocation_bindings.assign(count, kNoBinding);
		state_.complete_destructor_bindings.assign(count, kNoBinding);
		state_.base_destructor_bindings.assign(count, kNoBinding);
		state_.deleting_destructor_calls_complete.assign(count, 0);
		state_.need_dynamic_cast = false;
		state_.need_bad_cast = false;
		state_.need_bad_typeid = false;
		for (BindingId binding = 0; binding < program_.bindings.size(); ++binding)
		{
			const BindingRecord& candidate = program_.bindings[binding];
			if (candidate.member_owner == kNoEntity ||
				candidate.member_owner >= count) continue;
			if (candidate.destructor)
			{
				if (candidate.destructor_base_entry)
					state_.base_destructor_bindings[candidate.member_owner] = binding;
				else if (binding == candidate.canonical)
					state_.complete_destructor_bindings[candidate.member_owner] =
						binding;
			}
			if (candidate.operator_kind != OPERATOR_DELETE) continue;
			const TypeRecord& type = program_.types.Get(candidate.type);
			if (type.kind != TYPE_FUNCTION ||
				(type.parameter_count != 1 && type.parameter_count != 2)) continue;
			BindingId& selected =
				state_.deallocation_bindings[candidate.member_owner];
			if (selected == kNoBinding || type.parameter_count == 1)
				selected = candidate.canonical;
		}
		for (std::uint32_t node = 0; node < graph_.arena.nodes.size(); ++node)
		{
			const DumpNode& function = graph_.arena.nodes[node];
			if (function.kind != DUMP_FUNCTION_DEFINITION ||
				function.binding == kNoBinding ||
				function.binding >= program_.bindings.size()) continue;
			const BindingRecord& binding =
				program_.bindings[function.binding];
			const EntityId entity = binding.member_owner;
			if (!binding.destructor || binding.destructor_base_entry ||
				entity == kNoEntity || entity >= count ||
				state_.complete_destructor_bindings[entity] !=
					binding.canonical) continue;
			for (std::uint32_t edge = function.first_edge;
				edge != kNoDumpEdge; edge = graph_.arena.edges[edge].next)
			{
				const std::uint32_t child = graph_.arena.edges[edge].child;
				if (graph_.arena.nodes[child].kind == DUMP_COMPOUND_STATEMENT &&
					HasUninlinedDestructorWork(child))
					state_.deleting_destructor_calls_complete[entity] = 1;
			}
		}
	}

	TypeId RttiType(TypeId type) const
	{
		const TypeRecord* record = &program_.types.Get(type);
		if (record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE)
		{
			type = record->child;
			record = &program_.types.Get(type);
		}
		while (record->kind == TYPE_QUALIFIED)
		{
			type = record->child;
			record = &program_.types.Get(type);
		}
		return type;
	}

	void DemandRtti(TypeId requested)
	{
		if (stats_) ++stats_->rtti_demand_requests;
		std::vector<TypeId> pending(1, RttiType(requested));
		while (!pending.empty())
		{
			const TypeId type = pending.back();
			pending.pop_back();
			if (type >= state_.type_rtti_demanded.size())
				throw std::logic_error("RTTI demand type is out of range");
			if (state_.type_rtti_demanded[type]) continue;
			state_.type_rtti_demanded[type] = 1;
			if (stats_) ++stats_->rtti_types_demanded;
			const TypeRecord& record = program_.types.Get(type);
			if (record.kind == TYPE_POINTER)
				pending.push_back(RttiType(record.child));
			else if (record.kind == TYPE_MEMBER_POINTER)
			{
				pending.push_back(RttiType(record.child));
				pending.push_back(RttiType(record.bound));
			}
			else if (record.kind == TYPE_NAMED)
			{
				const EntityRecord& entity = program_.entities[record.entity];
				if (entity.flavor == NAMED_STRUCT ||
					entity.flavor == NAMED_CLASS ||
					entity.flavor == NAMED_UNION)
				{
					state_.class_rtti_demanded[record.entity] = 1;
					if (entity.direct_base != kNoEntity)
						pending.push_back(program_.entities[
							entity.direct_base].type);
				}
			}
		}
	}

	TypeId DynamicCastTarget(const DumpNode& record) const
	{
		TypeId type = RttiType(record.type);
		const TypeRecord& shape = program_.types.Get(type);
		return RttiType(shape.kind == TYPE_POINTER ? shape.child : type);
	}

	void CollectRttiDemands()
	{
		if (graph_.root >= graph_.arena.nodes.size())
			throw std::logic_error("RTTI demand graph has no root");
		std::vector<std::uint8_t> visited(graph_.arena.nodes.size(), 0);
		std::vector<std::uint32_t> pending(1, graph_.root);
		while (!pending.empty())
		{
			const std::uint32_t node = pending.back();
			pending.pop_back();
			if (node >= graph_.arena.nodes.size())
				throw std::logic_error("RTTI demand graph edge is out of range");
			if (visited[node]) continue;
			visited[node] = 1;
			if (stats_) ++stats_->rtti_graph_nodes_visited;
			const DumpNode& record = graph_.arena.nodes[node];
			if (record.kind == DUMP_TYPEID_EXPRESSION)
			{
				DemandRtti(record.operand_type);
				state_.need_bad_typeid = state_.need_bad_typeid ||
					record.dynamic_type_query;
			}
			else if (record.kind == DUMP_DYNAMIC_CAST_EXPRESSION)
			{
				DemandRtti(record.operand_type);
				DemandRtti(DynamicCastTarget(record));
				state_.need_dynamic_cast = true;
				state_.need_bad_cast = state_.need_bad_cast ||
					record.dynamic_cast_reference;
			}
			for (std::uint32_t edge = record.first_edge;
				edge != kNoDumpEdge; edge = graph_.arena.edges[edge].next)
				pending.push_back(graph_.arena.edges[edge].child);
		}
	}

	bool HasUninlinedDestructorWork(std::uint32_t root) const
	{
		std::vector<std::uint32_t> pending(1, root);
		while (!pending.empty())
		{
			const std::uint32_t node = pending.back();
			pending.pop_back();
			const DumpNode& record = graph_.arena.nodes[node];
			if (record.kind == DUMP_VPTR_INITIALIZATION_ACTION) continue;
			if (record.kind == DUMP_DESTRUCTOR_ACTION &&
				record.base_projection_count == 1 &&
				record.object_binding == kNoBinding &&
				record.lifetime_object == kNoDumpEdge &&
				!record.array_action) continue;
			if (record.kind != DUMP_COMPOUND_STATEMENT) return true;
			for (std::uint32_t edge = record.first_edge;
				edge != kNoDumpEdge; edge = graph_.arena.edges[edge].next)
				pending.push_back(graph_.arena.edges[edge].child);
		}
		return false;
	}

	SymbolId AddSyntheticSymbol(Symbol::Kind kind, const std::string& proposed,
		const std::string& object_name, bool internal)
	{
		std::size_t& count = output_.symbol_name_counts[proposed];
		const std::string name = count++ == 0 ? proposed :
			proposed + "__sym" + std::to_string(count);
		const SymbolId symbol = static_cast<SymbolId>(output_.symbols.size());
		output_.symbols.push_back(Symbol(kind, name, object_name,
			false, internal, false));
		return symbol;
	}

	std::string ClassStem(EntityId entity) const
	{
		std::string spelling = program_.names.Get(
			program_.entities[entity].name);
		const char* dependent_prefixes[] = {
			"template parameter ", "template_parameter_"
		};
		for (std::size_t prefix = 0;
			prefix != sizeof dependent_prefixes / sizeof *dependent_prefixes;
			++prefix) {
			const std::string marker(dependent_prefixes[prefix]);
			for (std::size_t position = spelling.find(marker);
				position != std::string::npos;
				position = spelling.find(marker, position))
				spelling.erase(position, marker.size());
		}
		return SanitizeSymbol(spelling);
	}

	std::string LocalTypeEncoding(EntityId entity) const
	{
		if (!IsFunctionLocalEntity(program_, entity)) return std::string();
		ScopeId scope = program_.entities[entity].owner;
		while (scope != kNoScope &&
			program_.KindOfScope(scope) != SCOPE_FUNCTION)
			scope = program_.ParentScope(scope);
		if (scope == kNoScope) return std::string();
		const std::string function =
			program_.names.Get(program_.NameOfScope(scope));
		const std::string leaf =
			program_.names.Get(program_.entities[entity].identity_name);
		return "Z" + std::to_string(function.size()) + function + "vE" +
			std::to_string(leaf.size()) + leaf;
	}

	std::string VtableName(EntityId entity) const
	{
		const std::string local = LocalTypeEncoding(entity);
		return local.empty() ? ClassStem(entity) + "__vtable" :
			"__vtable_type_" + local;
	}

	std::string ClassFlavor(EntityId entity) const
	{
		return program_.entities[entity].flavor == NAMED_CLASS ?
			"class" : "struct";
	}

	std::string TypeInfoEncoding(EntityId entity) const
	{
		return pa15_lowering_abi::MangleType(
			program_, program_.entities[entity].type);
	}

	bool VtableHasWeakLinkage(EntityId entity) const
	{
		const EntityRecord& owner = program_.entities[entity];
		if (owner.template_argument_count != 0 ||
			IsFunctionLocalEntity(program_, entity)) return true;
		const ClassPolymorphismFacts& facts =
			graph_.class_polymorphism[entity];
		for (std::size_t slot = 0; slot < facts.slots.size(); ++slot)
		{
			const BindingRecord& function = program_.bindings[
				facts.slots[slot].function];
			if (function.pure_virtual || function.inline_function) continue;
			// A non-inline key function uniquely owns an ordinary class's
			// vtable.  Template specializations remain vague-linkage entities.
			return function.weak_odr;
		}
		return true;
	}

	SymbolId AddPolymorphicGlobal(const std::string& name,
		const std::string& object_name, bool weak)
	{
		const SymbolId symbol = AddSyntheticSymbol(
			Symbol::GLOBAL_SYMBOL, name, object_name, false);
		output_.symbols[symbol].weak_linkage = weak;
		output_.symbols[symbol].definition_emitted = true;
		output_.symbols[symbol].referenced = true;
		return symbol;
	}

	SymbolId AddExternalRtti(const std::string& name,
		const std::string& object_name)
	{
		const SymbolId symbol = AddSyntheticSymbol(
			Symbol::GLOBAL_SYMBOL, name, object_name, false);
		output_.symbols[symbol].declaration_emitted = true;
		output_.symbols[symbol].referenced = true;
		GlobalDeclaration declaration;
		declaration.symbol = symbol;
		declaration.typed = false;
		output_.global_declarations.push_back(declaration);
		return symbol;
	}

	SymbolId AddExternalRuntime(const std::string& name,
		const std::string& object_name, const LowType& result,
		const std::vector<LowType>& parameters, bool noreturn)
	{
		const SymbolId symbol = AddSyntheticSymbol(
			Symbol::FUNCTION_SYMBOL, name, object_name, false);
		Symbol& record = output_.symbols[symbol];
		record.c_linkage = true;
		record.declaration_emitted = true;
		record.referenced = true;
		record.noreturn = noreturn;
		if (noreturn) record.effects = Symbol::EFFECTS_READNONE;
		FunctionDeclaration declaration;
		declaration.symbol = symbol;
		declaration.result = result;
		for (std::size_t i = 0; i < parameters.size(); ++i)
		{
			Parameter parameter;
			parameter.name = "arg" + std::to_string(i);
			parameter.type = parameters[i];
			declaration.parameters.push_back(parameter);
		}
		output_.declarations.push_back(declaration);
		return symbol;
	}

	bool IsClassRttiType(TypeId type, EntityId* entity = 0) const
	{
		const TypeRecord& record = program_.types.Get(type);
		if (record.kind != TYPE_NAMED) return false;
		const NamedFlavor flavor = program_.entities[record.entity].flavor;
		if (flavor != NAMED_STRUCT && flavor != NAMED_CLASS &&
			flavor != NAMED_UNION) return false;
		if (entity) *entity = record.entity;
		return true;
	}

	std::string RttiPresentationStem(TypeId type) const
	{
		const TypeRecord& record = program_.types.Get(type);
		if (record.kind == TYPE_FUNDAMENTAL)
			return SanitizeSymbol(program_.RenderType(type));
		return "type_" + pa15_lowering_abi::MangleType(program_, type);
	}

	void RegisterPureVirtual(BindingId prototype)
	{
		if (state_.pure_virtual_symbol != kNoLowId) return;
		state_.pure_virtual_symbol = AddSyntheticSymbol(Symbol::FUNCTION_SYMBOL,
			"__cxa_pure_virtual", std::string(), false);
		Symbol& symbol = output_.symbols[state_.pure_virtual_symbol];
		symbol.declaration_emitted = true;
		symbol.referenced = true;
		symbol.nonthrowing = true;
		symbol.noreturn = true;
		FunctionDeclaration declaration;
		declaration.symbol = state_.pure_virtual_symbol;
		const BindingRecord& binding = program_.bindings[prototype];
		const TypeRecord& type = program_.types.Get(binding.type);
		declaration.result = source_types_.Lower(type.child);
		Parameter object;
		object.name = "arg0";
		object.type = LowPtr();
		declaration.parameters.push_back(object);
		const TypeId* parameters = program_.types.Parameters(binding.type);
		for (std::size_t i = 0; i < type.parameter_count; ++i)
		{
			Parameter parameter;
			parameter.name = "arg" + std::to_string(i + 1);
			parameter.type = source_types_.Lower(parameters[i]);
			parameter.reference = source_types_.IsReference(parameters[i]);
			declaration.parameters.push_back(parameter);
		}
		declaration.variadic = type.variadic;
		output_.declarations.push_back(declaration);
	}

	void RegisterSymbols()
	{
		bool need_root_rtti = false;
		bool need_si_rtti = false;
		bool need_vmi_rtti = false;
		for (EntityId entity = 0;
			entity < graph_.class_polymorphism.size(); ++entity)
		{
			const ClassPolymorphismFacts& facts =
				graph_.class_polymorphism[entity];
			if (!facts.vtable_demanded || facts.slots.empty()) continue;
			EntityId dependency = entity;
			for (std::size_t depth = 0; dependency != kNoEntity &&
				depth <= program_.entities.size(); ++depth)
			{
				state_.class_rtti_demanded[dependency] = 1;
				dependency = program_.entities[dependency].direct_base;
			}
			state_.class_vtable_symbols[entity] = AddPolymorphicGlobal(
				VtableName(entity), "_ZTV" + TypeInfoEncoding(entity),
				VtableHasWeakLinkage(entity));
			for (std::size_t slot = 0; slot < facts.slots.size(); ++slot)
			{
				if (program_.bindings[facts.slots[slot].function].pure_virtual)
					RegisterPureVirtual(facts.slots[slot].root);
				if (!program_.bindings[facts.slots[slot].function].destructor ||
					state_.deleting_destructor_symbols[entity] != kNoLowId)
					continue;
				const BindingId destructor = program_.bindings[
					facts.slots[slot].function].canonical;
				std::string object_name;
				if (destructor < function_symbols_.size() &&
					function_symbols_[destructor] != kNoLowId)
				{
					object_name = output_.symbols[
						function_symbols_[destructor]].object_name;
					const std::size_t marker = object_name.rfind("D1E");
					if (marker != std::string::npos)
						object_name.replace(marker, 3, "D0E");
				}
				const std::string leaf =
					program_.names.Get(program_.entities[entity].identity_name);
				state_.deleting_destructor_symbols[entity] = AddSyntheticSymbol(
					Symbol::FUNCTION_SYMBOL,
					ClassStem(entity) + "___" + leaf + "__deleting_entry",
					object_name, false);
			}
		}
		for (EntityId entity = 0;
			entity < state_.class_rtti_demanded.size(); ++entity)
		{
			if (!state_.class_rtti_demanded[entity]) continue;
			const EntityRecord& record = program_.entities[entity];
			need_root_rtti = need_root_rtti || record.direct_base == kNoEntity;
			need_si_rtti = need_si_rtti || (record.direct_base != kNoEntity &&
				record.direct_base_offset == 0);
			need_vmi_rtti = need_vmi_rtti || (record.direct_base != kNoEntity &&
				record.direct_base_offset != 0);
			const std::string encoding = TypeInfoEncoding(entity);
			const std::string local = LocalTypeEncoding(entity);
			const bool generic_type = !local.empty() || !record.layout_complete;
			const std::string stem = !local.empty() ? local :
				generic_type ? encoding : ClassStem(entity);
			const std::string flavor = generic_type ?
				"type" : ClassFlavor(entity);
			state_.class_type_name_symbols[entity] = AddPolymorphicGlobal(
				(generic_type ? "__typeinfo_name_" : "__typeinfo_name__") +
				flavor + "_" + stem, "_ZTS" + encoding, true);
			state_.class_rtti_symbols[entity] = AddPolymorphicGlobal(
				"__rtti_" + flavor + "_" + stem, "_ZTI" + encoding, true);
			const TypeId type = record.type;
			if (type < state_.type_rtti_symbols.size())
			{
				state_.type_name_symbols[type] =
					state_.class_type_name_symbols[entity];
				state_.type_rtti_symbols[type] =
					state_.class_rtti_symbols[entity];
			}
		}
		bool need_fundamental_rtti = false;
		bool need_pointer_rtti = false;
		bool need_enum_rtti = false;
		bool need_array_rtti = false;
		bool need_function_rtti = false;
		bool need_member_pointer_rtti = false;
		for (TypeId type = 0; type < state_.type_rtti_demanded.size(); ++type)
		{
			if (!state_.type_rtti_demanded[type] ||
				state_.type_rtti_symbols[type] != kNoLowId) continue;
			const TypeRecord& record = program_.types.Get(type);
			need_fundamental_rtti = need_fundamental_rtti ||
				record.kind == TYPE_FUNDAMENTAL;
			need_pointer_rtti = need_pointer_rtti ||
				record.kind == TYPE_POINTER;
			need_enum_rtti = need_enum_rtti ||
				(record.kind == TYPE_NAMED && !IsClassRttiType(type));
			need_array_rtti = need_array_rtti || record.kind == TYPE_ARRAY;
			need_function_rtti = need_function_rtti ||
				record.kind == TYPE_FUNCTION;
			need_member_pointer_rtti = need_member_pointer_rtti ||
				record.kind == TYPE_MEMBER_POINTER;
			if (record.kind != TYPE_FUNDAMENTAL &&
				record.kind != TYPE_POINTER && record.kind != TYPE_ARRAY &&
				record.kind != TYPE_FUNCTION &&
				record.kind != TYPE_MEMBER_POINTER &&
				!(record.kind == TYPE_NAMED && !IsClassRttiType(type)))
				throw std::logic_error("demanded RTTI type has no ABI category");
			const std::string encoding =
				pa15_lowering_abi::MangleType(program_, type);
			const std::string stem = RttiPresentationStem(type);
			state_.type_name_symbols[type] = AddPolymorphicGlobal(
				std::string("__typeinfo_name_") +
				(record.kind == TYPE_FUNDAMENTAL ? "_" : "") + stem,
				"_ZTS" + encoding, true);
			state_.type_rtti_symbols[type] = AddPolymorphicGlobal(
				"__rtti_" + stem, "_ZTI" + encoding, true);
		}
		if (need_root_rtti)
			state_.rtti_class_symbol = AddExternalRtti(
				"__external_rtti_vtable____class_type_info",
				"_ZTVN10__cxxabiv117__class_type_infoE");
		if (need_si_rtti)
			state_.rtti_si_symbol = AddExternalRtti(
				"__external_rtti_vtable____si_class_type_info",
				"_ZTVN10__cxxabiv120__si_class_type_infoE");
		if (need_vmi_rtti)
			state_.rtti_vmi_symbol = AddExternalRtti(
				"__external_rtti_vtable____vmi_class_type_info",
				"_ZTVN10__cxxabiv121__vmi_class_type_infoE");
		if (need_fundamental_rtti)
			state_.rtti_fundamental_symbol = AddExternalRtti(
				"__external_rtti_vtable____fundamental_type_info",
				"_ZTVN10__cxxabiv123__fundamental_type_infoE");
		if (need_pointer_rtti)
			state_.rtti_pointer_symbol = AddExternalRtti(
				"__external_rtti_vtable____pointer_type_info",
				"_ZTVN10__cxxabiv119__pointer_type_infoE");
		if (need_enum_rtti)
			state_.rtti_enum_symbol = AddExternalRtti(
				"__external_rtti_vtable____enum_type_info",
				"_ZTVN10__cxxabiv116__enum_type_infoE");
		if (need_array_rtti)
			state_.rtti_array_symbol = AddExternalRtti(
				"__external_rtti_vtable____array_type_info",
				"_ZTVN10__cxxabiv117__array_type_infoE");
		if (need_function_rtti)
			state_.rtti_function_symbol = AddExternalRtti(
				"__external_rtti_vtable____function_type_info",
				"_ZTVN10__cxxabiv120__function_type_infoE");
		if (need_member_pointer_rtti)
			state_.rtti_member_pointer_symbol = AddExternalRtti(
				"__external_rtti_vtable____pointer_to_member_type_info",
				"_ZTVN10__cxxabiv129__pointer_to_member_type_infoE");
		if (state_.need_dynamic_cast)
			state_.dynamic_cast_symbol = AddExternalRuntime(
				"__external_runtime____dynamic_cast", "__dynamic_cast",
				LowPtr(), std::vector<LowType>{
					LowPtr(), LowPtr(), LowPtr(), LowI64()}, false);
		if (state_.need_bad_cast)
			state_.bad_cast_symbol = AddExternalRuntime(
				"__external_runtime____cxa_bad_cast", "__cxa_bad_cast",
				LowVoid(), std::vector<LowType>(), true);
		if (state_.need_bad_typeid)
			state_.bad_typeid_symbol = AddExternalRuntime(
				"__external_runtime____cxa_bad_typeid", "__cxa_bad_typeid",
				LowVoid(), std::vector<LowType>(), true);
	}

	void AddAddressItem(Global* global, SymbolId symbol, std::int64_t offset = 0)
	{
		Global::DataItem item;
		item.kind = Global::DataItem::ADDRESS_ITEM;
		item.type = LowPtr();
		item.symbol = symbol;
		item.offset = offset;
		global->items.push_back(item);
		output_.symbols[symbol].referenced = true;
	}

	void AddIntegerItem(Global* global, const LowType& type, std::int64_t value)
	{
		Global::DataItem item;
		item.kind = Global::DataItem::INTEGER_ITEM;
		item.type = type;
		item.integer_value = value;
		global->items.push_back(item);
	}

	void EmitGlobals()
	{
		for (EntityId entity = 0;
			entity < state_.class_rtti_demanded.size(); ++entity)
		{
			if (!state_.class_rtti_demanded[entity]) continue;
			const EntityRecord& record = program_.entities[entity];
			Global name;
			name.symbol = state_.class_type_name_symbols[entity];
			name.initializer_kind = Global::STRUCTURED_VALUE;
			const std::string encoding = TypeInfoEncoding(entity);
			for (std::size_t i = 0; i < encoding.size(); ++i)
				AddIntegerItem(&name, LowI8(),
					static_cast<unsigned char>(encoding[i]));
			AddIntegerItem(&name, LowI8(), 0);
			output_.globals.push_back(name);

			Global rtti;
			rtti.symbol = state_.class_rtti_symbols[entity];
			rtti.initializer_kind = Global::STRUCTURED_VALUE;
			if (record.direct_base == kNoEntity)
				AddAddressItem(&rtti, state_.rtti_class_symbol, 16);
			else if (record.direct_base_offset == 0)
				AddAddressItem(&rtti, state_.rtti_si_symbol, 16);
			else AddAddressItem(&rtti, state_.rtti_vmi_symbol, 16);
			AddAddressItem(&rtti, state_.class_type_name_symbols[entity]);
			if (record.direct_base != kNoEntity && record.direct_base_offset == 0)
				AddAddressItem(&rtti,
					state_.class_rtti_symbols[record.direct_base]);
			else if (record.direct_base != kNoEntity)
			{
				AddIntegerItem(&rtti, LowI32(), 0);
				AddIntegerItem(&rtti, LowI32(), 1);
				AddAddressItem(&rtti,
					state_.class_rtti_symbols[record.direct_base]);
				AddIntegerItem(&rtti, LowI64(), static_cast<std::int64_t>(
					record.direct_base_offset * 256 + 2));
			}
			output_.globals.push_back(rtti);
			if (stats_) stats_->globals += 2;
		}

		for (TypeId type = 0; type < state_.type_rtti_demanded.size(); ++type)
		{
			if (!state_.type_rtti_demanded[type] || IsClassRttiType(type))
				continue;
			const TypeRecord& record = program_.types.Get(type);
			Global name;
			name.symbol = state_.type_name_symbols[type];
			name.initializer_kind = Global::STRUCTURED_VALUE;
			const std::string encoding =
				pa15_lowering_abi::MangleType(program_, type);
			for (std::size_t i = 0; i < encoding.size(); ++i)
				AddIntegerItem(&name, LowI8(),
					static_cast<unsigned char>(encoding[i]));
			AddIntegerItem(&name, LowI8(), 0);
			output_.globals.push_back(name);

			Global rtti;
			rtti.symbol = state_.type_rtti_symbols[type];
			rtti.initializer_kind = Global::STRUCTURED_VALUE;
			const SymbolId runtime = record.kind == TYPE_FUNDAMENTAL ?
				state_.rtti_fundamental_symbol : record.kind == TYPE_POINTER ?
				state_.rtti_pointer_symbol : record.kind == TYPE_ARRAY ?
				state_.rtti_array_symbol : record.kind == TYPE_FUNCTION ?
				state_.rtti_function_symbol : record.kind == TYPE_MEMBER_POINTER ?
				state_.rtti_member_pointer_symbol : state_.rtti_enum_symbol;
			if (runtime == kNoLowId)
				throw std::logic_error("demanded RTTI kind has no ABI runtime");
			AddAddressItem(&rtti, runtime, 16);
			AddAddressItem(&rtti, state_.type_name_symbols[type]);
			if (record.kind == TYPE_POINTER ||
				record.kind == TYPE_MEMBER_POINTER)
			{
				std::uint32_t flags = 0;
				const TypeRecord& pointee = program_.types.Get(record.child);
				if (pointee.kind == TYPE_QUALIFIED)
				{
					if ((pointee.cv & CV_CONST) != 0) flags |= 1;
					if ((pointee.cv & CV_VOLATILE) != 0) flags |= 2;
				}
				const TypeId child = RttiType(record.child);
				EntityId child_entity = kNoEntity;
				if (IsClassRttiType(child, &child_entity) &&
					!program_.entities[child_entity].layout_complete)
					flags |= 8;
				TypeId context = kNoType;
				if (record.kind == TYPE_MEMBER_POINTER)
				{
					context = RttiType(record.bound);
					EntityId context_entity = kNoEntity;
					if (IsClassRttiType(context, &context_entity) &&
						!program_.entities[context_entity].layout_complete)
						flags |= 16;
				}
				AddIntegerItem(&rtti, LowI32(), flags);
				if (child >= state_.type_rtti_symbols.size() ||
					state_.type_rtti_symbols[child] == kNoLowId)
					throw std::logic_error(
						"pointer RTTI pointee was not demanded");
				AddAddressItem(&rtti, state_.type_rtti_symbols[child]);
				if (record.kind == TYPE_MEMBER_POINTER)
				{
					if (context >= state_.type_rtti_symbols.size() ||
						state_.type_rtti_symbols[context] == kNoLowId)
						throw std::logic_error(
							"member-pointer RTTI context was not demanded");
					AddAddressItem(&rtti,
						state_.type_rtti_symbols[context]);
				}
			}
			output_.globals.push_back(rtti);
			if (stats_) stats_->globals += 2;
		}

		for (EntityId entity = 0;
			entity < graph_.class_polymorphism.size(); ++entity)
		{
			const ClassPolymorphismFacts& facts =
				graph_.class_polymorphism[entity];
			if (!facts.vtable_demanded || facts.slots.empty()) continue;
			Global vtable;
			vtable.symbol = state_.class_vtable_symbols[entity];
			vtable.initializer_kind = Global::STRUCTURED_VALUE;
			AddIntegerItem(&vtable, LowI64(), 0);
			AddAddressItem(&vtable, state_.class_rtti_symbols[entity]);
			for (std::size_t slot = 0; slot < facts.slots.size(); ++slot)
			{
				const BindingId function = program_.bindings[
					facts.slots[slot].function].canonical;
				if (program_.bindings[function].pure_virtual)
					AddAddressItem(&vtable, state_.pure_virtual_symbol);
				else
				{
					if (function >= function_symbols_.size() ||
						function_symbols_[function] == kNoLowId)
						throw std::logic_error(
							"virtual slot has no lowered function symbol");
					AddAddressItem(&vtable, function_symbols_[function]);
				}
				if (program_.bindings[function].destructor)
					AddAddressItem(&vtable,
						state_.deleting_destructor_symbols[entity]);
				if (stats_) stats_->vtable_slots +=
					program_.bindings[function].destructor ? 2 : 1;
			}
			output_.globals.push_back(vtable);
			if (stats_) ++stats_->globals;
		}
	}

	const SemanticGraphView& graph_;
	const Program& program_;
	TypedProgram& output_;
	LowIRLoweringStats* stats_;
	std::size_t source_ordinal_;
	const std::vector<SymbolId>& function_symbols_;
	PolymorphismLoweringState& state_;
	pa15_lowering_detail::SourceTypeLowering source_types_;
};

class DeletingDestructorBuilder
{
public:
	DeletingDestructorBuilder(const SemanticGraphView& graph,
		TypedProgram& output, LowIRLoweringStats* stats,
		const std::vector<SymbolId>& function_symbols,
		PolymorphismLoweringState* state)
		: program_(graph.program), output_(output), stats_(stats),
		  function_symbols_(function_symbols), state_(*state), function_(0),
		  current_block_(0), temp_counter_(0), block_counter_(0),
		  source_types_(program_)
	{
	}

	void EmitAll()
	{
		const SymbolId global_deallocation = OperatorDeleteSymbol();
		for (EntityId entity = 0;
			entity < state_.deleting_destructor_symbols.size(); ++entity)
		{
			const SymbolId symbol = state_.deleting_destructor_symbols[entity];
			if (symbol == kNoLowId) continue;
			const BindingId deallocation_binding =
				state_.deallocation_bindings[entity];
			SymbolId deallocation = global_deallocation;
			if (deallocation_binding != kNoBinding &&
				deallocation_binding < function_symbols_.size())
				deallocation = function_symbols_[deallocation_binding];
			if (deallocation == kNoLowId)
				throw std::logic_error(
					"deleting destructor dependencies are not emitted");
			EmitOne(entity, symbol, deallocation_binding, deallocation);
		}
		MergeDeletingDestructors();
	}

private:
	BlockId AddBlock(const std::string& label)
	{
		const BlockId block =
			static_cast<BlockId>(function_->blocks.size());
		function_->blocks.push_back(Block(label));
		return block;
	}

	void SelectBlock(BlockId block)
	{
		current_block_ = block;
		Block& selected = function_->blocks[block];
		if (!selected.selected)
		{
			selected.selected = true;
			function_->block_order.push_back(block);
		}
	}

	std::string NewLabel(const std::string& prefix)
	{
		return prefix + "_" + std::to_string(++block_counter_);
	}

	Operand Temp(const LowType& type)
	{
		return Operand(static_cast<TempId>(++temp_counter_), type);
	}

	void Emit(const Instruction& instruction)
	{
		function_->blocks[current_block_].instructions.push_back(instruction);
		if (IsTerminator(instruction))
			function_->blocks[current_block_].terminated = true;
		if (stats_) ++stats_->instructions;
	}

	void EmitJump(BlockId target)
	{
		Instruction jump(Instruction::JUMP);
		jump.target = target;
		Emit(jump);
	}

	void EmitEhTarget(Instruction::Kind kind, BlockId target)
	{
		Instruction instruction(kind);
		instruction.target = target;
		Emit(instruction);
	}

	Operand Load(const Operand& storage)
	{
		const Operand result = Temp(LowPtr());
		Instruction load(Instruction::LOAD);
		load.dest = result.id;
		load.type = LowPtr();
		load.first = storage;
		Emit(load);
		return result;
	}

	void AttachCallArguments(Instruction* call,
		const std::vector<Operand>& arguments)
	{
		call->extra_first =
			static_cast<std::uint32_t>(output_.call_arguments.size());
		call->extra_count = static_cast<std::uint32_t>(arguments.size());
		for (std::size_t i = 0; i < arguments.size(); ++i)
		{
			output_.call_arguments.push_back(arguments[i]);
			output_.call_argument_references.push_back(0);
		}
	}

	void EmitVoidCall(SymbolId symbol, const Operand& object)
	{
		Instruction call(Instruction::CALL);
		call.type = LowVoid();
		call.first = Operand(Operand::FUNCTION, symbol, LowPtr());
		std::vector<Operand> arguments(1, object);
		AttachCallArguments(&call, arguments);
		output_.symbols[symbol].referenced = true;
		Emit(call);
	}

	void EmitDeallocation(SymbolId symbol, BindingId binding,
		EntityId entity, const Operand& object)
	{
		Instruction call(Instruction::CALL);
		call.type = LowVoid();
		call.first = Operand(Operand::FUNCTION, symbol, LowPtr());
		std::vector<Operand> arguments(1, object);
		if (binding != kNoBinding)
		{
			const TypeRecord& type = program_.types.Get(
				program_.bindings[binding].type);
			if (type.parameter_count == 2)
			{
				const TypeId* parameters = program_.types.Parameters(
					program_.bindings[binding].type);
				arguments.push_back(Operand(static_cast<std::int64_t>(
					program_.entities[entity].object_size),
					source_types_.Lower(parameters[1])));
			}
		}
		AttachCallArguments(&call, arguments);
		output_.symbols[symbol].referenced = true;
		Emit(call);
	}

	void EmitVptrStore(EntityId entity, const Operand& object)
	{
		if (stats_) ++stats_->vptr_stores;
		const SymbolId symbol = state_.class_vtable_symbols[entity];
		output_.symbols[symbol].referenced = true;
		const Operand table = Temp(LowPtr());
		Instruction address(Instruction::ADDR);
		address.dest = table.id;
		address.first = Operand(Operand::GLOBAL, symbol, LowPtr());
		Emit(address);
		const Operand address_point = Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = address_point.id;
		index.type = LowI8();
		index.first = table;
		index.second = Operand(16, LowI64());
		Emit(index);
		Instruction store(Instruction::STORE);
		store.type = LowPtr();
		store.first = address_point;
		store.second = object;
		Emit(store);
	}

	Operand ProjectBase(const Operand& object, EntityId entity)
	{
		const Operand projected = Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = projected.id;
		index.type = LowI8();
		index.first = object;
		index.second = Operand(static_cast<std::int64_t>(
			program_.entities[entity].direct_base_offset), LowI64());
		index.projection = INDEX_PROJECTION_BASE_SUBOBJECT;
		Emit(index);
		return projected;
	}

	SymbolId OperatorDeleteSymbol() const
	{
		for (BindingId binding = 0; binding < program_.bindings.size(); ++binding)
			if (program_.bindings[binding].builtin_function ==
				BUILTIN_FUNCTION_OPERATOR_DELETE &&
				binding < function_symbols_.size() &&
				function_symbols_[binding] != kNoLowId)
				return function_symbols_[binding];
		return kNoLowId;
	}

	void EmitOne(EntityId entity, SymbolId symbol,
		BindingId deallocation_binding, SymbolId deallocation)
	{
		Function result;
		result.symbol = symbol;
		result.result = LowVoid();
		Parameter parameter;
		parameter.name = "this";
		parameter.type = LowPtr();
		result.parameters.push_back(parameter);
		Slot this_record;
		this_record.name = "this";
		this_record.type = LowPtr();
		result.slots.push_back(this_record);
		function_ = &result;
		temp_counter_ = 0;
		block_counter_ = 0;
		SelectBlock(AddBlock("entry"));
		const Operand this_slot(SlotId(0), LowPtr());
		Instruction save(Instruction::STORE);
		save.type = LowPtr();
		save.first = Operand(ParameterId(0), LowPtr());
		save.second = this_slot;
		Emit(save);
		const BlockId cleanup = AddBlock(NewLabel("destructor_cleanup"));
		const BlockId end = AddBlock(NewLabel("destructor_end"));
		const bool call_complete =
			entity < state_.deleting_destructor_calls_complete.size() &&
			state_.deleting_destructor_calls_complete[entity] != 0;
		SymbolId complete_destructor = kNoLowId;
		if (call_complete)
		{
			const BindingId complete =
				state_.complete_destructor_bindings[entity];
			if (complete != kNoBinding && complete < function_symbols_.size())
				complete_destructor = function_symbols_[complete];
			if (complete_destructor == kNoLowId)
				throw std::logic_error(
					"deleting destructor has no complete entry");
		}
		EmitEhTarget(Instruction::EH_CLEANUP, cleanup);
		if (call_complete)
			EmitVoidCall(complete_destructor, Load(this_slot));
		else EmitVptrStore(entity, Load(this_slot));
		Emit(Instruction(Instruction::EH_END));

		const EntityId base = program_.entities[entity].direct_base;
		SymbolId base_destructor = kNoLowId;
		if (base != kNoEntity)
		{
			BindingId base_binding = state_.base_destructor_bindings[base];
			if (base_binding == kNoBinding)
				base_binding = state_.complete_destructor_bindings[base];
			if (base_binding != kNoBinding &&
				base_binding < function_symbols_.size())
				base_destructor = function_symbols_[base_binding];
		}
		if (!call_complete && base_destructor != kNoLowId)
		{
			const BlockId suffix_cleanup = AddBlock(
				NewLabel("destructor_suffix_cleanup"));
			const BlockId suffix_next = AddBlock(
				NewLabel("destructor_suffix_next"));
			EmitEhTarget(Instruction::EH_CLEANUP, suffix_cleanup);
			EmitVoidCall(base_destructor, ProjectBase(Load(this_slot), entity));
			Emit(Instruction(Instruction::EH_END));
			EmitJump(suffix_next);
			SelectBlock(suffix_cleanup);
			EmitDeallocation(deallocation, deallocation_binding,
				entity, Load(this_slot));
			Emit(Instruction(Instruction::EH_END));
			Emit(Instruction(Instruction::RESUME));
			SelectBlock(suffix_next);
		}
		EmitDeallocation(deallocation, deallocation_binding,
			entity, Load(this_slot));
		EmitJump(end);
		SelectBlock(cleanup);
		if (!call_complete && base_destructor != kNoLowId)
			EmitVoidCall(base_destructor,
				ProjectBase(Load(this_slot), entity));
		EmitDeallocation(deallocation, deallocation_binding,
			entity, Load(this_slot));
		Emit(Instruction(Instruction::EH_END));
		Emit(Instruction(Instruction::RESUME));
		SelectBlock(end);
		Emit(Instruction(Instruction::RETURN_VOID));
		if (stats_)
		{
			++stats_->functions;
			++stats_->deleting_destructors;
			stats_->blocks += result.block_order.size();
		}
		function_ = 0;
		output_.symbols[symbol].definition_emitted = true;
		deleting_functions_.push_back(std::move(result));
		const BindingId complete =
			state_.complete_destructor_bindings[entity];
		SymbolId complete_symbol = kNoLowId;
		if (complete != kNoBinding && complete < function_symbols_.size())
			complete_symbol = function_symbols_[complete];
		deleting_complete_symbols_.push_back(complete_symbol);
	}

	void MergeDeletingDestructors()
	{
		if (deleting_functions_.empty()) return;
		const std::size_t first = state_.source_function_first;
		if (first > output_.functions.size())
			throw std::logic_error("invalid PA18 function ordering boundary");
		const std::size_t missing =
			std::numeric_limits<std::size_t>::max();
		std::vector<std::size_t> pending_by_symbol(
			output_.symbols.size(), missing);
		for (std::size_t i = 0; i < deleting_complete_symbols_.size(); ++i)
		{
			const SymbolId symbol = deleting_complete_symbols_[i];
			if (symbol != kNoLowId && symbol < pending_by_symbol.size())
				pending_by_symbol[symbol] = i;
		}
		std::vector<std::uint8_t> emitted(deleting_functions_.size(), 0);
		std::vector<Function> ordered;
		ordered.reserve(output_.functions.size() - first +
			deleting_functions_.size());
		for (std::size_t i = first; i < output_.functions.size(); ++i)
		{
			const SymbolId symbol = output_.functions[i].symbol;
			const std::size_t pending = symbol < pending_by_symbol.size() ?
				pending_by_symbol[symbol] : missing;
			if (pending != missing)
			{
				ordered.push_back(std::move(deleting_functions_[pending]));
				emitted[pending] = 1;
			}
			ordered.push_back(std::move(output_.functions[i]));
		}
		for (std::size_t i = 0; i < deleting_functions_.size(); ++i)
			if (!emitted[i])
				ordered.push_back(std::move(deleting_functions_[i]));
		output_.functions.resize(first + ordered.size());
		for (std::size_t i = 0; i < ordered.size(); ++i)
			output_.functions[first + i] = std::move(ordered[i]);
	}

	const Program& program_;
	TypedProgram& output_;
	LowIRLoweringStats* stats_;
	const std::vector<SymbolId>& function_symbols_;
	PolymorphismLoweringState& state_;
	Function* function_;
	BlockId current_block_;
	std::size_t temp_counter_;
	std::size_t block_counter_;
	pa15_lowering_detail::SourceTypeLowering source_types_;
	std::vector<Function> deleting_functions_;
	std::vector<SymbolId> deleting_complete_symbols_;
};

}

void PreparePolymorphism(const SemanticGraphView& graph,
	TypedProgram& output, LowIRLoweringStats* stats,
	std::size_t source_ordinal, const std::vector<SymbolId>& function_symbols,
	PolymorphismLoweringState* state)
{
	GlobalEmitter(graph, output, stats, source_ordinal,
		function_symbols, state).Prepare();
}

void EmitDeletingDestructors(const SemanticGraphView& graph,
	TypedProgram& output, LowIRLoweringStats* stats,
	const std::vector<SymbolId>& function_symbols,
	PolymorphismLoweringState* state)
{
	DeletingDestructorBuilder(graph, output, stats,
		function_symbols, state).EmitAll();
}

}
}
