#include "pa15_lowir_model.h"

namespace cppgm
{
namespace pa15_lowir_detail
{

const char* LowOperationText(LowOperation operation)
{
	switch (operation)
	{
	case LOW_OP_NEG: return "neg";
	case LOW_OP_BITNOT: return "bitnot";
	case LOW_OP_BSWAP: return "bswap";
	case LOW_OP_ADD: return "add";
	case LOW_OP_SUB: return "sub";
	case LOW_OP_MUL: return "mul";
	case LOW_OP_DIV: return "div";
	case LOW_OP_UDIV: return "udiv";
	case LOW_OP_MOD: return "mod";
	case LOW_OP_UMOD: return "umod";
	case LOW_OP_AND: return "and";
	case LOW_OP_OR: return "or";
	case LOW_OP_XOR: return "xor";
	case LOW_OP_SHL: return "shl";
	case LOW_OP_SHR: return "shr";
	case LOW_OP_USHR: return "ushr";
	case LOW_OP_EQ: return "eq";
	case LOW_OP_NE: return "ne";
	case LOW_OP_LT: return "lt";
	case LOW_OP_ULT: return "ult";
	case LOW_OP_LE: return "le";
	case LOW_OP_ULE: return "ule";
	case LOW_OP_GT: return "gt";
	case LOW_OP_UGT: return "ugt";
	case LOW_OP_GE: return "ge";
	case LOW_OP_UGE: return "uge";
	case LOW_OP_TRUNC: return "trunc";
	case LOW_OP_SEXT: return "sext";
	case LOW_OP_ZEXT: return "zext";
	case LOW_OP_SITOFP: return "sitofp";
	case LOW_OP_UITOFP: return "uitofp";
	case LOW_OP_FPTOSI: return "fptosi";
	case LOW_OP_FPTOUI: return "fptoui";
	case LOW_OP_FPTRUNC: return "fptrunc";
	case LOW_OP_FPEXT: return "fpext";
	case LOW_OP_DECAY: return "decay";
	case LOW_OP_NONE: break;
	}
	throw std::logic_error("missing PA15 LowIR operation");
}

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

IdentityPathId EmissionIdentityTable::InternClassMemberPath(
	const Program& program, EntityId owner, NameId terminal)
{
	const EntityRecord& entity = program.entities[owner];
	const IdentityPathId parent = InternPath(program, entity.owner,
		entity.identity_name);
	const IdentityPathKey key = { parent,
		InternName(program.names.Get(terminal)) };
	return InternPathKey(key);
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

IdentityTypeId EmissionIdentityTable::InternTypeSequence(
	const Program& program, const TypeId* types, std::size_t count,
	std::vector<IdentityTypeId>& cache)
{
	IdentityTypeKey key;
	key.kind = TYPE_FUNCTION;
	key.parameters.reserve(count);
	for (std::size_t i = 0; i < count; ++i)
		key.parameters.push_back(InternType(program, types[i], cache));
	return InternTypeKey(key);
}

IdentityTypeId EmissionIdentityTable::InternBindingTemplateArguments(
	const Program& program, const BindingRecord& binding,
	std::vector<IdentityTypeId>& cache)
{
	if (binding.template_argument_count == 0) return kNoLowId;
	const std::size_t first = binding.template_argument_begin;
	const std::size_t count = binding.template_argument_count;
	if (first > program.template_arguments.size() ||
		count > program.template_arguments.size() - first)
		throw std::logic_error(
			"function template identity argument range is invalid");
	IdentityTypeKey key;
	key.kind = TYPE_FUNCTION;
	key.parameters.reserve(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		InternType(program, program.template_arguments[first + i], cache);
		key.parameters.push_back(InternStoredTemplateArgument(
			program, first + i, cache));
	}
	return InternTypeKey(key);
}

IdentityTypeId EmissionIdentityTable::InternEntityTemplateArguments(
	const Program& program, const EntityRecord& entity,
	std::vector<IdentityTypeId>& cache)
{
	if (entity.template_argument_count == 0) return kNoLowId;
	const std::size_t first = entity.template_argument_begin;
	const std::size_t count = entity.template_argument_count;
	if (first > program.template_arguments.size() ||
		count > program.template_arguments.size() - first)
		throw std::logic_error("class template identity argument range is invalid");
	IdentityTypeKey key;
	key.kind = TYPE_FUNCTION;
	key.parameters.reserve(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		InternType(program, program.template_arguments[first + i], cache);
		key.parameters.push_back(InternStoredTemplateArgument(
			program, first + i, cache));
	}
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
		kind == TYPE_BLOCK_POINTER ||
		kind == TYPE_LVALUE_REFERENCE || kind == TYPE_RVALUE_REFERENCE ||
		kind == TYPE_ARRAY || kind == TYPE_FUNCTION ||
		kind == TYPE_MEMBER_POINTER || kind == TYPE_VECTOR;
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
	if (source.kind == TYPE_NAMED || source.kind == TYPE_MEMBER_POINTER)
	{
		const EntityRecord& entity = program.entities[source.entity];
		if (entity.local_context != kNoBinding)
		{
			const TypeId context_type =
				program.bindings[entity.local_context].type;
			const TypeRecord& context = program.types.Get(context_type);
			if (context.kind != TYPE_FUNCTION)
				throw std::logic_error(
					"local type context has non-function type");
			const TypeId* parameters = program.types.Parameters(context_type);
			for (std::size_t i = 0; i < context.parameter_count; ++i)
				PushDependency(parameters[i], cache, pending);
		}
		const std::size_t first = entity.template_argument_begin;
		const std::size_t count = entity.template_argument_count;
		if (count != 0 &&
			(first > program.template_arguments.size() ||
			 count > program.template_arguments.size() - first))
			throw std::logic_error("class template type argument range is invalid");
		for (std::size_t i = 0; i < count; ++i)
			PushDependency(program.template_arguments[first + i], cache, pending);
	}
	if (source.kind == TYPE_FUNCTION)
	{
		const TypeId* parameters = program.types.Parameters(type);
		for (std::size_t i = 0; i < source.parameter_count; ++i)
			PushDependency(parameters[i], cache, pending);
	}
}

IdentityTypeKey EmissionIdentityTable::MakeTypeKey(const Program& program,
	const TypeRecord& source, TypeId type,
	std::vector<IdentityTypeId>& cache)
{
	IdentityTypeKey key;
	key.kind = source.kind;
	key.fundamental = source.fundamental;
	key.bound = source.kind == TYPE_MEMBER_POINTER ? 0 : source.bound;
	key.cv = source.cv;
	key.ref_qualifier = source.ref_qualifier;
	key.variadic = source.variadic;
	key.zero_length_array = source.zero_length_array;
	if (source.kind == TYPE_NAMED || source.kind == TYPE_MEMBER_POINTER)
	{
		const EntityRecord& entity = program.entities[source.entity];
		key.named = InternPath(program, entity.owner, entity.identity_name);
		if (entity.local_context != kNoBinding)
		{
			const BindingRecord& context =
				program.bindings[entity.local_context];
			key.local_context = InternPath(program, context.owner, context.name);
			key.local_context_signature =
				InternFunctionSignature(program, context.type, cache);
			key.local_ordinal = entity.local_name_ordinal;
		}
		const std::size_t first = entity.template_argument_begin;
		for (std::size_t i = 0; i < entity.template_argument_count; ++i)
			key.parameters.push_back(InternStoredTemplateArgument(
				program, first + i, cache));
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

IdentityTypeId EmissionIdentityTable::InternStoredTemplateArgument(
	const Program& program, std::size_t argument,
	const std::vector<IdentityTypeId>& cache)
{
	if (argument >= program.template_arguments.size())
		throw std::logic_error("template identity argument index is invalid");
	const TypeId type = program.template_arguments[argument];
	if (type >= cache.size() || cache[type] == kNoLowId)
		throw std::logic_error("template identity argument type is unresolved");
	if (argument >= program.canonical_template_arguments.size() ||
		program.canonical_template_arguments[argument].kind ==
			TEMPLATE_ARGUMENT_TYPE)
		return cache[type];
	const TemplateArgument& source =
		program.canonical_template_arguments[argument];
	IdentityTypeKey key;
	key.kind = TYPE_INVALID;
	key.child = cache[type];
	key.bound = source.value_binding == kNoBinding ?
		static_cast<std::uint64_t>(source.value) :
		(1ULL << 63) | source.value_binding;
	return InternTypeKey(key);
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

lowir_model::StringId TypedProgram::InternUniqueSymbolName(
	const std::string& proposed)
{
	const lowir_model::StringId base = strings.intern(proposed);
	const std::uint32_t index = base;
	if (symbol_name_counts.size() <= index)
		symbol_name_counts.resize(static_cast<std::size_t>(index) + 1, 0);
	std::uint32_t& ordinal = symbol_name_counts[index];
	if (ordinal == std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many typed LowIR symbol name collisions");
	++ordinal;
	return ordinal == 1 ? base : strings.intern(
		proposed + "__sym" + std::to_string(ordinal));
}

std::size_t TypedStorageBytes(const TypedProgram& program)
{
	std::size_t bytes = program.symbols.capacity() * sizeof(Symbol) +
		program.global_declarations.capacity() * sizeof(GlobalDeclaration) +
		program.declarations.capacity() * sizeof(FunctionDeclaration) +
		program.globals.capacity() * sizeof(Global) +
		program.functions.capacity() * sizeof(Function) +
		program.object_aliases.capacity() * sizeof(ObjectAlias) +
		program.call_arguments.capacity() * sizeof(Operand) +
		program.call_argument_references.capacity() * sizeof(std::uint8_t) +
		program.exception_filter_types.capacity() * sizeof(SymbolId) +
		program.switch_case_values.capacity() * sizeof(std::int64_t) +
		program.switch_case_targets.capacity() * sizeof(BlockId) +
		program.strings.storage_bytes() +
		program.string_literal_symbols.capacity() * sizeof(SymbolId) +
		program.identities.StorageBytes() + program.symbol_index.StorageBytes() +
		program.symbol_name_counts.capacity() * sizeof(std::uint32_t);
	for (std::size_t i = 0; i < program.globals.size(); ++i)
		bytes += program.globals[i].items.capacity() * sizeof(Global::DataItem);
	for (std::size_t i = 0; i < program.declarations.size(); ++i)
	{
		const FunctionDeclaration& declaration = program.declarations[i];
		bytes += declaration.parameters.capacity() * sizeof(Parameter);
	}
	for (std::size_t i = 0; i < program.functions.size(); ++i)
	{
		const Function& function = program.functions[i];
		bytes += function.parameters.capacity() * sizeof(Parameter) +
			function.slots.capacity() * sizeof(Slot) +
			function.blocks.capacity() * sizeof(Block) +
			function.block_order.capacity() * sizeof(BlockId) +
			function.block_presentations.capacity() *
				sizeof(BlockPresentationName) +
			function.block_presentation_order.capacity() *
				sizeof(std::uint32_t) +
			function.generated_name_reservations.storage_bytes();
		for (std::size_t b = 0; b < function.blocks.size(); ++b)
		{
			const Block& block = function.blocks[b];
			bytes += block.instructions.capacity() * sizeof(Instruction);
		}
	}
	return bytes;
}

}
}
