#include "pa15_lowering_abi.h"

#include "abi_mangle.h"
#include "pa15_lowir_model.h"
#include "pa18_polymorphism_lowering.h"
#include "pa22_lambda_presentation.h"

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cppgm
{
namespace pa15_lowering_abi
{

namespace
{

bool IsTrivialLifecycleBinding(const pa11::Program& program,
	pa11::BindingId binding)
{
	using namespace pa11;
	if (binding == kNoBinding || binding >= program.bindings.size())
		return false;
	const BindingRecord& record = program.bindings[binding];
	if (record.member_owner == kNoEntity ||
		record.member_owner >= program.entities.size() ||
		record.type == kNoType || !program.types.IsFunction(record.type))
		return false;
	const TypeRecord& function = program.types.Get(record.type);
	if (function.parameter_count != 0) return false;
	return (record.constructor && !record.constructor_base_entry &&
			program.entities[record.member_owner].trivial_default_constructor) ||
		(record.destructor &&
		 program.entities[record.member_owner].trivial_destructor);
}

bool HasTrivialLifecycleMetadata(const pa11::Program& program,
	pa11::BindingId binding)
{
	using namespace pa11;
	if (binding == kNoBinding || binding >= program.bindings.size())
		return false;
	const BindingRecord& record = program.bindings[binding];
	if (record.member_owner == kNoEntity ||
		record.member_owner >= program.entities.size() ||
		record.type == kNoType || !program.types.IsFunction(record.type))
		return false;
	const TypeRecord& function = program.types.Get(record.type);
	if (function.parameter_count != 0) return false;
	const EntityRecord& owner = program.entities[record.member_owner];
	return (record.constructor && !record.constructor_base_entry &&
			owner.trivial_default_constructor) ||
		(record.destructor && owner.trivial_destructor);
}

bool IsCompleteBoundaryObject(const pa11::Program& program, pa11::TypeId type)
{
	using namespace pa11;
	const TypeRecord* record = &program.types.Get(type);
	while (record->kind == TYPE_QUALIFIED)
	{
		type = record->child;
		record = &program.types.Get(type);
	}
	if (record->kind == TYPE_NAMED)
		return record->entity < program.entities.size() &&
			program.entities[record->entity].complete;
	if (record->kind == TYPE_ARRAY)
		return !record->IsIncompleteArray() &&
			IsCompleteBoundaryObject(program, record->child);
	if (record->kind == TYPE_COMPLEX)
		return IsCompleteBoundaryObject(program, record->child);
	if (record->kind == TYPE_VECTOR || record->kind == TYPE_BITINT)
		return record->dependent_bound_parameter == kNoTemplateParameter;
	return record->kind != TYPE_INVALID && record->kind != TYPE_FUNCTION;
}

}

void ApplyLifecycleSymbolMetadata(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node,
	pa15_lowir_detail::TypedProgram* output,
	pa15_lowir_detail::SymbolId symbol,
	abi_mangle::AbiMangleContext* context,
	abi_mangle::AbiMangleStats* stats)
{
	using namespace pa11;
	using namespace pa15_lowir_detail;
	const BindingRecord& binding = program.bindings[node.binding];
	const TypeRecord& function = program.types.Get(node.type);
	if (function.kind != TYPE_FUNCTION)
		throw std::logic_error("lifecycle ABI metadata has non-function type");
	const bool trivial_constructor = HasTrivialLifecycleMetadata(
		program, node.binding) && binding.constructor &&
		!binding.constructor_base_entry && binding.member_owner != kNoEntity &&
		function.parameter_count == 1;
	const bool trivial_destructor = HasTrivialLifecycleMetadata(
		program, node.binding) && binding.destructor;
	Symbol& record = output->symbols[symbol];
	record.lifecycle_base_entry |=
		binding.constructor_base_entry || binding.destructor_base_entry;
	record.trivial_lifecycle = trivial_constructor || trivial_destructor;
	if (output->host_object_emission && record.internal_linkage &&
		node.kind == pa12_semantic_detail::DUMP_FUNCTION_DEFINITION &&
		(binding.constructor || binding.destructor))
		record.object_output_root = true;
	const bool complete_entry =
		(binding.constructor && !binding.constructor_base_entry) ||
		(binding.destructor && !binding.destructor_base_entry);
	if (complete_entry && binding.member_owner != kNoEntity &&
		program.entities[binding.member_owner].virtual_base_count != 0)
		record.keep_internal_object_alias = false;
	const bool shared_base_entry = complete_entry &&
		binding.member_owner != kNoEntity &&
		program.entities[binding.member_owner].virtual_base_count == 0 &&
		(binding.lifecycle_base_entry == kNoBinding ||
		 binding.lifecycle_base_entry == binding.canonical);
	if (!output->host_object_emission ||
		node.kind != pa12_semantic_detail::DUMP_FUNCTION_DEFINITION ||
		!shared_base_entry) return;
	const std::string alias =
		MangleFunction(program, node, true, stats, context);
	if (!alias.empty() && (!record.object_name.valid() ||
		alias != output->strings.get(record.object_name)))
		output->object_aliases.push_back(ObjectAlias(
			output->strings.intern(alias), symbol));
}

namespace
{

using namespace pa11;

bool MakeBuiltinAbiType(const Program& program, const TypeRecord& source,
	abi_mangle::AbiType* result)
{
	using namespace abi_mangle;
	if (source.kind == TYPE_BITINT)
	{
		if (source.dependent_bound_parameter != kNoTemplateParameter ||
			source.bound == 0)
			throw std::runtime_error("dependent _BitInt has no ABI encoding");
		result->kind = ABI_TYPE_BUILTIN;
		result->builtin_type = source.bitint_unsigned ?
			ABI_BUILTIN_TYPE_UNSIGNED_BITINT : ABI_BUILTIN_TYPE_BITINT;
		result->index = source.bound;
		return true;
	}
	if (source.kind == TYPE_COMPLEX)
	{
		const TypeRecord& element = program.types.Get(source.child);
		if (element.kind != TYPE_FUNDAMENTAL)
			throw std::runtime_error("complex ABI element is not fundamental");
		result->kind = ABI_TYPE_BUILTIN;
		switch (element.fundamental)
		{
		case FUND_FLOAT:
			result->builtin_type = ABI_BUILTIN_TYPE_COMPLEX_FLOAT;
			return true;
		case FUND_DOUBLE:
			result->builtin_type = ABI_BUILTIN_TYPE_COMPLEX_DOUBLE;
			return true;
		case FUND_LONG_DOUBLE:
			result->builtin_type = ABI_BUILTIN_TYPE_COMPLEX_LONG_DOUBLE;
			return true;
		default:
			throw std::runtime_error("unsupported complex ABI element type");
		}
	}
	if (source.kind != TYPE_FUNDAMENTAL) return false;
	static const AbiBuiltinTypeKind fundamental_types[] = {
		ABI_BUILTIN_TYPE_BOOL, ABI_BUILTIN_TYPE_CHAR,
		ABI_BUILTIN_TYPE_SIGNED_CHAR, ABI_BUILTIN_TYPE_UNSIGNED_CHAR,
		ABI_BUILTIN_TYPE_SHORT, ABI_BUILTIN_TYPE_UNSIGNED_SHORT,
		ABI_BUILTIN_TYPE_INT, ABI_BUILTIN_TYPE_UNSIGNED_INT,
		ABI_BUILTIN_TYPE_LONG, ABI_BUILTIN_TYPE_UNSIGNED_LONG,
		ABI_BUILTIN_TYPE_LONG_LONG, ABI_BUILTIN_TYPE_UNSIGNED_LONG_LONG,
		ABI_BUILTIN_TYPE_FLOAT, ABI_BUILTIN_TYPE_DOUBLE,
		ABI_BUILTIN_TYPE_LONG_DOUBLE, ABI_BUILTIN_TYPE_VOID,
		ABI_BUILTIN_TYPE_NULLPTR, ABI_BUILTIN_TYPE_WCHAR,
		ABI_BUILTIN_TYPE_CHAR16, ABI_BUILTIN_TYPE_CHAR32,
		ABI_BUILTIN_TYPE_INT128, ABI_BUILTIN_TYPE_UINT128,
		ABI_BUILTIN_TYPE_FLOAT16, ABI_BUILTIN_TYPE_FLOAT32,
		ABI_BUILTIN_TYPE_FLOAT32X, ABI_BUILTIN_TYPE_FLOAT64,
		ABI_BUILTIN_TYPE_FLOAT64X, ABI_BUILTIN_TYPE_STDFLOAT128,
		ABI_BUILTIN_TYPE_FLOAT128
	};
	static_assert(sizeof(fundamental_types) / sizeof(fundamental_types[0]) ==
		FUND_FLOAT128 + 1, "fundamental ABI type table is incomplete");
	result->kind = ABI_TYPE_BUILTIN;
	result->builtin_type = fundamental_types[source.fundamental];
	return true;
}

void AppendTypedFact(abi_mangle::AbiTypedCase* facts,
	abi_mangle::AbiFactRecord* record)
{
	using namespace abi_mangle;
	switch (record->kind)
	{
	case ABI_FACT_RECORD_DEFINITION:
		facts->definitions.push_back(std::move(record->definition));
		return;
	case ABI_FACT_RECORD_FUNCTION:
		facts->functions.push_back(std::move(record->function));
		return;
	case ABI_FACT_RECORD_TARGET:
		if (facts->has_target)
			throw std::logic_error("production ABI case has two targets");
		facts->target = std::move(record->target);
		facts->has_target = true;
		return;
	}
	throw std::logic_error("invalid production ABI fact record");
}

void AppendFunctionAbiTagFacts(const Program& program,
	const BindingRecord& binding, abi_mangle::AbiMangleContext* context,
	abi_mangle::AbiTypedCase* facts)
{
	using namespace abi_mangle;
	if (binding.abi_tag_count == 0) return;
	if (binding.abi_tag_begin > program.abi_tags.size() ||
		binding.abi_tag_count > program.abi_tags.size() - binding.abi_tag_begin)
		throw std::logic_error("invalid function ABI tag fact range");
	for (std::size_t i = 0; i < binding.abi_tag_count; ++i)
	{
		AbiFactRecord tag;
		tag.set_kind(ABI_FACT_RECORD_FUNCTION);
		tag.function.kind = ABI_FUNCTION_RECORD_ABI_TAG;
		const NameId name = program.abi_tags[binding.abi_tag_begin + i];
		tag.function.set_resolved_source_name(
			context->resolve_external_name(name, program.names.Get(name)));
		AppendTypedFact(facts, &tag);
	}
}

void AppendComponentAbiTagFacts(const Program& program,
	const EntityRecord& entity, abi_mangle::AbiMangleContext* context,
	abi_mangle::AbiTypedCase* facts)
{
	using namespace abi_mangle;
	if (entity.abi_tag_begin > program.abi_tags.size() ||
		entity.abi_tag_count > program.abi_tags.size() - entity.abi_tag_begin)
		throw std::logic_error("invalid component ABI tag fact range");
	for (std::size_t i = 0; i < entity.abi_tag_count; ++i)
	{
		AbiFactRecord tag;
		tag.set_kind(ABI_FACT_RECORD_FUNCTION);
		tag.function.kind = ABI_FUNCTION_RECORD_COMPONENT_ABI_TAG;
		const NameId name = program.abi_tags[entity.abi_tag_begin + i];
		tag.function.set_resolved_source_name(
			context->resolve_external_name(name, program.names.Get(name)));
		AppendTypedFact(facts, &tag);
	}
}

abi_mangle::AbiTerminalKind OperatorTerminal(OperatorKind kind, bool member,
	std::size_t parameter_count);

struct StandardTemplateSubstitution
{
	abi_mangle::AbiStandardSubstitutionKind code;
	bool includes_arguments;

	StandardTemplateSubstitution(
		abi_mangle::AbiStandardSubstitutionKind code_value =
			abi_mangle::ABI_STANDARD_SUBSTITUTION_TEXT,
		bool includes_arguments_value = false)
		: code(code_value), includes_arguments(includes_arguments_value) {}
};

NameId StandardTemplateTerminal(const Program& program,
	const EntityRecord& entity)
{
	std::vector<NameId> path;
	program.BuildEmissionPath(entity.owner, entity.identity_name, &path);
	return path.size() == 2 && program.IsStandardNamespace(entity.owner) ?
		path[1] : 0;
}

bool IsClassTemplateSpecialization(const EntityRecord& entity)
{
	return entity.template_argument_begin != kNoBinding;
}

bool ClassOwnerHasAbiTags(const Program& program, EntityId entity)
{
	for (std::size_t depth = 0;
		entity != kNoEntity && entity < program.entities.size() &&
			depth < program.entities.size(); ++depth)
	{
		const EntityRecord& record = program.entities[entity];
		if (record.abi_tag_count != 0) return true;
		entity = record.enclosing_class;
	}
	return false;
}

bool ClassOwnerHasSpecializedAncestor(const Program& program, EntityId entity)
{
	bool first = true;
	for (std::size_t depth = 0;
		entity != kNoEntity && entity < program.entities.size() &&
			depth < program.entities.size(); ++depth)
	{
		const EntityRecord& record = program.entities[entity];
		if (!first && IsClassTemplateSpecialization(record)) return true;
		first = false;
		entity = record.enclosing_class;
	}
	return false;
}

bool ClassOwnerIsNested(const Program& program, EntityId entity)
{
	return entity != kNoEntity && entity < program.entities.size() &&
		program.entities[entity].enclosing_class != kNoEntity;
}

bool IsFundamentalType(const Program& program, TypeId type,
	FundamentalKind fundamental)
{
	type = program.types.RemoveTopCv(type);
	const TypeRecord& record = program.types.Get(type);
	return record.kind == TYPE_FUNDAMENTAL &&
		record.fundamental == fundamental;
}

bool IsStandardUnaryCharTemplate(const Program& program, TypeId type,
	const char* terminal)
{
	type = program.types.RemoveTopCv(type);
	const TypeRecord& record = program.types.Get(type);
	if (record.kind != TYPE_NAMED || record.entity >= program.entities.size())
		return false;
	const EntityRecord& entity = program.entities[record.entity];
	const NameId name = StandardTemplateTerminal(program, entity);
	if (name == 0 || program.names.Get(name) != terminal ||
		entity.template_argument_count != 1 ||
		entity.template_argument_begin >= program.template_arguments.size())
		return false;
	return IsFundamentalType(program,
		program.template_arguments[entity.template_argument_begin], FUND_CHAR);
}

bool HasExactStandardCharacterArguments(const Program& program,
	const EntityRecord& entity, bool allocator)
{
	const std::size_t expected = allocator ? 3 : 2;
	const std::size_t first = entity.template_argument_begin;
	if (entity.template_argument_count != expected ||
		first > program.template_arguments.size() ||
		expected > program.template_arguments.size() - first)
		return false;
	return IsFundamentalType(program, program.template_arguments[first],
			FUND_CHAR) &&
		IsStandardUnaryCharTemplate(program,
			program.template_arguments[first + 1], "char_traits") &&
		(!allocator || IsStandardUnaryCharTemplate(program,
			program.template_arguments[first + 2], "allocator"));
}

StandardTemplateSubstitution StandardSubstitutionFor(
	const Program& program, const EntityRecord& entity)
{
	const NameId terminal_id = StandardTemplateTerminal(program, entity);
	if (terminal_id == 0) return StandardTemplateSubstitution();
	const std::string& terminal = program.names.Get(terminal_id);
	if (terminal == "allocator")
		return StandardTemplateSubstitution(
			abi_mangle::ABI_STANDARD_SUBSTITUTION_ALLOCATOR, false);
	if (terminal == "basic_string")
		return HasExactStandardCharacterArguments(program, entity, true) ?
			StandardTemplateSubstitution(
				abi_mangle::ABI_STANDARD_SUBSTITUTION_STRING, true) :
			StandardTemplateSubstitution(
				abi_mangle::ABI_STANDARD_SUBSTITUTION_BASIC_STRING, false);
	if (terminal == "basic_istream" &&
		HasExactStandardCharacterArguments(program, entity, false))
		return StandardTemplateSubstitution(
			abi_mangle::ABI_STANDARD_SUBSTITUTION_ISTREAM, true);
	if (terminal == "basic_ostream" &&
		HasExactStandardCharacterArguments(program, entity, false))
		return StandardTemplateSubstitution(
			abi_mangle::ABI_STANDARD_SUBSTITUTION_OSTREAM, true);
	if (terminal == "basic_iostream" &&
		HasExactStandardCharacterArguments(program, entity, false))
		return StandardTemplateSubstitution(
			abi_mangle::ABI_STANDARD_SUBSTITUTION_IOSTREAM, true);
	return StandardTemplateSubstitution();
}

class AbiFactBuilder
{
	public:
	struct LocalContextHandle
	{
		std::size_t storage;
		std::size_t identity;
	};

	private:
	struct TypeArgumentCacheKey
	{
		TypeId type;
		BindingId function;
		FunctionTemplateAbiRecipeId recipe;

		TypeArgumentCacheKey(TypeId type_value, BindingId function_value,
			FunctionTemplateAbiRecipeId recipe_value)
			: type(type_value), function(function_value), recipe(recipe_value) {}

		bool operator==(const TypeArgumentCacheKey& other) const
		{
			return type == other.type && function == other.function &&
				recipe == other.recipe;
		}
	};

	struct TypeArgumentCacheEntry
	{
		TypeArgumentCacheKey key;
		std::size_t id;

		TypeArgumentCacheEntry(const TypeArgumentCacheKey& key_value,
			std::size_t id_value) : key(key_value), id(id_value) {}
	};

	const pa11::Program& program_;
	abi_mangle::AbiTypedCase& facts_;
	abi_mangle::AbiMangleContext* context_;
	abi_mangle::AbiMangleStats* stats_;
	std::size_t next_argument_;
	std::vector<TypeArgumentCacheEntry> type_argument_cache_;
	std::vector<std::uint32_t> type_argument_cache_slots_;
	std::vector<pa11::NameId> semantic_path_scratch_;
	std::vector<std::size_t> resolved_path_scratch_;

	TypeArgumentCacheKey TypeArgumentKey(pa11::TypeId type,
		const pa11::BindingRecord* function,
		const pa11::FunctionTemplateAbiRecipe* recipe) const
	{
		const BindingId function_id = function ?
			function->canonical : kNoBinding;
		FunctionTemplateAbiRecipeId recipe_id = kNoFunctionTemplateAbiRecipe;
		if (recipe)
		{
			if (program_.function_template_abi_recipes.empty())
				throw std::logic_error("ABI type argument recipe has no owner");
			const FunctionTemplateAbiRecipe* begin =
				&program_.function_template_abi_recipes[0];
			const std::ptrdiff_t offset = recipe - begin;
			if (offset < 0 || static_cast<std::size_t>(offset) >=
				program_.function_template_abi_recipes.size())
				throw std::logic_error("ABI type argument recipe is invalid");
			recipe_id = static_cast<FunctionTemplateAbiRecipeId>(offset);
		}
		return TypeArgumentCacheKey(type, function_id, recipe_id);
	}

	static std::size_t TypeArgumentCacheHash(
		const TypeArgumentCacheKey& key)
	{
		return MixHash(MixHash(key.type, key.function), key.recipe);
	}

	const std::size_t* FindTypeArgument(
		const TypeArgumentCacheKey& key) const
	{
		const std::size_t mask = type_argument_cache_slots_.size() - 1;
		std::size_t slot = TypeArgumentCacheHash(key) & mask;
		while (type_argument_cache_slots_[slot] != 0)
		{
			const TypeArgumentCacheEntry& entry = type_argument_cache_[
				type_argument_cache_slots_[slot] - 1];
			if (entry.key == key) return &entry.id;
			slot = (slot + 1) & mask;
		}
		return 0;
	}

	void RehashTypeArguments(std::size_t capacity)
	{
		std::vector<std::uint32_t> replacement(capacity, 0);
		const std::size_t mask = capacity - 1;
		for (std::size_t entry = 0; entry < type_argument_cache_.size(); ++entry)
		{
			std::size_t slot =
				TypeArgumentCacheHash(type_argument_cache_[entry].key) & mask;
			while (replacement[slot] != 0) slot = (slot + 1) & mask;
			replacement[slot] = static_cast<std::uint32_t>(entry + 1);
		}
		type_argument_cache_slots_.swap(replacement);
	}

	void CacheTypeArgument(const TypeArgumentCacheKey& key,
		std::size_t id)
	{
		if ((type_argument_cache_.size() + 1) * 10 >
			type_argument_cache_slots_.size() * 7)
			RehashTypeArguments(type_argument_cache_slots_.size() * 2);
		if (type_argument_cache_.size() >=
			std::numeric_limits<std::uint32_t>::max())
			throw std::runtime_error("too many ABI type argument facts");
		const std::size_t mask = type_argument_cache_slots_.size() - 1;
		std::size_t slot = TypeArgumentCacheHash(key) & mask;
		while (type_argument_cache_slots_[slot] != 0)
			slot = (slot + 1) & mask;
		type_argument_cache_.push_back(TypeArgumentCacheEntry(key, id));
		type_argument_cache_slots_[slot] =
			static_cast<std::uint32_t>(type_argument_cache_.size());
	}

	std::size_t ResolvePath(pa11::ScopeId owner, pa11::NameId terminal)
	{
		program_.BuildEmissionPath(owner, terminal, &semantic_path_scratch_);
		return ResolvePath(semantic_path_scratch_, 0,
			semantic_path_scratch_.size());
	}

	std::size_t ResolvePath(const std::vector<pa11::NameId>& path,
		std::size_t begin, std::size_t end)
	{
		if (begin >= end || end > path.size())
			throw std::logic_error("invalid semantic ABI path range");
		resolved_path_scratch_.clear();
		resolved_path_scratch_.reserve(end - begin);
		for (std::size_t i = begin; i < end; ++i)
		{
			const pa11::NameId name = path[i];
			resolved_path_scratch_.push_back(context_->resolve_external_name(
				name, program_.names.Get(name)));
		}
		return context_->resolve_path(resolved_path_scratch_);
	}

	std::size_t ResolveName(pa11::NameId name)
	{
		return context_->resolve_external_name(name, program_.names.Get(name));
	}

	std::size_t ResolveScopePath(pa11::ScopeId owner)
	{
		program_.BuildEmissionPath(owner, 0, &semantic_path_scratch_);
		if (!semantic_path_scratch_.empty())
			semantic_path_scratch_.pop_back();
		if (semantic_path_scratch_.empty())
			return abi_mangle::ABI_NO_RESOLVED_REFERENCE;
		return ResolvePath(semantic_path_scratch_, 0,
			semantic_path_scratch_.size());
	}

	std::size_t ResolveGeneratedPath(pa11::ScopeId owner,
		const std::string& terminal)
	{
		program_.BuildEmissionPath(owner, 0, &semantic_path_scratch_);
		if (!semantic_path_scratch_.empty())
			semantic_path_scratch_.pop_back();
		resolved_path_scratch_.clear();
		resolved_path_scratch_.reserve(semantic_path_scratch_.size() + 1);
		for (std::size_t i = 0; i < semantic_path_scratch_.size(); ++i)
		{
			const pa11::NameId name = semantic_path_scratch_[i];
			resolved_path_scratch_.push_back(
				context_->resolve_external_name(
					name, program_.names.Get(name)));
		}
		resolved_path_scratch_.push_back(
			context_->resolve_generated_name(terminal));
		return context_->resolve_path(resolved_path_scratch_);
	}

	void AppendTypeAbiTags(abi_mangle::AbiType* destination,
		std::uint32_t begin, std::uint32_t count)
	{
		if (begin > program_.abi_tags.size() ||
			count > program_.abi_tags.size() - begin)
			throw std::logic_error("invalid ABI tag fact range");
		for (std::size_t i = 0; i < count; ++i)
			destination->presentation_names.push_tag_resolved(
				ResolveName(program_.abi_tags[begin + i]));
	}

	LocalContextHandle StoreLocalContext(
		const abi_mangle::AbiLocalContext& context, std::size_t identity)
	{
		LocalContextHandle result;
		result.storage = context_->store_context(context);
		result.identity = identity;
		abi_mangle::AbiResolvedContextBinding binding;
		binding.identity = identity;
		binding.context = result.storage;
		facts_.contexts.push_back(binding);
		return result;
	}

	public:
	template<typename Fact>
	static void AssignLocalContext(Fact* fact,
		const LocalContextHandle& context)
	{
		fact->resolved_context = context.storage;
		fact->resolved_context_identity = context.identity;
	}

public:
	void SetPath(abi_mangle::AbiFunctionTarget* target,
		pa11::ScopeId owner, pa11::NameId terminal)
	{
		target->resolved_path = ResolvePath(owner, terminal);
	}

	void SetGeneratedPath(abi_mangle::AbiFunctionTarget* target,
		pa11::ScopeId owner, const std::string& terminal)
	{
		target->resolved_path = ResolveGeneratedPath(owner, terminal);
	}

	void SetNamespaceLambda(abi_mangle::AbiFunctionTarget* target,
		pa11::ScopeId owner, std::uint32_t ordinal)
	{
		target->resolved_path = ResolveScopePath(owner);
		target->resolved_context_identity = ordinal;
	}

	void SetNamespaceLambda(abi_mangle::AbiType* type,
		pa11::ScopeId owner, std::uint32_t ordinal)
	{
		const std::size_t path = ResolveScopePath(owner);
		type->index = path == abi_mangle::ABI_NO_RESOLVED_REFERENCE ?
			0 : path + 1;
		type->resolved_expression = ordinal;
	}

	void SetPath(abi_mangle::AbiFunctionTarget* target,
		const std::vector<pa11::NameId>& path)
	{
		target->resolved_path = ResolvePath(path, 0, path.size());
	}

	void SetSourceName(abi_mangle::AbiFunctionTarget* target,
		pa11::NameId name)
	{
		target->set_resolved_source_name(ResolveName(name));
	}

	void SetSourceName(abi_mangle::AbiFunctionRecord* target,
		pa11::NameId name)
	{
		target->set_resolved_source_name(ResolveName(name));
	}

	void SetLocalSourceName(abi_mangle::AbiFunctionRecord* target,
		pa11::NameId name)
	{
		target->type.index = ResolveName(name) + 1;
	}

	void SetGeneratedLocalSourceName(abi_mangle::AbiFunctionRecord* target,
		const std::string& name)
	{
		target->type.index = context_->resolve_generated_name(name) + 1;
	}

	void AppendNameComponent(abi_mangle::AbiFunctionRecord* target,
		pa11::NameId name, std::size_t* path)
	{
		const std::size_t resolved_name = ResolveName(name);
		*path = context_->resolve_path_component(*path, resolved_name);
		target->set_resolved_name_component(*path, resolved_name);
	}

	AbiFactBuilder(const pa11::Program& program,
		abi_mangle::AbiTypedCase& facts,
		abi_mangle::AbiMangleContext* context,
		abi_mangle::AbiMangleStats* stats)
		: program_(program), facts_(facts), context_(context), stats_(stats),
		  next_argument_(0),
		  type_argument_cache_slots_(32, 0)
	{
		if (!context_)
			throw std::logic_error("typed ABI builder has no graph context");
	}

	std::size_t AddTypeArgument(pa11::TypeId type,
		const pa11::BindingRecord* function = 0,
		const pa11::FunctionTemplateAbiRecipe* recipe = 0)
	{
		using namespace abi_mangle;
		const TypeArgumentCacheKey key =
			TypeArgumentKey(type, function, recipe);
		const std::size_t* cached = FindTypeArgument(key);
		if (cached) return *cached;
		++next_argument_;
		if (!context_)
			throw std::logic_error("typed ABI argument has no graph context");
		AbiTemplateArgument argument;
		argument.kind = ABI_TEMPLATE_ARGUMENT_TYPE;
		argument.type = MakeType(type, function, recipe);
		const std::size_t id = context_->resolve_argument(argument);
		CacheTypeArgument(key, id);
		return id;
	}

	std::size_t AddEntity(pa11::BindingId source)
	{
		using namespace abi_mangle;
		using namespace pa11;
		if (source == kNoBinding || source >= program_.bindings.size())
			throw std::logic_error("ABI template argument entity is invalid");
		source = program_.bindings[source].canonical;
		const BindingRecord& binding = program_.bindings[source];
		++next_argument_;
		AbiEntityFact entity;
		if (binding.kind == BIND_FUNCTION)
		{
			entity.kind = ABI_ENTITY_FACT_FUNCTION;
			entity.function.kind = ABI_FUNCTION_TARGET_PATH;
			if (binding.name == 0)
				throw std::logic_error(
					"function ABI entity has no semantic name");
			SetPath(&entity.function, binding.owner, binding.name);
			const TypeRecord& function = program_.types.Get(binding.type);
			const TypeId* parameters =
				program_.types.Parameters(binding.type);
			for (std::size_t i = 0; i < function.parameter_count; ++i)
				entity.function.signature_parameter_types.push_back(
					MakeType(parameters[i]));
		}
		else
		{
			entity.kind = ABI_ENTITY_FACT_VARIABLE;
			if (binding.name == 0)
				throw std::logic_error(
					"variable ABI entity has no semantic name");
			SetPath(&entity.function, binding.owner, binding.name);
			entity.internal_linkage =
				binding.storage_class == STORAGE_CLASS_STATIC &&
				binding.member_owner == kNoEntity &&
				!binding.unnamed_namespace_linkage;
		}
		return context_->store_entity(entity);
	}

	std::size_t AddTemplateArgument(std::size_t argument,
		const pa11::BindingRecord* function = 0,
		const pa11::FunctionTemplateAbiRecipe* recipe = 0,
		std::size_t source_parameter = pa11::kNoTemplateParameter)
	{
		using namespace abi_mangle;
		using namespace pa11;
		if (argument >= program_.template_arguments.size())
			throw std::logic_error("ABI template argument index is invalid");
		if (argument >= program_.canonical_template_arguments.size() ||
			program_.canonical_template_arguments[argument].kind ==
				TEMPLATE_ARGUMENT_TYPE)
			return source_parameter != kNoTemplateParameter ?
				AddTypeArgument(program_.template_arguments[argument]) :
				AddTypeArgument(
					program_.template_arguments[argument], function, recipe);
		const TemplateArgument& source =
			program_.canonical_template_arguments[argument];
		if (source.kind == TEMPLATE_ARGUMENT_TEMPLATE)
		{
			const std::size_t identity = next_argument_++;
			AbiTemplateArgument target;
			if (source.dependent_parameter != kNoTemplateParameter)
			{
				target.kind =
					ABI_TEMPLATE_ARGUMENT_TEMPLATE_PARAMETER_TEMPLATE;
				target.index = source.dependent_parameter;
			}
			else
			{
				// The canonical marker type carries the template's indexed
				// qualified path.  Encoding it as a type argument gives the
				// Itanium substitution order used for a template-name argument,
				// without introducing a value-expression wrapper.  Keep the
				// template-name identity separate from its qualified path: both
				// are substitution candidates in the ABI grammar.
				target.kind = ABI_TEMPLATE_ARGUMENT_TYPE;
				target.type = MakeType(source.type, function, recipe);
				target.type.resolved_expression = make_semantic_substitution(
					ABI_SEMANTIC_SUBSTITUTION_TEMPLATE_ARGUMENT, identity);
			}
			return context_->resolve_argument(target);
		}
		++next_argument_;
		AbiTemplateArgument target;
		if (recipe && source_parameter < recipe->template_parameter_count &&
			recipe->template_parameter_type_begin <=
				program_.function_template_abi_template_parameter_types.size() &&
			source_parameter <
				program_.function_template_abi_template_parameter_types.size() -
					recipe->template_parameter_type_begin &&
			program_.function_template_abi_template_parameter_types[
				recipe->template_parameter_type_begin + source_parameter] !=
					kNoFunctionTemplateAbiType)
		{
			target.kind = ABI_TEMPLATE_ARGUMENT_DEPENDENT_VALUE;
			target.type = MakeFunctionTemplateAbiType(
				program_.function_template_abi_template_parameter_types[
					recipe->template_parameter_type_begin + source_parameter],
				*recipe);
			const TypeId value_type = source.source_value_type != kNoType ?
				source.source_value_type : source.type;
			target.value_type = MakeType(value_type);
			target.has_value_type = true;
			target.value = source.value;
		}
		else if (source.source_value_type != kNoType)
		{
			target.kind = ABI_TEMPLATE_ARGUMENT_VALUE;
			target.value_type = MakeType(source.source_value_type);
			target.has_value_type = true;
			target.value = source.value;
		}
		else if (source.value_binding != kNoBinding)
		{
			const BindingRecord& value = program_.bindings[
				program_.bindings[source.value_binding].canonical];
			const bool nonstatic_member = value.member_owner != kNoEntity &&
				((value.kind == BIND_FUNCTION &&
				  !value.static_member_function) ||
				 (value.kind != BIND_FUNCTION &&
				  value.storage_class != STORAGE_CLASS_STATIC));
			if (nonstatic_member)
			{
				if (value.member_owner >= program_.entities.size())
					throw std::logic_error(
						"ABI member entity owner is invalid");
				target.kind = ABI_TEMPLATE_ARGUMENT_MEMBER_EXTERNAL_ENTITY;
				target.type = MakeType(
					program_.entities[value.member_owner].type);
				target.index = ResolveName(value.name) + 1;
				target.address_of = true;
				target.member_is_function = value.kind == BIND_FUNCTION;
				if (target.member_is_function)
				{
					const TypeRecord& member_type = program_.types.Get(value.type);
					if (member_type.kind != TYPE_FUNCTION)
						throw std::logic_error(
							"ABI member function entity is not callable");
					target.member_function_const =
						(member_type.cv & CV_CONST) != 0;
					target.member_function_volatile =
						(member_type.cv & CV_VOLATILE) != 0;
					target.member_function_lvalue_ref =
						member_type.ref_qualifier == FUNCTION_REF_LVALUE;
					target.member_function_rvalue_ref =
						member_type.ref_qualifier == FUNCTION_REF_RVALUE;
					target.member_function_variadic = member_type.variadic;
				if (value.conversion_function)
				{
					target.member_function_terminal_kind =
						ABI_MEMBER_FUNCTION_TERMINAL_CONVERSION;
					target.member_function_conversion_type =
						MakeType(value.conversion_target);
				}
				else if (value.operator_kind != OPERATOR_NONE)
				{
					target.member_function_terminal_kind =
						ABI_MEMBER_FUNCTION_TERMINAL_OPERATOR;
					target.member_function_terminal_code =
						value.operator_kind == OPERATOR_LITERAL ?
							ABI_TERMINAL_LITERAL :
						OperatorTerminal(value.operator_kind, true,
							member_type.parameter_count);
					if (value.operator_kind == OPERATOR_LITERAL)
						target.resolved_entity =
							ResolveName(value.operator_literal_suffix);
				}
				if (value.template_argument_count != 0)
				{
					if (value.function_template_abi_recipe ==
						kNoFunctionTemplateAbiRecipe ||
						value.function_template_abi_recipe >=
							program_.function_template_abi_recipes.size())
						throw std::logic_error(
							"ABI member function template has no canonical recipe");
					const FunctionTemplateAbiRecipe& member_recipe =
						program_.function_template_abi_recipes[
							value.function_template_abi_recipe];
					const std::size_t first = value.template_argument_begin;
					const std::size_t count = value.template_argument_count;
					if (first > program_.template_arguments.size() ||
						count > program_.template_arguments.size() - first)
						throw std::logic_error(
							"ABI member function template argument range is invalid");
					if ((member_recipe.template_parameter_pack &&
						 member_recipe.template_parameter_count == 0) ||
						(!member_recipe.template_parameter_pack &&
						 count != member_recipe.template_parameter_count))
						throw std::logic_error(
							"ABI member function template argument shape is invalid");
					const std::size_t fixed = member_recipe.template_parameter_pack ?
						member_recipe.template_parameter_count - 1 :
						member_recipe.template_parameter_count;
					if (fixed > count)
						throw std::logic_error(
							"ABI member function template pack range is invalid");
					for (std::size_t i = 0; i < fixed; ++i)
						target.argument_refs.push_resolved(AddTemplateArgument(
							first + i, &value, &member_recipe, i));
					if (member_recipe.template_parameter_pack)
						target.argument_refs.push_resolved(AddTemplateArgumentPack(
							first + fixed, count - fixed, &value, &member_recipe));
					target.resolved_expression = make_semantic_substitution(
						ABI_SEMANTIC_SUBSTITUTION_MEMBER_TEMPLATE,
						value.canonical);
					target.member_function_has_result_type =
						!value.conversion_function;
					if (target.member_function_has_result_type)
					{
						if (member_recipe.result_type !=
							kNoFunctionTemplateAbiType)
							target.member_function_result_type =
								MakeFunctionTemplateAbiType(
									member_recipe.result_type, member_recipe);
						else
						{
							const TypeRecord& recipe_type =
								program_.types.Get(member_recipe.function_type);
							target.member_function_result_type =
								MakeFunctionTemplateType(
									recipe_type.child, value, &member_recipe);
						}
					}
					const TypeRecord& recipe_type =
						program_.types.Get(member_recipe.function_type);
					const TypeId* recipe_parameters =
						program_.types.Parameters(member_recipe.function_type);
					for (std::size_t i = 0;
						i < recipe_type.parameter_count; ++i)
					{
						AbiType encoded = MakeFunctionTemplateType(
							recipe_parameters[i], value, &member_recipe);
						if (member_recipe.function_parameter_pack &&
							i + 1 == recipe_type.parameter_count)
						{
							AbiTypeModifier expansion;
							expansion.kind = ABI_TYPE_PACK_EXPANSION;
							encoded.modifiers.insert(
								encoded.modifiers.begin(), expansion);
						}
						target.parameter_types.push_back(encoded);
					}
				}
				else
				{
					const TypeId* member_parameters =
						program_.types.Parameters(value.type);
					for (std::size_t i = 0;
						i < member_type.parameter_count; ++i)
						target.parameter_types.push_back(
							MakeType(member_parameters[i]));
				}
				}
			}
			else
			{
				target.kind = ABI_TEMPLATE_ARGUMENT_ENTITY;
				target.resolved_entity = AddEntity(source.value_binding);
				const TypeRecord& type = program_.types.Get(
					program_.types.RemoveTopCv(source.type));
				target.address_of = type.kind == TYPE_POINTER;
			}
		}
		else
		{
			target.kind = ABI_TEMPLATE_ARGUMENT_VALUE;
			target.value_type = MakeType(source.type, function, recipe);
			target.has_value_type = true;
			target.value = source.value;
		}
		return context_->resolve_argument(target);
	}

	std::size_t AddTemplateArgumentPack(std::size_t first, std::size_t count,
		const pa11::BindingRecord* function = 0,
		const pa11::FunctionTemplateAbiRecipe* recipe = 0)
	{
		using namespace abi_mangle;
		if (first > program_.template_arguments.size() ||
			count > program_.template_arguments.size() - first)
			throw std::logic_error("ABI template argument pack range is invalid");
		++next_argument_;
		AbiTemplateArgument argument_pack;
		argument_pack.kind = ABI_TEMPLATE_ARGUMENT_PACK;
		for (std::size_t argument = 0; argument < count; ++argument)
			argument_pack.argument_refs.push_resolved(
				AddTemplateArgument(first + argument, function, recipe));
		return context_->resolve_argument(argument_pack);
	}

	std::size_t AddTemplateParameterExpression(std::size_t parameter)
	{
		using namespace abi_mangle;
		++next_argument_;
		AbiDependentExpression expression;
		expression.kind = ABI_EXPRESSION_TEMPLATE_PARAMETER;
		expression.index = parameter;
		return context_->resolve_expression(expression);
	}

	abi_mangle::AbiType AddContextType(pa11::TypeId type)
	{
		++next_argument_;
		return MakeType(type);
	}

	LocalContextHandle AddLocalContext(pa11::BindingId binding)
	{
		using namespace abi_mangle;
		using namespace pa11;
		if (binding == kNoBinding || binding >= program_.bindings.size())
			throw std::logic_error("local ABI type has no function context");
		const BindingRecord& function = program_.bindings[binding];
		const TypeRecord& type = program_.types.Get(function.type);
		if (type.kind != TYPE_FUNCTION)
			throw std::logic_error("local ABI context is not a function");
		const std::size_t identity = next_argument_++;
		AbiLocalContext context_fact;
		if (function.member_owner != kNoEntity &&
			function.member_owner < program_.entities.size())
		{
			const EntityRecord& owner = program_.entities[function.member_owner];
			const BindingId call = owner.lambda_call_operator;
			if (owner.lambda_closure && call != kNoBinding &&
				call < program_.bindings.size() &&
				program_.bindings[call].canonical == function.canonical)
			{
				AbiLocalContext& context = context_fact;
				context.kind = ABI_CONTEXT_FUNCTION;
				context.target_signature_is_parameter_list = true;
				AbiFunctionTarget& target = context.function;
				if (owner.local_context != kNoBinding &&
					pa18_lowering_detail::PreferLocalObjectBinding(
						program_, function.member_owner))
				{
					target.kind = ABI_FUNCTION_TARGET_LOCAL;
					AssignLocalContext(&target,
						AddLocalContext(owner.local_context));
					target.local_presentation =
						ABI_LOCAL_PRESENTATION_GENERATED_LAMBDA;
					target.resolved_path = owner.lambda_ordinal;
				}
				else if (owner.local_context != kNoBinding)
				{
					target.kind = ABI_FUNCTION_TARGET_LAMBDA;
					AssignLocalContext(&target,
						AddLocalContext(owner.local_context));
					target.local_presentation =
						ABI_LOCAL_PRESENTATION_LAMBDA_DISCRIMINATOR;
					target.resolved_path = owner.lambda_ordinal;
				}
				else
				{
					target.kind = ABI_FUNCTION_TARGET_NAMESPACE_LAMBDA;
					SetNamespaceLambda(
						&target, owner.owner, owner.lambda_ordinal);
				}
				target.terminal_code = ABI_TERMINAL_CALL;
				if ((type.cv & CV_CONST) != 0)
					context.qualifiers.push_back(
						ABI_FUNCTION_QUALIFIER_CONST);
				if ((type.cv & CV_VOLATILE) != 0)
					context.qualifiers.push_back(
						ABI_FUNCTION_QUALIFIER_VOLATILE);
				const TypeId* parameters =
					program_.types.Parameters(function.type);
				for (std::size_t i = 0; i < type.parameter_count; ++i)
					target.signature_parameter_types.push_back(
						MakeType(parameters[i]));
				target.variadic = type.variadic;
				return StoreLocalContext(context_fact, identity);
			}
		}
		if (function.member_owner != kNoEntity &&
			function.member_owner < program_.entities.size() &&
			IsClassTemplateSpecialization(
				program_.entities[function.member_owner]))
		{
			const EntityRecord& owner = program_.entities[function.member_owner];
			AbiLocalContext& context = context_fact;
			context.kind = ABI_CONTEXT_FUNCTION;
			context.target_signature_is_parameter_list = true;
			AbiFunctionTarget& target = context.function;
			target.kind = ABI_FUNCTION_TARGET_MEMBER;
			target.owner_type = AddContextType(owner.type);
			SetSourceName(&target, function.name);
			if (!function.static_member_function)
			{
				if ((type.cv & CV_CONST) != 0)
					context.qualifiers.push_back(
						ABI_FUNCTION_QUALIFIER_CONST);
				if ((type.cv & CV_VOLATILE) != 0)
					context.qualifiers.push_back(
						ABI_FUNCTION_QUALIFIER_VOLATILE);
			}
			const TypeId* parameters = program_.types.Parameters(function.type);
			for (std::size_t i = 0; i < type.parameter_count; ++i)
				target.signature_parameter_types.push_back(
					AddContextType(parameters[i]));
			target.variadic = type.variadic;
			return StoreLocalContext(context_fact, identity);
		}
		if (function.owner == program_.GlobalScope() &&
			program_.names.Get(function.name) == "main")
		{
			if (stats_) ++stats_->typed_main_contexts;
			context_fact.kind = ABI_CONTEXT_MAIN;
			return StoreLocalContext(context_fact, identity);
		}
		context_fact.kind = ABI_CONTEXT_FUNCTION;
		AbiFunctionTarget& target = context_fact.function;
		target.kind = ABI_FUNCTION_TARGET_PATH;
		if (function.name == 0)
			throw std::logic_error(
				"function ABI local context has no semantic name");
		SetPath(&target, function.owner, function.name);
		const FunctionTemplateAbiRecipe* recipe = 0;
		if (function.function_template_abi_recipe !=
			kNoFunctionTemplateAbiRecipe)
		{
			if (function.function_template_abi_recipe >=
				program_.function_template_abi_recipes.size())
				throw std::logic_error(
					"local ABI context template recipe is invalid");
			recipe = &program_.function_template_abi_recipes[
				function.function_template_abi_recipe];
		}
		if (function.template_argument_count != 0)
		{
			if (recipe == 0)
				throw std::logic_error(
					"function template specialization has no canonical recipe");
			const std::size_t first = function.template_argument_begin;
			const std::size_t count = function.template_argument_count;
			if (first > program_.template_arguments.size() ||
				count > program_.template_arguments.size() - first)
				throw std::logic_error(
					"local ABI context template arguments are invalid");
			const std::size_t fixed = recipe &&
				recipe->template_parameter_pack ?
				recipe->template_parameter_count - 1 : count;
			if (fixed > count)
				throw std::logic_error(
					"local ABI context template pack is invalid");
			for (std::size_t i = 0; i < fixed; ++i)
			{
				AbiFunctionPathOperand argument;
				argument.kind = ABI_FUNCTION_PATH_TEMPLATE_ARGUMENT;
				argument.resolved_argument = AddTemplateArgument(
					first + i, &function, recipe, i);
				target.path_operands.push_back(argument);
			}
			if (recipe && recipe->template_parameter_pack)
			{
				AbiFunctionPathOperand pack;
				pack.kind = ABI_FUNCTION_PATH_TEMPLATE_ARGUMENT;
				pack.resolved_argument = AddTemplateArgumentPack(
					first + fixed, count - fixed, &function, recipe);
				target.path_operands.push_back(pack);
			}
			if (recipe->result_type != kNoFunctionTemplateAbiType)
				target.result_type = MakeFunctionTemplateAbiType(
					recipe->result_type, *recipe);
			else
			{
				TypeId result = type.child;
				const TypeRecord& recipe_type =
					program_.types.Get(recipe->function_type);
				if (UsesFunctionTemplateParameter(
					recipe_type.child, function, *recipe))
					result = recipe_type.child;
				target.result_type = result == type.child ? MakeType(result) :
					MakeType(result, &function, recipe);
			}
			target.has_result_type = true;
		}
		const TypeId* parameters = program_.types.Parameters(function.type);
		const TypeRecord* recipe_type = recipe ?
			&program_.types.Get(recipe->function_type) : 0;
		const TypeId* recipe_parameters = recipe ?
			program_.types.Parameters(recipe->function_type) : 0;
		for (std::size_t i = 0; i < type.parameter_count; ++i)
		{
			TypeId parameter = parameters[i];
			const FunctionTemplateAbiRecipe* parameter_recipe = 0;
			std::size_t source = i;
			bool pack_expansion = false;
			if (recipe_type)
			{
				const std::size_t fixed = recipe->function_parameter_pack ?
					recipe_type->parameter_count - 1 :
					recipe_type->parameter_count;
				source = i < fixed ? i : fixed;
				if (source >= recipe_type->parameter_count)
					throw std::logic_error(
						"local ABI context parameter recipe is invalid");
				pack_expansion = recipe->function_parameter_pack && i >= fixed;
				if (pack_expansion || UsesFunctionTemplateParameter(
					recipe_parameters[source], function, *recipe))
				{
					parameter = recipe_parameters[source];
					parameter_recipe = recipe;
				}
			}
			const FunctionTemplateAbiTypeId retained = recipe ?
				FunctionTemplateParameterAbiType(*recipe, source) :
				kNoFunctionTemplateAbiType;
			AbiType encoded = retained != kNoFunctionTemplateAbiType ?
				MakeFunctionTemplateAbiType(retained, *recipe) :
				MakeFunctionTemplateType(parameter, function, parameter_recipe);
			if (pack_expansion)
			{
				AbiTypeModifier expansion;
				expansion.kind = ABI_TYPE_PACK_EXPANSION;
				encoded.modifiers.insert(encoded.modifiers.begin(), expansion);
			}
			target.signature_parameter_types.push_back(encoded);
		}
		target.variadic = type.variadic;
		return StoreLocalContext(context_fact, identity);
	}

	bool FunctionTemplateParameter(pa11::TypeId type,
		const pa11::BindingRecord* function,
		const pa11::FunctionTemplateAbiRecipe* recipe,
		std::size_t* index) const
	{
		using namespace pa11;
		if (recipe)
		{
			const TypeRecord& shape = program_.types.Get(type);
			if (shape.kind != TYPE_NAMED) return false;
			const EntityRecord& entity = program_.entities[shape.entity];
			const std::size_t parameter = entity.template_parameter_ordinal;
			if (parameter >= recipe->template_parameter_count ||
				recipe->parameter_shape_begin >
					program_.function_template_parameter_shapes.size() ||
				parameter >= program_.function_template_parameter_shapes.size() -
					recipe->parameter_shape_begin ||
				program_.function_template_parameter_shapes[
					recipe->parameter_shape_begin + parameter] != type)
				return false;
			*index = parameter;
			return true;
		}
		if (!function || function->template_argument_count == 0) return false;
		const std::size_t first = function->template_argument_begin;
		if (first > program_.template_arguments.size() ||
			function->template_argument_count >
				program_.template_arguments.size() - first)
			throw std::logic_error("function template ABI argument range is invalid");
		for (std::size_t i = 0; i < function->template_argument_count; ++i)
		{
			const std::size_t argument = first + i;
			if (argument < program_.canonical_template_arguments.size() &&
				program_.canonical_template_arguments[argument].kind !=
					TEMPLATE_ARGUMENT_TYPE)
				continue;
			if (program_.template_arguments[argument] == type)
			{
				*index = i;
				return true;
			}
		}
		return false;
	}

	abi_mangle::AbiType MakeType(pa11::TypeId type)
	{
		return MakeType(type, 0, 0);
	}

	abi_mangle::AbiType MakeFunctionTemplateType(pa11::TypeId type,
		const pa11::BindingRecord& function,
		const pa11::FunctionTemplateAbiRecipe* recipe = 0)
	{
		// Only the retained pattern recipe can prove that a type occurrence is
		// dependent.  Inferring dependence by comparing an instantiated type
		// with the specialization's template arguments is ambiguous: for
		// `template<class T> int f(T)`, f<int>'s non-dependent result happens to
		// equal T's argument but must be encoded as `i`, not `T_`.
		return recipe ? MakeType(type, &function, recipe) : MakeType(type);
	}

	abi_mangle::AbiType MakeType(pa11::TypeId type,
		const pa11::BindingRecord* function,
		const pa11::FunctionTemplateAbiRecipe* recipe)
	{
		const TypeArgumentCacheKey key =
			TypeArgumentKey(type, function, recipe);
		std::size_t cached = 0;
		if (context_ && context_->find_resolved_type(
			key.type, key.function, key.recipe, &cached))
		{
			abi_mangle::AbiType direct;
			direct.kind = abi_mangle::ABI_TYPE_RESOLVED;
			direct.index = cached;
			return direct;
		}
		abi_mangle::AbiType result =
			MakeTypeCore(type, function, recipe);
		if (!CanResolveType(result)) return result;
		std::size_t resolved = context_->resolve_type(result);
		if (!context_->resolved_type_uses_case_facts(resolved))
			resolved = context_->cache_resolved_type(
				key.type, key.function, key.recipe, resolved);
		abi_mangle::AbiType direct;
		direct.kind = abi_mangle::ABI_TYPE_RESOLVED;
		direct.index = resolved;
		return direct;
	}

	pa11::FunctionTemplateAbiTypeId FunctionTemplateParameterAbiType(
		const pa11::FunctionTemplateAbiRecipe& recipe,
		std::size_t parameter) const
	{
		using namespace pa11;
		if (parameter >= recipe.function_parameter_count ||
			recipe.function_parameter_type_begin >
				program_.function_template_abi_function_parameter_types.size() ||
			parameter >=
				program_.function_template_abi_function_parameter_types.size() -
					recipe.function_parameter_type_begin)
			throw std::logic_error(
				"function template ABI parameter recipe is invalid");
		return program_.function_template_abi_function_parameter_types[
			recipe.function_parameter_type_begin + parameter];
	}

	std::size_t AddFunctionTemplateAbiExpression(
		pa11::FunctionTemplateAbiExpressionId expression,
		const pa11::FunctionTemplateAbiRecipe& recipe)
	{
		using namespace abi_mangle;
		using namespace pa11;
		if (expression == kNoFunctionTemplateAbiExpression ||
			expression >= program_.function_template_abi_expressions.size())
			throw std::logic_error(
				"function template ABI expression recipe is invalid");
		const FunctionTemplateAbiExpression& source =
			program_.function_template_abi_expressions[expression];
		++next_argument_;
		AbiDependentExpression target;
		if (source.kind ==
			FUNCTION_TEMPLATE_ABI_EXPRESSION_TEMPLATE_PARAMETER)
		{
			target.kind = ABI_EXPRESSION_TEMPLATE_PARAMETER;
			target.index = source.parameter;
		}
		else if (source.kind ==
			FUNCTION_TEMPLATE_ABI_EXPRESSION_FUNCTION_PARAMETER)
		{
			target.kind = ABI_EXPRESSION_FUNCTION_PARAMETER;
			target.index = source.parameter;
		}
		else if (source.kind == FUNCTION_TEMPLATE_ABI_EXPRESSION_TYPE_MEMBER)
		{
			target.kind = ABI_EXPRESSION_MEMBER;
			target.type = MakeFunctionTemplateAbiType(source.type, recipe);
			target.type.suppress_template_prefix_substitution = true;
			target.index = ResolveName(source.name) + 1;
			target.close_member_owner = true;
		}
		else if (source.kind ==
			FUNCTION_TEMPLATE_ABI_EXPRESSION_OBJECT_MEMBER)
		{
			target.kind = ABI_EXPRESSION_OBJECT_MEMBER;
			target.operation = source.indirect_member ?
				ABI_EXPRESSION_OPERATION_INDIRECT_MEMBER :
				ABI_EXPRESSION_OPERATION_MEMBER;
			target.index = ResolveName(source.name) + 1;
			target.expression_refs.push_resolved(
				AddFunctionTemplateAbiExpression(source.left, recipe));
		}
		else if (source.kind == FUNCTION_TEMPLATE_ABI_EXPRESSION_CALL)
		{
			target.kind = ABI_EXPRESSION_CALL;
			target.expression_refs.push_resolved(
				AddFunctionTemplateAbiExpression(source.left, recipe));
		}
		else if (source.kind == FUNCTION_TEMPLATE_ABI_EXPRESSION_UNARY)
		{
			target.kind = ABI_EXPRESSION_UNARY;
			if (source.operation != OPERATOR_STAR)
				throw std::logic_error(
					"unsupported retained dependent unary operation");
			target.operation = ABI_EXPRESSION_OPERATION_DEREFERENCE;
			target.expression_refs.push_resolved(
				AddFunctionTemplateAbiExpression(source.left, recipe));
		}
		else if (source.kind == FUNCTION_TEMPLATE_ABI_EXPRESSION_BINARY)
		{
			target.kind = ABI_EXPRESSION_BINARY;
			if (source.operation != OPERATOR_MINUS)
				throw std::logic_error(
					"unsupported retained dependent binary operation");
			target.operation = ABI_EXPRESSION_OPERATION_SUBTRACT;
			target.expression_refs.push_resolved(
				AddFunctionTemplateAbiExpression(source.left, recipe));
			target.expression_refs.push_resolved(
				AddFunctionTemplateAbiExpression(source.right, recipe));
		}
		else if (source.kind == FUNCTION_TEMPLATE_ABI_EXPRESSION_TEMPLATE_ID)
		{
			if (source.argument_begin >
					program_.function_template_abi_arguments.size() ||
				source.argument_count >
					program_.function_template_abi_arguments.size() -
						source.argument_begin)
				throw std::logic_error(
					"retained dependent template-id is invalid");
			target.kind = ABI_EXPRESSION_TEMPLATE_ID;
			target.index = ResolveName(source.name) + 1;
			for (std::size_t i = 0; i < source.argument_count; ++i)
				target.argument_refs.push_resolved(AddFunctionTemplateAbiArgument(
					program_.function_template_abi_arguments[
						source.argument_begin + i], recipe));
		}
		else throw std::logic_error(
			"function template ABI expression node kind is invalid");
		return context_->resolve_expression(target);
	}

	std::size_t AddFunctionTemplateAbiArgument(
		const pa11::FunctionTemplateAbiArgument& source,
		const pa11::FunctionTemplateAbiRecipe& recipe)
	{
		using namespace abi_mangle;
		using namespace pa11;
		++next_argument_;
		AbiTemplateArgument target;
		if (source.kind == FUNCTION_TEMPLATE_ABI_ARGUMENT_TYPE)
		{
			target.kind = ABI_TEMPLATE_ARGUMENT_TYPE;
			target.type = MakeFunctionTemplateAbiType(source.type, recipe);
			target.pack_expansion = source.pack_expansion;
		}
		else
		{
			target.kind = ABI_TEMPLATE_ARGUMENT_EXPRESSION;
			target.resolved_expression = AddFunctionTemplateAbiExpression(
				source.expression, recipe);
		}
		return context_->resolve_argument(target);
	}

	abi_mangle::AbiType MakeFunctionTemplateAbiType(
		pa11::FunctionTemplateAbiTypeId type,
		const pa11::FunctionTemplateAbiRecipe& recipe)
	{
		using namespace abi_mangle;
		using namespace pa11;
		if (type == kNoFunctionTemplateAbiType ||
			type >= program_.function_template_abi_types.size())
			throw std::logic_error("function template ABI result recipe is invalid");
		const FunctionTemplateAbiType& source =
			program_.function_template_abi_types[type];
		AbiType result;
		if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_CONCRETE)
			return MakeType(source.concrete_type);
		if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_PARAMETER)
		{
			if (source.parameter >= recipe.template_parameter_count)
				throw std::logic_error(
					"function template ABI result parameter is invalid");
			result.kind = ABI_TYPE_TEMPLATE_PARAMETER;
			result.index = source.parameter;
			result.substitutable = true;
			return result;
		}
		if (source.kind ==
			FUNCTION_TEMPLATE_ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION)
		{
			if (source.parameter >= recipe.template_parameter_count ||
				source.argument_begin >
					program_.function_template_abi_arguments.size() ||
				source.argument_count >
					program_.function_template_abi_arguments.size() -
						source.argument_begin)
				throw std::logic_error(
					"function template ABI template-parameter specialization is invalid");
			result.kind = ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION;
			result.index = source.parameter;
			for (std::size_t i = 0; i < source.argument_count; ++i)
				result.argument_refs.push_resolved(AddFunctionTemplateAbiArgument(
					program_.function_template_abi_arguments[
						source.argument_begin + i], recipe));
			return result;
		}
		if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_TEMPLATE_SPECIALIZATION)
		{
			if (source.entity == kNoEntity ||
				source.entity >= program_.entities.size() ||
				source.argument_begin >
					program_.function_template_abi_arguments.size() ||
				source.argument_count >
					program_.function_template_abi_arguments.size() -
						source.argument_begin)
				throw std::logic_error(
					"function template ABI specialization is invalid");
			result.kind = source.child == kNoFunctionTemplateAbiType ?
				ABI_TYPE_TEMPLATE_SPECIALIZATION :
				ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION;
			const EntityRecord& entity = program_.entities[source.entity];
			if (source.child == kNoFunctionTemplateAbiType)
				result.index = ResolvePath(
					entity.owner, entity.identity_name) + 1;
			else
				result.index = ResolveName(entity.identity_name) + 1;
			if (source.child != kNoFunctionTemplateAbiType)
				result.types.push_back(
					MakeFunctionTemplateAbiType(source.child, recipe));
			for (std::size_t i = 0; i < source.argument_count; ++i)
				result.argument_refs.push_resolved(AddFunctionTemplateAbiArgument(
					program_.function_template_abi_arguments[
						source.argument_begin + i], recipe));
			return result;
		}
		if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_DECLTYPE)
		{
			result.kind = ABI_TYPE_DECLTYPE_EXPRESSION;
			result.resolved_expression = AddFunctionTemplateAbiExpression(
				source.expression, recipe);
			return result;
		}
		if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_BUILTIN_TRANSFORM)
		{
			if (source.child == kNoFunctionTemplateAbiType || source.name == 0)
				throw std::logic_error(
					"function template ABI builtin transform is invalid");
			result.kind = ABI_TYPE_BUILTIN_TRANSFORM;
			result.index = ResolveName(source.name) + 1;
			result.types.push_back(
				MakeFunctionTemplateAbiType(source.child, recipe));
			return result;
		}
		if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_MEMBER)
		{
			if (source.child == kNoFunctionTemplateAbiType || source.name == 0)
				throw std::logic_error(
					"function template ABI result member is invalid");
			result.kind = ABI_TYPE_MEMBER;
			result.index = ResolveName(source.name) + 1;
			result.types.push_back(
				MakeFunctionTemplateAbiType(source.child, recipe));
			return result;
		}
		if (source.child == kNoFunctionTemplateAbiType)
			throw std::logic_error("function template ABI result modifier is invalid");
		result = MakeFunctionTemplateAbiType(source.child, recipe);
		AbiTypeModifier modifier;
		if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_QUALIFIED)
		{
			modifier.kind = ABI_TYPE_CV;
			modifier.is_const = (source.cv & CV_CONST) != 0;
			modifier.is_volatile = (source.cv & CV_VOLATILE) != 0;
		}
		else if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_POINTER)
			modifier.kind = ABI_TYPE_POINTER;
		else if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_LVALUE_REFERENCE)
			modifier.kind = ABI_TYPE_LVALUE_REFERENCE;
		else if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_RVALUE_REFERENCE)
			modifier.kind = ABI_TYPE_RVALUE_REFERENCE;
		else if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_ARRAY)
		{
			modifier.kind = ABI_TYPE_ARRAY;
			if (source.parameter != kNoTemplateParameter)
			{
				modifier.array_bound.kind = ABI_ARRAY_BOUND_EXPRESSION;
				modifier.array_bound.resolved_expression =
					AddTemplateParameterExpression(source.parameter);
			}
			else if (source.bound != 0)
			{
				modifier.array_bound.kind = ABI_ARRAY_BOUND_INTEGER;
				modifier.array_bound.resolved_expression = source.bound;
			}
		}
		else throw std::logic_error(
			"function template ABI result node kind is invalid");
		result.modifiers.insert(result.modifiers.begin(), modifier);
		return result;
	}

	bool UsesFunctionTemplateParameter(pa11::TypeId type,
		const pa11::BindingRecord& function,
		const pa11::FunctionTemplateAbiRecipe& recipe) const
	{
		using namespace pa11;
		std::vector<TypeId> pending(1, type);
		std::vector<unsigned char> visited(program_.types.Size() + 1, 0);
		while (!pending.empty())
		{
			const TypeId current = pending.back();
			pending.pop_back();
			if (current >= visited.size())
				throw std::logic_error(
					"function template ABI source type is invalid");
			if (visited[current]) continue;
			visited[current] = 1;
			std::size_t parameter = 0;
			if (FunctionTemplateParameter(
				current, &function, &recipe, &parameter)) return true;
			const TypeRecord& record = program_.types.Get(current);
			if (record.kind == TYPE_ARRAY &&
				record.dependent_bound_parameter != kNoTemplateParameter)
				return true;
			if (record.kind == TYPE_QUALIFIED || record.kind == TYPE_POINTER ||
				record.kind == TYPE_BLOCK_POINTER ||
				record.kind == TYPE_LVALUE_REFERENCE ||
				record.kind == TYPE_RVALUE_REFERENCE || record.kind == TYPE_ARRAY)
			{
				pending.push_back(record.child);
				continue;
			}
			if (record.kind == TYPE_FUNCTION)
			{
				pending.push_back(record.child);
				const TypeId* parameters = program_.types.Parameters(current);
				for (std::size_t i = 0; i < record.parameter_count; ++i)
					pending.push_back(parameters[i]);
				continue;
			}
			if (record.kind == TYPE_MEMBER_POINTER)
			{
				pending.push_back(record.child);
				pending.push_back(static_cast<TypeId>(record.bound));
				continue;
			}
			if (record.kind != TYPE_NAMED) continue;
			const EntityRecord& entity = program_.entities[record.entity];
			if (entity.enclosing_class != kNoEntity)
				pending.push_back(
					program_.entities[entity.enclosing_class].type);
			if (entity.template_argument_count == 0) continue;
			const std::size_t first = entity.template_argument_begin;
			if (first > program_.template_arguments.size() ||
				entity.template_argument_count >
					program_.template_arguments.size() - first)
				throw std::logic_error(
					"function template ABI source argument range is invalid");
			for (std::size_t i = 0; i < entity.template_argument_count; ++i)
			{
				const std::size_t argument = first + i;
				if (argument >= program_.canonical_template_arguments.size() ||
					program_.canonical_template_arguments[argument].kind ==
						TEMPLATE_ARGUMENT_TYPE)
					pending.push_back(program_.template_arguments[argument]);
			}
		}
		return false;
	}

	bool MakeTemplateParameterSpecialization(
		const pa11::EntityRecord& entity,
		const pa11::BindingRecord* function,
		const pa11::FunctionTemplateAbiRecipe* recipe,
		abi_mangle::AbiType* result)
	{
		using namespace abi_mangle;
		using namespace pa11;
		if (!recipe || entity.template_parameter_ordinal >=
			recipe->template_parameter_count ||
			entity.template_argument_count == 0) return false;
		result->kind = ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION;
		result->index = entity.template_parameter_ordinal;
		const std::size_t first = entity.template_argument_begin;
		if (first > program_.template_arguments.size() ||
			entity.template_argument_count >
				program_.template_arguments.size() - first)
			throw std::logic_error(
				"template-parameter specialization ABI arguments are invalid");
		const std::size_t pack = entity.template_argument_pack_begin;
		const std::size_t fixed = pack == kNoTemplateParameter ?
			entity.template_argument_count : pack;
		if (fixed > entity.template_argument_count)
			throw std::logic_error(
				"template-parameter specialization ABI pack is invalid");
		for (std::size_t i = 0; i < fixed; ++i)
			result->argument_refs.push_resolved(AddTemplateArgument(
				first + i, function, recipe));
		if (pack != kNoTemplateParameter)
			result->argument_refs.push_resolved(AddTemplateArgumentPack(
				first + fixed, entity.template_argument_count - fixed,
				function, recipe));
		return true;
	}

	void AppendClassTemplateArguments(const pa11::EntityRecord& entity,
		const pa11::BindingRecord* function,
		const pa11::FunctionTemplateAbiRecipe* recipe,
		abi_mangle::AbiType* result)
	{
		using namespace pa11;
		const std::size_t first = entity.template_argument_begin;
		if (first > program_.template_arguments.size() ||
			entity.template_argument_count >
				program_.template_arguments.size() - first)
			throw std::logic_error(
				"class template ABI argument range is invalid");
		const std::size_t pack = entity.template_argument_pack_begin;
		const std::size_t fixed = pack == kNoTemplateParameter ?
			entity.template_argument_count : pack;
		if (fixed > entity.template_argument_count)
			throw std::logic_error("class template ABI pack offset is invalid");
		for (std::size_t i = 0; i < fixed; ++i)
			result->argument_refs.push_resolved(AddTemplateArgument(
				first + i, function, recipe));
		if (pack != kNoTemplateParameter)
			result->argument_refs.push_resolved(AddTemplateArgumentPack(
				first + fixed, entity.template_argument_count - fixed,
				function, recipe));
	}
	abi_mangle::AbiType MakeBlockPointerType(pa11::TypeId function_type,
		const pa11::BindingRecord* function,
		const pa11::FunctionTemplateAbiRecipe* recipe,
		abi_mangle::AbiType result)
	{
		result.kind = abi_mangle::ABI_TYPE_VENDOR_QUALIFIED;
		result.vendor_qualifier =
			abi_mangle::ABI_VENDOR_QUALIFIER_BLOCK_POINTER;
		result.types.push_back(MakeType(function_type, function, recipe));
		return result;
	}

	bool CanResolveType(const abi_mangle::AbiType& type) const
	{
		using namespace abi_mangle;
		if (!context_) return false;
		if (type.kind == ABI_TYPE_RESOLVED) return true;
		if (type.kind == ABI_TYPE_NAME_OR_REFERENCE ||
			!type.expression_ref.empty() || !type.context_ref.empty() ||
			(!type.argument_refs.resolved() &&
			 !type.argument_refs.empty())) return false;
		if (type.array_bound.kind == ABI_ARRAY_BOUND_EXPRESSION &&
			!type.array_bound.value.empty()) return false;
		for (std::size_t i = 0; i < type.modifiers.size(); ++i)
			if (type.modifiers[i].array_bound.kind ==
					ABI_ARRAY_BOUND_EXPRESSION &&
				!type.modifiers[i].array_bound.value.empty())
				return false;
		for (std::size_t i = 0; i < type.types.size(); ++i)
			if (!CanResolveType(type.types[i])) return false;
		return true;
	}

	abi_mangle::AbiType MakeTypeCore(pa11::TypeId type,
		const pa11::BindingRecord* function,
		const pa11::FunctionTemplateAbiRecipe* recipe)
	{
		using namespace abi_mangle;
		using namespace pa11;
		std::vector<AbiTypeModifier> modifiers;
		const TypeRecord* record = &program_.types.Get(type);
		std::size_t template_parameter = 0;
		while (record->kind == TYPE_QUALIFIED || record->kind == TYPE_POINTER ||
			record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE || record->kind == TYPE_ARRAY)
		{
			if (FunctionTemplateParameter(
				type, function, recipe, &template_parameter))
			{
				AbiType result;
				result.kind = ABI_TYPE_TEMPLATE_PARAMETER;
				result.index = template_parameter;
				result.substitutable = true;
				result.modifiers.swap(modifiers);
				return result;
			}
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
				if (record->dependent_bound_parameter !=
					kNoTemplateParameter)
				{
					modifier.array_bound.kind =
						ABI_ARRAY_BOUND_EXPRESSION;
					modifier.array_bound.resolved_expression =
						AddTemplateParameterExpression(
							record->dependent_bound_parameter);
				}
				else if (record->bound != 0 || record->zero_length_array)
				{
					modifier.array_bound.kind = ABI_ARRAY_BOUND_INTEGER;
					modifier.array_bound.resolved_expression = record->bound;
				}
			}
			else modifier.kind = record->kind == TYPE_POINTER ? ABI_TYPE_POINTER :
				record->kind == TYPE_LVALUE_REFERENCE ? ABI_TYPE_LVALUE_REFERENCE :
				ABI_TYPE_RVALUE_REFERENCE;
			modifiers.push_back(modifier);
			type = record->child;
			record = &program_.types.Get(type);
		}
		AbiType result;
		result.modifiers.swap(modifiers);
		if (FunctionTemplateParameter(type, function, recipe, &template_parameter))
		{
			result.kind = ABI_TYPE_TEMPLATE_PARAMETER;
			result.index = template_parameter;
			result.substitutable = true;
			return result;
		}
		if (record->kind == TYPE_BLOCK_POINTER) return MakeBlockPointerType(
			record->child, function, recipe, result);
		if (record->kind == TYPE_FUNCTION)
		{
			result.kind = ABI_TYPE_FUNCTION;
			result.types.push_back(MakeType(record->child, function, recipe));
			const TypeId* parameters = program_.types.Parameters(type);
			for (std::size_t i = 0; i < record->parameter_count; ++i)
				result.types.push_back(
					MakeType(parameters[i], function, recipe));
			result.is_const = (record->cv & CV_CONST) != 0;
			result.is_volatile = (record->cv & CV_VOLATILE) != 0;
			result.lvalue_ref =
				record->ref_qualifier == FUNCTION_REF_LVALUE;
			result.rvalue_ref =
				record->ref_qualifier == FUNCTION_REF_RVALUE;
			result.variadic = record->variadic;
			return result;
		}
		if (record->kind == TYPE_MEMBER_POINTER)
		{
			result.kind = ABI_TYPE_MEMBER_POINTER;
			result.types.push_back(MakeType(
				static_cast<TypeId>(record->bound), function, recipe));
			result.types.push_back(MakeType(record->child, function, recipe));
			return result;
		}
		if (record->kind == TYPE_VECTOR)
		{
			result.kind = ABI_TYPE_VECTOR;
			result.array_bound.kind = ABI_ARRAY_BOUND_INTEGER;
			result.array_bound.resolved_expression =
				record->bound / program_.SizeOf(record->child);
			result.types.push_back(MakeType(record->child, function, recipe));
			return result;
		}
		if (record->kind == TYPE_NAMED)
		{
			const EntityRecord& entity = program_.entities[record->entity];
			if (MakeTemplateParameterSpecialization(entity, function, recipe, &result)) {}
			else if (entity.lambda_closure)
			{
				if (entity.local_context != kNoBinding &&
					pa18_lowering_detail::PreferLocalObjectBinding(
						program_, record->entity))
				{
					result.kind = ABI_TYPE_LOCAL_TYPE;
					AssignLocalContext(&result,
						AddLocalContext(entity.local_context));
					result.local_presentation =
						ABI_LOCAL_PRESENTATION_GENERATED_LAMBDA;
					result.resolved_expression = entity.lambda_ordinal;
				}
				else if (entity.local_context != kNoBinding)
				{
					result.kind = ABI_TYPE_LAMBDA_CLOSURE;
					AssignLocalContext(&result,
						AddLocalContext(entity.local_context));
					result.local_presentation =
						ABI_LOCAL_PRESENTATION_LAMBDA_DISCRIMINATOR;
					result.resolved_expression = entity.lambda_ordinal;
					if (entity.lambda_call_operator == kNoBinding ||
						entity.lambda_call_operator >= program_.bindings.size())
						throw std::logic_error(
							"lambda ABI type has no call operator fact");
					const BindingRecord& call =
						program_.bindings[entity.lambda_call_operator];
					const TypeRecord& call_type =
						program_.types.Get(call.type);
					const TypeId* parameters =
						program_.types.Parameters(call.type);
					for (std::size_t i = 0;
						i < call_type.parameter_count; ++i)
						result.types.push_back(MakeType(parameters[i]));
				}
				else
				{
					result.kind = ABI_TYPE_NAMESPACE_LAMBDA;
					SetNamespaceLambda(
						&result, entity.owner, entity.lambda_ordinal);
				}
			}
			else if (entity.local_context != kNoBinding)
			{
				result.kind = ABI_TYPE_LOCAL_TYPE;
				AssignLocalContext(&result,
					AddLocalContext(entity.local_context));
				if (!entity.unnamed_class)
					result.index = ResolveName(entity.identity_name) + 1;
				result.local_presentation =
					ABI_LOCAL_PRESENTATION_NAME_ORDINAL;
				result.resolved_expression = entity.local_name_ordinal;
			}
			else if (entity.enclosing_class != kNoEntity)
			{
				if (entity.enclosing_class >= program_.entities.size())
					throw std::logic_error(
						"nested ABI type has no enclosing class");
				const bool specialization =
					IsClassTemplateSpecialization(entity);
				result.kind = specialization ?
					ABI_TYPE_MEMBER_TEMPLATE_SPECIALIZATION : ABI_TYPE_MEMBER;
				result.index = ResolveName(entity.identity_name) + 1;
				result.types.push_back(MakeType(
					program_.entities[entity.enclosing_class].type,
					function, recipe));
				if (specialization)
					AppendClassTemplateArguments(
						entity, function, recipe, &result);
			}
			else if (!IsClassTemplateSpecialization(entity))
			{
				result.kind = ABI_TYPE_NAMED;
				result.index = ResolvePath(
					entity.owner, entity.identity_name) + 1;
			}
			else
			{
				const StandardTemplateSubstitution standard =
					StandardSubstitutionFor(program_, entity);
				result.kind = standard.code !=
					ABI_STANDARD_SUBSTITUTION_TEXT ?
					ABI_TYPE_STD_TEMPLATE_SPECIALIZATION :
					ABI_TYPE_TEMPLATE_SPECIALIZATION;
				if (standard.code != ABI_STANDARD_SUBSTITUTION_TEXT)
				{
					result.standard_substitution_code = standard.code;
					result.standard_substitution_includes_arguments =
						standard.includes_arguments;
				}
				else result.resolved_expression = make_semantic_substitution(
					ABI_SEMANTIC_SUBSTITUTION_CLASS, record->entity);
				result.index = ResolvePath(
					entity.owner, entity.identity_name) + 1;
				AppendClassTemplateArguments(entity, function, recipe, &result);
			}
			AppendTypeAbiTags(
				&result, entity.abi_tag_begin, entity.abi_tag_count);
			return result;
		}
		if (MakeBuiltinAbiType(program_, *record, &result)) return result;
		throw std::runtime_error("unsupported ABI type in PA15");
	}
};

bool AppendClassTemplateOwner(const pa11::Program& program,
	const pa11::BindingRecord& binding, AbiFactBuilder* builder,
	abi_mangle::AbiTypedCase* facts, bool retain_complete_substitution)
{
	using namespace abi_mangle;
	using namespace pa11;
	if (binding.member_owner == kNoEntity) return false;
	const EntityRecord& entity = program.entities[binding.member_owner];
	if (!IsClassTemplateSpecialization(entity)) return false;
	const std::size_t first = entity.template_argument_begin;
	if (first > program.template_arguments.size() ||
		entity.template_argument_count > program.template_arguments.size() - first)
		throw std::logic_error("class template ABI owner arguments are invalid");
	std::vector<NameId> path;
	program.BuildEmissionPath(entity.owner, entity.identity_name, &path);
	const StandardTemplateSubstitution standard =
		StandardSubstitutionFor(program, entity);
	// Standard template substitutions include their `std::` owner. Do not emit
	// that namespace again when the substitution is a nested-name prefix.
	const std::size_t path_begin = standard.code !=
		ABI_STANDARD_SUBSTITUTION_TEXT && path.size() == 2 ? 1 : 0;
	std::size_t resolved_prefix = ABI_NO_RESOLVED_REFERENCE;
	for (std::size_t i = path_begin; i < path.size(); ++i)
	{
		AbiFactRecord component;
		component.set_kind(ABI_FACT_RECORD_FUNCTION);
		builder->AppendNameComponent(
			&component.function, path[i], &resolved_prefix);
		if (i + 1 == path.size())
		{
			component.function.kind = ABI_FUNCTION_RECORD_NAME_TEMPLATE;
			if (retain_complete_substitution)
				component.function.type.resolved_expression =
					make_semantic_substitution(
						ABI_SEMANTIC_SUBSTITUTION_CLASS,
						binding.member_owner);
			component.function.standard_substitution_code = standard.code;
			component.function.standard_substitution_includes_arguments =
				standard.includes_arguments;
			const std::size_t pack = entity.template_argument_pack_begin;
			const std::size_t fixed = pack == kNoTemplateParameter ?
				entity.template_argument_count : pack;
			if (fixed > entity.template_argument_count)
				throw std::logic_error("class template ABI pack offset is invalid");
			for (std::size_t argument = 0; argument < fixed; ++argument)
				component.function.argument_refs.push_resolved(
					builder->AddTemplateArgument(first + argument));
			if (pack != kNoTemplateParameter)
				component.function.argument_refs.push_resolved(
					builder->AddTemplateArgumentPack(first + fixed,
						entity.template_argument_count - fixed));
		}
		else if (i == 0 && program.IsInStandardNamespace(entity.owner))
			component.function.kind = ABI_FUNCTION_RECORD_NAME_STD;
		else component.function.kind = ABI_FUNCTION_RECORD_NAME_SOURCE;
		AppendTypedFact(facts, &component);
	}
	return true;
}

abi_mangle::AbiTerminalKind OperatorTerminal(OperatorKind kind, bool member,
	std::size_t parameter_count)
{
	using namespace abi_mangle;
	switch (kind)
	{
	case OPERATOR_PLUS: return ABI_TERMINAL_PLUS;
	case OPERATOR_MINUS: return ABI_TERMINAL_MINUS;
	case OPERATOR_STAR:
		return (member ? parameter_count == 0 : parameter_count == 1) ?
			ABI_TERMINAL_DEREFERENCE : ABI_TERMINAL_MULTIPLY;
	case OPERATOR_AMPERSAND:
		return (member ? parameter_count == 0 : parameter_count == 1) ?
			ABI_TERMINAL_ADDRESS_OF : ABI_TERMINAL_BIT_AND;
	case OPERATOR_DIVIDE: return ABI_TERMINAL_DIVIDE;
	case OPERATOR_REMAINDER: return ABI_TERMINAL_REMAINDER;
	case OPERATOR_BIT_OR: return ABI_TERMINAL_BIT_OR;
	case OPERATOR_BIT_XOR: return ABI_TERMINAL_BIT_XOR;
	case OPERATOR_ASSIGN: return ABI_TERMINAL_ASSIGN;
	case OPERATOR_PLUS_ASSIGN: return ABI_TERMINAL_PLUS_ASSIGN;
	case OPERATOR_MINUS_ASSIGN: return ABI_TERMINAL_MINUS_ASSIGN;
	case OPERATOR_MULTIPLY_ASSIGN: return ABI_TERMINAL_MULTIPLY_ASSIGN;
	case OPERATOR_DIVIDE_ASSIGN: return ABI_TERMINAL_DIVIDE_ASSIGN;
	case OPERATOR_REMAINDER_ASSIGN: return ABI_TERMINAL_REMAINDER_ASSIGN;
	case OPERATOR_AND_ASSIGN: return ABI_TERMINAL_AND_ASSIGN;
	case OPERATOR_OR_ASSIGN: return ABI_TERMINAL_OR_ASSIGN;
	case OPERATOR_XOR_ASSIGN: return ABI_TERMINAL_XOR_ASSIGN;
	case OPERATOR_LEFT_SHIFT: return ABI_TERMINAL_LEFT_SHIFT;
	case OPERATOR_RIGHT_SHIFT: return ABI_TERMINAL_RIGHT_SHIFT;
	case OPERATOR_LEFT_SHIFT_ASSIGN: return ABI_TERMINAL_LEFT_SHIFT_ASSIGN;
	case OPERATOR_RIGHT_SHIFT_ASSIGN: return ABI_TERMINAL_RIGHT_SHIFT_ASSIGN;
	case OPERATOR_EQUAL: return ABI_TERMINAL_EQUAL;
	case OPERATOR_NOT_EQUAL: return ABI_TERMINAL_NOT_EQUAL;
	case OPERATOR_LESS: return ABI_TERMINAL_LESS;
	case OPERATOR_GREATER: return ABI_TERMINAL_GREATER;
	case OPERATOR_LESS_EQUAL: return ABI_TERMINAL_LESS_EQUAL;
	case OPERATOR_GREATER_EQUAL: return ABI_TERMINAL_GREATER_EQUAL;
	case OPERATOR_LOGICAL_NOT: return ABI_TERMINAL_LOGICAL_NOT;
	case OPERATOR_LOGICAL_AND: return ABI_TERMINAL_LOGICAL_AND;
	case OPERATOR_LOGICAL_OR: return ABI_TERMINAL_LOGICAL_OR;
	case OPERATOR_INCREMENT: return ABI_TERMINAL_INCREMENT;
	case OPERATOR_DECREMENT: return ABI_TERMINAL_DECREMENT;
	case OPERATOR_COMMA: return ABI_TERMINAL_COMMA;
	case OPERATOR_MEMBER_POINTER: return ABI_TERMINAL_MEMBER_POINTER;
	case OPERATOR_ARROW: return ABI_TERMINAL_ARROW;
	case OPERATOR_CALL: return ABI_TERMINAL_CALL;
	case OPERATOR_INDEX: return ABI_TERMINAL_INDEX;
	case OPERATOR_NEW: return ABI_TERMINAL_NEW;
	case OPERATOR_NEW_ARRAY: return ABI_TERMINAL_NEW_ARRAY;
	case OPERATOR_DELETE: return ABI_TERMINAL_DELETE;
	case OPERATOR_DELETE_ARRAY: return ABI_TERMINAL_DELETE_ARRAY;
	case OPERATOR_NONE:
	case OPERATOR_LITERAL: return ABI_TERMINAL_NONE;
	}
	throw std::logic_error("invalid typed operator terminal");
}

}

namespace
{

std::string MangleProductionCase(const abi_mangle::AbiTypedCase& fact_case,
	abi_mangle::AbiMangleStats* stats,
	abi_mangle::AbiMangleContext* context)
{
	if (stats)
	{
		++stats->production_mangles;
		stats->production_fact_bytes +=
			abi_mangle::abi_typed_case_storage_bytes(fact_case);
		for (const abi_mangle::AbiDefinitionRecord& definition :
			fact_case.definitions)
		{
			switch (definition.kind)
			{
			case abi_mangle::ABI_DEFINITION_TYPE:
				++stats->production_type_definitions; break;
			case abi_mangle::ABI_DEFINITION_TEMPLATE_ARGUMENT:
				++stats->production_argument_definitions; break;
			case abi_mangle::ABI_DEFINITION_EXPRESSION:
				++stats->production_expression_definitions; break;
			case abi_mangle::ABI_DEFINITION_CONTEXT:
				++stats->production_context_definitions; break;
			case abi_mangle::ABI_DEFINITION_ENTITY:
				++stats->production_entity_definitions; break;
			}
		}
	}
	if (context) return context->mangle_case(fact_case);
	abi_mangle::AbiMangleContext local_context(stats);
	return local_context.mangle_case(fact_case);
}

}

std::string MangleType(const pa11::Program& program, pa11::TypeId type,
	abi_mangle::AbiMangleStats* stats,
	abi_mangle::AbiMangleContext* context)
{
	using namespace abi_mangle;
	std::unique_ptr<AbiMangleContext> local_context;
	if (!context)
	{
		local_context.reset(new AbiMangleContext(stats));
		context = local_context.get();
	}
	AbiTypedCase fact_case;
	AbiFactBuilder facts(program, fact_case, context, stats);
	AbiFactRecord target;
	target.set_kind(ABI_FACT_RECORD_TARGET);
	target.target.kind = ABI_TARGET_FACT_TYPE;
	target.target.type = facts.MakeType(type);
	AppendTypedFact(&fact_case, &target);
	return MangleProductionCase(fact_case, stats, context);
}

bool IsFunctionEmissionDemanded(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node, bool host_object_emission)
{
	using namespace pa11;
	if (node.binding == kNoBinding) return false;
	if (node.declaration_only) return true;
	const BindingId binding = program.bindings[node.binding].canonical;
	if (host_object_emission &&
		IsTrivialLifecycleBinding(program, binding)) return false;
	return !program.bindings[binding].inline_function ||
		program.bindings[binding].emission_demanded;
}

bool IsFunctionDeclarationBoundaryComplete(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node)
{
	using namespace pa11;
	if (node.kind != pa12_semantic_detail::DUMP_FUNCTION_DECLARATION)
		return true;
	const TypeRecord& function = program.types.Get(node.type);
	if (function.kind != TYPE_FUNCTION) return false;
	const TypeId* parameters = program.types.Parameters(node.type);
	for (std::size_t i = 0; i < function.parameter_count; ++i)
		if (!IsCompleteBoundaryObject(program, parameters[i])) return false;
	return true;
}

bool IsVariableDeclarationOnly(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node, bool has_initializer)
{
	const pa11::BindingRecord& binding = program.bindings[node.binding];
	return node.declaration_only ||
		program.bindings[binding.canonical].explicit_instantiation_suppressed ||
		(!has_initializer && binding.storage_class == pa11::STORAGE_CLASS_EXTERN);
}

bool HasWeakLinkage(
	const pa11::Program& program, pa11::BindingId binding, bool function)
{
	const pa11::BindingRecord& record = program.bindings[binding];
	const pa11::BindingRecord& canonical = program.bindings[record.canonical];
	const bool class_template_member = record.member_owner != pa11::kNoEntity &&
		IsClassTemplateSpecialization(program.entities[record.member_owner]);
	const bool primary_template_member = class_template_member &&
		!program.entities[record.member_owner].explicit_template_specialization;
	const bool preempted = record.explicit_instantiation_suppressed ||
		canonical.explicit_instantiation_suppressed;
	return canonical.weak_symbol || (!preempted &&
		(record.weak_odr || primary_template_member ||
		 (function && record.template_argument_count != 0 &&
		  !record.explicit_function_specialization)));
}

void ApplyBuiltinSymbolMetadata(pa15_lowir_detail::Symbol* symbol,
	pa11::BuiltinFunctionKind kind,
	hosted_builtin::MemoryIntrinsicKind memory_kind)
{
	using namespace pa11;
	using pa15_lowir_detail::Symbol;
	if (kind == BUILTIN_FUNCTION_HOSTED_MEMORY_INTRINSIC)
	{
		switch (hosted_builtin::GetMemoryIntrinsic(memory_kind).effect)
		{
		case hosted_builtin::MEMORY_EFFECT_READNONE:
			symbol->effects = Symbol::EFFECTS_READNONE; break;
		case hosted_builtin::MEMORY_EFFECT_READONLY:
			symbol->effects = Symbol::EFFECTS_READONLY; break;
		case hosted_builtin::MEMORY_EFFECT_READWRITE:
			symbol->effects = Symbol::EFFECTS_READWRITE; break;
		}
		return;
	}
	switch (kind)
	{
	case BUILTIN_FUNCTION_STRLEN: symbol->effects = Symbol::EFFECTS_READONLY; break;
	case BUILTIN_FUNCTION_UNREACHABLE:
		symbol->effects = Symbol::EFFECTS_READNONE;
		symbol->noreturn = true;
		symbol->runtime_role = Symbol::RUNTIME_ROLE_UNREACHABLE;
		break;
	case BUILTIN_FUNCTION_MEMCPY:
	case BUILTIN_FUNCTION_MEMMOVE: symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_NANL:
	case BUILTIN_FUNCTION_ISNAN:
	case BUILTIN_FUNCTION_HOSTED_INTEGER_INTRINSIC:
	case BUILTIN_FUNCTION_HOSTED_FLOATING_INTRINSIC:
		symbol->effects = Symbol::EFFECTS_READNONE; break;
	case BUILTIN_FUNCTION_HOSTED_MEMORY_INTRINSIC: break;
	case BUILTIN_FUNCTION_IA32_EMMS:
		symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_ABORT:
		symbol->noreturn = true; break;
	case BUILTIN_FUNCTION_ALLOCA:
	case BUILTIN_FUNCTION_VSNPRINTF:
	case BUILTIN_FUNCTION_VA_START:
	case BUILTIN_FUNCTION_VA_END:
	case BUILTIN_FUNCTION_VA_ARG:
		symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_OPERATOR_NEW:
	case BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY:
		symbol->runtime_role = Symbol::RUNTIME_ROLE_ALLOCATE_MEMORY;
		symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_OPERATOR_DELETE:
	case BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY:
		symbol->runtime_role = Symbol::RUNTIME_ROLE_FREE_MEMORY;
		symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_NONE: break;
	}
}

void ApplyNativeRuntimeSymbolMetadata(
	const pa15_lowir_detail::TypedProgram& program,
	pa15_lowir_detail::Symbol* symbol)
{
	using pa15_lowir_detail::Symbol;
	if (!symbol->c_linkage || !symbol->object_name.valid()) return;
	const std::string& object_name = program.strings.get(symbol->object_name);
	if (object_name == "malloc")
		symbol->runtime_role = Symbol::RUNTIME_ROLE_ALLOCATE_MEMORY;
	else if (object_name == "free")
		symbol->runtime_role = Symbol::RUNTIME_ROLE_FREE_MEMORY;
}

void ApplyBuiltinParameterMetadata(pa15_lowir_detail::Parameter* parameter,
	pa11::BuiltinFunctionKind kind,
	hosted_builtin::MemoryIntrinsicKind memory_kind, std::size_t index)
{
	using namespace pa11;
	using pa15_lowir_detail::Parameter;
	if (kind == BUILTIN_FUNCTION_HOSTED_MEMORY_INTRINSIC)
	{
		const bool comparison_parameter =
			(memory_kind == hosted_builtin::MEMORY_INTRINSIC_MEMCMP ||
			 memory_kind == hosted_builtin::MEMORY_INTRINSIC_STRCMP) && index < 2;
		const bool pointer_parameter = index == 0 || comparison_parameter ||
			((memory_kind == hosted_builtin::MEMORY_INTRINSIC_MEMCPY ||
			  memory_kind == hosted_builtin::MEMORY_INTRINSIC_MEMMOVE) &&
			 index == 1);
		if (pointer_parameter)
			parameter->capture = Parameter::CAPTURE_NOCAPTURE;
		if (comparison_parameter ||
			((memory_kind == hosted_builtin::MEMORY_INTRINSIC_STRLEN ||
			 memory_kind == hosted_builtin::MEMORY_INTRINSIC_STRCHR ||
			 memory_kind == hosted_builtin::MEMORY_INTRINSIC_MEMCHR) && index == 0))
			parameter->access = Parameter::ACCESS_READ;
		else if ((memory_kind == hosted_builtin::MEMORY_INTRINSIC_BZERO ||
			memory_kind == hosted_builtin::MEMORY_INTRINSIC_MEMSET) && index == 0)
			parameter->access = Parameter::ACCESS_WRITE;
		else if (memory_kind == hosted_builtin::MEMORY_INTRINSIC_MEMCPY &&
			index < 2)
		{
			parameter->access = index == 0 ?
				Parameter::ACCESS_WRITE : Parameter::ACCESS_READ;
			parameter->alias = Parameter::ALIAS_NOALIAS;
		}
		else if (memory_kind == hosted_builtin::MEMORY_INTRINSIC_MEMMOVE &&
			index == 1)
			parameter->access = Parameter::ACCESS_READ;
		return;
	}
	if (kind == BUILTIN_FUNCTION_STRLEN && index == 0)
	{
		parameter->capture = Parameter::CAPTURE_NOCAPTURE;
		parameter->access = Parameter::ACCESS_READ;
	}
	else if (kind == BUILTIN_FUNCTION_MEMCPY && index < 2)
	{
		parameter->capture = Parameter::CAPTURE_NOCAPTURE;
		parameter->access = index == 0 ? Parameter::ACCESS_WRITE :
			Parameter::ACCESS_READ;
		parameter->alias = Parameter::ALIAS_NOALIAS;
	}
	else if (kind == BUILTIN_FUNCTION_MEMMOVE && index < 2)
	{
		parameter->capture = Parameter::CAPTURE_NOCAPTURE;
		if (index == 1) parameter->access = Parameter::ACCESS_READ;
	}
}

void AppendFunctionTemplateArgumentsAndResult(const pa11::Program& program,
	const pa11::BindingRecord& binding,
	const pa11::TypeRecord& function_type,
	const pa11::FunctionTemplateAbiRecipe* recipe,
	AbiFactBuilder* facts, abi_mangle::AbiTypedCase* output)
{
	using namespace abi_mangle;
	using namespace pa11;
	if (binding.template_argument_count == 0) return;
	const std::size_t first = binding.template_argument_begin;
	const std::size_t count = binding.template_argument_count;
	if (first > program.template_arguments.size() ||
		count > program.template_arguments.size() - first)
		throw std::logic_error(
			"function template argument range is invalid during mangling");
	const std::size_t fixed = recipe && recipe->template_parameter_pack ?
		recipe->template_parameter_count - 1 : count;
	if (fixed > count)
		throw std::logic_error("function template ABI pack range is invalid");
	for (std::size_t i = 0; i < fixed; ++i)
	{
		AbiFactRecord argument;
		argument.set_kind(ABI_FACT_RECORD_FUNCTION);
		argument.function.kind =
			ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT;
		argument.function.argument_refs.push_resolved(
			facts->AddTemplateArgument(first + i, &binding, recipe, i));
		AppendTypedFact(output, &argument);
	}
	if (recipe && recipe->template_parameter_pack)
	{
		AbiFactRecord argument;
		argument.set_kind(ABI_FACT_RECORD_FUNCTION);
		argument.function.kind =
			ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT;
		argument.function.argument_refs.push_resolved(
			facts->AddTemplateArgumentPack(first + fixed, count - fixed));
		AppendTypedFact(output, &argument);
	}
	// Itanium constructor, destructor, and conversion-function encodings do
	// not carry a result type, including when the callable is a template.
	if (binding.constructor || binding.destructor ||
		binding.conversion_function) return;
	AbiFactRecord result;
	result.set_kind(ABI_FACT_RECORD_FUNCTION);
	result.function.kind = ABI_FUNCTION_RECORD_RESULT;
	if (recipe && recipe->result_type != kNoFunctionTemplateAbiType)
	{
		result.function.type = facts->MakeFunctionTemplateAbiType(
			recipe->result_type, *recipe);
	}
	else
	{
		TypeId result_type = function_type.child;
		if (recipe)
		{
			const TypeId source = program.types.Get(recipe->function_type).child;
			if (facts->UsesFunctionTemplateParameter(source, binding, *recipe))
				result_type = source;
		}
		result.function.type = facts->MakeFunctionTemplateType(result_type,
			binding, result_type == function_type.child ? 0 : recipe);
	}
	AppendTypedFact(output, &result);
}

std::string MangleLambdaCallOperator(const pa11::Program& program,
	const pa11::BindingRecord& binding,
	const pa11::EntityRecord& lambda, abi_mangle::AbiMangleStats* stats,
	abi_mangle::AbiMangleContext* context)
{
	using namespace abi_mangle;
	using namespace pa11;
	if (!context)
		throw std::logic_error("lambda ABI mangling has no graph context");
	if (binding.operator_kind != OPERATOR_CALL)
		throw std::logic_error("invalid lambda call-operator ABI identity");
	AbiTypedCase fact_case;
	AbiFactBuilder facts(program, fact_case, context, stats);
	const FunctionTemplateAbiRecipe* recipe = 0;
	if (binding.function_template_abi_recipe != kNoFunctionTemplateAbiRecipe)
	{
		if (binding.function_template_abi_recipe >=
			program.function_template_abi_recipes.size())
			throw std::logic_error("invalid generic lambda ABI recipe");
		recipe = &program.function_template_abi_recipes[
			binding.function_template_abi_recipe];
	}
	AbiFactRecord lambda_target;
	lambda_target.set_kind(ABI_FACT_RECORD_TARGET);
	lambda_target.target.kind = ABI_TARGET_FACT_FUNCTION;
	AbiFunctionTarget& function = lambda_target.target.function;
	if (lambda.local_context != kNoBinding &&
		pa18_lowering_detail::PreferLocalObjectBinding(
			program, binding.member_owner))
	{
		function.kind = ABI_FUNCTION_TARGET_LOCAL;
		AbiFactBuilder::AssignLocalContext(&function,
			facts.AddLocalContext(lambda.local_context));
		function.local_presentation =
			ABI_LOCAL_PRESENTATION_GENERATED_LAMBDA;
		function.resolved_path = lambda.lambda_ordinal;
	}
	else if (lambda.local_context != kNoBinding)
	{
		function.kind = ABI_FUNCTION_TARGET_LAMBDA;
		AbiFactBuilder::AssignLocalContext(&function,
			facts.AddLocalContext(lambda.local_context));
		function.local_presentation =
			ABI_LOCAL_PRESENTATION_LAMBDA_DISCRIMINATOR;
		function.resolved_path = lambda.lambda_ordinal;
	}
	else
	{
		function.kind = ABI_FUNCTION_TARGET_NAMESPACE_LAMBDA;
		facts.SetNamespaceLambda(
			&function, lambda.owner, lambda.lambda_ordinal);
	}
	function.terminal_code = ABI_TERMINAL_CALL;
	const TypeRecord& lambda_type = program.types.Get(binding.type);
	AbiFactRecord qualifier;
	qualifier.set_kind(ABI_FACT_RECORD_FUNCTION);
	qualifier.function.kind = ABI_FUNCTION_RECORD_QUALIFIER;
	if ((lambda_type.cv & CV_CONST) != 0)
		qualifier.function.qualifiers.push_back(
			ABI_FUNCTION_QUALIFIER_CONST);
	if ((lambda_type.cv & CV_VOLATILE) != 0)
		qualifier.function.qualifiers.push_back(
			ABI_FUNCTION_QUALIFIER_VOLATILE);
	if (!qualifier.function.qualifiers.empty())
		AppendTypedFact(&fact_case, &qualifier);
	const TypeId signature_type = recipe ? recipe->function_type : binding.type;
	const TypeRecord& signature = program.types.Get(signature_type);
	const TypeId* signature_parameters = program.types.Parameters(signature_type);
	if (function.kind == ABI_FUNCTION_TARGET_LAMBDA)
		for (std::size_t i = 0; i < signature.parameter_count; ++i)
			function.signature_parameter_types.push_back(recipe ?
				facts.MakeFunctionTemplateType(
					signature_parameters[i], binding, recipe) :
				facts.MakeType(signature_parameters[i]));
	AppendFunctionTemplateArgumentsAndResult(program, binding, lambda_type,
		recipe, &facts, &fact_case);
	const TypeId* lambda_parameters = program.types.Parameters(binding.type);
	for (std::size_t i = 0; i < lambda_type.parameter_count; ++i)
	{
		AbiFactRecord parameter;
		parameter.set_kind(ABI_FACT_RECORD_FUNCTION);
		parameter.function.kind = ABI_FUNCTION_RECORD_PARAMETER;
		parameter.function.type = facts.MakeType(lambda_parameters[i]);
		AppendTypedFact(&fact_case, &parameter);
	}
	AppendTypedFact(&fact_case, &lambda_target);
	return MangleProductionCase(fact_case, stats, context);
}

void AppendLocalFunctionOwner(const pa11::Program& program,
	const pa11::BindingRecord& binding, AbiFactBuilder* facts,
	abi_mangle::AbiMangleContext* context,
	abi_mangle::AbiTypedCase* fact_case)
{
	using namespace abi_mangle;
	const pa11::EntityRecord& owner =
		program.entities[binding.member_owner];
	AbiFactRecord local;
	local.set_kind(ABI_FACT_RECORD_FUNCTION);
	local.function.kind = ABI_FUNCTION_RECORD_LOCAL_CONTEXT;
	AbiFactBuilder::AssignLocalContext(&local.function,
		facts->AddLocalContext(owner.local_context));
	if (owner.lambda_closure)
		facts->SetGeneratedLocalSourceName(&local.function,
			pa22_lambda_presentation::RenderLambdaEntityTerminal(
				program, binding.member_owner));
	else if (!owner.unnamed_class)
		facts->SetLocalSourceName(&local.function, owner.identity_name);
	local.function.local_presentation =
		ABI_LOCAL_PRESENTATION_NAME_ORDINAL;
	local.function.type.resolved_expression = owner.lambda_closure ? 0 :
		owner.local_name_ordinal;
	local.function.discriminator_after_terminal = !owner.unnamed_class;
	AppendTypedFact(fact_case, &local);
	AppendComponentAbiTagFacts(program, owner, context, fact_case);
}

void AppendMemberFunctionQualifiers(const pa11::Program& program,
	const pa11::BindingRecord& binding, bool member,
	abi_mangle::AbiTypedCase* fact_case)
{
	using namespace abi_mangle;
	using namespace pa11;
	if (!member) return;
	const TypeRecord& declared_type = program.types.Get(binding.type);
	AbiFactRecord qualifier;
	qualifier.set_kind(ABI_FACT_RECORD_FUNCTION);
	qualifier.function.kind = ABI_FUNCTION_RECORD_QUALIFIER;
	if ((declared_type.cv & CV_CONST) != 0)
		qualifier.function.qualifiers.push_back(
			ABI_FUNCTION_QUALIFIER_CONST);
	if ((declared_type.cv & CV_VOLATILE) != 0)
		qualifier.function.qualifiers.push_back(
			ABI_FUNCTION_QUALIFIER_VOLATILE);
	if (declared_type.ref_qualifier == FUNCTION_REF_LVALUE)
		qualifier.function.qualifiers.push_back(
			ABI_FUNCTION_QUALIFIER_LVALUE_REFERENCE);
	else if (declared_type.ref_qualifier == FUNCTION_REF_RVALUE)
		qualifier.function.qualifiers.push_back(
			ABI_FUNCTION_QUALIFIER_RVALUE_REFERENCE);
	if (!qualifier.function.qualifiers.empty())
		AppendTypedFact(fact_case, &qualifier);
}

std::string MangleFunction(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node,
	bool force_lifecycle_base_entry, abi_mangle::AbiMangleStats* stats,
	abi_mangle::AbiMangleContext* context)
{
	using namespace abi_mangle;
	using namespace pa11;
	const BindingRecord& binding = program.bindings[node.binding];
	if (binding.assembly_name != 0)
	{
		if (stats) ++stats->external_assembly_names;
		return program.names.Get(binding.assembly_name);
	}
	if (binding.owner == program.GlobalScope() &&
		program.names.Get(binding.name) == "main") return std::string();
	if (binding.builtin_function != BUILTIN_FUNCTION_NONE)
	{
		if (stats) ++stats->external_builtin_runtime_names;
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_NEW)
			return "cppgm_builtin_operator_new";
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_DELETE)
			return "cppgm_builtin_operator_delete";
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY)
			return "cppgm_builtin_operator_new_array";
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY)
			return "cppgm_builtin_operator_delete_array";
		if (binding.builtin_function == BUILTIN_FUNCTION_ABORT)
			return "abort";
		return "cppgm_builtin_" + program.names.Get(binding.name).substr(10);
	}
	if (binding.language_linkage == LANGUAGE_LINKAGE_C)
	{
		if (stats) ++stats->external_c_function_names;
		return program.names.Get(binding.name);
	}
	std::unique_ptr<AbiMangleContext> local_context;
	if (!context)
	{
		local_context.reset(new AbiMangleContext(stats));
		context = local_context.get();
	}
	const EntityRecord* lambda = binding.member_owner == kNoEntity ? 0 :
		&program.entities[binding.member_owner];
	if (lambda && lambda->lambda_closure &&
		binding.operator_kind == OPERATOR_CALL)
		return MangleLambdaCallOperator(
			program, binding, *lambda, stats, context);
	AbiTypedCase fact_case;
	AbiFactBuilder facts(program, fact_case, context, stats);
	const FunctionTemplateAbiRecipe* recipe = 0;
	if (binding.function_template_abi_recipe != kNoFunctionTemplateAbiRecipe)
	{
		if (binding.function_template_abi_recipe >=
			program.function_template_abi_recipes.size())
			throw std::logic_error("invalid function template ABI recipe");
		recipe = &program.function_template_abi_recipes[
			binding.function_template_abi_recipe];
		if (!program.types.IsFunction(recipe->function_type))
			throw std::logic_error("function template ABI recipe is not callable");
	}
	const bool structured_local_owner = binding.member_owner != kNoEntity &&
		program.entities[binding.member_owner].local_context != kNoBinding;
	const bool tagged_class_owner = binding.member_owner != kNoEntity &&
		ClassOwnerHasAbiTags(program, binding.member_owner);
	const bool nested_specialized_class_owner =
		binding.member_owner != kNoEntity &&
		ClassOwnerHasSpecializedAncestor(program, binding.member_owner);
	const bool structured_class_owner = binding.member_owner != kNoEntity &&
		!structured_local_owner && !tagged_class_owner &&
		!nested_specialized_class_owner &&
		IsClassTemplateSpecialization(program.entities[binding.member_owner]);
	const bool structured_owner =
		structured_local_owner || structured_class_owner;
	const bool typed_class_owner = binding.member_owner != kNoEntity &&
		!structured_local_owner &&
		(tagged_class_owner || nested_specialized_class_owner ||
		 ClassOwnerIsNested(program, binding.member_owner));
	AbiFactRecord target;
	target.set_kind(ABI_FACT_RECORD_TARGET);
	target.target.kind = ABI_TARGET_FACT_FUNCTION;
	target.target.internal_linkage =
		binding.storage_class == STORAGE_CLASS_STATIC &&
		!binding.unnamed_namespace_linkage;
	target.target.function.kind = structured_owner ?
		ABI_FUNCTION_TARGET_ENCODING : typed_class_owner ?
			ABI_FUNCTION_TARGET_MEMBER : ABI_FUNCTION_TARGET_PATH;
	if (!structured_owner)
	{
		if (binding.lambda_invocation)
			facts.SetGeneratedPath(&target.target.function, binding.owner,
				pa22_lambda_presentation::RenderLambdaEntityTerminal(
					program, binding.lambda_invocation_owner));
		else facts.SetPath(
			&target.target.function, binding.owner, binding.name);
	}
	if (typed_class_owner)
	{
		const EntityRecord& owner = program.entities[binding.member_owner];
		target.target.function.owner_type = facts.MakeType(owner.type);
		facts.SetSourceName(&target.target.function, binding.name);
	}
	AppendTypedFact(&fact_case, &target);
	AppendFunctionAbiTagFacts(program, binding, context, &fact_case);
	if (structured_local_owner)
		AppendLocalFunctionOwner(
			program, binding, &facts, context, &fact_case);
	if (structured_class_owner &&
		!AppendClassTemplateOwner(program, binding, &facts, &fact_case, true))
		throw std::logic_error("class template ABI owner was lost");
	const TypeRecord& function_type = program.types.Get(node.type);
	const TypeId* parameters = program.types.Parameters(node.type);
	AppendFunctionTemplateArgumentsAndResult(program, binding, function_type,
		recipe, &facts, &fact_case);
	const bool member = binding.member_owner != kNoEntity && !binding.static_member_function;
	AppendMemberFunctionQualifiers(program, binding, member, &fact_case);
	const AbiTerminalKind operator_terminal =
		OperatorTerminal(binding.operator_kind, member,
			program.types.Get(binding.type).parameter_count);
	if (structured_owner && binding.operator_kind == OPERATOR_NONE &&
		!binding.conversion_function && !binding.constructor && !binding.destructor)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = structured_local_owner ?
			ABI_FUNCTION_RECORD_TERMINAL_SOURCE :
			ABI_FUNCTION_RECORD_NAME_SOURCE;
		facts.SetSourceName(&terminal.function, binding.name);
		AppendTypedFact(&fact_case, &terminal);
	}
	if (binding.operator_kind == OPERATOR_LITERAL ||
		operator_terminal != ABI_TERMINAL_NONE)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_OPERATOR_TERMINAL;
		if (binding.operator_kind == OPERATOR_LITERAL)
		{
			terminal.function.terminal_code = ABI_TERMINAL_LITERAL;
			facts.SetSourceName(
				&terminal.function, binding.operator_literal_suffix);
		}
		else terminal.function.terminal_code = operator_terminal;
		AppendTypedFact(&fact_case, &terminal);
	}
	else if (binding.conversion_function)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_CONVERSION_TERMINAL;
		terminal.function.type = facts.MakeType(binding.conversion_target);
		AppendTypedFact(&fact_case, &terminal);
	}
	else if (binding.constructor)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_TERMINAL;
		terminal.function.terminal_code =
			binding.constructor_base_entry || force_lifecycle_base_entry ?
			ABI_TERMINAL_CONSTRUCTOR_BASE :
			ABI_TERMINAL_CONSTRUCTOR_COMPLETE;
		AppendTypedFact(&fact_case, &terminal);
	}
	else if (binding.destructor)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_TERMINAL;
		terminal.function.terminal_code = binding.destructor_base_entry ||
			force_lifecycle_base_entry ?
			ABI_TERMINAL_DESTRUCTOR_BASE :
			ABI_TERMINAL_DESTRUCTOR_COMPLETE;
		AppendTypedFact(&fact_case, &terminal);
	}
	const std::size_t first_parameter = member ? 1 : 0;
	const TypeRecord* recipe_function = recipe ?
		&program.types.Get(recipe->function_type) : 0;
	const TypeId* recipe_parameters = recipe_function &&
		recipe_function->parameter_count != 0 ?
		program.types.Parameters(recipe->function_type) : 0;
	const auto append_parameter = [&](TypeId parameter_type,
		const FunctionTemplateAbiRecipe* parameter_recipe,
		FunctionTemplateAbiTypeId retained_type,
		bool pack_expansion)
	{
		AbiFactRecord parameter;
		parameter.set_kind(ABI_FACT_RECORD_FUNCTION);
		parameter.function.kind = ABI_FUNCTION_RECORD_PARAMETER;
		parameter.function.type = retained_type != kNoFunctionTemplateAbiType ?
			facts.MakeFunctionTemplateAbiType(retained_type, *parameter_recipe) :
			facts.MakeFunctionTemplateType(
				parameter_type, binding, parameter_recipe);
		if (pack_expansion)
		{
			AbiTypeModifier expansion;
			expansion.kind = ABI_TYPE_PACK_EXPANSION;
			parameter.function.type.modifiers.insert(
				parameter.function.type.modifiers.begin(), expansion);
		}
		AppendTypedFact(&fact_case, &parameter);
	};
	if (recipe_function)
	{
		for (std::size_t i = 0; i < recipe_function->parameter_count; ++i)
			append_parameter(recipe_parameters[i], recipe,
				facts.FunctionTemplateParameterAbiType(*recipe, i),
				recipe->function_parameter_pack &&
				i + 1 == recipe_function->parameter_count);
	}
	else
	{
		for (std::size_t i = first_parameter;
			i < function_type.parameter_count; ++i)
			append_parameter(parameters[i], 0,
				kNoFunctionTemplateAbiType, false);
	}
	if (function_type.variadic)
	{
		AbiFactRecord variadic;
		variadic.set_kind(ABI_FACT_RECORD_FUNCTION);
		variadic.function.kind = ABI_FUNCTION_RECORD_VARIADIC;
		AppendTypedFact(&fact_case, &variadic);
	}
	return MangleProductionCase(fact_case, stats, context);
}

std::string MangleVariable(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node,
	abi_mangle::AbiMangleStats* stats,
	abi_mangle::AbiMangleContext* context)
{
	using namespace abi_mangle;
	using namespace pa11;
	const BindingRecord& binding = program.bindings[node.binding];
	if (binding.language_linkage == LANGUAGE_LINKAGE_C &&
		binding.storage_class != STORAGE_CLASS_STATIC)
	{
		if (stats) ++stats->external_c_variable_names;
		return program.names.Get(binding.name);
	}
	// Linux global-namespace TLS objects retain their source spelling; wrapper
	// functions still use the corresponding Itanium TLS special name.
	if (binding.owner == program.GlobalScope() &&
		binding.member_owner == kNoEntity &&
		binding.thread_local_storage &&
		binding.storage_class != STORAGE_CLASS_STATIC &&
		!binding.unnamed_namespace_linkage &&
		!binding.variable_template_specialization &&
		binding.template_argument_count == 0)
	{
		if (stats) ++stats->external_global_tls_names;
		return program.names.Get(binding.name);
	}
	std::unique_ptr<AbiMangleContext> local_context;
	if (!context)
	{
		local_context.reset(new AbiMangleContext(stats));
		context = local_context.get();
	}
	AbiTypedCase fact_case;
	AbiFactBuilder facts(program, fact_case, context, stats);
	const bool tagged_class_owner = binding.member_owner != kNoEntity &&
		ClassOwnerHasAbiTags(program, binding.member_owner);
	const bool nested_specialized_class_owner =
		binding.member_owner != kNoEntity &&
		ClassOwnerHasSpecializedAncestor(program, binding.member_owner);
	const bool structured_class_owner = binding.member_owner != kNoEntity &&
		!tagged_class_owner && !nested_specialized_class_owner &&
		IsClassTemplateSpecialization(program.entities[binding.member_owner]);
	const bool typed_class_owner = binding.member_owner != kNoEntity &&
		(tagged_class_owner || nested_specialized_class_owner ||
		 ClassOwnerIsNested(program, binding.member_owner));
	const bool member_variable_template =
		(structured_class_owner || typed_class_owner) &&
		binding.variable_template_specialization;
	AbiFactRecord target;
	target.set_kind(ABI_FACT_RECORD_TARGET);
	target.target.kind = ABI_TARGET_FACT_VARIABLE;
	target.target.function.kind = structured_class_owner ?
		ABI_FUNCTION_TARGET_ENCODING : typed_class_owner ?
		ABI_FUNCTION_TARGET_MEMBER : ABI_FUNCTION_TARGET_PATH;
	target.target.internal_linkage =
		binding.storage_class == STORAGE_CLASS_STATIC &&
		binding.member_owner == kNoEntity &&
		!binding.unnamed_namespace_linkage;
	const bool needs_path =
		target.target.function.kind == ABI_FUNCTION_TARGET_PATH ||
		(target.target.function.kind == ABI_FUNCTION_TARGET_MEMBER &&
		 binding.template_argument_count != 0);
	if (needs_path)
	{
		if (binding.name == 0)
			throw std::logic_error("variable ABI target has no semantic name");
		facts.SetPath(&target.target.function, binding.owner, binding.name);
	}
	if (typed_class_owner)
	{
		const EntityRecord& owner = program.entities[binding.member_owner];
		target.target.function.owner_type = facts.MakeType(owner.type);
		facts.SetSourceName(&target.target.function, binding.name);
	}
	AppendTypedFact(&fact_case, &target);
	if (structured_class_owner)
	{
		if (!AppendClassTemplateOwner(
			program, binding, &facts, &fact_case, false))
			throw std::logic_error("class template ABI variable owner was lost");
		AbiFactRecord member;
		member.set_kind(ABI_FACT_RECORD_FUNCTION);
		member.function.kind = ABI_FUNCTION_RECORD_NAME_SOURCE;
		facts.SetSourceName(&member.function, binding.name);
		AppendTypedFact(&fact_case, &member);
	}
	if (member_variable_template && binding.template_argument_count != 0)
	{
		const std::size_t first = binding.template_argument_begin;
		const std::size_t count = binding.template_argument_count;
		if (first > program.template_arguments.size() ||
			count > program.template_arguments.size() - first)
			throw std::logic_error(
				"variable template argument range is invalid during mangling");
		for (std::size_t i = 0; i < count; ++i)
		{
			const std::size_t argument_id = facts.AddTemplateArgument(first + i);
			AbiFactRecord argument;
			argument.set_kind(ABI_FACT_RECORD_FUNCTION);
			argument.function.kind =
				ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT;
			argument.function.argument_refs.push_resolved(argument_id);
			AppendTypedFact(&fact_case, &argument);
		}
	}
	return MangleProductionCase(fact_case, stats, context);
}

std::string MangleThreadLocalWrapper(const pa11::Program& program,
	pa11::BindingId binding_id, pa11::NameId fallback_name,
	abi_mangle::AbiMangleStats* stats,
	abi_mangle::AbiMangleContext* context)
{
	using namespace abi_mangle;
	using namespace pa11;
	if (binding_id == kNoBinding || binding_id >= program.bindings.size())
		throw std::logic_error("invalid thread-local wrapper binding");
	const BindingRecord& binding = program.bindings[binding_id];
	std::unique_ptr<AbiMangleContext> local_context;
	if (!context)
	{
		local_context.reset(new AbiMangleContext(stats));
		context = local_context.get();
	}
	AbiTypedCase fact_case;
	AbiFactBuilder facts(program, fact_case, context, stats);
	AbiFactRecord target;
	target.set_kind(ABI_FACT_RECORD_TARGET);
	target.target.kind = ABI_TARGET_FACT_THREAD_LOCAL_WRAPPER;
	const NameId terminal = binding.name != 0 ? binding.name : fallback_name;
	if (terminal != 0 && !program.names.Get(terminal).empty())
		facts.SetPath(&target.target.function, binding.owner, terminal);
	if (target.target.function.resolved_path == ABI_NO_RESOLVED_REFERENCE)
		throw std::logic_error("thread-local wrapper has no semantic name (binding " +
			std::to_string(binding_id) + ", owner " +
			std::to_string(binding.owner) + ", terminal " +
			std::to_string(binding.name) + ", fallback " +
			std::to_string(fallback_name) + ")");
	AppendTypedFact(&fact_case, &target);
	return MangleProductionCase(fact_case, stats, context);
}

}
}
