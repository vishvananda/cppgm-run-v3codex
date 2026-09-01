#include "lowering/api.h"
#include "support/numeric/decimal_spelling.h"
#include "lowering/core/graph_lowering.h"
#include "lowering/control/control_flow.h"
#include "lowering/expressions/conditionals.h"
#include "lowering/abi/emission_policy.h"
#include "lowering/abi/mangling.h"
#include "lowering/abi/symbol_metadata.h"
#include "lowering/presentation/local_names.h"
#include "lowering/abi/symbol_names.h"
#include "lowering/objects/storage_facts.h"
#include "lowering/support/errors.h"
#include "lowering/support/identity_maps.h"
#include "lowering/support/sequences.h"
#include "lowering/expressions/scalar_unary.h"
#include "lowering/expressions/core.h"
#include "lowering/core/source_types.h"
#include "lowering/objects/static_members.h"
#include "semantic/model/program.h"
#include "semantic/semantic.h"
#include "semantic/model/graph.h"
#include "lowering/objects/array_lifetime.h"
#include "lowering/objects/aggregate_lifetime.h"
#include "lowering/expressions/assignment.h"
#include "lowering/calls/arguments.h"
#include "lowering/calls/function_calls.h"
#include "lowering/calls/constructors.h"
#include "lowering/objects/initialization.h"
#include "lowering/objects/lifetime_actions.h"
#include "lowering/expressions/member_address.h"
#include "lowering/objects/static_initialization.h"
#include "lowering/objects/storage_slots.h"
#include "lowering/objects/storage_access.h"
#include "lowering/expressions/bit_fields.h"
#include "lowering/expressions/logical.h"
#include "lowering/calls/value_boundary.h"
#include "lowering/calls/special_members.h"
#include "lowering/objects/temporary_lifetime.h"
#include "lowering/objects/polymorphism.h"
#include "lowering/constants/values.h"
#include "lowering/constants/templates.h"
#include "lowering/objects/local_statics.h"
#include "lowering/extensions/range_for.h"
#include "lowering/control/statements.h"
#include "lowering/extensions/initializer_lists.h"
#include "lowering/objects/rtti.h"
#include "lowering/objects/member_pointers.h"
#include "lowering/objects/virtual_bases.h"
#include "lowering/control/regions.h"
#include "lowering/objects/static_lifetime.h"
#include "lowering/extensions/gnu_asm.h"
#include "lowering/extensions/complex.h"
#include <algorithm>
#include <limits>
#include <ostream>
#include <string>
#include <vector>
namespace cppgm { namespace lowering { namespace {
using namespace semantic; using namespace semantic;
using namespace lowering::ir; using namespace lowering::support;
class ProgramLowerer :
	private lowering::VirtualBaseLowering<ProgramLowerer>,
	private lowering::ControlFlowLowering<ProgramLowerer>,
	private lowering::StatementLowering<ProgramLowerer>,
	private lowering::ConditionalLowering<ProgramLowerer>,
	private lowering::ScalarUnaryLowering<ProgramLowerer>,
	private lowering::ExpressionLowering<ProgramLowerer>,
	private lowering::StaticMemberSymbolLowering<ProgramLowerer>,
	private lowering::PolymorphismActionLowering<ProgramLowerer>,
	private lowering::BitFieldValueLowering<ProgramLowerer>,
	private lowering::ControlExpressionLowering<ProgramLowerer>,
	private lowering::ValueBoundaryLowering<ProgramLowerer>,
	private lowering::SpecialMemberLowering<ProgramLowerer>,
	private lowering::AssignmentLowering<ProgramLowerer>,
	private lowering::AggregateHelperLowering<ProgramLowerer>,
	private lowering::ConstructorActionLowering<ProgramLowerer>,
	private lowering::ArrayLifetimeLowering<ProgramLowerer>,
	private lowering::DestructorActionLowering<ProgramLowerer>,
	private lowering::CallArgumentLowering<ProgramLowerer>,
	private lowering::FunctionCallLowering<ProgramLowerer>,
	private lowering::InitializationLowering<ProgramLowerer>,
	private lowering::LifetimeActionLowering<ProgramLowerer>,
	private lowering::MemberAddressLowering<ProgramLowerer>,
	private lowering::SlotPlanning<ProgramLowerer>,
	private lowering::StorageAccessLowering<ProgramLowerer>,
	private lowering::TemporaryLifetimeLowering<ProgramLowerer>,
	private lowering::ConstantLowering<ProgramLowerer>,
	private lowering::LocalStaticLowering<ProgramLowerer>,
	private lowering::RangeForLowering<ProgramLowerer>,
	private lowering::ExceptionLowering<ProgramLowerer>,
	private lowering::InitializerListLowering<ProgramLowerer>,
	private lowering::RttiLowering<ProgramLowerer>,
	private lowering::MemberPointerLowering<ProgramLowerer>,
	private lowering::RegionLowering<ProgramLowerer>,
	private lowering::StaticLifecycleLowering<ProgramLowerer>,
	private lowering::GnuAsmLowering<ProgramLowerer>,
	private lowering::ComplexLowering<ProgramLowerer>
{
public:
	ProgramLowerer(const SemanticGraphView& graph, lowering::ir::Program& output,
		lowering::Stats* stats, std::size_t source_ordinal)
		: graph_(graph), program_(graph.program), arena_(graph.arena),
		  output_(output), stats_(stats),
		  abi_context_(stats ? &stats->abi : 0),
		  function_(0), current_block_(0), current_result_(LowVoid()),
		  current_result_reference_(false), current_indirect_result_(false),
		  temp_counter_(0), initialized_bit_field_owner_(kNoEntity),
		  initialized_bit_field_offset_(0), initialized_bit_field_unit_valid_(false),
		  source_ordinal_(source_ordinal), needs_global_class_initializer_(false), lowering_namespace_object_(false),
		  lowering_thread_local_initializer_object_(kNoLowId),
		  current_class_value_boundary_(false), current_this_binding_(kNoBinding), current_member_owner_(kNoEntity),
		  destructor_return_target_(kNoLowId),
		  destructor_return_routes_to_epilogue_(false),
		  constructor_body_cleanup_active_(false),
		  full_expression_cleanup_active_(false), full_expression_cleanup_dispatch_(kNoLowId),
		  full_expression_cleanup_end_(kNoLowId), full_expression_linked_cleanup_dispatch_(kNoLowId),
		  full_expression_cleanup_dispatch_reused_(false), full_expression_tracks_lifetime_state_(false),
		  full_expression_uses_linked_dispatch_(false), full_expression_uses_branch_cleanup_(false),
		  full_expression_cleanup_ready_(false), full_expression_deferred_cleanup_(false),
		  full_expression_linked_action_cursor_(0),
		  runtime_lifetime_cleanup_dispatch_(kNoLowId), conditional_cleanup_resume_(kNoLowId),
		  full_expression_cleanup_state_(
			  lowering::cleanup::kNoCleanupState),
		  presentation_names_(program_, stats ? &stats->semantic : 0),
		  source_types_(program_),
		  static_initializers_(program_, arena_, output_, stats_,
			function_symbols_, global_symbols_, literal_symbols_,
			function_definition_, polymorphism_.class_vtable_symbols),
		  constant_templates_(output_, stats_)
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
				ThrowLoweringInternal("invalid namespace object identity");
			const BindingId canonical = program_.bindings[action.object].canonical;
			if (canonical >= namespace_action_.size())
				ThrowLoweringInternal(
					"invalid canonical namespace object identity");
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
				ThrowLoweringInternal("invalid local static object identity");
			if (local_static_action_[action.object] != kNoDumpEdge)
				ThrowLoweringInternal("duplicate local static object action");
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
		std::vector<std::uint32_t> callee_nodes_by_symbol(output_.symbols.size(),
			kNoDumpEdge);
		for (std::size_t node = 0; node < arena_.nodes.size(); ++node)
			if (arena_.nodes[node].kind == DUMP_CALLEE && arena_.nodes[node].binding != kNoBinding &&
				function_symbols_[arena_.nodes[node].binding] == kNoLowId)
				RegisterFunction(node);
		for (std::size_t node = 0; node < arena_.nodes.size(); ++node)
			if (arena_.nodes[node].kind == DUMP_CALLEE &&
				arena_.nodes[node].binding != kNoBinding)
			{
				const SymbolId symbol =
					function_symbols_[arena_.nodes[node].binding];
				if (callee_nodes_by_symbol.size() <= symbol)
					callee_nodes_by_symbol.resize(
						static_cast<std::size_t>(symbol) + 1, kNoDumpEdge);
				if (callee_nodes_by_symbol[symbol] == kNoDumpEdge)
					callee_nodes_by_symbol[symbol] =
						static_cast<std::uint32_t>(node);
			}
		RegisterLocalStaticObjects();
		lowering::PreparePolymorphism(graph_, output_, stats_,
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
		OrderLifecycleBaseEntries();
		lowering::EmitDeletingDestructors(graph_, output_, stats_,
			function_symbols_, &polymorphism_);
		lowering::EmitVtableThunks(graph_, output_, stats_,
			function_symbols_, &polymorphism_);
		EmitAggregateHelpers();
		if (!output_.host_object_emission)
			EmitThreadLocalInitializers();
		EmitDynamicInitializer();
		EmitDynamicFinalizer();
		for (std::size_t symbol = 0;
			symbol < callee_nodes_by_symbol.size(); ++symbol)
			if (callee_nodes_by_symbol[symbol] != kNoDumpEdge &&
				output_.symbols[symbol].referenced &&
				!output_.symbols[symbol].declaration_emitted &&
				!output_.symbols[symbol].definition_emitted)
			{
				output_.declarations.push_back(
					LowerDeclaration(callee_nodes_by_symbol[symbol]));
				output_.symbols[symbol].declaration_emitted = true;
			}
	}
private:
	friend class lowering::VirtualBaseLowering<ProgramLowerer>;
	friend class lowering::ConditionalLowering<ProgramLowerer>;
	friend class lowering::VirtualBaseBoundaryShape<ProgramLowerer>;
	friend class lowering::VirtualBaseContractLookup<ProgramLowerer>;
	friend class lowering::ControlFlowLowering<ProgramLowerer>;
	friend class lowering::StatementLowering<ProgramLowerer>;
	friend class lowering::ScalarUnaryLowering<ProgramLowerer>;
	friend class lowering::ExpressionLowering<ProgramLowerer>;
	friend class lowering::StaticMemberSymbolLowering<ProgramLowerer>;
	friend class lowering::PolymorphismActionLowering<ProgramLowerer>;
	friend class lowering::BitFieldValueLowering<ProgramLowerer>;
	friend class lowering::ControlExpressionLowering<ProgramLowerer>;
	friend class lowering::ValueBoundaryLowering<ProgramLowerer>;
	friend class lowering::SpecialMemberLowering<ProgramLowerer>;
	friend class lowering::AssignmentLowering<ProgramLowerer>;
	friend class lowering::AggregateHelperLowering<ProgramLowerer>;
	friend class lowering::ConstructorActionLowering<ProgramLowerer>;
	friend class lowering::ArrayLifetimeLowering<ProgramLowerer>;
	friend class lowering::DestructorActionLowering<ProgramLowerer>;
	friend class lowering::CallArgumentLowering<ProgramLowerer>;
	friend class lowering::FunctionCallLowering<ProgramLowerer>;
	friend class lowering::InitializationLowering<ProgramLowerer>;
	friend class lowering::LifetimeActionLowering<ProgramLowerer>;
	friend class lowering::MemberAddressLowering<ProgramLowerer>;
	friend class lowering::SlotPlanning<ProgramLowerer>;
	friend class lowering::StorageAccessLowering<ProgramLowerer>;
	friend class lowering::TemporaryLifetimeLowering<ProgramLowerer>;
	friend class lowering::ConstantLowering<ProgramLowerer>;
	friend class lowering::LocalStaticLowering<ProgramLowerer>;
	friend class lowering::RangeForLowering<ProgramLowerer>;
	friend class lowering::ExceptionLowering<ProgramLowerer>;
	friend class lowering::InitializerListLowering<ProgramLowerer>;
	friend class lowering::RttiLowering<ProgramLowerer>;
	friend class lowering::MemberFunctionPointerLowering<ProgramLowerer>;
	friend class lowering::MemberPointerLowering<ProgramLowerer>;
	friend class lowering::RegionLowering<ProgramLowerer>;
	friend class lowering::StaticLifecycleLowering<ProgramLowerer>;
	friend class lowering::GnuAsmLowering<ProgramLowerer>;
	friend class lowering::ComplexLowering<ProgramLowerer>;
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
	struct LifecycleBaseEntry
	{
		SymbolIdentity complete_identity;
		SymbolId base_symbol;

		LifecycleBaseEntry(const SymbolIdentity& identity, SymbolId symbol)
			: complete_identity(identity), base_symbol(symbol)
		{
			complete_identity.lifecycle_role = 0;
		}
	};
	void RecordLifecycleBaseEntry(const SymbolIdentity& identity,
		SymbolId symbol)
	{
		if (identity.lifecycle_role != 0)
			lifecycle_base_entries_.push_back(
				LifecycleBaseEntry(identity, symbol));
	}
	void OrderLifecycleBaseEntries()
	{
		if (lifecycle_base_entries_.empty()) return;
		const std::size_t first = polymorphism_.source_function_first;
		if (first >= output_.functions.size()) return;
		const std::size_t missing = std::numeric_limits<std::size_t>::max();
		std::vector<std::size_t> function_by_symbol(
			output_.symbols.size(), missing);
		for (std::size_t i = first; i < output_.functions.size(); ++i)
			function_by_symbol[output_.functions[i].symbol] = i;
		std::vector<std::size_t> base_by_complete(
			output_.symbols.size(), missing);
		bool needs_ordering = false;
		for (std::size_t i = 0; i < lifecycle_base_entries_.size(); ++i)
		{
			SymbolId complete(kNoLowId);
			if (!output_.symbol_index.Find(
				lifecycle_base_entries_[i].complete_identity, &complete)) continue;
			const SymbolId base = lifecycle_base_entries_[i].base_symbol;
			if (complete >= function_by_symbol.size() ||
				base >= function_by_symbol.size()) continue;
			const std::size_t base_function = function_by_symbol[base];
			const std::size_t complete_function = function_by_symbol[complete];
			if (base_function == missing || complete_function == missing) continue;
			base_by_complete[complete] = base_function;
			needs_ordering |= base_function > complete_function;
		}
		if (!needs_ordering) return;
		std::vector<unsigned char> moved(output_.functions.size(), 0);
		std::vector<Function> ordered;
		ordered.reserve(output_.functions.size() - first);
		for (std::size_t i = first; i < output_.functions.size(); ++i)
		{
			if (moved[i]) continue;
			const SymbolId symbol = output_.functions[i].symbol;
			const std::size_t base = symbol < base_by_complete.size() ?
				base_by_complete[symbol] : missing;
			if (base != missing && base > i && !moved[base])
			{
				ordered.push_back(std::move(output_.functions[base]));
				moved[base] = 1;
			}
			ordered.push_back(std::move(output_.functions[i]));
		}
		for (std::size_t i = 0; i < ordered.size(); ++i)
			output_.functions[first + i] = std::move(ordered[i]);
	}
	SymbolId InternSymbol(const DumpNode& node, Symbol::Kind kind,
		const std::string& proposed_name, const std::string& object_name)
	{
		const lowir_model::StringId object_name_id = object_name.empty() ?
			lowir_model::StringId() : output_.strings.intern(object_name);
		const BindingRecord& binding = program_.bindings[node.binding];
		const BindingRecord& canonical_binding = program_.bindings[binding.canonical];
		const bool class_template_member = binding.member_owner != kNoEntity &&
			program_.entities[binding.member_owner].template_argument_begin != kNoBinding;
		const bool lambda_member = binding.member_owner != kNoEntity &&
			program_.entities[binding.member_owner].lambda_closure;
		EntityId lambda_identity_owner = lambda_member ?
			binding.member_owner : binding.lambda_invocation ?
				binding.lambda_invocation_owner : kNoEntity;
		for (ScopeId scope = binding.owner;
			lambda_identity_owner == kNoEntity && scope != kNoScope;
			scope = program_.ParentScope(scope))
		{
			const EntityId entity = program_.EntityForScope(scope);
			if (entity != kNoEntity && entity < program_.entities.size() &&
				program_.entities[entity].lambda_closure)
				lambda_identity_owner = entity;
		}
		const bool weak_linkage = lowering::abi::HasWeakLinkage(
			program_, node.binding, kind == Symbol::FUNCTION_SYMBOL);
		const bool local_member = lowering::IsFunctionLocalEntity(
			program_, binding.member_owner);
		const bool prefer_local =
			lowering::PreferLocalObjectBinding(
				program_, binding.member_owner);
		const bool internal = binding.unnamed_namespace_linkage ||
			canonical_binding.unnamed_namespace_linkage ||
			(binding.storage_class == STORAGE_CLASS_STATIC &&
			 binding.member_owner == kNoEntity);
		const bool c_linkage = binding.language_linkage == LANGUAGE_LINKAGE_C;
		SymbolIdentity identity;
		identity.kind = kind;
		identity.path = class_template_member || lambda_member ?
			output_.identities.InternClassMemberPath(
				program_, binding.member_owner, binding.name) :
			binding.lambda_invocation ? output_.identities.InternEntityPath(
				program_, binding.lambda_invocation_owner) :
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
				binding.member_owner], identity_type_cache_) :
			lambda_identity_owner != kNoEntity &&
			program_.entities[lambda_identity_owner].local_context != kNoBinding ?
				output_.identities.InternLambdaContextIdentity(program_,
					lambda_identity_owner, identity_type_cache_) : kNoLowId;
		identity.lifecycle_role = binding.constructor_base_entry ||
			binding.destructor_base_entry ? 1 : 0;
		identity.internal_owner = local_member ?
			((source_ordinal_ + 1) << 32) |
				(static_cast<std::size_t>(binding.member_owner) + 1) :
			internal ? source_ordinal_ + 1 : 0;
		const TypeId identity_source_type = binding.kind == BIND_VARIABLE ?
			canonical_binding.type : node.type;
		const IdentityTypeId source_type = output_.identities.InternType(
			program_, identity_source_type, identity_type_cache_);
		SymbolId found = kNoLowId;
		if (output_.symbol_index.Find(identity, &found))
		{
			Symbol& symbol = output_.symbols[found];
			if (symbol.source_type != source_type)
				ThrowLoweringSource(
					"conflicting cross-source PA15 symbol type for " +
					proposed_name + " (existing symbol " +
					output_.strings.get(symbol.name) + ")");
			if (symbol.object_name.valid() && object_name_id.valid() &&
				symbol.object_name != object_name_id)
				ThrowLoweringInternal(
					"conflicting PA15 ABI object identity for " +
					proposed_name + ": " +
					output_.strings.get(symbol.object_name) + " versus " +
					object_name);
			symbol.nonthrowing = symbol.nonthrowing || binding.nonthrowing;
			symbol.noreturn = symbol.noreturn || binding.noreturn_function ||
				canonical_binding.noreturn_function;
			symbol.weak_linkage |=
				weak_linkage && !prefer_local && !symbol.internal_linkage;
			symbol.prefer_local_object_binding |= prefer_local;
			if (!symbol.section_name.valid() &&
				canonical_binding.object_section_name != 0)
				symbol.section_name = output_.strings.intern(
					program_.names.Get(canonical_binding.object_section_name));
			symbol.object_output_root |= binding.object_output_root;
			symbol.demand_reason_mask |= canonical_binding.demand_reason_mask;
			symbol.force_inline |= binding.force_inline || canonical_binding.force_inline;
			symbol.inline_hint |= binding.inline_function ||
				canonical_binding.inline_function;
			symbol.no_inline |= binding.no_inline || canonical_binding.no_inline;
			symbol.stable_prefix_query |= binding.stable_prefix_query ||
				canonical_binding.stable_prefix_query;
			lowering::abi::ApplyBuiltinSymbolMetadata(
				&symbol, binding.builtin_function,
				binding.hosted_memory_intrinsic);
			lowering::abi::ApplyNativeRuntimeSymbolMetadata(output_, &symbol);
			const FunctionMemoryEffects effects =
				std::max(binding.function_effects,
					canonical_binding.function_effects);
			if (effects == FUNCTION_EFFECTS_READNONE)
				symbol.effects = Symbol::EFFECTS_READNONE;
			else if (effects == FUNCTION_EFFECTS_READONLY &&
				symbol.effects != Symbol::EFFECTS_READNONE)
				symbol.effects = Symbol::EFFECTS_READONLY;
			RecordLifecycleBaseEntry(identity, found);
			return found;
		}
		if (output_.symbols.size() >= kNoLowId)
			ThrowLoweringResourceLimit("too many PA15 emission symbols");
		const SymbolId symbol = static_cast<SymbolId>(output_.symbols.size());
		output_.symbols.push_back(Symbol(kind,
			output_.InternUniqueSymbolName(proposed_name),
			object_name_id, c_linkage,
			internal, binding.nonthrowing));
		output_.symbols.back().noreturn = binding.noreturn_function ||
			canonical_binding.noreturn_function;
		lowering::abi::ApplyBuiltinSymbolMetadata(&output_.symbols.back(),
			binding.builtin_function, binding.hosted_memory_intrinsic);
		lowering::abi::ApplyNativeRuntimeSymbolMetadata(
			output_, &output_.symbols.back());
		output_.symbols.back().source_type = source_type;
		output_.symbols.back().weak_linkage =
			weak_linkage && !prefer_local && !internal;
		output_.symbols.back().prefer_local_object_binding = prefer_local;
		if (canonical_binding.object_section_name != 0)
			output_.symbols.back().section_name = output_.strings.intern(
				program_.names.Get(canonical_binding.object_section_name));
		output_.symbols.back().object_output_root = binding.object_output_root;
		output_.symbols.back().demand_reason_mask =
			canonical_binding.demand_reason_mask;
		output_.symbols.back().force_inline = binding.force_inline || canonical_binding.force_inline;
		output_.symbols.back().inline_hint = binding.inline_function ||
			canonical_binding.inline_function;
		output_.symbols.back().no_inline = binding.no_inline || canonical_binding.no_inline;
		output_.symbols.back().stable_prefix_query = binding.stable_prefix_query ||
			canonical_binding.stable_prefix_query;
		const FunctionMemoryEffects effects =
			std::max(binding.function_effects,
				canonical_binding.function_effects);
		if (effects == FUNCTION_EFFECTS_READNONE)
			output_.symbols.back().effects = Symbol::EFFECTS_READNONE;
		else if (effects == FUNCTION_EFFECTS_READONLY)
			output_.symbols.back().effects = Symbol::EFFECTS_READONLY;
		output_.symbol_index.Insert(identity, symbol);
		RecordLifecycleBaseEntry(identity, symbol);
		return symbol;
	}
	void RegisterFunction(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.binding == kNoBinding) return;
		if (function_symbols_[record.binding] == kNoLowId)
		{
			const BindingRecord& binding = program_.bindings[record.binding];
			const std::string base = abi::NormalizeSymbolName(
				presentation_names_.Apply(binding));
			const std::uint32_t ordinal =
				program_.bindings[record.binding].overload_ordinal;
			const std::string name = ordinal <= 1 ? base :
				base + "__ov" + std::to_string(ordinal);
			const std::string entry_name = binding.constructor_base_entry ?
				name + "__base_entry" : binding.destructor_base_entry ?
				name + "__base_entry" : name;
			function_symbols_[record.binding] = InternSymbol(record, Symbol::FUNCTION_SYMBOL, entry_name,
				lowering::abi::MangleFunction(program_, record, false,
					stats_ ? &stats_->abi : 0, &abi_context_));
			lowering::abi::ApplyLifecycleSymbolMetadata(program_, record,
				&output_, function_symbols_[record.binding], &abi_context_,
				stats_ ? &stats_->abi : 0);
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
				if (lowering::abi::IsFunctionEmissionDemanded(
					program_, record, output_.host_object_emission))
					RegisterFunction(current);
				continue;
			}
			if (record.kind == DUMP_VARIABLE && record.binding != kNoBinding)
			{
				const BindingId canonical =
					program_.bindings[record.binding].canonical;
				(void)RegisterGlobalVariable(record);
				const bool declaration_only = lowering::abi::IsVariableDeclarationOnly(
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
				if (!lowering::abi::IsFunctionEmissionDemanded(
					program_, record, output_.host_object_emission)) continue;
				if (record.binding != kNoBinding &&
					function_definition_[record.binding] == kNoDumpEdge &&
					function_declaration_[record.binding] == current)
				{
					const SymbolId symbol = function_symbols_[record.binding];
					if (!output_.symbols[symbol].declaration_emitted && lowering::abi::IsFunctionDeclarationBoundaryComplete(program_, record))
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
				if (!lowering::abi::IsFunctionEmissionDemanded(
					program_, record, output_.host_object_emission)) continue;
				if (record.binding != kNoBinding &&
					function_definition_[record.binding] == current)
				{
						const SymbolId symbol = function_symbols_[record.binding];
						if (output_.symbols[symbol].definition_emitted)
							ThrowLoweringSource(
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
						const bool declaration_only = lowering::abi::IsVariableDeclarationOnly(
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
											lowering::abi::MangleThreadLocalWrapper(
												program_, record.binding, record.text,
												stats_ ? &stats_->abi : 0,
												&abi_context_)));
								}
						}
						else
						{
								const SymbolId symbol = global_symbols_[canonical];
								if (output_.symbols[symbol].definition_emitted)
									ThrowLoweringSource(
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
			&declaration.variadic, true);
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
			!type.IsIncompleteArray();
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
			ThrowLoweringInternal(
				"global definition has no namespace action fact");
		const NamespaceObjectAction& action =
			graph_.namespace_objects[action_index];
		RegisterNamespaceInitializerListBacking(action);
		const bool thread_local_object =
			program_.bindings[action.object].thread_local_storage;
		output_.symbols[global.symbol].thread_local_storage = thread_local_object;
		if (thread_local_object)
			thread_local_objects_.push_back(
				std::make_pair(action_index,
					lowering::abi::MangleThreadLocalWrapper(
						program_, record.binding, record.text,
						stats_ ? &stats_->abi : 0, &abi_context_)));
		bool keep_global_class_address = false;
		bool dynamic_initializer = false;
		if (!SetExplicitVariableZero(record, &global) &&
			!static_initializers_.Lower(action, thread_local_object, &global,
			&needs_global_class_initializer_, &keep_global_class_address))
		{
			static_initializers_.SetZero(action.type, &global);
			dynamic_initializer = true;
			if (thread_local_object)
				thread_local_dynamic_[action_index] = 1;
			else namespace_initializers_.push_back(std::make_pair(action_index, true));
		}
		else if (keep_global_class_address)
		{
			dynamic_initializer = true;
			namespace_initializers_.push_back(
				std::make_pair(action_index, false));
		}
		if (action.destructor != kNoDumpEdge &&
			!program_.bindings[action.object].thread_local_storage)
			dynamic_finalizers_.push_back(action_index);
		// A statically initialized const scalar can never be written, so its
		// storage is readonly and later passes may fold or deduplicate its
		// loads.  Class objects stay default: mutable members and lifecycle
		// actions can write through a const complete object.
		const TypeRecord& qualified = program_.types.Get(record.type);
		if (!dynamic_initializer && !thread_local_object &&
			action.destructor == kNoDumpEdge &&
			(qualified.cv & CV_CONST) != 0 &&
			(qualified.cv & (CV_VOLATILE | CV_ATOMIC)) == 0 &&
			global.type.kind != LOW_OBJECT)
			global.storage = Global::STORAGE_READONLY;
		if (stats_) ++stats_->globals;
		return global;
	}
	SymbolId AddSyntheticSymbol(Symbol::Kind kind, const std::string& proposed,
		const std::string& object_name, bool internal)
	{
		const SymbolId symbol = static_cast<SymbolId>(output_.symbols.size());
		output_.symbols.push_back(Symbol(kind,
			output_.InternUniqueSymbolName(proposed),
			object_name.empty() ? lowir_model::StringId() :
				output_.strings.intern(object_name),
			false, internal, false));
		return symbol;
	}
	void ResetCommonFunctionLoweringState(Function* function)
	{
		function_ = function;
		temp_counter_ = 0;
		ResetFunctionSlots(); ResetControlFlowReachability();
		ResetFullExpressionFunctionState();
		ResetExceptionFunctionState(); ResetInitializerListFunctionState();
		break_targets_.clear();
		continue_targets_.clear();
		label_blocks_.Clear();
		ResetInitializedBitFieldUnit();
		local_presentation_.Reset(output_.retain_local_names,
			stats_ ? &stats_->local_presentation : 0);
		current_this_binding_ = kNoBinding;
		current_member_owner_ = kNoEntity;
	}
	void BeginSyntheticFunction(Function* function)
	{
		ResetCommonFunctionLoweringState(function);
		current_result_ = LowVoid();
		current_result_reference_ = false;
		current_indirect_result_ = false;
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
		return local_presentation_.UniqueSlotName(requested);
	}

	std::string GeneratedSlotName(const std::string& prefix)
	{
		return local_presentation_.GeneratedSlotName(prefix);
	}

	SlotId EnsureGeneratedSlot(std::uint32_t node, const std::string& prefix,
		const LowType& type)
	{
		if (generated_slots_[node] != kNoLowId)
			return generated_slots_[node];
		generated_slots_[node] = CreateGeneratedSlot(prefix, type); generated_slot_nodes_.push_back(node);
		return generated_slots_[node];
	}

	SlotId CreateGeneratedSlot(const std::string& prefix, const LowType& type)
	{
		if (function_->slots.size() >= kNoLowId)
			ThrowLoweringResourceLimit("too many PA15 LowIR slots");
		const SlotId result = static_cast<SlotId>(function_->slots.size());
		Slot slot;
		slot.name = InternLocalName(output_, GeneratedSlotName(prefix));
		slot.type = type;
		function_->slots.push_back(slot);
		return result;
	}

	void CollectSourceNames(std::uint32_t node)
	{
		local_presentation_.CollectSourceNames(program_, arena_, node,
			&function_->generated_name_reservations);
	}

	Function LowerFunction(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		Function result;
		result.symbol = function_symbols_[record.binding];
		const semantic::BindingRecord& entry_binding =
			program_.bindings[program_.bindings[record.binding].canonical];
		result.entry = entry_binding.owner == program_.GlobalScope() &&
			program_.names.Get(entry_binding.name) == "main";
		FillBoundary(node, &result.parameters, &result.result, &result.variadic);
		ResetCommonFunctionLoweringState(&result);
		current_result_ = result.result;
		current_class_value_boundary_ = FunctionHasClassValueBoundary(record.type);
		const TypeRecord& source_function = program_.types.Get(record.type); current_indirect_result_ = UsesIndirectClassResult(source_function.child, record.binding);
		current_result_reference_ = IsReferenceType(source_function.child);
		ResetLifetimeFunctionState();
		parameter_slot_index_ = current_indirect_result_ ? 1 : 0;
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
			else ThrowLoweringSource("non-void function has no return");
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
	BlockPresentationName NewLabel(const std::string& prefix)
	{
		return local_presentation_.GeneratedBlockName(output_, prefix);
	}

	TempId NewTemp()
	{
		while (true)
		{
			if (temp_counter_ + 1 >= kNoLowId)
				ThrowLoweringResourceLimit("too many PA15 LowIR temporaries");
			const TempId candidate = static_cast<TempId>(++temp_counter_);
			if (!local_presentation_.ReservesTemporary(candidate))
			{
				function_->temporary_limit =
					static_cast<std::uint32_t>(candidate) + 1;
				return candidate;
			}
		}
	}

	Operand Temp(const LowType& type)
	{
		return Operand(NewTemp(), type);
	}

	void Emit(const Instruction& instruction)
	{
		if (CurrentBlock().terminated)
			ThrowLoweringInternal("PA15 attempted to emit after a terminator");
		CurrentBlock().instructions.push_back(instruction);
		if (IsTerminator(instruction)) CurrentBlock().terminated = true;
		if (stats_) ++stats_->instructions;
	}

	const SemanticGraphView& graph_;
	const semantic::Program& program_;
	const DumpArena& arena_;
	lowering::ir::Program& output_;
	lowering::Stats* stats_;
	abi_mangle::AbiMangleContext abi_context_;
	std::vector<SymbolId> function_symbols_;
	std::vector<LifecycleBaseEntry> lifecycle_base_entries_;
	std::vector<SymbolId> global_symbols_;
	std::vector<SymbolId> literal_symbols_;
	std::vector<std::uint8_t> temporary_initialized_;
	std::vector<Operand> temporary_addresses_;
	lowering::InitializerListLoweringState initializer_lists_;
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
	lowering::VirtualBaseContractState virtual_base_contracts_;
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
	lowering::PolymorphismLoweringState polymorphism_;
	std::vector<SlotId> binding_slots_;
	std::vector<ParameterId> binding_indirect_parameters_; std::vector<BindingId> function_slot_bindings_;
	std::vector<SlotId> generated_slots_; std::vector<std::uint32_t> generated_slot_nodes_;
	lowering::presentation::LocalPresentationState local_presentation_;
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
	EntityId initialized_bit_field_owner_;
	std::uint64_t initialized_bit_field_offset_;
	bool initialized_bit_field_unit_valid_;
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
	bool constructor_body_cleanup_active_;
	BlockId full_expression_cleanup_dispatch_, full_expression_cleanup_end_, full_expression_linked_cleanup_dispatch_;
	bool full_expression_cleanup_dispatch_reused_, full_expression_tracks_lifetime_state_, full_expression_uses_linked_dispatch_, full_expression_uses_branch_cleanup_, full_expression_cleanup_ready_, full_expression_deferred_cleanup_;
	std::size_t full_expression_linked_action_cursor_;
	BlockId runtime_lifetime_cleanup_dispatch_, conditional_cleanup_resume_;
	std::vector<std::uint32_t> full_expression_cleanup_actions_, full_expression_segment_actions_;
	lowering::cleanup::Interner cleanup_continuations_;
	std::uint32_t full_expression_cleanup_state_;
	std::vector<std::uint32_t> pending_cleanup_states_;
	FlatIdMap runtime_lifetime_temporaries_;
	FlatIdPairMap full_expression_branch_cleanup_heads_,
		full_expression_branch_cleanup_tails_;
	std::vector<std::uint32_t> full_expression_branch_cleanup_next_;
	std::vector<IdentityTypeId> identity_type_cache_;
	lowering::presentation::EmissionNameMap presentation_names_;
	lowering::SourceTypeLowering source_types_;
	lowering::StaticInitializerLowering static_initializers_;
	lowering::constant_pool::Pool constant_templates_;
};
}
void LowerGraph(const SemanticGraphView& graph, lowering::ir::Program& program,
	lowering::Stats* stats, std::size_t source_ordinal)
{
	ProgramLowerer(graph, program, stats, source_ordinal).Lower();
}
} }
