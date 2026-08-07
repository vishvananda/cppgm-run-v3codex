#include "pa15_lowir_model.h"

namespace cppgm
{
namespace pa15_lowir_detail
{

EmissionIdentityTable::EmissionIdentityTable()
	: path_slots_(32, kNoLowId), type_slots_(32, kNoLowId)
{}

IdentityPathId EmissionIdentityTable::InternPath(const Program& program,
	ScopeId owner, NameId terminal)
{
	program.BuildEmissionPath(owner, terminal, &path_scratch_);
	IdentityPathId path = kNoLowId;
	for (std::size_t i = 0; i < path_scratch_.size(); ++i)
	{
		const IdentityNameId name = InternName(program.names.Get(path_scratch_[i]));
		const IdentityPathKey key = { path, name };
		path = InternPathKey(key);
	}
	return path;
}

IdentityTypeId EmissionIdentityTable::InternType(const Program& program,
	TypeId type, std::vector<IdentityTypeId>& cache)
{
	if (cache.size() <= type)
		cache.resize(static_cast<std::size_t>(type) + 1, kNoLowId);
	if (cache[type] != kNoLowId) return cache[type];
	std::vector<PendingType> pending;
	pending.push_back(PendingType(type, false));
	while (!pending.empty())
	{
		PendingType& pending_type = pending.back();
		if (cache[pending_type.type] != kNoLowId)
		{
			pending.pop_back();
			continue;
		}
		const TypeRecord& source = program.types.Get(pending_type.type);
		if (!pending_type.expanded)
		{
			pending_type.expanded = true;
			PushTypeDependencies(program, source, pending_type.type, cache, pending);
			continue;
		}
		cache[pending_type.type] = InternTypeKey(
			MakeTypeKey(program, source, pending_type.type, cache));
		pending.pop_back();
	}
	return cache[type];
}

IdentityTypeId EmissionIdentityTable::InternFunctionSignature(
	const Program& program, TypeId type, std::vector<IdentityTypeId>& cache)
{
	const TypeRecord& source = program.types.Get(type);
	if (source.kind != TYPE_FUNCTION)
		throw std::logic_error("PA15 function identity has non-function type");
	IdentityTypeKey key;
	key.kind = TYPE_FUNCTION;
	key.variadic = source.variadic;
	key.cv = source.cv;
	key.ref_qualifier = source.ref_qualifier;
	const TypeId* parameters = program.types.Parameters(type);
	for (std::size_t i = 0; i < source.parameter_count; ++i)
		key.parameters.push_back(InternType(program, parameters[i], cache));
	return InternTypeKey(key);
}

std::size_t EmissionIdentityTable::StorageBytes() const
{
	std::size_t bytes = names_.StorageBytes() +
		path_records_.capacity() * sizeof(IdentityPathKey) +
		path_slots_.capacity() * sizeof(IdentityPathId) +
		type_records_.capacity() * sizeof(IdentityTypeKey) +
		type_slots_.capacity() * sizeof(IdentityTypeId);
	for (std::size_t i = 0; i < type_records_.size(); ++i)
		bytes += type_records_[i].parameters.capacity() * sizeof(IdentityTypeId);
	return bytes + path_scratch_.capacity() * sizeof(NameId);
}

IdentityNameId EmissionIdentityTable::InternName(const std::string& name)
{
	return names_.Intern(name);
}

bool EmissionIdentityTable::HasChild(TypeKind kind)
{
	return kind == TYPE_QUALIFIED || kind == TYPE_POINTER ||
		kind == TYPE_LVALUE_REFERENCE || kind == TYPE_RVALUE_REFERENCE ||
		kind == TYPE_ARRAY || kind == TYPE_FUNCTION ||
		kind == TYPE_MEMBER_POINTER;
}

void EmissionIdentityTable::PushDependency(TypeId dependency,
	std::vector<IdentityTypeId>& cache, std::vector<PendingType>& pending)
{
	if (cache.size() <= dependency)
		cache.resize(static_cast<std::size_t>(dependency) + 1, kNoLowId);
	if (cache[dependency] == kNoLowId)
		pending.push_back(PendingType(dependency, false));
}

void EmissionIdentityTable::PushTypeDependencies(const Program& program,
	const TypeRecord& source, TypeId type,
	std::vector<IdentityTypeId>& cache, std::vector<PendingType>& pending)
{
	if (HasChild(source.kind)) PushDependency(source.child, cache, pending);
	if (source.kind != TYPE_FUNCTION) return;
	const TypeId* parameters = program.types.Parameters(type);
	for (std::size_t i = 0; i < source.parameter_count; ++i)
		PushDependency(parameters[i], cache, pending);
}

IdentityTypeKey EmissionIdentityTable::MakeTypeKey(const Program& program,
	const TypeRecord& source, TypeId type,
	const std::vector<IdentityTypeId>& cache)
{
	IdentityTypeKey key;
	key.kind = source.kind;
	key.fundamental = source.fundamental;
	key.bound = source.kind == TYPE_MEMBER_POINTER ? 0 : source.bound;
	key.cv = source.cv;
	key.ref_qualifier = source.ref_qualifier;
	key.variadic = source.variadic;
	if (source.kind == TYPE_NAMED || source.kind == TYPE_MEMBER_POINTER)
	{
		const EntityRecord& entity = program.entities[source.entity];
		key.named = InternPath(program, entity.owner, entity.identity_name);
	}
	if (HasChild(source.kind)) key.child = cache[source.child];
	if (source.kind == TYPE_FUNCTION)
	{
		const TypeId* parameters = program.types.Parameters(type);
		key.parameters.reserve(source.parameter_count);
		for (std::size_t i = 0; i < source.parameter_count; ++i)
			key.parameters.push_back(cache[parameters[i]]);
	}
	return key;
}

IdentityPathId EmissionIdentityTable::InternPathKey(const IdentityPathKey& key)
{
	if ((path_records_.size() + 1) * 10 > path_slots_.size() * 7)
		RehashPaths(path_slots_.size() * 2);
	std::size_t slot = IdentityPathHash()(key) & (path_slots_.size() - 1);
	while (path_slots_[slot] != kNoLowId)
	{
		const IdentityPathId id = path_slots_[slot];
		if (path_records_[id] == key) return id;
		slot = (slot + 1) & (path_slots_.size() - 1);
	}
	if (path_records_.size() >= kNoLowId)
		throw std::runtime_error("too many PA15 identity paths");
	const IdentityPathId id = static_cast<IdentityPathId>(path_records_.size());
	path_records_.push_back(key);
	path_slots_[slot] = id;
	return id;
}

void EmissionIdentityTable::RehashPaths(std::size_t capacity)
{
	std::vector<IdentityPathId> replacement(capacity, kNoLowId);
	for (IdentityPathId id = 0; id < path_records_.size(); ++id)
	{
		std::size_t slot = IdentityPathHash()(path_records_[id]) & (capacity - 1);
		while (replacement[slot] != kNoLowId)
			slot = (slot + 1) & (capacity - 1);
		replacement[slot] = id;
	}
	path_slots_.swap(replacement);
}

IdentityTypeId EmissionIdentityTable::InternTypeKey(const IdentityTypeKey& key)
{
	if ((type_records_.size() + 1) * 10 > type_slots_.size() * 7)
		RehashTypes(type_slots_.size() * 2);
	std::size_t slot = IdentityTypeHash()(key) & (type_slots_.size() - 1);
	while (type_slots_[slot] != kNoLowId)
	{
		const IdentityTypeId id = type_slots_[slot];
		if (type_records_[id] == key) return id;
		slot = (slot + 1) & (type_slots_.size() - 1);
	}
	if (type_records_.size() >= kNoLowId)
		throw std::runtime_error("too many PA15 identity types");
	const IdentityTypeId id = static_cast<IdentityTypeId>(type_records_.size());
	type_records_.push_back(key);
	type_slots_[slot] = id;
	return id;
}

void EmissionIdentityTable::RehashTypes(std::size_t capacity)
{
	std::vector<IdentityTypeId> replacement(capacity, kNoLowId);
	for (IdentityTypeId id = 0; id < type_records_.size(); ++id)
	{
		std::size_t slot = IdentityTypeHash()(type_records_[id]) & (capacity - 1);
		while (replacement[slot] != kNoLowId)
			slot = (slot + 1) & (capacity - 1);
		replacement[slot] = id;
	}
	type_slots_.swap(replacement);
}

SymbolIdentityTable::SymbolIdentityTable() : slots_(32, kNoLowId) {}

bool SymbolIdentityTable::Find(const SymbolIdentity& key, SymbolId* symbol) const
{
	std::size_t slot = SymbolIdentityHash()(key) & (slots_.size() - 1);
	while (slots_[slot] != kNoLowId)
	{
		const SymbolId entry = slots_[slot];
		if (keys_[entry] == key)
		{
			*symbol = symbols_[entry];
			return true;
		}
		slot = (slot + 1) & (slots_.size() - 1);
	}
	return false;
}

void SymbolIdentityTable::Insert(const SymbolIdentity& key, SymbolId symbol)
{
	if ((keys_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	std::size_t slot = SymbolIdentityHash()(key) & (slots_.size() - 1);
	while (slots_[slot] != kNoLowId)
		slot = (slot + 1) & (slots_.size() - 1);
	const SymbolId entry = static_cast<SymbolId>(keys_.size());
	keys_.push_back(key);
	symbols_.push_back(symbol);
	slots_[slot] = entry;
}

std::size_t SymbolIdentityTable::StorageBytes() const
{
	return keys_.capacity() * sizeof(SymbolIdentity) +
		symbols_.capacity() * sizeof(SymbolId) +
		slots_.capacity() * sizeof(SymbolId);
}

void SymbolIdentityTable::Rehash(std::size_t capacity)
{
	std::vector<SymbolId> replacement(capacity, kNoLowId);
	for (std::size_t index = 0; index < keys_.size(); ++index)
	{
		const SymbolId entry = static_cast<SymbolId>(index);
		std::size_t slot = SymbolIdentityHash()(keys_[entry]) & (capacity - 1);
		while (replacement[slot] != kNoLowId)
			slot = (slot + 1) & (capacity - 1);
		replacement[slot] = entry;
	}
	slots_.swap(replacement);
}

StringCounterTable::StringCounterTable() : values_(1, 0) {}

std::size_t& StringCounterTable::operator[](const std::string& spelling)
{
	const InternedStringId id = names_.Intern(spelling);
	if (values_.size() <= id)
		values_.resize(static_cast<std::size_t>(id) + 1, 0);
	return values_[id];
}

void StringCounterTable::Clear()
{
	names_ = InternedStringTable();
	values_.assign(1, 0);
}

std::size_t StringCounterTable::StorageBytes() const
{
	return names_.StorageBytes() + values_.capacity() * sizeof(std::size_t);
}

std::size_t TypedStorageBytes(const TypedProgram& program)
{
	std::size_t bytes = program.symbols.capacity() * sizeof(Symbol) +
		program.global_declarations.capacity() * sizeof(GlobalDeclaration) +
		program.declarations.capacity() * sizeof(FunctionDeclaration) +
		program.globals.capacity() * sizeof(Global) +
		program.functions.capacity() * sizeof(Function) +
		program.call_arguments.capacity() * sizeof(Operand) +
		program.call_argument_references.capacity() * sizeof(std::uint8_t) +
		program.switch_case_values.capacity() * sizeof(std::int64_t) +
		program.switch_case_targets.capacity() * sizeof(BlockId) +
		program.literals.StorageBytes() +
		program.string_literal_symbols.capacity() * sizeof(SymbolId) +
		program.identities.StorageBytes() + program.symbol_index.StorageBytes() +
		program.symbol_name_counts.StorageBytes();
	for (std::size_t i = 0; i < program.symbols.size(); ++i)
		bytes += program.symbols[i].name.capacity() +
			program.symbols[i].object_name.capacity();
	for (std::size_t i = 0; i < program.globals.size(); ++i)
		bytes += program.globals[i].items.capacity() * sizeof(Global::DataItem);
	for (std::size_t i = 0; i < program.declarations.size(); ++i)
	{
		const FunctionDeclaration& declaration = program.declarations[i];
		bytes += declaration.parameters.capacity() * sizeof(Parameter);
		for (std::size_t p = 0; p < declaration.parameters.size(); ++p)
			bytes += declaration.parameters[p].name.capacity();
	}
	for (std::size_t i = 0; i < program.functions.size(); ++i)
	{
		const Function& function = program.functions[i];
		bytes += function.parameters.capacity() * sizeof(Parameter) +
			function.slots.capacity() * sizeof(Slot) +
			function.blocks.capacity() * sizeof(Block) +
			function.block_order.capacity() * sizeof(BlockId);
		for (std::size_t p = 0; p < function.parameters.size(); ++p)
			bytes += function.parameters[p].name.capacity();
		for (std::size_t s = 0; s < function.slots.size(); ++s)
			bytes += function.slots[s].name.capacity();
		for (std::size_t b = 0; b < function.blocks.size(); ++b)
		{
			const Block& block = function.blocks[b];
			bytes += block.label.capacity() +
				block.instructions.capacity() * sizeof(Instruction);
		}
	}
	return bytes;
}

}
}
