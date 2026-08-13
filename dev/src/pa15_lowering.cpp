#include "pa15_lowering.h"
#include "pa15_graph_lowering.h"
#include "pa15_control_flow_lowering.h"
#include "pa15_lowering_abi.h"
#include "pa15_lowering_support.h"
#include "pa15_scalar_unary_lowering.h"
#include "pa15_source_type_lowering.h"
#include "pa11_model.h"
#include "pa12_semantic.h"
#include "pa12_semantic_model.h"
#include "pa16_array_lifetime_lowering.h"
#include "pa16_aggregate_helper_lowering.h"
#include "pa16_assignment_lowering.h"
#include "pa16_call_argument_lowering.h"
#include "pa16_constructor_lowering.h"
#include "pa16_destructor_action_lowering.h"
#include "pa16_initialization_lowering.h"
#include "pa16_lifetime_lowering.h"
#include "pa16_member_address_lowering.h"
#include "pa16_static_initializer_lowering.h"
#include "pa16_slot_planning.h"
#include "pa17_bit_field_value_lowering.h"
#include "pa17_control_expression_lowering.h"
#include "pa17_value_boundary_lowering.h"
#include "pa17_special_member_lowering.h"
#include "pa17_temporary_lifetime_lowering.h"
#include "pa18_polymorphism_lowering.h"
#include "pa21_constant_lowering.h"
#include "pa21_local_static_lowering.h"
#include "pa25_range_for_lowering.h"
#include "pa26_exception_lowering.h"
#include "pa26_initializer_list_lowering.h"
#include "pa26_rtti_lowering.h"
#include "pa27_member_pointer_lowering.h"
#include "pa28_virtual_base_lowering.h"
#include "pa30_region_lowering.h"
#include "pa33_static_lifecycle_lowering.h"
#include "pa34_gnu_asm_lowering.h"
#include "pa34_complex_lowering.h"
#include <algorithm>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace cppgm { namespace {
using namespace pa11; using namespace pa12_semantic_detail;
using namespace pa15_lowir_detail; using namespace pa15_lowering_support;
const std::size_t kAggregateProjectionReplayLimit = 8;
typedef SmallSequence<BindingId, kAggregateProjectionReplayLimit> AggregatePath;
class GraphLowerer :
	private pa28_lowering_detail::VirtualBaseLowering<GraphLowerer>,
	private pa15_lowering_detail::ControlFlowLowering<GraphLowerer>,
	private pa15_lowering_detail::ScalarUnaryLowering<GraphLowerer>,
	private pa18_lowering_detail::PolymorphismActionLowering<GraphLowerer>,
	private pa17_lowering_detail::BitFieldValueLowering<GraphLowerer>,
	private pa17_lowering_detail::ControlExpressionLowering<GraphLowerer>,
	private pa17_lowering_detail::ValueBoundaryLowering<GraphLowerer>,
	private pa17_lowering_detail::SpecialMemberLowering<GraphLowerer>,
	private pa16_lowering_detail::AssignmentLowering<GraphLowerer>,
	private pa16_lowering_detail::AggregateHelperLowering<GraphLowerer>,
	private pa16_lowering_detail::ConstructorActionLowering<GraphLowerer>,
	private pa16_lowering_detail::ArrayLifetimeLowering<GraphLowerer>,
	private pa16_lowering_detail::DestructorActionLowering<GraphLowerer>,
	private pa16_lowering_detail::CallArgumentLowering<GraphLowerer>,
	private pa16_lowering_detail::InitializationLowering<GraphLowerer>,
	private pa16_lowering_detail::LifetimeActionLowering<GraphLowerer>,
	private pa16_lowering_detail::MemberAddressLowering<GraphLowerer>,
	private pa16_lowering_detail::SlotPlanning<GraphLowerer>,
	private pa17_lowering_detail::TemporaryLifetimeLowering<GraphLowerer>,
	private pa21_lowering_detail::ConstantLowering<GraphLowerer>,
	private pa21_lowering_detail::LocalStaticLowering<GraphLowerer>,
	private pa25_lowering_detail::RangeForLowering<GraphLowerer>,
	private pa26_lowering_detail::ExceptionLowering<GraphLowerer>,
	private pa26_lowering_detail::InitializerListLowering<GraphLowerer>,
	private pa26_lowering_detail::RttiLowering<GraphLowerer>,
	private pa27_lowering_detail::MemberPointerLowering<GraphLowerer>,
	private pa30_lowering_detail::RegionLowering<GraphLowerer>,
	private pa33_lowering_detail::StaticLifecycleLowering<GraphLowerer>,
	private pa34_lowering_detail::GnuAsmLowering<GraphLowerer>,
	private pa34_lowering_detail::ComplexLowering<GraphLowerer>
{
public:
	GraphLowerer(const SemanticGraphView& graph, TypedProgram& output,
		LowIRLoweringStats* stats, std::size_t source_ordinal)
		: graph_(graph), program_(graph.program), arena_(graph.arena),
		  output_(output), stats_(stats), function_(0), current_block_(0), current_result_(LowVoid()),
		  current_result_reference_(false), current_indirect_result_(false),
		  temp_counter_(0), block_counter_(0), generated_slot_ordinal_(0), initialized_bit_field_owner_(kNoEntity),
		  initialized_bit_field_offset_(0), initialized_bit_field_unit_valid_(false),
		  source_ordinal_(source_ordinal), needs_global_class_initializer_(false), lowering_namespace_object_(false),
		  lowering_thread_local_initializer_object_(kNoLowId),
		  current_class_value_boundary_(false), current_this_binding_(kNoBinding), current_member_owner_(kNoEntity),
		  destructor_return_target_(kNoLowId),
		  destructor_return_routes_to_epilogue_(false),
		  full_expression_cleanup_active_(false), full_expression_cleanup_dispatch_(kNoLowId),
		  full_expression_cleanup_end_(kNoLowId), full_expression_linked_cleanup_dispatch_(kNoLowId),
		  full_expression_cleanup_dispatch_reused_(false), full_expression_tracks_lifetime_state_(false),
		  full_expression_uses_linked_dispatch_(false), full_expression_uses_branch_cleanup_(false),
		  full_expression_cleanup_ready_(false), full_expression_deferred_cleanup_(false),
		  full_expression_linked_action_cursor_(0), runtime_lifetime_cleanup_dispatch_(kNoLowId), conditional_cleanup_resume_(kNoLowId),
		  source_types_(program_),
		  static_initializers_(program_, arena_, output_, stats_,
			function_symbols_, global_symbols_, literal_symbols_,
			function_definition_, polymorphism_.class_vtable_symbols)
	{
		function_symbols_.resize(program_.bindings.size(), kNoLowId);
		global_symbols_.resize(program_.bindings.size(), kNoLowId);
		literal_symbols_.resize(arena_.nodes.size(), kNoLowId);
		temporary_initialized_.resize(arena_.nodes.size(), 0);
		temporary_addresses_.resize(arena_.nodes.size());
		full_expression_branch_cleanup_next_.resize(
			arena_.nodes.size(), kNoDumpEdge);
		function_definition_.resize(program_.bindings.size(), kNoDumpEdge);
		function_declaration_.resize(program_.bindings.size(), kNoDumpEdge);
		virtual_base_contracts_.Reset(program_.bindings.size(), arena_.nodes.size());
		global_node_.resize(program_.bindings.size(), kNoDumpEdge);
		namespace_action_.resize(program_.bindings.size(), kNoDumpEdge);
		thread_local_dynamic_.resize(graph_.namespace_objects.size(), 0);
		for (std::size_t i = 0; i < graph_.namespace_objects.size(); ++i)
		{
			const NamespaceObjectAction& action = graph_.namespace_objects[i];
			if (action.object >= program_.bindings.size())
				throw std::logic_error("invalid namespace object identity");
			const BindingId canonical = program_.bindings[action.object].canonical;
			if (canonical >= namespace_action_.size())
				throw std::logic_error("invalid canonical namespace object identity");
			namespace_action_[canonical] = static_cast<std::uint32_t>(i);
			namespace_action_[action.object] = static_cast<std::uint32_t>(i);
		}
		local_static_action_.resize(program_.bindings.size(), kNoDumpEdge);
		local_static_guard_symbols_.resize(
			graph_.local_static_objects.size(), kNoLowId);
		local_static_destructor_symbols_.resize(
			graph_.local_static_objects.size(), kNoLowId);
		local_static_dynamic_.resize(graph_.local_static_objects.size(), 0);
		local_static_emitted_.resize(graph_.local_static_objects.size(), 0);
		for (std::size_t i = 0; i < graph_.local_static_objects.size(); ++i)
		{
			const LocalStaticObjectAction& action = graph_.local_static_objects[i];
			if (action.object >= program_.bindings.size())
				throw std::logic_error("invalid local static object identity");
			if (local_static_action_[action.object] != kNoDumpEdge)
				throw std::logic_error("duplicate local static object action");
			local_static_action_[action.object] = static_cast<std::uint32_t>(i);
		}
		binding_slots_.resize(program_.bindings.size(), kNoLowId);
		binding_indirect_parameters_.resize(program_.bindings.size());
		generated_slots_.resize(arena_.nodes.size(), kNoLowId);
		switch_case_blocks_.resize(arena_.nodes.size(), kNoLowId);
		bit_field_storage_transfer_owners_.resize(
			program_.entities.size(), 0);
		aggregate_helper_symbols_.resize(
			graph_.aggregate_helpers.size(), kNoLowId);
		IndexAggregateParameterEntities(&aggregate_parameter_entities_);
		process_atexit_runtime_symbol_ = kNoLowId;
		thread_atexit_runtime_symbol_ = kNoLowId;
		dso_handle_symbol_ = kNoLowId;
	}
	void Lower()
	{
		RegisterAggregateHelpers();
		ScanTop(graph_.root);
		for (std::size_t node = 0; node < arena_.nodes.size(); ++node)
			if (arena_.nodes[node].kind == DUMP_CALLEE && arena_.nodes[node].binding != kNoBinding &&
				function_symbols_[arena_.nodes[node].binding] == kNoLowId) RegisterFunction(node);
		RegisterLocalStaticObjects();
		pa18_lowering_detail::PreparePolymorphism(graph_, output_, stats_,
			source_ordinal_, function_symbols_, &polymorphism_);
		PrepareFunctionExceptionPolicyRuntime();
		EmitLocalStaticGlobals();
		if (output_.host_object_emission)
		{
			EmitTop(graph_.root, true, false);
			EmitThreadLocalInitializers();
			EmitTop(graph_.root, false, true);
		}
		else EmitTop(graph_.root, true, true);
		pa18_lowering_detail::EmitDeletingDestructors(graph_, output_, stats_,
			function_symbols_, &polymorphism_);
		pa18_lowering_detail::EmitVtableThunks(graph_, output_, stats_,
			function_symbols_, &polymorphism_);
		EmitAggregateHelpers();
		if (!output_.host_object_emission)
			EmitThreadLocalInitializers();
		EmitDynamicInitializer();
		EmitDynamicFinalizer();
	}
private:
	friend class pa28_lowering_detail::VirtualBaseLowering<GraphLowerer>;
	friend class pa28_lowering_detail::VirtualBaseBoundaryShape<GraphLowerer>;
	friend class pa28_lowering_detail::VirtualBaseContractLookup<GraphLowerer>;
	friend class pa15_lowering_detail::ControlFlowLowering<GraphLowerer>;
	friend class pa15_lowering_detail::ScalarUnaryLowering<GraphLowerer>;
	friend class pa18_lowering_detail::PolymorphismActionLowering<GraphLowerer>;
	friend class pa17_lowering_detail::BitFieldValueLowering<GraphLowerer>;
	friend class pa17_lowering_detail::ControlExpressionLowering<GraphLowerer>;
	friend class pa17_lowering_detail::ValueBoundaryLowering<GraphLowerer>;
	friend class pa17_lowering_detail::SpecialMemberLowering<GraphLowerer>;
	friend class pa16_lowering_detail::AssignmentLowering<GraphLowerer>;
	friend class pa16_lowering_detail::AggregateHelperLowering<GraphLowerer>;
	friend class pa16_lowering_detail::ConstructorActionLowering<GraphLowerer>;
	friend class pa16_lowering_detail::ArrayLifetimeLowering<GraphLowerer>;
	friend class pa16_lowering_detail::DestructorActionLowering<GraphLowerer>;
	friend class pa16_lowering_detail::CallArgumentLowering<GraphLowerer>;
	friend class pa16_lowering_detail::InitializationLowering<GraphLowerer>;
	friend class pa16_lowering_detail::LifetimeActionLowering<GraphLowerer>;
	friend class pa16_lowering_detail::MemberAddressLowering<GraphLowerer>;
	friend class pa16_lowering_detail::SlotPlanning<GraphLowerer>;
	friend class pa17_lowering_detail::TemporaryLifetimeLowering<GraphLowerer>;
	friend class pa21_lowering_detail::ConstantLowering<GraphLowerer>;
	friend class pa21_lowering_detail::LocalStaticLowering<GraphLowerer>;
	friend class pa25_lowering_detail::RangeForLowering<GraphLowerer>;
	friend class pa26_lowering_detail::ExceptionLowering<GraphLowerer>;
	friend class pa26_lowering_detail::InitializerListLowering<GraphLowerer>;
	friend class pa26_lowering_detail::RttiLowering<GraphLowerer>;
	friend class pa27_lowering_detail::MemberFunctionPointerLowering<GraphLowerer>;
	friend class pa27_lowering_detail::MemberPointerLowering<GraphLowerer>;
	friend class pa30_lowering_detail::RegionLowering<GraphLowerer>;
	friend class pa33_lowering_detail::StaticLifecycleLowering<GraphLowerer>;
	friend class pa34_lowering_detail::GnuAsmLowering<GraphLowerer>;
	friend class pa34_lowering_detail::ComplexLowering<GraphLowerer>;
	enum StatementTaskKind : std::uint8_t
	{
		STATEMENT_NODE,
		STATEMENT_SEQUENCE,
		STATEMENT_FOR_COMPONENTS,
		STATEMENT_IF_AFTER_THEN,
		STATEMENT_IF_AFTER_ELSE,
		STATEMENT_LOOP_AFTER_BODY,
		STATEMENT_DO_AFTER_BODY,
		STATEMENT_FOR_AFTER_INIT,
		STATEMENT_FOR_AFTER_BODY,
		STATEMENT_FOR_AFTER_ITERATION,
		STATEMENT_SWITCH_AFTER_BODY, STATEMENT_TRY_AFTER_BODY, STATEMENT_HANDLER_AFTER_BODY
	};
	struct StatementTask
	{
		std::uint32_t node;
		std::uint32_t auxiliary;
		std::uint32_t last;
		BlockId first;
		BlockId second;
		BlockId third;
		StatementTaskKind kind;
		bool flag;
		explicit StatementTask(StatementTaskKind kind_value) : node(kNoDumpEdge),
			auxiliary(kNoDumpEdge), last(kNoDumpEdge), first(kNoLowId), second(kNoLowId),
			third(kNoLowId), kind(kind_value), flag(false) {}
	};
	NodeChildren Children(std::uint32_t node) const
	{
		NodeChildren result;
		for (std::uint32_t edge = arena_.nodes[node].first_edge;
			edge != kNoDumpEdge; edge = arena_.edges[edge].next)
			result.Push(arena_.edges[edge].child);
		return result;
	}
	LowType LowerType(TypeId type) const { return source_types_.Lower(type); }
	bool IsReferenceType(TypeId type) const { return source_types_.IsReference(type); }
	TypeId RemoveReference(TypeId type) const { return source_types_.RemoveReference(type); }
	TypeId RemoveTopQualifiers(TypeId type) const { return source_types_.RemoveTopQualifiers(type); }
	TypeId ExpressionObjectType(TypeId type) const { return source_types_.ExpressionObject(type); }
	bool IsArrayType(TypeId type) const { return source_types_.IsArray(type); }
	bool IsFunctionType(TypeId type) const { return source_types_.IsFunction(type); }
	bool IsClassObjectType(TypeId type) const { return source_types_.IsClassObject(type); }
	bool IsComplexObjectType(TypeId type) const { return source_types_.IsComplexObject(type); }
	EntityId ClassEntity(TypeId type) const
	{
		type = program_.types.RemoveTopCv(ExpressionObjectType(type));
		const TypeRecord& record = program_.types.Get(type);
		return record.kind == TYPE_NAMED ? record.entity : kNoEntity;
	}
	EntityId BaseEntityForType(TypeId type) const
	{
		if (type == kNoType)
			return current_member_owner_;
		TypeId shape_id = program_.types.RemoveTopCv(type);
		const TypeRecord* shape = &program_.types.Get(shape_id);
		while (shape->kind == TYPE_LVALUE_REFERENCE ||
			shape->kind == TYPE_RVALUE_REFERENCE || shape->kind == TYPE_QUALIFIED)
		{
			shape_id = program_.types.RemoveTopCv(shape->child);
			shape = &program_.types.Get(shape_id);
		}
		if (shape->kind == TYPE_POINTER)
		{
			shape_id = program_.types.RemoveTopCv(shape->child);
			shape = &program_.types.Get(shape_id);
		}
		return shape->kind == TYPE_NAMED ? shape->entity : kNoEntity;
	}
	LowType LowerExpressionType(TypeId type) const { return source_types_.LowerExpression(type); }
	LowType LowerStorageType(TypeId type) const { return source_types_.LowerStorage(type); }
	TypeId ArrayElementType(TypeId type) const { return source_types_.ArrayElement(type); }
	bool IsPointerLikeType(TypeId type) const { return source_types_.IsPointerLike(type); }
	LowType NullPointerExpectation(std::uint32_t node,
		const LowType& target) const
	{
		return target.kind == LOW_PTR &&
			source_types_.IsNullptr(arena_.nodes[node].type) ? target : LowType();
	}
	TypeId PointeeType(TypeId type) const { return source_types_.Pointee(type); }
	LowType LowerBoundaryResult(TypeId type) const {
		const TypeId unqualified = program_.types.RemoveTopCv(type);
		const TypeRecord& record = program_.types.Get(unqualified);
		return record.kind == TYPE_NAMED &&
			!program_.entities[record.entity].complete ? LowVoid() : LowerType(type); }
	SymbolId InternSymbol(const DumpNode& node, Symbol::Kind kind,
		const std::string& proposed_name, const std::string& object_name)
	{
		const BindingRecord& binding = program_.bindings[node.binding];
		const BindingRecord& canonical_binding = program_.bindings[binding.canonical];
		const bool class_template_member = binding.member_owner != kNoEntity &&
			program_.entities[binding.member_owner].template_argument_begin != kNoBinding;
		const bool weak_linkage = pa15_lowering_abi::HasWeakLinkage(
			program_, node.binding, kind == Symbol::FUNCTION_SYMBOL);
		const bool local_member = pa18_lowering_detail::IsFunctionLocalEntity(
			program_, binding.member_owner);
		const bool prefer_local =
			pa18_lowering_detail::PreferLocalObjectBinding(
				program_, binding.member_owner);
		const bool internal = binding.unnamed_namespace_linkage ||
			canonical_binding.unnamed_namespace_linkage ||
			(binding.storage_class == STORAGE_CLASS_STATIC &&
			 binding.member_owner == kNoEntity);
		const bool c_linkage = binding.language_linkage == LANGUAGE_LINKAGE_C;
		SymbolIdentity identity;
		identity.kind = kind;
		identity.path = class_template_member ? output_.identities.InternClassMemberPath(
			program_, binding.member_owner, binding.name) :
			output_.identities.InternPath(program_, c_linkage && !internal ?
				program_.GlobalScope() : binding.owner, binding.name);
		identity.signature = kind == Symbol::FUNCTION_SYMBOL && !c_linkage ?
			output_.identities.InternFunctionSignature(program_, binding.type,
				identity_type_cache_) : kNoLowId;
		identity.template_arguments = kind == Symbol::FUNCTION_SYMBOL || binding.variable_template_specialization ?
			output_.identities.InternBindingTemplateArguments(program_, binding,
				identity_type_cache_) : kNoLowId;
		identity.owner_template_arguments = class_template_member ?
			output_.identities.InternEntityTemplateArguments(program_, program_.entities[
				binding.member_owner], identity_type_cache_) : kNoLowId;
		identity.internal_owner = local_member ?
			((source_ordinal_ + 1) << 32) |
				(static_cast<std::size_t>(binding.member_owner) + 1) :
			internal ? source_ordinal_ + 1 : 0;
		const IdentityTypeId source_type = output_.identities.InternType(
			program_, node.type, identity_type_cache_);
		SymbolId found = kNoLowId;
		if (output_.symbol_index.Find(identity, &found))
		{
			Symbol& symbol = output_.symbols[found];
			if (symbol.source_type != source_type)
				throw std::runtime_error("conflicting cross-source PA15 symbol type");
			if (!symbol.object_name.empty() && !object_name.empty() &&
				symbol.object_name != object_name)
				throw std::logic_error("conflicting PA15 ABI object identity");
			symbol.nonthrowing = symbol.nonthrowing || binding.nonthrowing;
			symbol.weak_linkage |=
				weak_linkage && !prefer_local && !symbol.internal_linkage;
			symbol.prefer_local_object_binding |= prefer_local;
			if (symbol.section_name.empty() &&
				canonical_binding.object_section_name != 0)
				symbol.section_name = program_.names.Get(canonical_binding.object_section_name);
			symbol.object_output_root |= binding.object_output_root;
			pa15_lowering_abi::ApplyBuiltinSymbolMetadata(
				&symbol, binding.builtin_function,
				binding.hosted_memory_intrinsic);
			pa15_lowering_abi::ApplyNativeRuntimeSymbolMetadata(&symbol);
			return found;
		}
		if (output_.symbols.size() >= kNoLowId)
			throw std::runtime_error("too many PA15 emission symbols");
		std::size_t& count = output_.symbol_name_counts[proposed_name];
		const std::string name = count++ == 0 ? proposed_name :
			proposed_name + "__sym" + std::to_string(count);
		const SymbolId symbol = static_cast<SymbolId>(output_.symbols.size());
		output_.symbols.push_back(Symbol(kind, name,
			object_name, c_linkage,
			internal, binding.nonthrowing));
		pa15_lowering_abi::ApplyBuiltinSymbolMetadata(&output_.symbols.back(),
			binding.builtin_function, binding.hosted_memory_intrinsic);
		pa15_lowering_abi::ApplyNativeRuntimeSymbolMetadata(
			&output_.symbols.back());
		output_.symbols.back().source_type = source_type;
		output_.symbols.back().weak_linkage =
			weak_linkage && !prefer_local && !internal;
		output_.symbols.back().prefer_local_object_binding = prefer_local;
		if (canonical_binding.object_section_name != 0)
			output_.symbols.back().section_name = program_.names.Get(canonical_binding.object_section_name);
		output_.symbols.back().object_output_root = binding.object_output_root;
		output_.symbol_index.Insert(identity, symbol);
		return symbol;
	}
	void RegisterFunction(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.binding == kNoBinding) return;
		if (function_symbols_[record.binding] == kNoLowId)
		{
			const BindingRecord& binding = program_.bindings[record.binding];
			const std::string base = SanitizeSymbol(program_.names.Get(
				binding.qualified_name != 0 ? binding.qualified_name : record.text));
			const std::uint32_t ordinal =
				program_.bindings[record.binding].overload_ordinal;
			const std::string name = ordinal <= 1 ? base :
				base + "__ov" + std::to_string(ordinal);
			const std::string entry_name = binding.constructor_base_entry ?
				name + "__base_entry" : binding.destructor_base_entry ?
				name + "__base_entry" : name;
			function_symbols_[record.binding] = InternSymbol(record, Symbol::FUNCTION_SYMBOL, entry_name,
				pa15_lowering_abi::MangleFunction(program_, record));
			pa15_lowering_abi::ApplyLifecycleSymbolMetadata(program_, record, &output_, function_symbols_[record.binding]);
		}
		if (record.kind == DUMP_FUNCTION_DEFINITION)
		{
			function_definition_[record.binding] = node;
			IndexBitFieldStorageTransferOwner(node);
		}
		else if (record.kind == DUMP_FUNCTION_DECLARATION && function_declaration_[record.binding] == kNoDumpEdge) function_declaration_[record.binding] = node;
		CacheVirtualBaseBoundary(node);
		if (record.declaration_only) output_.symbols[function_symbols_[record.binding]].referenced = true;
	}
	void ScanTop(std::uint32_t node)
	{
		std::vector<std::uint32_t> pending(1, node);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const DumpNode& record = arena_.nodes[current];
			if (record.kind == DUMP_FUNCTION_DEFINITION ||
				record.kind == DUMP_FUNCTION_DECLARATION)
			{
				if (pa15_lowering_abi::IsFunctionEmissionDemanded(
					program_, record, output_.host_object_emission))
					RegisterFunction(current);
				continue;
			}
			if (record.kind == DUMP_VARIABLE && record.binding != kNoBinding)
			{
				const BindingId canonical =
					program_.bindings[record.binding].canonical;
				if (global_symbols_[canonical] == kNoLowId)
				{
					const std::string name = SanitizeSymbol(program_.names.Get(
						program_.bindings[record.binding].qualified_name != 0 ?
						program_.bindings[record.binding].qualified_name : record.text));
					global_symbols_[canonical] = InternSymbol(record,
						Symbol::GLOBAL_SYMBOL, name,
						pa15_lowering_abi::MangleVariable(program_, record));
				}
				global_symbols_[record.binding] = global_symbols_[canonical];
				output_.symbols[global_symbols_[canonical]].thread_local_storage =
					program_.bindings[record.binding].thread_local_storage;
				const bool declaration_only = pa15_lowering_abi::IsVariableDeclarationOnly(
						program_, record, !Children(current).empty());
				if (!declaration_only || global_node_[canonical] == kNoDumpEdge)
					global_node_[canonical] = current;
				continue;
			}
			if (record.kind != DUMP_TRANSLATION_UNIT &&
				record.kind != DUMP_NAMESPACE)
				continue;
			const NodeChildren children = Children(current);
			for (std::size_t i = children.size(); i != 0; --i)
				pending.push_back(children[i - 1]);
		}
	}
	void EmitTop(std::uint32_t node, bool emit_variables, bool emit_callables)
	{
		std::vector<std::uint32_t> pending(1, node);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const DumpNode& record = arena_.nodes[current];
			if (record.kind == DUMP_FUNCTION_DECLARATION)
			{
				if (!emit_callables) continue;
				if (!pa15_lowering_abi::IsFunctionEmissionDemanded(
					program_, record, output_.host_object_emission)) continue;
				if (record.binding != kNoBinding &&
					function_definition_[record.binding] == kNoDumpEdge &&
					function_declaration_[record.binding] == current)
				{
					const SymbolId symbol = function_symbols_[record.binding];
					if (!output_.symbols[symbol].declaration_emitted)
						{
							output_.declarations.push_back(LowerDeclaration(current));
							output_.symbols[symbol].declaration_emitted = true;
						}
				}
				continue;
			}
			if (record.kind == DUMP_FUNCTION_DEFINITION)
			{
				if (!emit_callables) continue;
				if (!pa15_lowering_abi::IsFunctionEmissionDemanded(
					program_, record, output_.host_object_emission)) continue;
				if (record.binding != kNoBinding &&
					function_definition_[record.binding] == current)
				{
						const SymbolId symbol = function_symbols_[record.binding];
						if (output_.symbols[symbol].definition_emitted)
							throw std::runtime_error(
								"duplicate cross-source function definition");
						output_.functions.push_back(LowerFunction(current));
						output_.symbols[symbol].definition_emitted = true;
				}
				continue;
			}
			if (record.kind == DUMP_VARIABLE)
			{
				if (!emit_variables) continue;
				if (record.binding != kNoBinding)
				{
					const BindingId canonical =
						program_.bindings[record.binding].canonical;
					if (global_node_[canonical] == current)
					{
						const bool declaration_only = pa15_lowering_abi::IsVariableDeclarationOnly(
								program_, record, !Children(current).empty());
						if (declaration_only)
						{
								const SymbolId symbol = global_symbols_[canonical];
								if (!output_.symbols[symbol].declaration_emitted)
								{
									output_.global_declarations.push_back(
										LowerGlobalDeclaration(current));
									output_.symbols[symbol].declaration_emitted = true;
									if ((record.declaration_only ||
										 output_.host_object_emission) &&
										program_.bindings[record.binding].
										thread_local_storage)
										thread_local_declarations_.push_back(
											std::make_pair(symbol,
												pa15_lowering_abi::MangleThreadLocalWrapper(
													program_, record.binding, record.text)));
								}
						}
						else
						{
								const SymbolId symbol = global_symbols_[canonical];
								if (output_.symbols[symbol].definition_emitted)
									throw std::runtime_error(
										"duplicate cross-source global definition");
								output_.globals.push_back(LowerGlobal(current));
								output_.symbols[symbol].definition_emitted = true;
						}
					}
				}
				continue;
			}
			if (record.kind != DUMP_TRANSLATION_UNIT &&
				record.kind != DUMP_NAMESPACE)
				continue;
			const NodeChildren children = Children(current);
			for (std::size_t i = children.size(); i != 0; --i)
				pending.push_back(children[i - 1]);
		}
	}
	FunctionDeclaration LowerDeclaration(std::uint32_t node) const
	{
		const DumpNode& record = arena_.nodes[node];
		FunctionDeclaration declaration;
		declaration.symbol = function_symbols_[record.binding];
		FillBoundary(node, &declaration.parameters, &declaration.result,
			&declaration.variadic);
		return declaration;
	}
	GlobalDeclaration LowerGlobalDeclaration(std::uint32_t node) const
	{
		const DumpNode& record = arena_.nodes[node];
		GlobalDeclaration declaration;
		declaration.symbol = global_symbols_[record.binding];
		const TypeRecord& type = program_.types.Get(
			RemoveTopQualifiers(record.type));
		declaration.typed = !record.declaration_only &&
			(type.kind != TYPE_ARRAY || type.bound != 0);
		if (declaration.typed) declaration.type = LowerStorageType(record.type);
		return declaration;
	}
	Global LowerGlobal(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		Global global;
		global.symbol = global_symbols_[record.binding];
		global.type = LowerVariableStorage(record);
		const BindingId canonical = program_.bindings[record.binding].canonical;
		const std::uint32_t action_index = canonical < namespace_action_.size() ?
			namespace_action_[canonical] : kNoDumpEdge;
		if (action_index == kNoDumpEdge ||
			action_index >= graph_.namespace_objects.size())
			throw std::logic_error("global definition has no namespace action fact");
		const NamespaceObjectAction& action =
			graph_.namespace_objects[action_index];
		RegisterNamespaceInitializerListBacking(action);
		const bool thread_local_object =
			program_.bindings[action.object].thread_local_storage;
		output_.symbols[global.symbol].thread_local_storage = thread_local_object;
		if (thread_local_object)
			thread_local_objects_.push_back(
				std::make_pair(action_index,
					pa15_lowering_abi::MangleThreadLocalWrapper(
						program_, record.binding, record.text)));
		bool keep_global_class_address = false;
		if (!SetExplicitVariableZero(record, &global) &&
			!static_initializers_.Lower(action, thread_local_object, &global,
			&needs_global_class_initializer_, &keep_global_class_address))
		{
			static_initializers_.SetZero(action.type, &global);
			if (thread_local_object)
				thread_local_dynamic_[action_index] = 1;
			else namespace_initializers_.push_back(std::make_pair(action_index, true));
		}
		else if (keep_global_class_address) namespace_initializers_.push_back(
			std::make_pair(action_index, false));
		if (action.destructor != kNoDumpEdge &&
			!program_.bindings[action.object].thread_local_storage)
			dynamic_finalizers_.push_back(action_index);
		if (stats_) ++stats_->globals;
		return global;
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
	Operand FloatingOperand(const std::string& spelling, const LowType& type)
	{
		std::string numeric = spelling;
		if (!numeric.empty() &&
			((numeric[0] >= '0' && numeric[0] <= '9') || numeric[0] == '.'))
		{
			static const char* const suffixes[] = {
				"F128", "f128", "F32x", "f32x", "F64x", "f64x",
				"F16", "f16", "F32", "f32", "F64", "f64", "Q", "q"
			};
			bool normalized_suffix = false;
			for (std::size_t i = 0;
				i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
			{
				const std::size_t count =
					std::char_traits<char>::length(suffixes[i]);
				if (numeric.size() >= count && numeric.compare(
					numeric.size() - count, count, suffixes[i]) == 0)
				{
					numeric.erase(numeric.size() - count);
					normalized_suffix = true;
					break;
				}
			}
			if (normalized_suffix && type.kind == LOW_F32) numeric += "f";
			else if (normalized_suffix && type.kind == LOW_F80) numeric += "L";
		}
		return Operand::Floating(output_.literals.Intern(numeric), type);
	}

	void BeginSyntheticFunction(Function* function)
	{
		function_ = function;
		current_result_ = LowVoid();
		current_result_reference_ = false;
		current_indirect_result_ = false;
		temp_counter_ = 0;
		block_counter_ = 0;
		generated_slot_ordinal_ = 0;
		ResetControlFlowReachability();
		ResetFullExpressionFunctionState();
		ResetExceptionFunctionState(); ResetInitializerListFunctionState();
		break_targets_.clear();
		continue_targets_.clear();
		label_blocks_.Clear();
		ResetInitializedBitFieldUnit();
		used_names_.Clear();
		assigned_names_.Clear();
		slot_name_counts_.Clear();
		current_this_binding_ = kNoBinding;
		current_member_owner_ = kNoEntity;
		ResetVirtualBaseBoundary();
		current_class_value_boundary_ = false;
		SelectBlock(AddBlock("entry"));
	}

	void EndSyntheticFunction(const Function& function)
	{
		if (stats_)
		{
			++stats_->functions;
			stats_->blocks += function.block_order.size();
		}
		function_ = 0;
		current_result_reference_ = false;
		current_indirect_result_ = false;
		current_this_binding_ = kNoBinding;
		current_member_owner_ = kNoEntity;
	}

	std::string UniqueSlotName(const std::string& requested)
	{
		std::string base = requested.empty() ? "__slot" : requested;
		std::size_t& count = slot_name_counts_[base];
		++count;
		std::string candidate = count == 1 ? base :
			base + "__shadow" + std::to_string(count);
		while (assigned_names_[candidate])
		{
			++count;
			candidate = base + "__shadow" + std::to_string(count);
		}
		assigned_names_[candidate] = true;
		used_names_[candidate] = true;
		return candidate;
	}

	std::string GeneratedSlotName(const std::string& prefix)
	{
		while (true)
		{
			const std::string candidate = prefix + "__" +
				std::to_string(++generated_slot_ordinal_);
			if (!used_names_[candidate])
			{
				used_names_[candidate] = true;
				return candidate;
			}
		}
	}

	SlotId EnsureGeneratedSlot(std::uint32_t node, const std::string& prefix,
		const LowType& type)
	{
		if (generated_slots_[node] != kNoLowId)
			return generated_slots_[node];
		generated_slots_[node] = CreateGeneratedSlot(prefix, type);
		return generated_slots_[node];
	}

	SlotId CreateGeneratedSlot(const std::string& prefix, const LowType& type)
	{
		if (function_->slots.size() >= kNoLowId)
			throw std::runtime_error("too many PA15 LowIR slots");
		const SlotId result = static_cast<SlotId>(function_->slots.size());
		Slot slot;
		slot.name = GeneratedSlotName(prefix);
		slot.type = type;
		function_->slots.push_back(slot);
		return result;
	}

	void CollectSourceNames(std::uint32_t node)
	{
		std::vector<std::uint32_t> pending(1, node);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const DumpNode& record = arena_.nodes[current];
			if ((record.kind == DUMP_PARAMETER || record.kind == DUMP_VARIABLE) &&
				record.text != 0)
				used_names_[program_.names.Get(record.text)] = true;
			const NodeChildren children = Children(current);
			for (std::size_t i = children.size(); i != 0; --i)
				pending.push_back(children[i - 1]);
		}
	}

	Function LowerFunction(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		Function result;
		result.symbol = function_symbols_[record.binding];
		result.entry = program_.names.Get(record.text) == "main";
		FillBoundary(node, &result.parameters, &result.result, &result.variadic);
		function_ = &result;
		current_result_ = result.result;
		current_class_value_boundary_ = FunctionHasClassValueBoundary(record.type);
		const TypeRecord& source_function = program_.types.Get(record.type); current_indirect_result_ = UsesIndirectClassResult(source_function.child, record.binding);
		current_result_reference_ = IsReferenceType(source_function.child);
		temp_counter_ = 0;
		block_counter_ = 0;
		break_targets_.clear();
		continue_targets_.clear();
		label_blocks_.Clear();
		ResetInitializedBitFieldUnit();
		used_names_.Clear();
		assigned_names_.Clear();
		slot_name_counts_.Clear();
		generated_slot_ordinal_ = 0;
		ResetControlFlowReachability();
		ResetLifetimeFunctionState(); ResetFullExpressionFunctionState();
		ResetExceptionFunctionState(); ResetInitializerListFunctionState();
		parameter_slot_index_ = current_indirect_result_ ? 1 : 0;
		current_this_binding_ = kNoBinding;
		current_member_owner_ = record.binding == kNoBinding ? kNoEntity : program_.bindings[record.binding].member_owner;
		const NodeChildren children = Children(node);
		PrepareVirtualBaseBoundary(node, result.parameters);
		SetCurrentThisForSlotPlanning(record, children);
		CollectSourceNames(node);
		CollectSlots(node);
		SelectBlock(AddBlock("entry"));
		BeginFunctionExceptionBoundary(node, record.binding);
		std::size_t parameter_index = 0;
		std::uint32_t body = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& child = arena_.nodes[children[i]];
			if (child.kind == DUMP_PARAMETER)
			{
				const std::size_t boundary_parameter = parameter_index +
					(current_indirect_result_ ? 1 : 0) +
					(HasCurrentConstructionVtt() && parameter_index != 0 ? 1 : 0);
				if (parameter_index == 0 && record.binding != kNoBinding &&
					program_.bindings[record.binding].member_owner != kNoEntity &&
					!program_.bindings[record.binding].static_member_function)
					current_this_binding_ = child.binding;
				MaterializeBoundaryParameter(child, boundary_parameter);
				MaterializeVirtualBaseBoundaryParameter(child);
				++parameter_index;
			}
			else if (child.kind == DUMP_COMPOUND_STATEMENT ||
				child.kind == DUMP_TRY_STATEMENT) body = children[i];
		}
		if (body != kNoDumpEdge)
		{
			if (record.binding != kNoBinding && program_.bindings[record.binding].constructor &&
				arena_.nodes[body].unwind_only)
				LowerConstructorBody(body);
			else if (record.binding != kNoBinding &&
				program_.bindings[record.binding].destructor)
				LowerDestructorBody(body);
			else LowerStatement(body);
		}
		if (!CurrentBlock().terminated)
		{
			FinishFunctionExceptionBoundaryNormalExit();
			if (result.entry)
			{
				Instruction instruction(Instruction::RETURN_VALUE);
				instruction.type = result.result;
				instruction.first = Operand(0, result.result);
				Emit(instruction);
			}
			else if (result.result.kind == LOW_VOID)
				Emit(Instruction(Instruction::RETURN_VOID));
			else if (!HasBlockIncoming(current_block_))
			{
				Instruction instruction(Instruction::RETURN_VALUE);
				instruction.type = result.result;
				instruction.first = result.result.kind == LOW_OBJECT ?
					ZeroDirectReturnObject(node, result.result) :
					Operand(0, result.result);
				Emit(instruction);
			}
			else throw std::runtime_error("non-void function has no return");
		}
		FinishFunctionExceptionBoundary();
		if (stats_)
		{
			++stats_->functions;
			stats_->blocks += result.block_order.size();
		}
		function_ = 0;
		current_result_reference_ = false;
		current_indirect_result_ = false;
		current_class_value_boundary_ = false;
		current_this_binding_ = kNoBinding;
		current_member_owner_ = kNoEntity;
		ResetVirtualBaseBoundary();
		return result;
	}
	Block& CurrentBlock() { return function_->blocks[current_block_]; }
	std::string NewLabel(const std::string& prefix)
	{
		return prefix + "_" + std::to_string(++block_counter_);
	}

	TempId NewTemp()
	{
		while (true)
		{
			if (temp_counter_ + 1 >= kNoLowId)
				throw std::runtime_error("too many PA15 LowIR temporaries");
			const TempId candidate = static_cast<TempId>(++temp_counter_);
			if (!used_names_["t" + std::to_string(candidate)]) return candidate;
		}
	}

	Operand Temp(const LowType& type)
	{
		return Operand(NewTemp(), type);
	}

	void Emit(const Instruction& instruction)
	{
		if (CurrentBlock().terminated)
			throw std::runtime_error("PA15 attempted to emit after a terminator");
		CurrentBlock().instructions.push_back(instruction);
		if (IsTerminator(instruction)) CurrentBlock().terminated = true;
		if (stats_) ++stats_->instructions;
	}

	Operand StorageFor(BindingId binding, const LowType& type)
	{
		if (stats_) ++stats_->binding_index_probes;
		if (binding < binding_indirect_parameters_.size() &&
			binding_indirect_parameters_[binding] != kNoLowId)
			return Operand(binding_indirect_parameters_[binding], LowPtr());
		if (binding < binding_slots_.size() && binding_slots_[binding] != kNoLowId)
			return Operand(binding_slots_[binding], type);
		if (binding < program_.bindings.size())
			binding = program_.bindings[binding].canonical;
		if (binding < global_symbols_.size() && global_symbols_[binding] != kNoLowId)
		{
			const SymbolId global = global_symbols_[binding];
			output_.symbols[global].referenced = true;
			if (output_.host_object_emission &&
				output_.symbols[global].thread_local_storage &&
				global != lowering_thread_local_initializer_object_)
			{
				if (global >= tls_access_wrapper_symbols_.size() ||
					tls_access_wrapper_symbols_[global] == kNoLowId)
					throw std::logic_error(
						"thread-local storage has no access wrapper");
				const SymbolId wrapper = tls_access_wrapper_symbols_[global];
				output_.symbols[wrapper].referenced = true;
				const Operand address = Temp(LowPtr());
				Instruction call(Instruction::CALL);
				call.dest = address.id;
				call.type = LowPtr();
				call.first = Operand(Operand::FUNCTION, wrapper, LowPtr());
				Emit(call);
				return address;
			}
			return Operand(Operand::GLOBAL, global, type);
		}
		throw std::runtime_error("PA15 binding has no lowered storage: " +
			std::to_string(binding));
	}

	bool BindingIsReference(BindingId binding) const
	{
		return binding < program_.bindings.size() &&
			IsReferenceType(program_.bindings[binding].type);
	}

	Operand LoadStorage(const Operand& storage, const LowType& type)
	{
		const Operand result = Temp(type);
		Instruction load(Instruction::LOAD);
		load.dest = result.id;
		load.type = type;
		load.first = storage;
		Emit(load);
		return result;
	}
	Operand AddressOfStorage(const Operand& storage)
	{
		if (storage.kind == Operand::INTEGER && storage.type.kind == LOW_PTR) return storage;
		if (storage.kind == Operand::TEMP ||
			(storage.kind == Operand::PARAMETER && storage.type.kind == LOW_PTR))
		{
			if (storage.type.kind != LOW_PTR)
				throw std::logic_error("PA15 indirect storage is not a pointer");
			return storage;
		}
		if (storage.kind == Operand::GLOBAL || storage.kind == Operand::FUNCTION)
			output_.symbols[storage.id].referenced = true;
		const Operand result = Temp(LowPtr());
		Instruction address(Instruction::ADDR);
		address.dest = result.id;
		address.first = storage;
		Emit(address);
		return result;
	}

	Operand DecayAddress(const Operand& address)
	{
		const Operand result = Temp(LowPtr());
		Instruction decay(Instruction::UNARY);
		decay.dest = result.id;
		decay.op = LOW_OP_DECAY;
		decay.type = LowPtr();
		decay.first = address;
		Emit(decay);
		return result;
	}

	Operand IndexAddress(const LowType& element, const Operand& base,
		const Operand& offset, bool array_projection)
	{
		const Operand result = Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = result.id;
		index.type = element;
		index.first = base;
		index.second = offset;
		index.projection = array_projection ? INDEX_PROJECTION_ARRAY_ELEMENT :
			INDEX_PROJECTION_NONE;
		Emit(index);
		return result;
	}
	Operand LoadBlockInvoke(const Operand& block)
	{
		return LoadStorage(IndexAddress(LowI8(), block,
			Operand(16, LowI64()), false), LowPtr());
	}

	Operand LowerArrayPointer(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (IsArrayType(record.type))
		{
			if (record.kind == DUMP_LITERAL)
				return AddressOfStorage(LowerStorage(node));
			return record.kind == DUMP_CONDITIONAL_EXPRESSION || record.kind ==
				DUMP_SUBSCRIPT_EXPRESSION || record.kind == DUMP_CALL_EXPRESSION ?
				LowerStorage(node) :
				DecayAddress(AddressOfStorage(LowerStorage(node)));
		}
		return LowerValue(node, LowPtr());
	}

	Operand LowerStorage(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		const NodeChildren children = Children(node);
		Operand complex_storage;
		if (TryLowerComplexStorage(node, record, children, &complex_storage))
			return complex_storage;
		if (record.kind == DUMP_TYPEID_EXPRESSION)
			return LowerTypeid(record, children);
		if (record.kind == DUMP_DYNAMIC_CAST_EXPRESSION)
			return LowerDynamicCast(node, record, children);
		if (record.kind == DUMP_SPECIAL_MEMBER_CONSTRUCTION_ACTION)
			return LowerSpecialMemberConstruction(node);
		if (record.kind == DUMP_SPECIAL_MEMBER_ASSIGNMENT_ACTION)
			return LowerSpecialMemberAssignment(node);
		if (record.kind == DUMP_STATEMENT_EXPRESSION)
			return LowerStatementExpressionStorage(node, record);
		if (record.kind == DUMP_ID_EXPRESSION && record.binding != kNoBinding)
		{
			if (record.binding < function_symbols_.size() &&
				function_symbols_[record.binding] != kNoLowId)
				return Operand(Operand::FUNCTION,
					function_symbols_[record.binding], LowPtr());
			if (record.binding < binding_indirect_parameters_.size() &&
				binding_indirect_parameters_[record.binding] != kNoLowId)
				return Operand(
					binding_indirect_parameters_[record.binding], LowPtr());
			const Operand storage = StorageFor(record.binding,
				LowerStorageType(program_.bindings[record.binding].type));
			return BindingIsReference(record.binding) ?
				LoadStorage(storage, LowPtr()) : storage;
		}
		if (record.kind == DUMP_LITERAL && IsArrayType(record.type))
			return Operand(Operand::GLOBAL,
				static_initializers_.EnsureStringLiteral(node), LowPtr());
		if (record.kind == DUMP_TEMPORARY_OBJECT)
		{
			AggregatePath path;
			return LowerTemporaryObjectStorage(node, children, &path);
		}
		if (record.kind == DUMP_UNARY_EXPRESSION && children.size() == 1 &&
			StripOperationPrefix(program_.names.Get(record.text)) == "*")
			return LowerValue(children[0], LowPtr());
		if (record.kind == DUMP_UNARY_EXPRESSION && children.size() == 1 &&
			(StripOperationPrefix(program_.names.Get(record.text)) == "++" ||
			 StripOperationPrefix(program_.names.Get(record.text)) == "--"))
			return LowerIncrement(record, children[0], true);
		if (record.kind == DUMP_SUBSCRIPT_EXPRESSION && children.size() == 2)
		{
			const Operand base = LowerArrayPointer(children[0]);
			Operand offset = LowerValue(children[1]);
			if (IsClassObjectType(record.type) || IsArrayType(record.type))
			{
				const std::size_t element_size = program_.SizeOf(record.type);
				if (element_size != 1)
				{
					const Operand scaled = Temp(LowI64());
					Instruction multiply(Instruction::BINARY);
					multiply.dest = scaled.id;
					multiply.op = LOW_OP_MUL;
					multiply.type = LowI64();
					multiply.first = offset;
					multiply.second = Operand(
						static_cast<std::int64_t>(element_size), LowI64());
					Emit(multiply);
					offset = scaled;
				}
				return IndexAddress(LowI8(), base, offset, true);
			}
			return IndexAddress(LowerExpressionType(record.type), base, offset, true);
		}
		if (record.kind == DUMP_MEMBER_EXPRESSION)
			return MemberAddress(record, children);
		if (record.kind == DUMP_BINARY_EXPRESSION && children.size() == 2 &&
			StripOperationPrefix(program_.names.Get(record.text)) == ",")
		{
			LowerDiscardedValue(children[0]);
			return LowerStorage(children[1]);
		}
		if (IsMemberPointerApplication(record))
			return LowerMemberPointerStorage(record, children);
		if (record.kind == DUMP_CONDITIONAL_EXPRESSION &&
			(record.category == VALUE_LVALUE || record.category == VALUE_XVALUE))
			return LowerConditionalAddress(node, children);
		if (record.kind == DUMP_ASSIGNMENT_EXPRESSION)
			return LowerAssignmentCore(record, children, true);
		if (record.kind == DUMP_CALL_EXPRESSION && (IsReferenceType(record.type) ||
			UsesIndirectClassResult(record.type, record.binding)))
			return LowerCall(node, record, children);
		if (record.kind == DUMP_CAST_EXPRESSION && children.size() == 1 &&
			(record.category == VALUE_LVALUE || record.category == VALUE_XVALUE ||
			 arena_.nodes[children[0]].kind == DUMP_TEMPORARY_OBJECT))
		{
			if (record.base_projection_count != 0)
				return LowerProjectedClassPointer(children[0],
					record.base_projection_count, record.base_projection_offset,
					record.has_base_projection_offset,
					BaseEntityForType(record.type),
					record.inverse_base_projection);
			const Operand source = AddressOfStorage(LowerStorage(children[0]));
			return ProjectBaseSubobjects(source, 0,
				arena_.nodes[children[0]].type);
		}
		throw std::runtime_error("expression kind " +
			std::to_string(static_cast<unsigned>(record.kind)) +
			" does not designate scalar storage");
	}
	Operand Convert(Operand value, const LowType& target,
		bool canonicalize_immediate = true)
	{
		if (SameType(value.type, target) && (!IsInteger(value.type) || value.type.is_signed == target.is_signed))
		{
			value.type = target;
			return value;
		}
		if (IsInteger(value.type) && IsInteger(target) &&
			value.type.width == target.width)
		{
			if (value.kind == Operand::INTEGER)
			{
				value.type = target;
				return value;
			}
			const Operand result = Temp(target);
			Instruction copy(Instruction::COPY);
			copy.dest = result.id;
			copy.type = target;
			copy.first = value;
			Emit(copy);
			return result;
		}
		if (canonicalize_immediate && value.kind == Operand::INTEGER &&
			IsInteger(value.type) && IsInteger(target))
		{
			value.integer_value = CanonicalIntegerImmediate(
				value.integer_value, target.width, target.is_signed);
			value.type = target;
			return value;
		}
		if ((IsInteger(value.type) && target.kind == LOW_PTR) ||
			(value.type.kind == LOW_PTR && IsInteger(target)))
		{
			const Operand result = Temp(target);
			Instruction copy(Instruction::COPY);
			copy.dest = result.id;
			copy.type = target;
			copy.first = value;
			Emit(copy);
			return result;
		}
		Instruction instruction(Instruction::CONVERT);
		instruction.type = target;
		instruction.source_type = value.type;
		if (IsInteger(value.type) && IsInteger(target))
			instruction.op = target.width < value.type.width ? LOW_OP_TRUNC :
				value.type.is_signed ? LOW_OP_SEXT : LOW_OP_ZEXT;
		else if (IsInteger(value.type) && IsFloating(target))
			instruction.op = value.type.is_signed ? LOW_OP_SITOFP : LOW_OP_UITOFP;
		else if (IsFloating(value.type) && IsInteger(target))
			instruction.op = target.is_signed ? LOW_OP_FPTOSI : LOW_OP_FPTOUI;
		else if (IsFloating(value.type) && IsFloating(target))
			instruction.op = target.width < value.type.width ?
				LOW_OP_FPTRUNC : LOW_OP_FPEXT;
		else throw std::runtime_error("unsupported PA15 scalar conversion");
		const Operand result = Temp(target);
		instruction.dest = result.id;
		instruction.first = value;
		Emit(instruction);
		return result;
	}

	bool IsBooleanType(TypeId type) const
	{
		const TypeRecord* record = &program_.types.Get(type);
		while (record->kind == TYPE_QUALIFIED ||
			record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE)
		{
			type = record->child;
			record = &program_.types.Get(type);
		}
		return record->kind == TYPE_FUNDAMENTAL &&
			record->fundamental == FUND_BOOL;
	}

	Operand LowerCondition(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.boolean_conversion)
		{
			const TypeId source_type = program_.types.RemoveTopCv(record.type);
			const TypeRecord& source = program_.types.Get(source_type);
			if (source.kind == TYPE_MEMBER_POINTER)
				return LowerValue(node);
			return LowerBooleanConversion(node, LowU8());
		}
		Operand value = LowerValue(node);
		if (IsBooleanType(record.type) || !IsFloating(value.type))
			return value;
		const Operand result = Temp(LowU8());
		Instruction compare(Instruction::CMP);
		compare.dest = result.id;
		compare.op = LOW_OP_NE;
		compare.type = value.type;
		compare.first = value;
		compare.second = IsFloating(value.type) ? FloatingOperand("0.0", value.type) :
			Operand(0, value.type);
		Emit(compare);
		return result;
	}


	Operand LowerValue(std::uint32_t node, const LowType& expected = LowType())
	{
		if (stats_) ++stats_->lowered_nodes;
		const DumpNode& record = arena_.nodes[node];
		const NodeChildren children = Children(node);
		Operand result;
		if (TryLowerComplexValue(node, record, children, &result)) {}
		else if (record.kind == DUMP_TYPEID_EXPRESSION)
			result = LowerTypeid(record, children);
		else if (record.kind == DUMP_DYNAMIC_CAST_EXPRESSION)
			result = LowerDynamicCast(node, record, children);
		else if (record.kind == DUMP_THROW_EXPRESSION) result =
			LowerThrowExpression(node, record, children);
		else if (record.kind == DUMP_STATEMENT_EXPRESSION)
			result = IsClassObjectType(record.type) ?
				AddressOfStorage(LowerStatementExpressionStorage(node, record)) :
				LowerStatementExpressionValue(node, record, children);
		else if ((record.category == VALUE_LVALUE || record.category == VALUE_XVALUE) &&
			IsArrayType(record.type))
			result = record.kind == DUMP_LITERAL ?
				AddressOfStorage(LowerStorage(node)) :
				(record.kind == DUMP_CONDITIONAL_EXPRESSION ||
				 record.kind == DUMP_CALL_EXPRESSION) ?
				LowerStorage(node) :
				DecayAddress(AddressOfStorage(LowerStorage(node)));
		else if (record.kind == DUMP_LITERAL)
		{
			const LowType type = LowerType(record.type);
			if (record.null_member_pointer_constant ||
				(type.kind == LOW_PTR && record.constant &&
				 record.constant_value == 0 && record.value_initialization))
				result = Operand::NullPointer(type);
			else if (expected.kind == LOW_PTR &&
				source_types_.IsNullptr(record.type))
			{
				result = Temp(expected);
				Instruction copy(Instruction::COPY);
				copy.dest = result.id;
				copy.type = expected;
				copy.first = Operand::NullPointer(expected);
				Emit(copy);
			}
			else if (IsFloating(type))
				result = FloatingOperand(record.value_initialization ?
					"0.0" : program_.names.Get(record.text), type);
			else
			{
				if (!record.constant)
					throw std::runtime_error("literal is missing its PA12 constant fact");
				result = Operand(record.constant_value, type);
			}
		}
		else if (record.kind == DUMP_ID_EXPRESSION)
		{
			if (record.constant && record.binding != kNoBinding &&
				(program_.IsStaticDataMember(record.binding) ||
				 program_.bindings[record.binding].kind == BIND_PARAMETER))
				result = Operand(record.constant_value, LowerExpressionType(record.type));
			else if (record.binding != kNoBinding && record.binding < function_symbols_.size() &&
				function_symbols_[record.binding] != kNoLowId)
			{
				result = DecayAddress(AddressOfStorage(Operand(Operand::FUNCTION,
					function_symbols_[record.binding], LowPtr())));
			}
			else if (IsFunctionType(record.type))
			{
				result = DecayAddress(LowerStorage(node));
			}
			else
			{
				const LowType type = LowerExpressionType(record.type);
				const Operand storage = LowerStorage(node);
				result = LoadStorage(storage, type);
			}
		}
		else if (record.kind == DUMP_SUBSCRIPT_EXPRESSION)
			result = LoadStorage(LowerStorage(node),
				LowerExpressionType(record.type));
		else if (record.kind == DUMP_MEMBER_EXPRESSION)
			result = LowerMemberValue(node, record, children);
		else if (record.kind == DUMP_INITIALIZER_LIST_BEGIN ||
			record.kind == DUMP_INITIALIZER_LIST_SIZE ||
			record.kind == DUMP_INITIALIZER_LIST)
			result = LowerInitializerListValue(node, record, children);
		else if (record.kind == DUMP_SIZEOF_EXPRESSION)
		{
			if (!record.constant)
				throw std::runtime_error("sizeof is missing its PA12 constant fact");
			const LowType type = LowerExpressionType(record.type);
			result = Temp(type);
			Instruction constant(Instruction::CONST);
			constant.dest = result.id;
			constant.type = type;
			constant.first = Operand(record.constant_value, type);
			Emit(constant);
		}
		else if (record.kind == DUMP_BINARY_EXPRESSION)
			result = LowerBinary(node, record, children);
		else if (record.kind == DUMP_ASSIGNMENT_EXPRESSION)
			result = LowerAssignment(record, children);
		else if (record.kind == DUMP_UNARY_EXPRESSION ||
			record.kind == DUMP_POSTFIX_EXPRESSION)
			result = LowerUnary(record, children);
		else if (record.kind == DUMP_CALL_EXPRESSION)
		{
			result = LowerCall(node, record, children);
			if (IsReferenceType(record.type) &&
				!IsFunctionType(RemoveReference(record.type)))
				result = LoadStorage(result,
					LowerExpressionType(RemoveReference(record.type)));
		}
		else if (record.kind == DUMP_NEW_EXPRESSION) result = LowerNewExpression(node, record, children);
		else if (record.kind == DUMP_DELETE_EXPRESSION) result = LowerDeleteExpression(node, record, children);
		else if (record.kind == DUMP_SPECIAL_MEMBER_CONSTRUCTION_ACTION)
			result = LowerSpecialMemberConstruction(node);
		else if (record.kind == DUMP_CAST_EXPRESSION) {
			if (children.size() != 1) throw std::runtime_error("invalid semantic cast");
			if (IsBooleanType(record.type)) result = LowerBooleanConversion(children[0], LowerExpressionType(record.type));
			else if (record.member_pointer_conversion)
				result = LowerMemberPointerConversion(record, children);
			else if (LowerExpressionType(record.type).kind == LOW_VOID)
			{
				const DumpNode& source = arena_.nodes[children[0]];
				if ((source.category == VALUE_LVALUE ||
					 source.category == VALUE_XVALUE) &&
					(IsClassObjectType(source.type) || IsArrayType(source.type)))
					(void)AddressOfStorage(LowerStorage(children[0]));
				else (void)LowerValue(children[0]);
				result = Operand(0, LowVoid());
			}
			else if (record.category == VALUE_LVALUE || record.category == VALUE_XVALUE)
				result = LoadStorage(LowerStorage(node),
					LowerExpressionType(record.type));
			else if (record.base_projection_count != 0)
				result = LowerProjectedClassPointer(
					children[0], record.base_projection_count,
					record.base_projection_offset,
					record.has_base_projection_offset,
					BaseEntityForType(record.type),
					record.inverse_base_projection);
			else
			{
				const DumpNode& source = arena_.nodes[children[0]];
				const LowType type = LowerExpressionType(record.type);
				result = IsIntNullPointerLiteralCast(program_, source,
					record.type) ? Operand(0, type) :
					LowerInitializerConvertedValue(children[0], type);
			}
		}
		else if (record.kind == DUMP_CONDITIONAL_EXPRESSION)
			result = LowerConditional(node, record, children);
		else if (record.kind == DUMP_BRACED_INIT_LIST)
		{
			if (children.empty()) result = Operand(0, LowerType(record.type));
			else if (children.size() == 1) result = LowerValue(children[0],
				LowerExpressionType(record.type));
			else throw std::runtime_error("scalar initializer has excess elements");
		}
		else throw std::runtime_error("semantic expression kind " +
			std::to_string(static_cast<unsigned>(record.kind)) +
			" is outside the active PA15 checkpoint");
		return expected.kind == LOW_INVALID ? result : Convert(result, expected);
	}
	Operand LowerConvertedValue(std::uint32_t node, const LowType& target,
		bool canonicalize_immediate = true) {
		if (arena_.nodes[node].boolean_conversion)
			return LowerBooleanConversion(node, target);
		if (canonicalize_immediate && CanonicalizeNullPointerImmediate(node, target)) return Operand(0, target);
		return Convert(LowerValue(node, target.kind == LOW_PTR ?
			target : LowType()), target, canonicalize_immediate);
	}
	bool CanonicalizeImmediateConversion(std::uint32_t node) const { return arena_.nodes[node].integer_narrowing_conversion; }
	void LowerDiscardedValue(std::uint32_t node) { const DumpNode& record = arena_.nodes[node];
		if (record.kind == DUMP_BINARY_EXPRESSION) { (void)LowerBinary(node, record, Children(node), true); return; }
		if ((record.category == VALUE_LVALUE || record.category == VALUE_XVALUE) && !IsFunctionType(RemoveReference(record.type))) (void)LowerStorage(node);
		else (void)LowerValue(node); }
	Operand LowerBinary(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children, bool discarded = false)
	{
		if (children.size() != 2) throw std::runtime_error("invalid semantic binary");
		if (IsMemberPointerApplication(record))
			return LoadStorage(LowerMemberPointerStorage(record, children),
				LowerExpressionType(record.type));
		if (record.logical_operation != LOGICAL_OPERATION_NONE)
			return LowerLogical(node, children,
				record.logical_operation == LOGICAL_OPERATION_AND);
		const std::string op = StripOperationPrefix(program_.names.Get(record.text));
		if (op == ",")
		{
			LowerDiscardedValue(children[0]);
			if (discarded) { LowerDiscardedValue(children[1]); return Operand(0, LowVoid()); }
			return LowerValue(children[1]);
		}
		const bool comparison = op == "==" || op == "!=" || op == "<" ||
			op == "<=" || op == ">" || op == ">=";
		const bool left_pointer = IsPointerLikeType(arena_.nodes[children[0]].type);
		const bool right_pointer = IsPointerLikeType(arena_.nodes[children[1]].type);
		if ((op == "+" || op == "-") && left_pointer && !right_pointer)
			return LowerPointerOffset(children[0], children[1], op == "-");
		if (op == "+" && !left_pointer && right_pointer)
		{
			const Operand offset = LowerValue(children[0]), base = LowerArrayPointer(children[1]);
			return ApplyPointerOffset(base, offset, PointeeType(arena_.nodes[children[1]].type), false);
		}
		if (op == "-" && left_pointer && right_pointer)
			return LowerPointerDifference(children[0], children[1]);
		if (record.operand_type == kNoType &&
			!(comparison && (left_pointer || right_pointer)))
			throw std::runtime_error("binary expression is missing its PA12 operand type");
		const LowType operand_type = record.operand_type == kNoType ?
			LowPtr() : LowerExpressionType(record.operand_type);
		Operand left = LowerValue(children[0], comparison ?
			NullPointerExpectation(children[0], operand_type) : LowType());
		Operand right = LowerValue(children[1], comparison ?
			NullPointerExpectation(children[1], operand_type) : LowType());
		const bool canonical_pointer_difference_compare = comparison &&
			arena_.nodes[children[0]].kind == DUMP_BINARY_EXPRESSION &&
			arena_.nodes[children[0]].operand_type == kNoType &&
			LowerExpressionType(arena_.nodes[children[0]].type).kind == LOW_I64;
		const bool preserves_enum_conversion =
			(arena_.nodes[children[0]].enum_arithmetic_conversion && !SameType(left.type, operand_type)) ||
			(arena_.nodes[children[1]].enum_arithmetic_conversion && !SameType(right.type, operand_type));
		const bool canonicalize_immediates =
			CanonicalizeAdditiveImmediates(children[0], op, comparison, preserves_enum_conversion) ||
			(comparison &&
			 ((left.kind == Operand::INTEGER && IsInteger(left.type) &&
			   left.type.width < operand_type.width) ||
			  (right.kind == Operand::INTEGER && IsInteger(right.type) &&
			   right.type.width < operand_type.width))) ||
			canonical_pointer_difference_compare ||
			(comparison && (current_class_value_boundary_ ||
				CallHasClassValueBoundary(children[0]) ||
				CallHasClassValueBoundary(children[1]))) ||
			(comparison &&
			 (arena_.nodes[children[0]].kind == DUMP_MEMBER_EXPRESSION ||
			  arena_.nodes[children[1]].kind == DUMP_MEMBER_EXPRESSION ||
			  arena_.nodes[children[0]].user_conversion_call || arena_.nodes[children[1]].user_conversion_call));
		if (comparison && operand_type.kind == LOW_PTR &&
			left.kind == Operand::INTEGER && left.integer_value == 0)
			left.type = operand_type;
		else left = Convert(left, operand_type, CanonicalizeBinaryImmediate(
			children[0], operand_type, canonicalize_immediates, comparison,
			record.constant, record.template_layout_constant));
		if (comparison && operand_type.kind == LOW_PTR &&
			right.kind == Operand::INTEGER && right.integer_value == 0)
			right.type = operand_type;
		else right = Convert(right, operand_type, CanonicalizeBinaryImmediate(
			children[1], operand_type, canonicalize_immediates, comparison,
			record.constant, record.template_layout_constant));
		const LowType result_type = LowerType(record.type);
		const Operand result = Temp(result_type);
		Instruction instruction(comparison ? Instruction::CMP : Instruction::BINARY);
		instruction.dest = result.id;
		instruction.type = operand_type;
		instruction.first = left;
		instruction.second = right;
		if (comparison)
		{
			instruction.op = op == "==" ? LOW_OP_EQ : op == "!=" ? LOW_OP_NE :
				op == "<" ? (operand_type.is_signed ? LOW_OP_LT : LOW_OP_ULT) :
				op == "<=" ? (operand_type.is_signed ? LOW_OP_LE : LOW_OP_ULE) :
				op == ">" ? (operand_type.is_signed ? LOW_OP_GT : LOW_OP_UGT) :
				(operand_type.is_signed ? LOW_OP_GE : LOW_OP_UGE);
		}
		else
		{
			instruction.op = op == "+" ? LOW_OP_ADD : op == "-" ? LOW_OP_SUB :
				op == "*" ? LOW_OP_MUL : op == "/" ?
					(operand_type.is_signed || IsFloating(operand_type) ?
						LOW_OP_DIV : LOW_OP_UDIV) :
				op == "%" ? (operand_type.is_signed ? LOW_OP_MOD : LOW_OP_UMOD) :
				op == "&" ? LOW_OP_AND : op == "|" ? LOW_OP_OR :
				op == "^" ? LOW_OP_XOR : op == "<<" ? LOW_OP_SHL : op == ">>" ?
					(operand_type.is_signed ? LOW_OP_SHR : LOW_OP_USHR) : LOW_OP_NONE;
			if (instruction.op == LOW_OP_NONE)
				throw std::runtime_error("unsupported binary operator");
		}
		Emit(instruction);
		return result;
	}
	Operand LowerPointerOffset(std::uint32_t base_node,
		std::uint32_t offset_node, bool subtract)
	{
		const Operand base = LowerArrayPointer(base_node), offset = LowerValue(offset_node);
		return ApplyPointerOffset(base, offset, PointeeType(arena_.nodes[base_node].type), subtract);
	}

	Operand LowerPointerDifference(std::uint32_t left_node,
		std::uint32_t right_node)
	{
		const Operand left = LowerArrayPointer(left_node);
		const Operand right = LowerArrayPointer(right_node);
		const Operand bytes = Temp(LowI64());
		Instruction subtract(Instruction::BINARY);
		subtract.dest = bytes.id;
		subtract.op = LOW_OP_SUB;
		subtract.type = LowPtr();
		subtract.first = left;
		subtract.second = right;
		Emit(subtract);
		const std::size_t element_size = program_.SizeOf(PointeeType(arena_.nodes[left_node].type));
		if (element_size == 1) return bytes;
		const Operand result = Temp(LowI64());
		Instruction divide(Instruction::BINARY);
		divide.dest = result.id;
		divide.op = LOW_OP_DIV;
		divide.type = LowI64();
		divide.first = bytes;
		divide.second = Operand(static_cast<std::int64_t>(element_size), LowI64());
		Emit(divide);
		return result;
	}
	Operand ApplyPointerOffset(const Operand& base, const Operand& raw_offset,
		TypeId element_type, bool subtract)
	{
		const Operand offset = Convert(raw_offset, LowI64());
		const std::size_t element_size = program_.SizeOf(element_type);
		if (element_size == 1 && !subtract)
			return IndexAddress(LowI8(), base, offset, false);
		const Operand scaled = element_size == 1 ? offset : Temp(LowI64());
		if (element_size != 1) {
			Instruction multiply(Instruction::BINARY);
			multiply.dest = scaled.id;
			multiply.op = LOW_OP_MUL;
			multiply.type = LowI64();
			multiply.first = offset;
			multiply.second = Operand(static_cast<std::int64_t>(element_size), LowI64());
			Emit(multiply);
		}
		Operand displacement = scaled;
		if (subtract)
		{
			displacement = Temp(LowI64());
			Instruction negate(Instruction::BINARY);
			negate.dest = displacement.id;
			negate.op = LOW_OP_SUB;
			negate.type = LowI64();
			negate.first = Operand(0, LowI64());
			negate.second = scaled;
			Emit(negate);
		}
		return IndexAddress(LowI8(), base, displacement, false);
	}

	Operand LowerIncrement(const DumpNode& record, std::uint32_t operand_node,
		bool return_storage)
	{
		const std::string op = StripOperationPrefix(program_.names.Get(record.text));
		const Operand storage = LowerStorage(operand_node);
		const BindingId bit_field = BitFieldBinding(operand_node);
		const LowType type = bit_field == kNoBinding ?
			LowerExpressionType(arena_.nodes[operand_node].type) :
			BitFieldAccessType(program_.bindings[bit_field]);
		Operand old_value = bit_field == kNoBinding ?
			LoadStorage(storage, type) : LoadBitField(bit_field, storage);
		if (bit_field != kNoBinding) old_value.type = type;
		Operand new_value;
		if (IsPointerLikeType(arena_.nodes[operand_node].type))
			new_value = ApplyPointerOffset(old_value, Operand(1, LowI32()),
				PointeeType(arena_.nodes[operand_node].type), op == "--");
		else
		{
			new_value = Temp(type);
			Instruction binary(Instruction::BINARY);
			binary.dest = new_value.id;
			binary.op = op == "++" ? LOW_OP_ADD : LOW_OP_SUB;
			binary.type = type;
			binary.first = old_value;
			binary.second = Operand(1, type);
			Emit(binary);
		}
		if (bit_field == kNoBinding)
		{
			Instruction store(Instruction::STORE);
			store.type = type;
			store.first = new_value;
			store.second = storage;
			Emit(store);
		}
		else
		{
			const Operand write_storage = return_storage ?
				LowerStorage(operand_node) : storage;
			new_value = StoreBitField(
				bit_field, write_storage, new_value, true);
		}
		if (return_storage) return storage;
		return record.kind == DUMP_POSTFIX_EXPRESSION ? old_value : new_value;
	}
	Operand LowerCall(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children, const Operand& supplied_result = Operand())
	{
		if (children.empty()) throw std::runtime_error("semantic call has no callee");
		const DumpNode& callee = arena_.nodes[children[0]];
		Operand builtin_result;
		if (TryLowerCompilerBuiltinCall(record, children, &builtin_result)) return builtin_result;
		if (TryLowerNumericBuiltinCall(record, children, &builtin_result))
			return builtin_result;
		if (stats_) ++stats_->binding_index_probes;
		const bool direct = !record.virtual_call &&
			callee.kind == DUMP_CALLEE &&
			callee.binding != kNoBinding &&
			callee.binding < function_symbols_.size() &&
			function_symbols_[callee.binding] != kNoLowId;
		if (full_expression_cleanup_active_ &&
			((!direct || !program_.bindings[callee.binding].nonthrowing) ||
			 (full_expression_deferred_cleanup_ && full_expression_cleanup_ready_ &&
			  record.eager_full_expression_cleanup)))
			EnsureFullExpressionCleanupSegment();
		TypeId function_type_id = callee.type;
		bool block_pointer_call = false;
		if (!direct)
		{
			function_type_id = ExpressionObjectType(function_type_id);
			const TypeRecord& callable = program_.types.Get(function_type_id);
			block_pointer_call = callable.kind == TYPE_BLOCK_POINTER;
			if (callable.kind == TYPE_POINTER || block_pointer_call)
				function_type_id = callable.child;
		}
		const TypeRecord& function_type = program_.types.Get(function_type_id);
		if (function_type.kind != TYPE_FUNCTION)
			throw std::runtime_error("invalid PA15 indirect callee type");
		const TypeId* parameters = program_.types.Parameters(function_type_id);
		Instruction call(Instruction::CALL);
		CallArguments arguments;
		CallArgumentFlags argument_references;
		const bool indirect_result = UsesIndirectClassResult(function_type.child, callee.binding);
		call.type = indirect_result ? LowVoid() : LowerType(record.type);
		call.indirect = !direct;
		if (direct)
		{
			output_.symbols[function_symbols_[callee.binding]].referenced = true;
			call.first = Operand(Operand::FUNCTION,
				function_symbols_[callee.binding], LowPtr());
		}
		Operand result_storage;
		Operand virtual_object;
		if (indirect_result)
		{
			if (supplied_result.kind != Operand::NONE)
				result_storage = supplied_result;
			else
			{
				const LowType type = LowerStorageType(function_type.child);
				const char* purpose = record.reference_call_materialization ? "refcall" : "call";
				const Operand slot(EnsureGeneratedSlot(node, purpose, type), type);
				result_storage = AddressOfStorage(slot);
			}
			arguments.Push(result_storage);
			argument_references.Push(Instruction::CALL_PASS_INDIRECT_RESULT);
		}
		Operand block_object;
		if (block_pointer_call)
		{
			block_object = LowerValue(children[0], LowPtr());
			arguments.Push(block_object);
			argument_references.Push(Instruction::CALL_PASS_VALUE);
		}
		const bool member_pointer_call =
			IsMemberPointerApplication(callee);
		Operand member_pointer_callee;
		std::size_t member_pointer_argument =
			std::numeric_limits<std::size_t>::max();
		if (member_pointer_call)
		{
			const NodeChildren application_children = Children(children[0]);
			member_pointer_argument = arguments.size();
			arguments.Push(MemberPointerObject(
				callee, application_children));
			argument_references.Push(Instruction::CALL_PASS_VALUE);
		}
		const std::size_t lowered_argument_begin = arguments.size();
		for (std::size_t i = 1; i < children.size(); ++i)
		{
			const bool reference = i - 1 < function_type.parameter_count && IsReferenceType(parameters[i - 1]);
			argument_references.Push(i - 1 < function_type.parameter_count ?
				BoundaryCallPassing(parameters[i - 1]) : Instruction::CALL_PASS_VALUE);
			if (arena_.nodes[children[i]].variadic_class_argument)
				arguments.Push(LowerStorage(children[i]));
			else if (!reference && i - 1 < function_type.parameter_count &&
				IsComplexObjectType(parameters[i - 1]) &&
				UsesIndirectClassParameter(parameters[i - 1]))
				arguments.Push(AddressOfStorage(LowerStorage(children[i])));
			else if (!reference &&
				arena_.nodes[children[i]].class_argument_staging)
				arguments.Push(LowerClassArgumentStaging(
					children[i], parameters[i - 1]));
			else if (reference)
				arguments.Push(LowerReferenceCallArgument(
					children[i], parameters[i - 1]));
			else
			{
				LowType expected = i - 1 < function_type.parameter_count ?
					LowerType(parameters[i - 1]) :
					LowerExpressionType(arena_.nodes[children[i]].type);
				if (i - 1 >= function_type.parameter_count)
				{
					if (expected.kind == LOW_F32) expected = LowF64();
					else if (IsInteger(expected) && expected.width < 32)
						expected = LowI32();
				}
				arguments.Push(LowerConvertedValue(children[i], expected,
					CanonicalizeInitializerImmediate(children[i], expected) ||
					CanonicalizeImmediateConversion(children[i]) ||
					CanonicalizeOperatorLiteral(children[i], callee)));
			}
			if (record.virtual_call && i == 1)
				virtual_object = arguments[arguments.size() - 1];
		}
		CallArguments lowered_boundary_arguments;
		for (std::size_t i = lowered_argument_begin;
			i < arguments.size(); ++i)
			lowered_boundary_arguments.Push(arguments[i]);
		const std::size_t boundary_argument_begin = arguments.size();
		AppendCallVirtualBaseArguments(callee, children,
			lowered_boundary_arguments, &arguments, &argument_references);
		call.virtual_base_argument_count = static_cast<std::uint32_t>(
			arguments.size() - boundary_argument_begin);
		if (member_pointer_call)
		{
			const MemberPointerCallOperands lowered = LowerMemberPointerCall(
				children[0], callee, Children(children[0]),
				arguments[member_pointer_argument]);
			arguments[member_pointer_argument] = lowered.object;
			member_pointer_callee = lowered.callee;
		}
		if (full_expression_cleanup_active_ && full_expression_deferred_cleanup_) EnsureFullExpressionCleanupSegment();
		if (record.virtual_call)
		{
			if (virtual_object.kind == Operand::NONE || record.virtual_slot == kNoDumpEdge)
				throw std::logic_error("virtual call has no object or slot");
			call.first = LowerVirtualCallee(record, virtual_object,
				ResolveHostVirtualSlot(program_, output_.host_object_emission,
					polymorphism_, record, BaseEntityForType(arena_.nodes[children[1]].type)));
		}
		else if (!direct) call.first = member_pointer_call ?
			member_pointer_callee : block_pointer_call ?
			LoadBlockInvoke(block_object) : LowerValue(children[0], LowPtr());
		AttachCallArguments(&call, arguments, argument_references);
		if (call.type.kind == LOW_VOID)
		{
			Emit(call);
			return indirect_result ? result_storage : Operand(0, LowVoid());
		}
		const Operand result = Temp(call.type);
		call.dest = result.id;
		Emit(call);
		return RetainFullExpressionCallResult(node, record, result);
	}
	Operand LowerConditional(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children)
	{
		if (children.size() != 3) throw std::runtime_error("invalid semantic conditional");
		const LowType type = LowerExpressionType(record.type);
		if (type.kind == LOW_VOID)
			return LowerDiscardedConditional(node, children);
		const Operand slot(EnsureGeneratedSlot(node, "cond", type), type);
		const BlockId then_block = AddBlock(NewLabel("cond_then"));
		const BlockId else_block = AddBlock(NewLabel("cond_else"));
		const BlockId end_block = AddBlock(NewLabel("cond_end"));
		const Operand condition = LowerCondition(children[0]);
		if (full_expression_cleanup_active_) PauseFullExpressionCleanupSegment();
		EmitBranch(condition, then_block, else_block);
		SelectBlock(then_block);
		Instruction yes_store(Instruction::STORE);
		yes_store.type = type;
		yes_store.first = LowerConvertedValue(children[1], type, false);
		yes_store.second = slot;
		Emit(yes_store);
		LowerBranchCleanupActions(node, children[1]);
		if (full_expression_cleanup_active_) PauseFullExpressionCleanupSegment();
		EmitJump(end_block);
		SelectBlock(else_block);
		Instruction no_store(Instruction::STORE);
		no_store.type = type;
		no_store.first = LowerConvertedValue(children[2], type, false);
		no_store.second = slot;
		Emit(no_store);
		LowerBranchCleanupActions(node, children[2]);
		if (full_expression_cleanup_active_) PauseFullExpressionCleanupSegment();
		EmitJump(end_block);
		SelectBlock(end_block);
		const Operand result = Temp(type);
		Instruction load(Instruction::LOAD);
		load.dest = result.id;
		load.type = type;
		load.first = slot;
		Emit(load);
		return result;
	}

	Operand LowerDiscardedConditional(std::uint32_t node,
		const NodeChildren& children)
	{
		const BlockId then_block = AddBlock(NewLabel("discard_cond_then"));
		const BlockId else_block = AddBlock(NewLabel("discard_cond_else"));
		const BlockId end_block = AddBlock(NewLabel("discard_cond_end"));
		const Operand condition = LowerCondition(children[0]);
		if (full_expression_cleanup_active_) PauseFullExpressionCleanupSegment();
		EmitBranch(condition, then_block, else_block);
		SelectBlock(then_block);
		(void)LowerValue(children[1]);
		LowerBranchCleanupActions(node, children[1]);
		if (full_expression_cleanup_active_) PauseFullExpressionCleanupSegment();
		EmitJump(end_block);
		SelectBlock(else_block);
		(void)LowerValue(children[2]);
		LowerBranchCleanupActions(node, children[2]);
		if (full_expression_cleanup_active_) PauseFullExpressionCleanupSegment();
		EmitJump(end_block);
		SelectBlock(end_block);
		return Operand(0, LowVoid());
	}

	Operand LowerConditionalAddress(std::uint32_t node,
		const NodeChildren& children)
	{
		if (children.size() != 3)
			throw std::runtime_error("invalid semantic address conditional");
		const Operand slot(EnsureGeneratedSlot(node, "condaddr", LowPtr()),
			LowPtr());
		const BlockId then_block = AddBlock(NewLabel("condaddr_then"));
		const BlockId else_block = AddBlock(NewLabel("condaddr_else"));
		const BlockId end_block = AddBlock(NewLabel("condaddr_end"));
		const Operand condition = LowerCondition(children[0]);
		if (full_expression_cleanup_active_) PauseFullExpressionCleanupSegment();
		EmitBranch(condition, then_block, else_block);
		SelectBlock(then_block);
		Instruction yes_store(Instruction::STORE);
		yes_store.type = LowPtr();
		yes_store.first = AddressOfStorage(LowerStorage(children[1]));
		yes_store.second = slot;
		Emit(yes_store);
		LowerBranchCleanupActions(node, children[1]);
		if (full_expression_cleanup_active_) PauseFullExpressionCleanupSegment();
		EmitJump(end_block);
		SelectBlock(else_block);
		Instruction no_store(Instruction::STORE);
		no_store.type = LowPtr();
		no_store.first = AddressOfStorage(LowerStorage(children[2]));
		no_store.second = slot;
		Emit(no_store);
		LowerBranchCleanupActions(node, children[2]);
		if (full_expression_cleanup_active_) PauseFullExpressionCleanupSegment();
		EmitJump(end_block);
		SelectBlock(end_block);
		return LoadStorage(slot, LowPtr());
	}

	void PushStatementNode(std::uint32_t node)
	{
		StatementTask task(STATEMENT_NODE);
		task.node = node;
		statement_tasks_.push_back(task);
	}

	void PushStatementSequence(std::uint32_t edge,
		StatementTaskKind kind = STATEMENT_SEQUENCE)
	{
		if (edge == kNoDumpEdge) return;
		StatementTask task(kind);
		task.node = edge;
		statement_tasks_.push_back(task);
	}

	void RunStatementTask(const StatementTask& task)
	{
		if (task.kind == STATEMENT_NODE)
		{
			LowerStatementNode(task.node);
			return;
		}
		if (task.kind == STATEMENT_SEQUENCE)
		{
			const std::uint32_t child = arena_.edges[task.node].child;
			const DumpKind child_kind = arena_.nodes[child].kind;
			if (CurrentBlock().terminated && child_kind != DUMP_CASE_STATEMENT &&
				child_kind != DUMP_DEFAULT_STATEMENT &&
				child_kind != DUMP_LABELED_STATEMENT)
				return;
			PushStatementSequence(arena_.edges[task.node].next);
			PushStatementNode(child);
			return;
		}
		if (task.kind == STATEMENT_FOR_COMPONENTS)
		{
			const std::uint32_t child = arena_.edges[task.node].child;
			PushStatementSequence(arena_.edges[task.node].next,
				STATEMENT_FOR_COMPONENTS);
			LowerForComponent(child);
			return;
		}
		if (task.kind == STATEMENT_IF_AFTER_THEN)
		{
			const bool then_terminated = CurrentBlock().terminated;
			if (!then_terminated) EmitJump(task.second);
			SelectBlock(task.first);
			StatementTask after(STATEMENT_IF_AFTER_ELSE);
			after.first = task.second;
			after.flag = then_terminated;
			statement_tasks_.push_back(after);
			if (task.node != kNoDumpEdge) PushStatementNode(task.node);
			return;
		}
		if (task.kind == STATEMENT_IF_AFTER_ELSE)
		{
			const bool else_terminated = CurrentBlock().terminated;
			if (!else_terminated) EmitJump(task.first);
			if (!task.flag || !else_terminated) SelectBlock(task.first);
			return;
		}
		if (task.kind == STATEMENT_LOOP_AFTER_BODY)
		{
			PopLoopTargets();
			if (!CurrentBlock().terminated) EmitJump(task.first);
			SelectBlock(task.second);
			return;
		}
		if (task.kind == STATEMENT_DO_AFTER_BODY)
		{
			PopLoopTargets();
			if (!CurrentBlock().terminated) EmitJump(task.second);
			SelectBlock(task.second);
			EmitBranch(LowerControlCondition(task.node), task.first, task.third);
			SelectBlock(task.third);
			return;
		}
		if (task.kind == STATEMENT_FOR_AFTER_INIT)
		{
			StartForLoop(task.node, task.auxiliary, task.last);
			return;
		}
		if (task.kind == STATEMENT_FOR_AFTER_BODY)
		{
			PopLoopTargets();
			if (!CurrentBlock().terminated) EmitJump(task.first);
			SelectBlock(task.first);
			StatementTask after(STATEMENT_FOR_AFTER_ITERATION);
			after.first = task.second;
			after.second = task.third;
			statement_tasks_.push_back(after);
			if (task.node != kNoDumpEdge) PushStatementNode(task.node);
			return;
		}
		if (task.kind == STATEMENT_FOR_AFTER_ITERATION)
		{
			if (!CurrentBlock().terminated) EmitJump(task.first);
			SelectBlock(task.second);
			return;
		}
		if (task.kind == STATEMENT_SWITCH_AFTER_BODY)
		{
			if (break_targets_.empty())
				throw std::logic_error("missing PA15 switch target");
			break_targets_.pop_back();
			if (!CurrentBlock().terminated) EmitContinuationJump(task.first);
			SelectBlock(task.first);
			return;
		}
		if (RunExceptionStatementTask(task)) return;
		throw std::logic_error("invalid PA15 statement task");
	}
	void PopLoopTargets()
	{
		if (break_targets_.empty() || continue_targets_.empty())
			throw std::logic_error("missing PA15 loop target");
		continue_targets_.pop_back();
		break_targets_.pop_back();
	}
	void LowerVariableInitializationCore(const DumpNode& record,
		const NodeChildren& children,
		const Operand& retained_destination = Operand())
	{
		if (retained_destination.kind != Operand::NONE &&
			!IsReferenceType(record.type) &&
			IsClassObjectType(record.type) && children.size() == 1 &&
			arena_.nodes[children[0]].kind == DUMP_CALL_EXPRESSION)
		{
			const DumpNode& call = arena_.nodes[children[0]];
			(void)LowerCall(children[0], call, Children(children[0]),
				retained_destination);
			return;
		}
		if (LowerScalarCallReferenceInitialization(record, children,
			retained_destination)) return;
		if (TryLowerComplexVariableInitialization(
			record, children, retained_destination)) return;
		if (!IsReferenceType(record.type) &&
			IsClassObjectType(record.type) && children.size() == 1 &&
			arena_.nodes[children[0]].kind == DUMP_CONDITIONAL_EXPRESSION)
		{
			const LowType type = LowerStorageType(record.type);
			const Operand destination = retained_destination.kind == Operand::NONE ?
				AddressOfStorage(StorageFor(record.binding, type)) :
				retained_destination;
			LowerClassConditionalResult(children[0], destination);
			return;
		}
		if (IsClassObjectType(record.type) && children.size() == 1 &&
			arena_.nodes[children[0]].kind == DUMP_CLASS_VALUE_TRANSFER)
		{
			const LowType type = LowerStorageType(record.type);
			const Operand destination = retained_destination.kind == Operand::NONE ?
				AddressOfStorage(StorageFor(record.binding, type)) :
				retained_destination;
			LowerClassValueTransfer(children[0], destination, true);
			return;
		}
		if (LowerInitializerListVariable(record, children)) return;
		if (children.size() == 1 && arena_.nodes[children[0]].kind ==
			DUMP_CONSTRUCTOR_ARRAY_ACTION)
		{
			LowerBoundConstructorArray(children[0], record.binding);
			return;
		}
		if (IsClassObjectType(record.type) && children.size() == 1 &&
			arena_.nodes[children[0]].kind == DUMP_BRACED_INIT_LIST)
		{
			LowerClassInitializer(record, children[0]);
			return;
		}
		if (LowerVariableConstructor(record, children)) return;
		if (!children.empty())
		{
			if (!IsReferenceType(record.type) && IsArrayType(record.type))
			{
				LowerArrayInitializer(record, children);
				return;
			}
			const LowType type = LowerStorageType(record.type);
			Instruction store(Instruction::STORE);
			store.type = type;
			const Operand value = IsReferenceType(record.type) ?
				AddressOfStorage(LowerStorage(children[0])) :
				LowerInitializerConvertedValue(children[0], type);
			if (CurrentBlock().terminated) return;
			store.first = value;
			store.second = retained_destination.kind == Operand::NONE ?
				StorageFor(record.binding, type) : retained_destination;
			Emit(store);
		}
		else if (IsClassObjectType(record.type))
		{
			const LowType type = LowerStorageType(record.type);
			(void)AddressOfStorage(StorageFor(record.binding, type));
		}
	}

	void LowerStatementNode(std::uint32_t node)
	{
		if (stats_) ++stats_->lowered_nodes;
		const DumpNode& record = arena_.nodes[node];
		const NodeChildren children = Children(node);
		if (TryLowerGnuAsmStatement(record, children)) return;
		if (record.kind == DUMP_TYPE_ALIAS) return;
		if (record.kind == DUMP_COMPOUND_STATEMENT ||
			record.kind == DUMP_CONDITION_DECLARATION ||
			record.kind == DUMP_THEN || record.kind == DUMP_ELSE)
		{
			PushStatementSequence(record.first_edge);
			return;
		}
		if (record.kind == DUMP_SIMPLE_DECLARATION)
		{
			if (!children.empty() &&
				arena_.nodes[children[0]].kind == DUMP_VARIABLE)
			{
				const DumpNode& variable = arena_.nodes[children[0]];
				const std::uint32_t local_static = variable.binding <
					local_static_action_.size() ?
					local_static_action_[variable.binding] : kNoDumpEdge;
				if (local_static != kNoDumpEdge)
				{
					LowerLocalStaticVariable(local_static, variable,
						Children(children[0]), &children);
					return;
				}
			}
			if (!record.full_expression_staging ||
				!TryLowerFullExpressionDeclaration(children))
				PushStatementSequence(record.first_edge);
			return;
		}
		if (record.kind == DUMP_VARIABLE)
		{
			const std::uint32_t local_static = record.binding <
				local_static_action_.size() ?
				local_static_action_[record.binding] : kNoDumpEdge;
			if (local_static != kNoDumpEdge)
				LowerLocalStaticVariable(local_static, record, children);
			else LowerFullExpressionVariableInitialization(record, children);
			return;
		}
		if (record.kind == DUMP_INITIALIZER_ACTION)
		{
			LowerMemberInitializationAction(record, children);
			return;
		}
		if (record.kind == DUMP_VPTR_INITIALIZATION_ACTION)
		{
			LowerVptrInitializationAction(record);
			return;
		}
		if (TryLowerConstructorInitializationAction(record, children)) return;
		if (record.kind == DUMP_DESTRUCTOR_ACTION)
		{
			LowerDestructorAction(record);
			return;
		}
		if (record.kind == DUMP_RETURN_STATEMENT)
		{
			LowerReturn(children);
			return;
		}
		if (record.kind == DUMP_EXPRESSION_STATEMENT) {
			LowerFullExpressionStatement(children);
			return;
		}
		if (record.kind == DUMP_IF_STATEMENT) { LowerIf(children); return; }
		if (record.kind == DUMP_WHILE_STATEMENT) { LowerWhile(children); return; }
		if (record.kind == DUMP_DO_STATEMENT) { LowerDo(children); return; }
		if (record.kind == DUMP_FOR_STATEMENT) { LowerFor(children); return; }
		if (record.kind == DUMP_SWITCH_STATEMENT) { LowerSwitch(children); return; }
		if (TryLowerExceptionStatement(node, record, children)) return;
		if (record.kind == DUMP_CASE_STATEMENT ||
			record.kind == DUMP_DEFAULT_STATEMENT)
		{
			if (node >= switch_case_blocks_.size() ||
				switch_case_blocks_[node] == kNoLowId)
				throw std::runtime_error("PA15 case has no switch target");
			const BlockId target = switch_case_blocks_[node];
			if (!CurrentBlock().terminated) EmitContinuationJump(target);
			SelectBlock(target);
			std::uint32_t edge = record.first_edge;
			if (record.kind == DUMP_CASE_STATEMENT && edge != kNoDumpEdge)
				edge = arena_.edges[edge].next;
			PushStatementSequence(edge);
			return;
		}
		if (record.kind == DUMP_LABELED_STATEMENT)
		{
			const BlockId target = LabelBlock(record.text);
			if (!CurrentBlock().terminated) EmitJump(target);
			SelectBlock(target);
			PushStatementSequence(record.first_edge);
			return;
		}
		if (record.kind == DUMP_GOTO_STATEMENT)
		{
			LowerGotoControlExit(record, children);
			EmitJump(LabelBlock(record.text));
			return;
		}
		if (record.kind == DUMP_ITERATION ||
			(record.kind == DUMP_FOR_INIT_STATEMENT && !children.empty() &&
			 arena_.nodes[children[0]].kind != DUMP_SIMPLE_DECLARATION))
		{ LowerFullExpressionStatement(children); return; }
		if (record.kind == DUMP_FOR_INIT_STATEMENT)
		{ PushStatementSequence(record.first_edge, STATEMENT_FOR_COMPONENTS); return; }
		if (record.kind == DUMP_BREAK_STATEMENT)
		{
			if (break_targets_.empty())
				throw std::runtime_error("PA15 break has no target");
			LowerStructuredControlExit(children, break_targets_.back().region_depth);
			EmitJump(break_targets_.back().block);
			return;
		}
		if (record.kind == DUMP_CONTINUE_STATEMENT)
		{
			if (continue_targets_.empty())
				throw std::runtime_error("PA15 continue has no target");
			LowerStructuredControlExit(children, continue_targets_.back().region_depth);
			EmitJump(continue_targets_.back().block);
			return;
		}
		throw std::runtime_error("statement is outside the active PA15 checkpoint");
	}
	void LowerArrayInitializer(const DumpNode& record,
		const NodeChildren& variable_children)
	{
		if (variable_children.size() != 1)
			throw std::runtime_error("invalid PA15 array initializer");
		const NodeChildren values = Children(variable_children[0]);
		const TypeRecord& array = program_.types.Get(
			ExpressionObjectType(record.type));
		if (array.kind != TYPE_ARRAY ||
			(array.bound == 0 &&
			 (record.storage_size == 0 || !values.empty())) ||
			values.size() > array.bound)
			throw std::runtime_error("invalid PA15 bounded array initializer");
		if (array.bound == 0)
		{
			(void)AddressOfStorage(StorageFor(
				record.binding, LowerVariableStorage(record)));
			return;
		}
		if (!lowering_namespace_object_ &&
			!IsClassObjectType(array.child) && !IsArrayType(array.child))
		{
			const Operand base = AddressOfStorage(
				StorageFor(record.binding, LowerVariableStorage(record)));
			const LowType element = LowerExpressionType(array.child);
			const std::size_t element_size = program_.SizeOf(array.child);
			for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
			{
				Operand destination = base;
				if (i != 0)
					destination = IndexAddress(LowI8(), base,
						Operand(i * element_size, LowI64()), false);
				Instruction store(Instruction::STORE);
				store.type = element;
				store.first = i < values.size() ?
					LowerConvertedValue(values[i], element) : Operand(0, element);
				store.second = destination;
				Emit(store);
			}
			return;
		}
		if (IsClassObjectType(array.child))
		{
			if (!lowering_namespace_object_)
			{
				LowerLocalClassArrayInitializer(record, values);
				return;
			}
			AggregatePath path;
			LowerNamespaceClassArrayInitializer(record, array, values, &path);
			return;
		}
		const Operand storage = StorageFor(
			record.binding, LowerStorageType(record.type));
		LowerRuntimeArrayValues(record.type, variable_children[0],
			AddressOfStorage(storage), true);
	}
	void LowerBoundAggregateArrayActions(BindingId object, TypeId array_type,
		std::size_t element_index, std::uint32_t list_node,
		AggregatePath* path)
	{
		const NodeChildren actions = Children(list_node);
		for (std::size_t i = 0; i < actions.size(); ++i)
		{
			const DumpNode& action = arena_.nodes[actions[i]];
			if (action.kind != DUMP_INITIALIZER_ACTION ||
				action.binding == kNoBinding)
				throw std::logic_error("invalid bound aggregate array action");
			const NodeChildren values = Children(actions[i]);
			const bool nested = values.size() == 1 &&
				arena_.nodes[values[0]].kind == DUMP_BRACED_INIT_LIST &&
				IsClassObjectType(action.type);
			path->Push(action.binding);
			if (nested)
				LowerBoundAggregateArrayActions(object, array_type, element_index,
					values[0], path);
			else
			{
				if (values.size() > 1 || IsArrayType(action.type))
					throw std::runtime_error(
						"complex bound aggregate leaf is outside the checkpoint");
				Instruction store(Instruction::STORE);
				if (IsReferenceType(action.type))
				{
					if (values.empty())
						throw std::logic_error(
							"aggregate reference action has no value");
					store.type = LowPtr();
					store.first = AddressOfStorage(LowerStorage(values[0]));
				}
				else
				{
					store.type = LowerExpressionType(action.type);
					store.first = values.empty() ?
						(store.type.kind == LOW_PTR ?
							Operand::NullPointer(store.type) :
						 IsFloating(store.type) ?
							FloatingOperand("0.0", store.type) :
							Operand(0, store.type)) :
						LowerConvertedValue(values[0], store.type, false);
				}
				Operand destination = AddressOfStorage(StorageFor(object,
					LowerStorageType(array_type)));
				destination = DecayAddress(destination);
				const TypeRecord& array = program_.types.Get(
					ExpressionObjectType(array_type));
				Operand displacement(static_cast<std::int64_t>(element_index),
					LowI64());
				const std::size_t element_size = program_.SizeOf(array.child);
				if (element_size != 1)
				{
					const Operand scaled = Temp(LowI64());
					Instruction multiply(Instruction::BINARY);
					multiply.dest = scaled.id;
					multiply.op = LOW_OP_MUL;
					multiply.type = LowI64();
					multiply.first = displacement;
					multiply.second = Operand(
						static_cast<std::int64_t>(element_size), LowI64());
					Emit(multiply);
					displacement = scaled;
				}
				destination = IndexAddress(LowI8(), destination,
					displacement, true);
				for (std::size_t member = 0; member < path->size(); ++member)
					destination = ProjectAggregateMember(destination,
						(*path)[member]);
				store.second = destination;
				Emit(store);
			}
			path->Pop();
		}
	}
	void LowerRuntimeArrayValues(TypeId type, std::uint32_t list_node,
		const Operand& array_address, bool compact_addressing = false)
	{
		const TypeRecord& array = program_.types.Get(
			ExpressionObjectType(type));
		const NodeChildren values = Children(list_node);
		if (array.kind != TYPE_ARRAY || array.bound == 0 ||
			values.size() > array.bound)
			throw std::runtime_error("invalid runtime array initializer");
		const Operand base = compact_addressing ?
			array_address : DecayAddress(array_address);
		const std::size_t element_size = program_.SizeOf(array.child);
		for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
		{
			const Operand displacement(static_cast<std::int64_t>(
				i * element_size), LowI64());
			const Operand destination = compact_addressing && i == 0 ? base :
				IndexAddress(LowI8(), base, displacement, true);
			if (i < values.size())
				LowerRuntimeObjectValue(array.child, values[i], destination);
			else LowerRuntimeZeroValue(array.child, destination);
		}
	}
	void LowerRuntimeObjectValue(TypeId type, std::uint32_t node,
		const Operand& destination)
	{
		if (LowerInitializerListRuntimeValue(node, destination)) return;
		const TypeRecord& record = program_.types.Get(ExpressionObjectType(type));
		if (record.kind == TYPE_ARRAY)
		{
			if (arena_.nodes[node].kind != DUMP_BRACED_INIT_LIST)
				throw std::runtime_error("nested runtime array requires braces");
			LowerRuntimeArrayValues(type, node, destination, true);
			return;
		}
		if (IsClassObjectType(type))
		{
			if (LowerRuntimeConstructorValue(type, node, destination)) return;
			if (arena_.nodes[node].kind != DUMP_BRACED_INIT_LIST)
				throw std::runtime_error("runtime aggregate element requires braces");
			AggregatePath path;
			LowerAggregateActions(node, destination, &path, destination);
			return;
		}
		Instruction store(Instruction::STORE);
		store.type = LowerExpressionType(type);
		store.first = LowerConvertedValue(node, store.type, false);
		store.second = destination;
		Emit(store);
	}
	void LowerClassInitializer(const DumpNode& variable,
		std::uint32_t initializer)
	{
		ResetInitializedBitFieldUnit();
		const Operand storage = StorageFor(variable.binding,
			LowerStorageType(variable.type));
		if (LowerClassValueInitialization(variable, initializer, storage)) return;
		Operand retained_address; if (NeedsClassInitializerStorageAddress(variable, initializer)) retained_address = AddressOfStorage(storage);
		AggregatePath path; LowerAggregateActions(initializer, storage, &path,
			LambdaClosureEntity(program_, variable.type) == kNoEntity ? Operand() : retained_address);
	}
	bool AggregateHasLeaf(std::uint32_t list_node) const
	{
		const NodeChildren actions = Children(list_node);
		for (std::size_t i = 0; i < actions.size(); ++i)
		{
			const NodeChildren values = Children(actions[i]);
			if (values.size() == 1 &&
				arena_.nodes[values[0]].kind == DUMP_BRACED_INIT_LIST &&
				IsClassObjectType(arena_.nodes[actions[i]].type))
			{
				if (AggregateHasLeaf(values[0])) return true;
			}
			else return true;
		}
		return false;
	}
	void LowerAggregateActions(std::uint32_t list_node,
		const Operand& root, AggregatePath* path,
		const Operand& retained_address)
	{
		ResetInitializedBitFieldUnit();
		if (stats_) ++stats_->lowered_nodes;
		const DumpNode& list = arena_.nodes[list_node];
		if (list.kind != DUMP_BRACED_INIT_LIST)
			throw std::logic_error("class initializer is not an action list");
		const NodeChildren actions = Children(list_node);
		for (std::size_t i = 0; i < actions.size(); ++i)
		{
			const DumpNode& action = arena_.nodes[actions[i]];
			if (action.kind != DUMP_INITIALIZER_ACTION ||
				action.binding == kNoBinding ||
				action.binding >= program_.bindings.size())
				throw std::logic_error("invalid aggregate initializer action");
			if (stats_) ++stats_->lowered_nodes;
			const NodeChildren values = Children(actions[i]);
			const bool nested = values.size() == 1 &&
				arena_.nodes[values[0]].kind == DUMP_BRACED_INIT_LIST &&
				IsClassObjectType(action.type);
			if (retained_address.kind != Operand::NONE)
			{
				const Operand destination = ProjectAggregateMember(
					retained_address, action.binding);
				if (nested)
					LowerAggregateActions(values[0], root, path, destination);
				else
					LowerAggregateLeaf(action, values, root, *path, destination);
				continue;
			}
			path->Push(action.binding);
			if (nested && path->size() == kAggregateProjectionReplayLimit)
			{
				const Operand destination = ProjectAggregatePath(root, *path);
				LowerAggregateActions(values[0], root, path, destination);
			}
			else if (nested)
				LowerAggregateActions(values[0], root, path, Operand());
			else
				LowerAggregateLeaf(action, values, root, *path, Operand());
			path->Pop();
		}
	}
	Operand ProjectAggregateMember(const Operand& base, BindingId binding)
	{
		const BindingRecord& member = program_.bindings[binding];
		if (IsLambdaCaptureMember(program_, binding)) return base;
		const Operand projected = Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = projected.id;
		index.type = LowI8();
		index.first = base;
		index.second = Operand(
			static_cast<std::int64_t>(member.member_offset), LowI64());
		index.projection = INDEX_PROJECTION_FIELD;
		Emit(index);
		return projected;
	}
	Operand ProjectConstructorMemberPath(
		const pa16_lowering_detail::ConstructorMemberPath& path)
	{
		Operand destination = LoadStorage(
			StorageFor(current_this_binding_, LowPtr()), LowPtr());
		for (std::size_t i = 0; i < path.size(); ++i)
			destination = ProjectAggregateMember(destination, path[i]);
		return destination;
	}
	void LowerConstructorArrayActions(TypeId type, std::uint32_t list_node,
		const pa16_lowering_detail::ConstructorMemberPath& path)
	{
		const TypeRecord& array = program_.types.Get(ExpressionObjectType(type));
		const NodeChildren values = Children(list_node);
		if (array.kind != TYPE_ARRAY || array.bound == 0 ||
			values.size() > array.bound)
			throw std::runtime_error("invalid constructor array initializer");
		const LowType element = LowerExpressionType(array.child);
		for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
		{
			Operand value;
			if (i < values.size())
				value = LowerConvertedValue(values[i], element);
			else if (element.kind == LOW_PTR)
				value = Operand::NullPointer(element);
			else if (IsFloating(element))
				value = FloatingOperand("0.0", element);
			else value = Operand(0, element);
			const Operand base = DecayAddress(
				ProjectConstructorMemberPath(path));
			const Operand destination = IndexAddress(element, base,
				Operand(static_cast<std::int64_t>(i), LowI64()), true);
			Instruction store(Instruction::STORE);
			store.type = element;
			store.first = value;
			store.second = destination;
			Emit(store);
		}
	}

	Operand ProjectAggregatePath(const Operand& root,
		const AggregatePath& path)
	{
		Operand destination = AddressOfStorage(root);
		for (std::size_t i = 0; i < path.size(); ++i)
			destination = ProjectAggregateMember(destination, path[i]);
		return destination;
	}

	void LowerAggregateLeaf(const DumpNode& action,
		const NodeChildren& values, const Operand& root,
		const AggregatePath& path, const Operand& retained_destination)
	{
		if (values.size() > 1)
			throw std::logic_error("aggregate leaf has multiple values");
		if (IsArrayType(action.type))
		{
			LowerAggregateArrayLeaf(
				action, values, root, path, retained_destination);
			return;
		}
		if (LowerAggregateConstructorLeaf(
			action, values, root, path, retained_destination)) return;
		Instruction store(Instruction::STORE);
		if (IsReferenceType(action.type))
		{
			if (values.empty())
				throw std::logic_error("aggregate reference action has no value");
			store.type = LowPtr();
			store.first = AddressOfStorage(LowerStorage(values[0]));
		}
		else
		{
			store.type = LowerExpressionType(action.type);
			if (!values.empty())
			{
				const LowType source = LowerExpressionType(arena_.nodes[values[0]].type);
				store.first = LowerConvertedValue(values[0], store.type, IsInteger(source) &&
					IsInteger(store.type) && source.is_signed == store.type.is_signed);
			}
			else if (store.type.kind == LOW_PTR)
				store.first = Operand::NullPointer(store.type);
			else if (IsFloating(store.type))
				store.first = FloatingOperand("0.0", store.type);
			else if (IsInteger(store.type))
				store.first = Operand(0, store.type);
			else throw std::runtime_error(
				"aggregate leaf requires unsupported construction");
		}
		const Operand destination =
			retained_destination.kind == Operand::NONE ?
				ProjectAggregatePath(root, path) : retained_destination;
		if (action.binding != kNoBinding &&
			program_.bindings[action.binding].bit_field)
		{
			const LowType field_type = LowerExpressionType(action.type);
			store.second = destination;
			InitializeBitField(
				action.binding, store.first, store.second, field_type);
		}
		else
		{
			store.second = destination;
			Emit(store);
		}
	}

	__attribute__((noinline)) Operand LowerControlCondition(
		std::uint32_t condition_node)
	{
		const NodeChildren condition_children = Children(condition_node);
		if (condition_children.empty())
			throw std::runtime_error("invalid PA15 control condition");
		const std::uint32_t child = condition_children[0];
		if (arena_.nodes[child].kind != DUMP_CONDITION_DECLARATION)
		{
			return LowerFullExpressionCondition(condition_children);
		}
		return LowerDeclaredCondition(condition_children, true);
	}

	__attribute__((noinline)) Operand LowerSwitchCondition(
		std::uint32_t condition_node)
	{
		const NodeChildren condition_children = Children(condition_node);
		if (condition_children.empty())
			throw std::runtime_error("invalid PA15 switch condition");
		const std::uint32_t child = condition_children[0];
		if (arena_.nodes[child].kind != DUMP_CONDITION_DECLARATION)
			return LowerFullExpressionCondition(condition_children);
		return LowerDeclaredCondition(condition_children, false);
	}

	std::uint32_t FindChildKind(const NodeChildren& children, DumpKind kind) const
	{
		for (std::size_t i = 0; i < children.size(); ++i)
			if (arena_.nodes[children[i]].kind == kind) return children[i];
		return kNoDumpEdge;
	}

	std::uint32_t FindLoopBody(const NodeChildren& children) const
	{
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpKind kind = arena_.nodes[children[i]].kind;
			if (kind != DUMP_CONDITION && kind != DUMP_FOR_INIT_STATEMENT &&
				kind != DUMP_ITERATION)
				return children[i];
		}
		return kNoDumpEdge;
	}

	BlockId LabelBlock(NameId name)
	{
		std::uint32_t found = kNoLowId;
		if (label_blocks_.Find(name, &found)) return found;
		const BlockId block = AddBlock(NewLabel("goto"));
		label_blocks_.Insert(name, block);
		return block;
	}

	void CollectSwitchCases(std::uint32_t node, SwitchCases* cases) const
	{
		std::vector<std::uint32_t> pending(1, node);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const DumpNode& record = arena_.nodes[current];
			if (record.kind == DUMP_SWITCH_STATEMENT) continue;
			if (record.kind == DUMP_CASE_STATEMENT ||
				record.kind == DUMP_DEFAULT_STATEMENT)
				cases->Push(current);
			const NodeChildren children = Children(current);
			for (std::size_t i = children.size(); i != 0; --i)
			{
				if (record.kind == DUMP_CASE_STATEMENT && i == 1) continue;
				pending.push_back(children[i - 1]);
			}
		}
	}

	__attribute__((noinline)) void LowerSwitch(const NodeChildren& children)
	{
		const std::uint32_t condition = FindChildKind(children, DUMP_CONDITION);
		std::uint32_t body = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
			if (arena_.nodes[children[i]].kind != DUMP_CONDITION)
				body = children[i];
		if (condition == kNoDumpEdge || body == kNoDumpEdge)
			throw std::runtime_error("invalid PA15 switch statement");
		SwitchCases cases;
		CollectSwitchCases(body, &cases);
		const Operand value = LowerSwitchCondition(condition);
		const BlockId dispatch = AddBlock(NewLabel("switch_dispatch"));
		const BlockId end = AddBlock(NewLabel("switch_end"));
		BlockId default_target = end;
		for (std::size_t i = 0; i < cases.size(); ++i)
		{
			const bool is_default =
				arena_.nodes[cases[i]].kind == DUMP_DEFAULT_STATEMENT;
			const BlockId target = AddBlock(NewLabel(is_default ?
				"switch_default" : "switch_case"));
			switch_case_blocks_[cases[i]] = target;
			if (is_default) default_target = target;
		}
		EmitJump(dispatch);
		SelectBlock(dispatch);
		Instruction instruction(Instruction::SWITCH);
		instruction.first = value;
		instruction.target = default_target;
		SmallSequence<std::int64_t, 8> case_values;
		SmallSequence<BlockId, 8> case_targets;
		for (std::size_t i = 0; i < cases.size(); ++i)
		{
			if (arena_.nodes[cases[i]].kind != DUMP_CASE_STATEMENT) continue;
			const NodeChildren case_children = Children(cases[i]);
			if (case_children.empty() || !arena_.nodes[case_children[0]].constant)
				throw std::runtime_error("PA15 case lacks constant value");
			case_values.Push(arena_.nodes[case_children[0]].constant_value);
			case_targets.Push(switch_case_blocks_[cases[i]]);
		}
		AttachSwitchCases(&instruction, case_values, case_targets);
		Emit(instruction);
		RecordBlockIncoming(default_target);
		for (std::size_t i = 0; i < case_targets.size(); ++i)
			RecordBlockIncoming(case_targets[i]);
		break_targets_.push_back(ExceptionControlTarget(end, ActiveExceptionRegionCount()));
		StatementTask after(STATEMENT_SWITCH_AFTER_BODY);
		after.first = end;
		statement_tasks_.push_back(after);
		PushStatementNode(body);
	}

	void AttachSwitchCases(Instruction* instruction,
		const SmallSequence<std::int64_t, 8>& values,
		const SmallSequence<BlockId, 8>& targets)
	{
		if (values.size() != targets.size())
			throw std::logic_error("PA15 switch case fact mismatch");
		if (values.empty()) return;
		if (values.size() >= kNoLowId ||
			output_.switch_case_values.size() > kNoLowId - values.size() ||
			output_.switch_case_values.size() !=
				output_.switch_case_targets.size())
			throw std::runtime_error("too many PA15 switch cases");
		instruction->extra_first = static_cast<std::uint32_t>(
			output_.switch_case_values.size());
		instruction->extra_count = static_cast<std::uint32_t>(values.size());
		for (std::size_t i = 0; i < values.size(); ++i)
		{
			output_.switch_case_values.push_back(values[i]);
			output_.switch_case_targets.push_back(targets[i]);
		}
	}

	__attribute__((noinline)) void LowerWhile(const NodeChildren& children)
	{
		const std::uint32_t condition = FindChildKind(children, DUMP_CONDITION);
		const std::uint32_t body = FindLoopBody(children);
		if (condition == kNoDumpEdge || body == kNoDumpEdge)
			throw std::runtime_error("invalid PA15 while statement");
		const BlockId cond_block = AddBlock(NewLabel("while_cond"));
		const BlockId body_block = AddBlock(NewLabel("while_body"));
		const BlockId end_block = AddBlock(NewLabel("while_end"));
		EmitJump(cond_block);
		SelectBlock(cond_block);
		EmitBranch(LowerControlCondition(condition), body_block, end_block);
		SelectBlock(body_block);
		break_targets_.push_back(ExceptionControlTarget(end_block, ActiveExceptionRegionCount()));
		continue_targets_.push_back(ExceptionControlTarget(cond_block, ActiveExceptionRegionCount()));
		StatementTask after(STATEMENT_LOOP_AFTER_BODY);
		after.first = cond_block;
		after.second = end_block;
		statement_tasks_.push_back(after);
		PushStatementNode(body);
	}

	__attribute__((noinline)) void LowerDo(const NodeChildren& children)
	{
		const std::uint32_t condition = FindChildKind(children, DUMP_CONDITION);
		const std::uint32_t body = FindLoopBody(children);
		if (condition == kNoDumpEdge || body == kNoDumpEdge)
			throw std::runtime_error("invalid PA15 do statement");
		const BlockId body_block = AddBlock(NewLabel("do_body"));
		const BlockId cond_block = AddBlock(NewLabel("do_cond"));
		const BlockId end_block = AddBlock(NewLabel("do_end"));
		EmitJump(body_block);
		SelectBlock(body_block);
		break_targets_.push_back(ExceptionControlTarget(end_block, ActiveExceptionRegionCount()));
		continue_targets_.push_back(ExceptionControlTarget(cond_block, ActiveExceptionRegionCount()));
		StatementTask after(STATEMENT_DO_AFTER_BODY);
		after.node = condition;
		after.first = body_block;
		after.second = cond_block;
		after.third = end_block;
		statement_tasks_.push_back(after);
		PushStatementNode(body);
	}

	__attribute__((noinline)) void LowerFor(const NodeChildren& children)
	{
		const std::uint32_t init = FindChildKind(children, DUMP_FOR_INIT_STATEMENT);
		const std::uint32_t condition = FindChildKind(children, DUMP_CONDITION);
		const std::uint32_t iteration = FindChildKind(children, DUMP_ITERATION);
		const std::uint32_t body = FindLoopBody(children);
		if (body == kNoDumpEdge)
			throw std::runtime_error("invalid PA15 for statement");
		if (init != kNoDumpEdge)
		{
			StatementTask after(STATEMENT_FOR_AFTER_INIT);
			after.node = condition;
			after.auxiliary = iteration;
			after.last = body;
			statement_tasks_.push_back(after);
			PushStatementNode(init);
			return;
		}
		StartForLoop(condition, iteration, body);
	}

	void StartForLoop(std::uint32_t condition, std::uint32_t iteration,
		std::uint32_t body)
	{
		const BlockId cond_block = AddBlock(NewLabel("for_cond"));
		const BlockId body_block = AddBlock(NewLabel("for_body"));
		const BlockId iter_block = AddBlock(NewLabel("for_iter"));
		const BlockId end_block = AddBlock(NewLabel("for_end"));
		EmitJump(cond_block);
		SelectBlock(cond_block);
		if (condition == kNoDumpEdge) EmitJump(body_block);
		else EmitBranch(LowerControlCondition(condition), body_block, end_block);
		SelectBlock(body_block);
		break_targets_.push_back(ExceptionControlTarget(end_block, ActiveExceptionRegionCount()));
		continue_targets_.push_back(ExceptionControlTarget(iter_block, ActiveExceptionRegionCount()));
		StatementTask after(STATEMENT_FOR_AFTER_BODY);
		after.node = iteration;
		after.first = iter_block;
		after.second = cond_block;
		after.third = end_block;
		statement_tasks_.push_back(after);
		PushStatementNode(body);
	}

	__attribute__((noinline)) void LowerIf(const NodeChildren& children)
	{
		std::uint32_t condition = kNoDumpEdge;
		std::uint32_t then_node = kNoDumpEdge;
		std::uint32_t else_node = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpKind kind = arena_.nodes[children[i]].kind;
			if (kind == DUMP_CONDITION) condition = children[i];
			else if (kind == DUMP_THEN) then_node = children[i];
			else if (kind == DUMP_ELSE) else_node = children[i];
		}
		if (condition == kNoDumpEdge || then_node == kNoDumpEdge)
			throw std::runtime_error("invalid semantic if statement");
		const NodeChildren condition_children = Children(condition);
		if (condition_children.empty())
			throw std::runtime_error("condition declarations are outside the active checkpoint");
		const BlockId then_block = AddBlock(NewLabel("if_then"));
		const BlockId else_block = AddBlock(NewLabel("if_else"));
		const BlockId end_block = AddBlock(NewLabel("if_end"));
		if (arena_.nodes[condition_children[0]].kind ==
			DUMP_CONDITION_DECLARATION)
			EmitBranch(LowerControlCondition(condition), then_block, else_block);
		else if (condition_children.size() != 1 ||
			arena_.nodes[condition].full_expression_staging)
			EmitFullExpressionConditionBranch(condition_children, then_block, else_block,
				InitializerListLifetimeObservationDispatch(condition));
		else EmitConditionBranch(condition_children[0], then_block, else_block);
		SelectBlock(then_block);
		StatementTask after(STATEMENT_IF_AFTER_THEN);
		after.node = else_node;
		after.first = else_block;
		after.second = end_block;
		statement_tasks_.push_back(after);
		PushStatementNode(then_node);
	}

	void EmitConditionBranch(std::uint32_t node, BlockId true_block,
		BlockId false_block)
	{
		const DumpNode& record = arena_.nodes[node];
		const NodeChildren children = Children(node);
		if (record.constant &&
			(record.kind == DUMP_LITERAL || children.empty()))
		{ EmitJump(record.constant_value ? true_block : false_block); return; }
		if (record.kind == DUMP_BINARY_EXPRESSION && children.size() == 2)
		{
			if (record.logical_operation == LOGICAL_OPERATION_AND)
			{
				const BlockId rhs = AddBlock(NewLabel("land_rhs"));
				EmitConditionBranch(children[0], rhs, false_block);
				SelectBlock(rhs);
				EmitConditionBranch(children[1], true_block, false_block);
				return;
			}
			if (record.logical_operation == LOGICAL_OPERATION_OR)
			{
				const BlockId rhs = AddBlock(NewLabel("lor_rhs"));
				EmitConditionBranch(children[0], true_block, rhs);
				SelectBlock(rhs);
				EmitConditionBranch(children[1], true_block, false_block);
				return;
			}
		}
		const Operand value = LowerCondition(node);
		if (full_expression_cleanup_active_) PauseFullExpressionCleanupSegment();
		EmitBranch(value, true_block, false_block);
	}
	const SemanticGraphView& graph_;
	const Program& program_;
	const DumpArena& arena_;
	TypedProgram& output_;
	LowIRLoweringStats* stats_;
	std::vector<SymbolId> function_symbols_;
	std::vector<SymbolId> global_symbols_;
	std::vector<SymbolId> literal_symbols_;
	std::vector<std::uint8_t> temporary_initialized_;
	std::vector<Operand> temporary_addresses_;
	pa26_lowering_detail::InitializerListLoweringState initializer_lists_;
	std::vector<std::pair<std::uint32_t, bool> > namespace_initializers_;
	std::vector<std::uint32_t> dynamic_finalizers_;
	std::vector<std::pair<std::uint32_t, std::string> > thread_local_objects_;
	std::vector<std::pair<SymbolId, std::string> > thread_local_declarations_;
	std::vector<std::uint8_t> thread_local_dynamic_;
	std::vector<SymbolId> tls_access_wrapper_symbols_;
	SymbolId process_atexit_runtime_symbol_;
	SymbolId thread_atexit_runtime_symbol_;
	SymbolId dso_handle_symbol_;
	std::vector<std::uint32_t> function_definition_;
	std::vector<std::uint32_t> function_declaration_;
	pa28_lowering_detail::VirtualBaseContractState virtual_base_contracts_;
	std::vector<std::uint32_t> global_node_;
	std::vector<std::uint32_t> namespace_action_;
	std::vector<std::uint32_t> local_static_action_;
	std::vector<SymbolId> local_static_guard_symbols_;
	std::vector<SymbolId> local_static_destructor_symbols_;
	std::vector<std::uint8_t> local_static_dynamic_;
	std::vector<std::uint8_t> local_static_emitted_;
	std::vector<std::uint32_t> local_static_eager_initializers_;
	std::vector<std::uint32_t> local_static_finalizers_;
	Function* function_;
	BlockId current_block_;
	LowType current_result_;
	bool current_result_reference_, current_indirect_result_;
	std::size_t temp_counter_;
	std::size_t block_counter_;
	std::size_t generated_slot_ordinal_;
	pa18_lowering_detail::PolymorphismLoweringState polymorphism_;
	std::vector<SlotId> binding_slots_;
	std::vector<ParameterId> binding_indirect_parameters_;
	std::vector<SlotId> generated_slots_;
	FlatIdMap temporary_lifetime_slots_;
	std::vector<BlockId> switch_case_blocks_;
	std::vector<std::uint32_t> block_incoming_;
	std::vector<std::uint8_t> bit_field_storage_transfer_owners_;
	FlatIdMap class_value_boundary_types_;
	std::vector<SymbolId> aggregate_helper_symbols_;
	std::vector<std::uint8_t> aggregate_parameter_entities_;
	std::vector<ExceptionControlTarget> break_targets_;
	std::vector<ExceptionControlTarget> continue_targets_;
	std::vector<StatementTask> statement_tasks_;
	FlatIdMap label_blocks_;
	void ResetInitializedBitFieldUnit() {
		initialized_bit_field_unit_valid_ = false;
		initialized_bit_field_owner_ = kNoEntity;
		initialized_bit_field_offset_ = 0; }
	EntityId initialized_bit_field_owner_;
	std::uint64_t initialized_bit_field_offset_;
	bool initialized_bit_field_unit_valid_;
	StringCounterTable used_names_;
	StringCounterTable assigned_names_;
	StringCounterTable slot_name_counts_;
	std::size_t parameter_slot_index_;
	std::size_t source_ordinal_;
	bool needs_global_class_initializer_;
	bool lowering_namespace_object_;
	SymbolId lowering_thread_local_initializer_object_;
	bool current_class_value_boundary_;
	BindingId current_this_binding_;
	EntityId current_member_owner_;
	BlockId destructor_return_target_;
	bool destructor_return_routes_to_epilogue_, full_expression_cleanup_active_;
	BlockId full_expression_cleanup_dispatch_, full_expression_cleanup_end_, full_expression_linked_cleanup_dispatch_;
	bool full_expression_cleanup_dispatch_reused_, full_expression_tracks_lifetime_state_, full_expression_uses_linked_dispatch_, full_expression_uses_branch_cleanup_, full_expression_cleanup_ready_, full_expression_deferred_cleanup_;
	std::size_t full_expression_linked_action_cursor_;
	BlockId runtime_lifetime_cleanup_dispatch_, conditional_cleanup_resume_;
	std::vector<std::uint32_t> full_expression_cleanup_actions_, full_expression_segment_actions_;
	pa17_lowering_detail::CleanupDispatchCache full_expression_cleanup_dispatches_;
	FlatIdMap conditional_cleanup_dispatches_, conditional_cleanup_tails_, runtime_lifetime_temporaries_;
	FlatIdPairMap full_expression_branch_cleanup_heads_,
		full_expression_branch_cleanup_tails_;
	std::vector<std::uint32_t> full_expression_branch_cleanup_next_;
	std::vector<IdentityTypeId> identity_type_cache_;
	pa15_lowering_detail::SourceTypeLowering source_types_;
	pa16_lowering_detail::StaticInitializerLowering static_initializers_;
};
}
namespace pa15_lowering_detail
{
void LowerSemanticGraph(const SemanticGraphView& graph, TypedProgram& program,
	LowIRLoweringStats* stats, std::size_t source_ordinal)
{
	GraphLowerer(graph, program, stats, source_ordinal).Lower();
}
} }
