#include "semantic/model/program.h"
#include "support/exceptions.h"

#include <cstdint>
#include <vector>

namespace cppgm
{
namespace semantic
{

namespace
{

class VisitedBaseStates
{
public:
	VisitedBaseStates() : slots_(8, 0), size_(0) {}

	bool Insert(std::uint64_t value)
	{
		const std::uint64_t stored = value + 1;
		if ((size_ + 1) * 10 > slots_.size() * 7)
			Rehash(slots_.size() * 2);
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = MixHash(0, value) & mask;
		while (slots_[slot] != 0)
		{
			if (slots_[slot] == stored) return false;
			slot = (slot + 1) & mask;
		}
		slots_[slot] = stored;
		++size_;
		return true;
	}

private:
	void Rehash(std::size_t capacity)
	{
		std::vector<std::uint64_t> replacement(capacity, 0);
		const std::size_t mask = capacity - 1;
		for (std::size_t i = 0; i < slots_.size(); ++i)
		{
			if (slots_[i] == 0) continue;
			const std::uint64_t value = slots_[i] - 1;
			std::size_t slot = MixHash(0, value) & mask;
			while (replacement[slot] != 0)
				slot = (slot + 1) & mask;
			replacement[slot] = slots_[i];
		}
		slots_.swap(replacement);
	}

	std::vector<std::uint64_t> slots_;
	std::size_t size_;
};

struct PendingBase
{
	EntityId entity;
	bool saw_virtual;
	PendingBase(EntityId entity_, bool saw_virtual_)
		: entity(entity_), saw_virtual(saw_virtual_) {}
};

}

LookupResult::LookupResult()
	: name_space(kNoScope), type(kNoType), type_declaration(kNoBinding),
	  type_declaration_canonical(kNoBinding),
	  ordinary(kNoBinding), ordinary_declaration(kNoBinding),
	  naming_class(kNoEntity), extra_ordinary_inline_(),
	  extra_ordinary_overflow_(), extra_ordinary_count_(0),
	  template_owner_(kNoScope), extra_template_owner_inline_(),
	  extra_template_owner_overflow_(), extra_template_owner_count_(0),
	  function_template_lookup_(false), variable_template_lookup_(false)
{
}

bool LookupResult::Empty() const
{
	return name_space == kNoScope && type == kNoType && ordinary == kNoBinding &&
		!function_template_lookup_ && !variable_template_lookup_;
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
		ThrowInternalCompilerError("ordinary lookup candidate index is out of range");
	return extra_ordinary_count_ <= 2 ? extra_ordinary_inline_[index] :
		extra_ordinary_overflow_[index];
}

void LookupResult::AddOrdinary(BindingId binding)
{
	if (binding == kNoBinding)
		ThrowInternalCompilerError("ordinary lookup candidate has no identity");
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
	return TemplateOwnerCount();
}

ScopeId LookupResult::FunctionTemplateOwnerAt(std::size_t index) const
{
	return TemplateOwnerAt(index);
}

void LookupResult::AddFunctionTemplateOwner(ScopeId owner)
{
	function_template_lookup_ = true;
	AddTemplateOwner(owner);
}

bool LookupResult::HasVariableTemplateLookup() const
{
	return variable_template_lookup_;
}

void LookupResult::BeginVariableTemplateLookup()
{
	variable_template_lookup_ = true;
}

std::size_t LookupResult::VariableTemplateOwnerCount() const
{
	return TemplateOwnerCount();
}

ScopeId LookupResult::VariableTemplateOwnerAt(std::size_t index) const
{
	return TemplateOwnerAt(index);
}

void LookupResult::AddVariableTemplateOwner(ScopeId owner)
{
	variable_template_lookup_ = true;
	AddTemplateOwner(owner);
}

std::size_t LookupResult::TemplateOwnerCount() const
{
	return template_owner_ == kNoScope ? 0 :
		extra_template_owner_count_ + 1;
}

ScopeId LookupResult::TemplateOwnerAt(std::size_t index) const
{
	if (index == 0 && template_owner_ != kNoScope)
		return template_owner_;
	if (template_owner_ == kNoScope || --index >=
		extra_template_owner_count_)
		ThrowInternalCompilerError("template owner index is out of range");
	return extra_template_owner_count_ <= 2 ?
		extra_template_owner_inline_[index] :
		extra_template_owner_overflow_[index];
}

void LookupResult::AddTemplateOwner(ScopeId owner)
{
	if (owner == kNoScope)
		ThrowInternalCompilerError("template lookup owner is missing");
	for (std::size_t i = 0; i < TemplateOwnerCount(); ++i)
		if (TemplateOwnerAt(i) == owner) return;
	if (template_owner_ == kNoScope)
	{
		template_owner_ = owner;
		return;
	}
	if (extra_template_owner_count_ < 2 &&
		extra_template_owner_overflow_.empty())
		extra_template_owner_inline_[extra_template_owner_count_] = owner;
	else
	{
		if (extra_template_owner_overflow_.empty())
		{
			extra_template_owner_overflow_.reserve(4);
			extra_template_owner_overflow_.insert(
				extra_template_owner_overflow_.end(),
				extra_template_owner_inline_,
				extra_template_owner_inline_ + 2);
		}
		extra_template_owner_overflow_.push_back(owner);
	}
	++extra_template_owner_count_;
}

std::size_t LookupResult::DynamicStorageBytes() const
{
	return extra_ordinary_overflow_.capacity() * sizeof(BindingId) +
		extra_template_owner_overflow_.capacity() * sizeof(ScopeId);
}

bool Program::HasVirtualBasePath(EntityId derived, EntityId base) const
{
	if (base == kNoEntity || derived == kNoEntity ||
		base >= entities.size() || derived >= entities.size()) return false;
	std::vector<PendingBase> pending;
	VisitedBaseStates visited;
	pending.push_back(PendingBase(derived, false));
	while (!pending.empty())
	{
		const PendingBase current = pending.back();
		pending.pop_back();
		++virtual_base_path_visits;
		const std::uint64_t state =
			(static_cast<std::uint64_t>(current.entity) << 1) |
			(current.saw_virtual ? 1 : 0);
		if (!visited.Insert(state)) continue;
		if (current.entity == base && current.saw_virtual) return true;
		const EntityRecord& record = entities[current.entity];
		for (std::size_t i = 0; i < record.direct_base_count; ++i)
		{
			const DirectBaseEdge& edge = DirectBase(current.entity, i);
			pending.push_back(PendingBase(edge.entity,
				current.saw_virtual || edge.virtual_base));
		}
	}
	return false;
}

}
}
