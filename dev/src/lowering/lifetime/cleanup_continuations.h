#ifndef CPPGM_LOWERING_LIFETIME_CLEANUP_CONTINUATIONS_H
#define CPPGM_LOWERING_LIFETIME_CLEANUP_CONTINUATIONS_H

#include "lowering/ir/model.h"
#include "semantic/model/graph.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cppgm
{
namespace lowering
{
namespace cleanup
{

const std::uint32_t kNoCleanupState = UINT32_MAX;

enum Mode : std::uint8_t
{
	FULL_EXPRESSION_TERMINAL,
	FULL_EXPRESSION_ACTION,
	FULL_EXPRESSION_LANDING,
	CONDITIONAL_EXTERNAL_TAIL,
	CONDITIONAL_ACTION,
	LEXICAL_RETURN_CENSUS,
	LEXICAL_RETURN_TERMINAL,
	LEXICAL_RETURN_ACTION
};

struct ActionKey
{
	std::uint32_t lifetime_object;
	std::uint32_t object_binding;
	std::uint32_t destructor_binding;
	std::uint32_t operand_type;
	std::uint32_t base_projection_count;
	std::int64_t constant_value;
	std::uint64_t base_projection_offset;
	std::uint8_t flags;

	ActionKey();
	bool operator==(const ActionKey& other) const;
};

struct Action
{
	ActionKey key;
	std::uint32_t representative_node;
	Action(const ActionKey& key_value, std::uint32_t node);
};

ActionKey MakeActionKey(const semantic::DumpNode& action);

struct Key
{
	std::uint32_t action;
	std::uint32_t tail;
	std::uint32_t terminal;
	std::uint32_t context;
	Mode mode;

	Key();
	Key(std::uint32_t action_value, std::uint32_t tail_value,
		std::uint32_t terminal_value, std::uint32_t context_value,
		Mode mode_value);
	bool operator==(const Key& other) const;
};

struct State
{
	Key key;
	lowering::ir::BlockId block;
	bool block_bound;

	State();
	explicit State(const Key& key_value);
};

class Interner
{
public:
	Interner();
	void Clear();
	std::uint32_t InternAction(const ActionKey& key,
		std::uint32_t representative_node, bool* inserted);
	const Action& GetAction(std::uint32_t action) const;
	std::uint32_t Intern(const Key& key, bool* inserted);
	const State& Get(std::uint32_t state) const;
	void BindBlock(std::uint32_t state, lowering::ir::BlockId block);
	std::uint32_t StateForBlock(lowering::ir::BlockId block) const;

private:
	struct Slot
	{
		std::uint64_t fingerprint;
		std::uint32_t state;
		bool occupied;
		Slot();
	};
	struct ActionSlot
	{
		std::uint64_t fingerprint;
		std::uint32_t action;
		bool occupied;
		ActionSlot();
	};

	static std::uint64_t Fingerprint(const Key& key);
	static std::uint64_t Fingerprint(const ActionKey& key);
	void Rehash(std::size_t capacity);
	void RehashActions(std::size_t capacity);

	std::vector<Slot> slots_;
	std::vector<std::size_t> occupied_slots_;
	std::vector<State> states_;
	std::vector<std::uint32_t> block_states_;
	std::vector<ActionSlot> action_slots_;
	std::vector<std::size_t> occupied_action_slots_;
	std::vector<Action> actions_;
};

}  // namespace cleanup
}  // namespace lowering
}  // namespace cppgm

#endif
