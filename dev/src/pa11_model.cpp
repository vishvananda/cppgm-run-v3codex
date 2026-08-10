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
	return UseInterned(strings_.InternRange(spelling, first, count));
}

NameId NameTable::UseInterned(NameId name)
{
	(void)strings_.Get(name);
	if (used_.size() <= name)
		used_.resize(static_cast<std::size_t>(name) + 1, 0);
	if (used_[name] == 0)
	{
		used_[name] = 1;
		++size_;
	}
	return name;
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
	  dependent_bound_type(kNoType),
	  dependent_bound_parameter(kNoTemplateParameter),
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

TypeId TypeTable::TryQualify(TypeId type, std::uint8_t cv)
{
	if (cv == CV_NONE) return type;
	const TypeRecord& record = Get(type);
	if (record.kind == TYPE_ARRAY)
	{
		const TypeId qualified_child = TryQualify(record.child, cv);
		if (qualified_child == kNoType) return kNoType;
		return record.dependent_bound_parameter == kNoTemplateParameter ?
			TryArray(qualified_child, record.bound) :
			TryDependentArray(qualified_child,
				record.dependent_bound_type,
				record.dependent_bound_parameter);
	}
	if (record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE) return type;
	if (record.kind == TYPE_FUNCTION) return kNoType;
	if (record.kind == TYPE_QUALIFIED)
		return TryQualify(record.child,
			static_cast<std::uint8_t>(record.cv | cv));
	TypeRecord candidate;
	candidate.kind = TYPE_QUALIFIED;
	candidate.child = type;
	candidate.cv = cv;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::Qualify(TypeId type, std::uint8_t cv)
{
	const TypeId result = TryQualify(type, cv);
	if (result == kNoType)
		throw std::runtime_error("cv-qualified function type");
	return result;
}

TypeId TypeTable::TryPointer(TypeId type)
{
	if (IsReference(type)) return kNoType;
	return Unary(TYPE_POINTER, type);
}

TypeId TypeTable::Pointer(TypeId type)
{
	const TypeId result = TryPointer(type);
	if (result == kNoType)
		throw std::runtime_error("pointer to reference type");
	return result;
}

TypeId TypeTable::TryMemberPointer(TypeId owner, TypeId member)
{
	owner = RemoveTopCv(owner);
	const TypeRecord& class_type = Get(owner);
	if (class_type.kind != TYPE_NAMED) return kNoType;
	TypeRecord candidate;
	candidate.kind = TYPE_MEMBER_POINTER;
	candidate.child = member;
	candidate.entity = class_type.entity;
	candidate.bound = owner;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::MemberPointer(TypeId owner, TypeId member)
{
	const TypeId result = TryMemberPointer(owner, member);
	if (result == kNoType)
		throw std::runtime_error("member pointer owner is not a class");
	return result;
}

TypeId TypeTable::TryReference(TypeKind kind, TypeId type)
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
		return kNoType;
	return Unary(kind, type);
}

TypeId TypeTable::Reference(TypeKind kind, TypeId type)
{
	const TypeId result = TryReference(kind, type);
	if (result == kNoType)
		throw std::runtime_error("reference to void type");
	return result;
}

TypeId TypeTable::TryArray(TypeId type, std::uint64_t bound)
{
	const TypeRecord& record = Get(type);
	if (record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE || record.kind == TYPE_FUNCTION ||
		(record.kind == TYPE_FUNDAMENTAL &&
		 record.fundamental == FUND_VOID))
		return kNoType;
	TypeRecord candidate;
	candidate.kind = TYPE_ARRAY;
	candidate.child = type;
	candidate.bound = bound;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::Array(TypeId type, std::uint64_t bound)
{
	const TypeId result = TryArray(type, bound);
	if (result == kNoType)
		throw std::runtime_error("invalid array element type");
	return result;
}

TypeId TypeTable::TryDependentArray(TypeId type, TypeId bound_type,
	std::uint32_t parameter)
{
	if (parameter == kNoTemplateParameter)
		throw std::logic_error("dependent array has no parameter");
	const TypeRecord& record = Get(type);
	if (record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE || record.kind == TYPE_FUNCTION ||
		(record.kind == TYPE_FUNDAMENTAL &&
		 record.fundamental == FUND_VOID))
		return kNoType;
	TypeRecord candidate;
	candidate.kind = TYPE_ARRAY;
	candidate.child = type;
	candidate.dependent_bound_type = bound_type;
	candidate.dependent_bound_parameter = parameter;
	return Intern(candidate, 0, 0);
}

TypeId TypeTable::DependentArray(TypeId type, TypeId bound_type,
	std::uint32_t parameter)
{
	const TypeId result = TryDependentArray(type, bound_type, parameter);
	if (result == kNoType)
		throw std::runtime_error("invalid dependent array element type");
	return result;
}

TypeId TypeTable::TryFunction(TypeId result,
	const std::vector<TypeId>& parameters, bool variadic, std::uint8_t cv,
	std::uint8_t ref_qualifier)
{
	const TypeRecord& returned = Get(result);
	if (returned.kind == TYPE_ARRAY || returned.kind == TYPE_FUNCTION)
		return kNoType;
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

TypeId TypeTable::Function(TypeId result,
	const std::vector<TypeId>& parameters, bool variadic, std::uint8_t cv,
	std::uint8_t ref_qualifier)
{
	const TypeId type = TryFunction(
		result, parameters, variadic, cv, ref_qualifier);
	if (type == kNoType)
		throw std::runtime_error("invalid function return type");
	return type;
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
	hash = MixHash(hash, record.dependent_bound_type);
	hash = MixHash(hash, record.dependent_bound_parameter);
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
		existing.dependent_bound_type != candidate.dependent_bound_type ||
		existing.dependent_bound_parameter !=
			candidate.dependent_bound_parameter ||
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
	  local_context(kNoBinding),
	  template_argument_list(kNoTemplateArgumentList),
	  template_argument_begin(kNoBinding), template_argument_count(0),
	  template_argument_pack_begin(kNoTemplateParameter),
	  direct_base_begin(0), direct_base_count(0),
	  flavor(NAMED_NONE), type(kNoType),
	  underlying(kNoType), declaration(kNoBinding),
	  union_default_member(kNoBinding), object_size(0),
	  object_alignment(0), natural_alignment(0), requested_alignment(0),
	  packing_alignment(0), direct_base_offset(0),
	  base_access(ACCESS_PUBLIC), complete(false),
	  layout_complete(false),
	  has_user_declared_constructor(false),
	  has_user_provided_constructor(false), default_constructible(false),
	  trivial_default_constructor(false), has_user_declared_destructor(false),
	  destructible(true), trivial_destructor(true), has_direct_base(false),
	  is_aggregate(false), empty_class(false), indirect_class_value_abi(false),
	  polymorphic_class(false), abstract_class(false),
	  nonlinear_base_graph(false),
	  deferred_template_completion(false), lambda_closure(false),
	  lambda_ordinal(0)
{
}

BindingRecord::BindingRecord()
	: owner(kNoScope), name(0), qualified_name(0), kind(BIND_VARIABLE), type(kNoType),
	  conversion_target(kNoType),
	  next(kNoBinding), member_owner(kNoEntity), access_owner(kNoEntity),
	  member_offset(0), requested_alignment(0), bit_offset(0), bit_width(0),
	  bit_storage_bits(0),
	  overload_ordinal(0), member_ordinal(kNoBinding),
	  template_argument_list(kNoTemplateArgumentList),
	  template_argument_begin(0), template_argument_count(0),
	  display_flavor(NAMED_NONE), display_type_name(0),
		  canonical(kNoBinding), value(0), operator_kind(OPERATOR_NONE),
		  builtin_function(BUILTIN_FUNCTION_NONE),
		  operator_literal_suffix(0), language_linkage(LANGUAGE_LINKAGE_CPP),
	  storage_class(STORAGE_CLASS_NONE), access(ACCESS_PUBLIC),
	  constant(false), nonthrowing(false), unnamed_namespace_linkage(false),
	  thread_local_storage(false),
	  non_static_data_member(false), mutable_member(false), bit_field(false),
	  anonymous_union_storage(false),
	  static_member_function(false),
	  has_default_member_initializer(false), conversion_function(false),
	  constructor(false),
	  constructor_base_entry(false),
	  destructor(false), destructor_base_entry(false), inline_function(false),
	  virtual_function(false), pure_virtual(false), final_virtual(false),
	  override_specifier(false), weak_odr(false), object_output_root(false),
	  emission_demanded(false), explicit_instantiation_suppressed(false),
	  force_indirect_class_result_abi(false),
	  closure_template_specialization(false)
{
}

LookupResult::LookupResult()
	: name_space(kNoScope), type(kNoType), type_declaration(kNoBinding),
	  type_declaration_canonical(kNoBinding),
	  ordinary(kNoBinding), ordinary_declaration(kNoBinding),
	  naming_class(kNoEntity), extra_ordinary_inline_(),
	  extra_ordinary_overflow_(), extra_ordinary_count_(0),
	  function_template_owner_(kNoScope),
	  extra_function_template_owner_inline_(),
	  extra_function_template_owner_overflow_(),
	  extra_function_template_owner_count_(0),
	  function_template_lookup_(false)
{
}

bool LookupResult::Empty() const
{
	return name_space == kNoScope && type == kNoType && ordinary == kNoBinding &&
		!function_template_lookup_;
}

std::size_t LookupResult::OrdinaryCount() const
{
	return ordinary == kNoBinding ? 0 : extra_ordinary_count_ + 1;
}

BindingId LookupResult::OrdinaryAt(std::size_t index) const
{
	if (index == 0) return ordinary;
	--index;
	if (index >= extra_ordinary_count_)
		throw std::logic_error("ordinary lookup candidate index is out of range");
	return extra_ordinary_count_ <= 2 ? extra_ordinary_inline_[index] :
		extra_ordinary_overflow_[index];
}

void LookupResult::AddOrdinary(BindingId binding)
{
	if (binding == kNoBinding)
		throw std::logic_error("ordinary lookup candidate has no identity");
	if (ordinary == kNoBinding)
	{
		ordinary = binding;
		return;
	}
	if (extra_ordinary_count_ < 2 && extra_ordinary_overflow_.empty())
		extra_ordinary_inline_[extra_ordinary_count_] = binding;
	else
	{
		if (extra_ordinary_overflow_.empty())
		{
			extra_ordinary_overflow_.reserve(4);
			extra_ordinary_overflow_.insert(extra_ordinary_overflow_.end(),
				extra_ordinary_inline_, extra_ordinary_inline_ + 2);
		}
		extra_ordinary_overflow_.push_back(binding);
	}
	++extra_ordinary_count_;
}

bool LookupResult::HasFunctionTemplateLookup() const
{
	return function_template_lookup_;
}

void LookupResult::BeginFunctionTemplateLookup()
{
	function_template_lookup_ = true;
}

std::size_t LookupResult::FunctionTemplateOwnerCount() const
{
	return function_template_owner_ == kNoScope ? 0 :
		extra_function_template_owner_count_ + 1;
}

ScopeId LookupResult::FunctionTemplateOwnerAt(std::size_t index) const
{
	if (index == 0 && function_template_owner_ != kNoScope)
		return function_template_owner_;
	if (function_template_owner_ == kNoScope || --index >=
		extra_function_template_owner_count_)
		throw std::logic_error(
			"function-template owner index is out of range");
	return extra_function_template_owner_count_ <= 2 ?
		extra_function_template_owner_inline_[index] :
		extra_function_template_owner_overflow_[index];
}

void LookupResult::AddFunctionTemplateOwner(ScopeId owner)
{
	if (owner == kNoScope)
		throw std::logic_error("function-template lookup owner is missing");
	function_template_lookup_ = true;
	for (std::size_t i = 0; i < FunctionTemplateOwnerCount(); ++i)
		if (FunctionTemplateOwnerAt(i) == owner) return;
	if (function_template_owner_ == kNoScope)
	{
		function_template_owner_ = owner;
		return;
	}
	if (extra_function_template_owner_count_ < 2 &&
		extra_function_template_owner_overflow_.empty())
		extra_function_template_owner_inline_[
			extra_function_template_owner_count_] = owner;
	else
	{
		if (extra_function_template_owner_overflow_.empty())
		{
			extra_function_template_owner_overflow_.reserve(4);
			extra_function_template_owner_overflow_.insert(
				extra_function_template_owner_overflow_.end(),
				extra_function_template_owner_inline_,
				extra_function_template_owner_inline_ + 2);
		}
		extra_function_template_owner_overflow_.push_back(owner);
	}
	++extra_function_template_owner_count_;
}

std::size_t LookupResult::DynamicStorageBytes() const
{
	return extra_ordinary_overflow_.capacity() * sizeof(BindingId) +
		extra_function_template_owner_overflow_.capacity() * sizeof(ScopeId);
}

struct Program::ScopeRecord
{
	ScopeId parent;
	ScopeKind kind;
	NameId name, emission_name;
	EntityId entity;
	std::uint32_t depth;
	BindingId first_binding;
	BindingId last_binding;
	std::uint32_t first_child;
	std::uint32_t last_child;
	std::uint32_t first_incoming_using;
	std::uint32_t first_visible_name;
	bool inline_namespace;

	ScopeRecord()
		: parent(kNoScope), kind(SCOPE_NAMESPACE), name(0), emission_name(0),
		  entity(kNoEntity), depth(0),
		  first_binding(kNoBinding), last_binding(kNoBinding),
		  first_child(std::numeric_limits<std::uint32_t>::max()),
		  last_child(std::numeric_limits<std::uint32_t>::max()),
		  first_incoming_using(std::numeric_limits<std::uint32_t>::max()),
		  first_visible_name(std::numeric_limits<std::uint32_t>::max()),
		  inline_namespace(false) {}
};

struct Program::NameEntry
{
	ScopeId scope;
	NameId name;
	ScopeId name_space;
	TypeId type;
	BindingId type_declaration;
	BindingId ordinary;
	bool function_template;

	NameEntry()
		: scope(kNoScope), name(0), name_space(kNoScope), type(kNoType),
		  type_declaration(kNoBinding), ordinary(kNoBinding),
		  function_template(false) {}
};

struct Program::UsingEdge
{
	ScopeId owner;
	ScopeId target;
	ScopeId injection;
	std::uint32_t next_incoming;
	UsingEdge(ScopeId owner_value, ScopeId target_value,
		ScopeId injection_value,
		std::uint32_t next_incoming_value)
		: owner(owner_value), target(target_value), injection(injection_value),
		  next_incoming(next_incoming_value) {}
};

struct Program::ScopeVisibleName
{
	ScopeId scope;
	NameId name;
	std::uint32_t first_relation;
	std::uint32_t next_in_scope;
	ScopeVisibleName(ScopeId scope_value, NameId name_value,
		std::uint32_t next_value)
		: scope(scope_value), name(name_value),
		  first_relation(std::numeric_limits<std::uint32_t>::max()),
		  next_in_scope(next_value) {}
};

struct Program::UsingNameRelation
{
	std::uint32_t edge;
	NameId name;
	std::uint32_t next;
	UsingNameRelation(std::uint32_t edge_value, NameId name_value,
		std::uint32_t next_value)
		: edge(edge_value), name(name_value), next(next_value) {}
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

struct Program::TemplateArgumentListRecord
{
	std::uint32_t first, count;
	std::size_t hash;

	TemplateArgumentListRecord(std::uint32_t first_value,
		std::uint32_t count_value, std::size_t hash_value)
		: first(first_value), count(count_value), hash(hash_value) {}
};

Program::Program(InternedStringTable& strings)
	: names(strings), lookup_queries(0), lookup_scope_visits(0),
	  lookup_edge_visits(0), lookup_cache_hits(0), lookup_cache_misses(0),
	  lookup_cache_invalidations(0), lookup_cache_dependency_edges(0),
	  lookup_cache_invalidation_pushes(0),
	  virtual_base_path_visits(0),
	  name_index_probes(0), using_index_probes(0),
	  template_argument_list_requests(0),
	  template_argument_list_cache_hits(0),
	  template_argument_list_index_probes(0),
	  using_edge_slots_(64, 0), visible_name_slots_(64, 0),
	  using_name_relation_slots_(64, 0),
	  using_name_invalidation_generation_(0), entry_slots_(64, 0),
	  template_argument_list_slots_(32, 0),
	  lookup_generation_(1),
	  lookup_dependency_generation_(0), lookup_pending_generation_(0),
	  collecting_lookup_dependencies_(false),
	  lookup_cache_(new LookupCache())
{
	NewScope(kNoScope, SCOPE_NAMESPACE, names.Intern("<global>"));
}

void Program::RehashTemplateArgumentLists(std::size_t capacity)
{
	std::vector<std::uint32_t> replacement(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < template_argument_lists_.size(); ++i)
	{
		std::size_t slot = template_argument_lists_[i].hash & mask;
		while (replacement[slot] != 0) slot = (slot + 1) & mask;
		replacement[slot] = static_cast<std::uint32_t>(i + 1);
	}
	template_argument_list_slots_.swap(replacement);
}

TemplateArgumentListId Program::InternTemplateArgumentList(
	const std::vector<TemplateArgument>& arguments,
	std::uint32_t* first, std::uint32_t* count)
{
	++template_argument_list_requests;
	std::size_t hash = MixHash(0, arguments.size());
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		hash = MixHash(hash, arguments[i].kind);
		hash = MixHash(hash, arguments[i].type);
		hash = MixHash(hash, static_cast<std::uint64_t>(arguments[i].value));
		hash = MixHash(hash, arguments[i].value_binding);
		hash = MixHash(hash, arguments[i].dependent_parameter);
		hash = MixHash(hash, arguments[i].pack_expansion ? 1 : 0);
	}
	if ((template_argument_lists_.size() + 1) * 10 >
		template_argument_list_slots_.size() * 7)
		RehashTemplateArgumentLists(template_argument_list_slots_.size() * 2);
	const std::size_t mask = template_argument_list_slots_.size() - 1;
	std::size_t slot = hash & mask;
	while (template_argument_list_slots_[slot] != 0)
	{
		++template_argument_list_index_probes;
		const TemplateArgumentListId id =
			template_argument_list_slots_[slot] - 1;
		const TemplateArgumentListRecord& record =
			template_argument_lists_[id];
		bool equal = record.hash == hash && record.count == arguments.size();
		for (std::size_t i = 0; equal && i < arguments.size(); ++i)
			equal = canonical_template_arguments[record.first + i] ==
				arguments[i];
		if (equal)
		{
			++template_argument_list_cache_hits;
			if (first) *first = record.first;
			if (count) *count = record.count;
			return id;
		}
		slot = (slot + 1) & mask;
	}
	++template_argument_list_index_probes;
	if (template_argument_lists_.size() >= kNoTemplateArgumentList ||
		arguments.size() > std::numeric_limits<std::uint32_t>::max() ||
		template_arguments.size() >
			std::numeric_limits<std::uint32_t>::max() - arguments.size())
		throw std::runtime_error("too many canonical template argument lists");
	if (template_arguments.size() != canonical_template_arguments.size())
		throw std::logic_error("canonical template argument storage diverged");
	const std::uint32_t range_first =
		static_cast<std::uint32_t>(template_arguments.size());
	const std::uint32_t range_count =
		static_cast<std::uint32_t>(arguments.size());
	for (std::size_t i = 0; i < arguments.size(); ++i)
		template_arguments.push_back(arguments[i].type);
	canonical_template_arguments.insert(canonical_template_arguments.end(),
		arguments.begin(), arguments.end());
	const TemplateArgumentListId id = static_cast<TemplateArgumentListId>(
		template_argument_lists_.size());
	template_argument_lists_.push_back(TemplateArgumentListRecord(
		range_first, range_count, hash));
	template_argument_list_slots_[slot] = id + 1;
	if (first) *first = range_first;
	if (count) *count = range_count;
	return id;
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
	record.depth = parent == kNoScope ? 0 : scopes_[parent].depth + 1;
	lookup_marks_.push_back(0);
	lookup_dependency_marks_.push_back(0);
	using_name_invalidation_marks_.push_back(0);
	lookup_pending_heads_.push_back(std::numeric_limits<std::uint32_t>::max());
	lookup_pending_head_marks_.push_back(0);
	lookup_pending_target_marks_.push_back(0);
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
	if (is_inline)
	{
		scopes_[entry->name_space].inline_namespace = true;
		AddUsingEdge(parent, entry->name_space);
	}
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
	ScopeId owner_namespace = owner;
	while (owner_namespace != kNoScope &&
		scopes_[owner_namespace].kind != SCOPE_NAMESPACE)
		owner_namespace = scopes_[owner_namespace].parent;
	if (owner_namespace == kNoScope || target >= scopes_.size() ||
		scopes_[target].kind != SCOPE_NAMESPACE)
		throw std::logic_error("using edge has no namespace injection scope");
	ScopeId left = owner_namespace;
	ScopeId right = target;
	while (scopes_[left].depth > scopes_[right].depth)
		left = scopes_[left].parent;
	while (scopes_[right].depth > scopes_[left].depth)
		right = scopes_[right].parent;
	while (left != right)
	{
		left = scopes_[left].parent;
		right = scopes_[right].parent;
	}
	const ScopeId injection = left;
	const std::uint32_t edge =
		static_cast<std::uint32_t>(using_edges_.size());
	using_edges_.push_back(UsingEdge(owner, target, injection,
		scopes_[target].first_incoming_using));
	using_edge_slots_[slot] = edge + 1;
	scopes_[target].first_incoming_using = edge;
	for (std::uint32_t visible = scopes_[target].first_visible_name;
		visible != std::numeric_limits<std::uint32_t>::max();
		visible = visible_names_[visible].next_in_scope)
	{
		const NameId visible_name = visible_names_[visible].name;
		bool owner_became_visible = false;
		if (!AddUsingNameRelation(
			edge, visible_name, &owner_became_visible)) continue;
		if (owner_became_visible) PropagateUsingName(owner, visible_name);
		InvalidateLookupName(owner, visible_name);
	}
}

void Program::PublishFunctionTemplateName(ScopeId owner, NameId name)
{
	NameEntry* entry = EnsureEntry(owner, name);
	if (entry->function_template) return;
	entry->function_template = true;
	InvalidateLookupName(owner, name);
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

void Program::RehashVisibleNames(std::size_t capacity)
{
	visible_name_slots_.assign(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < visible_names_.size(); ++i)
	{
		const ScopeVisibleName& fact = visible_names_[i];
		std::size_t slot = MixHash(fact.scope, fact.name) & mask;
		while (visible_name_slots_[slot] != 0) slot = (slot + 1) & mask;
		visible_name_slots_[slot] = static_cast<std::uint32_t>(i + 1);
	}
}

std::uint32_t Program::FindVisibleName(ScopeId scope, NameId name) const
{
	const std::size_t mask = visible_name_slots_.size() - 1;
	std::size_t slot = MixHash(scope, name) & mask;
	while (visible_name_slots_[slot] != 0)
	{
		const std::uint32_t fact = visible_name_slots_[slot] - 1;
		if (visible_names_[fact].scope == scope &&
			visible_names_[fact].name == name)
			return fact;
		slot = (slot + 1) & mask;
	}
	return std::numeric_limits<std::uint32_t>::max();
}

std::uint32_t Program::EnsureVisibleName(ScopeId scope, NameId name,
	bool* created)
{
	if ((visible_names_.size() + 1) * 10 > visible_name_slots_.size() * 7)
		RehashVisibleNames(visible_name_slots_.size() * 2);
	const std::size_t mask = visible_name_slots_.size() - 1;
	std::size_t slot = MixHash(scope, name) & mask;
	while (visible_name_slots_[slot] != 0)
	{
		const std::uint32_t fact = visible_name_slots_[slot] - 1;
		if (visible_names_[fact].scope == scope &&
			visible_names_[fact].name == name)
		{
			*created = false;
			return fact;
		}
		slot = (slot + 1) & mask;
	}
	if (visible_names_.size() >= std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many visible scope names");
	const std::uint32_t fact =
		static_cast<std::uint32_t>(visible_names_.size());
	visible_names_.push_back(ScopeVisibleName(
		scope, name, scopes_[scope].first_visible_name));
	scopes_[scope].first_visible_name = fact;
	visible_name_slots_[slot] = fact + 1;
	*created = true;
	return fact;
}

void Program::RehashUsingNameRelations(std::size_t capacity)
{
	using_name_relation_slots_.assign(capacity, 0);
	const std::size_t mask = capacity - 1;
	for (std::size_t i = 0; i < using_name_relations_.size(); ++i)
	{
		const UsingNameRelation& relation = using_name_relations_[i];
		std::size_t slot = MixHash(relation.edge, relation.name) & mask;
		while (using_name_relation_slots_[slot] != 0)
			slot = (slot + 1) & mask;
		using_name_relation_slots_[slot] = static_cast<std::uint32_t>(i + 1);
	}
}

std::uint32_t Program::FindUsingNameRelation(std::uint32_t edge,
	NameId name) const
{
	const std::size_t mask = using_name_relation_slots_.size() - 1;
	std::size_t slot = MixHash(edge, name) & mask;
	while (using_name_relation_slots_[slot] != 0)
	{
		const std::uint32_t relation =
			using_name_relation_slots_[slot] - 1;
		if (using_name_relations_[relation].edge == edge &&
			using_name_relations_[relation].name == name)
			return relation;
		slot = (slot + 1) & mask;
	}
	return std::numeric_limits<std::uint32_t>::max();
}

bool Program::AddUsingNameRelation(std::uint32_t edge, NameId name,
	bool* owner_became_visible)
{
	if (FindUsingNameRelation(edge, name) !=
		std::numeric_limits<std::uint32_t>::max())
	{
		*owner_became_visible = false;
		return false;
	}
	if ((using_name_relations_.size() + 1) * 10 >
		using_name_relation_slots_.size() * 7)
		RehashUsingNameRelations(using_name_relation_slots_.size() * 2);
	const ScopeId owner = using_edges_[edge].owner;
	const std::uint32_t visible =
		EnsureVisibleName(owner, name, owner_became_visible);
	if (using_name_relations_.size() >=
		std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many indexed using names");
	const std::uint32_t relation =
		static_cast<std::uint32_t>(using_name_relations_.size());
	using_name_relations_.push_back(UsingNameRelation(
		edge, name, visible_names_[visible].first_relation));
	visible_names_[visible].first_relation = relation;
	const std::size_t mask = using_name_relation_slots_.size() - 1;
	std::size_t slot = MixHash(edge, name) & mask;
	while (using_name_relation_slots_[slot] != 0) slot = (slot + 1) & mask;
	using_name_relation_slots_[slot] = relation + 1;
	return true;
}

void Program::PropagateUsingName(ScopeId scope, NameId name)
{
	using_name_worklist_.clear();
	using_name_worklist_.push_back(scope);
	for (std::size_t i = 0; i < using_name_worklist_.size(); ++i)
	{
		const ScopeId target = using_name_worklist_[i];
		for (std::uint32_t edge = scopes_[target].first_incoming_using;
			edge != std::numeric_limits<std::uint32_t>::max();
			edge = using_edges_[edge].next_incoming)
		{
			bool owner_became_visible = false;
			if (AddUsingNameRelation(edge, name, &owner_became_visible) &&
				owner_became_visible)
				using_name_worklist_.push_back(using_edges_[edge].owner);
		}
	}
}

void Program::PublishUsingName(ScopeId scope, NameId name)
{
	bool created = false;
	(void)EnsureVisibleName(scope, name, &created);
	if (created) PropagateUsingName(scope, name);
}

EntityId Program::NewEntity(NameId name, NamedFlavor flavor, bool complete,
	TypeId underlying, ScopeId owner, NameId identity_name)
{
	if (entities.size() >= kNoEntity)
		throw std::runtime_error("too many PA11 entities");
	const EntityId entity = static_cast<EntityId>(entities.size());
	entities.push_back(EntityRecord());
	base_jump_offsets_.push_back(0);
	base_jump_counts_.push_back(0);
	base_depths_.push_back(0);
	deepest_nonpublic_base_depths_.push_back(0);
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

BindingId Program::AddUnindexedBinding(ScopeId owner, BindingKind kind,
	NameId name, TypeId type, BindingId canonical)
{
	if (bindings.size() >= kNoBinding)
		throw std::runtime_error("too many PA11 bindings");
	const BindingId binding = static_cast<BindingId>(bindings.size());
	bindings.push_back(BindingRecord());
	BindingRecord& record = bindings.back();
	record.owner = owner;
	record.kind = kind;
	record.name = name;
	record.type = type;
	record.canonical = canonical == kNoBinding ? binding : canonical;
	ScopeRecord& scope = scopes_[owner];
	if (scope.first_binding == kNoBinding) scope.first_binding = binding;
	else bindings[scope.last_binding].next = binding;
	scope.last_binding = binding;
	return binding;
}

bool Program::IsStaticDataMember(BindingId binding) const
{
	if (binding == kNoBinding || binding >= bindings.size()) return false;
	const BindingRecord& record = bindings[bindings[binding].canonical];
	return record.kind == BIND_VARIABLE && record.member_owner != kNoEntity &&
		!record.non_static_data_member;
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

void Program::ResetClassDefinition(EntityId entity)
{
	if (entity >= entities.size())
		throw std::logic_error("class reset entity is invalid");
	const EntityRecord old = entities[entity];
	if (old.member_scope != kNoScope)
	{
		if (old.member_scope >= scopes_.size())
			throw std::logic_error("class reset member scope is invalid");
		scopes_[old.member_scope].entity = kNoEntity;
		InvalidateLookupScope(old.member_scope);
	}
	EntityRecord reset;
	reset.name = old.name;
	reset.identity_name = old.identity_name;
	reset.owner = old.owner;
	reset.enclosing_class = old.enclosing_class;
	reset.local_context = old.local_context;
	reset.template_argument_list = old.template_argument_list;
	reset.template_argument_begin = old.template_argument_begin;
	reset.template_argument_count = old.template_argument_count;
	reset.template_argument_pack_begin = old.template_argument_pack_begin;
	reset.flavor = old.flavor;
	reset.type = old.type;
	reset.declaration = old.declaration;
	entities[entity] = reset;
	base_jump_offsets_[entity] = 0;
	base_jump_counts_[entity] = 0;
	base_depths_[entity] = 0;
	deepest_nonpublic_base_depths_[entity] = 0;
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

bool Program::IsInlineNamespace(ScopeId scope) const
{
	return scope < scopes_.size() && scopes_[scope].inline_namespace;
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
	std::vector<DirectBaseEdge> bases(1, DirectBaseEdge(base, access));
	SetDirectBases(derived, bases);
}

void Program::SetDirectBases(EntityId derived,
	const std::vector<DirectBaseEdge>& bases)
{
	if (derived >= entities.size())
		throw std::runtime_error("invalid direct base owner");
	EntityRecord& record = entities[derived];
	if (record.direct_base_count != 0)
		throw std::runtime_error("direct bases are already fixed");
	if (record.member_scope != kNoScope)
		throw std::logic_error(
			"direct bases must be fixed before publishing the member scope");
	for (std::size_t i = 0; i < bases.size(); ++i)
	{
		const EntityId base = bases[i].entity;
		if (base >= entities.size() || derived == base)
			throw std::runtime_error("invalid direct base relationship");
		for (std::size_t previous = 0; previous < i; ++previous)
			if (bases[previous].entity == base)
				throw std::runtime_error("duplicate direct base");
		if (IsBaseOf(derived, base))
			throw std::runtime_error("cyclic class inheritance");
		if (base_depths_[base] == std::numeric_limits<std::uint32_t>::max())
			throw std::runtime_error("class inheritance is too deep");
	}
	if (bases.size() > std::numeric_limits<std::uint32_t>::max() ||
		direct_bases.size() > std::numeric_limits<std::uint32_t>::max() -
			bases.size())
		throw std::runtime_error("too many direct base relationships");
	record.direct_base_begin = static_cast<std::uint32_t>(direct_bases.size());
	record.direct_base_count = static_cast<std::uint32_t>(bases.size());
	direct_bases.insert(direct_bases.end(), bases.begin(), bases.end());
	if (bases.empty()) return;
	record.direct_base = bases[0].entity;
	record.base_access = bases[0].access;
	record.has_direct_base = true;
	std::uint32_t maximum_depth = 0;
	std::uint32_t nonpublic_depth = 0;
	for (std::size_t i = 0; i < bases.size(); ++i)
	{
		maximum_depth = std::max(maximum_depth, base_depths_[bases[i].entity]);
		nonpublic_depth = std::max(nonpublic_depth,
			bases[i].access == ACCESS_PUBLIC ?
				deepest_nonpublic_base_depths_[bases[i].entity] :
				base_depths_[bases[i].entity] + 1);
		if (entities[bases[i].entity].nonlinear_base_graph)
			record.nonlinear_base_graph = true;
	}
	record.nonlinear_base_graph = record.nonlinear_base_graph || bases.size() != 1;
	base_depths_[derived] = maximum_depth + 1;
	deepest_nonpublic_base_depths_[derived] = nonpublic_depth;
	if (record.nonlinear_base_graph) return;
	std::uint32_t remaining_depth = base_depths_[derived];
	std::uint8_t jump_count = 0;
	while (remaining_depth != 0)
	{
		++jump_count;
		remaining_depth >>= 1;
	}
	base_jump_offsets_[derived] = base_jumps_.size();
	base_jump_counts_[derived] = jump_count;
	base_jumps_.insert(base_jumps_.end(), jump_count, kNoEntity);
	base_jumps_[base_jump_offsets_[derived]] = bases[0].entity;
	for (std::size_t level = 1; level < jump_count; ++level)
	{
		const EntityId previous =
			base_jumps_[base_jump_offsets_[derived] + level - 1];
		base_jumps_[base_jump_offsets_[derived] + level] =
			previous == kNoEntity || base_jump_counts_[previous] < level ?
			kNoEntity : base_jumps_[base_jump_offsets_[previous] + level - 1];
	}
}

const DirectBaseEdge& Program::DirectBase(EntityId derived,
	std::size_t ordinal) const
{
	if (derived >= entities.size() || ordinal >= entities[derived].direct_base_count)
		throw std::logic_error("invalid direct base edge query");
	return direct_bases[entities[derived].direct_base_begin + ordinal];
}

DirectBaseEdge& Program::MutableDirectBase(EntityId derived,
	std::size_t ordinal)
{
	if (derived >= entities.size() || ordinal >= entities[derived].direct_base_count)
		throw std::logic_error("invalid direct base edge mutation");
	return direct_bases[entities[derived].direct_base_begin + ordinal];
}

bool Program::IsBaseOf(EntityId base, EntityId derived) const
{
	return QueryBasePath(derived, base, 0, 0);
}

bool Program::QueryBasePath(EntityId derived, EntityId base,
	std::size_t* distance, bool* all_public) const
{
	if (base == kNoEntity || derived == kNoEntity ||
		base >= entities.size() || derived >= entities.size()) return false;
	if (entities[derived].nonlinear_base_graph)
	{
		struct PendingBase
		{
			EntityId entity;
			std::size_t distance;
			bool all_public;
			PendingBase(EntityId entity_, std::size_t distance_, bool public_)
				: entity(entity_), distance(distance_), all_public(public_) {}
		};
		std::vector<PendingBase> pending;
		pending.push_back(PendingBase(derived, 0, true));
		while (!pending.empty())
		{
			const PendingBase current = pending.back();
			pending.pop_back();
			if (current.entity == base)
			{
				if (distance) *distance = current.distance;
				if (all_public) *all_public = current.all_public;
				return true;
			}
			const EntityRecord& current_record = entities[current.entity];
			for (std::size_t i = current_record.direct_base_count; i != 0; --i)
			{
				const DirectBaseEdge& edge = DirectBase(current.entity, i - 1);
				pending.push_back(PendingBase(edge.entity,
					current.distance + 1,
					current.all_public && edge.access == ACCESS_PUBLIC));
			}
		}
		return false;
	}
	if (base_depths_[derived] < base_depths_[base]) return false;
	const std::uint32_t difference =
		base_depths_[derived] - base_depths_[base];
	EntityId current = derived;
	std::uint32_t remaining = difference;
	for (std::size_t level = 0; remaining != 0 && current != kNoEntity;
		++level, remaining >>= 1)
		if ((remaining & 1) != 0)
			current = level < base_jump_counts_[current] ?
				base_jumps_[base_jump_offsets_[current] + level] : kNoEntity;
	if (current != base) return false;
	if (distance) *distance = difference;
	if (all_public) *all_public =
		deepest_nonpublic_base_depths_[derived] <= base_depths_[base];
	return true;
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
	PublishUsingName(scope, name);
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
	++using_name_invalidation_generation_;
	if (using_name_invalidation_generation_ == 0)
	{
		std::fill(using_name_invalidation_marks_.begin(),
			using_name_invalidation_marks_.end(), 0);
		using_name_invalidation_generation_ = 1;
	}
	using_name_worklist_.clear();
	using_name_invalidation_marks_[scope] =
		using_name_invalidation_generation_;
	using_name_worklist_.push_back(scope);
	for (std::size_t i = 0; i < using_name_worklist_.size(); ++i)
	{
		const ScopeId current = using_name_worklist_[i];
		std::size_t worklist_pushes = 0;
		lookup_cache_invalidations += lookup_cache_->InvalidateName(
			current, name, &worklist_pushes);
		lookup_cache_invalidation_pushes += worklist_pushes;
		for (std::uint32_t edge = scopes_[current].first_incoming_using;
			edge != std::numeric_limits<std::uint32_t>::max();
			edge = using_edges_[edge].next_incoming)
		{
			if (FindUsingNameRelation(edge, name) ==
				std::numeric_limits<std::uint32_t>::max()) continue;
			const ScopeId owner = using_edges_[edge].owner;
			if (using_name_invalidation_marks_[owner] ==
				using_name_invalidation_generation_) continue;
			using_name_invalidation_marks_[owner] =
				using_name_invalidation_generation_;
			using_name_worklist_.push_back(owner);
		}
	}
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
	if (kind == LOOKUP_FUNCTION_TEMPLATE &&
		(entry->ordinary != kNoBinding || entry->type != kNoType ||
		 entry->function_template))
	{
		result.BeginFunctionTemplateLookup();
		if (entry->function_template)
			result.AddFunctionTemplateOwner(scope);
	}
	return result;
}

void Program::MergeLookup(LookupResult* result,
	const LookupResult& candidate) const
{
	if (candidate.Empty()) return;
	if (candidate.HasFunctionTemplateLookup())
	{
		if (!result->HasFunctionTemplateLookup())
		{
			*result = candidate;
			return;
		}
		for (std::size_t i = 0;
			i < candidate.FunctionTemplateOwnerCount(); ++i)
			result->AddFunctionTemplateOwner(
				candidate.FunctionTemplateOwnerAt(i));
		return;
	}
	if (result->Empty())
	{
		*result = candidate;
		return;
	}
	if (result->name_space != candidate.name_space ||
		result->type != candidate.type ||
		result->type_declaration_canonical !=
			candidate.type_declaration_canonical)
		throw std::runtime_error("ambiguous PA11 lookup");
	if (result->ordinary == kNoBinding && candidate.ordinary == kNoBinding)
		return;
	if (result->ordinary == kNoBinding || candidate.ordinary == kNoBinding)
		throw std::runtime_error("ambiguous PA11 lookup");
	const bool result_functions =
		bindings[result->ordinary].kind == BIND_FUNCTION;
	const bool candidate_functions =
		bindings[candidate.ordinary].kind == BIND_FUNCTION;
	if (!result_functions || !candidate_functions)
	{
		if (result->OrdinaryCount() == 1 && candidate.OrdinaryCount() == 1 &&
			bindings[result->ordinary].canonical ==
				bindings[candidate.ordinary].canonical)
			return;
		throw std::runtime_error("ambiguous PA11 lookup");
	}
	for (std::size_t i = 0; i < candidate.OrdinaryCount(); ++i)
		result->AddOrdinary(candidate.OrdinaryAt(i));
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
	if (owner_entity != kNoEntity)
	{
		const EntityRecord& owner_record = entities[owner_entity];
		for (std::size_t base_index = 0;
			base_index < owner_record.direct_base_count; ++base_index)
		{
			const ScopeId target = entities[
				DirectBase(owner_entity, base_index).entity].member_scope;
			if (target != kNoScope && lookup_marks_[target] != lookup_generation_)
			{
				++lookup_edge_visits;
				lookup_marks_[target] = lookup_generation_;
				lookup_worklist_.push_back(target);
			}
		}
	}
	const std::uint32_t scope_visible = FindVisibleName(scope, name);
	for (std::uint32_t relation = scope_visible ==
			std::numeric_limits<std::uint32_t>::max() ?
			std::numeric_limits<std::uint32_t>::max() :
			visible_names_[scope_visible].first_relation;
		relation != std::numeric_limits<std::uint32_t>::max();
		relation = using_name_relations_[relation].next)
	{
		++lookup_edge_visits;
		const std::uint32_t edge = using_name_relations_[relation].edge;
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
		if (current_entity != kNoEntity)
		{
			const EntityRecord& current_record = entities[current_entity];
			for (std::size_t base_index = 0;
				base_index < current_record.direct_base_count; ++base_index)
			{
				const ScopeId target = entities[
					DirectBase(current_entity, base_index).entity].member_scope;
				if (target != kNoScope &&
					lookup_marks_[target] != lookup_generation_)
				{
					++lookup_edge_visits;
					lookup_marks_[target] = lookup_generation_;
					lookup_worklist_.push_back(target);
				}
			}
		}
		const std::uint32_t current_visible = FindVisibleName(current, name);
		for (std::uint32_t relation = current_visible ==
				std::numeric_limits<std::uint32_t>::max() ?
				std::numeric_limits<std::uint32_t>::max() :
				visible_names_[current_visible].first_relation;
			relation != std::numeric_limits<std::uint32_t>::max();
			relation = using_name_relations_[relation].next)
		{
			++lookup_edge_visits;
			const std::uint32_t edge = using_name_relations_[relation].edge;
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
	LookupResult cached;
	std::uint32_t cache_entry = std::numeric_limits<std::uint32_t>::max();
	if (lookup_cache_->Find(requested, name, kind, &cached, &cache_entry))
	{
		++lookup_cache_hits;
		collecting_lookup_dependencies_ = false;
		return cached;
	}
	++lookup_cache_misses;

	lookup_pending_targets_.clear();
	lookup_pending_next_.clear();
	++lookup_pending_generation_;
	if (lookup_pending_generation_ == 0)
	{
		std::fill(lookup_pending_target_marks_.begin(),
			lookup_pending_target_marks_.end(), 0);
		std::fill(lookup_pending_head_marks_.begin(),
			lookup_pending_head_marks_.end(), 0);
		lookup_pending_generation_ = 1;
	}

	LookupResult result;
	for (ScopeId current = scope; current != kNoScope;
		current = scopes_[current].parent)
	{
		RecordLookupDependency(current);
		const std::uint32_t current_visible = FindVisibleName(current, name);
		for (std::uint32_t relation = current_visible ==
				std::numeric_limits<std::uint32_t>::max() ?
				std::numeric_limits<std::uint32_t>::max() :
				visible_names_[current_visible].first_relation;
			relation != std::numeric_limits<std::uint32_t>::max();
			relation = using_name_relations_[relation].next)
		{
			++lookup_edge_visits;
			const std::uint32_t edge = using_name_relations_[relation].edge;
			const UsingEdge& using_edge = using_edges_[edge];
			if (lookup_pending_target_marks_[using_edge.target] ==
				lookup_pending_generation_)
				continue;
			lookup_pending_target_marks_[using_edge.target] =
				lookup_pending_generation_;
			if (lookup_pending_targets_.size() >=
				std::numeric_limits<std::uint32_t>::max())
				throw std::runtime_error("too many pending using targets");
			const std::uint32_t pending =
				static_cast<std::uint32_t>(lookup_pending_targets_.size());
			lookup_pending_targets_.push_back(using_edge.target);
			lookup_pending_next_.push_back(
				lookup_pending_head_marks_[using_edge.injection] ==
					lookup_pending_generation_ ?
					lookup_pending_heads_[using_edge.injection] :
					std::numeric_limits<std::uint32_t>::max());
			lookup_pending_heads_[using_edge.injection] = pending;
			lookup_pending_head_marks_[using_edge.injection] =
				lookup_pending_generation_;
		}

		++lookup_scope_visits;
		result = DirectLookup(current, name, kind);
		const EntityId scope_entity = scopes_[current].entity;
		if (result.Empty() && scope_entity != kNoEntity &&
			(entities[scope_entity].flavor == NAMED_STRUCT ||
			 entities[scope_entity].flavor == NAMED_CLASS ||
			 entities[scope_entity].flavor == NAMED_UNION))
			result = LookupGraph(current, name, kind);

		for (std::uint32_t pending =
				lookup_pending_head_marks_[current] ==
					lookup_pending_generation_ ? lookup_pending_heads_[current] :
					std::numeric_limits<std::uint32_t>::max();
			pending != std::numeric_limits<std::uint32_t>::max();
			pending = lookup_pending_next_[pending])
			MergeLookup(&result,
				LookupGraph(lookup_pending_targets_[pending], name, kind));
		if (!result.Empty()) break;
	}

	std::size_t dependency_edges = 0;
	lookup_cache_->Store(requested, name, kind, result,
		lookup_dependencies_, std::numeric_limits<std::uint32_t>::max(),
		&dependency_edges);
	lookup_cache_dependency_edges += dependency_edges;
	collecting_lookup_dependencies_ = false;
	return result;
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
		bytes += entries[i].scope_dependencies.StorageBytes() +
			entries[i].result.DynamicStorageBytes();
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
			if (record.dependent_bound_parameter != kNoTemplateParameter ||
				record.bound == 0 ||
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
			output += record.dependent_bound_parameter == kNoTemplateParameter ?
				std::to_string(record.bound) : "dependent";
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
	std::size_t bytes = names.StorageBytes() + types.StorageBytes() +
		scopes_.capacity() * sizeof(ScopeRecord) +
		child_edges_.capacity() * sizeof(ChildEdge) +
		using_edges_.capacity() * sizeof(UsingEdge) +
		using_edge_slots_.capacity() * sizeof(std::uint32_t) +
		visible_names_.capacity() * sizeof(ScopeVisibleName) +
		visible_name_slots_.capacity() * sizeof(std::uint32_t) +
		using_name_relations_.capacity() * sizeof(UsingNameRelation) +
		using_name_relation_slots_.capacity() * sizeof(std::uint32_t) +
		using_name_worklist_.capacity() * sizeof(ScopeId) +
		using_name_invalidation_marks_.capacity() * sizeof(std::uint32_t) +
		entries_.capacity() * sizeof(NameEntry) +
		entry_slots_.capacity() * sizeof(std::uint32_t) +
		template_argument_lists_.capacity() *
			sizeof(TemplateArgumentListRecord) +
		template_argument_list_slots_.capacity() * sizeof(std::uint32_t) +
		lookup_marks_.capacity() * sizeof(std::uint32_t) +
		lookup_worklist_.capacity() * sizeof(ScopeId) +
		lookup_dependency_marks_.capacity() * sizeof(std::uint32_t) +
		lookup_dependencies_.capacity() * sizeof(ScopeId) +
		lookup_pending_heads_.capacity() * sizeof(std::uint32_t) +
		lookup_pending_head_marks_.capacity() * sizeof(std::uint32_t) +
		lookup_pending_targets_.capacity() * sizeof(ScopeId) +
		lookup_pending_next_.capacity() * sizeof(std::uint32_t) +
		lookup_pending_target_marks_.capacity() * sizeof(std::uint32_t) +
		direct_bases.capacity() * sizeof(DirectBaseEdge) +
		base_jumps_.capacity() * sizeof(EntityId) +
		base_jump_offsets_.capacity() * sizeof(std::size_t) +
		base_jump_counts_.capacity() * sizeof(std::uint8_t) +
		base_depths_.capacity() * sizeof(std::uint32_t) +
		deepest_nonpublic_base_depths_.capacity() * sizeof(std::uint32_t) +
		lookup_cache_->StorageBytes() +
		entities.capacity() * sizeof(EntityRecord) +
		bindings.capacity() * sizeof(BindingRecord) +
		template_arguments.capacity() * sizeof(TypeId) +
		canonical_template_arguments.capacity() * sizeof(TemplateArgument);
	return bytes;
}

}
}
