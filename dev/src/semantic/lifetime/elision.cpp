#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <algorithm>
#include <limits>

namespace cppgm
{
namespace semantic
{

bool Analyzer::EmptyDefaultConstructorChain(BindingId constructor,
	std::vector<BindingId>* base_entries)
{
	constructor = program_->bindings[constructor].canonical;
	++empty_constructor_chain_requests_;
	if (empty_constructor_chain_states_.size() <= constructor)
	{
		const std::size_t size = static_cast<std::size_t>(constructor) + 1;
		empty_constructor_chain_states_.resize(size, 0);
		empty_constructor_chain_dependency_begins_.resize(size, 0);
		empty_constructor_chain_dependency_counts_.resize(size, 0);
	}
	if (empty_constructor_chain_states_[constructor] != 0)
	{
		++empty_constructor_chain_cache_hits_;
		if (empty_constructor_chain_states_[constructor] == 2)
		{
			const std::uint32_t begin =
				empty_constructor_chain_dependency_begins_[constructor];
			const std::uint32_t count =
				empty_constructor_chain_dependency_counts_[constructor];
			base_entries->insert(base_entries->end(),
				empty_constructor_chain_dependencies_.begin() + begin,
				empty_constructor_chain_dependencies_.begin() + begin + count);
		}
		return empty_constructor_chain_states_[constructor] == 2;
	}

	const BindingId root = constructor;
	const auto fail = [this, root]()
	{
		empty_constructor_chain_states_[root] = 1;
		return false;
	};
	empty_constructor_chain_pending_.clear();
	empty_constructor_chain_member_dependencies_.clear();
	empty_constructor_chain_base_dependencies_.clear();
	empty_constructor_chain_pending_.push_back(constructor);
	if (empty_constructor_chain_entity_marks_.size() < program_->entities.size())
		empty_constructor_chain_entity_marks_.resize(
			program_->entities.size(), 0);
	if (empty_constructor_chain_binding_marks_.size() < program_->bindings.size())
		empty_constructor_chain_binding_marks_.resize(
			program_->bindings.size(), 0);
	if (empty_constructor_chain_generation_ ==
		std::numeric_limits<std::uint32_t>::max())
	{
		std::fill(empty_constructor_chain_entity_marks_.begin(),
			empty_constructor_chain_entity_marks_.end(), 0);
		std::fill(empty_constructor_chain_binding_marks_.begin(),
			empty_constructor_chain_binding_marks_.end(), 0);
		empty_constructor_chain_generation_ = 0;
	}
	const std::uint32_t marker = ++empty_constructor_chain_generation_;
	while (!empty_constructor_chain_pending_.empty())
	{
		constructor = empty_constructor_chain_pending_.back();
		empty_constructor_chain_pending_.pop_back();
		const FunctionInfo& info = GetFunction(constructor);
		const BindingRecord& binding = program_->bindings[constructor];
		const bool known_empty_body = info.definition_body != kNoNode &&
			FirstSemanticChild(info.definition_body) == kNoNode;
		if (!info.constructor || !info.parameters.empty() ||
			info.constructor_initializer != kNoNode ||
			(!info.implicit_constructor && !info.defaulted_constructor &&
			 !known_empty_body) ||
			(info.definition_body != kNoNode &&
			 FirstSemanticChild(info.definition_body) != kNoNode))
			return fail();
		const EntityId entity = binding.member_owner;
		if (entity == kNoEntity || entity >= entity_data_members_.size())
			return fail();
		if (empty_constructor_chain_entity_marks_[entity] == marker) continue;
		empty_constructor_chain_entity_marks_[entity] = marker;
		++empty_constructor_chain_entity_visits_;
		if (program_->entities[entity].polymorphic_class ||
			program_->entities[entity].virtual_base_count != 0)
			return fail();
		const std::vector<BindingId>& members = entity_data_members_[entity];
		for (std::size_t i = 0; i < members.size(); ++i)
		{
			++empty_constructor_chain_dependency_edges_;
			const BindingRecord& member = program_->bindings[members[i]];
			if (member.has_default_member_initializer) return fail();
			TypeId member_type = member.type;
			const TypeRecord* member_record =
				&program_->types.Get(member_type);
			while (member_record->kind == TYPE_ARRAY ||
				member_record->kind == TYPE_QUALIFIED)
			{
				member_type = member_record->child;
				member_record = &program_->types.Get(member_type);
			}
			if (member_record->kind != TYPE_NAMED) continue;
			const EntityRecord& subobject =
				program_->entities[member_record->entity];
			if (!IsClassNamedFlavor(subobject.flavor)) continue;
			if (subobject.trivial_default_constructor) continue;
			if (member_record->entity >= entity_constructors_.size()) return fail();
			BindingId selected = kNoBinding;
			const std::vector<BindingId>& candidates =
				entity_constructors_[member_record->entity];
			for (std::size_t candidate = 0;
				candidate < candidates.size(); ++candidate)
			{
				const FunctionInfo& function = GetFunction(candidates[candidate]);
				std::size_t required = function.parameters.size();
				while (required != 0 &&
					function.parameters[required - 1].default_argument != kNoNode)
					--required;
				if (!function.constructor || function.deleted_constructor ||
					required != 0) continue;
				if (selected != kNoBinding) return fail();
				selected = candidates[candidate];
			}
			if (selected == kNoBinding) return fail();
			selected = program_->bindings[selected].canonical;
			if (empty_constructor_chain_binding_marks_[selected] != marker)
			{
				empty_constructor_chain_binding_marks_[selected] = marker;
				empty_constructor_chain_member_dependencies_.push_back(selected);
			}
			empty_constructor_chain_pending_.push_back(selected);
		}
		for (std::size_t base_index = 0;
			base_index < program_->entities[entity].direct_base_count; ++base_index)
		{
			++empty_constructor_chain_dependency_edges_;
			const EntityId base = program_->DirectBase(entity, base_index).entity;
			if (program_->entities[base].trivial_default_constructor) continue;
			if (base >= entity_constructors_.size()) return fail();
			BindingId next = kNoBinding;
			const std::vector<BindingId>& candidates = entity_constructors_[base];
			for (std::size_t i = 0; i < candidates.size(); ++i)
			{
				const FunctionInfo& candidate = GetFunction(candidates[i]);
				std::size_t required = candidate.parameters.size();
				while (required != 0 &&
					candidate.parameters[required - 1].default_argument != kNoNode)
					--required;
				if (!candidate.constructor || candidate.deleted_constructor ||
					required != 0) continue;
				if (next != kNoBinding) return fail();
				next = candidates[i];
			}
			if (next == kNoBinding) return fail();
			next = program_->bindings[next].canonical;
			const FunctionInfo& next_info = GetFunction(next);
			if (!(next_info.implicit_constructor &&
				program_->entities[base].trivial_default_constructor) &&
				empty_constructor_chain_binding_marks_[next] != marker)
			{
				empty_constructor_chain_binding_marks_[next] = marker;
				empty_constructor_chain_base_dependencies_.push_back(next);
			}
			empty_constructor_chain_pending_.push_back(next);
		}
	}
	// This routine is also a speculative elision query.  Publish ABI entries
	// only after the complete chain has proved empty; a failed query must not
	// change which constructor entry a later real base-subobject call demands.
	const std::size_t dependency_begin =
		empty_constructor_chain_dependencies_.size();
	empty_constructor_chain_dependencies_.insert(
		empty_constructor_chain_dependencies_.end(),
		empty_constructor_chain_member_dependencies_.begin(),
		empty_constructor_chain_member_dependencies_.end());
	for (std::size_t i = 0;
		i < empty_constructor_chain_base_dependencies_.size(); ++i)
	{
		const BindingId entry =
			EnsureConstructorBaseEntry(
				empty_constructor_chain_base_dependencies_[i]);
		empty_constructor_chain_dependencies_.push_back(entry);
	}
	const std::size_t dependency_count =
		empty_constructor_chain_dependencies_.size() - dependency_begin;
	if (dependency_begin > std::numeric_limits<std::uint32_t>::max() ||
		dependency_count > std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit(
			"empty constructor dependency index overflow");
	empty_constructor_chain_dependency_begins_[root] =
		static_cast<std::uint32_t>(dependency_begin);
	empty_constructor_chain_dependency_counts_[root] =
		static_cast<std::uint32_t>(dependency_count);
	empty_constructor_chain_states_[root] = 2;
	base_entries->insert(base_entries->end(),
		empty_constructor_chain_dependencies_.begin() + dependency_begin,
		empty_constructor_chain_dependencies_.end());
	return true;
}

void Analyzer::PublishInitializationStats() const
{
	PublishStaticConstantEvaluationStats();
	stats_->empty_constructor_chain_requests =
		empty_constructor_chain_requests_;
	stats_->empty_constructor_chain_cache_hits =
		empty_constructor_chain_cache_hits_;
	stats_->empty_constructor_chain_entity_visits =
		empty_constructor_chain_entity_visits_;
	stats_->empty_constructor_chain_dependency_edges =
		empty_constructor_chain_dependency_edges_;
}

}
}
