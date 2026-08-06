#pragma once

#include "pa11_model.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa15_lowir_detail
{

using namespace pa11;

enum LowKind
{
	LOW_INVALID,
	LOW_VOID,
	LOW_I8,
	LOW_U8,
	LOW_I16,
	LOW_U16,
	LOW_I32,
	LOW_U32,
	LOW_I64,
	LOW_F32,
	LOW_F64,
	LOW_F80,
	LOW_PTR,
	LOW_OBJECT
};

struct LowType
{
	LowKind kind;
	std::string text;
	std::size_t width;
	bool is_signed;

	LowType() : kind(LOW_INVALID), text("<invalid>"), width(0),
		is_signed(false) {}
	LowType(LowKind kind_value, const std::string& text_value,
		std::size_t width_value, bool signed_value)
		: kind(kind_value), text(text_value), width(width_value),
		  is_signed(signed_value) {}
};

LowType LowVoid() { return LowType(LOW_VOID, "void", 0, false); }
LowType LowI8() { return LowType(LOW_I8, "i8", 8, true); }
LowType LowU8() { return LowType(LOW_U8, "u8", 8, false); }
LowType LowI16() { return LowType(LOW_I16, "i16", 16, true); }
LowType LowU16() { return LowType(LOW_U16, "u16", 16, false); }
LowType LowI32() { return LowType(LOW_I32, "i32", 32, true); }
LowType LowU32() { return LowType(LOW_U32, "u32", 32, false); }
LowType LowI64() { return LowType(LOW_I64, "i64", 64, true); }
LowType LowU64() { return LowType(LOW_I64, "i64", 64, false); }
LowType LowF32() { return LowType(LOW_F32, "f32", 32, true); }
LowType LowF64() { return LowType(LOW_F64, "f64", 64, true); }
LowType LowF80() { return LowType(LOW_F80, "f80", 80, true); }
LowType LowPtr() { return LowType(LOW_PTR, "ptr", 64, false); }
LowType LowObject(std::size_t size, std::size_t alignment)
{
	return LowType(LOW_OBJECT, "obj<" + std::to_string(size) + "x" +
		std::to_string(alignment) + ">", size * 8, false);
}

bool SameType(const LowType& left, const LowType& right)
{
	return left.kind == right.kind;
}

bool IsInteger(const LowType& type)
{
	return type.kind == LOW_I8 || type.kind == LOW_U8 ||
		type.kind == LOW_I16 || type.kind == LOW_U16 ||
		type.kind == LOW_I32 || type.kind == LOW_U32 ||
		type.kind == LOW_I64;
}

bool IsFloating(const LowType& type)
{
	return type.kind == LOW_F32 || type.kind == LOW_F64 ||
		type.kind == LOW_F80;
}

const std::uint32_t kNoLowId = std::numeric_limits<std::uint32_t>::max();

template <typename Tag>
class LowId
{
public:
	LowId() : value_(kNoLowId) {}
	LowId(std::uint32_t value) : value_(value) {}
	operator std::uint32_t() const { return value_; }

private:
	std::uint32_t value_;
};

struct SymbolIdTag {};
struct ParameterIdTag {};
struct SlotIdTag {};
struct BlockIdTag {};
struct TempIdTag {};
typedef LowId<SymbolIdTag> SymbolId;
typedef LowId<ParameterIdTag> ParameterId;
typedef LowId<SlotIdTag> SlotId;
typedef LowId<BlockIdTag> BlockId;
typedef LowId<TempIdTag> TempId;

struct Operand
{
	enum Kind
	{
		NONE,
		TEMP,
		PARAMETER,
		SLOT,
		GLOBAL,
		FUNCTION,
		INTEGER,
		FLOATING
	} kind;
	std::uint32_t id;
	std::int64_t integer_value;
	std::string floating_spelling;
	LowType type;

	Operand() : kind(NONE), id(kNoLowId), integer_value(0) {}
	Operand(TempId id_value, const LowType& type_value)
		: kind(TEMP), id(id_value), integer_value(0), type(type_value) {}
	Operand(ParameterId id_value, const LowType& type_value)
		: kind(PARAMETER), id(id_value), integer_value(0), type(type_value) {}
	Operand(SlotId id_value, const LowType& type_value)
		: kind(SLOT), id(id_value), integer_value(0), type(type_value) {}
	Operand(Kind kind_value, SymbolId id_value, const LowType& type_value)
		: kind(kind_value), id(id_value), integer_value(0), type(type_value)
	{
		if (kind != GLOBAL && kind != FUNCTION)
			throw std::logic_error("invalid PA15 symbol operand kind");
	}
	Operand(std::int64_t value, const LowType& type_value)
		: kind(INTEGER), id(kNoLowId), integer_value(value), type(type_value) {}
	Operand(const std::string& spelling, const LowType& type_value)
		: kind(FLOATING), id(kNoLowId), integer_value(0),
		  floating_spelling(spelling), type(type_value) {}
};

enum LowOperation
{
	LOW_OP_NONE,
	LOW_OP_NEG,
	LOW_OP_BITNOT,
	LOW_OP_ADD,
	LOW_OP_SUB,
	LOW_OP_MUL,
	LOW_OP_DIV,
	LOW_OP_UDIV,
	LOW_OP_MOD,
	LOW_OP_UMOD,
	LOW_OP_AND,
	LOW_OP_OR,
	LOW_OP_XOR,
	LOW_OP_SHL,
	LOW_OP_SHR,
	LOW_OP_USHR,
	LOW_OP_EQ,
	LOW_OP_NE,
	LOW_OP_LT,
	LOW_OP_ULT,
	LOW_OP_LE,
	LOW_OP_ULE,
	LOW_OP_GT,
	LOW_OP_UGT,
	LOW_OP_GE,
	LOW_OP_UGE,
	LOW_OP_TRUNC,
	LOW_OP_SEXT,
	LOW_OP_ZEXT,
	LOW_OP_SITOFP,
	LOW_OP_UITOFP,
	LOW_OP_FPTOSI,
	LOW_OP_FPTOUI,
	LOW_OP_FPTRUNC,
	LOW_OP_FPEXT,
	LOW_OP_DECAY
};

struct Instruction
{
	enum Kind
	{
		CONST,
		COPY,
		ADDR,
		LOAD,
		STORE,
		INDEX,
		UNARY,
		BINARY,
		CMP,
		CONVERT,
		CALL,
		JUMP,
		BRANCH,
		SWITCH,
		RETURN_VALUE,
		RETURN_VOID
	} kind;
	TempId dest;
	LowOperation op;
	LowType type;
	LowType source_type;
	Operand first;
	Operand second;
	std::vector<Operand> arguments;
	std::vector<std::uint8_t> argument_references;
	std::vector<std::int64_t> case_values;
	std::vector<BlockId> case_targets;
	BlockId target;
	BlockId alternate;
	bool indirect;

	explicit Instruction(Kind kind_value)
		: kind(kind_value), dest(kNoLowId), op(LOW_OP_NONE), target(kNoLowId),
		  alternate(kNoLowId), indirect(false) {}
};

bool IsTerminator(const Instruction& instruction)
{
	return instruction.kind == Instruction::JUMP ||
		instruction.kind == Instruction::BRANCH ||
		instruction.kind == Instruction::SWITCH ||
		instruction.kind == Instruction::RETURN_VALUE ||
		instruction.kind == Instruction::RETURN_VOID;
}

struct Block
{
	std::string label;
	std::vector<Instruction> instructions;
	bool terminated;
	bool selected;

	explicit Block(const std::string& label_value)
		: label(label_value), terminated(false), selected(false) {}
};

struct Parameter
{
	std::string name;
	LowType type;
	bool reference;
};

struct Slot
{
	std::string name;
	LowType type;
};

struct Function
{
	SymbolId symbol;
	LowType result;
	std::vector<Parameter> parameters;
	std::vector<Slot> slots;
	std::vector<Block> blocks;
	std::vector<BlockId> block_order;
	bool entry;
	bool initializer;
	bool variadic;

	Function() : symbol(kNoLowId), entry(false), initializer(false), variadic(false) {}
};

struct FunctionDeclaration
{
	SymbolId symbol;
	LowType result;
	std::vector<Parameter> parameters;
	bool variadic;

	FunctionDeclaration() : symbol(kNoLowId), variadic(false) {}
};

struct GlobalDeclaration
{
	SymbolId symbol;
	LowType type;
	bool typed;

	GlobalDeclaration() : symbol(kNoLowId), typed(true) {}
};

struct Global
{
	enum InitializerKind { ZERO, INTEGER_VALUE, FLOATING_VALUE, ADDRESS_VALUE,
		STRUCTURED_VALUE } initializer_kind;
	struct DataItem
	{
		enum Kind { INTEGER_ITEM, FLOATING_ITEM, ADDRESS_ITEM, ZERO_ITEM } kind;
		LowType type;
		std::int64_t integer_value;
		std::string floating_spelling;
		SymbolId symbol;
		std::int64_t offset;
		std::size_t zero_bytes;

		DataItem() : kind(INTEGER_ITEM), integer_value(0), symbol(kNoLowId),
			offset(0), zero_bytes(0) {}
	};
	SymbolId symbol;
	LowType type;
	std::int64_t initializer;
	std::string floating_initializer;
	SymbolId address_symbol;
	std::int64_t address_offset;
	std::vector<DataItem> items;

	Global() : initializer_kind(ZERO), symbol(kNoLowId), initializer(0),
		address_symbol(kNoLowId), address_offset(0) {}
};

struct DynamicInitializer
{
	SymbolId destination;
	std::uint32_t expression;

	DynamicInitializer(SymbolId destination_value, std::uint32_t expression_value)
		: destination(destination_value), expression(expression_value) {}
};

struct Symbol
{
	enum Kind { FUNCTION_SYMBOL, GLOBAL_SYMBOL } kind;
	std::string name;
	std::string object_name;
	bool c_linkage;
	bool internal_linkage;
	bool nonthrowing;
	std::uint32_t source_type;
	bool declaration_emitted;
	bool definition_emitted;
	bool referenced;

	Symbol(Kind kind_value, const std::string& name_value,
		const std::string& object_name_value, bool c_linkage_value,
		bool internal_linkage_value, bool nonthrowing_value)
		: kind(kind_value), name(name_value), object_name(object_name_value),
		  c_linkage(c_linkage_value), internal_linkage(internal_linkage_value),
		  nonthrowing(nonthrowing_value), source_type(kNoLowId),
		  declaration_emitted(false), definition_emitted(false), referenced(false) {}
};

typedef std::uint32_t IdentityNameId;
typedef std::uint32_t IdentityPathId;
typedef std::uint32_t IdentityTypeId;

struct IdentityPathKey
{
	IdentityPathId parent;
	IdentityNameId name;

	bool operator==(const IdentityPathKey& other) const
	{
		return parent == other.parent && name == other.name;
	}
};

struct IdentityPathHash
{
	std::size_t operator()(const IdentityPathKey& key) const
	{
		return static_cast<std::size_t>(key.parent) * 16777619U ^ key.name;
	}
};

struct IdentityTypeKey
{
	TypeKind kind;
	FundamentalKind fundamental;
	IdentityTypeId child;
	IdentityPathId named;
	std::uint64_t bound;
	std::uint8_t cv;
	bool variadic;
	std::vector<IdentityTypeId> parameters;

	IdentityTypeKey()
		: kind(TYPE_FUNDAMENTAL), fundamental(FUND_VOID), child(kNoLowId),
		  named(kNoLowId), bound(0), cv(0), variadic(false) {}

	bool operator==(const IdentityTypeKey& other) const
	{
		return kind == other.kind && fundamental == other.fundamental &&
			child == other.child && named == other.named && bound == other.bound &&
			cv == other.cv && variadic == other.variadic &&
			parameters == other.parameters;
	}
};

struct IdentityTypeHash
{
	std::size_t operator()(const IdentityTypeKey& key) const
	{
		std::size_t hash = static_cast<std::size_t>(key.kind) * 16777619U ^
			static_cast<std::size_t>(key.fundamental);
		hash = hash * 16777619U ^ key.child;
		hash = hash * 16777619U ^ key.named;
		hash = hash * 16777619U ^ static_cast<std::size_t>(key.bound);
		hash = hash * 16777619U ^ key.cv;
		hash = hash * 16777619U ^ key.variadic;
		for (std::size_t i = 0; i < key.parameters.size(); ++i)
			hash = hash * 16777619U ^ key.parameters[i];
		return hash;
	}
};

class EmissionIdentityTable
{
private:
	struct PendingType
	{
		TypeId type;
		bool expanded;
		PendingType(TypeId type_value, bool expanded_value)
			: type(type_value), expanded(expanded_value) {}
	};

public:
	EmissionIdentityTable() : path_slots_(32, kNoLowId),
		type_slots_(32, kNoLowId) {}

	IdentityPathId InternPath(const Program& program, ScopeId owner,
		NameId terminal)
	{
		program.BuildEmissionPath(owner, terminal, &path_scratch_);
		IdentityPathId path = kNoLowId;
		for (std::size_t i = 0; i < path_scratch_.size(); ++i)
		{
			const IdentityNameId name = InternName(
				program.names.Get(path_scratch_[i]));
			const IdentityPathKey key = { path, name };
			path = InternPathKey(key);
		}
		return path;
	}

	IdentityTypeId InternType(const Program& program, TypeId type,
		std::vector<IdentityTypeId>& cache)
	{
		if (cache.size() <= type) cache.resize(static_cast<std::size_t>(type) + 1,
			kNoLowId);
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
				PushTypeDependencies(program, source, pending_type.type, cache,
					pending);
				continue;
			}
			cache[pending_type.type] = InternTypeKey(
				MakeTypeKey(program, source, pending_type.type, cache));
			pending.pop_back();
		}
		return cache[type];
	}

	IdentityTypeId InternFunctionSignature(const Program& program, TypeId type,
		std::vector<IdentityTypeId>& cache)
	{
		const TypeRecord& source = program.types.Get(type);
		if (source.kind != TYPE_FUNCTION)
			throw std::logic_error("PA15 function identity has non-function type");
		IdentityTypeKey key;
		key.kind = TYPE_FUNCTION;
		key.variadic = source.variadic;
		key.cv = source.cv;
		const TypeId* parameters = program.types.Parameters(type);
		for (std::size_t i = 0; i < source.parameter_count; ++i)
			key.parameters.push_back(InternType(program, parameters[i], cache));
		return InternTypeKey(key);
	}

	std::size_t StorageBytes() const
	{
		std::size_t bytes = names_.StorageBytes() +
			path_records_.capacity() * sizeof(IdentityPathKey) +
			path_slots_.capacity() * sizeof(IdentityPathId) +
			type_records_.capacity() * sizeof(IdentityTypeKey) +
			type_slots_.capacity() * sizeof(IdentityTypeId);
		for (std::size_t i = 0; i < type_records_.size(); ++i)
			bytes += type_records_[i].parameters.capacity() * sizeof(IdentityTypeId);
		bytes += path_scratch_.capacity() * sizeof(NameId);
		return bytes;
	}

private:
	IdentityNameId InternName(const std::string& name)
	{
		return names_.Intern(name);
	}

	static bool HasChild(TypeKind kind)
	{
		return kind == TYPE_QUALIFIED || kind == TYPE_POINTER ||
			kind == TYPE_LVALUE_REFERENCE || kind == TYPE_RVALUE_REFERENCE ||
			kind == TYPE_ARRAY || kind == TYPE_FUNCTION ||
			kind == TYPE_MEMBER_POINTER;
	}

	static void PushDependency(TypeId dependency,
		std::vector<IdentityTypeId>& cache, std::vector<PendingType>& pending)
	{
		if (cache.size() <= dependency)
			cache.resize(static_cast<std::size_t>(dependency) + 1, kNoLowId);
		if (cache[dependency] == kNoLowId)
			pending.push_back(PendingType(dependency, false));
	}

	static void PushTypeDependencies(const Program& program,
		const TypeRecord& source, TypeId type,
		std::vector<IdentityTypeId>& cache, std::vector<PendingType>& pending)
	{
		if (HasChild(source.kind)) PushDependency(source.child, cache, pending);
		if (source.kind != TYPE_FUNCTION) return;
		const TypeId* parameters = program.types.Parameters(type);
		for (std::size_t i = 0; i < source.parameter_count; ++i)
			PushDependency(parameters[i], cache, pending);
	}

	IdentityTypeKey MakeTypeKey(const Program& program,
		const TypeRecord& source, TypeId type,
		const std::vector<IdentityTypeId>& cache)
	{
		IdentityTypeKey key;
		key.kind = source.kind;
		key.fundamental = source.fundamental;
		key.bound = source.kind == TYPE_MEMBER_POINTER ? 0 : source.bound;
		key.cv = source.cv;
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

	IdentityPathId InternPathKey(const IdentityPathKey& key)
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

	void RehashPaths(std::size_t capacity)
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

	IdentityTypeId InternTypeKey(const IdentityTypeKey& key)
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

	void RehashTypes(std::size_t capacity)
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

	InternedStringTable names_;
	std::vector<IdentityPathKey> path_records_;
	std::vector<IdentityPathId> path_slots_;
	std::vector<IdentityTypeKey> type_records_;
	std::vector<IdentityTypeId> type_slots_;
	std::vector<NameId> path_scratch_;
};

struct SymbolIdentity
{
	Symbol::Kind kind;
	IdentityPathId path;
	IdentityTypeId signature;
	std::size_t internal_owner;

	bool operator==(const SymbolIdentity& other) const
	{
		return kind == other.kind && path == other.path &&
			signature == other.signature && internal_owner == other.internal_owner;
	}
};

struct SymbolIdentityHash
{
	std::size_t operator()(const SymbolIdentity& key) const
	{
		return static_cast<std::size_t>(key.kind) * 16777619U ^
			key.path * 257U ^ key.signature * 17U ^ key.internal_owner;
	}
};

class SymbolIdentityTable
{
public:
	SymbolIdentityTable() : slots_(32, kNoLowId) {}

	bool Find(const SymbolIdentity& key, SymbolId* symbol) const
	{
		std::size_t slot = SymbolIdentityHash()(key) & (slots_.size() - 1);
		while (slots_[slot] != kNoLowId)
		{
			const SymbolId id = slots_[slot];
			if (keys_[id] == key)
			{
				*symbol = id;
				return true;
			}
			slot = (slot + 1) & (slots_.size() - 1);
		}
		return false;
	}

	void Insert(const SymbolIdentity& key, SymbolId symbol)
	{
		if (symbol != keys_.size())
			throw std::logic_error("nonsequential PA15 symbol identity");
		if ((keys_.size() + 1) * 10 > slots_.size() * 7)
			Rehash(slots_.size() * 2);
		std::size_t slot = SymbolIdentityHash()(key) & (slots_.size() - 1);
		while (slots_[slot] != kNoLowId)
			slot = (slot + 1) & (slots_.size() - 1);
		keys_.push_back(key);
		slots_[slot] = symbol;
	}

	std::size_t StorageBytes() const
	{
		return keys_.capacity() * sizeof(SymbolIdentity) +
			slots_.capacity() * sizeof(SymbolId);
	}

private:
	void Rehash(std::size_t capacity)
	{
		std::vector<SymbolId> replacement(capacity, kNoLowId);
		for (std::size_t index = 0; index < keys_.size(); ++index)
		{
			const SymbolId id = static_cast<SymbolId>(index);
			std::size_t slot = SymbolIdentityHash()(keys_[id]) & (capacity - 1);
			while (replacement[slot] != kNoLowId)
				slot = (slot + 1) & (capacity - 1);
			replacement[slot] = id;
		}
		slots_.swap(replacement);
	}

	std::vector<SymbolIdentity> keys_;
	std::vector<SymbolId> slots_;
};

class StringCounterTable
{
public:
	StringCounterTable() : values_(1, 0) {}

	std::size_t& operator[](const std::string& spelling)
	{
		const InternedStringId id = names_.Intern(spelling);
		if (values_.size() <= id) values_.resize(static_cast<std::size_t>(id) + 1, 0);
		return values_[id];
	}

	void Clear()
	{
		names_ = InternedStringTable();
		values_.assign(1, 0);
	}

	std::size_t StorageBytes() const
	{
		return names_.StorageBytes() + values_.capacity() * sizeof(std::size_t);
	}

private:
	InternedStringTable names_;
	std::vector<std::size_t> values_;
};

struct TypedProgram
{
	std::vector<Symbol> symbols;
	std::vector<GlobalDeclaration> global_declarations;
	std::vector<FunctionDeclaration> declarations;
	std::vector<Global> globals;
	std::vector<Function> functions;
	EmissionIdentityTable identities;
	SymbolIdentityTable symbol_index;
	StringCounterTable symbol_name_counts;
	std::size_t string_literal_count;

	TypedProgram() : string_literal_count(0) {}
};

std::size_t TypedStorageBytes(const TypedProgram& program)
{
	std::size_t bytes = program.symbols.capacity() * sizeof(Symbol) +
		program.global_declarations.capacity() * sizeof(GlobalDeclaration) +
		program.declarations.capacity() * sizeof(FunctionDeclaration) +
		program.globals.capacity() * sizeof(Global) +
		program.functions.capacity() * sizeof(Function) +
		program.identities.StorageBytes() + program.symbol_index.StorageBytes() +
		program.symbol_name_counts.StorageBytes();
	for (std::size_t i = 0; i < program.symbols.size(); ++i)
		bytes += program.symbols[i].name.capacity() +
			program.symbols[i].object_name.capacity();
	for (std::size_t i = 0; i < program.globals.size(); ++i)
	{
		bytes += program.globals[i].floating_initializer.capacity() +
			program.globals[i].items.capacity() * sizeof(Global::DataItem);
		for (std::size_t item = 0; item < program.globals[i].items.size(); ++item)
			bytes += program.globals[i].items[item].floating_spelling.capacity();
	}
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
			for (std::size_t j = 0; j < block.instructions.size(); ++j)
			{
				const Instruction& instruction = block.instructions[j];
				bytes += instruction.arguments.capacity() * sizeof(Operand) +
					instruction.argument_references.capacity() * sizeof(std::uint8_t) +
					instruction.case_values.capacity() * sizeof(std::int64_t) +
					instruction.case_targets.capacity() * sizeof(BlockId) +
					instruction.first.floating_spelling.capacity() +
					instruction.second.floating_spelling.capacity();
				for (std::size_t a = 0; a < instruction.arguments.size(); ++a)
					bytes += instruction.arguments[a].floating_spelling.capacity();
			}
		}
	}
	return bytes;
}

}
}
