#include "pa12_semantic_detail.h"

#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::RegisterLocalTypeAbiIdentity(EntityId entity)
{
	if (entity == kNoEntity || entity >= program_->entities.size())
		throw std::logic_error("local ABI identity has no entity");
	EntityRecord& record = program_->entities[entity];
	if (record.local_context == kNoBinding) return;
	const std::uint64_t key =
		(static_cast<std::uint64_t>(record.local_context) << 32) |
		record.identity_name;
	CompactIndexSequence& occurrences = local_type_occurrences_.Ensure(key);
	if (occurrences.Size() >= kNoEntity)
		throw std::runtime_error("too many local ABI type occurrences");
	record.local_name_ordinal =
		static_cast<std::uint32_t>(occurrences.Size());
	occurrences.Push(entity);
}

void SemanticAnalyzer::RegisterInjectedStorageMember(BindingId alias,
	BindingId storage, BindingId member)
{
	if (alias >= program_->bindings.size() ||
		storage >= program_->bindings.size() ||
		member >= program_->bindings.size())
		throw std::logic_error("injected storage member identity is invalid");
	if (injected_fact_by_binding_.size() <= alias)
		injected_fact_by_binding_.resize(
			static_cast<std::size_t>(alias) + 1, kNoDumpEdge);
	if (injected_members_.size() >= kNoDumpEdge)
		throw std::runtime_error("too many injected storage members");
	injected_fact_by_binding_[alias] =
		static_cast<std::uint32_t>(injected_members_.size());
	injected_members_.push_back(InjectedMemberInfo(storage, member));
	if (injected_aliases_by_storage_.size() <= storage)
		injected_aliases_by_storage_.resize(
			static_cast<std::size_t>(storage) + 1);
	injected_aliases_by_storage_[storage].push_back(alias);
	if (member < member_initializer_by_binding_.size() &&
		member_initializer_by_binding_[member] != kNoNode)
	{
		if (member_initializer_by_binding_.size() <= alias)
			member_initializer_by_binding_.resize(
				static_cast<std::size_t>(alias) + 1, kNoNode);
		member_initializer_by_binding_[alias] =
			member_initializer_by_binding_[member];
	}
}

bool SemanticAnalyzer::RecordInjectedMemberInitializer(BindingId member,
	EntityId owner, NodeId initializer)
{
	const std::uint32_t fact = member < injected_fact_by_binding_.size() ?
		injected_fact_by_binding_[member] : kNoDumpEdge;
	if (fact == kNoDumpEdge || fact >= injected_members_.size()) return false;
	const BindingId storage = injected_members_[fact].storage;
	if (storage >= program_->bindings.size() ||
		program_->bindings[storage].member_owner != owner) return false;
	if (injected_constructor_initializer_scratch_.size() <= fact)
		injected_constructor_initializer_scratch_.resize(
			static_cast<std::size_t>(fact) + 1, kNoNode);
	if (injected_constructor_initializer_scratch_[fact] != kNoNode)
		throw std::runtime_error(
			"duplicate constructor member initializer");
	injected_constructor_initializer_scratch_[fact] = initializer;
	injected_constructor_initializer_touched_.push_back(fact);
	return true;
}

bool SemanticAnalyzer::AddInjectedStorageInitializationActions(
	BindingId storage, ScopeId scope, std::uint32_t body)
{
	if (storage >= injected_aliases_by_storage_.size() ||
		injected_aliases_by_storage_[storage].empty()) return false;
	const std::vector<BindingId>& aliases =
		injected_aliases_by_storage_[storage];
	std::size_t explicit_count = 0;
	for (std::size_t i = 0; i < aliases.size(); ++i)
	{
		const BindingId alias = aliases[i];
		const std::uint32_t fact = alias < injected_fact_by_binding_.size() ?
			injected_fact_by_binding_[alias] : kNoDumpEdge;
		if (fact != kNoDumpEdge &&
			fact < injected_constructor_initializer_scratch_.size() &&
			injected_constructor_initializer_scratch_[fact] != kNoNode)
			++explicit_count;
	}
	if (explicit_count == 0) return false;
	const EntityId storage_entity = EntityOf(program_->bindings[storage].type);
	if (storage_entity == kNoEntity ||
		storage_entity >= program_->entities.size())
		throw std::logic_error("injected storage has no class entity");
	const bool union_storage =
		program_->entities[storage_entity].flavor == NAMED_UNION;
	if (union_storage && explicit_count != 1)
		throw std::runtime_error(
			"union constructor initializes multiple variants");
	for (std::size_t i = 0; i < aliases.size(); ++i)
	{
		const BindingId alias = aliases[i];
		const std::uint32_t fact = injected_fact_by_binding_[alias];
		NodeId initializer = fact <
			injected_constructor_initializer_scratch_.size() ?
				injected_constructor_initializer_scratch_[fact] : kNoNode;
		if (union_storage && initializer == kNoNode) continue;
		if (initializer == kNoNode &&
			alias < member_initializer_by_binding_.size())
			initializer = member_initializer_by_binding_[alias];
		AddMemberInitializationAction(alias, initializer, scope, body);
	}
	return true;
}

void SemanticAnalyzer::ClearInjectedConstructorInitializers()
{
	for (std::size_t i = 0;
		i < injected_constructor_initializer_touched_.size(); ++i)
	{
		const std::uint32_t fact =
			injected_constructor_initializer_touched_[i];
		if (fact < injected_constructor_initializer_scratch_.size())
			injected_constructor_initializer_scratch_[fact] = kNoNode;
	}
	injected_constructor_initializer_touched_.clear();
}

}
}
