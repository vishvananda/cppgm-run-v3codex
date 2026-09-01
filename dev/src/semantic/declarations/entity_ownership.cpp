#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

namespace cppgm
{
namespace semantic
{

void Analyzer::RegisterLocalTypeAbiIdentity(EntityId entity)
{
	if (entity == kNoEntity || entity >= program_->entities.size())
		ThrowInternalCompilerError("local ABI identity has no entity");
	EntityRecord& record = program_->entities[entity];
	if (record.local_context == kNoBinding) return;
	const std::uint64_t key =
		(static_cast<std::uint64_t>(record.local_context) << 32) |
		(record.unnamed_class ? 0 : record.identity_name);
	CompactIndexSequence& occurrences = local_type_occurrences_.Ensure(key);
	if (occurrences.Size() >= kNoEntity)
		ThrowSemanticResourceLimit("too many local ABI type occurrences");
	record.local_name_ordinal =
		static_cast<std::uint32_t>(occurrences.Size());
	occurrences.Push(entity);
}

void Analyzer::RegisterInjectedStorageMember(BindingId alias,
	BindingId storage, BindingId member)
{
	if (alias >= program_->bindings.size() ||
		storage >= program_->bindings.size() ||
		member >= program_->bindings.size())
		ThrowInternalCompilerError("injected storage member identity is invalid");
	if (injected_fact_by_binding_.size() <= alias)
		injected_fact_by_binding_.resize(
			static_cast<std::size_t>(alias) + 1, kNoDumpEdge);
	if (injected_members_.size() >= kNoDumpEdge)
		ThrowSemanticResourceLimit("too many injected storage members");
	injected_fact_by_binding_[alias] =
		static_cast<std::uint32_t>(injected_members_.size());
	injected_members_.push_back(InjectedMemberInfo(storage, member));
	injected_aliases_by_storage_.Ensure(storage).Push(alias);
	if (program_->bindings[member].has_default_member_initializer)
		program_->bindings[storage].has_default_member_initializer = true;
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

bool Analyzer::RecordInjectedMemberInitializer(BindingId member,
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
		ThrowSemanticError(
			"duplicate constructor member initializer");
	injected_constructor_initializer_scratch_[fact] = initializer;
	injected_constructor_initializer_touched_.push_back(fact);
	if (program_->entities[owner].flavor == NAMED_UNION)
	{
		const std::vector<BindingId>& members = entity_data_members_[owner];
		const std::uint32_t ordinal =
			program_->BindingLayout(
				program_->bindings[storage]).member_ordinal;
		if (ordinal >= members.size() || members[ordinal] != storage)
			ThrowInternalCompilerError(
				"projected union storage has no canonical ordinal");
		if (!constructor_initializer_touched_.empty() &&
			constructor_initializer_touched_[0] != storage)
			ThrowSemanticError(
				"union constructor initializes multiple variants");
		if (constructor_initializer_touched_.empty())
			constructor_initializer_touched_.push_back(storage);
	}
	return true;
}

bool Analyzer::AddInjectedStorageInitializationActions(
	BindingId storage, ScopeId scope, std::uint32_t body)
{
	const CompactIndexSequence* aliases =
		injected_aliases_by_storage_.Find(storage);
	if (aliases == 0 || aliases->Size() == 0) return false;
	std::size_t explicit_count = 0;
	for (std::size_t i = 0; i < aliases->Size(); ++i)
	{
		const BindingId alias = static_cast<BindingId>((*aliases)[i]);
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
		ThrowInternalCompilerError("injected storage has no class entity");
	const bool union_storage =
		program_->entities[storage_entity].flavor == NAMED_UNION;
	if (union_storage && explicit_count != 1)
		ThrowSemanticError(
			"union constructor initializes multiple variants");
	for (std::size_t i = 0; i < aliases->Size(); ++i)
	{
		const BindingId alias = static_cast<BindingId>((*aliases)[i]);
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

void Analyzer::ClearInjectedConstructorInitializers()
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
