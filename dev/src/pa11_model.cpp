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
	case FUND_NULLPTR_T: return "nullptr_t";
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

const char* FunctionReturnText(std::uint8_t cv,
	std::uint8_t ref_qualifier)
{
	if (ref_qualifier == FUNCTION_REF_LVALUE)
	{
		if (cv == CV_CONST) return ") const & returning ";
		if (cv == CV_VOLATILE) return ") volatile & returning ";
		if (cv == (CV_CONST | CV_VOLATILE))
			return ") const volatile & returning ";
		return ") & returning ";
	}
	if (ref_qualifier == FUNCTION_REF_RVALUE)
	{
		if (cv == CV_CONST) return ") const && returning ";
		if (cv == CV_VOLATILE) return ") volatile && returning ";
		if (cv == (CV_CONST | CV_VOLATILE))
			return ") const volatile && returning ";
		return ") && returning ";
	}
	if (cv == CV_CONST) return ") const returning ";
	if (cv == CV_VOLATILE) return ") volatile returning ";
	if (cv == (CV_CONST | CV_VOLATILE))
		return ") const volatile returning ";
	return ") returning ";
}

template <typename T, std::size_t InlineCapacity>
class SmallStack
{
public:
	SmallStack() : size_(0), spilled_(false) {}
	bool Empty() const { return size_ == 0; }
	T& Back()
	{
		return spilled_ ? overflow_.back() : inline_[size_ - 1];
	}
	void Pop()
	{
		if (spilled_) overflow_.pop_back();
		--size_;
	}
	void Push(const T& value)
	{
		if (!spilled_ && size_ < InlineCapacity)
		{
			inline_[size_++] = value;
			return;
		}
		if (!spilled_)
		{
			overflow_.reserve(InlineCapacity * 2);
			overflow_.insert(overflow_.end(), inline_,
				inline_ + InlineCapacity);
			spilled_ = true;
		}
		overflow_.push_back(value);
		++size_;
	}
	std::size_t StorageBytes() const
	{
		return sizeof(inline_) + overflow_.capacity() * sizeof(T);
	}

private:
	T inline_[InlineCapacity];
	std::vector<T> overflow_;
	std::size_t size_;
	bool spilled_;
};

class CompactIdList
{
public:
	CompactIdList() : inline_(), overflow_(), size_(0), spilled_(false) {}
	void Assign(const std::vector<ScopeId>& values)
	{
		size_ = 0;
		overflow_.clear();
		spilled_ = false;
		for (std::size_t i = 0; i < values.size(); ++i) Push(values[i]);
	}
	void Push(std::uint32_t value)
	{
		if (size_ < 2 && !spilled_) inline_[size_] = value;
		else
		{
			if (!spilled_)
			{
				overflow_.insert(overflow_.end(), inline_, inline_ + 2);
				spilled_ = true;
			}
			overflow_.push_back(value);
		}
		++size_;
	}
	std::size_t Size() const { return size_; }
	std::uint32_t operator[](std::size_t index) const
	{
		return spilled_ ? overflow_[index] : inline_[index];
	}
	std::size_t StorageBytes() const
	{
		return overflow_.capacity() * sizeof(ScopeId);
	}

private:
	std::uint32_t inline_[2];
	std::vector<std::uint32_t> overflow_;
	std::size_t size_;
	bool spilled_;
};

}

std::size_t MixHash(std::size_t seed, std::uint64_t value)
{
	std::uint64_t mixed = static_cast<std::uint64_t>(seed);
	mixed ^= value + 0x9e3779b97f4a7c15ULL + (mixed << 6) + (mixed >> 2);
	mixed ^= mixed >> 30;
	mixed *= 0xbf58476d1ce4e5b9ULL;
	mixed ^= mixed >> 27;
	mixed *= 0x94d049bb133111ebULL;
	mixed ^= mixed >> 31;
	return static_cast<std::size_t>(mixed);
}

NameTable::NameTable(InternedStringTable& strings)
	: strings_(strings), size_(0)
{
}

NameId NameTable::Intern(const std::string& spelling)
{
	return InternRange(spelling, 0, spelling.size());
}

NameId NameTable::InternRange(const std::string& spelling,
	std::size_t first, std::size_t count)
{
	const NameId id = strings_.InternRange(spelling, first, count);
	if (used_.size() <= id) used_.resize(static_cast<std::size_t>(id) + 1, 0);
	if (used_[id] == 0)
	{
		used_[id] = 1;
		++size_;
	}
	return id;
}

const std::string& NameTable::Get(NameId name) const
{
	return strings_.Get(name);
}

std::size_t NameTable::Size() const
{
	return size_;
}

std::size_t NameTable::StorageBytes() const
{
	return used_.capacity() * sizeof(std::uint8_t);
}

NamePath::NamePath() : global(false), size_(0)
{
	std::fill(inline_parts_, inline_parts_ + 4, 0);
}

void NamePath::Reserve(std::size_t count)
{
	if (count > 4) overflow_parts_.reserve(count);
}

void NamePath::Push(NameId name)
{
	if (size_ < 4 && overflow_parts_.empty())
		inline_parts_[size_] = name;
	else
	{
		if (overflow_parts_.empty())
			overflow_parts_.insert(overflow_parts_.end(), inline_parts_,
				inline_parts_ + 4);
		overflow_parts_.push_back(name);
	}
	++size_;
}

void NamePath::Pop()
{
	if (size_ == 0) throw std::logic_error("empty qualified name");
	if (!overflow_parts_.empty()) overflow_parts_.pop_back();
	--size_;
}

bool NamePath::Empty() const
{
	return size_ == 0;
}

std::size_t NamePath::Size() const
{
	return size_;
}

NameId NamePath::operator[](std::size_t index) const
{
	return overflow_parts_.empty() ? inline_parts_[index] :
		overflow_parts_[index];
}

NameId NamePath::Last() const
{
	return Empty() ? 0 : (*this)[size_ - 1];
}

TypeRecord::TypeRecord()
	: kind(TYPE_INVALID), child(kNoType), entity(kNoEntity), bound(0),
	  parameter_offset(0), parameter_count(0), cv(CV_NONE),
	  ref_qualifier(FUNCTION_REF_NONE), variadic(false),
	  fundamental(FUND_INT)
{
}

TypeTable::TypeTable() : slots_(64, 0), index_probes_(0)
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

TypeId TypeTable::MemberPointer(TypeId owner, TypeId member)
{
	owner = RemoveTopCv(owner);
	const TypeRecord& class_type = Get(owner);
	if (class_type.kind != TYPE_NAMED)
		throw std::runtime_error("member pointer owner is not a class");
	TypeRecord candidate;
	candidate.kind = TYPE_MEMBER_POINTER;
	candidate.child = member;
	candidate.entity = class_type.entity;
	candidate.bound = owner;
	return Intern(candidate, 0, 0);
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
	const std::vector<TypeId>& parameters, bool variadic, std::uint8_t cv,
	std::uint8_t ref_qualifier)
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
	candidate.cv = cv;
	candidate.ref_qualifier = ref_qualifier;
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

std::size_t TypeTable::IndexProbes() const
{
	return index_probes_;
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
	hash = MixHash(hash, record.ref_qualifier);
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
		existing.cv != candidate.cv ||
		existing.ref_qualifier != candidate.ref_qualifier ||
		existing.variadic != candidate.variadic ||
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
		++index_probes_;
		const TypeId type = slots_[slot];
		if (Equal(types_[type], candidate, parameters, count)) return type;
		slot = (slot + 1) & mask;
	}
	++index_probes_;
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
	: name(0), identity_name(0), owner(kNoScope), member_scope(kNoScope),
	  direct_base(kNoEntity), enclosing_class(kNoEntity),
	  flavor(NAMED_NONE), type(kNoType),
	  underlying(kNoType), declaration(kNoBinding), object_size(0),
	  object_alignment(0), natural_alignment(0), requested_alignment(0),
	  packing_alignment(0), base_access(ACCESS_PUBLIC), complete(false),
	  layout_complete(false),
	  has_user_declared_constructor(false),
	  has_user_provided_constructor(false), default_constructible(false),
	  trivial_default_constructor(false), has_user_declared_destructor(false),
	  destructible(true), trivial_destructor(true), has_direct_base(false),
	  is_aggregate(false), empty_class(false), indirect_class_value_abi(false)
{
}

BindingRecord::BindingRecord()
	: owner(kNoScope), name(0), qualified_name(0), kind(BIND_VARIABLE), type(kNoType),
	  conversion_target(kNoType),
	  next(kNoBinding), member_owner(kNoEntity), access_owner(kNoEntity),
	  member_offset(0), requested_alignment(0), bit_offset(0), bit_width(0),
	  bit_storage_bits(0),
	  overload_ordinal(0), member_ordinal(kNoBinding),
	  display_flavor(NAMED_NONE), display_type_name(0),
		  canonical(kNoBinding), value(0), operator_kind(OPERATOR_NONE),
		  builtin_function(BUILTIN_FUNCTION_NONE),
		  operator_literal_suffix(0), language_linkage(LANGUAGE_LINKAGE_CPP),
	  storage_class(STORAGE_CLASS_NONE), access(ACCESS_PUBLIC),
	  constant(false), nonthrowing(false), unnamed_namespace_linkage(false),
	  thread_local_storage(false),
	  non_static_data_member(false), mutable_member(false), bit_field(false),
	  static_member_function(false),
	  has_default_member_initializer(false), conversion_function(false),
	  constructor(false),
	  constructor_base_entry(false),
	  destructor(false), destructor_base_entry(false), inline_function(false)
{
}

LookupResult::LookupResult()
	: name_space(kNoScope), type(kNoType), type_declaration(kNoBinding),
	  type_declaration_canonical(kNoBinding),
	  ordinary(kNoBinding), ordinary_declaration(kNoBinding),
	  naming_class(kNoEntity)
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
	NameId name, emission_name;
	EntityId entity;
	BindingId first_binding;
	BindingId last_binding;
	std::uint32_t first_child;
	std::uint32_t last_child;
	std::uint32_t first_using;

	ScopeRecord()
		: parent(kNoScope), kind(SCOPE_NAMESPACE), name(0), emission_name(0),
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
	BindingId type_declaration;
	BindingId ordinary;

	NameEntry()
		: scope(kNoScope), name(0), name_space(kNoScope), type(kNoType),
		  type_declaration(kNoBinding), ordinary(kNoBinding) {}
};

struct Program::UsingEdge
{
	ScopeId owner;
	ScopeId target;
	std::uint32_t next;
	UsingEdge(ScopeId owner_value, ScopeId target_value,
		std::uint32_t next_value)
		: owner(owner_value), target(target_value), next(next_value) {}
};

struct Program::ChildEdge
{
	ScopeId child;
	std::uint32_t next;
	explicit ChildEdge(ScopeId child_value)
		: child(child_value),
		  next(std::numeric_limits<std::uint32_t>::max()) {}
};

struct Program::LookupCacheEntry
{
	ScopeId scope;
	NameId name;
	LookupKind kind;
	LookupResult result;
	CompactIdList scope_dependencies;
	std::uint32_t cache_dependency;
	std::uint64_t generation;
	bool valid;

	LookupCacheEntry(ScopeId scope_value, NameId name_value,
		LookupKind kind_value,
		const LookupResult& result_value)
		: scope(scope_value), name(name_value), kind(kind_value),
		  result(result_value),
		  cache_dependency(std::numeric_limits<std::uint32_t>::max()),
		  generation(1), valid(true) {}
};

struct Program::LookupCache
{
	// Reverse links are generation-qualified so invalidated links can remain in
	// compact append-only lists until local density justifies compaction.
	struct Dependent
	{
		std::uint32_t entry;
		std::uint64_t generation;
		Dependent() : entry(0), generation(0) {}
		Dependent(std::uint32_t entry_value, std::uint64_t generation_value)
			: entry(entry_value), generation(generation_value) {}
	};
	class DependentList
	{
	public:
		DependentList()
			: inline_(), overflow_(), size_(0), spilled_(false) {}
		std::size_t Size() const { return size_; }
		const Dependent& operator[](std::size_t index) const
			{ return spilled_ ? overflow_[index] : inline_[index]; }
		void Set(std::size_t index, const Dependent& value)
		{
			if (spilled_) overflow_[index] = value;
			else inline_[index] = value;
		}
		void Push(const Dependent& value)
		{
			if (size_ < 2 && !spilled_) inline_[size_] = value;
			else
			{
				if (!spilled_)
				{
					overflow_.insert(overflow_.end(), inline_, inline_ + 2);
					spilled_ = true;
				}
				overflow_.push_back(value);
			}
			++size_;
		}
		void Resize(std::size_t size)
		{
			if (spilled_) overflow_.resize(size);
			size_ = size;
		}
		void Clear()
		{
			if (spilled_) overflow_.clear();
			size_ = 0;
		}
		std::size_t StorageBytes() const
			{ return overflow_.capacity() * sizeof(Dependent); }

	private:
		Dependent inline_[2];
		std::vector<Dependent> overflow_;
		std::size_t size_;
		bool spilled_;
	};
	struct ScopeDependencyBucket
	{
		// A declaration insertion affects one interned name. Using-edge insertion
		// deliberately walks every bucket owned by the affected scope.
		ScopeId scope;
		NameId name;
		DependentList dependents;
		std::size_t active;
		ScopeDependencyBucket(ScopeId scope_value, NameId name_value)
			: scope(scope_value), name(name_value), active(0) {}
	};

	std::vector<LookupCacheEntry> entries;
	std::vector<std::uint32_t> slots;
	std::vector<ScopeDependencyBucket> scope_dependency_buckets;
	std::vector<std::uint32_t> scope_dependency_slots;
	std::vector<CompactIdList> scope_dependency_buckets_by_scope;
	std::vector<DependentList> cache_dependents;
	std::vector<std::size_t> active_cache_dependents;
	std::vector<std::uint32_t> invalidation_worklist;

	LookupCache() : slots(64, 0), scope_dependency_slots(64, 0) {}
	void AddScope();
	bool Find(ScopeId scope, NameId name, LookupKind kind,
		LookupResult* result, std::uint32_t* entry) const;
	std::uint32_t Store(ScopeId scope, NameId name, LookupKind kind,
		const LookupResult& result, const std::vector<ScopeId>& dependencies,
		std::uint32_t cache_dependency, std::size_t* dependency_edges);
	std::size_t InvalidateName(ScopeId scope, NameId name,
		std::size_t* worklist_pushes);
	std::size_t InvalidateScope(ScopeId scope, std::size_t* worklist_pushes);
	void Rehash(std::size_t capacity);
	void RehashScopeDependencies(std::size_t capacity);
	std::uint32_t FindScopeDependency(ScopeId scope, NameId name) const;
	std::uint32_t EnsureScopeDependency(ScopeId scope, NameId name);
	void CollectScopeDependents(std::uint32_t bucket,
		std::size_t* worklist_pushes);
	std::size_t DrainInvalidationWorklist(std::size_t* worklist_pushes);
	void Register(std::uint32_t entry, std::size_t* dependency_edges);
	void Deactivate(std::uint32_t entry);
	void CompactScopeDependents(std::uint32_t bucket);
	void CompactCacheDependents(std::uint32_t entry);
	std::size_t StorageBytes() const;
};

Program::Program(InternedStringTable& strings)
	: names(strings), lookup_queries(0), lookup_scope_visits(0),
	  lookup_edge_visits(0), lookup_cache_hits(0), lookup_cache_misses(0),
	  lookup_cache_invalidations(0), lookup_cache_dependency_edges(0),
	  lookup_cache_invalidation_pushes(0),
	  name_index_probes(0), using_index_probes(0),
	  using_edge_slots_(64, 0), entry_slots_(64, 0), lookup_generation_(1),
	  lookup_dependency_generation_(0), collecting_lookup_dependencies_(false),
	  lookup_cache_(new LookupCache())
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
	EntityId entity, ScopeId output_parent)
{
	if (scopes_.size() >= kNoScope)
		throw std::runtime_error("too many PA11 scopes");
	const ScopeId scope = static_cast<ScopeId>(scopes_.size());
	scopes_.push_back(ScopeRecord());
	ScopeRecord& record = scopes_.back();
	record.parent = parent;
	record.kind = kind;
	record.name = name;
	record.emission_name = name;
	record.entity = entity;
	lookup_marks_.push_back(0);
	lookup_dependency_marks_.push_back(0);
	lookup_cache_->AddScope();
	const ScopeId tree_parent = output_parent == kNoScope ? parent : output_parent;
	if (tree_parent != kNoScope)
	{
		const std::uint32_t edge =
			static_cast<std::uint32_t>(child_edges_.size());
		child_edges_.push_back(ChildEdge(scope));
		ScopeRecord& owner = scopes_[tree_parent];
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
	{
		entry->name_space = NewScope(parent, SCOPE_NAMESPACE, name);
		InvalidateLookupName(parent, name);
	}
	if (is_inline) AddUsingEdge(parent, entry->name_space);
	return entry->name_space;
}

void Program::SetScopeEmissionName(ScopeId scope, NameId name)
{
	if (scope >= scopes_.size())
		throw std::logic_error("invalid emission scope identity");
	scopes_[scope].emission_name = name;
}

void Program::AddNamespaceAlias(ScopeId owner, NameId name, ScopeId target)
{
	NameEntry* entry = EnsureEntry(owner, name);
	if (entry->ordinary != kNoBinding || entry->type != kNoType ||
		(entry->name_space != kNoScope && entry->name_space != target))
		throw std::runtime_error("invalid namespace alias binding");
	entry->name_space = target;
	InvalidateLookupName(owner, name);
}

void Program::AddUsingEdge(ScopeId owner, ScopeId target)
{
	if ((using_edges_.size() + 1) * 10 > using_edge_slots_.size() * 7)
		RehashUsingEdges(using_edge_slots_.size() * 2);
	const std::size_t mask = using_edge_slots_.size() - 1;
	std::size_t slot = MixHash(owner, target) & mask;
	while (using_edge_slots_[slot] != 0)
	{
		++using_index_probes;
		const UsingEdge& existing =
			using_edges_[using_edge_slots_[slot] - 1];
		if (existing.owner == owner && existing.target == target) return;
		slot = (slot + 1) & mask;
	}
	++using_index_probes;
	if (using_edges_.size() >=
		std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many PA11 using edges");
	const std::uint32_t edge =
		static_cast<std::uint32_t>(using_edges_.size());
	using_edges_.push_back(UsingEdge(owner, target,
		scopes_[owner].first_using));
	using_edge_slots_[slot] = edge + 1;
	scopes_[owner].first_using = edge;
	InvalidateLookupScope(owner);
}

void Program::RehashUsingEdges(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < using_edges_.size(); ++i)
	{
		const UsingEdge& edge = using_edges_[i];
		std::size_t slot = MixHash(edge.owner, edge.target) & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	using_edge_slots_.swap(replacement);
}

EntityId Program::NewEntity(NameId name, NamedFlavor flavor, bool complete,
	TypeId underlying, ScopeId owner, NameId identity_name)
{
	if (entities.size() >= kNoEntity)
		throw std::runtime_error("too many PA11 entities");
	const EntityId entity = static_cast<EntityId>(entities.size());
	entities.push_back(EntityRecord());
	EntityRecord& record = entities.back();
	record.name = name;
	record.identity_name = identity_name == 0 ? name : identity_name;
	record.owner = owner;
	if (owner != kNoScope && owner < scopes_.size() &&
		scopes_[owner].kind == SCOPE_CLASS)
		record.enclosing_class = scopes_[owner].entity;
	record.flavor = flavor;
	record.complete = complete;
	record.underlying = underlying;
	record.type = types.Named(entity);
	return entity;
}

void Program::BuildEmissionPath(ScopeId owner, NameId terminal,
	std::vector<NameId>* path) const
{
	path->clear();
	while (owner != kNoScope && owner != GlobalScope())
	{
		const ScopeRecord& scope = scopes_[owner];
		if (scope.emission_name != 0 && (scope.kind == SCOPE_NAMESPACE ||
			scope.kind == SCOPE_CLASS || scope.kind == SCOPE_ENUM))
			path->push_back(scope.emission_name);
		owner = scope.parent;
	}
	std::reverse(path->begin(), path->end());
	path->push_back(terminal);
}

BindingId Program::AddBinding(ScopeId owner, BindingKind kind, NameId name,
	TypeId type, bool constant, std::int64_t value, NamedFlavor display,
	NameId display_type_name, BindingId canonical, bool merge_redeclaration)
{
	NameEntry* entry = EnsureEntry(owner, name);
	if (entry->name_space != kNoScope)
		throw std::runtime_error("binding conflicts with namespace");
	if (merge_redeclaration && canonical == kNoBinding &&
		entry->ordinary != kNoBinding &&
		(kind == BIND_FUNCTION || kind == BIND_VARIABLE))
	{
		const BindingRecord& previous = bindings[entry->ordinary];
		if (previous.kind == kind && previous.type == type)
			canonical = previous.canonical;
	}
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
	record.display_type_name = display_type_name;
	if (canonical == kNoBinding && kind == BIND_TYPE && types.IsNamed(type))
	{
		const TypeRecord& named = types.Get(types.RemoveTopCv(type));
		canonical = entities[named.entity].declaration;
		if (canonical == kNoBinding)
			entities[named.entity].declaration = binding;
	}
	record.canonical = canonical == kNoBinding ? binding : canonical;
	ScopeRecord& scope = scopes_[owner];
	if (scope.first_binding == kNoBinding) scope.first_binding = binding;
	else bindings[scope.last_binding].next = binding;
	scope.last_binding = binding;
	if (kind == BIND_TYPE || kind == BIND_TYPE_ALIAS)
	{
		entry->type = type;
		entry->type_declaration = binding;
	}
	else entry->ordinary = binding;
	InvalidateLookupName(owner, name);
	return binding;
}

BindingId Program::AddOutputTypeBinding(ScopeId owner, NameId display_name,
	TypeId type, NamedFlavor display)
{
	if (bindings.size() >= kNoBinding)
		throw std::runtime_error("too many PA11 bindings");
	const BindingId binding = static_cast<BindingId>(bindings.size());
	bindings.push_back(BindingRecord());
	BindingRecord& record = bindings.back();
	record.owner = owner;
	record.kind = BIND_TYPE;
	record.name = display_name;
	record.type = type;
	record.display_flavor = display;
	record.canonical = binding;
	ScopeRecord& scope = scopes_[owner];
	if (scope.first_binding == kNoBinding) scope.first_binding = binding;
	else bindings[scope.last_binding].next = binding;
	scope.last_binding = binding;
	return binding;
}

void Program::SetTypeName(ScopeId owner, NameId name, TypeId type)
{
	NameEntry* entry = EnsureEntry(owner, name);
	if (entry->name_space != kNoScope)
		throw std::runtime_error("type conflicts with namespace");
	entry->type = type;
	InvalidateLookupName(owner, name);
}

void Program::SetEntityScope(EntityId entity, ScopeId scope)
{
	entities[entity].member_scope = scope;
	if (scope >= scopes_.size())
		throw std::logic_error("entity member scope is invalid");
	scopes_[scope].entity = entity;
}

ScopeId Program::ParentScope(ScopeId scope) const
{
	if (scope >= scopes_.size()) return kNoScope;
	return scopes_[scope].parent;
}

ScopeKind Program::KindOfScope(ScopeId scope) const
{
	if (scope >= scopes_.size())
		throw std::logic_error("invalid scope kind query");
	return scopes_[scope].kind;
}

NameId Program::NameOfScope(ScopeId scope) const
{
	if (scope >= scopes_.size()) return 0;
	return scopes_[scope].name;
}

EntityId Program::EntityForScope(ScopeId scope) const
{
	if (scope >= scopes_.size()) return kNoEntity;
	return scopes_[scope].entity;
}

void Program::SetDirectBase(EntityId derived, EntityId base, AccessKind access)
{
	if (derived >= entities.size() || base >= entities.size() || derived == base)
		throw std::runtime_error("invalid direct base relationship");
	if (entities[derived].direct_base != kNoEntity &&
		entities[derived].direct_base != base)
		throw std::runtime_error("multiple inheritance is outside PA16");
	if (entities[derived].member_scope != kNoScope)
		throw std::logic_error(
			"direct base must be fixed before publishing the member scope");
	if (IsBaseOf(derived, base))
		throw std::runtime_error("cyclic class inheritance");
	entities[derived].direct_base = base;
	entities[derived].base_access = access;
	entities[derived].has_direct_base = true;
}

bool Program::IsBaseOf(EntityId base, EntityId derived) const
{
	if (base == kNoEntity || derived == kNoEntity ||
		base >= entities.size() || derived >= entities.size()) return false;
	for (EntityId current = derived; current != kNoEntity;
		current = entities[current].direct_base)
		if (current == base) return true;
	return false;
}

Program::NameEntry* Program::EnsureEntry(ScopeId scope, NameId name)
{
	if ((entries_.size() + 1) * 10 > entry_slots_.size() * 7)
		RehashEntries(entry_slots_.size() * 2);
	const std::size_t mask = entry_slots_.size() - 1;
	std::size_t slot = MixHash(scope, name) & mask;
	while (entry_slots_[slot] != 0)
	{
		++name_index_probes;
		NameEntry& entry = entries_[entry_slots_[slot] - 1];
		if (entry.scope == scope && entry.name == name) return &entry;
		slot = (slot + 1) & mask;
	}
	++name_index_probes;
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
		++name_index_probes;
		const NameEntry& entry = entries_[entry_slots_[slot] - 1];
		if (entry.scope == scope && entry.name == name) return &entry;
		slot = (slot + 1) & mask;
	}
	++name_index_probes;
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

void Program::BeginLookupDependencies()
{
	collecting_lookup_dependencies_ = true;
	lookup_dependencies_.clear();
	++lookup_dependency_generation_;
	if (lookup_dependency_generation_ == 0)
	{
		std::fill(lookup_dependency_marks_.begin(),
			lookup_dependency_marks_.end(), 0);
		lookup_dependency_generation_ = 1;
	}
}

void Program::RecordLookupDependency(ScopeId scope)
{
	if (!collecting_lookup_dependencies_ || scope >= scopes_.size()) return;
	if (lookup_dependency_marks_[scope] == lookup_dependency_generation_) return;
	lookup_dependency_marks_[scope] = lookup_dependency_generation_;
	lookup_dependencies_.push_back(scope);
}

void Program::InvalidateLookupScope(ScopeId scope)
{
	std::size_t worklist_pushes = 0;
	lookup_cache_invalidations +=
		lookup_cache_->InvalidateScope(scope, &worklist_pushes);
	lookup_cache_invalidation_pushes += worklist_pushes;
}

void Program::InvalidateLookupName(ScopeId scope, NameId name)
{
	std::size_t worklist_pushes = 0;
	lookup_cache_invalidations +=
		lookup_cache_->InvalidateName(scope, name, &worklist_pushes);
	lookup_cache_invalidation_pushes += worklist_pushes;
}

LookupResult Program::DirectLookup(ScopeId scope, NameId name,
	LookupKind kind) const
{
	LookupResult result;
	const NameEntry* entry = FindEntry(scope, name);
	if (!entry) return result;
	const EntityId scope_entity = scopes_[scope].entity;
	if (scope_entity != kNoEntity)
	{
		const NamedFlavor flavor = entities[scope_entity].flavor;
		if (flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
			flavor == NAMED_UNION)
			result.naming_class = scope_entity;
	}
	if (kind == LOOKUP_NAMESPACE || kind == LOOKUP_SCOPE_CARRIER)
		result.name_space = entry->name_space;
	if (kind == LOOKUP_TYPE || kind == LOOKUP_SCOPE_CARRIER)
	{
		result.type = entry->type;
		result.type_declaration = entry->type_declaration;
		result.type_declaration_canonical =
			entry->type_declaration == kNoBinding ?
			kNoBinding : bindings[entry->type_declaration].canonical;
	}
	if (kind == LOOKUP_ORDINARY)
	{
		result.ordinary = entry->ordinary;
		result.ordinary_declaration = entry->ordinary == kNoBinding ?
			kNoBinding : bindings[entry->ordinary].canonical;
	}
	return result;
}

void Program::MergeLookup(LookupResult* result,
	const LookupResult& candidate) const
{
	if (candidate.Empty()) return;
	if (result->Empty())
	{
		*result = candidate;
		return;
	}
	if (result->name_space != candidate.name_space ||
		result->type != candidate.type ||
		result->type_declaration_canonical !=
			candidate.type_declaration_canonical ||
		result->ordinary_declaration != candidate.ordinary_declaration)
		throw std::runtime_error("ambiguous PA11 lookup");
}

LookupResult Program::LookupGraph(ScopeId scope, NameId name,
	LookupKind kind)
{
	RecordLookupDependency(scope);
	const EntityId scope_entity = scopes_[scope].entity;
	const EntityId naming_class = scope_entity != kNoEntity &&
		(entities[scope_entity].flavor == NAMED_STRUCT ||
		 entities[scope_entity].flavor == NAMED_CLASS ||
		 entities[scope_entity].flavor == NAMED_UNION) ?
		scope_entity : kNoEntity;
	++lookup_generation_;
	if (lookup_generation_ == 0)
	{
		std::fill(lookup_marks_.begin(), lookup_marks_.end(), 0);
		lookup_generation_ = 1;
	}
	lookup_worklist_.clear();
	lookup_marks_[scope] = lookup_generation_;
	++lookup_scope_visits;
	const LookupResult local = DirectLookup(scope, name, kind);
	if (!local.Empty()) return local;
	const EntityId owner_entity = scopes_[scope].entity;
	if (owner_entity != kNoEntity &&
		entities[owner_entity].direct_base != kNoEntity)
	{
		const ScopeId target =
			entities[entities[owner_entity].direct_base].member_scope;
		if (target != kNoScope && lookup_marks_[target] != lookup_generation_)
		{
			++lookup_edge_visits;
			lookup_marks_[target] = lookup_generation_;
			lookup_worklist_.push_back(target);
		}
	}
	for (std::uint32_t edge = scopes_[scope].first_using;
		edge != std::numeric_limits<std::uint32_t>::max();
		edge = using_edges_[edge].next)
	{
		++lookup_edge_visits;
		const ScopeId target = using_edges_[edge].target;
		if (lookup_marks_[target] == lookup_generation_) continue;
		lookup_marks_[target] = lookup_generation_;
		lookup_worklist_.push_back(target);
	}
	LookupResult result;
	for (std::size_t i = 0; i < lookup_worklist_.size(); ++i)
	{
		const ScopeId current = lookup_worklist_[i];
		RecordLookupDependency(current);
		++lookup_scope_visits;
		const LookupResult direct = DirectLookup(current, name, kind);
		if (!direct.Empty())
		{
			MergeLookup(&result, direct);
			continue;
		}
		const EntityId current_entity = scopes_[current].entity;
		if (current_entity != kNoEntity &&
			entities[current_entity].direct_base != kNoEntity)
		{
			const ScopeId target =
				entities[entities[current_entity].direct_base].member_scope;
			if (target != kNoScope &&
				lookup_marks_[target] != lookup_generation_)
			{
				++lookup_edge_visits;
				lookup_marks_[target] = lookup_generation_;
				lookup_worklist_.push_back(target);
			}
		}
		for (std::uint32_t edge = scopes_[current].first_using;
			edge != std::numeric_limits<std::uint32_t>::max();
			edge = using_edges_[edge].next)
		{
			++lookup_edge_visits;
			const ScopeId target = using_edges_[edge].target;
			if (lookup_marks_[target] == lookup_generation_) continue;
			lookup_marks_[target] = lookup_generation_;
			lookup_worklist_.push_back(target);
		}
	}
	if (!result.Empty() && naming_class != kNoEntity)
		result.naming_class = naming_class;
	return result;
}

LookupResult Program::LookupUnqualified(ScopeId scope, NameId name,
	LookupKind kind)
{
	const ScopeId requested = scope;
	BeginLookupDependencies();
	for (ScopeId current = scope; current != kNoScope;
		current = scopes_[current].parent)
	{
		LookupResult cached;
		std::uint32_t cache_entry = std::numeric_limits<std::uint32_t>::max();
		if (lookup_cache_->Find(current, name, kind, &cached, &cache_entry))
		{
			++lookup_cache_hits;
			if (current != requested)
			{
				// Preserve the lexical shortcut as one cache-fact edge instead of
				// copying the parent's transitive scope dependency set.
				std::size_t dependency_edges = 0;
				lookup_cache_->Store(requested, name, kind, cached,
					lookup_dependencies_, cache_entry, &dependency_edges);
				lookup_cache_dependency_edges += dependency_edges;
			}
			collecting_lookup_dependencies_ = false;
			return cached;
		}
		++lookup_cache_misses;
		const LookupResult result = LookupGraph(current, name, kind);
		if (!result.Empty())
		{
			std::size_t dependency_edges = 0;
			lookup_cache_->Store(requested, name, kind, result,
				lookup_dependencies_, std::numeric_limits<std::uint32_t>::max(),
				&dependency_edges);
			lookup_cache_dependency_edges += dependency_edges;
			collecting_lookup_dependencies_ = false;
			return result;
		}
	}
	const LookupResult missing;
	std::size_t dependency_edges = 0;
	lookup_cache_->Store(requested, name, kind, missing,
		lookup_dependencies_, std::numeric_limits<std::uint32_t>::max(),
		&dependency_edges);
	lookup_cache_dependency_edges += dependency_edges;
	collecting_lookup_dependencies_ = false;
	return missing;
}

void Program::LookupCache::AddScope()
{
	scope_dependency_buckets_by_scope.push_back(CompactIdList());
}

std::uint32_t Program::LookupCache::FindScopeDependency(ScopeId scope,
	NameId name) const
{
	const std::size_t mask = scope_dependency_slots.size() - 1;
	std::size_t slot = MixHash(scope, name) & mask;
	while (scope_dependency_slots[slot] != 0)
	{
		const std::uint32_t id = scope_dependency_slots[slot] - 1;
		const ScopeDependencyBucket& bucket = scope_dependency_buckets[id];
		if (bucket.scope == scope && bucket.name == name) return id;
		slot = (slot + 1) & mask;
	}
	return std::numeric_limits<std::uint32_t>::max();
}

void Program::LookupCache::RehashScopeDependencies(std::size_t capacity)
{
	scope_dependency_slots.assign(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < scope_dependency_buckets.size(); ++i)
	{
		const ScopeDependencyBucket& bucket = scope_dependency_buckets[i];
		std::size_t slot = MixHash(bucket.scope, bucket.name) & mask;
		while (scope_dependency_slots[slot] != 0) slot = (slot + 1) & mask;
		scope_dependency_slots[slot] = static_cast<std::uint32_t>(i + 1);
	}
}

std::uint32_t Program::LookupCache::EnsureScopeDependency(ScopeId scope,
	NameId name)
{
	if (scope >= scope_dependency_buckets_by_scope.size())
		throw std::logic_error("lookup cache dependency has invalid scope");
	if ((scope_dependency_buckets.size() + 1) * 10 >
		scope_dependency_slots.size() * 7)
		RehashScopeDependencies(scope_dependency_slots.size() * 2);
	const std::size_t mask = scope_dependency_slots.size() - 1;
	std::size_t slot = MixHash(scope, name) & mask;
	while (scope_dependency_slots[slot] != 0)
	{
		const std::uint32_t id = scope_dependency_slots[slot] - 1;
		const ScopeDependencyBucket& bucket = scope_dependency_buckets[id];
		if (bucket.scope == scope && bucket.name == name) return id;
		slot = (slot + 1) & mask;
	}
	if (scope_dependency_buckets.size() >=
		std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many lookup dependency owners");
	const std::uint32_t id =
		static_cast<std::uint32_t>(scope_dependency_buckets.size());
	scope_dependency_buckets.push_back(ScopeDependencyBucket(scope, name));
	scope_dependency_slots[slot] = id + 1;
	scope_dependency_buckets_by_scope[scope].Push(id);
	return id;
}

bool Program::LookupCache::Find(ScopeId scope, NameId name, LookupKind kind,
	LookupResult* result, std::uint32_t* entry_id) const
{
	const std::size_t mask = slots.size() - 1;
	std::size_t slot = (MixHash(scope, name) * 5U +
		static_cast<std::size_t>(kind)) & mask;
	while (slots[slot] != 0)
	{
		const std::uint32_t id = slots[slot] - 1;
		const LookupCacheEntry& entry = entries[id];
		if (entry.scope == scope && entry.name == name && entry.kind == kind)
		{
			if (!entry.valid) return false;
			*result = entry.result;
			*entry_id = id;
			return true;
		}
		slot = (slot + 1) & mask;
	}
	return false;
}

void Program::LookupCache::CompactScopeDependents(std::uint32_t bucket_id)
{
	ScopeDependencyBucket& bucket = scope_dependency_buckets[bucket_id];
	DependentList& dependents = bucket.dependents;
	const std::size_t active = bucket.active;
	if (dependents.Size() <= active * 2 + 16) return;
	std::size_t output = 0;
	for (std::size_t i = 0; i < dependents.Size(); ++i)
	{
		const Dependent link = dependents[i];
		if (link.entry >= entries.size()) continue;
		const LookupCacheEntry& entry = entries[link.entry];
		if (!entry.valid || entry.generation != link.generation) continue;
		dependents.Set(output++, link);
	}
	dependents.Resize(output);
	bucket.active = output;
}

void Program::LookupCache::CompactCacheDependents(std::uint32_t owner)
{
	DependentList& dependents = cache_dependents[owner];
	const std::size_t active = active_cache_dependents[owner];
	if (dependents.Size() <= active * 2 + 16) return;
	std::size_t output = 0;
	for (std::size_t i = 0; i < dependents.Size(); ++i)
	{
		const Dependent link = dependents[i];
		if (link.entry >= entries.size()) continue;
		const LookupCacheEntry& entry = entries[link.entry];
		if (!entry.valid || entry.generation != link.generation ||
			entry.cache_dependency != owner) continue;
		dependents.Set(output++, link);
	}
	dependents.Resize(output);
	active_cache_dependents[owner] = output;
}

void Program::LookupCache::Register(std::uint32_t id,
	std::size_t* dependency_edges)
{
	const LookupCacheEntry& entry = entries[id];
	for (std::size_t i = 0; i < entry.scope_dependencies.Size(); ++i)
	{
		const ScopeId scope = entry.scope_dependencies[i];
		const std::uint32_t bucket_id =
			EnsureScopeDependency(scope, entry.name);
		CompactScopeDependents(bucket_id);
		ScopeDependencyBucket& bucket = scope_dependency_buckets[bucket_id];
		bucket.dependents.Push(Dependent(id, entry.generation));
		++bucket.active;
		++*dependency_edges;
	}
	if (entry.cache_dependency == std::numeric_limits<std::uint32_t>::max())
		return;
	if (entry.cache_dependency >= entries.size())
		throw std::logic_error("lookup cache dependency has invalid entry");
	CompactCacheDependents(entry.cache_dependency);
	cache_dependents[entry.cache_dependency].Push(
		Dependent(id, entry.generation));
	++active_cache_dependents[entry.cache_dependency];
	++*dependency_edges;
}

std::uint32_t Program::LookupCache::Store(ScopeId scope, NameId name,
	LookupKind kind, const LookupResult& result,
	const std::vector<ScopeId>& dependencies, std::uint32_t cache_dependency,
	std::size_t* dependency_edges)
{
	if ((entries.size() + 1) * 10 > slots.size() * 7)
		Rehash(slots.size() * 2);
	const std::size_t mask = slots.size() - 1;
	std::size_t slot = (MixHash(scope, name) * 5U +
		static_cast<std::size_t>(kind)) & mask;
	while (slots[slot] != 0)
	{
		const std::uint32_t id = slots[slot] - 1;
		LookupCacheEntry& entry = entries[id];
		if (entry.scope == scope && entry.name == name && entry.kind == kind)
		{
			if (entry.valid) return id;
			entry.result = result;
			entry.scope_dependencies.Assign(dependencies);
			entry.cache_dependency = cache_dependency;
			++entry.generation;
			if (entry.generation == 0) entry.generation = 1;
			entry.valid = true;
			Register(id, dependency_edges);
			return id;
		}
		slot = (slot + 1) & mask;
	}
	if (entries.size() >= std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many lookup cache entries");
	const std::uint32_t id = static_cast<std::uint32_t>(entries.size());
	entries.push_back(LookupCacheEntry(scope, name, kind, result));
	entries.back().scope_dependencies.Assign(dependencies);
	entries.back().cache_dependency = cache_dependency;
	cache_dependents.push_back(DependentList());
	active_cache_dependents.push_back(0);
	slots[slot] = id + 1;
	Register(id, dependency_edges);
	return id;
}

void Program::LookupCache::Rehash(std::size_t capacity)
{
	slots.assign(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		const LookupCacheEntry& entry = entries[i];
		std::size_t slot = (MixHash(entry.scope, entry.name) * 5U +
			static_cast<std::size_t>(entry.kind)) & mask;
		while (slots[slot] != 0) slot = (slot + 1) & mask;
		slots[slot] = static_cast<std::uint32_t>(i + 1);
	}
}

void Program::LookupCache::Deactivate(std::uint32_t id)
{
	LookupCacheEntry& entry = entries[id];
	if (!entry.valid) return;
	entry.valid = false;
	for (std::size_t i = 0; i < entry.scope_dependencies.Size(); ++i)
	{
		const std::uint32_t bucket_id = FindScopeDependency(
			entry.scope_dependencies[i], entry.name);
		if (bucket_id < scope_dependency_buckets.size() &&
			scope_dependency_buckets[bucket_id].active != 0)
			--scope_dependency_buckets[bucket_id].active;
	}
	if (entry.cache_dependency < active_cache_dependents.size() &&
		active_cache_dependents[entry.cache_dependency] != 0)
		--active_cache_dependents[entry.cache_dependency];
}

void Program::LookupCache::CollectScopeDependents(std::uint32_t bucket_id,
	std::size_t* worklist_pushes)
{
	if (bucket_id >= scope_dependency_buckets.size()) return;
	ScopeDependencyBucket& bucket = scope_dependency_buckets[bucket_id];
	const DependentList& direct = bucket.dependents;
	for (std::size_t i = 0; i < direct.Size(); ++i)
	{
		const Dependent link = direct[i];
		if (link.entry >= entries.size()) continue;
		const LookupCacheEntry& entry = entries[link.entry];
		if (!entry.valid || entry.generation != link.generation) continue;
		invalidation_worklist.push_back(link.entry);
		++*worklist_pushes;
	}
	bucket.dependents.Clear();
	bucket.active = 0;
}

std::size_t Program::LookupCache::DrainInvalidationWorklist(
	std::size_t* worklist_pushes)
{
	std::size_t invalidated = 0;
	while (!invalidation_worklist.empty())
	{
		const std::uint32_t id = invalidation_worklist.back();
		invalidation_worklist.pop_back();
		if (id >= entries.size() || !entries[id].valid) continue;
		Deactivate(id);
		++invalidated;
		const DependentList& children = cache_dependents[id];
		for (std::size_t i = 0; i < children.Size(); ++i)
		{
			const Dependent link = children[i];
			if (link.entry >= entries.size()) continue;
			const LookupCacheEntry& child = entries[link.entry];
			if (!child.valid || child.generation != link.generation ||
				child.cache_dependency != id) continue;
			invalidation_worklist.push_back(link.entry);
			++*worklist_pushes;
		}
		cache_dependents[id].Clear();
		active_cache_dependents[id] = 0;
	}
	return invalidated;
}

std::size_t Program::LookupCache::InvalidateName(ScopeId scope, NameId name,
	std::size_t* worklist_pushes)
{
	invalidation_worklist.clear();
	const std::uint32_t bucket = FindScopeDependency(scope, name);
	if (bucket < scope_dependency_buckets.size())
		CollectScopeDependents(bucket, worklist_pushes);
	return DrainInvalidationWorklist(worklist_pushes);
}

std::size_t Program::LookupCache::InvalidateScope(ScopeId scope,
	std::size_t* worklist_pushes)
{
	invalidation_worklist.clear();
	if (scope >= scope_dependency_buckets_by_scope.size()) return 0;
	const CompactIdList& buckets =
		scope_dependency_buckets_by_scope[scope];
	for (std::size_t i = 0; i < buckets.Size(); ++i)
		CollectScopeDependents(buckets[i], worklist_pushes);
	return DrainInvalidationWorklist(worklist_pushes);
}

std::size_t Program::LookupCache::StorageBytes() const
{
	std::size_t bytes = entries.capacity() * sizeof(LookupCacheEntry) +
		slots.capacity() * sizeof(std::uint32_t) +
		scope_dependency_buckets.capacity() * sizeof(ScopeDependencyBucket) +
		scope_dependency_slots.capacity() * sizeof(std::uint32_t) +
		scope_dependency_buckets_by_scope.capacity() *
			sizeof(CompactIdList) +
		cache_dependents.capacity() * sizeof(DependentList) +
		active_cache_dependents.capacity() * sizeof(std::size_t) +
		invalidation_worklist.capacity() * sizeof(std::uint32_t);
	for (std::size_t i = 0; i < entries.size(); ++i)
		bytes += entries[i].scope_dependencies.StorageBytes();
	for (std::size_t i = 0; i < scope_dependency_buckets.size(); ++i)
		bytes += scope_dependency_buckets[i].dependents.StorageBytes();
	for (std::size_t i = 0;
		i < scope_dependency_buckets_by_scope.size(); ++i)
		bytes += scope_dependency_buckets_by_scope[i].StorageBytes();
	for (std::size_t i = 0; i < cache_dependents.size(); ++i)
		bytes += cache_dependents[i].StorageBytes();
	return bytes;
}

ScopeId Program::CarrierScope(const LookupResult& result) const
{
	if (result.name_space != kNoScope) return result.name_space;
	if (result.type != kNoType) return ScopeForType(result.type);
	return kNoScope;
}

LookupResult Program::Lookup(ScopeId current, const NamePath& name,
	LookupKind kind)
{
	++lookup_queries;
	if (name.Empty()) return LookupResult();
	if (name.Size() == 1)
		return name.global ? LookupGraph(GlobalScope(), name[0], kind) :
			LookupUnqualified(current, name[0], kind);
	LookupResult carrier = name.global ?
		LookupGraph(GlobalScope(), name[0], LOOKUP_SCOPE_CARRIER) :
		LookupUnqualified(current, name[0], LOOKUP_SCOPE_CARRIER);
	ScopeId owner = CarrierScope(carrier);
	if (owner == kNoScope) return LookupResult();
	for (std::size_t i = 1; i + 1 < name.Size(); ++i)
	{
		carrier = LookupGraph(owner, name[i], LOOKUP_SCOPE_CARRIER);
		owner = CarrierScope(carrier);
		if (owner == kNoScope) return LookupResult();
	}
	return LookupGraph(owner, name.Last(), kind);
}

LookupResult Program::LookupName(ScopeId current, NameId name,
	LookupKind kind)
{
	++lookup_queries;
	return LookupUnqualified(current, name, kind);
}

LookupResult Program::LookupDirect(ScopeId scope, NameId name,
	LookupKind kind)
{
	++lookup_queries;
	return DirectLookup(scope, name, kind);
}

LookupResult Program::LookupMember(EntityId entity, NameId name,
	LookupKind kind)
{
	++lookup_queries;
	if (entity == kNoEntity || entity >= entities.size() ||
		entities[entity].member_scope == kNoScope) return LookupResult();
	return LookupGraph(entities[entity].member_scope, name, kind);
}

LookupResult Program::LookupQualified(ScopeId owner, const NamePath& name,
	LookupKind kind)
{
	++lookup_queries;
	if (name.Empty() || owner == kNoScope) return LookupResult();
	for (std::size_t i = 0; i + 1 < name.Size(); ++i)
	{
		const LookupResult carrier =
			LookupGraph(owner, name[i], LOOKUP_SCOPE_CARRIER);
		owner = CarrierScope(carrier);
		if (owner == kNoScope) return LookupResult();
	}
	return LookupGraph(owner, name.Last(), kind);
}

ScopeId Program::ResolveScope(ScopeId current, const NamePath& name)
{
	return CarrierScope(Lookup(current, name, LOOKUP_SCOPE_CARRIER));
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
	case FUND_NULLPTR_T: return 8;
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
		case TYPE_MEMBER_POINTER: size = 8; break;
		case TYPE_NAMED:
		{
			const EntityRecord& entity = entities[record.entity];
			if (!entity.complete)
				throw std::runtime_error("incomplete named type");
			if (entity.flavor == NAMED_ENUM || entity.flavor == NAMED_ENUM_CLASS)
				size = SizeOf(entity.underlying);
			else
			{
				if (!entity.layout_complete || entity.object_size == 0)
					throw std::runtime_error("class layout is incomplete");
				size = static_cast<std::size_t>(entity.object_size);
			}
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
	if (record->kind == TYPE_POINTER || record->kind == TYPE_LVALUE_REFERENCE ||
		record->kind == TYPE_RVALUE_REFERENCE ||
		record->kind == TYPE_MEMBER_POINTER) return 8;
	if (record->kind == TYPE_FUNDAMENTAL)
		return FundamentalSize(record->fundamental);
	if (record->kind == TYPE_NAMED)
	{
		const EntityRecord& entity = entities[record->entity];
		if (!entity.complete)
			throw std::runtime_error("incomplete named type");
		if (entity.flavor == NAMED_ENUM || entity.flavor == NAMED_ENUM_CLASS)
			return AlignOf(entity.underlying);
		if (!entity.layout_complete || entity.object_alignment == 0)
			throw std::runtime_error("class layout is incomplete");
		return static_cast<std::size_t>(entity.object_alignment);
	}
	throw std::runtime_error("invalid alignof operand type");
}

void Program::AppendType(std::string& output, TypeId type,
	std::size_t* rendered_type_nodes,
	std::size_t* stack_storage_bytes) const
{
	struct Task
	{
		TypeId type;
		const char* text;
		bool is_type;
		Task() : type(kNoType), text(0), is_type(false) {}
		Task(TypeId value, bool type_task)
			: type(value), text(0), is_type(type_task) {}
		explicit Task(const char* value)
			: type(kNoType), text(value), is_type(false) {}
	};
	SmallStack<Task, 8> tasks;
	tasks.Push(Task(type, true));
	while (!tasks.Empty())
	{
		const Task task = tasks.Back();
		tasks.Pop();
		if (!task.is_type)
		{
			output += task.text;
			continue;
		}
		if (rendered_type_nodes) ++*rendered_type_nodes;
		const TypeRecord& record = types.Get(task.type);
		switch (record.kind)
		{
		case TYPE_FUNDAMENTAL:
			output += FundamentalName(record.fundamental);
			break;
		case TYPE_NAMED:
		{
			const EntityRecord& entity = entities[record.entity];
			output += FlavorName(entity.flavor);
			output += ' ';
			output += names.Get(entity.name);
			break;
		}
		case TYPE_QUALIFIED:
			if ((record.cv & CV_CONST) != 0) output += "const ";
			if ((record.cv & CV_VOLATILE) != 0) output += "volatile ";
			tasks.Push(Task(record.child, true));
			break;
		case TYPE_POINTER:
			output += "pointer to ";
			tasks.Push(Task(record.child, true));
			break;
		case TYPE_LVALUE_REFERENCE:
			output += "lvalue-reference to ";
			tasks.Push(Task(record.child, true));
			break;
		case TYPE_RVALUE_REFERENCE:
			output += "rvalue-reference to ";
			tasks.Push(Task(record.child, true));
			break;
		case TYPE_ARRAY:
			output += "array of ";
			output += std::to_string(record.bound);
			output += ' ';
			tasks.Push(Task(record.child, true));
			break;
		case TYPE_FUNCTION:
		{
			output += "function of (";
			const TypeId* parameters = types.Parameters(task.type);
			tasks.Push(Task(record.child, true));
			tasks.Push(Task(FunctionReturnText(record.cv,
				record.ref_qualifier)));
			if (record.variadic) tasks.Push(Task("..."));
			for (std::size_t i = record.parameter_count; i != 0; --i)
			{
				if (i != record.parameter_count || record.variadic)
					tasks.Push(Task(", "));
				tasks.Push(Task(parameters[i - 1], true));
			}
			break;
		}
		case TYPE_MEMBER_POINTER:
			output += "member-pointer of ";
			tasks.Push(Task(record.child, true));
			tasks.Push(Task(" to "));
			tasks.Push(Task(static_cast<TypeId>(record.bound), true));
			break;
		case TYPE_INVALID:
			throw std::logic_error("cannot render invalid type");
		}
	}
	if (stack_storage_bytes)
		*stack_storage_bytes = std::max(*stack_storage_bytes,
			tasks.StorageBytes());
}

std::string Program::RenderType(TypeId type) const
{
	std::string result;
	result.reserve(64);
	AppendType(result, type, 0, 0);
	return result;
}

void Program::WriteType(std::ostream& output, TypeId type,
	std::size_t* rendered_type_nodes,
	std::size_t* stack_storage_bytes) const
{
	std::string rendered;
	rendered.reserve(64);
	AppendType(rendered, type, rendered_type_nodes, stack_storage_bytes);
	output << rendered;
}

void Program::WriteScope(std::ostream& output, ScopeId scope,
	std::size_t depth, std::size_t* max_depth,
	std::size_t* stack_storage_bytes,
	std::size_t* rendered_type_nodes) const

{
	struct Frame
	{
		ScopeId scope;
		std::uint32_t edge;
		std::size_t depth;
		bool entered;
		Frame()
			: scope(kNoScope),
			  edge(std::numeric_limits<std::uint32_t>::max()),
			  depth(0), entered(false) {}
		Frame(ScopeId scope_value, std::size_t depth_value)
			: scope(scope_value),
			  edge(std::numeric_limits<std::uint32_t>::max()),
			  depth(depth_value), entered(false) {}
	};
	SmallStack<Frame, 8> stack;
	stack.Push(Frame(scope, depth));
	while (!stack.Empty())
	{
		Frame& frame = stack.Back();
		const ScopeRecord& record = scopes_[frame.scope];
		if (!frame.entered)
		{
			if (max_depth) *max_depth = std::max(*max_depth, frame.depth);
			for (std::size_t i = 0; i < frame.depth; ++i) output << "  ";
			output << "scope ";
			switch (record.kind)
			{
			case SCOPE_NAMESPACE:
				output << "namespace " << names.Get(record.name); break;
			case SCOPE_TEMPLATE_PARAMETERS:
				output << "template-parameters"; break;
			case SCOPE_CLASS:
				output << "class " << names.Get(record.name); break;
			case SCOPE_ENUM:
				output << "enum " << names.Get(record.name); break;
			case SCOPE_FUNCTION:
				output << "function " << names.Get(record.name); break;
			case SCOPE_BLOCK: output << "block"; break;
			}
			output << '\n';
			for (BindingId binding = record.first_binding;
				binding != kNoBinding; binding = bindings[binding].next)
			{
				const BindingRecord& item = bindings[binding];
				for (std::size_t i = 0; i < frame.depth + 1; ++i)
					output << "  ";
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
				if (item.kind == BIND_TYPE &&
					item.display_flavor != NAMED_NONE)
					output << FlavorName(item.display_flavor) << ' ' <<
						names.Get(item.name);
				else if (item.display_type_name != 0)
					output << FlavorName(item.display_flavor) << ' ' <<
						names.Get(item.display_type_name);
				else
				{
					std::size_t type_stack_storage = 0;
					WriteType(output, item.type, rendered_type_nodes,
						&type_stack_storage);
					if (stack_storage_bytes)
						*stack_storage_bytes = std::max(*stack_storage_bytes,
							stack.StorageBytes() + type_stack_storage);
				}
				if (item.kind == BIND_ENUMERATOR) output << ' ' << item.value;
				output << '\n';
			}
			frame.entered = true;
			frame.edge = record.first_child;
		}
		if (frame.edge == std::numeric_limits<std::uint32_t>::max())
		{
			stack.Pop();
			continue;
		}
		const std::uint32_t edge = frame.edge;
		frame.edge = child_edges_[edge].next;
		const std::size_t child_depth = frame.depth + 1;
		stack.Push(Frame(child_edges_[edge].child, child_depth));
	}
	if (stack_storage_bytes)
		*stack_storage_bytes = std::max(*stack_storage_bytes,
			stack.StorageBytes());
}

void Program::Render(std::ostream& output, std::size_t* max_depth,
	std::size_t* stack_storage_bytes,
	std::size_t* rendered_type_nodes) const
{
	if (max_depth) *max_depth = 0;
	if (stack_storage_bytes) *stack_storage_bytes = 0;
	if (rendered_type_nodes) *rendered_type_nodes = 0;
	output << "translation-unit\n";
	WriteScope(output, GlobalScope(), 1, max_depth, stack_storage_bytes,
		rendered_type_nodes);
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
		using_edge_slots_.capacity() * sizeof(std::uint32_t) +
		entries_.capacity() * sizeof(NameEntry) +
		entry_slots_.capacity() * sizeof(std::uint32_t) +
		lookup_marks_.capacity() * sizeof(std::uint32_t) +
		lookup_worklist_.capacity() * sizeof(ScopeId) +
		lookup_dependency_marks_.capacity() * sizeof(std::uint32_t) +
		lookup_dependencies_.capacity() * sizeof(ScopeId) +
		lookup_cache_->StorageBytes() +
		entities.capacity() * sizeof(EntityRecord) +
		bindings.capacity() * sizeof(BindingRecord);
}

}
}
