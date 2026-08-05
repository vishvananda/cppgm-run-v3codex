#include "pa7_semantic.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cppgm
{
namespace
{

typedef std::uint32_t NameId;
typedef std::uint32_t TypeId;
typedef std::uint32_t NamespaceId;
typedef std::uint32_t EntityId;

const NamespaceId kNoNamespace = std::numeric_limits<NamespaceId>::max();
const EntityId kNoEntity = std::numeric_limits<EntityId>::max();
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
			throw std::runtime_error("too many identifiers");
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
		throw std::runtime_error("invalid phase-7 token: " + source);
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
		throw std::runtime_error("user-defined literal in PA7 input");
	}

	void EmitUserDefinedString(const std::string&, const std::string&,
		std::size_t, FundamentalType, const void*, std::size_t)
	{
		throw std::runtime_error("user-defined literal in PA7 input");
	}

	void EmitUserDefinedInteger(const std::string&, const std::string&,
		const std::string&)
	{
		throw std::runtime_error("user-defined literal in PA7 input");
	}

	void EmitUserDefinedFloating(const std::string&, const std::string&,
		const std::string&)
	{
		throw std::runtime_error("user-defined literal in PA7 input");
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
			throw std::runtime_error("incompatible redeclaration");
		const TypeId child = MergeRedeclaration(left.child, right.child);
		if (left.bound != 0 && right.bound != 0 && left.bound != right.bound)
			throw std::runtime_error("incompatible array bounds");
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
		const TypeRecord& record = Get(type);
		switch (record.kind)
		{
		case TYPE_FUNDAMENTAL:
			output << FundamentalTypeName(record.fundamental);
			break;
		case TYPE_QUALIFIED:
			if ((record.cv & CV_CONST) != 0) output << "const ";
			if ((record.cv & CV_VOLATILE) != 0) output << "volatile ";
			Render(output, record.child);
			break;
		case TYPE_POINTER:
			output << "pointer to ";
			Render(output, record.child);
			break;
		case TYPE_LVALUE_REFERENCE:
			output << "lvalue-reference to ";
			Render(output, record.child);
			break;
		case TYPE_RVALUE_REFERENCE:
			output << "rvalue-reference to ";
			Render(output, record.child);
			break;
		case TYPE_ARRAY:
			if (record.bound == 0) output << "array of unknown bound of ";
			else output << "array of " << record.bound << ' ';
			Render(output, record.child);
			break;
		case TYPE_FUNCTION:
			RenderFunction(output, record);
			break;
		default:
			throw std::logic_error("invalid canonical type");
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
			throw std::runtime_error("too many canonical types");
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

	void RenderFunction(std::ostream& output, const TypeRecord& record) const
	{
		output << "function of (";
		for (std::uint32_t i = 0; i < record.parameter_count; ++i)
		{
			if (i != 0) output << ", ";
			Render(output, parameters_[record.parameter_offset + i]);
		}
		if (record.variadic)
		{
			if (record.parameter_count != 0) output << ", ";
			output << "...";
		}
		output << ") returning ";
		Render(output, record.child);
	}

	std::vector<TypeRecord> types_;
	std::vector<TypeId> parameters_;
	std::vector<TypeId> slots_;
};

struct Binding
{
	NameId name;
	TypeId type;
	NamespaceId name_space;
	EntityId variable;
	EntityId function;

	explicit Binding(NameId binding_name = 0)
		: name(binding_name), type(0), name_space(kNoNamespace),
		  variable(kNoEntity), function(kNoEntity) {}
};

class BindingIndex
{
public:
	BindingIndex() : slots_(8, 0) {}

	std::uint32_t Find(NameId name, const std::vector<Binding>& bindings) const
	{
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = MixHash(0, name) & mask;
		while (slots_[slot] != 0)
		{
			const std::uint32_t index = slots_[slot] - 1;
			if (bindings[index].name == name) return index;
			slot = (slot + 1) & mask;
		}
		return std::numeric_limits<std::uint32_t>::max();
	}

	std::uint32_t Ensure(NameId name, std::vector<Binding>* bindings)
	{
		if ((bindings->size() + 1) * 10 > slots_.size() * 7)
			Rehash(slots_.size() * 2, *bindings);
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = MixHash(0, name) & mask;
		while (slots_[slot] != 0)
		{
			const std::uint32_t index = slots_[slot] - 1;
			if ((*bindings)[index].name == name) return index;
			slot = (slot + 1) & mask;
		}
		bindings->push_back(Binding(name));
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
	void Rehash(std::size_t capacity, const std::vector<Binding>& bindings)
	{
		std::vector<std::uint32_t> replacement(capacity, 0);
		const std::size_t mask = capacity - 1;
		for (std::uint32_t i = 0; i < bindings.size(); ++i)
		{
			std::size_t slot = MixHash(0, bindings[i].name) & mask;
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
	bool is_inline;
	std::vector<Binding> bindings;
	BindingIndex binding_index;
	std::vector<NamespaceId> using_edges;
	std::vector<EntityId> variables;
	std::vector<EntityId> functions;
	std::vector<NamespaceId> children;

	NamespaceRecord(NameId namespace_name = 0,
		NamespaceId namespace_parent = kNoNamespace, bool inline_value = false)
		: name(namespace_name), parent(namespace_parent),
		  unnamed_child(kNoNamespace), is_inline(inline_value) {}
};

struct EntityRecord
{
	NameId name;
	TypeId type;
	NamespaceId owner;

	EntityRecord(NameId entity_name, TypeId entity_type,
		NamespaceId entity_owner)
		: name(entity_name), type(entity_type), owner(entity_owner) {}
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

struct QualifiedName
{
	bool absolute;
	std::vector<NameId> segments;

	QualifiedName() : absolute(false) {}
};

class SemanticModel
{
public:
	explicit SemanticModel(SemanticAnalysisStats* stats)
		: stats_(stats), lookup_generation_(0)
	{
		namespaces_.push_back(NamespaceRecord());
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
					throw std::runtime_error("namespace inline mismatch");
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
				throw std::runtime_error("namespace alias cannot be reopened");
			if (is_inline && !namespaces_[existing].is_inline)
				throw std::runtime_error("namespace inline mismatch");
			return existing;
		}
		if (binding && (binding->type != 0 || binding->variable != kNoEntity ||
			binding->function != kNoEntity))
			throw std::runtime_error("namespace name conflicts with declaration");

		const NamespaceId created = NewNamespace(parent, name, is_inline);
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
				throw std::runtime_error("namespace alias name conflict");
			binding.name_space = target;
			return;
		}
		if (binding.name_space != target)
			throw std::runtime_error("namespace alias target mismatch");
	}

	void AddUsingEdge(NamespaceId owner, NamespaceId target)
	{
		std::vector<NamespaceId>& edges = namespaces_[owner].using_edges;
		if (std::find(edges.begin(), edges.end(), target) == edges.end())
		{
			edges.push_back(target);
			if (stats_) ++stats_->using_edges;
		}
	}

	void AddUsingDeclaration(NamespaceId owner, NameId name,
		const LookupResult& target)
	{
		Binding& binding = EnsureBinding(owner, name);
		MergeImportedType(&binding.type, target.type);
		MergeImportedEntity(&binding.variable, target.variable);
		MergeImportedEntity(&binding.function, target.function);
	}

	void AddTypeAlias(NamespaceId owner, NameId name, TypeId type)
	{
		Binding& binding = EnsureBinding(owner, name);
		if (binding.type != 0 && binding.type != type)
			throw std::runtime_error("type alias redeclaration mismatch");
		if (binding.variable != kNoEntity || binding.function != kNoEntity)
			throw std::runtime_error("type alias name conflict");
		binding.type = type;
	}

	void Declare(NamespaceId current, const QualifiedName& name, TypeId type)
	{
		if (name.segments.empty())
			throw std::runtime_error("declarator has no name");
		const bool function = types.IsFunction(type);
		const bool qualified = name.absolute || name.segments.size() > 1;
		NamespaceId owner = current;
		if (qualified && !ResolvePrefix(current, name, &owner))
			throw std::runtime_error("declarator qualifier lookup failed");
		const NameId unqualified = name.segments.back();

		if (qualified)
		{
			const LookupResult found = SearchGraph(owner, unqualified,
				function ? LOOKUP_FUNCTION : LOOKUP_VARIABLE);
			const EntityId entity = function ? found.function : found.variable;
			if (entity == kNoEntity)
				throw std::runtime_error("qualified declaration not found");
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
			throw std::runtime_error("declaration conflicts with type name");
		const EntityId entity = static_cast<EntityId>(entities_.size());
		entities_.push_back(EntityRecord(unqualified, type, owner));
		existing = entity;
		if (function) namespaces_[owner].functions.push_back(entity);
		else namespaces_[owner].variables.push_back(entity);
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

	std::size_t StorageBytes() const
	{
		std::size_t result = identifiers.StorageBytes() + types.StorageBytes() +
			namespaces_.capacity() * sizeof(NamespaceRecord) +
			entities_.capacity() * sizeof(EntityRecord) +
			lookup_marks_.capacity() * sizeof(std::uint32_t) +
			lookup_worklist_.capacity() * sizeof(NamespaceId);
		for (std::size_t i = 0; i < namespaces_.size(); ++i)
		{
			const NamespaceRecord& item = namespaces_[i];
			result += item.bindings.capacity() * sizeof(Binding) +
				item.binding_index.StorageBytes() +
				item.using_edges.capacity() * sizeof(NamespaceId) +
				item.variables.capacity() * sizeof(EntityId) +
				item.functions.capacity() * sizeof(EntityId) +
				item.children.capacity() * sizeof(NamespaceId);
		}
		return result;
	}

	void Render(std::ostream& output) const { RenderNamespace(output, 0); }

private:
	NamespaceId NewNamespace(NamespaceId parent, NameId name, bool is_inline)
	{
		if (namespaces_.size() > std::numeric_limits<NamespaceId>::max())
			throw std::runtime_error("too many namespaces");
		const NamespaceId id = static_cast<NamespaceId>(namespaces_.size());
		namespaces_.push_back(NamespaceRecord(name, parent, is_inline));
		namespaces_[parent].children.push_back(id);
		return id;
	}

	Binding* FindDirectBinding(NamespaceId owner, NameId name)
	{
		NamespaceRecord& scope = namespaces_[owner];
		const std::uint32_t index = scope.binding_index.Find(name,
			scope.bindings);
		return index == std::numeric_limits<std::uint32_t>::max() ? 0 :
			&scope.bindings[index];
	}

	const Binding* FindDirectBinding(NamespaceId owner, NameId name) const
	{
		const NamespaceRecord& scope = namespaces_[owner];
		const std::uint32_t index = scope.binding_index.Find(name,
			scope.bindings);
		return index == std::numeric_limits<std::uint32_t>::max() ? 0 :
			&scope.bindings[index];
	}

	Binding& EnsureBinding(NamespaceId owner, NameId name)
	{
		NamespaceRecord& scope = namespaces_[owner];
		const std::uint32_t index = scope.binding_index.Ensure(name,
			&scope.bindings);
		return scope.bindings[index];
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
		for (std::size_t head = 0; head < lookup_worklist_.size(); ++head)
		{
			const NamespaceId current = lookup_worklist_[head];
			if (stats_) ++stats_->lookup_scope_visits;
			const LookupResult direct = DirectLookup(current, name, kind);
			if (direct.Found(kind)) return direct;
			const std::vector<NamespaceId>& edges =
				namespaces_[current].using_edges;
			for (std::size_t i = 0; i < edges.size(); ++i)
			{
				if (stats_) ++stats_->lookup_edge_visits;
				const NamespaceId target = edges[i];
				if (lookup_marks_[target] == lookup_generation_) continue;
				lookup_marks_[target] = lookup_generation_;
				lookup_worklist_.push_back(target);
			}
		}
		return LookupResult();
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
			throw std::runtime_error("using declaration type conflict");
		*destination = source;
	}

	void MergeImportedEntity(EntityId* destination, EntityId source)
	{
		if (source == kNoEntity) return;
		if (*destination != kNoEntity && *destination != source)
			throw std::runtime_error("using declaration entity conflict");
		*destination = source;
	}

	void RenderNamespace(std::ostream& output, NamespaceId id) const
	{
		const NamespaceRecord& scope = namespaces_[id];
		if (scope.name == 0) output << "start unnamed namespace\n";
		else output << "start namespace " << Name(scope.name) << '\n';
		if (scope.is_inline) output << "inline namespace\n";
		for (std::size_t i = 0; i < scope.variables.size(); ++i)
		{
			const EntityRecord& entity = entities_[scope.variables[i]];
			output << "variable " << Name(entity.name) << ' ';
			types.Render(output, entity.type);
			output << '\n';
		}
		for (std::size_t i = 0; i < scope.functions.size(); ++i)
		{
			const EntityRecord& entity = entities_[scope.functions[i]];
			output << "function " << Name(entity.name) << ' ';
			types.Render(output, entity.type);
			output << '\n';
		}
		for (std::size_t i = 0; i < scope.children.size(); ++i)
			RenderNamespace(output, scope.children[i]);
		output << "end namespace\n";
	}

	SemanticAnalysisStats* stats_;
	std::vector<NamespaceRecord> namespaces_;
	std::vector<EntityRecord> entities_;
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

class SemanticParser
{
public:
	SemanticParser(const std::vector<SemanticToken>& tokens,
		SemanticModel& model)
		: tokens_(tokens), model_(model), position_(0), current_namespace_(0) {}

	void Parse()
	{
		ParseNamespaceBody(false);
		if (!AtEof()) throw std::runtime_error("unexpected token after unit");
	}

private:
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
			throw std::runtime_error(std::string("expected ") +
				SimpleTokenKindName(kind));
	}

	NameId ConsumeIdentifier()
	{
		if (!AtIdentifier()) throw std::runtime_error("expected identifier");
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
				throw std::runtime_error("void parameter in parameter list");
			parsed.parameters.clear();
		}
		*operation = std::move(parsed);
		return true;
	}

	bool ParseDeclarator(DeclaratorMode mode, Declarator* result)
	{
		const std::size_t start = position_;
		std::vector<DeclaratorOperation> prefixes;
		while (true)
		{
			DeclaratorOperation operation(DECL_POINTER);
			if (!ParsePointerOperator(&operation)) break;
			prefixes.push_back(std::move(operation));
		}

		Declarator parsed;
		bool direct = false;
		if (mode == DECLARATOR_NAMED &&
			(AtIdentifier() || At(OP_COLON2)))
		{
			if (!ParseQualifiedName(&parsed.name))
			{
				position_ = start;
				return false;
			}
			parsed.has_name = true;
			direct = true;
		}
		else if (At(OP_LPAREN))
		{
			const std::size_t group_start = position_;
			++position_;
			Declarator nested;
			if (ParseDeclarator(mode, &nested) && Match(OP_RPAREN))
			{
				parsed = std::move(nested);
				direct = true;
			}
			else
			{
				position_ = group_start;
			}
		}

		bool suffix = false;
		while (true)
		{
			DeclaratorOperation operation(DECL_ARRAY);
			if (ParseArrayOperation(&operation) ||
				ParseFunctionOperation(&operation))
			{
				parsed.operations.push_back(std::move(operation));
				suffix = true;
				continue;
			}
			break;
		}
		for (std::vector<DeclaratorOperation>::reverse_iterator i =
			prefixes.rbegin(); i != prefixes.rend(); ++i)
			parsed.operations.push_back(std::move(*i));

		if ((!direct && !suffix && prefixes.empty()) ||
			(mode == DECLARATOR_NAMED && !parsed.has_name) ||
			(mode == DECLARATOR_ABSTRACT && parsed.has_name))
		{
			position_ = start;
			return false;
		}
		*result = std::move(parsed);
		return true;
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

	void ParseNamespaceBody(bool stop_at_right_brace)
	{
		while (true)
		{
			if (stop_at_right_brace && At(OP_RBRACE)) return;
			if (AtEof())
			{
				if (stop_at_right_brace)
					throw std::runtime_error("unterminated namespace");
				return;
			}
			ParseDeclaration();
		}
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
				throw std::runtime_error("expected namespace alias target");
			NamespaceId target;
			if (!model_.ResolveNamespaceName(current_namespace_, target_name,
				&target))
				throw std::runtime_error("namespace alias lookup failed");
			Expect(OP_SEMICOLON);
			model_.AddNamespaceAlias(current_namespace_, alias, target);
			return;
		}

		NameId name = 0;
		if (AtIdentifier()) name = ConsumeIdentifier();
		Expect(OP_LBRACE);
		const NamespaceId enclosing = current_namespace_;
		current_namespace_ = model_.OpenNamespace(enclosing, name, is_inline);
		ParseNamespaceBody(true);
		Expect(OP_RBRACE);
		current_namespace_ = enclosing;
	}

	void ParseUsingDeclaration()
	{
		Expect(KW_USING);
		if (Match(KW_NAMESPACE))
		{
			QualifiedName target_name;
			if (!ParseQualifiedName(&target_name))
				throw std::runtime_error("expected using-directive target");
			NamespaceId target;
			if (!model_.ResolveNamespaceName(current_namespace_, target_name,
				&target))
				throw std::runtime_error("using-directive lookup failed");
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
				throw std::runtime_error("expected alias type-id");
			Declarator declarator;
			if (!At(OP_SEMICOLON) &&
				!ParseDeclarator(DECLARATOR_ABSTRACT, &declarator))
				throw std::runtime_error("invalid alias declarator");
			Expect(OP_SEMICOLON);
			model_.AddTypeAlias(current_namespace_, alias,
				ApplyDeclarator(specifiers.type, declarator));
			return;
		}

		QualifiedName target_name;
		if (!ParseQualifiedName(&target_name) ||
			(!target_name.absolute && target_name.segments.size() < 2))
			throw std::runtime_error("expected qualified using-declaration");
		LookupResult target;
		if (!model_.ResolveUsingTarget(current_namespace_, target_name, &target))
			throw std::runtime_error("using-declaration lookup failed");
		Expect(OP_SEMICOLON);
		model_.AddUsingDeclaration(current_namespace_,
			target_name.segments.back(), target);
	}

	void ParseSimpleDeclaration()
	{
		DeclSpecifiers specifiers;
		if (!ParseDeclSpecifiers(&specifiers))
			throw std::runtime_error("expected declaration specifiers");
		while (true)
		{
			Declarator declarator;
			if (!ParseDeclarator(DECLARATOR_NAMED, &declarator))
				throw std::runtime_error("expected declarator");
			const TypeId type = ApplyDeclarator(specifiers.type, declarator);
			if (specifiers.is_typedef)
			{
				if (declarator.name.absolute ||
					declarator.name.segments.size() != 1)
					throw std::runtime_error("qualified typedef declarator");
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
	SemanticModel& model_;
	std::size_t position_;
	NamespaceId current_namespace_;
};

}

struct SemanticTranslationUnit::Impl
{
	explicit Impl(SemanticAnalysisStats* stats) : model(stats) {}

	SemanticModel model;
};

SemanticAnalysisStats::SemanticAnalysisStats()
	: tokens(0), token_storage_bytes(0), identifiers(0), identifier_bytes(0),
	  canonical_types(0), namespaces(0), declarations(0), using_edges(0),
	  lookup_queries(0), lookup_scope_visits(0), lookup_edge_visits(0),
	  semantic_storage_bytes(0), peak_stage_storage_bytes(0),
	  elapsed_nanoseconds(0)
{
}

SemanticTranslationUnit::SemanticTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	SemanticAnalysisStats* stats)
	: impl_(new Impl(stats))
{
	std::chrono::steady_clock::time_point started;
	if (stats)
	{
		*stats = SemanticAnalysisStats();
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
	SemanticParser parser(sink.Tokens(), impl_->model);
	parser.Parse();
	if (stats)
	{
		stats->identifiers = impl_->model.identifiers.Size();
		stats->identifier_bytes = impl_->model.identifiers.SpellingBytes();
		stats->canonical_types = impl_->model.types.Size();
		stats->namespaces = impl_->model.NamespaceCount();
		stats->semantic_storage_bytes = impl_->model.StorageBytes();
		stats->peak_stage_storage_bytes = stats->token_storage_bytes +
			stats->semantic_storage_bytes;
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count());
	}
}

SemanticTranslationUnit::~SemanticTranslationUnit() {}

SemanticTranslationUnit::SemanticTranslationUnit(
	SemanticTranslationUnit&& other) noexcept
	: impl_(std::move(other.impl_))
{
}

SemanticTranslationUnit& SemanticTranslationUnit::operator=(
	SemanticTranslationUnit&& other) noexcept
{
	impl_ = std::move(other.impl_);
	return *this;
}

void SemanticTranslationUnit::Render(std::ostream& output) const
{
	impl_->model.Render(output);
}

}
