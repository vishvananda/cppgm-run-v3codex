#include "pa12_semantic_detail.h"

namespace cppgm
{
namespace pa12_semantic_detail
{

std::size_t SemanticAnalyzer::SideStorageBytes() const
{
	std::size_t bytes =
		scope_prefixes_.capacity() * sizeof(NameId) +
		scope_prefix_segments_.capacity() * sizeof(NameId) +
		scope_parents_.capacity() * sizeof(ScopeId) +
		scope_prefix_scratch_.capacity() * sizeof(NameId) +
		function_sets_.StorageBytes() +
		ordinary_function_sets_.StorageBytes() +
		hidden_friend_sets_.StorageBytes() +
		friend_class_grants_.StorageBytes() +
		friend_function_grants_.StorageBytes() +
		function_declarations_.StorageBytes() +
		using_function_declarations_.StorageBytes() +
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
		entity_member_functions_.capacity() *
			sizeof(std::vector<BindingId>) +
		class_polymorphism_.capacity() * sizeof(ClassPolymorphismFacts) +
		virtual_slot_by_binding_.capacity() * sizeof(std::uint32_t) +
		class_special_members_.capacity() * sizeof(ClassSpecialMemberFacts) +
		implicit_constructor_by_entity_.capacity() * sizeof(BindingId) +
		constructor_base_entry_by_binding_.capacity() * sizeof(BindingId) +
		destructor_base_entry_by_binding_.capacity() * sizeof(BindingId) +
		static_member_storage_by_binding_.capacity() * sizeof(std::uint32_t) +
		entity_destructor_by_entity_.capacity() * sizeof(BindingId) +
		hidden_friend_anchor_by_entity_.capacity() * sizeof(BindingId) +
		member_initializer_by_binding_.capacity() * sizeof(NodeId) +
		constructor_initializer_scratch_.capacity() * sizeof(NodeId) +
		constructor_initializer_touched_.capacity() * sizeof(BindingId) +
		function_templates_.capacity() * sizeof(FunctionTemplatePattern) +
		function_template_shape_parameters_.capacity() * sizeof(TypeId) +
		template_function_sets_.StorageBytes() +
		retained_call_function_sets_.StorageBytes() +
		retained_call_template_sets_.StorageBytes() +
		retained_call_lookup_states_.capacity() * sizeof(std::uint8_t) +
		retained_call_naming_classes_.capacity() * sizeof(EntityId) +
		template_instantiations_.StorageBytes() +
		class_templates_.capacity() * sizeof(ClassTemplatePattern) +
		class_template_pattern_by_entity_.capacity() * sizeof(std::uint32_t) +
		class_template_instantiations_.StorageBytes() +
		class_template_specialization_states_.capacity() * sizeof(std::uint8_t) +
		class_template_explicit_instantiation_states_.capacity() *
			sizeof(std::uint8_t) +
		class_template_member_definition_counts_.capacity() *
			sizeof(std::uint32_t) +
		deferred_class_definition_by_entity_.capacity() * sizeof(NodeId) +
		deferred_class_scope_by_entity_.capacity() * sizeof(ScopeId) +
		injected_fact_by_binding_.capacity() * sizeof(std::uint32_t) +
		injected_members_.capacity() * sizeof(InjectedMemberInfo) +
		scope_lifetimes_.capacity() *
			sizeof(std::vector<LifetimeObligation>) +
		nearest_lifetime_scopes_.capacity() * sizeof(ScopeId) +
		namespace_objects_.capacity() * sizeof(NamespaceObjectAction) +
		aggregate_helpers_.capacity() * sizeof(AggregateHelperInfo) +
		aggregate_helper_index_.StorageBytes() +
		break_cleanup_stops_.capacity() * sizeof(ScopeId) +
		continue_cleanup_stops_.capacity() * sizeof(ScopeId) +
		demanded_default_constructor_entities_.capacity() * sizeof(EntityId) +
		default_constructor_demand_states_.capacity() * sizeof(std::uint8_t) +
		demanded_functions_.capacity() * sizeof(BindingId) +
		associated_entities_.capacity() * sizeof(EntityId) +
		associated_scopes_.capacity() * sizeof(ScopeId) +
		associated_type_scratch_.capacity() * sizeof(TypeId) +
		associated_entity_marks_.capacity() * sizeof(std::uint32_t) +
		associated_scope_marks_.capacity() * sizeof(std::uint32_t) +
		associated_type_marks_.capacity() * sizeof(std::uint32_t) +
		candidate_marks_.capacity() * sizeof(std::uint32_t) +
		empty_destructor_chain_cache_.capacity() * sizeof(std::uint8_t) +
		pack_alignment_stack_.capacity() * sizeof(std::size_t);
	for (std::size_t i = 0; i < functions_.size(); ++i)
		bytes += functions_[i].parameters.capacity() * sizeof(ParameterInfo);
	for (std::size_t i = 0; i < entity_data_members_.size(); ++i)
		bytes += entity_data_members_[i].capacity() * sizeof(BindingId);
	for (std::size_t i = 0; i < entity_layout_members_.size(); ++i)
		bytes += entity_layout_members_[i].capacity() *
			sizeof(ClassLayoutMember);
	for (std::size_t i = 0; i < entity_constructors_.size(); ++i)
		bytes += entity_constructors_[i].capacity() * sizeof(BindingId);
	for (std::size_t i = 0; i < entity_conversion_functions_.size(); ++i)
		bytes += entity_conversion_functions_[i].capacity() * sizeof(BindingId);
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
		bytes += function_templates_[i].type_parameters.capacity() *
				sizeof(NameId) +
			function_templates_[i].specialization_bindings.capacity() *
				sizeof(BindingId) +
			function_templates_[i].specialization_arguments.capacity() *
				sizeof(TypeId);
	for (std::size_t i = 0; i < class_templates_.size(); ++i)
	{
		bytes += class_templates_[i].type_parameters.capacity() *
				sizeof(NameId) +
			class_templates_[i].default_arguments.capacity() * sizeof(NodeId) +
			class_templates_[i].specialization_bindings.capacity() *
				sizeof(BindingId) +
			class_templates_[i].member_definitions.capacity() *
				sizeof(ClassTemplateMemberPattern);
		for (std::size_t member = 0;
			member < class_templates_[i].member_definitions.size(); ++member)
		{
			const ClassTemplateMemberPattern& definition =
				class_templates_[i].member_definitions[member];
			bytes += definition.type_parameters.capacity() * sizeof(NameId) +
				definition.owner_parameter_indices.capacity() *
					sizeof(std::uint32_t) +
				definition.owner_fixed_arguments.capacity() * sizeof(TypeId) +
				definition.nested_owner_path.capacity() * sizeof(NameId);
		}
	}
	return bytes;
}

}
}
