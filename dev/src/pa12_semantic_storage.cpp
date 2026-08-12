#include "pa12_semantic_detail.h"

namespace cppgm
{
namespace pa12_semantic_detail
{

std::size_t SemanticAnalyzer::SideStorageBytes() const
{
	std::size_t bytes =
		string_literal_units_.capacity() * sizeof(std::uint32_t) +
		scope_prefixes_.capacity() * sizeof(NameId) +
		scope_prefix_segments_.capacity() * sizeof(NameId) +
		scope_parents_.capacity() * sizeof(ScopeId) +
		scope_prefix_scratch_.capacity() * sizeof(NameId) +
		function_sets_.StorageBytes() +
		ordinary_function_sets_.StorageBytes() +
		ordinary_nontemplate_function_sets_.StorageBytes() +
		enum_operator_candidates_.StorageBytes() +
		hidden_friend_sets_.StorageBytes() +
		hidden_friend_template_sets_.StorageBytes() +
		friend_class_grants_.StorageBytes() +
		friend_function_grants_.StorageBytes() +
		function_declarations_.StorageBytes() +
		using_function_declarations_.StorageBytes() +
		function_template_specialization_declarations_.StorageBytes() +
		member_ref_qualifier_shapes_.StorageBytes() +
		function_fact_by_binding_.capacity() * sizeof(std::uint32_t) +
		functions_.capacity() * sizeof(FunctionInfo) +
		variable_node_by_binding_.capacity() * sizeof(std::uint32_t) +
		builtin_functions_.capacity() * sizeof(BindingId) +
		entity_data_members_.capacity() * sizeof(std::vector<BindingId>) +
		entity_layout_members_.capacity() *
			sizeof(std::vector<ClassLayoutMember>) +
		zero_offset_subobject_marks_.capacity() * sizeof(std::uint32_t) +
		zero_offset_subobject_scratch_.capacity() * sizeof(EntityId) +
		entity_constructors_.capacity() * sizeof(std::vector<BindingId>) +
		entity_conversion_functions_.capacity() *
			sizeof(std::vector<BindingId>) +
		entity_conversion_function_templates_.capacity() *
			sizeof(std::vector<std::size_t>) +
		entity_member_functions_.capacity() *
			sizeof(std::vector<BindingId>) +
		class_polymorphism_.capacity() * sizeof(ClassPolymorphismFacts) +
		virtual_slot_by_binding_.capacity() * sizeof(std::uint32_t) +
		class_special_members_.capacity() * sizeof(ClassSpecialMemberFacts) +
		implicit_constructor_by_entity_.capacity() * sizeof(BindingId) +
		constructor_base_entry_by_binding_.capacity() * sizeof(BindingId) +
		destructor_base_entry_by_binding_.capacity() * sizeof(BindingId) +
		static_member_storage_by_binding_.capacity() * sizeof(std::uint32_t) +
		explicit_static_member_specialization_states_.capacity() *
			sizeof(std::uint8_t) +
		static_constant_initializers_by_binding_.capacity() *
			sizeof(StaticConstantInitializerFact) +
		static_constant_dependency_owner_marks_.capacity() *
			sizeof(BindingId) +
		entity_destructor_by_entity_.capacity() * sizeof(BindingId) +
		hidden_friend_anchor_by_entity_.capacity() * sizeof(BindingId) +
		member_initializer_by_binding_.capacity() * sizeof(NodeId) +
		constructor_initializer_scratch_.capacity() * sizeof(NodeId) +
		constructor_initializer_touched_.capacity() * sizeof(BindingId) +
		function_templates_.size() * sizeof(FunctionTemplatePattern) +
		function_template_shape_parameters_.capacity() * sizeof(TypeId) +
		dependent_template_argument_shapes_.capacity() * sizeof(TypeId) +
		template_function_sets_.StorageBytes() +
		function_template_using_fact_sets_.StorageBytes() +
		function_template_using_facts_.capacity() *
			sizeof(FunctionTemplateUsingFact) +
		template_argument_pack_bindings_.StorageBytes() +
		template_argument_pack_values_.capacity() * sizeof(TemplateArgument) +
		function_parameter_pack_bindings_.StorageBytes() +
		retained_call_function_sets_.StorageBytes() +
		retained_call_template_sets_.StorageBytes() +
		retained_call_lookup_states_.capacity() * sizeof(std::uint8_t) +
		retained_call_naming_classes_.capacity() * sizeof(EntityId) +
		template_argument_partitions_.StorageBytes() +
		function_template_result_identities_.StorageBytes() +
		template_instantiations_.StorageBytes() +
		function_template_default_requests_.StorageBytes() +
		lambda_closure_index_.StorageBytes() +
		lambda_capture_uses_.StorageBytes() +
		lambda_closures_.capacity() * sizeof(LambdaClosureFact) +
		lambda_captures_.capacity() * sizeof(LambdaCaptureFact) +
		lambda_count_by_function_.capacity() * sizeof(std::uint32_t) +
		lambda_count_by_namespace_.capacity() * sizeof(std::uint32_t) +
		range_for_hidden_count_by_function_.capacity() * sizeof(std::uint32_t) +
		class_templates_.size() * sizeof(ClassTemplatePattern) +
		demanded_static_member_definitions_.StorageBytes() +
		alias_templates_.capacity() * sizeof(AliasTemplatePattern) +
		alias_template_pattern_by_entity_.capacity() * sizeof(std::uint32_t) +
		alias_template_instantiations_.StorageBytes() +
		alias_template_instantiation_states_.capacity() * sizeof(std::uint8_t) +
		variable_templates_.capacity() * sizeof(VariableTemplatePattern) +
		variable_template_sets_.StorageBytes() +
		variable_template_bindings_.capacity() * sizeof(std::uint8_t) +
		class_template_pattern_by_entity_.capacity() * sizeof(std::uint32_t) +
		class_template_instantiations_.StorageBytes() +
		class_template_specialization_states_.capacity() * sizeof(std::uint8_t) +
		class_template_specialization_use_states_.capacity() *
			sizeof(std::uint8_t) +
		class_template_partial_selections_.capacity() *
			sizeof(ClassTemplatePartialSelection) +
		class_template_explicit_instantiation_states_.capacity() *
			sizeof(std::uint8_t) +
		class_template_explicit_specialization_states_.capacity() *
			sizeof(std::uint8_t) +
		function_explicit_instantiation_states_.capacity() *
			sizeof(std::uint8_t) +
		function_explicit_specialization_states_.capacity() *
			sizeof(std::uint8_t) +
		class_template_member_definition_counts_.capacity() *
			sizeof(std::uint32_t) +
		class_template_demanded_member_definition_counts_.capacity() *
			sizeof(std::uint32_t) +
		class_template_member_definition_demand_states_.capacity() *
			sizeof(std::uint8_t) +
		demanded_class_template_member_definitions_.capacity() *
			sizeof(BindingId) +
		deferred_class_definition_by_entity_.capacity() * sizeof(NodeId) +
		deferred_class_scope_by_entity_.capacity() * sizeof(ScopeId) +
		injected_fact_by_binding_.capacity() * sizeof(std::uint32_t) +
		injected_members_.capacity() * sizeof(InjectedMemberInfo) +
		scope_lifetimes_.capacity() *
			sizeof(std::vector<LifetimeObligation>) +
		nearest_lifetime_scopes_.capacity() * sizeof(ScopeId) +
		nearest_initializer_list_lifetime_scopes_.capacity() * sizeof(ScopeId) +
		scope_nontrivial_object_lifetime_prefixes_.capacity() *
			sizeof(std::uint32_t) +
		namespace_objects_.capacity() * sizeof(NamespaceObjectAction) +
		local_static_objects_.capacity() * sizeof(LocalStaticObjectAction) +
		local_static_count_by_function_.capacity() * sizeof(std::uint32_t) +
		aggregate_helpers_.capacity() * sizeof(AggregateHelperInfo) +
		aggregate_helper_index_.StorageBytes() +
		widest_aggregate_helper_by_entity_.capacity() * sizeof(std::uint32_t) +
		break_cleanup_stops_.capacity() * sizeof(ScopeId) +
		continue_cleanup_stops_.capacity() * sizeof(ScopeId) +
		exception_cleanup_stops_.capacity() * sizeof(ScopeId) +
		exception_handler_cleanup_stops_.capacity() * sizeof(ScopeId) +
		demanded_default_constructor_entities_.capacity() * sizeof(EntityId) +
		default_constructor_demand_states_.capacity() * sizeof(std::uint8_t) +
		demanded_functions_.capacity() * sizeof(BindingId) +
		constexpr_frames_.capacity() * sizeof(ConstexprFrame) +
		constexpr_locals_.capacity() * sizeof(ConstexprLocalValue) +
		constexpr_local_by_name_.capacity() * sizeof(std::size_t) +
		constexpr_local_by_pack_.capacity() * sizeof(std::size_t) +
		constexpr_scope_facts_.capacity() * sizeof(ConstexprScopeFact) +
		constexpr_type_alias_by_name_.capacity() * sizeof(std::size_t) +
		constexpr_block_offsets_.capacity() * sizeof(ConstexprBlockOffset) +
		floating_constant_fact_by_binding_.capacity() * sizeof(std::uint32_t) +
		floating_constant_values_.capacity() * sizeof(long double) +
		constexpr_member_pointer_by_binding_.capacity() * sizeof(BindingId) +
		constexpr_object_by_binding_.capacity() * sizeof(std::uint32_t) +
		constexpr_address_by_binding_.capacity() * sizeof(std::uint32_t) +
		access_base_path_scratch_.capacity() * sizeof(std::uint32_t) +
		protected_object_unprivileged_marks_.capacity() *
			sizeof(std::uint32_t) +
		protected_object_privileged_marks_.capacity() *
			sizeof(std::uint32_t) +
		protected_object_path_scratch_.capacity() *
			sizeof(std::pair<EntityId, bool>) +
		constexpr_addresses_.capacity() * sizeof(ConstexprAddressValue) +
		constexpr_address_index_.bucket_count() * sizeof(void*) +
		constexpr_address_index_.size() *
			(sizeof(ConstexprAddressValue) + sizeof(std::uint32_t)) +
		constexpr_objects_.capacity() * sizeof(ConstexprObjectValue) +
		constexpr_object_elements_.capacity() *
			sizeof(ConstexprObjectElement) +
		constexpr_object_index_.bucket_count() * sizeof(void*) +
		constexpr_object_index_.size() *
			(sizeof(std::size_t) + sizeof(std::uint32_t)) +
		constexpr_object_by_dump_.capacity() * sizeof(std::uint32_t) +
		constexpr_scratch_object_by_dump_.capacity() * sizeof(std::uint32_t) +
		constexpr_scratch_dump_.StorageBytes() +
		constexpr_call_facts_.bucket_count() * sizeof(void*) +
		constexpr_call_facts_.size() *
			(sizeof(ConstexprCallKey) + sizeof(ConstexprCallFact)) +
		associated_entities_.capacity() * sizeof(EntityId) +
		associated_scopes_.capacity() * sizeof(ScopeId) +
		associated_type_scratch_.capacity() * sizeof(TypeId) +
		associated_entity_marks_.capacity() * sizeof(std::uint32_t) +
		associated_scope_marks_.capacity() * sizeof(std::uint32_t) +
		associated_type_marks_.capacity() * sizeof(std::uint32_t) +
		candidate_marks_.capacity() * sizeof(std::uint32_t) +
		candidate_substitution_failures_.capacity() * sizeof(std::uint8_t) +
		empty_constructor_chain_states_.capacity() * sizeof(std::uint8_t) +
		empty_constructor_chain_dependency_begins_.capacity() *
			sizeof(std::uint32_t) +
		empty_constructor_chain_dependency_counts_.capacity() *
			sizeof(std::uint32_t) +
		empty_constructor_chain_dependencies_.capacity() * sizeof(BindingId) +
		empty_constructor_chain_entity_marks_.capacity() *
			sizeof(std::uint32_t) +
		empty_constructor_chain_binding_marks_.capacity() *
			sizeof(std::uint32_t) +
		empty_constructor_chain_pending_.capacity() * sizeof(BindingId) +
		empty_constructor_chain_member_dependencies_.capacity() *
			sizeof(BindingId) +
		empty_constructor_chain_base_dependencies_.capacity() *
			sizeof(BindingId) +
		empty_destructor_chain_cache_.capacity() * sizeof(std::uint8_t) +
		pack_alignment_stack_.capacity() * sizeof(std::size_t);
	for (std::size_t i = 0; i < functions_.size(); ++i)
		bytes += functions_[i].parameters.capacity() * sizeof(ParameterInfo);
	for (std::unordered_map<ConstexprCallKey, ConstexprCallFact,
		ConstexprCallKeyHash>::const_iterator i = constexpr_call_facts_.begin();
		i != constexpr_call_facts_.end(); ++i)
		bytes += i->first.arguments.capacity() * sizeof(ConstexprCallArgument);
	for (std::size_t i = 0; i < entity_data_members_.size(); ++i)
		bytes += entity_data_members_[i].capacity() * sizeof(BindingId);
	for (std::size_t i = 0;
		i < static_constant_initializers_by_binding_.size(); ++i)
		bytes += static_constant_initializers_by_binding_[i].
			function_dependencies.capacity() * sizeof(BindingId);
	for (std::size_t i = 0; i < entity_layout_members_.size(); ++i)
		bytes += entity_layout_members_[i].capacity() *
			sizeof(ClassLayoutMember);
	for (std::size_t i = 0; i < entity_constructors_.size(); ++i)
		bytes += entity_constructors_[i].capacity() * sizeof(BindingId);
	for (std::size_t i = 0; i < entity_conversion_functions_.size(); ++i)
		bytes += entity_conversion_functions_[i].capacity() * sizeof(BindingId);
	for (std::size_t i = 0;
		i < entity_conversion_function_templates_.size(); ++i)
		bytes += entity_conversion_function_templates_[i].capacity() *
			sizeof(std::size_t);
	for (std::size_t i = 0; i < entity_member_functions_.size(); ++i)
		bytes += entity_member_functions_[i].capacity() * sizeof(BindingId);
	for (std::size_t i = 0; i < class_polymorphism_.size(); ++i)
		bytes += class_polymorphism_[i].slots.capacity() *
			sizeof(VirtualSlotFact);
	for (std::size_t i = 0; i < scope_lifetimes_.size(); ++i)
		bytes += scope_lifetimes_[i].capacity() * sizeof(LifetimeObligation);
	for (std::size_t i = 0; i < aggregate_helpers_.size(); ++i)
		bytes += aggregate_helpers_[i].members.capacity() * sizeof(BindingId) +
			aggregate_helpers_[i].member_constructors.capacity() *
				sizeof(BindingId) +
			aggregate_helpers_[i].trivial_member_constructors.capacity() *
				sizeof(std::uint8_t);
	for (std::size_t i = 0; i < function_templates_.size(); ++i)
	{
		bytes += function_templates_[i].parameters.capacity() *
				sizeof(TemplateParameter) +
			function_templates_[i].default_context_by_parameter.capacity() *
				sizeof(std::uint32_t) +
			function_templates_[i].default_contexts.capacity() *
				sizeof(FunctionTemplateDefaultContext) +
			function_templates_[i].function_parameter_names.capacity() *
				sizeof(NameId) +
			function_templates_[i].function_parameter_defaults.capacity() *
				sizeof(NodeId) +
			function_templates_[i].function_parameter_nondeduced_syntax.capacity() *
				sizeof(NodeId) +
			function_templates_[i].function_parameter_nondeduced.capacity() *
				sizeof(std::uint8_t) +
			function_templates_[i].result_lookup_facts.capacity() *
				sizeof(FunctionTemplateResultLookupFact) +
			function_templates_[i].specialization_bindings.capacity() *
				sizeof(BindingId) +
			function_templates_[i].specialization_arguments.capacity() *
				sizeof(TemplateArgument) +
			function_templates_[i].specialization_argument_offsets.capacity() *
				sizeof(std::uint32_t) +
			function_templates_[i].specialization_parameter_offsets.capacity() *
				sizeof(std::uint32_t) +
			function_templates_[i].friend_owners.capacity() * sizeof(EntityId);
		for (std::size_t context = 0;
			context < function_templates_[i].default_contexts.size(); ++context)
		{
			bytes += function_templates_[i].default_contexts[context].
				parameters.capacity() * sizeof(TemplateParameter);
			const std::vector<TemplateParameter>& parameters =
				function_templates_[i].default_contexts[context].parameters;
			for (std::size_t parameter = 0;
				parameter < parameters.size(); ++parameter)
				bytes += parameters[parameter].template_parameters.capacity() *
					sizeof(TemplateParameter);
		}
	}
	for (std::size_t i = 0; i < class_templates_.size(); ++i)
	{
		bytes += class_templates_[i].parameters.capacity() *
				sizeof(TemplateParameter) +
			class_templates_[i].specialization_bindings.capacity() *
				sizeof(BindingId) +
			class_templates_[i].friend_owners.capacity() * sizeof(EntityId);
		for (std::size_t set = 0; set < 2; ++set)
		{
			const std::deque<ClassTemplateMemberPattern>& definitions = set == 0 ?
				class_templates_[i].member_definitions :
				class_templates_[i].demanded_member_definitions;
			bytes += definitions.size() * sizeof(ClassTemplateMemberPattern);
			for (std::size_t member = 0; member < definitions.size(); ++member)
			{
				const ClassTemplateMemberPattern& definition = definitions[member];
				bytes += definition.parameters.capacity() *
					sizeof(TemplateParameter) +
					definition.canonical_owner_arguments.capacity() *
						sizeof(TemplateArgument) +
					definition.nested_owner_path.capacity() * sizeof(NameId) +
					definition.nested_owner_argument_lists.capacity() *
						sizeof(NodeId);
			}
		}
		for (std::size_t partial = 0;
			partial < class_templates_[i].partial_specializations.size(); ++partial)
		{
			const ClassTemplatePartialPattern& pattern =
				class_templates_[i].partial_specializations[partial];
			bytes += pattern.parameters.capacity() * sizeof(TemplateParameter) +
				pattern.arguments.capacity() * sizeof(NodeId) +
				pattern.canonical_arguments.capacity() *
					sizeof(TemplateArgument);
		}
	}
	for (std::size_t i = 0;
		i < class_template_partial_selections_.size(); ++i)
	{
		const FunctionTemplateDeduction& bindings =
			class_template_partial_selections_[i].bindings;
		bytes += bindings.fixed_arguments.capacity() *
				sizeof(TemplateArgument) +
			bindings.pack_arguments.capacity() *
				sizeof(std::vector<TemplateArgument>) +
			bindings.pack_deduction_positions.capacity() * sizeof(std::size_t) +
			bindings.pack_deduction_started.capacity() * sizeof(std::uint8_t);
		for (std::size_t pack = 0;
			pack < bindings.pack_arguments.size(); ++pack)
			bytes += bindings.pack_arguments[pack].capacity() *
				sizeof(TemplateArgument);
	}
	for (std::size_t i = 0; i < variable_templates_.size(); ++i)
		bytes += variable_templates_[i].parameters.capacity() *
				sizeof(TemplateParameter) +
			variable_templates_[i].specialization_arguments.capacity() *
				sizeof(NodeId) +
			variable_templates_[i].canonical_specialization_arguments.capacity() *
				sizeof(TemplateArgument);
	bytes += variable_template_instantiations_.StorageBytes();
	return bytes;
}

}
}
