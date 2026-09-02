#include "namespace_semantics/analysis.h"

#include "support/driver_errors.h"
#include "support/exceptions.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace cppgm
{
namespace namespace_semantics
{
namespace
{

typedef std::uint32_t NameId;
typedef std::uint32_t TypeId;
typedef std::uint32_t NamespaceId;
typedef std::uint32_t EntityId;
typedef std::uint32_t UsingEdgeId;

const NamespaceId kNoNamespace = std::numeric_limits<NamespaceId>::max();
const EntityId kNoEntity = std::numeric_limits<EntityId>::max();
const UsingEdgeId kNoUsingEdge =
	std::numeric_limits<UsingEdgeId>::max();
// Parenthesized grouping uses an explicit stack. This bound applies only to
// semantic function-parameter recursion and prevents native-stack exhaustion.
const std::size_t kMaxDeclaratorCallDepth = 4096;
const std::uint16_t kIdentifierToken =
	static_cast<std::uint16_t>(OP_ARROW) + 1;
const std::uint16_t kLiteralToken = kIdentifierToken + 1;
const std::uint16_t kEofToken = kIdentifierToken + 2;

std::size_t HashSpelling(const std::string& spelling)
{
	std::size_t value = sizeof(std::size_t) == 8 ?
		static_cast<std::size_t>(1469598103934665603ULL) :
		static_cast<std::size_t>(2166136261U);
	const std::size_t prime = sizeof(std::size_t) == 8 ?
		static_cast<std::size_t>(1099511628211ULL) :
		static_cast<std::size_t>(16777619U);
	for (std::size_t i = 0; i < spelling.size(); ++i)
	{
		value ^= static_cast<unsigned char>(spelling[i]);
		value *= prime;
	}
	return value;
}

std::size_t MixHash(std::size_t seed, std::uint64_t value)
{
	seed ^= static_cast<std::size_t>(value) +
		static_cast<std::size_t>(0x9e3779b9U) + (seed << 6) + (seed >> 2);
	return seed;
}

class IdentifierTable
{
public:
	IdentifierTable() : slots_(16, 0), spelling_bytes_(0)
	{
		spellings_.push_back(std::string());
	}

	NameId Intern(const std::string& spelling)
	{
		if ((spellings_.size() + 1) * 10 > slots_.size() * 7)
			Rehash(slots_.size() * 2);
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = HashSpelling(spelling) & mask;
		while (slots_[slot] != 0)
		{
			const NameId id = slots_[slot];
			if (spellings_[id] == spelling) return id;
			slot = (slot + 1) & mask;
		}
		if (spellings_.size() > std::numeric_limits<NameId>::max())
			ThrowSemanticResourceLimit("too many identifiers");
		const NameId id = static_cast<NameId>(spellings_.size());
		spellings_.push_back(spelling);
		spelling_bytes_ += spelling.size();
		slots_[slot] = id;
		return id;
	}

	const std::string& Get(NameId id) const { return spellings_[id]; }
	std::size_t Size() const { return spellings_.size() - 1; }
	std::size_t SpellingBytes() const { return spelling_bytes_; }
	std::size_t StorageBytes() const
	{
		std::size_t result = spellings_.capacity() * sizeof(std::string) +
			slots_.capacity() * sizeof(NameId);
		for (std::size_t i = 1; i < spellings_.size(); ++i)
			result += spellings_[i].capacity();
		return result;
	}

private:
	void Rehash(std::size_t capacity)
	{
		std::vector<NameId> replacement(capacity, 0);
		const std::size_t mask = capacity - 1;
		for (NameId id = 1; id < spellings_.size(); ++id)
		{
			std::size_t slot = HashSpelling(spellings_[id]) & mask;
			while (replacement[slot] != 0) slot = (slot + 1) & mask;
			replacement[slot] = id;
		}
		slots_.swap(replacement);
	}

	std::vector<std::string> spellings_;
	std::vector<NameId> slots_;
	std::size_t spelling_bytes_;
};

struct SemanticToken
{
	std::uint16_t kind;
	NameId name;
	std::uint64_t value;

	SemanticToken(std::uint16_t token_kind, NameId token_name = 0,
		std::uint64_t token_value = 0)
		: kind(token_kind), name(token_name), value(token_value) {}
};

std::uint64_t DecodePositiveLiteral(const void* data, std::size_t size)
{
	std::uint64_t value = 0;
	std::memcpy(&value, data, std::min(size, sizeof(value)));
	return value;
}

class SemanticTokenSink : public IPostTokenStream
{
public:
	explicit SemanticTokenSink(IdentifierTable& identifiers)
		: identifiers_(identifiers) {}

	void EmitInvalid(const std::string& source)
	{
		driver_errors::ThrowLexicalSource("invalid phase-7 token: " + source);
	}

	void EmitSimple(const std::string&, SimpleTokenKind kind)
	{
		tokens_.push_back(SemanticToken(static_cast<std::uint16_t>(kind)));
	}

	void EmitIdentifier(const std::string& source)
	{
		tokens_.push_back(SemanticToken(kIdentifierToken,
			identifiers_.Intern(source)));
	}

	void EmitLiteral(const std::string&, FundamentalType, const void* data,
		std::size_t size)
	{
		tokens_.push_back(SemanticToken(kLiteralToken, 0,
			DecodePositiveLiteral(data, size)));
	}

	void EmitLiteralArray(const std::string&, std::size_t, FundamentalType,
		const void*, std::size_t)
	{
		tokens_.push_back(SemanticToken(kLiteralToken));
	}

	void EmitUserDefinedCharacter(const std::string&, const std::string&,
		FundamentalType, const void*, std::size_t)
	{
		ThrowSyntaxError("user-defined literal in PA7 input");
	}

	void EmitUserDefinedString(const std::string&, const std::string&,
		std::size_t, FundamentalType, const void*, std::size_t)
	{
		ThrowSyntaxError("user-defined literal in PA7 input");
	}

	void EmitUserDefinedInteger(const std::string&, const std::string&,
		const std::string&)
	{
		ThrowSyntaxError("user-defined literal in PA7 input");
	}

	void EmitUserDefinedFloating(const std::string&, const std::string&,
		const std::string&)
	{
		ThrowSyntaxError("user-defined literal in PA7 input");
	}

	void EmitEof() { tokens_.push_back(SemanticToken(kEofToken)); }

	const std::vector<SemanticToken>& Tokens() const { return tokens_; }
	std::size_t StorageBytes() const
	{
		return tokens_.capacity() * sizeof(SemanticToken);
	}

private:
	IdentifierTable& identifiers_;
	std::vector<SemanticToken> tokens_;
};

enum TypeKind
{
	TYPE_INVALID,
	TYPE_FUNDAMENTAL,
	TYPE_QUALIFIED,
	TYPE_POINTER,
	TYPE_LVALUE_REFERENCE,
	TYPE_RVALUE_REFERENCE,
	TYPE_ARRAY,
	TYPE_FUNCTION
};

enum CvFlags
{
	CV_NONE = 0,
	CV_CONST = 1,
	CV_VOLATILE = 2
};

struct TypeRecord
{
	TypeKind kind;
	TypeId child;
	std::uint64_t bound;
	std::uint32_t parameter_offset;
	std::uint32_t parameter_count;
	unsigned char cv;
	bool variadic;
	FundamentalType fundamental;

	TypeRecord()
		: kind(TYPE_INVALID), child(0), bound(0), parameter_offset(0),
		  parameter_count(0), cv(0), variadic(false), fundamental(FT_INT) {}
};

class TypeTable
{
public:
	TypeTable() : slots_(32, 0)
	{
		types_.push_back(TypeRecord());
	}

	TypeId Fundamental(FundamentalType fundamental)
	{
		TypeRecord candidate;
		candidate.kind = TYPE_FUNDAMENTAL;
		candidate.fundamental = fundamental;
		return Intern(candidate, 0, 0);
	}

	TypeId Pointer(TypeId child)
	{
		return Unary(TYPE_POINTER, child);
	}

	TypeId Reference(TypeKind kind, TypeId child)
	{
		const TypeRecord& referred = Get(child);
		if (referred.kind == TYPE_LVALUE_REFERENCE)
			return Unary(TYPE_LVALUE_REFERENCE, referred.child);
		if (referred.kind == TYPE_RVALUE_REFERENCE)
		{
			return Unary(kind == TYPE_LVALUE_REFERENCE ?
				TYPE_LVALUE_REFERENCE : TYPE_RVALUE_REFERENCE,
				referred.child);
		}
		return Unary(kind, child);
	}

	TypeId Array(TypeId child, std::uint64_t bound)
	{
		TypeRecord candidate;
		candidate.kind = TYPE_ARRAY;
		candidate.child = child;
		candidate.bound = bound;
		return Intern(candidate, 0, 0);
	}

	TypeId Function(TypeId result, const std::vector<TypeId>& parameters,
		bool variadic)
	{
		TypeRecord candidate;
		candidate.kind = TYPE_FUNCTION;
		candidate.child = result;
		candidate.parameter_count =
			static_cast<std::uint32_t>(parameters.size());
		candidate.variadic = variadic;
		return Intern(candidate, parameters.empty() ? 0 : &parameters[0],
			parameters.size());
	}

	TypeId Qualify(TypeId type, unsigned char cv)
	{
		if (cv == CV_NONE) return type;
		const TypeRecord& record = Get(type);
		if (record.kind == TYPE_ARRAY)
			return Array(Qualify(record.child, cv), record.bound);
		if (record.kind == TYPE_LVALUE_REFERENCE ||
			record.kind == TYPE_RVALUE_REFERENCE) return type;
		if (record.kind == TYPE_QUALIFIED)
			return Qualify(record.child, record.cv | cv);
		TypeRecord candidate;
		candidate.kind = TYPE_QUALIFIED;
		candidate.child = type;
		candidate.cv = cv;
		return Intern(candidate, 0, 0);
	}

	TypeId AdjustParameter(TypeId type)
	{
		const TypeRecord& record = Get(type);
		if (record.kind == TYPE_ARRAY) return Pointer(record.child);
		if (record.kind == TYPE_FUNCTION) return Pointer(type);
		if (record.kind == TYPE_QUALIFIED) return record.child;
		return type;
	}

	TypeId MergeRedeclaration(TypeId first, TypeId second)
	{
		if (first == second) return first;
		const TypeRecord& left = Get(first);
		const TypeRecord& right = Get(second);
		if (left.kind != TYPE_ARRAY || right.kind != TYPE_ARRAY)
			ThrowSemanticError("incompatible redeclaration");
		const TypeId child = MergeRedeclaration(left.child, right.child);
		if (left.bound != 0 && right.bound != 0 && left.bound != right.bound)
			ThrowSemanticError("incompatible array bounds");
		return Array(child, left.bound == 0 ? right.bound : left.bound);
	}

	bool IsFunction(TypeId type) const
	{
		return Get(type).kind == TYPE_FUNCTION;
	}

	bool IsVoid(TypeId type) const
	{
		const TypeRecord& record = Get(type);
		return record.kind == TYPE_FUNDAMENTAL &&
			record.fundamental == FT_VOID;
	}

	const TypeRecord& Get(TypeId type) const { return types_[type]; }
	std::size_t Size() const { return types_.size() - 1; }
	std::size_t StorageBytes() const
	{
		return types_.capacity() * sizeof(TypeRecord) +
			parameters_.capacity() * sizeof(TypeId) +
			slots_.capacity() * sizeof(TypeId);
	}

	void Render(std::ostream& output, TypeId type) const
	{
		struct Frame
		{
			TypeId type;
			std::uint32_t next_parameter;
			unsigned char state;

			explicit Frame(TypeId frame_type)
				: type(frame_type), next_parameter(0), state(0) {}
		};
		std::vector<Frame> stack;
		stack.push_back(Frame(type));
		while (!stack.empty())
		{
			Frame& frame = stack.back();
			const TypeRecord& record = Get(frame.type);
			if (frame.state == 1)
			{
				if (frame.next_parameter < record.parameter_count)
				{
					output << ", ";
					stack.push_back(Frame(parameters_[record.parameter_offset +
						frame.next_parameter++]));
					continue;
				}
				if (record.variadic)
				{
					if (record.parameter_count != 0) output << ", ";
					output << "...";
				}
				output << ") returning ";
				frame.state = 2;
				stack.push_back(Frame(record.child));
				continue;
			}
			if (frame.state == 2)
			{
				stack.pop_back();
				continue;
			}
			switch (record.kind)
			{
			case TYPE_FUNDAMENTAL:
				output << FundamentalTypeName(record.fundamental);
				stack.pop_back();
				break;
			case TYPE_QUALIFIED:
				if ((record.cv & CV_CONST) != 0) output << "const ";
				if ((record.cv & CV_VOLATILE) != 0) output << "volatile ";
				frame.type = record.child;
				break;
			case TYPE_POINTER:
				output << "pointer to ";
				frame.type = record.child;
				break;
			case TYPE_LVALUE_REFERENCE:
				output << "lvalue-reference to ";
				frame.type = record.child;
				break;
			case TYPE_RVALUE_REFERENCE:
				output << "rvalue-reference to ";
				frame.type = record.child;
				break;
			case TYPE_ARRAY:
				if (record.bound == 0)
					output << "array of unknown bound of ";
				else
					output << "array of " << record.bound << ' ';
				frame.type = record.child;
				break;
			case TYPE_FUNCTION:
				output << "function of (";
				frame.state = 1;
				if (record.parameter_count != 0)
				{
					frame.next_parameter = 1;
					stack.push_back(Frame(
						parameters_[record.parameter_offset]));
				}
				break;
			default:
				ThrowSemanticInternal("invalid canonical type");
			}
		}
	}

private:
	TypeId Unary(TypeKind kind, TypeId child)
	{
		TypeRecord candidate;
		candidate.kind = kind;
		candidate.child = child;
		return Intern(candidate, 0, 0);
	}

	std::size_t Hash(const TypeRecord& record, const TypeId* parameters,
		std::size_t parameter_count) const
	{
		std::size_t hash = MixHash(0, record.kind);
		hash = MixHash(hash, record.child);
		hash = MixHash(hash, record.bound);
		hash = MixHash(hash, record.cv);
		hash = MixHash(hash, record.variadic ? 1 : 0);
		hash = MixHash(hash, record.fundamental);
		for (std::size_t i = 0; i < parameter_count; ++i)
			hash = MixHash(hash, parameters[i]);
		return hash;
	}

	bool Equal(const TypeRecord& existing, const TypeRecord& candidate,
		const TypeId* parameters, std::size_t parameter_count) const
	{
		if (existing.kind != candidate.kind ||
			existing.child != candidate.child ||
			existing.bound != candidate.bound || existing.cv != candidate.cv ||
			existing.variadic != candidate.variadic ||
			existing.fundamental != candidate.fundamental ||
			existing.parameter_count != parameter_count) return false;
		for (std::size_t i = 0; i < parameter_count; ++i)
		{
			if (parameters_[existing.parameter_offset + i] != parameters[i])
				return false;
		}
		return true;
	}

	TypeId Intern(TypeRecord candidate, const TypeId* parameters,
		std::size_t parameter_count)
	{
		if ((types_.size() + 1) * 10 > slots_.size() * 7)
			Rehash(slots_.size() * 2);
		const std::size_t hash = Hash(candidate, parameters, parameter_count);
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = hash & mask;
		while (slots_[slot] != 0)
		{
			const TypeId id = slots_[slot];
			if (Equal(types_[id], candidate, parameters, parameter_count))
				return id;
			slot = (slot + 1) & mask;
		}
		if (types_.size() > std::numeric_limits<TypeId>::max())
			ThrowSemanticResourceLimit("too many canonical types");
		candidate.parameter_offset =
			static_cast<std::uint32_t>(parameters_.size());
		if (parameter_count != 0)
			parameters_.insert(parameters_.end(), parameters,
				parameters + parameter_count);
		const TypeId id = static_cast<TypeId>(types_.size());
		types_.push_back(candidate);
		slots_[slot] = id;
		return id;
	}

	void Rehash(std::size_t capacity)
	{
		std::vector<TypeId> replacement(capacity, 0);
		const std::size_t mask = capacity - 1;
		for (TypeId id = 1; id < types_.size(); ++id)
		{
			const TypeRecord& record = types_[id];
			const TypeId* begin = record.parameter_count == 0 ? 0 :
				&parameters_[record.parameter_offset];
			std::size_t slot = Hash(record, begin,
				record.parameter_count) & mask;
			while (replacement[slot] != 0) slot = (slot + 1) & mask;
			replacement[slot] = id;
		}
		slots_.swap(replacement);
	}

	std::vector<TypeRecord> types_;
	std::vector<TypeId> parameters_;
	std::vector<TypeId> slots_;
};

struct Binding
{
	NamespaceId owner;
	NameId name;
	TypeId type;
	NamespaceId name_space;
	EntityId variable;
	EntityId function;

	Binding(NamespaceId binding_owner = 0, NameId binding_name = 0)
		: owner(binding_owner), name(binding_name), type(0),
		  name_space(kNoNamespace),
		  variable(kNoEntity), function(kNoEntity) {}
};

class BindingIndex
{
public:
	BindingIndex() : slots_(16, 0) {}

	std::uint32_t Find(NamespaceId owner, NameId name,
		const std::vector<Binding>& bindings) const
	{
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = Hash(owner, name) & mask;
		while (slots_[slot] != 0)
		{
			const std::uint32_t index = slots_[slot] - 1;
			if (bindings[index].owner == owner &&
				bindings[index].name == name) return index;
			slot = (slot + 1) & mask;
		}
		return std::numeric_limits<std::uint32_t>::max();
	}

	std::uint32_t Ensure(NamespaceId owner, NameId name,
		std::vector<Binding>* bindings)
	{
		if ((bindings->size() + 1) * 10 > slots_.size() * 7)
			Rehash(slots_.size() * 2, *bindings);
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = Hash(owner, name) & mask;
		while (slots_[slot] != 0)
		{
			const std::uint32_t index = slots_[slot] - 1;
			if ((*bindings)[index].owner == owner &&
				(*bindings)[index].name == name) return index;
			slot = (slot + 1) & mask;
		}
		bindings->push_back(Binding(owner, name));
		const std::uint32_t index =
			static_cast<std::uint32_t>(bindings->size() - 1);
		slots_[slot] = index + 1;
		return index;
	}

	std::size_t StorageBytes() const
	{
		return slots_.capacity() * sizeof(std::uint32_t);
	}

private:
	std::size_t Hash(NamespaceId owner, NameId name) const
	{
		return MixHash(MixHash(0, owner), name);
	}

	void Rehash(std::size_t capacity, const std::vector<Binding>& bindings)
	{
		std::vector<std::uint32_t> replacement(capacity, 0);
		const std::size_t mask = capacity - 1;
		for (std::uint32_t i = 0; i < bindings.size(); ++i)
		{
			std::size_t slot = Hash(bindings[i].owner,
				bindings[i].name) & mask;
			while (replacement[slot] != 0) slot = (slot + 1) & mask;
			replacement[slot] = i + 1;
		}
		slots_.swap(replacement);
	}

	std::vector<std::uint32_t> slots_;
};

struct NamespaceRecord
{
	NameId name;
	NamespaceId parent;
	NamespaceId unnamed_child;
	NamespaceId first_child;
	NamespaceId last_child;
	NamespaceId next_sibling;
	EntityId first_variable;
	EntityId last_variable;
	EntityId first_function;
	EntityId last_function;
	UsingEdgeId first_using_edge;
	UsingEdgeId last_using_edge;
	UsingEdgeId first_using_predecessor;
	UsingEdgeId last_using_predecessor;
	bool is_inline;

	NamespaceRecord(NameId namespace_name = 0,
		NamespaceId namespace_parent = kNoNamespace, bool inline_value = false)
		: name(namespace_name), parent(namespace_parent),
		  unnamed_child(kNoNamespace), first_child(kNoNamespace),
		  last_child(kNoNamespace), next_sibling(kNoNamespace),
		  first_variable(kNoEntity), last_variable(kNoEntity),
		  first_function(kNoEntity), last_function(kNoEntity),
		  first_using_edge(kNoUsingEdge), last_using_edge(kNoUsingEdge),
		  first_using_predecessor(kNoUsingEdge),
		  last_using_predecessor(kNoUsingEdge), is_inline(inline_value) {}
};

struct EntityRecord
{
	NameId name;
	TypeId type;
	NamespaceId owner;
	EntityId next_in_namespace;

	EntityRecord(NameId entity_name, TypeId entity_type,
		NamespaceId entity_owner)
		: name(entity_name), type(entity_type), owner(entity_owner),
		  next_in_namespace(kNoEntity) {}
};

struct UsingEdgeRecord
{
	NamespaceId owner;
	NamespaceId target;
	UsingEdgeId next_from_owner;
	UsingEdgeId next_to_target;

	UsingEdgeRecord(NamespaceId edge_owner, NamespaceId edge_target)
		: owner(edge_owner), target(edge_target),
		  next_from_owner(kNoUsingEdge), next_to_target(kNoUsingEdge) {}
};

enum LookupKind
{
	LOOKUP_TYPE,
	LOOKUP_NAMESPACE,
	LOOKUP_VARIABLE,
	LOOKUP_FUNCTION,
	LOOKUP_USING_TARGET
};

struct LookupResult
{
	TypeId type;
	NamespaceId name_space;
	EntityId variable;
	EntityId function;

	LookupResult()
		: type(0), name_space(kNoNamespace), variable(kNoEntity),
		  function(kNoEntity) {}

	bool Found(LookupKind kind) const
	{
		switch (kind)
		{
		case LOOKUP_TYPE: return type != 0;
		case LOOKUP_NAMESPACE: return name_space != kNoNamespace;
		case LOOKUP_VARIABLE: return variable != kNoEntity;
		case LOOKUP_FUNCTION: return function != kNoEntity;
		case LOOKUP_USING_TARGET:
			return type != 0 || variable != kNoEntity || function != kNoEntity;
		}
		return false;
	}
};

struct LookupCacheEntry
{
	NamespaceId start;
	NameId name;
	LookupKind kind;
	std::uint32_t generation;
	LookupResult result;

	LookupCacheEntry(NamespaceId lookup_start, NameId lookup_name,
		LookupKind lookup_kind, std::uint32_t lookup_generation,
		const LookupResult& lookup_result)
		: start(lookup_start), name(lookup_name), kind(lookup_kind),
		  generation(lookup_generation), result(lookup_result) {}
};

class LookupCacheIndex
{
public:
	LookupCacheIndex() : slots_(32, 0) {}

	bool Find(NamespaceId start, NameId name, LookupKind kind,
		std::uint32_t generation,
		const std::vector<LookupCacheEntry>& entries,
		LookupResult* result) const
	{
		const std::uint32_t index = FindIndex(start, name, kind, entries);
		if (index == std::numeric_limits<std::uint32_t>::max() ||
			entries[index].generation != generation) return false;
		*result = entries[index].result;
		return true;
	}

	void Store(NamespaceId start, NameId name, LookupKind kind,
		std::uint32_t generation, const LookupResult& result,
		std::vector<LookupCacheEntry>* entries)
	{
		if ((entries->size() + 1) * 10 > slots_.size() * 7)
			Rehash(slots_.size() * 2, *entries);
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = Hash(start, name, kind) & mask;
		while (slots_[slot] != 0)
		{
			LookupCacheEntry& entry = (*entries)[slots_[slot] - 1];
			if (entry.start == start && entry.name == name &&
				entry.kind == kind)
			{
				entry.generation = generation;
				entry.result = result;
				return;
			}
			slot = (slot + 1) & mask;
		}
		entries->push_back(LookupCacheEntry(start, name, kind, generation,
			result));
		slots_[slot] = static_cast<std::uint32_t>(entries->size());
	}

	bool Invalidate(NamespaceId start, NameId name, LookupKind kind,
		std::uint32_t generation, std::vector<LookupCacheEntry>* entries)
	{
		const std::uint32_t index = FindIndex(start, name, kind, *entries);
		if (index == std::numeric_limits<std::uint32_t>::max() ||
			(*entries)[index].generation != generation) return false;
		(*entries)[index].generation = 0;
		return true;
	}

	std::size_t StorageBytes() const
	{
		return slots_.capacity() * sizeof(std::uint32_t);
	}

private:
	std::size_t Hash(NamespaceId start, NameId name, LookupKind kind) const
	{
		return MixHash(MixHash(MixHash(0, start), name), kind);
	}

	std::uint32_t FindIndex(NamespaceId start, NameId name, LookupKind kind,
		const std::vector<LookupCacheEntry>& entries) const
	{
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = Hash(start, name, kind) & mask;
		while (slots_[slot] != 0)
		{
			const std::uint32_t index = slots_[slot] - 1;
			const LookupCacheEntry& entry = entries[index];
			if (entry.start == start && entry.name == name &&
				entry.kind == kind) return index;
			slot = (slot + 1) & mask;
		}
		return std::numeric_limits<std::uint32_t>::max();
	}

	void Rehash(std::size_t capacity,
		const std::vector<LookupCacheEntry>& entries)
	{
		std::vector<std::uint32_t> replacement(capacity, 0);
		const std::size_t mask = capacity - 1;
		for (std::uint32_t i = 0; i < entries.size(); ++i)
		{
			const LookupCacheEntry& entry = entries[i];
			std::size_t slot = Hash(entry.start, entry.name, entry.kind) & mask;
			while (replacement[slot] != 0) slot = (slot + 1) & mask;
			replacement[slot] = i + 1;
		}
		slots_.swap(replacement);
	}

	std::vector<std::uint32_t> slots_;
};

class NameSequence
{
public:
	NameSequence() : inline_names_(), size_(0) {}

	void push_back(NameId name)
	{
		if (size_ < inline_names_.size()) inline_names_[size_] = name;
		else overflow_names_.push_back(name);
		++size_;
	}

	bool empty() const { return size_ == 0; }
	std::size_t size() const { return size_; }

	NameId operator[](std::size_t index) const
	{
		return index < inline_names_.size() ? inline_names_[index] :
			overflow_names_[index - inline_names_.size()];
	}

	NameId back() const { return (*this)[size_ - 1]; }

private:
	std::array<NameId, 4> inline_names_;
	std::vector<NameId> overflow_names_;
	std::size_t size_;
};

struct QualifiedName
{
	bool absolute;
	NameSequence segments;

	QualifiedName() : absolute(false) {}
};

class Program
{
public:
	explicit Program(Stats* stats)
		: stats_(stats), using_edge_slots_(16, 0), lookup_generation_(0)
	{
		namespaces_.push_back(NamespaceRecord());
		scope_lookup_generations_.push_back(1);
	}

	IdentifierTable identifiers;
	TypeTable types;

	NamespaceId OpenNamespace(NamespaceId parent, NameId name,
		bool is_inline)
	{
		if (name == 0)
		{
			NamespaceId existing = namespaces_[parent].unnamed_child;
			if (existing != kNoNamespace)
			{
				if (is_inline && !namespaces_[existing].is_inline)
					ThrowSemanticError("namespace inline mismatch");
				return existing;
			}
			const NamespaceId created = NewNamespace(parent, name, is_inline);
			namespaces_[parent].unnamed_child = created;
			AddUsingEdge(parent, created);
			return created;
		}

		Binding* binding = FindDirectBinding(parent, name);
		if (binding && binding->name_space != kNoNamespace)
		{
			const NamespaceId existing = binding->name_space;
			if (namespaces_[existing].parent != parent ||
				namespaces_[existing].name != name)
				ThrowSemanticError("namespace alias cannot be reopened");
			if (is_inline && !namespaces_[existing].is_inline)
				ThrowSemanticError("namespace inline mismatch");
			return existing;
		}
		if (binding && (binding->type != 0 || binding->variable != kNoEntity ||
			binding->function != kNoEntity))
			ThrowSemanticError("namespace name conflicts with declaration");

		const NamespaceId created = NewNamespace(parent, name, is_inline);
		InvalidateLookupName(parent, name);
		Binding& inserted = EnsureBinding(parent, name);
		inserted.name_space = created;
		if (is_inline) AddUsingEdge(parent, created);
		return created;
	}

	void AddNamespaceAlias(NamespaceId owner, NameId name,
		NamespaceId target)
	{
		Binding& binding = EnsureBinding(owner, name);
		if (binding.name_space == kNoNamespace)
		{
			if (binding.type != 0 || binding.variable != kNoEntity ||
				binding.function != kNoEntity)
				ThrowSemanticError("namespace alias name conflict");
			InvalidateLookupName(owner, name);
			binding.name_space = target;
			return;
		}
		if (binding.name_space != target)
			ThrowSemanticError("namespace alias target mismatch");
	}

	void AddUsingEdge(NamespaceId owner, NamespaceId target)
	{
		if ((using_edges_.size() + 1) * 10 > using_edge_slots_.size() * 7)
			RehashUsingEdges(using_edge_slots_.size() * 2);
		const std::size_t mask = using_edge_slots_.size() - 1;
		std::size_t slot = HashUsingEdge(owner, target) & mask;
		while (using_edge_slots_[slot] != 0)
		{
			const UsingEdgeRecord& existing =
				using_edges_[using_edge_slots_[slot] - 1];
			if (existing.owner == owner && existing.target == target) return;
			slot = (slot + 1) & mask;
		}
		if (using_edges_.size() >= kNoUsingEdge)
			ThrowSemanticResourceLimit("too many using-directive edges");
		const UsingEdgeId id = static_cast<UsingEdgeId>(using_edges_.size());
		using_edges_.push_back(UsingEdgeRecord(owner, target));
		using_edge_slots_[slot] = id + 1;
		AppendOutgoingEdge(owner, id);
		AppendIncomingEdge(target, id);
		InvalidateLookupGraph(owner);
		if (stats_) ++stats_->using_edges;
	}

	void AddUsingDeclaration(NamespaceId owner, NameId name,
		const LookupResult& target)
	{
		Binding& binding = EnsureBinding(owner, name);
		const bool changed = (target.type != 0 && binding.type != target.type) ||
			(target.variable != kNoEntity &&
				binding.variable != target.variable) ||
			(target.function != kNoEntity &&
				binding.function != target.function);
		if (changed) InvalidateLookupName(owner, name);
		MergeImportedType(&binding.type, target.type);
		MergeImportedEntity(&binding.variable, target.variable);
		MergeImportedEntity(&binding.function, target.function);
	}

	void AddTypeAlias(NamespaceId owner, NameId name, TypeId type)
	{
		Binding& binding = EnsureBinding(owner, name);
		if (binding.type != 0 && binding.type != type)
			ThrowSemanticError("type alias redeclaration mismatch");
		if (binding.variable != kNoEntity || binding.function != kNoEntity)
			ThrowSemanticError("type alias name conflict");
		if (binding.type != type) InvalidateLookupName(owner, name);
		binding.type = type;
	}

	void Declare(NamespaceId current, const QualifiedName& name, TypeId type)
	{
		if (name.segments.empty())
			ThrowSemanticError("declarator has no name");
		const bool function = types.IsFunction(type);
		const bool qualified = name.absolute || name.segments.size() > 1;
		NamespaceId owner = current;
		if (qualified && !ResolvePrefix(current, name, &owner))
			ThrowSemanticError("declarator qualifier lookup failed");
		const NameId unqualified = name.segments.back();

		if (qualified)
		{
			const LookupResult found = SearchGraph(owner, unqualified,
				function ? LOOKUP_FUNCTION : LOOKUP_VARIABLE);
			const EntityId entity = function ? found.function : found.variable;
			if (entity == kNoEntity)
				ThrowSemanticError("qualified declaration not found");
			entities_[entity].type = types.MergeRedeclaration(
				entities_[entity].type, type);
			return;
		}

		Binding& binding = EnsureBinding(owner, unqualified);
		EntityId& existing = function ? binding.function : binding.variable;
		if (existing != kNoEntity)
		{
			entities_[existing].type = types.MergeRedeclaration(
				entities_[existing].type, type);
			return;
		}
		if (binding.type != 0)
			ThrowSemanticError("declaration conflicts with type name");
		InvalidateLookupName(owner, unqualified);
		const EntityId entity = static_cast<EntityId>(entities_.size());
		entities_.push_back(EntityRecord(unqualified, type, owner));
		existing = entity;
		AppendEntity(owner, entity, function);
		if (stats_) ++stats_->declarations;
	}

	LookupResult LookupUnqualified(NamespaceId current, NameId name,
		LookupKind kind)
	{
		NamespaceId scope = current;
		while (scope != kNoNamespace)
		{
			const LookupResult result = SearchGraph(scope, name, kind);
			if (result.Found(kind)) return result;
			scope = namespaces_[scope].parent;
		}
		return LookupResult();
	}

	LookupResult LookupQualified(NamespaceId owner, NameId name,
		LookupKind kind)
	{
		return SearchGraph(owner, name, kind);
	}

	bool ResolveNamespaceName(NamespaceId current,
		const QualifiedName& name, NamespaceId* result)
	{
		if (name.segments.empty()) return false;
		NamespaceId owner = kNoNamespace;
		std::size_t index = 0;
		if (name.absolute)
		{
			owner = 0;
		}
		else
		{
			const LookupResult first = LookupUnqualified(current,
				name.segments[0], LOOKUP_NAMESPACE);
			if (!first.Found(LOOKUP_NAMESPACE)) return false;
			owner = first.name_space;
			index = 1;
		}
		for (; index < name.segments.size(); ++index)
		{
			const LookupResult next = LookupQualified(owner,
				name.segments[index], LOOKUP_NAMESPACE);
			if (!next.Found(LOOKUP_NAMESPACE)) return false;
			owner = next.name_space;
		}
		*result = owner;
		return true;
	}

	bool ResolveTypeName(NamespaceId current, const QualifiedName& name,
		TypeId* result)
	{
		if (name.segments.empty()) return false;
		if (!name.absolute && name.segments.size() == 1)
		{
			const LookupResult found = LookupUnqualified(current,
				name.segments[0], LOOKUP_TYPE);
			if (!found.Found(LOOKUP_TYPE)) return false;
			*result = found.type;
			return true;
		}
		NamespaceId owner;
		if (!ResolvePrefix(current, name, &owner)) return false;
		const LookupResult found = LookupQualified(owner,
			name.segments.back(), LOOKUP_TYPE);
		if (!found.Found(LOOKUP_TYPE)) return false;
		*result = found.type;
		return true;
	}

	bool ResolveUsingTarget(NamespaceId current, const QualifiedName& name,
		LookupResult* result)
	{
		if (name.segments.empty()) return false;
		NamespaceId owner;
		if (!ResolvePrefix(current, name, &owner)) return false;
		*result = LookupQualified(owner, name.segments.back(),
			LOOKUP_USING_TARGET);
		return result->Found(LOOKUP_USING_TARGET);
	}

	const std::string& Name(NameId name) const { return identifiers.Get(name); }
	std::size_t NamespaceCount() const { return namespaces_.size(); }
	std::size_t LookupCacheEntryCount() const
	{
		return lookup_cache_entries_.size();
	}

	std::size_t StorageBytes() const
	{
		std::size_t result = identifiers.StorageBytes() + types.StorageBytes() +
			namespaces_.capacity() * sizeof(NamespaceRecord) +
			entities_.capacity() * sizeof(EntityRecord) +
			bindings_.capacity() * sizeof(Binding) +
			binding_index_.StorageBytes() +
			using_edges_.capacity() * sizeof(UsingEdgeRecord) +
			using_edge_slots_.capacity() * sizeof(std::uint32_t) +
			lookup_cache_entries_.capacity() * sizeof(LookupCacheEntry) +
			lookup_cache_index_.StorageBytes() +
			scope_lookup_generations_.capacity() * sizeof(std::uint32_t) +
			lookup_marks_.capacity() * sizeof(std::uint32_t) +
			lookup_worklist_.capacity() * sizeof(NamespaceId);
		return result;
	}

	void Render(std::ostream& output) const
	{
		struct Frame
		{
			NamespaceId id;
			NamespaceId next_child;
			bool entered;

			explicit Frame(NamespaceId namespace_id)
				: id(namespace_id), next_child(kNoNamespace), entered(false) {}
		};
		std::vector<Frame> stack;
		stack.push_back(Frame(0));
		while (!stack.empty())
		{
			Frame& frame = stack.back();
			if (!frame.entered)
			{
				RenderNamespaceContents(output, frame.id);
				frame.next_child = namespaces_[frame.id].first_child;
				frame.entered = true;
			}
			if (frame.next_child != kNoNamespace)
			{
				const NamespaceId child = frame.next_child;
				frame.next_child = namespaces_[child].next_sibling;
				stack.push_back(Frame(child));
				continue;
			}
			output << "end namespace\n";
			stack.pop_back();
		}
	}

private:
	NamespaceId NewNamespace(NamespaceId parent, NameId name, bool is_inline)
	{
		if (namespaces_.size() > std::numeric_limits<NamespaceId>::max())
			ThrowSemanticResourceLimit("too many namespaces");
		const NamespaceId id = static_cast<NamespaceId>(namespaces_.size());
		namespaces_.push_back(NamespaceRecord(name, parent, is_inline));
		scope_lookup_generations_.push_back(1);
		NamespaceRecord& enclosing = namespaces_[parent];
		if (enclosing.last_child == kNoNamespace)
			enclosing.first_child = id;
		else
			namespaces_[enclosing.last_child].next_sibling = id;
		enclosing.last_child = id;
		return id;
	}

	Binding* FindDirectBinding(NamespaceId owner, NameId name)
	{
		const std::uint32_t index = binding_index_.Find(owner, name, bindings_);
		return index == std::numeric_limits<std::uint32_t>::max() ? 0 :
			&bindings_[index];
	}

	const Binding* FindDirectBinding(NamespaceId owner, NameId name) const
	{
		const std::uint32_t index = binding_index_.Find(owner, name, bindings_);
		return index == std::numeric_limits<std::uint32_t>::max() ? 0 :
			&bindings_[index];
	}

	Binding& EnsureBinding(NamespaceId owner, NameId name)
	{
		const std::uint32_t index = binding_index_.Ensure(owner, name,
			&bindings_);
		return bindings_[index];
	}

	std::size_t HashUsingEdge(NamespaceId owner, NamespaceId target) const
	{
		return MixHash(MixHash(0, owner), target);
	}

	void RehashUsingEdges(std::size_t capacity)
	{
		std::vector<std::uint32_t> replacement(capacity, 0);
		const std::size_t mask = capacity - 1;
		for (UsingEdgeId id = 0; id < using_edges_.size(); ++id)
		{
			const UsingEdgeRecord& edge = using_edges_[id];
			std::size_t slot = HashUsingEdge(edge.owner, edge.target) & mask;
			while (replacement[slot] != 0) slot = (slot + 1) & mask;
			replacement[slot] = id + 1;
		}
		using_edge_slots_.swap(replacement);
	}

	void AppendOutgoingEdge(NamespaceId owner, UsingEdgeId id)
	{
		NamespaceRecord& scope = namespaces_[owner];
		if (scope.last_using_edge == kNoUsingEdge)
			scope.first_using_edge = id;
		else
			using_edges_[scope.last_using_edge].next_from_owner = id;
		scope.last_using_edge = id;
	}

	void AppendIncomingEdge(NamespaceId target, UsingEdgeId id)
	{
		NamespaceRecord& scope = namespaces_[target];
		if (scope.last_using_predecessor == kNoUsingEdge)
			scope.first_using_predecessor = id;
		else
			using_edges_[scope.last_using_predecessor].next_to_target = id;
		scope.last_using_predecessor = id;
	}

	void AppendEntity(NamespaceId owner, EntityId id, bool function)
	{
		NamespaceRecord& scope = namespaces_[owner];
		EntityId& first = function ? scope.first_function : scope.first_variable;
		EntityId& last = function ? scope.last_function : scope.last_variable;
		if (last == kNoEntity) first = id;
		else entities_[last].next_in_namespace = id;
		last = id;
	}

	void BeginNamespaceWalk(NamespaceId start)
	{
		if (lookup_marks_.size() < namespaces_.size())
			lookup_marks_.resize(namespaces_.size(), 0);
		++lookup_generation_;
		if (lookup_generation_ == 0)
		{
			std::fill(lookup_marks_.begin(), lookup_marks_.end(), 0);
			++lookup_generation_;
		}
		lookup_worklist_.clear();
		lookup_worklist_.push_back(start);
		lookup_marks_[start] = lookup_generation_;
	}

	void AddWalkTarget(NamespaceId target)
	{
		if (lookup_marks_[target] == lookup_generation_) return;
		lookup_marks_[target] = lookup_generation_;
		lookup_worklist_.push_back(target);
	}

	void AdvanceScopeLookupGeneration(NamespaceId scope)
	{
		++scope_lookup_generations_[scope];
		if (scope_lookup_generations_[scope] != 0) return;
		for (std::size_t i = 0; i < lookup_cache_entries_.size(); ++i)
			lookup_cache_entries_[i].generation = 0;
		std::fill(scope_lookup_generations_.begin(),
			scope_lookup_generations_.end(), 1);
	}

	void InvalidateLookupGraph(NamespaceId changed)
	{
		if (lookup_cache_entries_.empty()) return;
		BeginNamespaceWalk(changed);
		for (std::size_t head = 0; head < lookup_worklist_.size(); ++head)
		{
			const NamespaceId current = lookup_worklist_[head];
			AdvanceScopeLookupGeneration(current);
			if (stats_) ++stats_->lookup_cache_invalidations;
			for (UsingEdgeId id =
				namespaces_[current].first_using_predecessor;
				id != kNoUsingEdge; id = using_edges_[id].next_to_target)
				AddWalkTarget(using_edges_[id].owner);
		}
	}

	void InvalidateLookupName(NamespaceId changed, NameId name)
	{
		if (lookup_cache_entries_.empty()) return;
		BeginNamespaceWalk(changed);
		for (std::size_t head = 0; head < lookup_worklist_.size(); ++head)
		{
			const NamespaceId current = lookup_worklist_[head];
			for (int value = LOOKUP_TYPE; value <= LOOKUP_USING_TARGET; ++value)
			{
				if (lookup_cache_index_.Invalidate(current, name,
					static_cast<LookupKind>(value),
					scope_lookup_generations_[current],
					&lookup_cache_entries_) && stats_)
					++stats_->lookup_cache_invalidations;
			}
			for (UsingEdgeId id =
				namespaces_[current].first_using_predecessor;
				id != kNoUsingEdge; id = using_edges_[id].next_to_target)
				AddWalkTarget(using_edges_[id].owner);
		}
	}

	LookupResult DirectLookup(NamespaceId owner, NameId name,
		LookupKind kind) const
	{
		LookupResult result;
		const Binding* binding = FindDirectBinding(owner, name);
		if (!binding) return result;
		switch (kind)
		{
		case LOOKUP_TYPE: result.type = binding->type; break;
		case LOOKUP_NAMESPACE: result.name_space = binding->name_space; break;
		case LOOKUP_VARIABLE: result.variable = binding->variable; break;
		case LOOKUP_FUNCTION: result.function = binding->function; break;
		case LOOKUP_USING_TARGET:
			result.type = binding->type;
			result.variable = binding->variable;
			result.function = binding->function;
			break;
		}
		return result;
	}

	LookupResult SearchGraph(NamespaceId start, NameId name, LookupKind kind)
	{
		if (stats_) ++stats_->lookup_queries;
		if (namespaces_[start].first_using_edge == kNoUsingEdge)
		{
			if (stats_) ++stats_->lookup_scope_visits;
			return DirectLookup(start, name, kind);
		}
		LookupResult result;
		bool start_checked = false;
		if (lookup_cache_entries_.empty())
		{
			if (stats_) ++stats_->lookup_scope_visits;
			result = DirectLookup(start, name, kind);
			if (result.Found(kind)) return result;
			start_checked = true;
		}
		else if (lookup_cache_index_.Find(start, name, kind,
			scope_lookup_generations_[start], lookup_cache_entries_, &result))
		{
			if (stats_) ++stats_->lookup_cache_hits;
			return result;
		}
		else
		{
			if (stats_) ++stats_->lookup_scope_visits;
			result = DirectLookup(start, name, kind);
			if (result.Found(kind)) return result;
			start_checked = true;
		}
		if (stats_) ++stats_->lookup_cache_misses;
		BeginNamespaceWalk(start);
		bool traversed_edge = false;
		for (std::size_t head = 0; head < lookup_worklist_.size(); ++head)
		{
			const NamespaceId current = lookup_worklist_[head];
			LookupResult direct;
			if (start_checked && current == start)
				start_checked = false;
			else
			{
				if (stats_) ++stats_->lookup_scope_visits;
				direct = DirectLookup(current, name, kind);
			}
			if (direct.Found(kind))
			{
				if (traversed_edge)
				{
					lookup_cache_index_.Store(start, name, kind,
						scope_lookup_generations_[start], direct,
						&lookup_cache_entries_);
				}
				return direct;
			}
			for (UsingEdgeId id = namespaces_[current].first_using_edge;
				id != kNoUsingEdge; id = using_edges_[id].next_from_owner)
			{
				if (stats_) ++stats_->lookup_edge_visits;
				traversed_edge = true;
				AddWalkTarget(using_edges_[id].target);
			}
		}
		if (traversed_edge)
		{
			lookup_cache_index_.Store(start, name, kind,
				scope_lookup_generations_[start], result,
				&lookup_cache_entries_);
		}
		return result;
	}

	bool ResolvePrefix(NamespaceId current, const QualifiedName& name,
		NamespaceId* result)
	{
		const std::size_t prefix_count = name.segments.size() - 1;
		if (prefix_count == 0)
		{
			if (!name.absolute) return false;
			*result = 0;
			return true;
		}
		NamespaceId owner = kNoNamespace;
		std::size_t index = 0;
		if (name.absolute)
		{
			owner = 0;
		}
		else
		{
			const LookupResult first = LookupUnqualified(current,
				name.segments[0], LOOKUP_NAMESPACE);
			if (!first.Found(LOOKUP_NAMESPACE)) return false;
			owner = first.name_space;
			index = 1;
		}
		for (; index < prefix_count; ++index)
		{
			const LookupResult next = LookupQualified(owner,
				name.segments[index], LOOKUP_NAMESPACE);
			if (!next.Found(LOOKUP_NAMESPACE)) return false;
			owner = next.name_space;
		}
		*result = owner;
		return true;
	}

	void MergeImportedType(TypeId* destination, TypeId source)
	{
		if (source == 0) return;
		if (*destination != 0 && *destination != source)
			ThrowSemanticError("using declaration type conflict");
		*destination = source;
	}

	void MergeImportedEntity(EntityId* destination, EntityId source)
	{
		if (source == kNoEntity) return;
		if (*destination != kNoEntity && *destination != source)
			ThrowSemanticError("using declaration entity conflict");
		*destination = source;
	}

	void RenderNamespaceContents(std::ostream& output, NamespaceId id) const
	{
		const NamespaceRecord& scope = namespaces_[id];
		if (scope.name == 0) output << "start unnamed namespace\n";
		else output << "start namespace " << Name(scope.name) << '\n';
		if (scope.is_inline) output << "inline namespace\n";
		for (EntityId id = scope.first_variable; id != kNoEntity;
			id = entities_[id].next_in_namespace)
		{
			const EntityRecord& entity = entities_[id];
			output << "variable " << Name(entity.name) << ' ';
			types.Render(output, entity.type);
			output << '\n';
		}
		for (EntityId id = scope.first_function; id != kNoEntity;
			id = entities_[id].next_in_namespace)
		{
			const EntityRecord& entity = entities_[id];
			output << "function " << Name(entity.name) << ' ';
			types.Render(output, entity.type);
			output << '\n';
		}
	}

	Stats* stats_;
	std::vector<NamespaceRecord> namespaces_;
	std::vector<EntityRecord> entities_;
	std::vector<Binding> bindings_;
	BindingIndex binding_index_;
	std::vector<UsingEdgeRecord> using_edges_;
	std::vector<std::uint32_t> using_edge_slots_;
	std::vector<LookupCacheEntry> lookup_cache_entries_;
	LookupCacheIndex lookup_cache_index_;
	std::vector<std::uint32_t> scope_lookup_generations_;
	std::vector<std::uint32_t> lookup_marks_;
	std::vector<NamespaceId> lookup_worklist_;
	std::uint32_t lookup_generation_;
};

struct DeclSpecifiers
{
	TypeId type;
	bool is_typedef;

	DeclSpecifiers() : type(0), is_typedef(false) {}
};

enum DeclaratorOperationKind
{
	DECL_POINTER,
	DECL_LVALUE_REFERENCE,
	DECL_RVALUE_REFERENCE,
	DECL_ARRAY,
	DECL_FUNCTION
};

struct DeclaratorOperation
{
	DeclaratorOperationKind kind;
	unsigned char cv;
	std::uint64_t bound;
	bool variadic;
	std::vector<TypeId> parameters;

	explicit DeclaratorOperation(DeclaratorOperationKind operation_kind)
		: kind(operation_kind), cv(CV_NONE), bound(0), variadic(false) {}
};

struct Declarator
{
	QualifiedName name;
	bool has_name;
	std::vector<DeclaratorOperation> operations;

	Declarator() : has_name(false) {}

	std::size_t ChildStorageBytes() const
	{
		std::size_t result = operations.capacity() *
			sizeof(DeclaratorOperation);
		for (std::size_t i = 0; i < operations.size(); ++i)
			result += operations[i].parameters.capacity() * sizeof(TypeId);
		return result;
	}
};

struct DeclaratorMemoEntry
{
	std::uint64_t key;
	std::size_t end;
	bool success;
	Declarator declarator;

	DeclaratorMemoEntry(std::uint64_t memo_key, std::size_t memo_end,
		bool memo_success, Declarator&& memo_declarator)
		: key(memo_key), end(memo_end), success(memo_success),
		  declarator(std::move(memo_declarator)) {}
};

struct DeclaratorMemoSlot
{
	std::uint64_t key;
	std::uint32_t entry;
	std::uint32_t generation;

	DeclaratorMemoSlot() : key(0), entry(0), generation(0) {}
};

enum DeclaratorMode
{
	DECLARATOR_NAMED,
	DECLARATOR_ABSTRACT
};

struct FundamentalSpecifiers
{
	bool saw_char;
	bool saw_char16;
	bool saw_char32;
	bool saw_wchar;
	bool saw_bool;
	bool saw_short;
	bool saw_int;
	bool saw_signed;
	bool saw_unsigned;
	bool saw_float;
	bool saw_double;
	bool saw_void;
	unsigned int long_count;

	FundamentalSpecifiers()
		: saw_char(false), saw_char16(false), saw_char32(false),
		  saw_wchar(false), saw_bool(false), saw_short(false), saw_int(false),
		  saw_signed(false), saw_unsigned(false), saw_float(false),
		  saw_double(false), saw_void(false), long_count(0) {}

	bool Empty() const
	{
		return !saw_char && !saw_char16 && !saw_char32 && !saw_wchar &&
			!saw_bool && !saw_short && !saw_int && !saw_signed &&
			!saw_unsigned && !saw_float && !saw_double && !saw_void &&
			long_count == 0;
	}
};

class Parser
{
public:
	Parser(const std::vector<SemanticToken>& tokens,
		Program& model, Stats* stats)
		: tokens_(tokens), model_(model), stats_(stats), position_(0),
		  current_namespace_(0), declarator_memo_generation_(0),
		  declarator_call_depth_(0), memo_child_storage_bytes_(0),
		  active_declarator_stack_bytes_(0) {}

	void Parse()
	{
		while (true)
		{
			if (AtEof())
			{
				if (!namespace_stack_.empty())
					ThrowSyntaxError("unterminated namespace");
				return;
			}
			if (Match(OP_RBRACE))
			{
				if (namespace_stack_.empty())
					ThrowSyntaxError("unexpected namespace close");
				current_namespace_ = namespace_stack_.back();
				namespace_stack_.pop_back();
				continue;
			}
			ParseDeclaration();
		}
	}

private:
	void RecordParserStack()
	{
		if (!stats_) return;
		const std::size_t bytes = active_declarator_stack_bytes_ +
			namespace_stack_.capacity() * sizeof(NamespaceId);
		stats_->peak_parser_scratch_bytes = std::max(
			stats_->peak_parser_scratch_bytes, bytes);
	}

	void RecordDeclaratorFrame(std::size_t capacity_bytes,
		std::size_t* accounted_bytes)
	{
		if (!stats_) return;
		++stats_->declarator_frames;
		if (capacity_bytes > *accounted_bytes)
		{
			active_declarator_stack_bytes_ +=
				capacity_bytes - *accounted_bytes;
			*accounted_bytes = capacity_bytes;
		}
		RecordParserStack();
	}

	void BeginDeclaratorMemoSession()
	{
		if (declarator_call_depth_ != 0) return;
		// No declaration is published while one top-level declarator is parsed,
		// so position and mode are complete keys for this generation.
		++declarator_memo_generation_;
		if (declarator_memo_generation_ == 0)
		{
			for (std::size_t i = 0; i < declarator_memo_slots_.size(); ++i)
				declarator_memo_slots_[i].generation = 0;
			++declarator_memo_generation_;
		}
		declarator_memo_entries_.clear();
		memo_child_storage_bytes_ = 0;
	}

	std::uint64_t DeclaratorMemoKey(std::size_t position,
		DeclaratorMode mode) const
	{
		return (static_cast<std::uint64_t>(position) << 1) |
			static_cast<std::uint64_t>(mode);
	}

	std::size_t HashDeclaratorMemo(std::uint64_t key) const
	{
		return MixHash(0, key);
	}

	void RehashDeclaratorMemo(std::size_t capacity)
	{
		std::vector<DeclaratorMemoSlot> replacement(capacity);
		const std::size_t mask = capacity - 1;
		for (std::uint32_t i = 0; i < declarator_memo_entries_.size(); ++i)
		{
			const std::uint64_t key = declarator_memo_entries_[i].key;
			std::size_t slot = HashDeclaratorMemo(key) & mask;
			while (replacement[slot].generation ==
				declarator_memo_generation_)
				slot = (slot + 1) & mask;
			replacement[slot].key = key;
			replacement[slot].entry = i;
			replacement[slot].generation = declarator_memo_generation_;
		}
		declarator_memo_slots_.swap(replacement);
	}

	bool FindDeclaratorMemo(std::uint64_t key, bool* success,
		Declarator* result)
	{
		if (declarator_memo_slots_.empty()) return false;
		const std::size_t mask = declarator_memo_slots_.size() - 1;
		std::size_t slot = HashDeclaratorMemo(key) & mask;
		while (declarator_memo_slots_[slot].generation ==
			declarator_memo_generation_)
		{
			const DeclaratorMemoSlot& item = declarator_memo_slots_[slot];
			if (item.key == key)
			{
				const DeclaratorMemoEntry& entry =
					declarator_memo_entries_[item.entry];
				position_ = entry.end;
				*success = entry.success;
				if (entry.success) *result = entry.declarator;
				return true;
			}
			slot = (slot + 1) & mask;
		}
		return false;
	}

	void StoreDeclaratorMemo(std::uint64_t key, std::size_t end,
		bool success, Declarator&& declarator)
	{
		if (declarator_memo_slots_.empty())
			declarator_memo_slots_.resize(32);
		if ((declarator_memo_entries_.size() + 1) * 10 >
			declarator_memo_slots_.size() * 7)
			RehashDeclaratorMemo(declarator_memo_slots_.size() * 2);
		if (declarator_memo_entries_.size() >=
			std::numeric_limits<std::uint32_t>::max())
			ThrowSemanticResourceLimit("too many declarator memo entries");
		const std::size_t mask = declarator_memo_slots_.size() - 1;
		std::size_t slot = HashDeclaratorMemo(key) & mask;
		while (declarator_memo_slots_[slot].generation ==
			declarator_memo_generation_)
			slot = (slot + 1) & mask;
		const std::uint32_t index =
			static_cast<std::uint32_t>(declarator_memo_entries_.size());
		declarator_memo_entries_.push_back(DeclaratorMemoEntry(key, end,
			success, std::move(declarator)));
		declarator_memo_slots_[slot].key = key;
		declarator_memo_slots_[slot].entry = index;
		declarator_memo_slots_[slot].generation =
			declarator_memo_generation_;
		memo_child_storage_bytes_ +=
			declarator_memo_entries_.back().declarator.ChildStorageBytes();
		if (!stats_) return;
		stats_->declarator_memo_entries = std::max(
			stats_->declarator_memo_entries,
			declarator_memo_entries_.size());
		const std::size_t storage = declarator_memo_slots_.capacity() *
				sizeof(DeclaratorMemoSlot) +
			declarator_memo_entries_.capacity() *
				sizeof(DeclaratorMemoEntry) + memo_child_storage_bytes_;
		stats_->parser_memo_storage_bytes = std::max(
			stats_->parser_memo_storage_bytes, storage);
	}

	bool At(SimpleTokenKind kind) const
	{
		return position_ < tokens_.size() &&
			tokens_[position_].kind == static_cast<std::uint16_t>(kind);
	}

	bool AtOffset(std::size_t offset, SimpleTokenKind kind) const
	{
		return position_ + offset < tokens_.size() &&
			tokens_[position_ + offset].kind ==
				static_cast<std::uint16_t>(kind);
	}

	bool AtIdentifier() const
	{
		return position_ < tokens_.size() &&
			tokens_[position_].kind == kIdentifierToken;
	}

	bool AtLiteral() const
	{
		return position_ < tokens_.size() &&
			tokens_[position_].kind == kLiteralToken;
	}

	bool AtEof() const
	{
		return position_ < tokens_.size() &&
			tokens_[position_].kind == kEofToken;
	}

	bool Match(SimpleTokenKind kind)
	{
		if (!At(kind)) return false;
		++position_;
		return true;
	}

	void Expect(SimpleTokenKind kind)
	{
		if (!Match(kind))
			ThrowSyntaxError(std::string("expected ") +
				SimpleTokenKindName(kind));
	}

	NameId ConsumeIdentifier()
	{
		if (!AtIdentifier()) ThrowSyntaxError("expected identifier");
		return tokens_[position_++].name;
	}

	bool ParseQualifiedName(QualifiedName* result)
	{
		const std::size_t start = position_;
		QualifiedName parsed;
		parsed.absolute = Match(OP_COLON2);
		if (!AtIdentifier())
		{
			position_ = start;
			return false;
		}
		parsed.segments.push_back(ConsumeIdentifier());
		while (Match(OP_COLON2))
		{
			if (!AtIdentifier())
			{
				position_ = start;
				return false;
			}
			parsed.segments.push_back(ConsumeIdentifier());
		}
		*result = std::move(parsed);
		return true;
	}

	bool TryParseTypeName(TypeId* result)
	{
		const std::size_t start = position_;
		QualifiedName name;
		if (!ParseQualifiedName(&name) ||
			!model_.ResolveTypeName(current_namespace_, name, result))
		{
			position_ = start;
			return false;
		}
		return true;
	}

	bool ConsumeFundamental(FundamentalSpecifiers* specifiers)
	{
		if (Match(KW_CHAR)) specifiers->saw_char = true;
		else if (Match(KW_CHAR16_T)) specifiers->saw_char16 = true;
		else if (Match(KW_CHAR32_T)) specifiers->saw_char32 = true;
		else if (Match(KW_WCHAR_T)) specifiers->saw_wchar = true;
		else if (Match(KW_BOOL)) specifiers->saw_bool = true;
		else if (Match(KW_SHORT)) specifiers->saw_short = true;
		else if (Match(KW_INT)) specifiers->saw_int = true;
		else if (Match(KW_SIGNED)) specifiers->saw_signed = true;
		else if (Match(KW_UNSIGNED)) specifiers->saw_unsigned = true;
		else if (Match(KW_FLOAT)) specifiers->saw_float = true;
		else if (Match(KW_DOUBLE)) specifiers->saw_double = true;
		else if (Match(KW_VOID)) specifiers->saw_void = true;
		else if (Match(KW_LONG)) ++specifiers->long_count;
		else return false;
		return true;
	}

	FundamentalType SelectFundamental(const FundamentalSpecifiers& value)
	{
		if (value.saw_char)
		{
			if (value.saw_unsigned) return FT_UNSIGNED_CHAR;
			if (value.saw_signed) return FT_SIGNED_CHAR;
			return FT_CHAR;
		}
		if (value.saw_char16) return FT_CHAR16_T;
		if (value.saw_char32) return FT_CHAR32_T;
		if (value.saw_wchar) return FT_WCHAR_T;
		if (value.saw_bool) return FT_BOOL;
		if (value.saw_float) return FT_FLOAT;
		if (value.saw_double)
			return value.long_count == 0 ? FT_DOUBLE : FT_LONG_DOUBLE;
		if (value.saw_void) return FT_VOID;
		if (value.saw_short)
			return value.saw_unsigned ? FT_UNSIGNED_SHORT_INT : FT_SHORT_INT;
		if (value.long_count >= 2)
		{
			return value.saw_unsigned ? FT_UNSIGNED_LONG_LONG_INT :
				FT_LONG_LONG_INT;
		}
		if (value.long_count == 1)
			return value.saw_unsigned ? FT_UNSIGNED_LONG_INT : FT_LONG_INT;
		return value.saw_unsigned ? FT_UNSIGNED_INT : FT_INT;
	}

	bool ParseDeclSpecifiers(DeclSpecifiers* result)
	{
		const std::size_t start = position_;
		FundamentalSpecifiers fundamental;
		TypeId named_type = 0;
		unsigned char cv = CV_NONE;
		bool is_typedef = false;
		bool consumed = false;
		while (true)
		{
			if (Match(KW_STATIC) || Match(KW_THREAD_LOCAL) || Match(KW_EXTERN))
			{
				consumed = true;
				continue;
			}
			if (Match(KW_TYPEDEF))
			{
				is_typedef = true;
				consumed = true;
				continue;
			}
			if (Match(KW_CONST))
			{
				cv |= CV_CONST;
				consumed = true;
				continue;
			}
			if (Match(KW_VOLATILE))
			{
				cv |= CV_VOLATILE;
				consumed = true;
				continue;
			}
			if (named_type == 0 && ConsumeFundamental(&fundamental))
			{
				consumed = true;
				continue;
			}
			if (named_type == 0 && fundamental.Empty() &&
				TryParseTypeName(&named_type))
			{
				consumed = true;
				continue;
			}
			break;
		}
		if (!consumed || (named_type == 0 && fundamental.Empty()))
		{
			position_ = start;
			return false;
		}
		TypeId type = named_type != 0 ? named_type :
			model_.types.Fundamental(SelectFundamental(fundamental));
		result->type = model_.types.Qualify(type, cv);
		result->is_typedef = is_typedef;
		return true;
	}

	bool ParsePointerOperator(DeclaratorOperation* operation)
	{
		if (Match(OP_STAR))
		{
			*operation = DeclaratorOperation(DECL_POINTER);
			while (true)
			{
				if (Match(KW_CONST)) operation->cv |= CV_CONST;
				else if (Match(KW_VOLATILE)) operation->cv |= CV_VOLATILE;
				else break;
			}
			return true;
		}
		if (Match(OP_AMP))
		{
			*operation = DeclaratorOperation(DECL_LVALUE_REFERENCE);
			return true;
		}
		if (Match(OP_LAND))
		{
			*operation = DeclaratorOperation(DECL_RVALUE_REFERENCE);
			return true;
		}
		return false;
	}

	bool ParseArrayOperation(DeclaratorOperation* operation)
	{
		const std::size_t start = position_;
		if (!Match(OP_LSQUARE)) return false;
		*operation = DeclaratorOperation(DECL_ARRAY);
		if (AtLiteral()) operation->bound = tokens_[position_++].value;
		if (!Match(OP_RSQUARE))
		{
			position_ = start;
			return false;
		}
		return true;
	}

	bool IsParameterEnd() const
	{
		return At(OP_COMMA) || At(OP_RPAREN) || At(OP_DOTS);
	}

	bool ParseParameter(TypeId* result, bool* bare_void)
	{
		DeclSpecifiers specifiers;
		if (!ParseDeclSpecifiers(&specifiers)) return false;
		Declarator declarator;
		bool has_declarator = false;
		if (!IsParameterEnd())
		{
			const std::size_t declarator_start = position_;
			if (ParseDeclarator(DECLARATOR_NAMED, &declarator) &&
				IsParameterEnd())
			{
				has_declarator = true;
			}
			else
			{
				position_ = declarator_start;
				if (!ParseDeclarator(DECLARATOR_ABSTRACT, &declarator) ||
					!IsParameterEnd()) return false;
				has_declarator = true;
			}
		}
		TypeId type = ApplyDeclarator(specifiers.type, declarator);
		*bare_void = !has_declarator && model_.types.IsVoid(type);
		*result = model_.types.AdjustParameter(type);
		return true;
	}

	bool ParseFunctionOperation(DeclaratorOperation* operation)
	{
		const std::size_t start = position_;
		if (!Match(OP_LPAREN)) return false;
		DeclaratorOperation parsed(DECL_FUNCTION);
		bool saw_bare_void = false;
		if (Match(OP_RPAREN))
		{
			*operation = std::move(parsed);
			return true;
		}
		if (Match(OP_DOTS))
		{
			if (!Match(OP_RPAREN))
			{
				position_ = start;
				return false;
			}
			parsed.variadic = true;
			*operation = std::move(parsed);
			return true;
		}
		while (true)
		{
			TypeId parameter;
			bool bare_void = false;
			if (!ParseParameter(&parameter, &bare_void))
			{
				position_ = start;
				return false;
			}
			parsed.parameters.push_back(parameter);
			saw_bare_void = saw_bare_void || bare_void;
			if (Match(OP_DOTS))
			{
				parsed.variadic = true;
				break;
			}
			if (!Match(OP_COMMA)) break;
			if (Match(OP_DOTS))
			{
				parsed.variadic = true;
				break;
			}
		}
		if (!Match(OP_RPAREN))
		{
			position_ = start;
			return false;
		}
		if (saw_bare_void)
		{
			if (parsed.parameters.size() != 1 || parsed.variadic)
				ThrowSemanticError("void parameter in parameter list");
			parsed.parameters.clear();
		}
		*operation = std::move(parsed);
		return true;
	}

	bool ParseDeclarator(DeclaratorMode mode, Declarator* result)
	{
		BeginDeclaratorMemoSession();
		if (declarator_call_depth_ >= kMaxDeclaratorCallDepth)
			ThrowSemanticResourceLimit("declarator nesting limit exceeded");
		++declarator_call_depth_;
		const std::size_t start = position_;
		if (declarator_call_depth_ == 1)
		{
			const bool success = ParseDeclaratorUncached(mode, result);
			--declarator_call_depth_;
			return success;
		}
		const std::uint64_t key = DeclaratorMemoKey(start, mode);
		bool success = false;
		if (FindDeclaratorMemo(key, &success, result))
		{
			if (stats_) ++stats_->declarator_cache_hits;
			--declarator_call_depth_;
			return success;
		}
		if (stats_) ++stats_->declarator_cache_misses;
		Declarator parsed;
		success = ParseDeclaratorUncached(mode, &parsed);
		StoreDeclaratorMemo(key, position_, success, std::move(parsed));
		if (success)
			*result = declarator_memo_entries_.back().declarator;
		--declarator_call_depth_;
		return success;
	}

	bool ParseDeclaratorUncached(DeclaratorMode mode, Declarator* result)
	{
		enum FrameState
		{
			FRAME_START,
			FRAME_WAITING_FOR_GROUP,
			FRAME_SUFFIXES
		};
		struct Frame
		{
			std::size_t start;
			std::size_t group_start;
			FrameState state;
			bool direct;
			bool suffix;
			bool failed;
			Declarator parsed;
			std::vector<DeclaratorOperation> prefixes;

			explicit Frame(std::size_t frame_start)
				: start(frame_start), group_start(frame_start),
				  state(FRAME_START), direct(false), suffix(false),
				  failed(false) {}
		};

		std::vector<Frame> frames;
		frames.push_back(Frame(position_));
		std::size_t accounted_frame_bytes = 0;
		RecordDeclaratorFrame(frames.capacity() * sizeof(Frame),
			&accounted_frame_bytes);
		while (true)
		{
			Frame& frame = frames.back();
			if (frame.state == FRAME_START)
			{
				while (true)
				{
					DeclaratorOperation operation(DECL_POINTER);
					if (!ParsePointerOperator(&operation)) break;
					frame.prefixes.push_back(std::move(operation));
				}
				if (mode == DECLARATOR_NAMED &&
					(AtIdentifier() || At(OP_COLON2)))
				{
					if (!ParseQualifiedName(&frame.parsed.name))
					{
						position_ = frame.start;
						frame.failed = true;
						frame.state = FRAME_SUFFIXES;
						continue;
					}
					frame.parsed.has_name = true;
					frame.direct = true;
					frame.state = FRAME_SUFFIXES;
					continue;
				}
				if (At(OP_LPAREN))
				{
					frame.group_start = position_++;
					frame.state = FRAME_WAITING_FOR_GROUP;
					frames.push_back(Frame(position_));
					RecordDeclaratorFrame(frames.capacity() * sizeof(Frame),
						&accounted_frame_bytes);
					continue;
				}
				frame.state = FRAME_SUFFIXES;
				continue;
			}
			if (frame.state == FRAME_WAITING_FOR_GROUP)
				ThrowSemanticInternal("declarator frame did not resume");

			while (!frame.failed)
			{
				DeclaratorOperation operation(DECL_ARRAY);
				if (ParseArrayOperation(&operation) ||
					ParseFunctionOperation(&operation))
				{
					frame.parsed.operations.push_back(std::move(operation));
					frame.suffix = true;
					continue;
				}
				break;
			}
			for (std::vector<DeclaratorOperation>::reverse_iterator i =
				frame.prefixes.rbegin(); i != frame.prefixes.rend(); ++i)
				frame.parsed.operations.push_back(std::move(*i));

			const bool success = !frame.failed &&
				(frame.direct || frame.suffix || !frame.prefixes.empty()) &&
				(mode != DECLARATOR_NAMED || frame.parsed.has_name) &&
				(mode != DECLARATOR_ABSTRACT || !frame.parsed.has_name);
			const std::size_t frame_start = frame.start;
			Declarator completed = std::move(frame.parsed);
			frames.pop_back();
			if (frames.empty())
			{
				active_declarator_stack_bytes_ -= accounted_frame_bytes;
				if (!success)
				{
					position_ = frame_start;
					return false;
				}
				*result = std::move(completed);
				return true;
			}
			Frame& parent = frames.back();
			if (parent.state != FRAME_WAITING_FOR_GROUP)
				ThrowSemanticInternal("invalid declarator frame state");
			if (success && Match(OP_RPAREN))
			{
				parent.parsed = std::move(completed);
				parent.direct = true;
			}
			else
			{
				position_ = parent.group_start;
			}
			parent.state = FRAME_SUFFIXES;
		}
	}

	TypeId ApplyDeclarator(TypeId base, const Declarator& declarator)
	{
		TypeId type = base;
		for (std::vector<DeclaratorOperation>::const_reverse_iterator i =
			declarator.operations.rbegin();
			i != declarator.operations.rend(); ++i)
		{
			switch (i->kind)
			{
			case DECL_POINTER:
				type = model_.types.Qualify(model_.types.Pointer(type), i->cv);
				break;
			case DECL_LVALUE_REFERENCE:
				type = model_.types.Reference(TYPE_LVALUE_REFERENCE, type);
				break;
			case DECL_RVALUE_REFERENCE:
				type = model_.types.Reference(TYPE_RVALUE_REFERENCE, type);
				break;
			case DECL_ARRAY:
				type = model_.types.Array(type, i->bound);
				break;
			case DECL_FUNCTION:
				type = model_.types.Function(type, i->parameters, i->variadic);
				break;
			}
		}
		return type;
	}

	void ParseDeclaration()
	{
		if (At(KW_NAMESPACE) || At(KW_INLINE))
		{
			ParseNamespaceDeclaration();
			return;
		}
		if (At(KW_USING))
		{
			ParseUsingDeclaration();
			return;
		}
		if (Match(OP_SEMICOLON)) return;
		ParseSimpleDeclaration();
	}

	void ParseNamespaceDeclaration()
	{
		const bool is_inline = Match(KW_INLINE);
		Expect(KW_NAMESPACE);
		if (!is_inline && AtIdentifier() && AtOffset(1, OP_ASS))
		{
			const NameId alias = ConsumeIdentifier();
			Expect(OP_ASS);
			QualifiedName target_name;
			if (!ParseQualifiedName(&target_name))
				ThrowSyntaxError("expected namespace alias target");
			NamespaceId target;
			if (!model_.ResolveNamespaceName(current_namespace_, target_name,
				&target))
				ThrowSemanticError("namespace alias lookup failed");
			Expect(OP_SEMICOLON);
			model_.AddNamespaceAlias(current_namespace_, alias, target);
			return;
		}

		NameId name = 0;
		if (AtIdentifier()) name = ConsumeIdentifier();
		Expect(OP_LBRACE);
		namespace_stack_.push_back(current_namespace_);
		RecordParserStack();
		current_namespace_ = model_.OpenNamespace(current_namespace_, name,
			is_inline);
	}

	void ParseUsingDeclaration()
	{
		Expect(KW_USING);
		if (Match(KW_NAMESPACE))
		{
			QualifiedName target_name;
			if (!ParseQualifiedName(&target_name))
				ThrowSyntaxError("expected using-directive target");
			NamespaceId target;
			if (!model_.ResolveNamespaceName(current_namespace_, target_name,
				&target))
				ThrowSemanticError("using-directive lookup failed");
			Expect(OP_SEMICOLON);
			model_.AddUsingEdge(current_namespace_, target);
			return;
		}

		if (AtIdentifier() && AtOffset(1, OP_ASS))
		{
			const NameId alias = ConsumeIdentifier();
			Expect(OP_ASS);
			DeclSpecifiers specifiers;
			if (!ParseDeclSpecifiers(&specifiers))
				ThrowSyntaxError("expected alias type-id");
			Declarator declarator;
			if (!At(OP_SEMICOLON) &&
				!ParseDeclarator(DECLARATOR_ABSTRACT, &declarator))
				ThrowSyntaxError("invalid alias declarator");
			Expect(OP_SEMICOLON);
			model_.AddTypeAlias(current_namespace_, alias,
				ApplyDeclarator(specifiers.type, declarator));
			return;
		}

		QualifiedName target_name;
		if (!ParseQualifiedName(&target_name) ||
			(!target_name.absolute && target_name.segments.size() < 2))
			ThrowSyntaxError("expected qualified using-declaration");
		LookupResult target;
		if (!model_.ResolveUsingTarget(current_namespace_, target_name, &target))
			ThrowSemanticError("using-declaration lookup failed");
		Expect(OP_SEMICOLON);
		model_.AddUsingDeclaration(current_namespace_,
			target_name.segments.back(), target);
	}

	void ParseSimpleDeclaration()
	{
		DeclSpecifiers specifiers;
		if (!ParseDeclSpecifiers(&specifiers))
			ThrowSyntaxError("expected declaration specifiers");
		while (true)
		{
			Declarator declarator;
			if (!ParseDeclarator(DECLARATOR_NAMED, &declarator))
				ThrowSyntaxError("expected declarator");
			const TypeId type = ApplyDeclarator(specifiers.type, declarator);
			if (specifiers.is_typedef)
			{
				if (declarator.name.absolute ||
					declarator.name.segments.size() != 1)
					ThrowSemanticError("qualified typedef declarator");
				model_.AddTypeAlias(current_namespace_,
					declarator.name.segments[0], type);
			}
			else
			{
				model_.Declare(current_namespace_, declarator.name, type);
			}
			if (!Match(OP_COMMA)) break;
		}
		Expect(OP_SEMICOLON);
	}

	const std::vector<SemanticToken>& tokens_;
	Program& model_;
	Stats* stats_;
	std::size_t position_;
	NamespaceId current_namespace_;
	std::vector<NamespaceId> namespace_stack_;
	std::vector<DeclaratorMemoSlot> declarator_memo_slots_;
	std::vector<DeclaratorMemoEntry> declarator_memo_entries_;
	std::uint32_t declarator_memo_generation_;
	std::size_t declarator_call_depth_;
	std::size_t memo_child_storage_bytes_;
	std::size_t active_declarator_stack_bytes_;
};

}

struct TranslationUnit::Impl
{
	explicit Impl(Stats* stats) : model(stats) {}

	Program model;
};

Stats::Stats()
	: tokens(0), token_storage_bytes(0), declarator_frames(0),
	  declarator_cache_hits(0), declarator_cache_misses(0),
	  declarator_memo_entries(0), peak_parser_scratch_bytes(0),
	  parser_memo_storage_bytes(0), identifiers(0), identifier_bytes(0),
	  canonical_types(0), namespaces(0), declarations(0), using_edges(0),
	  lookup_queries(0), lookup_cache_hits(0), lookup_cache_misses(0),
	  lookup_cache_invalidations(0), lookup_cache_entries(0),
	  lookup_scope_visits(0),
	  lookup_edge_visits(0),
	  semantic_storage_bytes(0), peak_stage_storage_bytes(0),
	  elapsed_nanoseconds(0)
{
}

TranslationUnit::TranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	Stats* stats)
	: impl_(new Impl(stats))
{
	std::chrono::steady_clock::time_point started;
	if (stats)
	{
		*stats = Stats();
		started = std::chrono::steady_clock::now();
	}
	SemanticTokenSink sink(impl_->model.identifiers);
	PreprocessFile(path, source, sink, options,
		stats ? &stats->preprocessing : 0);
	if (stats)
	{
		stats->tokens = sink.Tokens().size();
		stats->token_storage_bytes = sink.StorageBytes();
	}
	Parser parser(sink.Tokens(), impl_->model, stats);
	parser.Parse();
	if (stats)
	{
		stats->identifiers = impl_->model.identifiers.Size();
		stats->identifier_bytes = impl_->model.identifiers.SpellingBytes();
		stats->canonical_types = impl_->model.types.Size();
		stats->namespaces = impl_->model.NamespaceCount();
		stats->lookup_cache_entries =
			impl_->model.LookupCacheEntryCount();
		stats->semantic_storage_bytes = impl_->model.StorageBytes();
		stats->peak_stage_storage_bytes = source.size() +
			stats->token_storage_bytes + stats->semantic_storage_bytes +
			stats->peak_parser_scratch_bytes +
			stats->parser_memo_storage_bytes;
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count());
	}
}

TranslationUnit::~TranslationUnit() {}

TranslationUnit::TranslationUnit(
	TranslationUnit&& other) noexcept
	: impl_(std::move(other.impl_))
{
}

TranslationUnit& TranslationUnit::operator=(
	TranslationUnit&& other) noexcept
{
	impl_ = std::move(other.impl_);
	return *this;
}

void TranslationUnit::Render(std::ostream& output) const
{
	impl_->model.Render(output);
}

}
}
