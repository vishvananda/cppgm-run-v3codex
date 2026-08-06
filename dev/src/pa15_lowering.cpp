#include "pa15_lowering.h"

#include "abi_mangle.h"
#include "pa11_model.h"
#include "pa12_semantic.h"
#include "pa12_semantic_model.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <ostream>
#include <streambuf>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace
{

using namespace pa11;
using namespace pa12_semantic_detail;

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
	LOW_PTR
};

struct LowType
{
	LowKind kind;
	const char* text;
	std::size_t width;
	bool is_signed;

	LowType() : kind(LOW_INVALID), text("<invalid>"), width(0),
		is_signed(false) {}
	LowType(LowKind kind_value, const char* text_value,
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
	LOW_OP_FPEXT
};

struct Instruction
{
	enum Kind
	{
		ADDR,
		LOAD,
		STORE,
		UNARY,
		BINARY,
		CMP,
		CONVERT,
		CALL,
		JUMP,
		BRANCH,
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
		instruction.kind == Instruction::RETURN_VALUE ||
		instruction.kind == Instruction::RETURN_VOID;
}

struct Block
{
	std::string label;
	std::vector<Instruction> instructions;
	bool terminated;

	explicit Block(const std::string& label_value)
		: label(label_value), terminated(false) {}
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
	bool entry;
	bool variadic;

	Function() : symbol(kNoLowId), entry(false), variadic(false) {}
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

	GlobalDeclaration() : symbol(kNoLowId) {}
};

struct Global
{
	SymbolId symbol;
	LowType type;
	std::int64_t initializer;
	bool zero_initialize;

	Global() : symbol(kNoLowId), initializer(0), zero_initialize(true) {}
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

	Symbol(Kind kind_value, const std::string& name_value,
		const std::string& object_name_value, bool c_linkage_value,
		bool internal_linkage_value, bool nonthrowing_value)
		: kind(kind_value), name(name_value), object_name(object_name_value),
		  c_linkage(c_linkage_value), internal_linkage(internal_linkage_value),
		  nonthrowing(nonthrowing_value), source_type(kNoLowId),
		  declaration_emitted(false), definition_emitted(false) {}
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
};

std::string StripOperationPrefix(const std::string& operation)
{
	const std::size_t colon = operation.rfind(':');
	return colon == std::string::npos ? operation : operation.substr(colon + 1);
}

std::string SanitizeSymbol(const std::string& name)
{
	std::string result;
	result.reserve(name.size() + 8);
	for (std::size_t i = 0; i < name.size(); ++i)
	{
		if (i + 1 < name.size() && name[i] == ':' && name[i + 1] == ':')
		{
			result += "__";
			++i;
		}
		else
		{
			const unsigned char c = static_cast<unsigned char>(name[i]);
			result += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '_' ? static_cast<char>(c) : '_';
		}
	}
	if (result.empty()) result = "anonymous";
	return result;
}

class NodeChildren
{
public:
	NodeChildren() : count_(0) {}

	void Push(std::uint32_t child)
	{
		if (count_ < kInlineCount) inline_[count_] = child;
		else overflow_.push_back(child);
		++count_;
	}

	std::size_t size() const { return count_; }
	bool empty() const { return count_ == 0; }
	std::uint32_t operator[](std::size_t index) const
	{
		return index < kInlineCount ? inline_[index] : overflow_[index - kInlineCount];
	}

private:
	static const std::size_t kInlineCount = 8;
	std::uint32_t inline_[kInlineCount];
	std::vector<std::uint32_t> overflow_;
	std::size_t count_;
};

class GraphLowerer
{
public:
	GraphLowerer(const SemanticGraphView& graph, TypedProgram& output,
		LowIRLoweringStats* stats, std::size_t source_ordinal)
		: graph_(graph), program_(graph.program), arena_(graph.arena),
		  output_(output), stats_(stats), function_(0), current_block_(0),
		  current_result_(LowVoid()), temp_counter_(0), block_counter_(0),
		  source_ordinal_(source_ordinal)
	{
		function_symbols_.resize(program_.bindings.size(), kNoLowId);
		global_symbols_.resize(program_.bindings.size(), kNoLowId);
		function_definition_.resize(program_.bindings.size(), kNoDumpEdge);
		function_declaration_.resize(program_.bindings.size(), kNoDumpEdge);
		global_node_.resize(program_.bindings.size(), kNoDumpEdge);
	}

	void Lower()
	{
		ScanTop(graph_.root);
		EmitTop(graph_.root);
	}

private:
	NodeChildren Children(std::uint32_t node) const
	{
		NodeChildren result;
		for (std::uint32_t edge = arena_.nodes[node].first_edge;
			edge != kNoDumpEdge; edge = arena_.edges[edge].next)
			result.Push(arena_.edges[edge].child);
		return result;
	}

	LowType LowerType(TypeId type) const
	{
		const TypeRecord* record = &program_.types.Get(type);
		while (record->kind == TYPE_QUALIFIED)
		{
			type = record->child;
			record = &program_.types.Get(type);
		}
		if (record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE || record->kind == TYPE_POINTER ||
			record->kind == TYPE_ARRAY || record->kind == TYPE_FUNCTION ||
			record->kind == TYPE_MEMBER_POINTER) return LowPtr();
		if (record->kind == TYPE_NAMED)
		{
			const EntityRecord& entity = program_.entities[record->entity];
			if ((entity.flavor == NAMED_ENUM || entity.flavor == NAMED_ENUM_CLASS) &&
				entity.underlying != kNoType) return LowerType(entity.underlying);
			throw std::runtime_error("PA15 scalar checkpoint cannot lower class type");
		}
		if (record->kind != TYPE_FUNDAMENTAL)
			throw std::runtime_error("invalid PA15 scalar type");
		switch (record->fundamental)
		{
		case FUND_BOOL: return LowU8();
		case FUND_CHAR: case FUND_SIGNED_CHAR: return LowI8();
		case FUND_UNSIGNED_CHAR: return LowU8();
		case FUND_SHORT_INT: return LowI16();
		case FUND_UNSIGNED_SHORT_INT: return LowU16();
		case FUND_INT: return LowI32();
		case FUND_UNSIGNED_INT: return LowU32();
		case FUND_LONG_INT: case FUND_LONG_LONG_INT:
		case FUND_WCHAR_T: case FUND_CHAR16_T: case FUND_CHAR32_T:
			return LowI64();
		case FUND_UNSIGNED_LONG_INT: case FUND_UNSIGNED_LONG_LONG_INT:
			return LowU64();
		case FUND_FLOAT: return LowF32();
		case FUND_DOUBLE: return LowF64();
		case FUND_LONG_DOUBLE: return LowF80();
		case FUND_VOID: return LowVoid();
		case FUND_NULLPTR_T: return LowPtr();
		}
		throw std::runtime_error("unsupported PA15 fundamental type");
	}

	bool IsReferenceType(TypeId type) const
	{
		const TypeRecord& record = program_.types.Get(type);
		return record.kind == TYPE_LVALUE_REFERENCE ||
			record.kind == TYPE_RVALUE_REFERENCE;
	}

	abi_mangle::AbiType MakeAbiType(TypeId type) const
	{
		using namespace abi_mangle;
		std::vector<AbiTypeModifier> modifiers;
		const TypeRecord* record = &program_.types.Get(type);
		while (record->kind == TYPE_QUALIFIED || record->kind == TYPE_POINTER ||
			record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE || record->kind == TYPE_ARRAY)
		{
			AbiTypeModifier modifier;
			if (record->kind == TYPE_QUALIFIED)
			{
				modifier.kind = ABI_TYPE_CV;
				modifier.is_const = (record->cv & CV_CONST) != 0;
				modifier.is_volatile = (record->cv & CV_VOLATILE) != 0;
			}
			else if (record->kind == TYPE_ARRAY)
			{
				modifier.kind = ABI_TYPE_ARRAY;
				modifier.array_bound.kind = ABI_ARRAY_BOUND_VALUE;
				modifier.array_bound.value = std::to_string(record->bound);
			}
			else modifier.kind = record->kind == TYPE_POINTER ? ABI_TYPE_POINTER :
				record->kind == TYPE_LVALUE_REFERENCE ? ABI_TYPE_LVALUE_REFERENCE :
				ABI_TYPE_RVALUE_REFERENCE;
			modifiers.push_back(modifier);
			type = record->child;
			record = &program_.types.Get(type);
		}
		abi_mangle::AbiType result;
		result.modifiers.swap(modifiers);
		if (record->kind == TYPE_FUNCTION)
		{
			result.kind = ABI_TYPE_FUNCTION;
			result.types.push_back(MakeAbiType(record->child));
			const TypeId* parameters = program_.types.Parameters(type);
			for (std::size_t i = 0; i < record->parameter_count; ++i)
				result.types.push_back(MakeAbiType(parameters[i]));
			result.variadic = record->variadic;
			return result;
		}
		if (record->kind == TYPE_NAMED)
		{
			result.kind = ABI_TYPE_NAMED;
			result.name = program_.names.Get(program_.entities[record->entity].name);
			return result;
		}
		if (record->kind != TYPE_FUNDAMENTAL)
			throw std::runtime_error("unsupported ABI type in PA15");
		result.kind = ABI_TYPE_BUILTIN;
		switch (record->fundamental)
		{
		case FUND_VOID: result.name = "void"; break;
		case FUND_BOOL: result.name = "bool"; break;
		case FUND_CHAR: result.name = "char"; break;
		case FUND_SIGNED_CHAR: result.name = "schar"; break;
		case FUND_UNSIGNED_CHAR: result.name = "uchar"; break;
		case FUND_SHORT_INT: result.name = "short"; break;
		case FUND_UNSIGNED_SHORT_INT: result.name = "ushort"; break;
		case FUND_INT: result.name = "int"; break;
		case FUND_UNSIGNED_INT: result.name = "uint"; break;
		case FUND_LONG_INT: result.name = "long"; break;
		case FUND_UNSIGNED_LONG_INT: result.name = "ulong"; break;
		case FUND_LONG_LONG_INT: result.name = "longlong"; break;
		case FUND_UNSIGNED_LONG_LONG_INT: result.name = "ulonglong"; break;
		case FUND_FLOAT: result.name = "float"; break;
		case FUND_DOUBLE: result.name = "double"; break;
		case FUND_LONG_DOUBLE: result.name = "longdouble"; break;
		case FUND_WCHAR_T: result.name = "wchar"; break;
		case FUND_CHAR16_T: result.name = "char16"; break;
		case FUND_CHAR32_T: result.name = "char32"; break;
		case FUND_NULLPTR_T: result.name = "ulong"; break;
		}
		return result;
	}

	std::string MangleFunction(const DumpNode& node) const
	{
		using namespace abi_mangle;
		const std::string qualified = program_.names.Get(node.text);
		if (qualified == "main") return std::string();
		const BindingRecord& binding = program_.bindings[node.binding];
		if (binding.language_linkage == LANGUAGE_LINKAGE_C &&
			binding.storage_class != STORAGE_CLASS_STATIC)
			return program_.names.Get(binding.name);
		AbiFactFile file;
		file.cases.push_back(AbiFactCase());
		AbiFactRecord target;
		target.set_kind(ABI_FACT_RECORD_TARGET);
		target.target.kind = ABI_TARGET_FACT_FUNCTION;
		target.target.internal_linkage = binding.storage_class == STORAGE_CLASS_STATIC;
		target.target.function.kind = ABI_FUNCTION_TARGET_PATH;
		target.target.function.qualified_name = qualified;
		file.cases[0].records.push_back(target);
		const TypeRecord& function_type = program_.types.Get(node.type);
		const TypeId* parameters = program_.types.Parameters(node.type);
		for (std::size_t i = 0; i < function_type.parameter_count; ++i)
		{
			AbiFactRecord parameter;
			parameter.set_kind(ABI_FACT_RECORD_FUNCTION);
			parameter.function.kind = ABI_FUNCTION_RECORD_PARAMETER;
			parameter.function.type = MakeAbiType(parameters[i]);
			file.cases[0].records.push_back(parameter);
		}
		if (function_type.variadic)
		{
			AbiFactRecord variadic;
			variadic.set_kind(ABI_FACT_RECORD_FUNCTION);
			variadic.function.kind = ABI_FUNCTION_RECORD_VARIADIC;
			file.cases[0].records.push_back(variadic);
		}
		std::string result = mangle_fact_file(file);
		if (!result.empty() && result[result.size() - 1] == '\n') result.resize(result.size() - 1);
		return result;
	}

	std::string MangleVariable(const DumpNode& node) const
	{
		using namespace abi_mangle;
		const BindingRecord& binding = program_.bindings[node.binding];
		if (binding.language_linkage == LANGUAGE_LINKAGE_C &&
			binding.storage_class != STORAGE_CLASS_STATIC)
			return program_.names.Get(binding.name);
		AbiFactFile file;
		file.cases.push_back(AbiFactCase());
		AbiFactRecord target;
		target.set_kind(ABI_FACT_RECORD_TARGET);
		target.target.kind = ABI_TARGET_FACT_VARIABLE;
		target.target.internal_linkage = binding.storage_class == STORAGE_CLASS_STATIC;
		target.target.qualified_name = program_.names.Get(
			binding.qualified_name != 0 ? binding.qualified_name : node.text);
		file.cases[0].records.push_back(target);
		std::string result = mangle_fact_file(file);
		if (!result.empty() && result[result.size() - 1] == '\n') result.resize(result.size() - 1);
		return result;
	}

	SymbolId InternSymbol(const DumpNode& node, Symbol::Kind kind,
		const std::string& proposed_name, const std::string& object_name)
	{
		const BindingRecord& binding = program_.bindings[node.binding];
		const bool internal = binding.storage_class == STORAGE_CLASS_STATIC;
		const bool c_linkage =
			binding.language_linkage == LANGUAGE_LINKAGE_C;
		SymbolIdentity identity;
		identity.kind = kind;
		identity.path = output_.identities.InternPath(program_,
			c_linkage && !internal ? program_.GlobalScope() : binding.owner,
			binding.name);
		identity.signature = kind == Symbol::FUNCTION_SYMBOL && !c_linkage ?
			output_.identities.InternFunctionSignature(program_, node.type,
				identity_type_cache_) : kNoLowId;
		identity.internal_owner = internal ? source_ordinal_ + 1 : 0;
		const IdentityTypeId source_type = output_.identities.InternType(
			program_, node.type, identity_type_cache_);
		SymbolId found = kNoLowId;
		if (output_.symbol_index.Find(identity, &found))
		{
			Symbol& symbol = output_.symbols[found];
			if (symbol.source_type != source_type)
				throw std::runtime_error("conflicting cross-source PA15 symbol type");
			if (!symbol.object_name.empty() && !object_name.empty() &&
				symbol.object_name != object_name)
				throw std::logic_error("conflicting PA15 ABI object identity");
			symbol.nonthrowing = symbol.nonthrowing || binding.nonthrowing;
			return found;
		}
		if (output_.symbols.size() >= kNoLowId)
			throw std::runtime_error("too many PA15 emission symbols");
		std::size_t& count = output_.symbol_name_counts[proposed_name];
		const std::string name = count++ == 0 ? proposed_name :
			proposed_name + "__sym" + std::to_string(count);
		const SymbolId symbol = static_cast<SymbolId>(output_.symbols.size());
		output_.symbols.push_back(Symbol(kind, name, object_name, c_linkage,
			internal, binding.nonthrowing));
		output_.symbols.back().source_type = source_type;
		output_.symbol_index.Insert(identity, symbol);
		return symbol;
	}

	void RegisterFunction(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.binding == kNoBinding) return;
		if (function_symbols_[record.binding] == kNoLowId)
		{
			const std::string base = SanitizeSymbol(program_.names.Get(record.text));
			std::size_t& count = overload_counts_[base];
			++count;
			const std::string name = count == 1 ? base :
				base + "__ov" + std::to_string(count);
			function_symbols_[record.binding] = InternSymbol(record,
				Symbol::FUNCTION_SYMBOL, name, MangleFunction(record));
		}
		if (record.kind == DUMP_FUNCTION_DEFINITION)
			function_definition_[record.binding] = node;
		else if (function_declaration_[record.binding] == kNoDumpEdge)
			function_declaration_[record.binding] = node;
	}

	void ScanTop(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.kind == DUMP_FUNCTION_DEFINITION ||
			record.kind == DUMP_FUNCTION_DECLARATION)
		{
			RegisterFunction(node);
			return;
		}
		if (record.kind == DUMP_VARIABLE && record.binding != kNoBinding)
		{
			const BindingId canonical =
				program_.bindings[record.binding].canonical;
			if (global_symbols_[canonical] == kNoLowId)
			{
				const std::string name = SanitizeSymbol(program_.names.Get(
					program_.bindings[record.binding].qualified_name != 0 ?
					program_.bindings[record.binding].qualified_name : record.text));
				global_symbols_[canonical] = InternSymbol(record,
					Symbol::GLOBAL_SYMBOL, name, MangleVariable(record));
			}
			global_symbols_[record.binding] = global_symbols_[canonical];
			const bool declaration_only = Children(node).empty() &&
				program_.bindings[record.binding].storage_class == STORAGE_CLASS_EXTERN;
			if (!declaration_only || global_node_[canonical] == kNoDumpEdge)
				global_node_[canonical] = node;
			return;
		}
		if (record.kind != DUMP_TRANSLATION_UNIT && record.kind != DUMP_NAMESPACE)
			return;
		const NodeChildren children = Children(node);
		for (std::size_t i = 0; i < children.size(); ++i) ScanTop(children[i]);
	}

	void EmitTop(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.kind == DUMP_FUNCTION_DECLARATION)
		{
			if (record.binding != kNoBinding &&
				function_definition_[record.binding] == kNoDumpEdge &&
				function_declaration_[record.binding] == node)
			{
				Symbol& symbol = output_.symbols[function_symbols_[record.binding]];
				if (!symbol.declaration_emitted)
				{
					output_.declarations.push_back(LowerDeclaration(node));
					symbol.declaration_emitted = true;
				}
			}
			return;
		}
		if (record.kind == DUMP_FUNCTION_DEFINITION)
		{
			if (record.binding != kNoBinding &&
				function_definition_[record.binding] == node)
			{
				Symbol& symbol = output_.symbols[function_symbols_[record.binding]];
				if (symbol.definition_emitted)
					throw std::runtime_error("duplicate cross-source function definition");
				output_.functions.push_back(LowerFunction(node));
				symbol.definition_emitted = true;
			}
			return;
		}
		if (record.kind == DUMP_VARIABLE)
		{
			if (record.binding != kNoBinding)
			{
				const BindingId canonical =
					program_.bindings[record.binding].canonical;
				if (global_node_[canonical] == node)
				{
					const bool declaration_only = Children(node).empty() &&
						program_.bindings[record.binding].storage_class ==
							STORAGE_CLASS_EXTERN;
					if (declaration_only)
					{
						Symbol& symbol = output_.symbols[global_symbols_[canonical]];
						if (!symbol.declaration_emitted)
						{
							output_.global_declarations.push_back(
								LowerGlobalDeclaration(node));
							symbol.declaration_emitted = true;
						}
					}
					else
					{
						Symbol& symbol = output_.symbols[global_symbols_[canonical]];
						if (symbol.definition_emitted)
							throw std::runtime_error(
								"duplicate cross-source global definition");
						output_.globals.push_back(LowerGlobal(node));
						symbol.definition_emitted = true;
					}
				}
			}
			return;
		}
		if (record.kind != DUMP_TRANSLATION_UNIT && record.kind != DUMP_NAMESPACE)
			return;
		const NodeChildren children = Children(node);
		for (std::size_t i = 0; i < children.size(); ++i) EmitTop(children[i]);
	}

	void FillBoundary(std::uint32_t node, std::vector<Parameter>* parameters,
		LowType* result, bool* variadic) const
	{
		const DumpNode& record = arena_.nodes[node];
		const TypeRecord& function_type = program_.types.Get(record.type);
		*result = LowerType(function_type.child);
		*variadic = function_type.variadic;
		const NodeChildren children = Children(node);
		std::size_t parameter_index = 0;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& child = arena_.nodes[children[i]];
			if (child.kind != DUMP_PARAMETER) continue;
			Parameter parameter;
			parameter.name = child.text == 0 ? std::string() : program_.names.Get(child.text);
			if (parameter.name.empty()) parameter.name = "__param" +
				std::to_string(parameter_index);
			parameter.type = LowerType(child.type);
			const TypeId* source_parameters = program_.types.Parameters(record.type);
			parameter.reference = parameter_index < function_type.parameter_count &&
				IsReferenceType(source_parameters[parameter_index]);
			parameters->push_back(parameter);
			++parameter_index;
		}
		const TypeId* source_parameters = program_.types.Parameters(record.type);
		while (parameter_index < function_type.parameter_count)
		{
			Parameter parameter;
			parameter.name = "__param" + std::to_string(parameter_index);
			parameter.type = LowerType(source_parameters[parameter_index]);
			parameter.reference = IsReferenceType(source_parameters[parameter_index]);
			parameters->push_back(parameter);
			++parameter_index;
		}
	}

	FunctionDeclaration LowerDeclaration(std::uint32_t node) const
	{
		const DumpNode& record = arena_.nodes[node];
		FunctionDeclaration declaration;
		declaration.symbol = function_symbols_[record.binding];
		FillBoundary(node, &declaration.parameters, &declaration.result,
			&declaration.variadic);
		return declaration;
	}

	GlobalDeclaration LowerGlobalDeclaration(std::uint32_t node) const
	{
		const DumpNode& record = arena_.nodes[node];
		GlobalDeclaration declaration;
		declaration.symbol = global_symbols_[record.binding];
		declaration.type = LowerType(record.type);
		return declaration;
	}

	Global LowerGlobal(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		const TypeRecord& source_type = program_.types.Get(record.type);
		if (source_type.kind == TYPE_ARRAY ||
			source_type.kind == TYPE_LVALUE_REFERENCE ||
			source_type.kind == TYPE_RVALUE_REFERENCE)
			throw std::runtime_error("aggregate global lowering is outside the active checkpoint");
		Global global;
		global.symbol = global_symbols_[record.binding];
		global.type = LowerType(record.type);
		const NodeChildren children = Children(node);
		if (!children.empty())
		{
			const DumpNode& initializer = arena_.nodes[children[0]];
			if (!initializer.constant)
				throw std::runtime_error(
					"global initializer is missing its PA12 constant fact");
			global.zero_initialize = false;
			global.initializer = initializer.constant_value;
		}
		if (stats_) ++stats_->globals;
		return global;
	}

	std::string UniqueSlotName(const std::string& requested)
	{
		std::string base = requested.empty() ? "__slot" : requested;
		std::size_t& count = slot_name_counts_[base];
		++count;
		std::string candidate = count == 1 ? base :
			base + "__shadow" + std::to_string(count);
		while (assigned_names_[candidate])
		{
			++count;
			candidate = base + "__shadow" + std::to_string(count);
		}
		assigned_names_[candidate] = true;
		used_names_[candidate] = true;
		return candidate;
	}

	std::string GeneratedSlotName(const std::string& prefix)
	{
		std::size_t& next = generated_slot_counts_[prefix];
		while (true)
		{
			const std::string candidate = prefix + "__" + std::to_string(++next);
			if (!used_names_[candidate])
			{
				used_names_[candidate] = true;
				return candidate;
			}
		}
	}

	void CollectSourceNames(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if ((record.kind == DUMP_PARAMETER || record.kind == DUMP_VARIABLE) &&
			record.text != 0)
			used_names_[program_.names.Get(record.text)] = true;
		const NodeChildren children = Children(node);
		for (std::size_t i = 0; i < children.size(); ++i) CollectSourceNames(children[i]);
	}

	void CollectSlots(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if ((record.kind == DUMP_PARAMETER || record.kind == DUMP_VARIABLE) &&
			record.binding != kNoBinding)
		{
			if (binding_slots_[record.binding] == kNoLowId)
			{
				std::string requested = record.text == 0 ? std::string() :
					program_.names.Get(record.text);
				if (record.kind == DUMP_PARAMETER && requested.empty())
					requested = parameter_slot_index_ < function_->parameters.size() ?
						function_->parameters[parameter_slot_index_].name : "__param";
				const std::string name = UniqueSlotName(requested);
				binding_slots_[record.binding] =
					static_cast<SlotId>(function_->slots.size());
				Slot slot;
				slot.name = name;
				slot.type = LowerType(record.type);
				function_->slots.push_back(slot);
			}
			if (record.kind == DUMP_PARAMETER) ++parameter_slot_index_;
		}
		const NodeChildren children = Children(node);
		for (std::size_t i = 0; i < children.size(); ++i) CollectSlots(children[i]);
	}

	void CollectGeneratedSlots(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.kind == DUMP_CONDITIONAL_EXPRESSION)
		{
			const std::string name = GeneratedSlotName("cond");
			generated_slots_[node] = static_cast<SlotId>(function_->slots.size());
			Slot slot;
			slot.name = name;
			slot.type = LowerType(record.type);
			function_->slots.push_back(slot);
		}
		const NodeChildren children = Children(node);
		for (std::size_t i = 0; i < children.size(); ++i)
			CollectGeneratedSlots(children[i]);
	}

	Function LowerFunction(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		Function result;
		result.symbol = function_symbols_[record.binding];
		result.entry = program_.names.Get(record.text) == "main";
		FillBoundary(node, &result.parameters, &result.result, &result.variadic);
		function_ = &result;
		current_result_ = result.result;
		temp_counter_ = 0;
		block_counter_ = 0;
		binding_slots_.assign(program_.bindings.size(), kNoLowId);
		generated_slots_.assign(arena_.nodes.size(), kNoLowId);
		used_names_.Clear();
		assigned_names_.Clear();
		slot_name_counts_.Clear();
		generated_slot_counts_.Clear();
		parameter_slot_index_ = 0;
		CollectSourceNames(node);
		CollectSlots(node);
		CollectGeneratedSlots(node);
		SelectBlock(AddBlock("entry"));

		const NodeChildren children = Children(node);
		std::size_t parameter_index = 0;
		std::uint32_t body = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& child = arena_.nodes[children[i]];
			if (child.kind == DUMP_PARAMETER)
			{
				Instruction store(Instruction::STORE);
				store.type = result.parameters[parameter_index].type;
				store.first = Operand(static_cast<ParameterId>(parameter_index),
					store.type);
				store.second = Operand(binding_slots_[child.binding], store.type);
				Emit(store);
				++parameter_index;
			}
			else if (child.kind == DUMP_COMPOUND_STATEMENT) body = children[i];
		}
		if (body != kNoDumpEdge) LowerStatement(body);
		if (!CurrentBlock().terminated)
		{
			if (result.entry)
			{
				Instruction instruction(Instruction::RETURN_VALUE);
				instruction.type = result.result;
				instruction.first = Operand(0, result.result);
				Emit(instruction);
			}
			else if (result.result.kind == LOW_VOID)
				Emit(Instruction(Instruction::RETURN_VOID));
			else throw std::runtime_error("non-void function has no return");
		}
		if (stats_)
		{
			++stats_->functions;
			stats_->blocks += result.blocks.size();
		}
		function_ = 0;
		return result;
	}

	Block& CurrentBlock() { return function_->blocks[current_block_]; }

	BlockId AddBlock(const std::string& label)
	{
		if (function_->blocks.size() >= kNoLowId)
			throw std::runtime_error("too many PA15 LowIR blocks");
		const BlockId block = static_cast<BlockId>(function_->blocks.size());
		function_->blocks.push_back(Block(label));
		return block;
	}

	void SelectBlock(BlockId block) { current_block_ = block; }

	std::string NewLabel(const std::string& prefix)
	{
		return prefix + "_" + std::to_string(++block_counter_);
	}

	TempId NewTemp()
	{
		while (true)
		{
			if (temp_counter_ + 1 >= kNoLowId)
				throw std::runtime_error("too many PA15 LowIR temporaries");
			const TempId candidate = static_cast<TempId>(++temp_counter_);
			if (!used_names_["t" + std::to_string(candidate)]) return candidate;
		}
	}

	Operand Temp(const LowType& type)
	{
		return Operand(NewTemp(), type);
	}

	void Emit(const Instruction& instruction)
	{
		if (CurrentBlock().terminated)
			throw std::runtime_error("PA15 attempted to emit after a terminator");
		CurrentBlock().instructions.push_back(instruction);
		if (IsTerminator(instruction)) CurrentBlock().terminated = true;
		if (stats_) ++stats_->instructions;
	}

	Operand StorageFor(BindingId binding, const LowType& type)
	{
		if (stats_) ++stats_->binding_index_probes;
		if (binding < binding_slots_.size() && binding_slots_[binding] != kNoLowId)
			return Operand(binding_slots_[binding], type);
		if (binding < program_.bindings.size())
			binding = program_.bindings[binding].canonical;
		if (binding < global_symbols_.size() && global_symbols_[binding] != kNoLowId)
			return Operand(Operand::GLOBAL, global_symbols_[binding], type);
		throw std::runtime_error("PA15 binding has no lowered storage");
	}

	Operand LowerStorage(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.kind == DUMP_ID_EXPRESSION && record.binding != kNoBinding)
			return StorageFor(record.binding, LowerType(record.type));
		throw std::runtime_error("expression does not designate scalar storage");
	}

	Operand Convert(Operand value, const LowType& target,
		bool canonicalize_immediate = true)
	{
		if (SameType(value.type, target))
		{
			value.type = target;
			return value;
		}
		if (IsInteger(value.type) && IsInteger(target) &&
			value.type.width == target.width)
		{
			value.type = target;
			return value;
		}
		if (canonicalize_immediate && value.kind == Operand::INTEGER &&
			IsInteger(value.type) && IsInteger(target))
		{
			value.type = target;
			return value;
		}
		Instruction instruction(Instruction::CONVERT);
		instruction.type = target;
		instruction.source_type = value.type;
		if (IsInteger(value.type) && IsInteger(target))
			instruction.op = target.width < value.type.width ? LOW_OP_TRUNC :
				value.type.is_signed ? LOW_OP_SEXT : LOW_OP_ZEXT;
		else if (IsInteger(value.type) && IsFloating(target))
			instruction.op = value.type.is_signed ? LOW_OP_SITOFP : LOW_OP_UITOFP;
		else if (IsFloating(value.type) && IsInteger(target))
			instruction.op = target.is_signed ? LOW_OP_FPTOSI : LOW_OP_FPTOUI;
		else if (IsFloating(value.type) && IsFloating(target))
			instruction.op = target.width < value.type.width ?
				LOW_OP_FPTRUNC : LOW_OP_FPEXT;
		else throw std::runtime_error("unsupported PA15 scalar conversion");
		const Operand result = Temp(target);
		instruction.dest = result.id;
		instruction.first = value;
		Emit(instruction);
		return result;
	}

	bool IsBooleanType(TypeId type) const
	{
		const TypeRecord* record = &program_.types.Get(type);
		while (record->kind == TYPE_QUALIFIED ||
			record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE)
		{
			type = record->child;
			record = &program_.types.Get(type);
		}
		return record->kind == TYPE_FUNDAMENTAL &&
			record->fundamental == FUND_BOOL;
	}

	Operand LowerCondition(std::uint32_t node)
	{
		Operand value = LowerValue(node);
		if (IsBooleanType(arena_.nodes[node].type) || !IsFloating(value.type))
			return value;
		const Operand result = Temp(LowU8());
		Instruction compare(Instruction::CMP);
		compare.dest = result.id;
		compare.op = LOW_OP_NE;
		compare.type = value.type;
		compare.first = value;
		compare.second = IsFloating(value.type) ? Operand("0.0", value.type) :
			Operand(0, value.type);
		Emit(compare);
		return result;
	}

	Operand LowerValue(std::uint32_t node, const LowType& expected = LowType())
	{
		if (stats_) ++stats_->lowered_nodes;
		const DumpNode& record = arena_.nodes[node];
		const NodeChildren children = Children(node);
		Operand result;
		if (record.kind == DUMP_LITERAL)
		{
			const LowType type = LowerType(record.type);
			if (IsFloating(type))
				result = Operand(program_.names.Get(record.text), type);
			else
			{
				if (!record.constant)
					throw std::runtime_error("literal is missing its PA12 constant fact");
				result = Operand(record.constant_value, type);
			}
		}
		else if (record.kind == DUMP_ID_EXPRESSION)
		{
			if (record.binding != kNoBinding && record.binding < function_symbols_.size() &&
				function_symbols_[record.binding] != kNoLowId)
			{
				const Operand address = Temp(LowPtr());
				Instruction instruction(Instruction::ADDR);
				instruction.dest = address.id;
				instruction.first = Operand(Operand::FUNCTION,
					function_symbols_[record.binding], LowPtr());
				Emit(instruction);
				result = address;
			}
			else
			{
				const LowType type = LowerType(record.type);
				const Operand storage = LowerStorage(node);
				result = Temp(type);
				Instruction load(Instruction::LOAD);
				load.dest = result.id;
				load.type = type;
				load.first = storage;
				Emit(load);
			}
		}
		else if (record.kind == DUMP_BINARY_EXPRESSION)
			result = LowerBinary(record, children);
		else if (record.kind == DUMP_ASSIGNMENT_EXPRESSION)
			result = LowerAssignment(record, children);
		else if (record.kind == DUMP_UNARY_EXPRESSION ||
			record.kind == DUMP_POSTFIX_EXPRESSION)
			result = LowerUnary(record, children);
		else if (record.kind == DUMP_CALL_EXPRESSION)
			result = LowerCall(record, children);
		else if (record.kind == DUMP_CAST_EXPRESSION)
		{
			if (children.size() != 1) throw std::runtime_error("invalid semantic cast");
			result = Convert(LowerValue(children[0]), LowerType(record.type));
		}
		else if (record.kind == DUMP_CONDITIONAL_EXPRESSION)
			result = LowerConditional(node, record, children);
		else if (record.kind == DUMP_BRACED_INIT_LIST && children.empty())
			result = Operand(0, LowerType(record.type));
		else throw std::runtime_error("semantic expression is outside the active PA15 checkpoint");
		return expected.kind == LOW_INVALID ? result : Convert(result, expected);
	}

	Operand LowerBinary(const DumpNode& record,
		const NodeChildren& children)
	{
		if (children.size() != 2) throw std::runtime_error("invalid semantic binary");
		const std::string op = StripOperationPrefix(program_.names.Get(record.text));
		if (op == "&&" || op == "||" || op == ",")
			throw std::runtime_error("short-circuit/comma lowering is outside the active checkpoint");
		const bool comparison = op == "==" || op == "!=" || op == "<" ||
			op == "<=" || op == ">" || op == ">=";
		if (record.operand_type == kNoType)
			throw std::runtime_error("binary expression is missing its PA12 operand type");
		const LowType operand_type = LowerType(record.operand_type);
		Operand left = Convert(LowerValue(children[0]), operand_type, false);
		Operand right = Convert(LowerValue(children[1]), operand_type, false);
		const LowType result_type = LowerType(record.type);
		const Operand result = Temp(result_type);
		Instruction instruction(comparison ? Instruction::CMP : Instruction::BINARY);
		instruction.dest = result.id;
		instruction.type = operand_type;
		instruction.first = left;
		instruction.second = right;
		if (comparison)
		{
			instruction.op = op == "==" ? LOW_OP_EQ : op == "!=" ? LOW_OP_NE :
				op == "<" ? (operand_type.is_signed ? LOW_OP_LT : LOW_OP_ULT) :
				op == "<=" ? (operand_type.is_signed ? LOW_OP_LE : LOW_OP_ULE) :
				op == ">" ? (operand_type.is_signed ? LOW_OP_GT : LOW_OP_UGT) :
				(operand_type.is_signed ? LOW_OP_GE : LOW_OP_UGE);
		}
		else
		{
			instruction.op = op == "+" ? LOW_OP_ADD : op == "-" ? LOW_OP_SUB :
				op == "*" ? LOW_OP_MUL : op == "/" ?
					(operand_type.is_signed || IsFloating(operand_type) ?
						LOW_OP_DIV : LOW_OP_UDIV) :
				op == "%" ? (operand_type.is_signed ? LOW_OP_MOD : LOW_OP_UMOD) :
				op == "&" ? LOW_OP_AND : op == "|" ? LOW_OP_OR :
				op == "^" ? LOW_OP_XOR : op == "<<" ? LOW_OP_SHL : op == ">>" ?
					(operand_type.is_signed ? LOW_OP_SHR : LOW_OP_USHR) : LOW_OP_NONE;
			if (instruction.op == LOW_OP_NONE)
				throw std::runtime_error("unsupported binary operator");
		}
		Emit(instruction);
		return result;
	}

	Operand LowerAssignment(const DumpNode& record,
		const NodeChildren& children)
	{
		if (children.size() != 2) throw std::runtime_error("invalid semantic assignment");
		const std::string op = StripOperationPrefix(program_.names.Get(record.text));
		const Operand storage = LowerStorage(children[0]);
		const LowType type = LowerType(record.type);
		Operand value;
		if (op == "=") value = Convert(LowerValue(children[1]), type, false);
		else
		{
			Operand left = Temp(type);
			Instruction load(Instruction::LOAD);
			load.dest = left.id;
			load.type = type;
			load.first = storage;
			Emit(load);
			if (record.operand_type == kNoType)
				throw std::runtime_error(
					"compound assignment is missing its PA12 operand type");
			const LowType operation_type = LowerType(record.operand_type);
			left = Convert(left, operation_type, false);
			const Operand right = Convert(LowerValue(children[1]), operation_type, false);
			value = Temp(operation_type);
			Instruction binary(Instruction::BINARY);
			binary.dest = value.id;
			binary.type = operation_type;
			binary.first = left;
			binary.second = right;
			binary.op = op == "+=" ? LOW_OP_ADD : op == "-=" ? LOW_OP_SUB :
				op == "*=" ? LOW_OP_MUL : op == "/=" ?
					(operation_type.is_signed || IsFloating(operation_type) ?
						LOW_OP_DIV : LOW_OP_UDIV) :
				op == "%=" ? (operation_type.is_signed ? LOW_OP_MOD : LOW_OP_UMOD) :
				op == "&=" ? LOW_OP_AND : op == "|=" ? LOW_OP_OR :
				op == "^=" ? LOW_OP_XOR : op == "<<=" ? LOW_OP_SHL : op == ">>=" ?
					(operation_type.is_signed ? LOW_OP_SHR : LOW_OP_USHR) : LOW_OP_NONE;
			if (binary.op == LOW_OP_NONE)
				throw std::runtime_error("unsupported PA15 compound assignment");
			Emit(binary);
			value = Convert(value, type, false);
		}
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = value;
		store.second = storage;
		Emit(store);
		return value;
	}

	Operand LowerUnary(const DumpNode& record,
		const NodeChildren& children)
	{
		if (children.size() != 1) throw std::runtime_error("invalid semantic unary");
		const std::string op = StripOperationPrefix(program_.names.Get(record.text));
		if (op == "!")
		{
			const Operand value = LowerValue(children[0]);
			const Operand result = Temp(LowU8());
			Instruction compare(Instruction::CMP);
			compare.dest = result.id;
			compare.op = LOW_OP_EQ;
			compare.type = value.type;
			compare.first = value;
			compare.second = IsFloating(value.type) ? Operand("0.0", value.type) :
				Operand(0, value.type);
			Emit(compare);
			return result;
		}
		const LowType type = LowerType(record.type);
		Operand value = LowerValue(children[0], type);
		if (op == "+") return value;
		const Operand result = Temp(type);
		Instruction instruction(Instruction::UNARY);
		instruction.dest = result.id;
		instruction.op = op == "-" ? LOW_OP_NEG :
			op == "~" ? LOW_OP_BITNOT : LOW_OP_NONE;
		if (instruction.op == LOW_OP_NONE)
			throw std::runtime_error("increment/address unary lowering is outside the active checkpoint");
		instruction.type = type;
		instruction.first = value;
		Emit(instruction);
		return result;
	}

	Operand LowerCall(const DumpNode& record,
		const NodeChildren& children)
	{
		if (children.empty()) throw std::runtime_error("semantic call has no callee");
		const DumpNode& callee = arena_.nodes[children[0]];
		if (stats_) ++stats_->binding_index_probes;
		if (callee.kind != DUMP_CALLEE || callee.binding == kNoBinding ||
			callee.binding >= function_symbols_.size() ||
			function_symbols_[callee.binding] == kNoLowId)
			throw std::runtime_error("indirect call lowering is outside the active checkpoint");
		const TypeRecord& function_type = program_.types.Get(callee.type);
		const TypeId* parameters = program_.types.Parameters(callee.type);
		Instruction call(Instruction::CALL);
		call.type = LowerType(record.type);
		call.first = Operand(Operand::FUNCTION,
			function_symbols_[callee.binding], LowPtr());
		for (std::size_t i = 1; i < children.size(); ++i)
		{
			const LowType expected = i - 1 < function_type.parameter_count ?
				LowerType(parameters[i - 1]) : LowerType(arena_.nodes[children[i]].type);
			call.arguments.push_back(Convert(LowerValue(children[i]), expected, false));
		}
		if (call.type.kind == LOW_VOID)
		{
			Emit(call);
			return Operand(0, LowVoid());
		}
		const Operand result = Temp(call.type);
		call.dest = result.id;
		Emit(call);
		return result;
	}

	Operand LowerConditional(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children)
	{
		if (children.size() != 3) throw std::runtime_error("invalid semantic conditional");
		const Operand condition = LowerCondition(children[0]);
		const BlockId then_block = AddBlock(NewLabel("cond_then"));
		const BlockId else_block = AddBlock(NewLabel("cond_else"));
		const BlockId end_block = AddBlock(NewLabel("cond_end"));
		Instruction branch(Instruction::BRANCH);
		branch.first = condition;
		branch.target = then_block;
		branch.alternate = else_block;
		Emit(branch);
		const LowType type = LowerType(record.type);
		const Operand slot(generated_slots_[node], type);
		SelectBlock(then_block);
		Instruction yes_store(Instruction::STORE);
		yes_store.type = type;
		yes_store.first = LowerValue(children[1], type);
		yes_store.second = slot;
		Emit(yes_store);
		Instruction yes_jump(Instruction::JUMP);
		yes_jump.target = end_block;
		Emit(yes_jump);
		SelectBlock(else_block);
		Instruction no_store(Instruction::STORE);
		no_store.type = type;
		no_store.first = LowerValue(children[2], type);
		no_store.second = slot;
		Emit(no_store);
		Instruction no_jump(Instruction::JUMP);
		no_jump.target = end_block;
		Emit(no_jump);
		SelectBlock(end_block);
		const Operand result = Temp(type);
		Instruction load(Instruction::LOAD);
		load.dest = result.id;
		load.type = type;
		load.first = slot;
		Emit(load);
		return result;
	}

	void LowerStatement(std::uint32_t node)
	{
		if (stats_) ++stats_->lowered_nodes;
		const DumpNode& record = arena_.nodes[node];
		const NodeChildren children = Children(node);
		if (record.kind == DUMP_COMPOUND_STATEMENT ||
			record.kind == DUMP_SIMPLE_DECLARATION ||
			record.kind == DUMP_THEN || record.kind == DUMP_ELSE)
		{
			for (std::size_t i = 0; i < children.size(); ++i)
			{
				if (CurrentBlock().terminated) break;
				LowerStatement(children[i]);
			}
			return;
		}
		if (record.kind == DUMP_VARIABLE)
		{
			if (!children.empty())
			{
				const LowType type = LowerType(record.type);
				Instruction store(Instruction::STORE);
				store.type = type;
				store.first = Convert(LowerValue(children[0]), type, false);
				store.second = StorageFor(record.binding, type);
				Emit(store);
			}
			return;
		}
		if (record.kind == DUMP_RETURN_STATEMENT)
		{
			if (children.empty()) Emit(Instruction(Instruction::RETURN_VOID));
			else
			{
				Instruction instruction(Instruction::RETURN_VALUE);
				instruction.type = current_result_;
				instruction.first = LowerValue(children[0], current_result_);
				Emit(instruction);
			}
			return;
		}
		if (record.kind == DUMP_EXPRESSION_STATEMENT)
		{
			if (!children.empty()) (void)LowerValue(children[0]);
			return;
		}
		if (record.kind == DUMP_IF_STATEMENT)
		{
			LowerIf(children);
			return;
		}
		throw std::runtime_error("statement is outside the active PA15 checkpoint");
	}

	void LowerIf(const NodeChildren& children)
	{
		std::uint32_t condition = kNoDumpEdge;
		std::uint32_t then_node = kNoDumpEdge;
		std::uint32_t else_node = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpKind kind = arena_.nodes[children[i]].kind;
			if (kind == DUMP_CONDITION) condition = children[i];
			else if (kind == DUMP_THEN) then_node = children[i];
			else if (kind == DUMP_ELSE) else_node = children[i];
		}
		if (condition == kNoDumpEdge || then_node == kNoDumpEdge)
			throw std::runtime_error("invalid semantic if statement");
		const NodeChildren condition_children = Children(condition);
		if (condition_children.size() != 1)
			throw std::runtime_error("condition declarations are outside the active checkpoint");
		const Operand condition_value = LowerCondition(condition_children[0]);
		const BlockId then_block = AddBlock(NewLabel("if_then"));
		const BlockId else_block = AddBlock(NewLabel("if_else"));
		Instruction branch(Instruction::BRANCH);
		branch.first = condition_value;
		branch.target = then_block;
		branch.alternate = else_block;
		Emit(branch);
		SelectBlock(then_block);
		LowerStatement(then_node);
		const BlockId then_exit = static_cast<BlockId>(current_block_);
		SelectBlock(else_block);
		if (else_node != kNoDumpEdge) LowerStatement(else_node);
		const BlockId else_exit = static_cast<BlockId>(current_block_);
		const bool then_terminated = function_->blocks[then_exit].terminated;
		const bool else_terminated = function_->blocks[else_exit].terminated;
		if (then_terminated && else_terminated) return;
		const BlockId end_block = AddBlock(NewLabel("if_end"));
		if (!then_terminated)
		{
			Instruction then_jump(Instruction::JUMP);
			then_jump.target = end_block;
			current_block_ = then_exit;
			Emit(then_jump);
		}
		if (!else_terminated)
		{
			Instruction else_jump(Instruction::JUMP);
			else_jump.target = end_block;
			current_block_ = else_exit;
			Emit(else_jump);
		}
		SelectBlock(end_block);
	}

	const SemanticGraphView& graph_;
	const Program& program_;
	const DumpArena& arena_;
	TypedProgram& output_;
	LowIRLoweringStats* stats_;
	std::vector<SymbolId> function_symbols_;
	std::vector<SymbolId> global_symbols_;
	std::vector<std::uint32_t> function_definition_;
	std::vector<std::uint32_t> function_declaration_;
	std::vector<std::uint32_t> global_node_;
	StringCounterTable overload_counts_;
	Function* function_;
	BlockId current_block_;
	LowType current_result_;
	std::size_t temp_counter_;
	std::size_t block_counter_;
	std::vector<SlotId> binding_slots_;
	std::vector<SlotId> generated_slots_;
	StringCounterTable used_names_;
	StringCounterTable assigned_names_;
	StringCounterTable slot_name_counts_;
	StringCounterTable generated_slot_counts_;
	std::size_t parameter_slot_index_;
	std::size_t source_ordinal_;
	std::vector<IdentityTypeId> identity_type_cache_;
};

void WriteParameter(std::ostream& output, const Parameter& parameter)
{
	output << '%' << parameter.name << " : " << parameter.type.text;
	if (parameter.reference) output << " [pass=reference]";
}

void WriteBoundary(std::ostream& output,
	const std::vector<Parameter>& parameters, const LowType& result,
	bool variadic)
{
	output << '(';
	for (std::size_t i = 0; i < parameters.size(); ++i)
	{
		if (i != 0) output << ", ";
		WriteParameter(output, parameters[i]);
	}
	output << ") -> " << result.text;
	if (variadic) output << " [arity=variadic]";
}

void WriteOperand(std::ostream& output, const Operand& operand,
	const TypedProgram& program, const Function& function)
{
	switch (operand.kind)
	{
	case Operand::TEMP: output << "%t" << operand.id; break;
	case Operand::PARAMETER:
		if (operand.id >= function.parameters.size())
			throw std::logic_error("invalid PA15 parameter reference");
		output << '%' << function.parameters[operand.id].name;
		break;
	case Operand::SLOT:
		if (operand.id >= function.slots.size())
			throw std::logic_error("invalid PA15 slot reference");
		output << '$' << function.slots[operand.id].name;
		break;
	case Operand::GLOBAL: case Operand::FUNCTION:
		if (operand.id >= program.symbols.size())
			throw std::logic_error("invalid PA15 symbol reference");
		output << '@' << program.symbols[operand.id].name;
		break;
	case Operand::INTEGER: output << operand.integer_value; break;
	case Operand::FLOATING: output << operand.floating_spelling; break;
	case Operand::NONE: throw std::logic_error("missing PA15 LowIR operand");
	}
}

const char* OperationText(LowOperation operation)
{
	switch (operation)
	{
	case LOW_OP_NEG: return "neg";
	case LOW_OP_BITNOT: return "bitnot";
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
	case LOW_OP_NONE: break;
	}
	throw std::logic_error("missing PA15 LowIR operation");
}

void WriteInstruction(std::ostream& output, const Instruction& instruction,
	const TypedProgram& program, const Function& function)
{
	switch (instruction.kind)
	{
	case Instruction::ADDR:
		output << "%t" << instruction.dest << " = addr ";
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::LOAD:
		output << "%t" << instruction.dest << " = load " << instruction.type.text << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::STORE:
		output << "store " << instruction.type.text << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::UNARY:
		output << "%t" << instruction.dest << " = unary "
			<< OperationText(instruction.op) << ' '
			<< instruction.type.text << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::BINARY:
		output << "%t" << instruction.dest << " = binary "
			<< OperationText(instruction.op) << ' '
			<< instruction.type.text << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::CMP:
		output << "%t" << instruction.dest << " = cmp "
			<< OperationText(instruction.op) << ' '
			<< instruction.type.text << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << ", ";
		WriteOperand(output, instruction.second, program, function);
		break;
	case Instruction::CONVERT:
		output << "%t" << instruction.dest << " = convert "
			<< OperationText(instruction.op) << ' '
			<< instruction.type.text << ' ' << instruction.source_type.text << ' '
			;
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::CALL:
		if (instruction.dest != kNoLowId) output << "%t" << instruction.dest << " = ";
		output << "call " << instruction.type.text << ' ';
		WriteOperand(output, instruction.first, program, function);
		output << '(';
		for (std::size_t i = 0; i < instruction.arguments.size(); ++i)
		{
			if (i != 0) output << ", ";
			WriteOperand(output, instruction.arguments[i], program, function);
		}
		output << ')';
		break;
	case Instruction::JUMP:
		if (instruction.target >= function.blocks.size())
			throw std::logic_error("invalid PA15 jump target");
		output << "jump ^" << function.blocks[instruction.target].label;
		break;
	case Instruction::BRANCH:
		if (instruction.target >= function.blocks.size() ||
			instruction.alternate >= function.blocks.size())
			throw std::logic_error("invalid PA15 branch target");
		output << "branch ";
		WriteOperand(output, instruction.first, program, function);
		output << ", ^" << function.blocks[instruction.target].label << ", ^"
			<< function.blocks[instruction.alternate].label;
		break;
	case Instruction::RETURN_VALUE:
		output << "return " << instruction.type.text << ' ';
		WriteOperand(output, instruction.first, program, function);
		break;
	case Instruction::RETURN_VOID: output << "return void"; break;
	}
}

void WriteSymbolMetadata(std::ostream& output, const Symbol& symbol,
	bool entry, bool function)
{
	output << " [";
	bool separator = false;
	if (function && symbol.nonthrowing)
	{
		output << "unwind=no";
		separator = true;
	}
	if (entry)
	{
		if (separator) output << ", ";
		output << "role=entry";
		separator = true;
	}
	if (symbol.c_linkage)
	{
		if (separator) output << ", ";
		output << "linkage=c";
		separator = true;
	}
	if (separator) output << ", ";
	output << "binding=" << (symbol.internal_linkage ? "internal" : "strong");
	if (!symbol.object_name.empty()) output << ", object=" << symbol.object_name;
	if (entry) output << ", keep_alias=yes";
	output << ']';
}

void RenderProgram(const TypedProgram& program, std::ostream& output)
{
	bool wrote = false;
	for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
	{
		const GlobalDeclaration& declaration = program.global_declarations[i];
		const Symbol& symbol = program.symbols[declaration.symbol];
		if (symbol.definition_emitted) continue;
		if (wrote) output << '\n';
		output << "declare global @" << symbol.name << " : "
			<< declaration.type.text;
		WriteSymbolMetadata(output, symbol, false, false);
		output << '\n';
		wrote = true;
	}
	for (std::size_t i = 0; i < program.declarations.size(); ++i)
	{
		const FunctionDeclaration& declaration = program.declarations[i];
		const Symbol& symbol = program.symbols[declaration.symbol];
		if (symbol.definition_emitted) continue;
		if (wrote) output << '\n';
		output << "declare function @" << symbol.name;
		WriteBoundary(output, declaration.parameters, declaration.result,
			declaration.variadic);
		WriteSymbolMetadata(output, symbol, false, true);
		output << '\n';
		wrote = true;
	}
	for (std::size_t i = 0; i < program.globals.size(); ++i)
	{
		const Global& global = program.globals[i];
		const Symbol& symbol = program.symbols[global.symbol];
		if (wrote) output << '\n';
		output << "global @" << symbol.name << " : " << global.type.text;
		WriteSymbolMetadata(output, symbol, false, false);
		output << " = ";
		if (global.zero_initialize) output << "zero";
		else output << global.initializer;
		output << '\n';
		wrote = true;
	}
	for (std::size_t i = 0; i < program.functions.size(); ++i)
	{
		const Function& function = program.functions[i];
		const Symbol& symbol = program.symbols[function.symbol];
		if (wrote) output << '\n';
		output << "function @" << symbol.name;
		WriteBoundary(output, function.parameters, function.result, function.variadic);
		WriteSymbolMetadata(output, symbol, function.entry, true);
		output << " {\n";
		for (std::size_t s = 0; s < function.slots.size(); ++s)
			output << "  slot $" << function.slots[s].name << " : "
				<< function.slots[s].type.text << '\n';
		if (!function.slots.empty()) output << '\n';
		for (std::size_t b = 0; b < function.blocks.size(); ++b)
		{
			if (b != 0) output << '\n';
			output << "  block ^" << function.blocks[b].label << ":\n";
			for (std::size_t j = 0; j < function.blocks[b].instructions.size(); ++j)
			{
				output << "    ";
				WriteInstruction(output, function.blocks[b].instructions[j],
					program, function);
				output << '\n';
			}
		}
		output << "}\n";
		wrote = true;
	}
}

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
			function.blocks.capacity() * sizeof(Block);
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
					instruction.first.floating_spelling.capacity() +
					instruction.second.floating_spelling.capacity();
				for (std::size_t a = 0; a < instruction.arguments.size(); ++a)
					bytes += instruction.arguments[a].floating_spelling.capacity();
			}
		}
	}
	return bytes;
}

class CountingStreamBuffer : public std::streambuf
{
public:
	explicit CountingStreamBuffer(std::streambuf* destination)
		: destination_(destination), bytes_(0) {}

	std::size_t Bytes() const { return bytes_; }

protected:
	int_type overflow(int_type character)
	{
		if (traits_type::eq_int_type(character, traits_type::eof()))
			return traits_type::not_eof(character);
		const int_type written = destination_->sputc(
			traits_type::to_char_type(character));
		if (!traits_type::eq_int_type(written, traits_type::eof())) ++bytes_;
		return written;
	}

	std::streamsize xsputn(const char* data, std::streamsize size)
	{
		const std::streamsize written = destination_->sputn(data, size);
		if (written > 0) bytes_ += static_cast<std::size_t>(written);
		return written;
	}

	int sync() { return destination_->pubsync(); }

private:
	std::streambuf* destination_;
	std::size_t bytes_;
};

class GraphConsumer : public SemanticGraphConsumer
{
public:
	GraphConsumer(TypedProgram& program, LowIRLoweringStats* stats,
		std::size_t source_ordinal)
		: program_(program), stats_(stats), source_ordinal_(source_ordinal) {}

	void Consume(const SemanticGraphView& graph)
	{
		const std::chrono::steady_clock::time_point started =
			std::chrono::steady_clock::now();
		GraphLowerer(graph, program_, stats_, source_ordinal_).Lower();
		if (stats_)
			stats_->lowering_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started).count());
	}

private:
	TypedProgram& program_;
	LowIRLoweringStats* stats_;
	std::size_t source_ordinal_;
};

}

LowIRLoweringStats::LowIRLoweringStats()
	: source_bytes(0), tokens(0), semantic_nodes(0), semantic_edges(0),
	  lowered_nodes(0), functions(0), globals(0), blocks(0), instructions(0),
	  binding_index_probes(0), typed_storage_bytes(0), output_bytes(0),
	  semantic_nanoseconds(0), lowering_nanoseconds(0), render_nanoseconds(0)
{
}

void WriteLowIRProgram(const std::vector<LowIRSource>& sources,
	const PreprocessingOptions& options, std::ostream& output,
	LowIRLoweringStats* stats)
{
	if (sources.empty()) throw std::runtime_error("no PA15 source inputs");
	if (stats) *stats = LowIRLoweringStats();
	TypedProgram program;
	for (std::size_t i = 0; i < sources.size(); ++i)
	{
		GraphConsumer consumer(program, stats, i);
		SemanticAnalysisStats semantic_stats;
		ConsumeSemanticTranslationUnit(sources[i].path, sources[i].source,
			options, consumer, stats ? &semantic_stats : 0);
		if (stats)
		{
			stats->source_bytes += sources[i].source.size();
			stats->tokens += semantic_stats.tokens;
			stats->semantic_nodes += semantic_stats.semantic_nodes;
			stats->semantic_edges += semantic_stats.semantic_edges;
			stats->semantic_nanoseconds += semantic_stats.analysis_nanoseconds;
		}
	}
	const std::chrono::steady_clock::time_point render_started =
		std::chrono::steady_clock::now();
	CountingStreamBuffer buffer(output.rdbuf());
	std::ostream rendered(&buffer);
	RenderProgram(program, rendered);
	rendered.flush();
	if (!rendered || !output)
		throw std::runtime_error("unable to write LowIR output");
	if (stats)
	{
		stats->typed_storage_bytes = TypedStorageBytes(program);
		stats->output_bytes = buffer.Bytes();
		stats->render_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - render_started).count());
	}
}

}
