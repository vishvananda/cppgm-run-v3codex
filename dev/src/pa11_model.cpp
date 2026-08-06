#include "pa11_model.h"

#include <algorithm>
#include <ostream>
#include <stdexcept>

namespace cppgm
{
namespace pa11
{
namespace
{

std::size_t HashText(const std::string& text)
{
	std::size_t value = sizeof(std::size_t) == 8 ?
		static_cast<std::size_t>(1469598103934665603ULL) :
		static_cast<std::size_t>(2166136261U);
	const std::size_t prime = sizeof(std::size_t) == 8 ?
		static_cast<std::size_t>(1099511628211ULL) :
		static_cast<std::size_t>(16777619U);
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		value ^= static_cast<unsigned char>(text[i]);
		value *= prime;
	}
	return value;
}

const char* FundamentalName(FundamentalKind kind)
{
	switch (kind)
	{
	case FUND_BOOL: return "bool";
	case FUND_CHAR: return "char";
	case FUND_SIGNED_CHAR: return "signed char";
	case FUND_UNSIGNED_CHAR: return "unsigned char";
	case FUND_SHORT_INT: return "short int";
	case FUND_UNSIGNED_SHORT_INT: return "unsigned short int";
	case FUND_INT: return "int";
	case FUND_UNSIGNED_INT: return "unsigned int";
	case FUND_LONG_INT: return "long int";
	case FUND_UNSIGNED_LONG_INT: return "unsigned long int";
	case FUND_LONG_LONG_INT: return "long long int";
	case FUND_UNSIGNED_LONG_LONG_INT: return "unsigned long long int";
	case FUND_FLOAT: return "float";
	case FUND_DOUBLE: return "double";
	case FUND_LONG_DOUBLE: return "long double";
	case FUND_VOID: return "void";
	case FUND_WCHAR_T: return "wchar_t";
	case FUND_CHAR16_T: return "char16_t";
	case FUND_CHAR32_T: return "char32_t";
	}
	throw std::logic_error("invalid fundamental type");
}

const char* FlavorName(NamedFlavor flavor)
{
	switch (flavor)
	{
	case NAMED_STRUCT: return "struct";
	case NAMED_CLASS: return "class";
	case NAMED_UNION: return "union";
	case NAMED_ENUM: return "enum";
	case NAMED_ENUM_CLASS: return "enum class";
	case NAMED_TYPENAME_PARAMETER: return "typename";
	case NAMED_TEMPLATE_PARAMETER: return "template-parameter";
	case NAMED_NONE: break;
	}
	throw std::logic_error("invalid named type flavor");
}

}

std::size_t MixHash(std::size_t seed, std::uint64_t value)
{
	seed ^= static_cast<std::size_t>(value) +
		static_cast<std::size_t>(0x9e3779b9U) + (seed << 6) + (seed >> 2);
	return seed;
}

NameTable::NameTable() : slots_(32, 0)
{
	spellings_.push_back(std::string());
}

NameId NameTable::Intern(const std::string& spelling)
{
	if ((spellings_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = HashText(spelling) & mask;
	while (slots_[slot] != 0)
	{
		const NameId id = slots_[slot];
		if (spellings_[id] == spelling) return id;
		slot = (slot + 1) & mask;
	}
	if (spellings_.size() > std::numeric_limits<NameId>::max())
		throw std::runtime_error("too many PA11 names");
	const NameId id = static_cast<NameId>(spellings_.size());
	spellings_.push_back(spelling);
	slots_[slot] = id;
	return id;
}

const std::string& NameTable::Get(NameId name) const
{
	return spellings_[name];
}

std::size_t NameTable::Size() const
{
	return spellings_.size() - 1;
}

std::size_t NameTable::StorageBytes() const
{
	std::size_t bytes = spellings_.capacity() * sizeof(std::string) +
		slots_.capacity() * sizeof(NameId);
	for (std::size_t i = 1; i < spellings_.size(); ++i)
		bytes += spellings_[i].capacity();
	return bytes;
}

void NameTable::Rehash(std::size_t capacity)
{
	std::vector<NameId> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (NameId id = 1; id < spellings_.size(); ++id)
	{
		std::size_t slot = HashText(spellings_[id]) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = id;
	}
	slots_.swap(replacement);
}

TypeRecord::TypeRecord()
	: kind(TYPE_INVALID), child(kNoType), entity(kNoEntity), bound(0),
	  parameter_offset(0), parameter_count(0), cv(CV_NONE), variadic(false),
	  fundamental(FUND_INT)
{
}

TypeTable::TypeTable() : slots_(64, 0)
{
	types_.push_back(TypeRecord());
}

TypeId TypeTable::Fundamental(FundamentalKind kind)
{
	TypeRecord candidate;
	candidate.kind = TYPE_FUNDAMENTAL;
	candidate.fundamental = kind;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::Named(EntityId entity)
{
	TypeRecord candidate;
	candidate.kind = TYPE_NAMED;
	candidate.entity = entity;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::Unary(TypeKind kind, TypeId child)
{
	TypeRecord candidate;
	candidate.kind = kind;
	candidate.child = child;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::Qualify(TypeId type, std::uint8_t cv)
{
	if (cv == CV_NONE) return type;
	const TypeRecord& record = Get(type);
	if (record.kind == TYPE_ARRAY)
		return Array(Qualify(record.child, cv), record.bound);
	if (record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE) return type;
	if (record.kind == TYPE_FUNCTION)
		throw std::runtime_error("cv-qualified function type");
	if (record.kind == TYPE_QUALIFIED)
		return Qualify(record.child,
			static_cast<std::uint8_t>(record.cv | cv));
	TypeRecord candidate;
	candidate.kind = TYPE_QUALIFIED;
	candidate.child = type;
	candidate.cv = cv;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::Pointer(TypeId type)
{
	if (IsReference(type))
		throw std::runtime_error("pointer to reference type");
	return Unary(TYPE_POINTER, type);
}

TypeId TypeTable::Reference(TypeKind kind, TypeId type)
{
	if (kind != TYPE_LVALUE_REFERENCE && kind != TYPE_RVALUE_REFERENCE)
		throw std::logic_error("invalid reference kind");
	const TypeRecord& record = Get(type);
	if (record.kind == TYPE_LVALUE_REFERENCE)
		return Unary(TYPE_LVALUE_REFERENCE, record.child);
	if (record.kind == TYPE_RVALUE_REFERENCE)
		return Unary(kind == TYPE_LVALUE_REFERENCE ? TYPE_LVALUE_REFERENCE :
			TYPE_RVALUE_REFERENCE, record.child);
	if (record.kind == TYPE_FUNDAMENTAL && record.fundamental == FUND_VOID)
		throw std::runtime_error("reference to void type");
	return Unary(kind, type);
}

TypeId TypeTable::Array(TypeId type, std::uint64_t bound)
{
	const TypeRecord& record = Get(type);
	if (record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE || record.kind == TYPE_FUNCTION ||
		(record.kind == TYPE_FUNDAMENTAL &&
		 record.fundamental == FUND_VOID))
		throw std::runtime_error("invalid array element type");
	TypeRecord candidate;
	candidate.kind = TYPE_ARRAY;
	candidate.child = type;
	candidate.bound = bound;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::Function(TypeId result,
	const std::vector<TypeId>& parameters, bool variadic)
{
	const TypeRecord& returned = Get(result);
	if (returned.kind == TYPE_ARRAY || returned.kind == TYPE_FUNCTION)
		throw std::runtime_error("invalid function return type");
	if (parameters.size() > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many function parameters");
	TypeRecord candidate;
	candidate.kind = TYPE_FUNCTION;
	candidate.child = result;
	candidate.parameter_count =
		static_cast<std::uint32_t>(parameters.size());
	candidate.variadic = variadic;
	return Intern(candidate, parameters.empty() ? 0 : &parameters[0],
		parameters.size());
}

TypeId TypeTable::RemoveTopCv(TypeId type) const
{
	const TypeRecord& record = Get(type);
	return record.kind == TYPE_QUALIFIED ? record.child : type;
}

bool TypeTable::IsFunction(TypeId type) const
{
	return Get(type).kind == TYPE_FUNCTION;
}

bool TypeTable::IsReference(TypeId type) const
{
	const TypeKind kind = Get(type).kind;
	return kind == TYPE_LVALUE_REFERENCE || kind == TYPE_RVALUE_REFERENCE;
}

bool TypeTable::IsNamed(TypeId type) const
{
	return Get(RemoveTopCv(type)).kind == TYPE_NAMED;
}

const TypeRecord& TypeTable::Get(TypeId type) const
{
	if (type == kNoType || type >= types_.size())
		throw std::logic_error("invalid PA11 type identity");
	return types_[type];
}

const TypeId* TypeTable::Parameters(TypeId function) const
{
	const TypeRecord& record = Get(function);
	if (record.kind != TYPE_FUNCTION)
		throw std::logic_error("parameters requested for non-function type");
	return record.parameter_count == 0 ? 0 :
		&parameters_[record.parameter_offset];
}

std::size_t TypeTable::Size() const
{
	return types_.size() - 1;
}

std::size_t TypeTable::StorageBytes() const
{
	return types_.capacity() * sizeof(TypeRecord) +
		parameters_.capacity() * sizeof(TypeId) +
		slots_.capacity() * sizeof(TypeId);
}

std::size_t TypeTable::Hash(const TypeRecord& record,
	const TypeId* parameters, std::size_t count) const
{
	std::size_t hash = MixHash(0, record.kind);
	hash = MixHash(hash, record.child);
	hash = MixHash(hash, record.entity);
	hash = MixHash(hash, record.bound);
	hash = MixHash(hash, record.cv);
	hash = MixHash(hash, record.variadic ? 1 : 0);
	hash = MixHash(hash, record.fundamental);
	for (std::size_t i = 0; i < count; ++i)
		hash = MixHash(hash, parameters[i]);
	return hash;
}

bool TypeTable::Equal(const TypeRecord& existing,
	const TypeRecord& candidate, const TypeId* parameters,
	std::size_t count) const
{
	if (existing.kind != candidate.kind || existing.child != candidate.child ||
		existing.entity != candidate.entity || existing.bound != candidate.bound ||
		existing.cv != candidate.cv || existing.variadic != candidate.variadic ||
		existing.fundamental != candidate.fundamental ||
		existing.parameter_count != count) return false;
	for (std::size_t i = 0; i < count; ++i)
		if (parameters_[existing.parameter_offset + i] != parameters[i])
			return false;
	return true;
}

TypeId TypeTable::Intern(TypeRecord candidate, const TypeId* parameters,
	std::size_t count)
{
	if (parameters_.size() > std::numeric_limits<std::uint32_t>::max() - count)
		throw std::runtime_error("canonical type parameter storage is too large");
	if ((types_.size() + 1) * 10 > slots_.size() * 7)
		Rehash(slots_.size() * 2);
	const std::size_t mask = slots_.size() - 1;
	std::size_t slot = Hash(candidate, parameters, count) & mask;
	while (slots_[slot] != 0)
	{
		const TypeId type = slots_[slot];
		if (Equal(types_[type], candidate, parameters, count)) return type;
		slot = (slot + 1) & mask;
	}
	if (types_.size() > std::numeric_limits<TypeId>::max())
		throw std::runtime_error("too many canonical types");
	candidate.parameter_offset =
		static_cast<std::uint32_t>(parameters_.size());
	if (count != 0)
		parameters_.insert(parameters_.end(), parameters, parameters + count);
	const TypeId type = static_cast<TypeId>(types_.size());
	types_.push_back(candidate);
	slots_[slot] = type;
	return type;
}

void TypeTable::Rehash(std::size_t capacity)
{
	std::vector<TypeId> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (TypeId type = 1; type < types_.size(); ++type)
	{
		const TypeRecord& record = types_[type];
		const TypeId* parameters = record.parameter_count == 0 ? 0 :
			&parameters_[record.parameter_offset];
		std::size_t slot = Hash(record, parameters,
			record.parameter_count) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = type;
	}
	slots_.swap(replacement);
}

EntityRecord::EntityRecord()
	: name(0), flavor(NAMED_NONE), member_scope(kNoScope), type(kNoType),
	  underlying(kNoType), complete(false)
{
}

BindingRecord::BindingRecord()
	: owner(kNoScope), name(0), kind(BIND_VARIABLE), type(kNoType),
	  next(kNoBinding), display_flavor(NAMED_NONE), value(0), constant(false)
{
}

LookupResult::LookupResult()
	: name_space(kNoScope), type(kNoType), ordinary(kNoBinding)
{
}

bool LookupResult::Empty() const
{
	return name_space == kNoScope && type == kNoType && ordinary == kNoBinding;
}

struct Program::ScopeRecord
{
	ScopeId parent;
	ScopeKind kind;
	NameId name;
	EntityId entity;
	BindingId first_binding;
	BindingId last_binding;
	std::uint32_t first_child;
	std::uint32_t last_child;
	std::uint32_t first_using;

	ScopeRecord()
		: parent(kNoScope), kind(SCOPE_NAMESPACE), name(0), entity(kNoEntity),
		  first_binding(kNoBinding), last_binding(kNoBinding),
		  first_child(std::numeric_limits<std::uint32_t>::max()),
		  last_child(std::numeric_limits<std::uint32_t>::max()),
		  first_using(std::numeric_limits<std::uint32_t>::max()) {}
};

struct Program::NameEntry
{
	ScopeId scope;
	NameId name;
	ScopeId name_space;
	TypeId type;
	BindingId ordinary;

	NameEntry()
		: scope(kNoScope), name(0), name_space(kNoScope), type(kNoType),
		  ordinary(kNoBinding) {}
};

struct Program::UsingEdge
{
	ScopeId target;
	std::uint32_t next;
	UsingEdge(ScopeId target_value, std::uint32_t next_value)
		: target(target_value), next(next_value) {}
};

struct Program::ChildEdge
{
	ScopeId child;
	std::uint32_t next;
	explicit ChildEdge(ScopeId child_value)
		: child(child_value),
		  next(std::numeric_limits<std::uint32_t>::max()) {}
};

Program::Program()
	: lookup_queries(0), lookup_scope_visits(0), lookup_edge_visits(0),
	  entry_slots_(64, 0), lookup_generation_(1)
{
	NewScope(kNoScope, SCOPE_NAMESPACE, names.Intern("<global>"));
}

Program::~Program()
{
}

ScopeId Program::GlobalScope() const
{
	return 0;
}

ScopeId Program::NewScope(ScopeId parent, ScopeKind kind, NameId name,
	EntityId entity)
{
	if (scopes_.size() >= kNoScope)
		throw std::runtime_error("too many PA11 scopes");
	const ScopeId scope = static_cast<ScopeId>(scopes_.size());
	scopes_.push_back(ScopeRecord());
	ScopeRecord& record = scopes_.back();
	record.parent = parent;
	record.kind = kind;
	record.name = name;
	record.entity = entity;
	lookup_marks_.push_back(0);
	if (parent != kNoScope)
	{
		const std::uint32_t edge =
			static_cast<std::uint32_t>(child_edges_.size());
		child_edges_.push_back(ChildEdge(scope));
		ScopeRecord& owner = scopes_[parent];
		if (owner.first_child == std::numeric_limits<std::uint32_t>::max())
			owner.first_child = edge;
		else child_edges_[owner.last_child].next = edge;
		owner.last_child = edge;
	}
	return scope;
}

ScopeId Program::OpenNamespace(ScopeId parent, NameId name, bool is_inline)
{
	NameEntry* entry = EnsureEntry(parent, name);
	if (entry->ordinary != kNoBinding || entry->type != kNoType)
		throw std::runtime_error("namespace conflicts with existing binding");
	if (entry->name_space == kNoScope)
		entry->name_space = NewScope(parent, SCOPE_NAMESPACE, name);
	if (is_inline) AddUsingEdge(parent, entry->name_space);
	return entry->name_space;
}

void Program::AddNamespaceAlias(ScopeId owner, NameId name, ScopeId target)
{
	NameEntry* entry = EnsureEntry(owner, name);
	if (entry->ordinary != kNoBinding || entry->type != kNoType ||
		(entry->name_space != kNoScope && entry->name_space != target))
		throw std::runtime_error("invalid namespace alias binding");
	entry->name_space = target;
}

void Program::AddUsingEdge(ScopeId owner, ScopeId target)
{
	for (std::uint32_t edge = scopes_[owner].first_using;
		edge != std::numeric_limits<std::uint32_t>::max();
		edge = using_edges_[edge].next)
		if (using_edges_[edge].target == target) return;
	const std::uint32_t edge =
		static_cast<std::uint32_t>(using_edges_.size());
	using_edges_.push_back(UsingEdge(target, scopes_[owner].first_using));
	scopes_[owner].first_using = edge;
}

EntityId Program::NewEntity(NameId name, NamedFlavor flavor, bool complete,
	TypeId underlying)
{
	if (entities.size() >= kNoEntity)
		throw std::runtime_error("too many PA11 entities");
	const EntityId entity = static_cast<EntityId>(entities.size());
	entities.push_back(EntityRecord());
	EntityRecord& record = entities.back();
	record.name = name;
	record.flavor = flavor;
	record.complete = complete;
	record.underlying = underlying;
	record.type = types.Named(entity);
	return entity;
}

BindingId Program::AddBinding(ScopeId owner, BindingKind kind, NameId name,
	TypeId type, bool constant, std::int64_t value, NamedFlavor display)
{
	NameEntry* entry = EnsureEntry(owner, name);
	if (entry->name_space != kNoScope)
		throw std::runtime_error("binding conflicts with namespace");
	if (bindings.size() >= kNoBinding)
		throw std::runtime_error("too many PA11 bindings");
	const BindingId binding = static_cast<BindingId>(bindings.size());
	bindings.push_back(BindingRecord());
	BindingRecord& record = bindings.back();
	record.owner = owner;
	record.kind = kind;
	record.name = name;
	record.type = type;
	record.constant = constant;
	record.value = value;
	record.display_flavor = display;
	ScopeRecord& scope = scopes_[owner];
	if (scope.first_binding == kNoBinding) scope.first_binding = binding;
	else bindings[scope.last_binding].next = binding;
	scope.last_binding = binding;
	if (kind == BIND_TYPE || kind == BIND_TYPE_ALIAS) entry->type = type;
	else entry->ordinary = binding;
	return binding;
}

void Program::SetTypeName(ScopeId owner, NameId name, TypeId type)
{
	NameEntry* entry = EnsureEntry(owner, name);
	if (entry->name_space != kNoScope)
		throw std::runtime_error("type conflicts with namespace");
	entry->type = type;
}

void Program::SetEntityScope(EntityId entity, ScopeId scope)
{
	entities[entity].member_scope = scope;
}

Program::NameEntry* Program::EnsureEntry(ScopeId scope, NameId name)
{
	if ((entries_.size() + 1) * 10 > entry_slots_.size() * 7)
		RehashEntries(entry_slots_.size() * 2);
	const std::size_t mask = entry_slots_.size() - 1;
	std::size_t slot = MixHash(scope, name) & mask;
	while (entry_slots_[slot] != 0)
	{
		NameEntry& entry = entries_[entry_slots_[slot] - 1];
		if (entry.scope == scope && entry.name == name) return &entry;
		slot = (slot + 1) & mask;
	}
	entries_.push_back(NameEntry());
	NameEntry& entry = entries_.back();
	entry.scope = scope;
	entry.name = name;
	entry_slots_[slot] = static_cast<std::uint32_t>(entries_.size());
	return &entry;
}

const Program::NameEntry* Program::FindEntry(ScopeId scope,
	NameId name) const
{
	const std::size_t mask = entry_slots_.size() - 1;
	std::size_t slot = MixHash(scope, name) & mask;
	while (entry_slots_[slot] != 0)
	{
		const NameEntry& entry = entries_[entry_slots_[slot] - 1];
		if (entry.scope == scope && entry.name == name) return &entry;
		slot = (slot + 1) & mask;
	}
	return 0;
}

void Program::RehashEntries(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < entries_.size(); ++i)
	{
		std::size_t slot = MixHash(entries_[i].scope, entries_[i].name) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	entry_slots_.swap(replacement);
}

LookupResult Program::DirectLookup(ScopeId scope, NameId name,
	LookupKind kind) const
{
	LookupResult result;
	const NameEntry* entry = FindEntry(scope, name);
	if (!entry) return result;
	if (kind == LOOKUP_NAMESPACE || kind == LOOKUP_SCOPE_CARRIER)
		result.name_space = entry->name_space;
	if (kind == LOOKUP_TYPE || kind == LOOKUP_SCOPE_CARRIER)
		result.type = entry->type;
	if (kind == LOOKUP_ORDINARY) result.ordinary = entry->ordinary;
	return result;
}

LookupResult Program::LookupGraph(ScopeId scope, NameId name,
	LookupKind kind)
{
	++lookup_generation_;
	if (lookup_generation_ == 0)
	{
		std::fill(lookup_marks_.begin(), lookup_marks_.end(), 0);
		lookup_generation_ = 1;
	}
	std::vector<ScopeId> worklist(1, scope);
	lookup_marks_[scope] = lookup_generation_;
	for (std::size_t i = 0; i < worklist.size(); ++i)
	{
		const ScopeId current = worklist[i];
		++lookup_scope_visits;
		const LookupResult direct = DirectLookup(current, name, kind);
		if (!direct.Empty()) return direct;
		for (std::uint32_t edge = scopes_[current].first_using;
			edge != std::numeric_limits<std::uint32_t>::max();
			edge = using_edges_[edge].next)
		{
			++lookup_edge_visits;
			const ScopeId target = using_edges_[edge].target;
			if (lookup_marks_[target] == lookup_generation_) continue;
			lookup_marks_[target] = lookup_generation_;
			worklist.push_back(target);
		}
	}
	return LookupResult();
}

LookupResult Program::LookupUnqualified(ScopeId scope, NameId name,
	LookupKind kind)
{
	for (ScopeId current = scope; current != kNoScope;
		current = scopes_[current].parent)
	{
		const LookupResult result = LookupGraph(current, name, kind);
		if (!result.Empty()) return result;
	}
	return LookupResult();
}

ScopeId Program::CarrierScope(const LookupResult& result) const
{
	if (result.name_space != kNoScope) return result.name_space;
	if (result.type != kNoType) return ScopeForType(result.type);
	return kNoScope;
}

std::vector<NameId> Program::SplitName(const std::string& spelling,
	bool* global)
{
	std::vector<NameId> parts;
	std::size_t first = 0;
	*global = spelling.size() >= 2 && spelling[0] == ':' && spelling[1] == ':';
	if (*global) first = 2;
	while (first < spelling.size())
	{
		const std::size_t separator = spelling.find("::", first);
		const std::size_t last = separator == std::string::npos ?
			spelling.size() : separator;
		if (last == first) throw std::runtime_error("invalid qualified name");
		parts.push_back(names.Intern(spelling.substr(first, last - first)));
		if (separator == std::string::npos) break;
		first = separator + 2;
	}
	return parts;
}

LookupResult Program::Lookup(ScopeId current, const std::string& spelling,
	LookupKind kind)
{
	++lookup_queries;
	bool global = false;
	const std::vector<NameId> parts = SplitName(spelling, &global);
	if (parts.empty()) return LookupResult();
	if (parts.size() == 1)
		return global ? LookupGraph(GlobalScope(), parts[0], kind) :
			LookupUnqualified(current, parts[0], kind);
	LookupResult carrier = global ?
		LookupGraph(GlobalScope(), parts[0], LOOKUP_SCOPE_CARRIER) :
		LookupUnqualified(current, parts[0], LOOKUP_SCOPE_CARRIER);
	ScopeId owner = CarrierScope(carrier);
	if (owner == kNoScope) return LookupResult();
	for (std::size_t i = 1; i + 1 < parts.size(); ++i)
	{
		carrier = LookupGraph(owner, parts[i], LOOKUP_SCOPE_CARRIER);
		owner = CarrierScope(carrier);
		if (owner == kNoScope) return LookupResult();
	}
	return LookupGraph(owner, parts.back(), kind);
}

LookupResult Program::LookupDirect(ScopeId scope,
	const std::string& spelling, LookupKind kind)
{
	++lookup_queries;
	return DirectLookup(scope, names.Intern(spelling), kind);
}

ScopeId Program::ResolveScope(ScopeId current, const std::string& spelling)
{
	return CarrierScope(Lookup(current, spelling, LOOKUP_SCOPE_CARRIER));
}

ScopeId Program::ScopeForType(TypeId type) const
{
	type = types.RemoveTopCv(type);
	const TypeRecord& record = types.Get(type);
	if (record.kind != TYPE_NAMED) return kNoScope;
	return entities[record.entity].member_scope;
}

std::size_t Program::FundamentalSize(FundamentalKind kind) const
{
	switch (kind)
	{
	case FUND_BOOL: case FUND_CHAR: case FUND_SIGNED_CHAR:
	case FUND_UNSIGNED_CHAR: return 1;
	case FUND_SHORT_INT: case FUND_UNSIGNED_SHORT_INT:
	case FUND_CHAR16_T: return 2;
	case FUND_INT: case FUND_UNSIGNED_INT: case FUND_FLOAT:
	case FUND_WCHAR_T: case FUND_CHAR32_T: return 4;
	case FUND_LONG_INT: case FUND_UNSIGNED_LONG_INT:
	case FUND_LONG_LONG_INT: case FUND_UNSIGNED_LONG_LONG_INT:
	case FUND_DOUBLE: return 8;
	case FUND_LONG_DOUBLE: return 16;
	case FUND_VOID: break;
	}
	throw std::runtime_error("incomplete fundamental type");
}

std::size_t Program::SizeOf(TypeId type) const
{
	std::size_t multiplier = 1;
	while (true)
	{
		const TypeRecord& record = types.Get(type);
		if (record.kind == TYPE_QUALIFIED)
		{
			type = record.child;
			continue;
		}
		if (record.kind == TYPE_ARRAY)
		{
			if (record.bound == 0 ||
				record.bound > std::numeric_limits<std::size_t>::max() ||
				multiplier > std::numeric_limits<std::size_t>::max() /
					static_cast<std::size_t>(record.bound))
				throw std::runtime_error("invalid array size");
			multiplier *= static_cast<std::size_t>(record.bound);
			type = record.child;
			continue;
		}
		std::size_t size = 0;
		switch (record.kind)
		{
		case TYPE_FUNDAMENTAL: size = FundamentalSize(record.fundamental); break;
		case TYPE_POINTER: case TYPE_LVALUE_REFERENCE:
		case TYPE_RVALUE_REFERENCE: size = 8; break;
		case TYPE_NAMED:
		{
			const EntityRecord& entity = entities[record.entity];
			if (!entity.complete)
				throw std::runtime_error("incomplete named type");
			size = entity.flavor == NAMED_ENUM ||
				entity.flavor == NAMED_ENUM_CLASS ?
				SizeOf(entity.underlying) : 1;
			break;
		}
		default: throw std::runtime_error("invalid sizeof operand type");
		}
		if (multiplier > std::numeric_limits<std::size_t>::max() / size)
			throw std::runtime_error("object type is too large");
		return multiplier * size;
	}
}

std::size_t Program::AlignOf(TypeId type) const
{
	const TypeRecord* record = &types.Get(type);
	while (record->kind == TYPE_QUALIFIED || record->kind == TYPE_ARRAY)
	{
		type = record->child;
		record = &types.Get(type);
	}
	return SizeOf(type);
}

std::string Program::RenderTypeInner(TypeId type) const
{
	const TypeRecord& record = types.Get(type);
	switch (record.kind)
	{
	case TYPE_FUNDAMENTAL: return FundamentalName(record.fundamental);
	case TYPE_NAMED:
	{
		const EntityRecord& entity = entities[record.entity];
		return std::string(FlavorName(entity.flavor)) + " " +
			names.Get(entity.name);
	}
	case TYPE_QUALIFIED:
	{
		std::string prefix;
		if ((record.cv & CV_CONST) != 0) prefix += "const ";
		if ((record.cv & CV_VOLATILE) != 0) prefix += "volatile ";
		return prefix + RenderTypeInner(record.child);
	}
	case TYPE_POINTER:
		return "pointer to " + RenderTypeInner(record.child);
	case TYPE_LVALUE_REFERENCE:
		return "lvalue-reference to " + RenderTypeInner(record.child);
	case TYPE_RVALUE_REFERENCE:
		return "rvalue-reference to " + RenderTypeInner(record.child);
	case TYPE_ARRAY:
		return "array of " + std::to_string(record.bound) + " " +
			RenderTypeInner(record.child);
	case TYPE_FUNCTION:
	{
		std::string result = "function of (";
		const TypeId* parameters = types.Parameters(type);
		for (std::size_t i = 0; i < record.parameter_count; ++i)
		{
			if (i != 0) result += ", ";
			result += RenderTypeInner(parameters[i]);
		}
		if (record.variadic)
		{
			if (record.parameter_count != 0) result += ", ";
			result += "...";
		}
		return result + ") returning " + RenderTypeInner(record.child);
	}
	case TYPE_INVALID: break;
	}
	throw std::logic_error("cannot render invalid type");
}

std::string Program::RenderType(TypeId type) const
{
	return RenderTypeInner(type);
}

void Program::RenderScope(std::ostream& output, ScopeId scope,
	std::size_t depth) const
{
	const ScopeRecord& record = scopes_[scope];
	for (std::size_t i = 0; i < depth; ++i) output << "  ";
	output << "scope ";
	switch (record.kind)
	{
	case SCOPE_NAMESPACE: output << "namespace " << names.Get(record.name); break;
	case SCOPE_TEMPLATE_PARAMETERS: output << "template-parameters"; break;
	case SCOPE_CLASS: output << "class " << names.Get(record.name); break;
	case SCOPE_ENUM: output << "enum " << names.Get(record.name); break;
	case SCOPE_FUNCTION: output << "function " << names.Get(record.name); break;
	case SCOPE_BLOCK: output << "block"; break;
	}
	output << '\n';
	for (BindingId binding = record.first_binding; binding != kNoBinding;
		binding = bindings[binding].next)
	{
		const BindingRecord& item = bindings[binding];
		for (std::size_t i = 0; i < depth + 1; ++i) output << "  ";
		switch (item.kind)
		{
		case BIND_TYPE: output << "type "; break;
		case BIND_TYPE_ALIAS: output << "type-alias "; break;
		case BIND_ENUMERATOR: output << "enumerator "; break;
		case BIND_FUNCTION: output << "function "; break;
		case BIND_VARIABLE: output << "variable "; break;
		case BIND_PARAMETER: output << "parameter "; break;
		}
		output << names.Get(item.name) << ' ';
		if (item.kind == BIND_TYPE && item.display_flavor != NAMED_NONE)
			output << FlavorName(item.display_flavor) << ' ' << names.Get(item.name);
		else output << RenderTypeInner(item.type);
		if (item.kind == BIND_ENUMERATOR) output << ' ' << item.value;
		output << '\n';
	}
	for (std::uint32_t edge = record.first_child;
		edge != std::numeric_limits<std::uint32_t>::max();
		edge = child_edges_[edge].next)
		RenderScope(output, child_edges_[edge].child, depth + 1);
}

void Program::Render(std::ostream& output) const
{
	output << "translation-unit\n";
	RenderScope(output, GlobalScope(), 1);
}

std::size_t Program::ScopeCount() const
{
	return scopes_.size();
}

std::size_t Program::StorageBytes() const
{
	return names.StorageBytes() + types.StorageBytes() +
		scopes_.capacity() * sizeof(ScopeRecord) +
		child_edges_.capacity() * sizeof(ChildEdge) +
		using_edges_.capacity() * sizeof(UsingEdge) +
		entries_.capacity() * sizeof(NameEntry) +
		entry_slots_.capacity() * sizeof(std::uint32_t) +
		lookup_marks_.capacity() * sizeof(std::uint32_t) +
		entities.capacity() * sizeof(EntityRecord) +
		bindings.capacity() * sizeof(BindingRecord);
}

}
}
