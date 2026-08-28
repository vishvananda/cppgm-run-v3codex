#include "lowir/optimize/lowir_memory_gvn.h"

#include "lowir/analysis/lowir_eh_context.h"
#include "lowir/optimize/lowir_opt.h"
#include "lowir/optimize/lowir_optimizer_support.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lowir_opt {
namespace {

using lowir_model::Function;
using lowir_model::Instruction;
using lowir_model::LowType;
using lowir_model::Operand;
using optimizer_support::combine_hash;

const std::size_t kNoIndex = static_cast<std::size_t>(-1);
const std::size_t kMaximumClasses = 4096;
const std::size_t kMaximumMergeVersions = 8192;

enum MemoryClassKind
{
  MCK_SLOT,
  MCK_GLOBAL,
  MCK_UNKNOWN_POINTER,
  MCK_UNKNOWN_GLOBAL,
  MCK_EH_BARRIER
};

struct MemoryClass
{
  MemoryClassKind kind;
  std::uint32_t identity;
  lowir_model::ValueId address_identity;
  long long byte_offset;
  std::size_t byte_size;
  std::size_t next_same_root;
  bool precise;
  bool readonly;
  bool enabled;
};

bool scalar_load_type(const LowType & type)
{
  return type.kind != lowir_model::LTK_INVALID &&
    type.kind != lowir_model::LTK_VOID &&
    type.kind != lowir_model::LTK_OBJECT;
}

enum AddressRootKind
{
  ARK_NONE,
  ARK_SLOT,
  ARK_GLOBAL,
  ARK_UNKNOWN
};

struct AddressFact
{
  AddressRootKind root_kind = ARK_NONE;
  std::uint32_t identity = 0;
  lowir_model::ValueId address_identity;
  long long byte_offset = 0;
  bool precise = false;

  bool known() const { return root_kind != ARK_NONE; }
};

AddressFact operand_address(
    const Operand & operand, const std::vector<AddressFact> & addresses)
{
  AddressFact result;
  if(operand.kind == Operand::OP_SLOT) {
    result.root_kind = ARK_SLOT;
    result.identity = operand.slot;
    result.precise = true;
  } else if(operand.kind == Operand::OP_GLOBAL) {
    result.root_kind = ARK_GLOBAL;
    result.identity = operand.symbol;
    result.precise = true;
  } else if(operand.kind == Operand::OP_TEMP &&
            operand.value < addresses.size()) {
    result = addresses[operand.value];
    if(!result.known()) {
      result.root_kind = ARK_UNKNOWN;
      result.identity = operand.value;
      result.address_identity = operand.value;
    }
  }
  return result;
}

bool same_address(const AddressFact & left, const AddressFact & right)
{
  if(left.root_kind != right.root_kind ||
     left.identity != right.identity || left.precise != right.precise)
    return false;
  return left.precise ? left.byte_offset == right.byte_offset :
    left.address_identity == right.address_identity;
}

bool offset_address(const AddressFact & base, const Instruction & instruction,
                    AddressFact * result)
{
  if(!base.known()) return false;
  *result = base;
  result->address_identity = instruction.dest;
  if(!base.precise || instruction.second.kind != Operand::OP_INTEGER ||
     !instruction.second.has_int_value || instruction.second.int_high != 0) {
    result->precise = false;
    return true;
  }
  const __int128 offset = static_cast<__int128>(base.byte_offset) +
    static_cast<__int128>(instruction.second.int_value) *
      static_cast<__int128>(instruction.type.storage_size);
  if(offset < static_cast<__int128>(
       std::numeric_limits<long long>::min()) ||
     offset > static_cast<__int128>(
       std::numeric_limits<long long>::max())) {
    result->precise = false;
    return true;
  }
  result->byte_offset = static_cast<long long>(offset);
  return true;
}

std::vector<AddressFact> derive_addresses(const Function & function)
{
  std::vector<AddressFact> result(function.value_names.size());
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      if(!instruction.dest.valid() || instruction.dest >= result.size())
        continue;
      AddressFact fact;
      if(instruction.kind == Instruction::IK_ADDR)
        fact = operand_address(instruction.first, result);
      else if(instruction.kind == Instruction::IK_INDEX)
        offset_address(
          operand_address(instruction.first, result), instruction, &fact);
      else if(instruction.kind == Instruction::IK_COPY)
        fact = operand_address(instruction.first, result);
      else if(instruction.kind == Instruction::IK_PHI &&
              instruction.args.size() >= 2) {
        fact = operand_address(instruction.args[1], result);
        for(std::size_t incoming = 3;
            fact.known() && incoming < instruction.args.size();
            incoming += 2)
          if(!same_address(fact,
               operand_address(instruction.args[incoming], result)))
            fact = AddressFact();
      }
      if(fact.known()) {
        // A copy and an equal-input phi preserve an unknown pointer's exact
        // identity.  An imprecise index already received its result identity
        // in offset_address above.
        result[instruction.dest] = fact;
      }
    }
  return result;
}

bool permitted_slot_address_use(const Instruction & instruction,
                                const Operand * operand)
{
  return (instruction.kind == Instruction::IK_LOAD &&
          operand == &instruction.first) ||
    (instruction.kind == Instruction::IK_STORE &&
     operand == &instruction.second) ||
    (instruction.kind == Instruction::IK_ADDR &&
     operand == &instruction.first) ||
    (instruction.kind == Instruction::IK_INDEX &&
     operand == &instruction.first) ||
    (instruction.kind == Instruction::IK_COPY &&
     operand == &instruction.first);
}

std::vector<unsigned char> find_escaped_slots(
    const Function & function, const std::vector<AddressFact> & addresses)
{
  std::vector<unsigned char> escaped(function.slot_names.size(), 0);
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      const Operand * operands[] = {
        &instruction.first, &instruction.second, &instruction.third};
      for(std::size_t operand = 0; operand < 3; ++operand) {
        AddressFact fact = operand_address(*operands[operand], addresses);
        if(fact.root_kind == ARK_SLOT &&
           !permitted_slot_address_use(instruction, operands[operand]))
          escaped[fact.identity] = 1;
      }
      for(std::size_t operand = 0;
          operand < instruction.args.size(); ++operand) {
        AddressFact fact = operand_address(
          instruction.args[operand], addresses);
        if(fact.root_kind == ARK_SLOT) escaped[fact.identity] = 1;
      }
    }
  return escaped;
}

std::vector<std::size_t> layout_last_uses(const Function & function)
{
  std::vector<std::size_t> result(function.value_names.size(), kNoIndex);
  std::size_t position = 0;
  const auto record = [&result, &position](const Operand & operand) {
    if(operand.kind == Operand::OP_TEMP && operand.value < result.size())
      result[operand.value] = position;
  };
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      record(instruction.first);
      record(instruction.second);
      record(instruction.third);
      for(std::size_t argument = 0;
          argument < instruction.args.size(); ++argument)
        record(instruction.args[argument]);
      ++position;
    }
  return result;
}

struct LocationKey
{
  AddressRootKind root_kind;
  std::uint32_t identity;
  lowir_model::ValueId address_identity;
  long long byte_offset;
  lowir_model::LowTypeKind type_kind;
  std::size_t type_size;
  std::uint32_t type_alignment;
  bool precise;

  bool operator==(const LocationKey & other) const
  {
    return root_kind == other.root_kind && identity == other.identity &&
      address_identity == other.address_identity &&
      byte_offset == other.byte_offset && type_kind == other.type_kind &&
      type_size == other.type_size &&
      type_alignment == other.type_alignment && precise == other.precise;
  }
};

struct MemoryKey
{
  std::size_t memory_class;
  std::size_t version;
  std::size_t unknown_version;
  std::size_t eh_version;
  lowir_model::LowTypeKind type_kind;
  std::size_t type_size;
  std::uint32_t type_alignment;

  bool operator==(const MemoryKey & other) const
  {
    return memory_class == other.memory_class &&
      version == other.version &&
      unknown_version == other.unknown_version &&
      eh_version == other.eh_version &&
      type_kind == other.type_kind && type_size == other.type_size &&
      type_alignment == other.type_alignment;
  }
};

struct LocationKeyHash
{
  std::size_t operator()(const LocationKey & key) const
  {
    std::size_t result = static_cast<std::size_t>(key.root_kind);
    combine_hash(&result, key.identity);
    combine_hash(&result, static_cast<std::uint32_t>(key.address_identity));
    combine_hash(&result, std::hash<long long>()(key.byte_offset));
    combine_hash(&result, static_cast<std::size_t>(key.type_kind));
    combine_hash(&result, key.type_size);
    combine_hash(&result, key.type_alignment);
    combine_hash(&result, key.precise ? 1 : 0);
    return result;
  }
};

LocationKey location_key(const AddressFact & address, const LowType & type)
{
  LocationKey result;
  result.root_kind = address.root_kind;
  result.identity = address.identity;
  result.address_identity = address.precise ?
    lowir_model::ValueId() : address.address_identity;
  result.byte_offset = address.precise ? address.byte_offset : 0;
  result.type_kind = type.kind;
  result.type_size = type.storage_size;
  result.type_alignment = type.alignment;
  result.precise = address.precise;
  return result;
}

struct LocationInfo
{
  std::size_t load_count = 0;
  std::size_t memory_class = kNoIndex;
};

struct MemoryKeyHash
{
  std::size_t operator()(const MemoryKey & key) const
  {
    std::size_t result = key.memory_class;
    combine_hash(&result, key.version);
    combine_hash(&result, key.unknown_version);
    combine_hash(&result, key.eh_version);
    combine_hash(&result, static_cast<std::size_t>(key.type_kind));
    combine_hash(&result, key.type_size);
    combine_hash(&result, key.type_alignment);
    return result;
  }
};

struct BlockEvent
{
  std::size_t block;
  bool entering;
};

std::vector<BlockEvent> dominator_events(
    const lowir_analysis::DominatorTree & dominators,
    const std::vector<lowir_analysis::EdgeList> & children)
{
  struct Frame { std::size_t block; std::size_t child; };
  std::vector<BlockEvent> result;
  std::vector<Frame> stack;
  std::vector<unsigned char> scheduled(children.size(), 0);
  result.reserve(children.size() * 2);
  for(std::size_t root = 0; root < children.size(); ++root) {
    if(scheduled[root] ||
       (root != 0 && dominators.preorder[root] != 0)) continue;
    scheduled[root] = 1;
    result.push_back(BlockEvent{root, true});
    stack.push_back(Frame{root, 0});
    while(!stack.empty()) {
      Frame & frame = stack.back();
      if(frame.child < children[frame.block].size()) {
        const std::size_t child =
          children[frame.block][frame.child++];
        scheduled[child] = 1;
        result.push_back(BlockEvent{child, true});
        stack.push_back(Frame{child, 0});
      } else {
        result.push_back(BlockEvent{frame.block, false});
        stack.pop_back();
      }
    }
  }
  return result;
}

void add_definition(std::vector<std::vector<std::size_t> > * definitions,
                    std::size_t memory_class, std::size_t block)
{
  std::vector<std::size_t> & blocks = (*definitions)[memory_class];
  if(blocks.empty() || blocks.back() != block) blocks.push_back(block);
}

bool is_atomic_barrier(Instruction::Kind kind)
{
  return kind == Instruction::IK_ATOMIC_LOAD ||
    kind == Instruction::IK_ATOMIC_STORE ||
    kind == Instruction::IK_ATOMIC_EXCHANGE ||
    kind == Instruction::IK_ATOMIC_ADD_FETCH ||
    kind == Instruction::IK_ATOMIC_COMPARE_EXCHANGE ||
    kind == Instruction::IK_ATOMIC_THREAD_FENCE ||
    kind == Instruction::IK_ATOMIC_SIGNAL_FENCE;
}

bool is_other_memory_write(Instruction::Kind kind)
{
  return kind == Instruction::IK_COPYOBJ ||
    kind == Instruction::IK_ZEROINIT ||
    kind == Instruction::IK_VA_START ||
    kind == Instruction::IK_VA_ARG;
}

typedef std::unordered_map<LocationKey, LocationInfo, LocationKeyHash>
  LocationMap;

void collect_load_locations(
    const Function & function, const std::vector<AddressFact> & addresses,
    const std::vector<unsigned char> & escaped_slots,
    const std::vector<lowir_model::GlobalStorageMode> & global_storage,
    LocationMap * locations, std::vector<LocationKey> * location_order)
{
  locations->reserve(function.value_names.size() / 8 + 1);
  for(std::size_t block = 0; block < function.blocks.size(); ++block)
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      if(instruction.kind != Instruction::IK_LOAD ||
         instruction.volatile_access || instruction.debug_location.present() ||
         !scalar_load_type(instruction.type)) continue;
      const AddressFact address = operand_address(
        instruction.first, addresses);
      if(!address.known() ||
         (address.root_kind == ARK_SLOT &&
          escaped_slots[address.identity]) ||
         (address.root_kind == ARK_GLOBAL &&
          (address.identity >= global_storage.size() ||
           global_storage[address.identity] ==
             lowir_model::GSM_THREAD_LOCAL))) continue;
      const LocationKey key = location_key(address, instruction.type);
      const auto inserted = locations->emplace(key, LocationInfo());
      if(inserted.second) location_order->push_back(key);
      ++inserted.first->second.load_count;
    }
}

void record_elapsed(Stats * stats,
                    const std::chrono::steady_clock::time_point & started)
{
  if(stats) stats->memory_gvn_nanoseconds += static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - started).count());
}

std::size_t load_class(const Instruction & instruction,
                       const std::vector<AddressFact> & addresses,
                       const LocationMap & locations)
{
  const AddressFact address = operand_address(instruction.first, addresses);
  if(!address.known()) return kNoIndex;
  const auto found = locations.find(location_key(address, instruction.type));
  return found == locations.end() ? kNoIndex : found->second.memory_class;
}

bool ranges_overlap(long long left_offset, std::size_t left_size,
                    long long right_offset, std::size_t right_size)
{
  const __int128 left_end = static_cast<__int128>(left_offset) + left_size;
  const __int128 right_end = static_cast<__int128>(right_offset) + right_size;
  return static_cast<__int128>(left_offset) < right_end &&
    static_cast<__int128>(right_offset) < left_end;
}

bool store_overlaps(const AddressFact & store, std::size_t store_size,
                    const MemoryClass & memory_class)
{
  if(memory_class.kind == MCK_UNKNOWN_GLOBAL ||
     store.root_kind == ARK_NONE) return false;
  const MemoryClassKind store_kind = store.root_kind == ARK_SLOT ?
    MCK_SLOT : store.root_kind == ARK_GLOBAL ?
      MCK_GLOBAL : MCK_UNKNOWN_POINTER;
  if(store_kind != memory_class.kind ||
     store.identity != memory_class.identity) return false;
  if(store.precise && memory_class.precise)
    return ranges_overlap(store.byte_offset, store_size,
      memory_class.byte_offset, memory_class.byte_size);
  return store.precise == memory_class.precise ?
    store.address_identity == memory_class.address_identity : true;
}

template<typename Callback>
void visit_store_classes(
    const AddressFact & store, std::size_t store_size,
    const std::vector<MemoryClass> & classes,
    const std::vector<std::size_t> & first_slot_class,
    const std::vector<std::uint32_t> & symbol_epochs,
    std::uint32_t function_epoch,
    const std::vector<std::size_t> & first_symbol_class,
    const Callback & callback)
{
  std::size_t memory_class = kNoIndex;
  if(store.root_kind == ARK_SLOT && store.identity < first_slot_class.size())
    memory_class = first_slot_class[store.identity];
  else if(store.root_kind == ARK_GLOBAL &&
          store.identity < symbol_epochs.size() &&
          symbol_epochs[store.identity] == function_epoch)
    memory_class = first_symbol_class[store.identity];
  while(memory_class != kNoIndex) {
    if(classes[memory_class].enabled && !classes[memory_class].readonly &&
       store_overlaps(store, store_size, classes[memory_class]))
      callback(memory_class);
    memory_class = classes[memory_class].next_same_root;
  }
}

template<typename CallBoundary>
void collect_memory_definitions(
    const Function & function, const std::vector<AddressFact> & addresses,
    const std::vector<MemoryClass> & classes,
    const std::vector<std::size_t> & first_slot_class,
    const std::vector<std::uint32_t> & symbol_epochs,
    std::uint32_t function_epoch,
    const std::vector<std::size_t> & first_symbol_class,
    std::size_t unknown_class, std::size_t eh_class,
    bool has_unknown_pointer,
    const lowir_eh_context::Context & eh_context,
    const CallBoundary & call_boundary,
    std::vector<std::vector<std::size_t> > * definitions)
{
  for(std::size_t block = 0; block < function.blocks.size(); ++block) {
    if(eh_class != kNoIndex && eh_context.entry_barriers[block])
      add_definition(definitions, eh_class, block);
    for(std::size_t index = 0;
        index < function.blocks[block].instructions.size(); ++index) {
      const Instruction & instruction =
        function.blocks[block].instructions[index];
      bool unknown_write = is_atomic_barrier(instruction.kind) ||
        is_other_memory_write(instruction.kind);
      lowir_model::FunctionBoundaryMetadata boundary;
      if(instruction.kind == Instruction::IK_CALL) {
        boundary = call_boundary(instruction);
        unknown_write = boundary.effects != lowir_model::CFXM_READNONE &&
          boundary.effects != lowir_model::CFXM_READONLY;
      }
      if(eh_class != kNoIndex &&
         (lowir_eh_context::is_eh_instruction(instruction.kind) ||
          (instruction.kind == Instruction::IK_CALL &&
           boundary.unwind != lowir_model::CUM_NO)))
        add_definition(definitions, eh_class, block);
      if(instruction.kind == Instruction::IK_STORE) {
        const AddressFact store = operand_address(
          instruction.second, addresses);
        if(store.root_kind == ARK_SLOT || store.root_kind == ARK_GLOBAL)
          visit_store_classes(store, instruction.type.storage_size, classes,
            first_slot_class, symbol_epochs, function_epoch,
            first_symbol_class,
            [definitions, block](std::size_t memory_class) {
              add_definition(definitions, memory_class, block);
            });
        if(store.root_kind == ARK_UNKNOWN || !store.known() ||
           has_unknown_pointer) unknown_write = true;
      }
      if(unknown_write && unknown_class != kNoIndex)
        add_definition(definitions, unknown_class, block);
    }
  }
}

bool place_memory_merges(
    const std::vector<std::vector<std::size_t> > & definitions,
    const std::vector<lowir_analysis::EdgeList> & frontiers,
    std::vector<MemoryClass> * classes,
    std::vector<std::vector<std::size_t> > * block_merges,
    Stats * stats)
{
  std::vector<std::uint32_t> definition_epochs(frontiers.size(), 0);
  std::vector<std::uint32_t> merge_epochs(frontiers.size(), 0);
  std::vector<std::size_t> work;
  std::vector<std::size_t> pending;
  std::uint32_t epoch = 0;
  std::size_t total = 0;
  for(std::size_t memory_class = 0;
      memory_class < classes->size(); ++memory_class) {
    if(definitions[memory_class].empty()) continue;
    ++epoch;
    if(epoch == 0) {
      std::fill(definition_epochs.begin(), definition_epochs.end(), 0);
      std::fill(merge_epochs.begin(), merge_epochs.end(), 0);
      epoch = 1;
    }
    work = definitions[memory_class];
    for(std::size_t index = 0; index < work.size(); ++index)
      definition_epochs[work[index]] = epoch;
    pending.clear();
    for(std::size_t cursor = 0; cursor < work.size(); ++cursor) {
      const lowir_analysis::EdgeList & frontier = frontiers[work[cursor]];
      for(std::size_t edge = 0; edge < frontier.size(); ++edge) {
        const std::size_t block = frontier[edge];
        if(merge_epochs[block] == epoch) continue;
        merge_epochs[block] = epoch;
        pending.push_back(block);
        if(definition_epochs[block] != epoch) work.push_back(block);
        if(total + pending.size() > kMaximumMergeVersions) {
          (*classes)[memory_class].enabled = false;
          pending.clear();
          if(stats) {
            ++stats->memory_gvn_budget_skips;
            ++stats->budget_skips;
          }
          break;
        }
      }
      if(!(*classes)[memory_class].enabled) break;
    }
    if(!(*classes)[memory_class].enabled) continue;
    total += pending.size();
    for(std::size_t index = 0; index < pending.size(); ++index)
      (*block_merges)[pending[index]].push_back(memory_class);
  }
  if(stats) stats->memory_gvn_merge_versions += total;
  return total != 0;
}

struct VersionChange
{
  std::size_t memory_class;
  std::size_t previous;
};

struct AvailableLoad
{
  Operand value;
  std::size_t key;
  std::size_t previous;
};

Instruction load_replacement(const Instruction & load,
                             const Operand & value)
{
  Instruction result;
  result.kind = Instruction::IK_COPY;
  result.dest = load.dest;
  result.type = load.type;
  result.first = value;
  result.debug_location = load.debug_location;
  return result;
}

}  // namespace

MemoryGVNSession::MemoryGVNSession(const lowir_model::Program & program)
  : boundaries_(program.symbol_names.size()),
    known_boundaries_(program.symbol_names.size(), 0),
    global_storage_(program.symbol_names.size(), lowir_model::GSM_DEFAULT),
    symbol_epochs_(program.symbol_names.size(), 0),
    symbol_classes_(program.symbol_names.size(), kNoIndex), function_epoch_(0)
{
  for(std::size_t index = 0;
      index < program.function_declarations.size(); ++index) {
    const lowir_model::SymbolId symbol =
      program.function_declarations[index].symbol;
    boundaries_[symbol] = program.function_declarations[index].boundary;
    known_boundaries_[symbol] = 1;
  }
  for(std::size_t index = 0; index < program.functions.size(); ++index) {
    const lowir_model::SymbolId symbol = program.functions[index].symbol;
    boundaries_[symbol] = program.functions[index].boundary;
    known_boundaries_[symbol] = 1;
  }
  for(std::size_t index = 0;
      index < program.global_declarations.size(); ++index)
    global_storage_[program.global_declarations[index].symbol] =
      program.global_declarations[index].storage;
  for(std::size_t index = 0; index < program.globals.size(); ++index)
    global_storage_[program.globals[index].symbol] =
      program.globals[index].storage;
}

lowir_model::FunctionBoundaryMetadata MemoryGVNSession::call_boundary(
    const Instruction & instruction) const
{
  lowir_model::FunctionBoundaryMetadata result = instruction.call_boundary;
  if(instruction.first.kind == Operand::OP_GLOBAL &&
     instruction.first.symbol < known_boundaries_.size() &&
     known_boundaries_[instruction.first.symbol])
    result = boundaries_[instruction.first.symbol];
  return result;
}

void MemoryGVNSession::begin_function()
{
  ++function_epoch_;
  if(function_epoch_ == 0) {
    std::fill(symbol_epochs_.begin(), symbol_epochs_.end(), 0);
    function_epoch_ = 1;
  }
}

void MemoryGVNSession::touch_symbol(lowir_model::SymbolId symbol)
{
  if(symbol_epochs_[symbol] == function_epoch_) return;
  symbol_epochs_[symbol] = function_epoch_;
  symbol_classes_[symbol] = kNoIndex;
}

bool MemoryGVNSession::eliminate_redundant_loads(
    Function * function, lowir_analysis::FunctionAnalysis * analysis, Stats * stats,
    bool preserve_value_lifetimes)
{
  const std::chrono::steady_clock::time_point started =
    stats ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
  if(stats) ++stats->memory_gvn_runs;
  if(function->blocks.empty()) return false;
  const std::vector<std::size_t> final_uses = preserve_value_lifetimes ? layout_last_uses(*function) : std::vector<std::size_t>();
  const lowir_eh_context::Context eh_context = lowir_eh_context::analyze(*function, analysis->graph());
  if(eh_context.has_eh && stats) {
    ++stats->memory_gvn_eh_functions;
    stats->memory_gvn_eh_barriers += eh_context.barrier_count;
  }
  if(eh_context.conflicting) {
    if(stats) {
      ++stats->memory_gvn_eh_skips;
      record_elapsed(stats, started);
    }
    return false;
  }

  begin_function();
  const std::vector<AddressFact> addresses = derive_addresses(*function);
  const std::vector<unsigned char> escaped_slots = find_escaped_slots(*function, addresses);
  LocationMap locations;
  std::vector<LocationKey> location_order;
  collect_load_locations(*function, addresses, escaped_slots,
    global_storage_, &locations, &location_order);

  std::vector<MemoryClass> classes;
  std::vector<std::size_t> first_slot_class(function->slot_names.size(), kNoIndex);
  for(std::size_t index = 0; index < location_order.size(); ++index) {
    LocationInfo & info = locations.find(location_order[index])->second;
    if(info.load_count < 2) continue;
    const LocationKey & key = location_order[index];
    const bool slot = key.root_kind == ARK_SLOT;
    const bool global = key.root_kind == ARK_GLOBAL;
    std::size_t previous = kNoIndex;
    if(slot) previous = first_slot_class[key.identity];
    else if(global) {
      touch_symbol(key.identity);
      previous = symbol_classes_[key.identity];
    }
    info.memory_class = classes.size();
    classes.push_back(MemoryClass{
      slot ? MCK_SLOT : global ? MCK_GLOBAL : MCK_UNKNOWN_POINTER,
      key.identity, key.address_identity,
      key.byte_offset, key.type_size, previous, key.precise,
      global && global_storage_[key.identity] == lowir_model::GSM_READONLY,
      true});
    if(slot) first_slot_class[key.identity] = info.memory_class;
    else if(global) symbol_classes_[key.identity] = info.memory_class;
  }
  if(classes.empty()) {
    record_elapsed(stats, started);
    return false;
  }
  if(classes.size() > kMaximumClasses) {
    if(stats) {
      ++stats->memory_gvn_budget_skips;
      ++stats->budget_skips;
      record_elapsed(stats, started);
    }
    return false;
  }

  std::size_t unknown_class = kNoIndex;
  for(std::size_t index = 0; index < classes.size(); ++index)
    if((classes[index].kind == MCK_GLOBAL && !classes[index].readonly) ||
       classes[index].kind == MCK_UNKNOWN_POINTER) {
      unknown_class = classes.size();
      classes.push_back(MemoryClass{
        MCK_UNKNOWN_GLOBAL, 0, lowir_model::ValueId(), 0, 0, kNoIndex,
        false, false, true});
      break;
  }
  const bool has_unknown_pointer = std::find_if(
    classes.begin(), classes.end(), [](const MemoryClass & memory_class) {
      return memory_class.kind == MCK_UNKNOWN_POINTER;
    }) != classes.end();
  const std::size_t eh_class = eh_context.has_eh ? classes.size() : kNoIndex;
  if(eh_class != kNoIndex)
    classes.push_back(MemoryClass{
      MCK_EH_BARRIER, 0, lowir_model::ValueId(), 0, 0, kNoIndex,
      false, false, true});
  std::vector<std::vector<std::size_t> > definitions(classes.size());
  collect_memory_definitions(*function, addresses, classes,
    first_slot_class, symbol_epochs_, function_epoch_, symbol_classes_,
    unknown_class, eh_class, has_unknown_pointer, eh_context,
    [this](const Instruction & instruction) {
      return call_boundary(instruction);
    }, &definitions);
  const std::vector<lowir_analysis::EdgeList> & frontiers = analysis->dominance_frontier();
  std::vector<std::vector<std::size_t> > block_merges(function->blocks.size());
  place_memory_merges(definitions, frontiers, &classes, &block_merges, stats);
  if(eh_class != kNoIndex && !classes[eh_class].enabled) {
    if(stats) {
      ++stats->memory_gvn_eh_skips;
      record_elapsed(stats, started);
    }
    return false;
  }
  if(unknown_class != kNoIndex && !classes[unknown_class].enabled)
    for(std::size_t index = 0; index < classes.size(); ++index)
      if((classes[index].kind == MCK_GLOBAL && !classes[index].readonly) ||
         classes[index].kind == MCK_UNKNOWN_POINTER)
        classes[index].enabled = false;
  if(stats) stats->memory_gvn_classes += classes.size() -
    (unknown_class == kNoIndex ? 0 : 1) -
    (eh_class == kNoIndex ? 0 : 1);

  const lowir_analysis::DominatorTree & dominators = analysis->dominator_tree();
  const std::vector<BlockEvent> events = dominator_events(dominators, analysis->dominator_children());
  std::vector<std::size_t> versions(classes.size(), 0);
  std::size_t next_version = 0;
  std::vector<VersionChange> version_changes;
  std::vector<std::size_t> version_marks(function->blocks.size(), 0);
  std::unordered_map<MemoryKey, std::size_t, MemoryKeyHash> key_ids;
  std::vector<std::size_t> heads;
  std::vector<AvailableLoad> available;
  std::vector<std::size_t> available_marks(function->blocks.size(), 0);
  key_ids.reserve(function->value_names.size() / 8 + 1);
  heads.reserve(function->value_names.size() / 8 + 1);
  available.reserve(function->value_names.size() / 8 + 1);
  const auto assign_version = [&versions, &version_changes, &next_version](
      std::size_t memory_class) {
    version_changes.push_back(VersionChange{
      memory_class, versions[memory_class]});
    versions[memory_class] = ++next_version;
  };
  bool changed = false;
  for(std::size_t event = 0; event < events.size(); ++event) {
    const std::size_t block = events[event].block;
    if(!events[event].entering) {
      while(available.size() > available_marks[block]) {
        const AvailableLoad & load = available.back();
        heads[load.key] = load.previous;
        available.pop_back();
      }
      while(version_changes.size() > version_marks[block]) {
        const VersionChange & change = version_changes.back();
        versions[change.memory_class] = change.previous;
        version_changes.pop_back();
      }
      continue;
    }
    version_marks[block] = version_changes.size();
    available_marks[block] = available.size();
    for(std::size_t merge = 0;
        merge < block_merges[block].size(); ++merge)
      if(classes[block_merges[block][merge]].enabled)
        assign_version(block_merges[block][merge]);
    if(eh_class != kNoIndex && eh_context.entry_barriers[block])
      assign_version(eh_class);
    std::vector<Instruction> & instructions = function->blocks[block].instructions;
    for(std::size_t index = 0; index < instructions.size(); ++index) {
      Instruction & instruction = instructions[index];
      if(stats) ++stats->instruction_visits;
      lowir_model::FunctionBoundaryMetadata boundary;
      const bool call = instruction.kind == Instruction::IK_CALL;
      if(call) boundary = call_boundary(instruction);
      if(eh_class != kNoIndex &&
         (lowir_eh_context::is_eh_instruction(instruction.kind) ||
          (call && boundary.unwind != lowir_model::CUM_NO))) {
        assign_version(eh_class);
        if(stats && call) ++stats->memory_gvn_eh_barriers;
      }
      if(instruction.kind == Instruction::IK_LOAD &&
         !instruction.volatile_access && !instruction.debug_location.present() &&
         scalar_load_type(instruction.type)) {
        const std::size_t memory_class = load_class(instruction, addresses, locations);
        if(memory_class != kNoIndex && classes[memory_class].enabled) {
          const std::size_t unknown_version =
            ((classes[memory_class].kind == MCK_GLOBAL &&
              !classes[memory_class].readonly) ||
             classes[memory_class].kind == MCK_UNKNOWN_POINTER) &&
            unknown_class != kNoIndex ?
              versions[unknown_class] : 0;
          const MemoryKey key = {
            memory_class, versions[memory_class], unknown_version,
            eh_class == kNoIndex ? 0 : versions[eh_class],
            instruction.type.kind, instruction.type.storage_size,
            instruction.type.alignment};
          if(stats) ++stats->memory_gvn_load_probes;
          const auto inserted = key_ids.emplace(key, heads.size());
          if(inserted.second) heads.push_back(kNoIndex);
          const std::size_t key_id = inserted.first->second;
          bool reusable = heads[key_id] != kNoIndex;
          if(reusable && preserve_value_lifetimes) {
            const Operand & available_value = available[heads[key_id]].value;
            reusable = available_value.kind == Operand::OP_TEMP &&
              available_value.value < final_uses.size() &&
              instruction.dest < final_uses.size() &&
              final_uses[available_value.value] != kNoIndex &&
              final_uses[available_value.value] >= final_uses[instruction.dest];
          }
          if(reusable) {
            instruction = load_replacement(instruction, available[heads[key_id]].value);
            changed = true;
            if(stats) {
              ++stats->memory_gvn_loads_eliminated;
              ++stats->rewrites;
            }
          } else {
            Operand value;
            value.kind = Operand::OP_TEMP;
            value.value = instruction.dest;
            available.push_back(AvailableLoad{
              value, key_id, heads[key_id]});
            heads[key_id] = available.size() - 1;
          }
        }
      }

      bool unknown_write = is_atomic_barrier(instruction.kind) ||
        is_other_memory_write(instruction.kind);
      if(call) {
        unknown_write = boundary.effects != lowir_model::CFXM_READNONE &&
          boundary.effects != lowir_model::CFXM_READONLY;
      }
      if(instruction.kind == Instruction::IK_STORE) {
        const AddressFact store = operand_address(instruction.second, addresses);
        if(store.root_kind == ARK_SLOT || store.root_kind == ARK_GLOBAL)
          visit_store_classes(store, instruction.type.storage_size, classes,
            first_slot_class, symbol_epochs_, function_epoch_,
            symbol_classes_, assign_version);
        if(store.root_kind == ARK_UNKNOWN || !store.known() ||
           has_unknown_pointer) unknown_write = true;
      }
      if(unknown_write && unknown_class != kNoIndex &&
         classes[unknown_class].enabled) {
        assign_version(unknown_class);
        if(stats) ++stats->memory_gvn_unknown_barriers;
      }
    }
  }
  record_elapsed(stats, started);
  return changed;
}

}  // namespace lowir_opt
