#include "lowering/objects/cleanup_continuations.h"
#include "lowering/support/errors.h"


namespace cppgm
{
namespace lowering
{
namespace cleanup
{

ActionKey::ActionKey()
	: lifetime_object(kNoCleanupState), object_binding(kNoCleanupState),
	  destructor_binding(kNoCleanupState), operand_type(kNoCleanupState),
	  base_projection_count(0), constant_value(0), base_projection_offset(0),
	  flags(0) {}

bool ActionKey::operator==(const ActionKey& other) const
{
	return lifetime_object == other.lifetime_object &&
		object_binding == other.object_binding &&
		destructor_binding == other.destructor_binding &&
		operand_type == other.operand_type &&
		base_projection_count == other.base_projection_count &&
		constant_value == other.constant_value &&
		base_projection_offset == other.base_projection_offset &&
		flags == other.flags;
}

Action::Action(const ActionKey& key_value, std::uint32_t node)
	: key(key_value), representative_node(node) {}

ActionKey MakeActionKey(const semantic::DumpNode& action)
{
	ActionKey key;
	key.lifetime_object = action.lifetime_object;
	key.object_binding = action.object_binding;
	key.destructor_binding = action.binding;
	key.operand_type = action.operand_type;
	key.base_projection_count = action.base_projection_count;
	key.constant_value = action.constant_value;
	key.base_projection_offset = action.base_projection_offset;
	key.flags = (action.exception_handler_exit ? 1 : 0) |
		(action.exception_cleanup_region_exit ? 2 : 0) |
		(action.constant ? 4 : 0) |
		(action.complete_object_destruction ? 8 : 0) |
		(action.has_base_projection_offset ? 16 : 0);
	return key;
}

Key::Key()
	: action(kNoCleanupState), tail(kNoCleanupState), terminal(0), context(0),
	  mode(FULL_EXPRESSION_TERMINAL) {}

Key::Key(std::uint32_t action_value, std::uint32_t tail_value,
	std::uint32_t terminal_value, std::uint32_t context_value,
	Mode mode_value)
	: action(action_value), tail(tail_value), terminal(terminal_value),
	  context(context_value), mode(mode_value) {}

bool Key::operator==(const Key& other) const
{
	return action == other.action && tail == other.tail &&
		terminal == other.terminal && context == other.context &&
		mode == other.mode;
}

State::State() : block(lowering::ir::BlockId(0)), block_bound(false) {}
State::State(const Key& key_value)
	: key(key_value), block(lowering::ir::BlockId(0)),
	  block_bound(false) {}

Interner::Slot::Slot() : fingerprint(0), state(0), occupied(false) {}
Interner::ActionSlot::ActionSlot()
	: fingerprint(0), action(0), occupied(false) {}

Interner::Interner() : slots_(16), action_slots_(16) {}

void Interner::Clear()
{
	for (std::size_t i = 0; i < occupied_slots_.size(); ++i)
		slots_[occupied_slots_[i]].occupied = false;
	occupied_slots_.clear();
	states_.clear();
	block_states_.clear();
	for (std::size_t i = 0; i < occupied_action_slots_.size(); ++i)
		action_slots_[occupied_action_slots_[i]].occupied = false;
	occupied_action_slots_.clear();
	actions_.clear();
}

std::uint64_t Interner::Fingerprint(const ActionKey& key)
{
	std::uint64_t hash = 1469598103934665603ULL;
	const std::uint64_t words[] = {
		key.lifetime_object, key.object_binding, key.destructor_binding,
		key.operand_type, key.base_projection_count,
		static_cast<std::uint64_t>(key.constant_value),
		key.base_projection_offset, key.flags };
	for (std::size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
	{
		hash ^= words[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

std::uint32_t Interner::InternAction(const ActionKey& key,
	std::uint32_t representative_node, bool* inserted)
{
	if ((actions_.size() + 1) * 2 >= action_slots_.size())
		RehashActions(action_slots_.size() * 2);
	const std::uint64_t fingerprint = Fingerprint(key);
	std::size_t index = static_cast<std::size_t>(fingerprint) &
		(action_slots_.size() - 1);
	while (action_slots_[index].occupied)
	{
		const ActionSlot& slot = action_slots_[index];
		if (slot.fingerprint == fingerprint && actions_[slot.action].key == key)
		{
			if (inserted) *inserted = false;
			return slot.action;
		}
		index = (index + 1) & (action_slots_.size() - 1);
	}
	if (actions_.size() >= kNoCleanupState)
		ThrowLoweringResourceLimit("cleanup action identity overflow");
	const std::uint32_t action = static_cast<std::uint32_t>(actions_.size());
	actions_.push_back(Action(key, representative_node));
	action_slots_[index].fingerprint = fingerprint;
	action_slots_[index].action = action;
	action_slots_[index].occupied = true;
	occupied_action_slots_.push_back(index);
	if (inserted) *inserted = true;
	return action;
}

const Action& Interner::GetAction(std::uint32_t action) const
{
	if (action >= actions_.size())
		ThrowLoweringInternal("invalid cleanup action identity");
	return actions_[action];
}

std::uint64_t Interner::Fingerprint(const Key& key)
{
	std::uint64_t hash = 1469598103934665603ULL;
	const std::uint32_t words[] = { key.action, key.tail, key.terminal,
		key.context, static_cast<std::uint32_t>(key.mode) };
	for (std::size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
	{
		hash ^= words[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

std::uint32_t Interner::Intern(const Key& key, bool* inserted)
{
	if ((states_.size() + 1) * 2 >= slots_.size())
		Rehash(slots_.size() * 2);
	const std::uint64_t fingerprint = Fingerprint(key);
	std::size_t index = static_cast<std::size_t>(fingerprint) &
		(slots_.size() - 1);
	while (slots_[index].occupied)
	{
		const Slot& slot = slots_[index];
		if (slot.fingerprint == fingerprint && states_[slot.state].key == key)
		{
			if (inserted) *inserted = false;
			return slot.state;
		}
		index = (index + 1) & (slots_.size() - 1);
	}
	if (states_.size() >= kNoCleanupState)
		ThrowLoweringResourceLimit("cleanup continuation identity overflow");
	const std::uint32_t state = static_cast<std::uint32_t>(states_.size());
	states_.push_back(State(key));
	slots_[index].fingerprint = fingerprint;
	slots_[index].state = state;
	slots_[index].occupied = true;
	occupied_slots_.push_back(index);
	if (inserted) *inserted = true;
	return state;
}

const State& Interner::Get(std::uint32_t state) const
{
	if (state >= states_.size())
		ThrowLoweringInternal("invalid cleanup continuation identity");
	return states_[state];
}

void Interner::BindBlock(std::uint32_t state,
	lowering::ir::BlockId block)
{
	if (state >= states_.size())
		ThrowLoweringInternal("invalid cleanup continuation identity");
	State& record = states_[state];
	if (record.block_bound && record.block != block)
		ThrowLoweringInternal("cleanup continuation acquired a second block");
	record.block = block;
	record.block_bound = true;
	const std::uint32_t block_index = block;
	if (block_index >= block_states_.size())
		block_states_.resize(static_cast<std::size_t>(block_index) + 1,
			kNoCleanupState);
	if (block_states_[block_index] != kNoCleanupState &&
		block_states_[block_index] != state)
		ThrowLoweringInternal("cleanup block acquired a second state");
	block_states_[block_index] = state;
}

std::uint32_t Interner::StateForBlock(
	lowering::ir::BlockId block) const
{
	const std::uint32_t block_index = block;
	return block_index < block_states_.size() ? block_states_[block_index] :
		kNoCleanupState;
}

void Interner::Rehash(std::size_t capacity)
{
	std::vector<Slot> replacement(capacity);
	std::vector<std::size_t> replacement_occupied;
	replacement_occupied.reserve(states_.size());
	for (std::size_t i = 0; i < occupied_slots_.size(); ++i)
	{
		const Slot& source = slots_[occupied_slots_[i]];
		std::size_t index = static_cast<std::size_t>(source.fingerprint) &
			(capacity - 1);
		while (replacement[index].occupied)
			index = (index + 1) & (capacity - 1);
		replacement[index] = source;
		replacement_occupied.push_back(index);
	}
	slots_.swap(replacement);
	occupied_slots_.swap(replacement_occupied);
}

void Interner::RehashActions(std::size_t capacity)
{
	std::vector<ActionSlot> replacement(capacity);
	std::vector<std::size_t> replacement_occupied;
	replacement_occupied.reserve(actions_.size());
	for (std::size_t i = 0; i < occupied_action_slots_.size(); ++i)
	{
		const ActionSlot& source = action_slots_[occupied_action_slots_[i]];
		std::size_t index = static_cast<std::size_t>(source.fingerprint) &
			(capacity - 1);
		while (replacement[index].occupied)
			index = (index + 1) & (capacity - 1);
		replacement[index] = source;
		replacement_occupied.push_back(index);
	}
	action_slots_.swap(replacement);
	occupied_action_slots_.swap(replacement_occupied);
}

}  // namespace cleanup
}  // namespace lowering
}  // namespace cppgm
