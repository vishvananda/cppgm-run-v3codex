#include "pa12_semantic_detail.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

std::size_t AlignUp(std::size_t value, std::size_t alignment)
{
	if (alignment == 0 || (alignment & (alignment - 1)) != 0)
		throw std::runtime_error("invalid class member alignment");
	const std::size_t remainder = value & (alignment - 1);
	if (remainder == 0) return value;
	const std::size_t addition = alignment - remainder;
	if (value > std::numeric_limits<std::size_t>::max() - addition)
		throw std::runtime_error("class layout is too large");
	return value + addition;
}

}

TypeId SemanticAnalyzer::AnalyzeClass(NodeId node, ScopeId scope,
	const std::string& hint, bool elaborated)
{
	const NodeId key = FindChild(node, "class-key");
	if (key == kNoNode) throw std::runtime_error("class without class-key");
	const std::string key_text = PayloadSource(key);
	const NamedFlavor flavor = key_text == "struct" ? NAMED_STRUCT :
		key_text == "class" ? NAMED_CLASS :
		key_text == "union" ? NAMED_UNION : NAMED_NONE;
	if (flavor == NAMED_NONE) throw std::runtime_error("invalid class-key");
	std::string spelling = arena_->Payload(node);
	const bool anonymous_source = spelling.empty();
	if (spelling.empty() && !hint.empty())
	{
		++local_type_count_;
		spelling = "__local_type" + std::to_string(local_type_count_);
	}
	if (spelling.empty())
	{
		std::ostringstream generated;
		generated << "__anonymous_union_type__" << arena_->TokenFirst(node)
			<< '_' << arena_->TokenLast(node);
		spelling = generated.str();
	}
	const NamePath path = ParseNamePath(spelling);
	const NameId name = path.Last();
	const ScopeId owner = ResolveOwner(scope, path);
	if (owner == kNoScope) throw std::runtime_error("class owner not found");
	const LookupResult old = path.global || path.Size() > 1 ?
		program_->LookupDirect(owner, name, LOOKUP_TYPE) :
		(elaborated ? program_->LookupName(scope, name, LOOKUP_TYPE) :
		 program_->LookupDirect(owner, name, LOOKUP_TYPE));
	EntityId entity = kNoEntity;
	if (old.type != kNoType)
	{
		const TypeRecord named = program_->types.Get(
			program_->types.RemoveTopCv(old.type));
		if (named.kind != TYPE_NAMED)
			throw std::runtime_error("class redeclared as non-class");
		entity = named.entity;
		const NamedFlavor previous = program_->entities[entity].flavor;
		if ((previous == NAMED_UNION) != (flavor == NAMED_UNION) ||
			(previous != NAMED_STRUCT && previous != NAMED_CLASS &&
			 previous != NAMED_UNION))
			throw std::runtime_error("incompatible class redeclaration");
	}
	else
	{
		if ((path.global || path.Size() > 1) && elaborated)
			throw std::runtime_error("qualified class was not declared");
		const NameId entity_name = ScopePrefix(owner).empty() ? name :
			DisplayName(owner, name);
		entity = program_->NewEntity(entity_name, flavor, false,
			kNoType, owner, name);
		program_->SetTypeName(owner, name, program_->entities[entity].type);
	}
	const TypeId type = program_->entities[entity].type;
	if (entity_data_members_.size() <= entity)
		entity_data_members_.resize(static_cast<std::size_t>(entity) + 1);
	if (entity_constructors_.size() <= entity)
		entity_constructors_.resize(static_cast<std::size_t>(entity) + 1);
	if (implicit_constructor_by_entity_.size() <= entity)
		implicit_constructor_by_entity_.resize(
			static_cast<std::size_t>(entity) + 1, kNoBinding);
	if (entity_destructor_by_entity_.size() <= entity)
		entity_destructor_by_entity_.resize(
			static_cast<std::size_t>(entity) + 1, kNoBinding);
	if (old.type == kNoType && arena_->Payload(node).size() != 0)
		program_->AddBinding(owner, BIND_TYPE, name, type, false, 0, flavor);
	if ((arena_->Flags(node) & SYNTAX_FLAG_DEFINITION) != 0)
	{
		if (program_->entities[entity].complete)
			throw std::runtime_error("duplicate class definition");
		const NodeId base_clause = FindChild(node, "base-clause");
		if (base_clause != kNoNode)
		{
			NodeId base_specifier = kNoNode;
			for (std::uint32_t edge = arena_->FirstEdge(base_clause);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
			{
				const NodeId candidate = arena_->EdgeChild(edge);
				if (!arena_->IsTag(candidate, "base-specifier")) continue;
				if (base_specifier != kNoNode)
					throw std::runtime_error(
						"multiple inheritance is outside PA16");
				base_specifier = candidate;
			}
			if (base_specifier == kNoNode)
				throw std::runtime_error("base clause has no base type");
			const NodeId base_name = FindChild(base_specifier, "base-name");
			if (base_name == kNoNode)
				throw std::runtime_error("base specifier has no base name");
			const LookupResult base_lookup = LookupSpelling(scope,
				arena_->Payload(base_name), LOOKUP_TYPE);
			const EntityId base = EntityOf(base_lookup.type);
			if (base == kNoEntity || !program_->entities[base].complete ||
				program_->entities[base].flavor == NAMED_UNION)
				throw std::runtime_error(
					"direct base must name a complete non-union class");
			if (base_lookup.type_declaration != kNoBinding &&
				!CanAccessMember(base_lookup.type_declaration,
					base_lookup.naming_class))
				throw std::runtime_error("inaccessible direct base type");
			AccessKind base_access = flavor == NAMED_CLASS ?
				ACCESS_PRIVATE : ACCESS_PUBLIC;
			const NodeId access = FindChild(base_specifier, "access-specifier");
			if (access != kNoNode)
			{
				const std::string access_text = PayloadSource(access);
				base_access = access_text == "private" ? ACCESS_PRIVATE :
					access_text == "protected" ? ACCESS_PROTECTED : ACCESS_PUBLIC;
			}
			program_->SetDirectBase(entity, base, base_access);
		}
		ScopeId member_scope = program_->entities[entity].member_scope;
		if (member_scope == kNoScope)
		{
			const std::string prefix =
				program_->names.Get(DisplayName(owner, name)) + "::";
			member_scope = NewScope(owner, SCOPE_CLASS, name,
				program_->names.Intern(prefix));
			program_->SetEntityScope(entity, member_scope);
			program_->SetTypeName(member_scope, name, type);
			const BindingId injected = program_->AddBinding(member_scope,
				BIND_TYPE, name, type, false, 0, flavor);
			program_->bindings[injected].member_owner = entity;
			program_->bindings[injected].access = ACCESS_PUBLIC;
		}
		// The stable class scope owns indexed field/function identities even
		// though class declarations are not part of the PA12 output view.
		const EntityId previous_class_context = current_class_context_;
		current_class_context_ = entity;
		AccessKind member_access = flavor == NAMED_CLASS ?
			ACCESS_PRIVATE : ACCESS_PUBLIC;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId member = arena_->EdgeChild(edge);
			if (arena_->IsTag(member, "access-specifier"))
			{
				const std::string access = PayloadSource(member);
				member_access = access == "private" ? ACCESS_PRIVATE :
					access == "protected" ? ACCESS_PROTECTED : ACCESS_PUBLIC;
				continue;
			}
			if (arena_->IsTag(member, "simple-declaration") ||
				arena_->IsTag(member, "function-definition"))
				AnalyzeClassMember(member, member_scope, type, member_access);
			else if (arena_->IsTag(member, "special-member-declaration") ||
				arena_->IsTag(member, "special-member-definition"))
				AnalyzeSpecialMember(member, member_scope, type, member_access);
		}
		CompleteClassLayout(entity);
		if (!program_->entities[entity].has_user_declared_constructor &&
			program_->entities[entity].default_constructible)
			EnsureImplicitConstructor(entity);
		if (!program_->entities[entity].has_user_declared_destructor)
			EnsureImplicitDestructor(entity);
		current_class_context_ = previous_class_context;
		program_->entities[entity].complete = true;
	}
	(void)anonymous_source;
	return type;
}

void SemanticAnalyzer::CompleteClassLayout(EntityId entity)
{
	EntityRecord& owner = program_->entities[entity];
	if (owner.layout_complete) return;
	++class_layouts_;
	std::size_t size = 0;
	std::size_t alignment = 1;
	const EntityRecord* base = 0;
	if (!owner.has_user_declared_destructor)
	{
		owner.destructible = true;
		owner.trivial_destructor = true;
	}
	if (owner.direct_base != kNoEntity)
	{
		base = &program_->entities[owner.direct_base];
		if (!base->layout_complete)
			throw std::runtime_error("direct base layout is incomplete");
		size = static_cast<std::size_t>(base->object_size);
		alignment = static_cast<std::size_t>(base->object_alignment);
		if (!base->destructible) owner.destructible = false;
		if (!base->trivial_destructor) owner.trivial_destructor = false;
		const BindingId destructor = DestructorForType(base->type);
		if (destructor == kNoBinding ||
			!CanAccessMember(destructor, owner.direct_base))
			owner.destructible = false;
	}
	const bool is_union = owner.flavor == NAMED_UNION;
	bool defaulted_destructor = !owner.has_user_declared_destructor;
	if (owner.has_user_declared_destructor)
	{
		const BindingId destructor = DestructorForType(owner.type);
		defaulted_destructor = destructor != kNoBinding &&
			GetFunction(destructor).defaulted_destructor;
	}
	const std::vector<BindingId>& members = entity_data_members_[entity];
	const bool implicit_default_constructor =
		!owner.has_user_declared_constructor;
	owner.is_aggregate = !owner.has_user_provided_constructor &&
		!owner.has_direct_base;
	if (implicit_default_constructor)
	{
		owner.default_constructible = true;
		owner.trivial_default_constructor = true;
		if (base)
		{
			if (!base->default_constructible)
				owner.default_constructible = false;
			if (!base->trivial_default_constructor)
				owner.trivial_default_constructor = false;
		}
	}
	for (std::size_t i = 0; i < members.size(); ++i)
	{
		++class_layout_member_visits_;
		BindingRecord& member = program_->bindings[members[i]];
		if (member.has_default_member_initializer ||
			member.access != ACCESS_PUBLIC)
			owner.is_aggregate = false;
		const std::size_t member_size = program_->SizeOf(member.type);
		const std::size_t member_alignment = program_->AlignOf(member.type);
		alignment = std::max(alignment, member_alignment);
		if (is_union)
		{
			member.member_offset = 0;
			size = std::max(size, member_size);
		}
		else
		{
			size = AlignUp(size, member_alignment);
			member.member_offset = size;
			if (size > std::numeric_limits<std::size_t>::max() - member_size)
				throw std::runtime_error("class layout is too large");
			size += member_size;
		}
		if (!implicit_default_constructor) continue;
		if (member.has_default_member_initializer)
		{
			owner.trivial_default_constructor = false;
			continue;
		}
		TypeId member_type = member.type;
		const TypeRecord* member_record = &program_->types.Get(member_type);
		while (member_record->kind == TYPE_ARRAY)
		{
			member_type = member_record->child;
			member_record = &program_->types.Get(member_type);
		}
		if (member_record->kind == TYPE_LVALUE_REFERENCE ||
			member_record->kind == TYPE_RVALUE_REFERENCE)
		{
			owner.default_constructible = false;
			continue;
		}
		const bool const_member = member_record->kind == TYPE_QUALIFIED &&
			(member_record->cv & CV_CONST) != 0;
		if (const_member)
		{
			member_type = member_record->child;
			member_record = &program_->types.Get(member_type);
		}
		if (member_record->kind != TYPE_NAMED)
		{
			if (const_member) owner.default_constructible = false;
			continue;
		}
		const EntityRecord& subobject =
			program_->entities[member_record->entity];
		if (subobject.flavor == NAMED_ENUM ||
			subobject.flavor == NAMED_ENUM_CLASS)
		{
			if (const_member) owner.default_constructible = false;
			continue;
		}
		if (!subobject.default_constructible)
			owner.default_constructible = false;
		if (!subobject.trivial_default_constructor)
			owner.trivial_default_constructor = false;
	}
	for (std::size_t i = 0; i < members.size(); ++i)
	{
		TypeId member_type = program_->bindings[members[i]].type;
		const TypeRecord* member_record = &program_->types.Get(member_type);
		while (member_record->kind == TYPE_ARRAY ||
			member_record->kind == TYPE_QUALIFIED)
		{
			member_type = member_record->child;
			member_record = &program_->types.Get(member_type);
		}
		if (member_record->kind != TYPE_NAMED) continue;
		const EntityRecord& subobject =
			program_->entities[member_record->entity];
		if (subobject.flavor != NAMED_STRUCT &&
			subobject.flavor != NAMED_CLASS &&
			subobject.flavor != NAMED_UNION) continue;
		if (is_union)
		{
			if (defaulted_destructor && !subobject.trivial_destructor)
				owner.destructible = false;
			if (!subobject.trivial_destructor)
				owner.trivial_destructor = false;
			continue;
		}
		if (!subobject.destructible) owner.destructible = false;
		if (!subobject.trivial_destructor) owner.trivial_destructor = false;
		const BindingId destructor = DestructorForType(member_type);
		if (destructor == kNoBinding ||
			!CanAccessMember(destructor, member_record->entity))
			owner.destructible = false;
	}
	if (size == 0) size = 1;
	size = AlignUp(size, alignment);
	owner.object_size = size;
	owner.object_alignment = alignment;
	owner.layout_complete = true;
}

BindingId SemanticAnalyzer::EnsureImplicitConstructor(EntityId entity)
{
	if (entity >= implicit_constructor_by_entity_.size())
		implicit_constructor_by_entity_.resize(
			static_cast<std::size_t>(entity) + 1, kNoBinding);
	if (implicit_constructor_by_entity_[entity] != kNoBinding)
		return implicit_constructor_by_entity_[entity];
	EntityRecord& owner = program_->entities[entity];
	if (!owner.default_constructible)
		throw std::runtime_error("class has no usable implicit constructor");
	const NameId name = owner.identity_name;
	const TypeId type = program_->types.Function(
		program_->types.Fundamental(FUND_VOID), std::vector<TypeId>(), false);
	const BindingId constructor = DeclareFunction(owner.member_scope, name,
		type, std::vector<ParameterInfo>(), true, false, STORAGE_CLASS_NONE,
		LANGUAGE_LINKAGE_CPP, owner.trivial_default_constructor);
	BindingRecord& binding = program_->bindings[constructor];
	binding.member_owner = entity;
	binding.constructor = true;
	FunctionInfo& info = GetMutableFunction(constructor);
	info.member_owner = owner.type;
	info.constructor = true;
	info.implicit_constructor = true;
	info.deferred = true;
	implicit_constructor_by_entity_[entity] = constructor;
	if (entity_constructors_.size() <= entity)
		entity_constructors_.resize(static_cast<std::size_t>(entity) + 1);
	entity_constructors_[entity].push_back(constructor);
	return constructor;
}

BindingId SemanticAnalyzer::EnsureImplicitDestructor(EntityId entity)
{
	if (entity_destructor_by_entity_.size() <= entity)
		entity_destructor_by_entity_.resize(
			static_cast<std::size_t>(entity) + 1, kNoBinding);
	if (entity_destructor_by_entity_[entity] != kNoBinding)
		return entity_destructor_by_entity_[entity];
	EntityRecord& owner = program_->entities[entity];
	const std::string leaf = program_->names.Get(owner.identity_name);
	const NameId name = program_->names.Intern("~" + leaf);
	const TypeId type = program_->types.Function(
		program_->types.Fundamental(FUND_VOID), std::vector<TypeId>(), false);
	const BindingId destructor = DeclareFunction(owner.member_scope, name,
		type, std::vector<ParameterInfo>(), owner.destructible, false,
		STORAGE_CLASS_NONE,
		LANGUAGE_LINKAGE_CPP, owner.trivial_destructor);
	BindingRecord& binding = program_->bindings[destructor];
	binding.member_owner = entity;
	binding.destructor = true;
	FunctionInfo& info = GetMutableFunction(destructor);
	info.member_owner = owner.type;
	info.destructor = true;
	info.implicit_destructor = true;
	info.deleted_destructor = !owner.destructible;
	info.deferred = owner.destructible;
	entity_destructor_by_entity_[entity] = destructor;
	return destructor;
}

EntityId SemanticAnalyzer::DestructedEntity(TypeId type) const
{
	const TypeRecord& initial = program_->types.Get(type);
	if (initial.kind == TYPE_LVALUE_REFERENCE ||
		initial.kind == TYPE_RVALUE_REFERENCE)
		return kNoEntity;
	const TypeRecord* record = &program_->types.Get(type);
	while (record->kind == TYPE_ARRAY || record->kind == TYPE_QUALIFIED)
	{
		type = record->child;
		record = &program_->types.Get(type);
	}
	if (record->kind != TYPE_NAMED) return kNoEntity;
	const NamedFlavor flavor = program_->entities[record->entity].flavor;
	return flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
		flavor == NAMED_UNION ? record->entity : kNoEntity;
}

BindingId SemanticAnalyzer::DestructorForType(TypeId type) const
{
	const EntityId entity = DestructedEntity(type);
	if (entity == kNoEntity || entity >= entity_destructor_by_entity_.size())
		return kNoBinding;
	return entity_destructor_by_entity_[entity];
}

const std::vector<BindingId>& SemanticAnalyzer::ConstructorCandidates(
	EntityId entity) const
{
	if (entity >= entity_constructors_.size())
		throw std::logic_error("class is missing its constructor index");
	return entity_constructors_[entity];
}

EntityId SemanticAnalyzer::EntityOf(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	return record.kind == TYPE_NAMED ? record.entity : kNoEntity;
}

void SemanticAnalyzer::AnalyzeClassMember(NodeId node, ScopeId scope,
	TypeId owner_type, AccessKind access)
{
	const NodeId specifiers = FindChild(node, "decl-specifier-seq");
	if (specifiers == kNoNode) return;
	const SpecInfo spec = BuildSpecifiers(specifiers, scope, std::string(), true);
	if (arena_->IsTag(node, "function-definition"))
	{
		if (spec.thread_local_storage)
			throw std::runtime_error("thread_local member function");
		const NodeId declarator = FindChild(node, "declarator");
		DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, scope);
		const BindingId function = DeclareFunction(scope, parsed.name,
			parsed.type, parsed.parameters, true, false, STORAGE_CLASS_NONE,
			current_language_linkage_, IsNonthrowing(declarator, scope));
		FunctionInfo& info = GetMutableFunction(function);
		const EntityId owner_entity = EntityOf(owner_type);
		BindingRecord& binding = program_->bindings[function];
		binding.member_owner = owner_entity;
		binding.access = access;
		binding.static_member_function =
			spec.storage_class == STORAGE_CLASS_STATIC;
		if (!binding.static_member_function) info.member_owner = owner_type;
		info.definition_body =
			FindChild(node, "compound-statement");
		info.deferred = true;
		return;
	}
	const NodeId list = FindChild(node, "init-declarator-list");
	if (list == kNoNode) return;
	for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId item = arena_->EdgeChild(edge);
		const NodeId declarator = FindChild(item, "declarator");
		if (declarator == kNoNode) continue;
		const DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, scope);
		if (spec.is_typedef)
		{
			const BindingId alias = program_->AddBinding(scope, BIND_TYPE_ALIAS,
				parsed.name, parsed.type);
			program_->bindings[alias].member_owner = EntityOf(owner_type);
			program_->bindings[alias].access = access;
			continue;
		}
		if (program_->types.IsFunction(parsed.type))
		{
			if (spec.thread_local_storage)
				throw std::runtime_error("thread_local member function");
			const BindingId function = DeclareFunction(scope, parsed.name,
				parsed.type, parsed.parameters, false, false, STORAGE_CLASS_NONE,
				current_language_linkage_, IsNonthrowing(declarator, scope));
			BindingRecord& binding = program_->bindings[function];
			binding.member_owner = EntityOf(owner_type);
			binding.access = access;
			binding.static_member_function =
				spec.storage_class == STORAGE_CLASS_STATIC;
			if (!binding.static_member_function)
				GetMutableFunction(function).member_owner = owner_type;
		}
		else
		{
			if (spec.thread_local_storage &&
				spec.storage_class != STORAGE_CLASS_STATIC)
				throw std::runtime_error(
					"thread_local class member must be static");
			if (spec.is_constexpr &&
				spec.storage_class != STORAGE_CLASS_STATIC)
				throw std::runtime_error(
					"constexpr class data member must be static");
			TypeId member_type = parsed.type;
			if (spec.is_constexpr)
				member_type = program_->types.Qualify(member_type, CV_CONST);
			const LookupResult occupied =
				program_->LookupDirect(scope, parsed.name, LOOKUP_ORDINARY);
			if (parsed.name != 0 && occupied.ordinary != kNoBinding)
				throw std::runtime_error("duplicate or conflicting class member");
			const BindingId member = program_->AddBinding(scope, BIND_VARIABLE,
				parsed.name, member_type, false, 0, NAMED_NONE, 0, kNoBinding,
				false);
			BindingRecord& binding = program_->bindings[member];
			binding.storage_class = spec.storage_class;
			binding.thread_local_storage = spec.thread_local_storage;
			binding.member_owner = EntityOf(owner_type);
			binding.access = access;
			binding.non_static_data_member =
				spec.storage_class == STORAGE_CLASS_NONE;
			binding.has_default_member_initializer =
				binding.non_static_data_member &&
				FindChild(item, "initializer") != kNoNode;
			if (!binding.non_static_data_member &&
				FindChild(item, "initializer") != kNoNode)
			{
				if (!spec.is_constexpr &&
					!(IsConst(binding.type) && IsIntegral(binding.type, true)))
					throw std::runtime_error(
						"invalid in-class static data member initializer");
				const NodeId initializer = FirstSemanticChild(
					FindChild(item, "initializer"));
				const ExpressionInfo value = AnalyzeExpression(initializer,
					scope, binding.type);
				if (!value.constant)
					throw std::runtime_error(
						"nonconstant in-class static data member initializer");
				binding.constant = true;
				binding.value = value.value;
			}
			else if (!binding.non_static_data_member && spec.is_constexpr)
				throw std::runtime_error(
					"constexpr static data member requires initializer");
			if (member_initializer_by_binding_.size() <= member)
				member_initializer_by_binding_.resize(
					static_cast<std::size_t>(member) + 1, kNoNode);
			if (binding.has_default_member_initializer)
				member_initializer_by_binding_[member] =
					FindChild(item, "initializer");
			const EntityId entity = EntityOf(owner_type);
			if (binding.non_static_data_member && entity_data_members_.size() <= entity)
				entity_data_members_.resize(static_cast<std::size_t>(entity) + 1);
			if (binding.non_static_data_member)
			{
				binding.member_ordinal = static_cast<std::uint32_t>(
					entity_data_members_[entity].size());
				entity_data_members_[entity].push_back(member);
			}
		}
	}
}

void SemanticAnalyzer::PublishVariableDeclarationFacts(BindingId binding,
	ScopeId declaration_scope, NameId name, TypeId type,
	const SpecInfo& spec, bool local)
{
	BindingRecord& record = program_->bindings[binding];
	record.language_linkage = current_language_linkage_;
	record.storage_class = spec.storage_class;
	record.thread_local_storage = spec.thread_local_storage;
	const TypeRecord top_type = program_->types.Get(type);
	if (!local && record.storage_class == STORAGE_CLASS_NONE &&
		top_type.kind == TYPE_QUALIFIED && (top_type.cv & CV_CONST) != 0)
		record.storage_class = STORAGE_CLASS_STATIC;
	BindingRecord& canonical = program_->bindings[record.canonical];
	canonical.language_linkage = record.language_linkage;
	if (canonical.storage_class == STORAGE_CLASS_NONE ||
		record.storage_class == STORAGE_CLASS_STATIC)
		canonical.storage_class = record.storage_class;
	if (record.canonical != binding && canonical.thread_local_storage !=
		record.thread_local_storage)
		throw std::runtime_error("thread_local redeclaration mismatch");
	canonical.thread_local_storage = record.thread_local_storage;
	if (record.canonical != binding && canonical.constant)
	{
		record.constant = true;
		record.value = canonical.value;
	}
	if (!local) record.qualified_name = DisplayName(declaration_scope, name);
}

void SemanticAnalyzer::AnalyzeSpecialMember(NodeId node, ScopeId scope,
	TypeId owner_type, AccessKind access)
{
	const EntityId entity = EntityOf(owner_type);
	if (entity == kNoEntity) throw std::logic_error("special member has no class");
	const std::string special_name = arena_->Payload(node);
	const std::string class_name =
		program_->names.Get(program_->entities[entity].identity_name);
	if (special_name == "~" + class_name)
	{
		EntityRecord& class_record = program_->entities[entity];
		class_record.has_user_declared_destructor = true;
		const NodeId declarator = FindChild(node, "declarator");
		if (declarator == kNoNode)
			throw std::runtime_error("destructor is missing its declarator");
		const DeclaratorInfo parsed = BuildDeclarator(declarator,
			program_->types.Fundamental(FUND_VOID), scope);
		if (!program_->types.IsFunction(parsed.type) ||
			!parsed.parameters.empty())
			throw std::runtime_error("destructor must have no parameters");
		const NodeId initializer = FindChild(node, "initializer");
		const NodeId special = initializer == kNoNode ? kNoNode :
			FindChild(initializer, "special-initializer");
		const bool defaulted = special != kNoNode &&
			arena_->Payload(special) == "default";
		const bool deleted = special != kNoNode &&
			arena_->Payload(special) == "delete";
		const bool source_definition =
			arena_->IsTag(node, "special-member-definition");
		const BindingId destructor = DeclareFunction(scope, parsed.name,
			parsed.type, parsed.parameters, source_definition || defaulted,
			false, STORAGE_CLASS_NONE, current_language_linkage_,
			IsNonthrowing(declarator, scope));
		BindingRecord& binding = program_->bindings[destructor];
		binding.member_owner = entity;
		binding.access = access;
		binding.destructor = true;
		FunctionInfo& info = GetMutableFunction(destructor);
		info.member_owner = owner_type;
		info.destructor = true;
		info.defaulted_destructor =
			info.defaulted_destructor || defaulted;
		info.deleted_destructor = info.deleted_destructor || deleted;
		if (source_definition)
			info.definition_body = FindChild(node, "compound-statement");
		info.deferred = !info.deleted_destructor;
		if (entity_destructor_by_entity_.size() <= entity)
			entity_destructor_by_entity_.resize(
				static_cast<std::size_t>(entity) + 1, kNoBinding);
		if (entity_destructor_by_entity_[entity] != kNoBinding &&
			program_->bindings[entity_destructor_by_entity_[entity]].canonical !=
				binding.canonical)
			throw std::runtime_error("class has multiple destructors");
		entity_destructor_by_entity_[entity] = destructor;
		class_record.destructible = !info.deleted_destructor;
		class_record.trivial_destructor = defaulted && !deleted;
		return;
	}
	if (special_name != class_name) return;

	EntityRecord& class_record = program_->entities[entity];
	class_record.has_user_declared_constructor = true;
	const NodeId declarator = FindChild(node, "declarator");
	if (declarator == kNoNode)
		throw std::runtime_error("constructor is missing its declarator");
	const DeclaratorInfo parsed = BuildDeclarator(declarator,
		program_->types.Fundamental(FUND_VOID), scope);
	if (!program_->types.IsFunction(parsed.type))
		throw std::runtime_error("constructor declarator is not a function");
	const NodeId initializer = FindChild(node, "initializer");
	const NodeId special = initializer == kNoNode ? kNoNode :
		FindChild(initializer, "special-initializer");
	const bool defaulted = special != kNoNode &&
		arena_->Payload(special) == "default";
	const bool deleted = special != kNoNode &&
		arena_->Payload(special) == "delete";
	const bool source_definition =
		arena_->IsTag(node, "special-member-definition");
	const bool definition = source_definition || defaulted;
	const BindingId constructor = DeclareFunction(scope, parsed.name,
		parsed.type, parsed.parameters, definition, false, STORAGE_CLASS_NONE,
		current_language_linkage_, IsNonthrowing(declarator, scope));
	BindingRecord& binding = program_->bindings[constructor];
	binding.member_owner = entity;
	binding.access = access;
	binding.constructor = true;
	FunctionInfo& info = GetMutableFunction(constructor);
	info.member_owner = owner_type;
	info.constructor = true;
	info.defaulted_constructor = info.defaulted_constructor || defaulted;
	info.deleted_constructor = info.deleted_constructor || deleted;
	const NodeId specifiers = FindChild(node, "member-specifiers");
	if (specifiers != kNoNode)
		for (std::uint32_t edge = arena_->FirstEdge(specifiers); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
			if (PayloadSource(arena_->EdgeChild(edge)) == "explicit")
				info.explicit_constructor = true;
	if (source_definition)
	{
		info.definition_body = FindChild(node, "compound-statement");
		info.constructor_initializer = FindChild(node, "ctor-initializer");
	}
	info.deferred = !info.deleted_constructor;

	if (entity_constructors_.size() <= entity)
		entity_constructors_.resize(static_cast<std::size_t>(entity) + 1);
	std::vector<BindingId>& constructors = entity_constructors_[entity];
	if (std::find(constructors.begin(), constructors.end(), constructor) ==
		constructors.end()) constructors.push_back(constructor);
	std::size_t required = info.parameters.size();
	while (required != 0 &&
		info.parameters[required - 1].default_argument != kNoNode) --required;
	if (!info.deleted_constructor && required == 0)
		class_record.default_constructible = true;
	class_record.has_user_provided_constructor =
		class_record.has_user_provided_constructor ||
		(source_definition || (!defaulted && !deleted));
}

TypeId SemanticAnalyzer::AnalyzeEnum(NodeId node, ScopeId scope,
	const std::string& hint, bool elaborated)
{
	std::string spelling = arena_->Payload(node);
	if (spelling.empty()) spelling = hint;
	if (spelling.empty())
	{
		++anonymous_enum_count_;
		spelling = "__anonymous_enum" +
			std::to_string(anonymous_enum_count_);
	}
	const bool scoped = FindChild(node, "enum-key") != kNoNode;
	const NamedFlavor flavor = scoped ? NAMED_ENUM_CLASS : NAMED_ENUM;
	const NamePath path = ParseNamePath(spelling);
	const NameId name = path.Last();
	const ScopeId owner = ResolveOwner(scope, path);
	if (owner == kNoScope) throw std::runtime_error("enum owner not found");
	const NodeId underlying_node = FindChild(node, "type-id");
	TypeId underlying = underlying_node == kNoNode ?
		program_->types.Fundamental(FUND_INT) :
		BuildTypeId(underlying_node, owner);
	const LookupResult old = path.global || path.Size() > 1 ?
		program_->LookupDirect(owner, name, LOOKUP_TYPE) :
		(elaborated ? program_->LookupName(scope, name, LOOKUP_TYPE) :
		 program_->LookupDirect(owner, name, LOOKUP_TYPE));
	if (elaborated)
	{
		if (old.type == kNoType) throw std::runtime_error("unknown enum type");
		return old.type;
	}
	EntityId entity = kNoEntity;
	if (old.type != kNoType)
	{
		const TypeRecord named = program_->types.Get(
			program_->types.RemoveTopCv(old.type));
		if (named.kind != TYPE_NAMED)
			throw std::runtime_error("enum redeclared as non-enum");
		entity = named.entity;
		if (program_->entities[entity].flavor != flavor)
			throw std::runtime_error("incompatible enum redeclaration");
	}
	else
	{
		entity = program_->NewEntity(name, flavor, true, underlying, owner);
		program_->SetTypeName(owner, name, program_->entities[entity].type);
		if (arena_->Payload(node).size() != 0)
			program_->AddBinding(owner, BIND_TYPE, name,
				program_->entities[entity].type, false, 0, flavor);
	}
	const TypeId type = program_->entities[entity].type;
	ScopeId value_scope = owner;
	if (scoped)
	{
		value_scope = program_->entities[entity].member_scope;
		if (value_scope == kNoScope)
		{
			const std::string prefix =
				program_->names.Get(DisplayName(owner, name)) + "::";
			value_scope = NewScope(owner, SCOPE_ENUM, name,
				program_->names.Intern(prefix));
			program_->SetEntityScope(entity, value_scope);
		}
	}
	std::int64_t next = 0;
	std::int64_t minimum = 0;
	std::int64_t maximum = 0;
	std::vector<BindingId> enumerators;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId enumerator = arena_->EdgeChild(edge);
		if (!arena_->IsTag(enumerator, "enumerator")) continue;
		const NodeId initializer = FirstSemanticChild(enumerator);
		std::int64_t value = next;
		if (initializer != kNoNode)
		{
			const ExpressionInfo expression =
				AnalyzeExpression(initializer, value_scope);
			if (!expression.constant)
				throw std::runtime_error("nonconstant enumerator");
			value = expression.value;
		}
		const NameId enumerator_name =
			program_->names.Intern(arena_->Payload(enumerator));
		const BindingId binding = program_->AddBinding(value_scope,
			BIND_ENUMERATOR, enumerator_name, underlying, true, value);
		enumerators.push_back(binding);
		if (value < minimum) minimum = value;
		if (value > maximum) maximum = value;
		if (value == INT64_MAX) throw std::runtime_error("enumerator overflow");
		next = value + 1;
	}
	if (underlying_node == kNoNode && !scoped)
	{
		if (minimum >= std::numeric_limits<std::int32_t>::min() &&
			maximum <= std::numeric_limits<std::int32_t>::max())
			underlying = program_->types.Fundamental(FUND_INT);
		else if (minimum >= 0 &&
			static_cast<std::uint64_t>(maximum) <=
				std::numeric_limits<std::uint32_t>::max())
			underlying = program_->types.Fundamental(FUND_UNSIGNED_INT);
		else underlying = program_->types.Fundamental(FUND_LONG_LONG_INT);
		program_->entities[entity].underlying = underlying;
	}
	for (std::size_t i = 0; i < enumerators.size(); ++i)
		program_->bindings[enumerators[i]].type = type;
	return type;
}

SpecInfo SemanticAnalyzer::BuildSpecifiers(NodeId node, ScopeId scope,
	const std::string& hint, bool has_declarators)
{
	if (node == kNoNode) throw std::runtime_error("missing type specifiers");
	SpecInfo result;
	std::uint8_t cv = CV_NONE;
	bool is_unsigned = false;
	bool is_signed = false;
	bool is_short = false;
	int longs = 0;
	bool is_char = false;
	bool is_void = false;
	bool is_bool = false;
	bool is_float = false;
	bool is_double = false;
	bool is_wchar = false;
	bool is_char16 = false;
	bool is_char32 = false;
	bool saw_int = false;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, "class-specifier") ||
			arena_->IsTag(child, "class-forward-declaration"))
		{
			result.type = AnalyzeClass(child, scope, hint,
				has_declarators &&
				arena_->IsTag(child, "class-forward-declaration"));
			continue;
		}
		if (arena_->IsTag(child, "enum-specifier"))
		{
			const bool definition =
				(arena_->Flags(child) & SYNTAX_FLAG_DEFINITION) != 0;
			result.type = AnalyzeEnum(child, scope, hint,
				has_declarators && !definition &&
				arena_->Payload(child).size() != 0);
			continue;
		}
		if (arena_->IsTag(child, "decltype-specifier") ||
			(arena_->IsTag(child, "decl-specifier") &&
			 FirstSemanticChild(child) != kNoNode))
		{
			result.type = DecltypeType(FirstSemanticChild(child), scope);
			continue;
		}
		if (!arena_->IsTag(child, "cv-qualifier") &&
			!arena_->IsTag(child, "decl-specifier") &&
			!arena_->IsTag(child, "type-specifier") &&
			!arena_->IsTag(child, "type-name")) continue;
		const std::string spelling = PayloadSource(child);
		if (spelling == "typedef") result.is_typedef = true;
		else if (spelling == "constexpr") result.is_constexpr = true;
		else if (spelling == "extern") result.storage_class = STORAGE_CLASS_EXTERN;
		else if (spelling == "static") result.storage_class = STORAGE_CLASS_STATIC;
		else if (spelling == "thread_local")
			result.thread_local_storage = true;
		else if (spelling == "const") cv |= CV_CONST;
		else if (spelling == "volatile") cv |= CV_VOLATILE;
		else if (spelling == "unsigned") is_unsigned = true;
		else if (spelling == "signed") is_signed = true;
		else if (spelling == "short") is_short = true;
		else if (spelling == "long") ++longs;
		else if (spelling == "int") saw_int = true;
		else if (spelling == "char") is_char = true;
		else if (spelling == "void") is_void = true;
		else if (spelling == "bool") is_bool = true;
		else if (spelling == "float") is_float = true;
		else if (spelling == "double") is_double = true;
		else if (spelling == "wchar_t") is_wchar = true;
		else if (spelling == "char16_t") is_char16 = true;
		else if (spelling == "char32_t") is_char32 = true;
		else if (spelling != "inline" &&
			spelling != "virtual" && spelling != "friend" &&
			spelling != "explicit")
		{
			const LookupResult found = LookupSpelling(scope, spelling, LOOKUP_TYPE);
			if (found.type == kNoType)
				throw std::runtime_error("unknown type name: " + spelling);
			if (found.type_declaration != kNoBinding &&
				!CanAccessMember(found.type_declaration,
					found.naming_class))
				throw std::runtime_error("inaccessible member type");
			result.type = found.type;
		}
	}
	if (result.type == kNoType)
	{
		FundamentalKind kind = FUND_INT;
		if (is_void) kind = FUND_VOID;
		else if (is_bool) kind = FUND_BOOL;
		else if (is_wchar) kind = FUND_WCHAR_T;
		else if (is_char16) kind = FUND_CHAR16_T;
		else if (is_char32) kind = FUND_CHAR32_T;
		else if (is_float) kind = FUND_FLOAT;
		else if (is_double && longs != 0) kind = FUND_LONG_DOUBLE;
		else if (is_double) kind = FUND_DOUBLE;
		else if (is_char && is_unsigned) kind = FUND_UNSIGNED_CHAR;
		else if (is_char && is_signed) kind = FUND_SIGNED_CHAR;
		else if (is_char) kind = FUND_CHAR;
		else if (is_short && is_unsigned) kind = FUND_UNSIGNED_SHORT_INT;
		else if (is_short) kind = FUND_SHORT_INT;
		else if (longs > 1 && is_unsigned) kind = FUND_UNSIGNED_LONG_LONG_INT;
		else if (longs > 1) kind = FUND_LONG_LONG_INT;
		else if (longs == 1 && is_unsigned) kind = FUND_UNSIGNED_LONG_INT;
		else if (longs == 1) kind = FUND_LONG_INT;
		else if (is_unsigned) kind = FUND_UNSIGNED_INT;
		else if (!saw_int && !is_signed)
			throw std::runtime_error("declaration has no type specifier");
		result.type = program_->types.Fundamental(kind);
	}
	result.type = program_->types.Qualify(result.type, cv);
	return result;
}

TypeId SemanticAnalyzer::BuildTypeId(NodeId node, ScopeId scope)
{
	if (node == kNoNode) throw std::runtime_error("missing type-id");
	NodeId specifiers = FindChild(node, "type-specifier-seq");
	if (specifiers == kNoNode)
		specifiers = FindChild(node, "decl-specifier-seq");
	const SpecInfo spec = BuildSpecifiers(specifiers, scope, std::string(), false);
	NodeId declarator = FindChild(node, "abstract-declarator");
	if (declarator == kNoNode) declarator = FindChild(node, "declarator");
	return declarator == kNoNode ? spec.type :
		BuildDeclarator(declarator, spec.type, scope).type;
}

NamePath SemanticAnalyzer::DeclaratorNamePath(NodeId node)
{
	const NodeId identifier = FindChild(node, "identifier");
	if (identifier != kNoNode)
		return ParseNamePath(arena_->Payload(identifier));
	const NodeId nested = FindChild(node, "nested-declarator");
	return nested == kNoNode ? NamePath() :
		DeclaratorNamePath(FirstSemanticChild(nested));
}

NameId SemanticAnalyzer::DeclaratorName(NodeId node)
{
	return DeclaratorNamePath(node).Last();
}

std::vector<ParameterInfo> SemanticAnalyzer::BuildParameters(NodeId node,
	ScopeId scope, bool* variadic)
{
	std::vector<ParameterInfo> result;
	*variadic = false;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, "parameter-pack"))
		{
			*variadic = true;
			continue;
		}
		if (!arena_->IsTag(child, "parameter-declaration")) continue;
		const NodeId specifiers = FindChild(child, "decl-specifier-seq");
		const NodeId declarator = FindChild(child, "declarator");
		const SpecInfo spec = BuildSpecifiers(specifiers, scope,
			std::string(), declarator != kNoNode);
		NameId name = 0;
		TypeId declared = spec.type;
		if (declarator != kNoNode)
		{
			const DeclaratorInfo parsed =
				BuildDeclarator(declarator, spec.type, scope);
			name = parsed.name;
			declared = parsed.type;
			if (FindChild(declarator, "parameter-pack") != kNoNode)
				*variadic = true;
		}
		result.push_back(ParameterInfo(name, declared,
			AdjustParameterType(declared)));
		const NodeId default_node = FindChild(child, "default-argument");
		if (default_node != kNoNode)
		{
			NodeId default_expression = FirstSemanticChild(default_node);
			if (default_expression != kNoNode &&
				arena_->IsTag(default_expression, "initializer"))
				default_expression = FirstSemanticChild(default_expression);
			result.back().default_argument = default_expression;
			result.back().default_scope = scope;
		}
	}
	if (result.size() == 1 && result[0].name == 0 &&
		IsVoid(result[0].declared_type)) result.clear();
	return result;
}

DeclaratorInfo SemanticAnalyzer::BuildDeclarator(NodeId node, TypeId base,
	ScopeId scope)
{
	DeclaratorInfo result;
	result.name = DeclaratorName(node);
	TypeId type = base;
	std::vector<NodeId> suffixes;
	NodeId nested = kNoNode;
	std::uint8_t function_cv = CV_NONE;
	bool saw_function_suffix = false;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, "ptr-operator"))
		{
			const std::string operation = PayloadSource(child);
			if (operation == "*") type = program_->types.Pointer(type);
			else if (operation == "&")
				type = program_->types.Reference(TYPE_LVALUE_REFERENCE, type);
			else if (operation == "&&")
				type = program_->types.Reference(TYPE_RVALUE_REFERENCE, type);
			else if (operation.size() > 3 &&
				operation.compare(operation.size() - 3, 3, "::*") == 0)
			{
				const std::string owner_name =
					operation.substr(0, operation.size() - 3);
				const LookupResult owner = LookupSpelling(scope, owner_name,
					LOOKUP_TYPE);
				if (owner.type == kNoType)
					throw std::runtime_error("member pointer owner not found");
				type = program_->types.MemberPointer(owner.type, type);
			}
			else throw std::runtime_error("invalid pointer operator");
		}
		else if (arena_->IsTag(child, "cv-qualifier"))
		{
			const std::string qualifier = PayloadSource(child);
			const std::uint8_t flag = qualifier == "const" ?
				CV_CONST : CV_VOLATILE;
			if (saw_function_suffix) function_cv |= flag;
			else type = program_->types.Qualify(type, flag);
		}
		else if (arena_->IsTag(child, "nested-declarator"))
			nested = FirstSemanticChild(child);
		else if (arena_->IsTag(child, "array-suffix") ||
			arena_->IsTag(child, "parameter-clause"))
		{
			suffixes.push_back(child);
			if (arena_->IsTag(child, "parameter-clause"))
				saw_function_suffix = true;
		}
	}
	for (std::size_t i = suffixes.size(); i != 0; --i)
	{
		const NodeId suffix = suffixes[i - 1];
		if (arena_->IsTag(suffix, "array-suffix"))
		{
			const NodeId bound_node = FirstSemanticChild(suffix);
			std::uint64_t bound = 0;
			if (bound_node != kNoNode)
			{
				const ExpressionInfo expression = AnalyzeExpression(bound_node, scope);
				if (!expression.constant || expression.value <= 0)
					throw std::runtime_error("invalid array bound");
				bound = static_cast<std::uint64_t>(expression.value);
			}
			type = program_->types.Array(type, bound);
		}
		else
		{
			bool variadic = false;
			const std::vector<ParameterInfo> parameters =
				BuildParameters(suffix, scope, &variadic);
			std::vector<TypeId> function_parameters;
			for (std::size_t p = 0; p < parameters.size(); ++p)
				function_parameters.push_back(parameters[p].function_type);
			type = program_->types.Function(type, function_parameters, variadic,
				function_cv);
			result.parameters = parameters;
		}
	}
	if (nested != kNoNode)
	{
		DeclaratorInfo inner = BuildDeclarator(nested, type, scope);
		if (!result.parameters.empty()) inner.parameters = result.parameters;
		return inner;
	}
	result.type = type;
	return result;
}

BindingId SemanticAnalyzer::DeclareFunction(ScopeId owner, NameId name,
	TypeId type, const std::vector<ParameterInfo>& parameters, bool definition,
	bool template_specialization, StorageClass storage_class,
	LanguageLinkage language_linkage, bool nonthrowing)
{
	const LookupResult occupied =
		program_->LookupDirect(owner, name, LOOKUP_ORDINARY);
	if (occupied.ordinary != kNoBinding &&
		program_->bindings[occupied.ordinary].kind != BIND_FUNCTION)
		throw std::runtime_error("function conflicts with ordinary binding");
	const TypeRecord declared_type = program_->types.Get(type);
	if (declared_type.kind != TYPE_FUNCTION)
		throw std::logic_error("function declaration has non-function type");
	std::vector<TypeId> signature_parameters;
	signature_parameters.reserve(declared_type.parameter_count);
	const TypeId* declared_parameters = program_->types.Parameters(type);
	for (std::size_t i = 0; i < declared_type.parameter_count; ++i)
		signature_parameters.push_back(declared_parameters[i]);
	const TypeId signature = program_->types.Function(
		program_->types.Fundamental(FUND_VOID), signature_parameters,
		declared_type.variadic, declared_type.cv);
	const FunctionSignatureKey signature_key(owner, name, signature);
	++function_signature_lookups_;
	const BindingId previous = template_specialization ? kNoBinding :
		function_declarations_.Find(signature_key);
	const std::uint64_t key = (static_cast<std::uint64_t>(owner) << 32) | name;
	CompactIndexSequence& overloads = function_sets_.Ensure(key);
	BindingId canonical = kNoBinding;
	if (previous != kNoBinding)
	{
		const FunctionInfo& existing = GetFunction(previous);
		const TypeRecord old_type = program_->types.Get(existing.type);
		if (old_type.child != declared_type.child)
			throw std::runtime_error("conflicting function return type");
		canonical = existing.binding;
		if (definition && existing.defined)
			throw std::runtime_error("duplicate function definition");
	}
	const BindingId declaration = program_->AddBinding(owner, BIND_FUNCTION,
		name, type, false, 0, NAMED_NONE, 0, canonical,
		!template_specialization);
	BindingRecord& declaration_record = program_->bindings[declaration];
	declaration_record.storage_class = storage_class;
	declaration_record.language_linkage = language_linkage;
	declaration_record.nonthrowing = nonthrowing;
	if (canonical == kNoBinding)
	{
		FunctionInfo info;
		info.binding = declaration;
		info.owner = owner;
		info.type = type;
		info.display_name = DisplayName(owner, name);
		info.parameters = parameters;
		info.defined = definition;
		info.template_specialization = template_specialization;
		if (function_fact_by_binding_.size() <= declaration)
			function_fact_by_binding_.resize(
				static_cast<std::size_t>(declaration) + 1, kNoDumpEdge);
		function_fact_by_binding_[declaration] =
			static_cast<std::uint32_t>(functions_.size());
		functions_.push_back(info);
		overloads.Push(declaration);
		program_->bindings[declaration].overload_ordinal =
			static_cast<std::uint32_t>(overloads.Size());
		if (!template_specialization)
			function_declarations_.Insert(signature_key, declaration);
		canonical = declaration;
	}
	else if (definition)
		GetMutableFunction(canonical).defined = true;
	if (previous != kNoBinding)
	{
		FunctionInfo& merged = GetMutableFunction(canonical);
		if (merged.parameters.size() != parameters.size())
			throw std::logic_error("PA12 function parameter fact mismatch");
		for (std::size_t i = 0; i < parameters.size(); ++i)
			if (parameters[i].default_argument != kNoNode)
			{
				merged.parameters[i].default_argument = parameters[i].default_argument;
				merged.parameters[i].default_scope = parameters[i].default_scope;
			}
	}
	BindingRecord& canonical_record = program_->bindings[canonical];
	if (previous != kNoBinding &&
		canonical_record.language_linkage == LANGUAGE_LINKAGE_C)
		language_linkage = LANGUAGE_LINKAGE_C;
	if (canonical_record.storage_class == STORAGE_CLASS_NONE ||
		storage_class == STORAGE_CLASS_STATIC)
		canonical_record.storage_class = storage_class;
	canonical_record.language_linkage = language_linkage;
	canonical_record.nonthrowing = canonical_record.nonthrowing || nonthrowing;
	return canonical;
}

const FunctionInfo& SemanticAnalyzer::GetFunction(BindingId binding) const
{
	const BindingId canonical = program_->bindings[binding].canonical;
	if (canonical >= function_fact_by_binding_.size() ||
		function_fact_by_binding_[canonical] == kNoDumpEdge)
		throw std::logic_error("missing PA12 function fact");
	return functions_[function_fact_by_binding_[canonical]];
}

FunctionInfo& SemanticAnalyzer::GetMutableFunction(BindingId binding)
{
	const BindingId canonical = program_->bindings[binding].canonical;
	if (canonical >= function_fact_by_binding_.size() ||
		function_fact_by_binding_[canonical] == kNoDumpEdge)
		throw std::logic_error("missing PA12 function fact");
	return functions_[function_fact_by_binding_[canonical]];
}

std::vector<BindingId> SemanticAnalyzer::FunctionCandidates(ScopeId scope,
	const std::string& spelling, EntityId* naming_class)
{
	if (naming_class) *naming_class = kNoEntity;
	std::string lookup_name = spelling;
	std::string explicit_base;
	std::vector<TypeId> explicit_arguments;
	if (ParseExplicitTemplateArguments(scope, spelling, &explicit_base,
		&explicit_arguments))
	{
		std::vector<BindingId> explicit_candidates;
		const std::vector<std::size_t> patterns =
			FindFunctionTemplates(scope, explicit_base);
		for (std::size_t i = 0; i < patterns.size(); ++i)
			if (function_templates_[patterns[i]].type_parameters.size() ==
				explicit_arguments.size())
			{
				const BindingId candidate = InstantiateFunctionTemplate(
					patterns[i], explicit_arguments);
				if (candidate != kNoBinding &&
					std::find(explicit_candidates.begin(),
						explicit_candidates.end(), candidate) ==
						explicit_candidates.end())
					explicit_candidates.push_back(candidate);
			}
		return explicit_candidates;
	}
	const LookupResult found =
		LookupSpelling(scope, lookup_name, LOOKUP_ORDINARY);
	if (naming_class) *naming_class = found.naming_class;
	if (found.ordinary == kNoBinding) return std::vector<BindingId>();
	const BindingRecord& binding = program_->bindings[found.ordinary];
	if (binding.kind != BIND_FUNCTION) return std::vector<BindingId>();
	return FunctionSet(found.ordinary);
}

std::vector<BindingId> SemanticAnalyzer::FunctionSet(BindingId binding) const
{
	if (binding == kNoBinding || binding >= program_->bindings.size() ||
		program_->bindings[binding].kind != BIND_FUNCTION)
		return std::vector<BindingId>();
	const BindingRecord& record = program_->bindings[binding];
	const std::uint64_t key = (static_cast<std::uint64_t>(record.owner) << 32) |
		record.name;
	const CompactIndexSequence* set = function_sets_.Find(key);
	if (!set)
		return std::vector<BindingId>(1, record.canonical);
	std::vector<BindingId> result;
	result.reserve(set->Size());
	for (std::size_t i = 0; i < set->Size(); ++i)
		result.push_back(static_cast<BindingId>((*set)[i]));
	return result;
}

std::vector<std::size_t> SemanticAnalyzer::FindFunctionTemplates(
	ScopeId scope, const std::string& spelling)
{
	std::string base = spelling;
	const std::size_t angle = base.find('<');
	if (angle != std::string::npos) base.erase(angle);
	const NamePath path = ParseNamePath(base);
	const NameId name = path.Last();
	if (name == 0) return std::vector<std::size_t>();
	if (path.global || path.Size() > 1)
	{
		const ScopeId owner = ResolveOwner(scope, path);
		if (owner == kNoScope) return std::vector<std::size_t>();
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owner) << 32) | name;
		const CompactIndexSequence* found =
			template_function_sets_.Find(key);
		return found ? found->Copy() : std::vector<std::size_t>();
	}
	for (ScopeId current = scope; current != kNoScope; )
	{
		const std::uint64_t key =
			(static_cast<std::uint64_t>(current) << 32) | name;
		const CompactIndexSequence* found =
			template_function_sets_.Find(key);
		if (found) return found->Copy();
		current = current < scope_parents_.size() ?
			scope_parents_[current] : kNoScope;
	}
	return std::vector<std::size_t>();
}

TypeId SemanticAnalyzer::ResolveTemplateTypeArgument(ScopeId scope,
	const std::string& untrimmed)
{
	const std::size_t first = untrimmed.find_first_not_of(" \t\r\n");
	const std::size_t last = untrimmed.find_last_not_of(" \t\r\n");
	if (first == std::string::npos) return kNoType;
	const std::string spelling = untrimmed.substr(first, last - first + 1);
	FundamentalKind fundamental = FUND_INT;
	bool known_fundamental = true;
	if (spelling == "bool") fundamental = FUND_BOOL;
	else if (spelling == "char") fundamental = FUND_CHAR;
	else if (spelling == "signed char") fundamental = FUND_SIGNED_CHAR;
	else if (spelling == "unsigned char") fundamental = FUND_UNSIGNED_CHAR;
	else if (spelling == "short" || spelling == "short int")
		fundamental = FUND_SHORT_INT;
	else if (spelling == "unsigned short" ||
		spelling == "unsigned short int") fundamental = FUND_UNSIGNED_SHORT_INT;
	else if (spelling == "int") fundamental = FUND_INT;
	else if (spelling == "unsigned" || spelling == "unsigned int")
		fundamental = FUND_UNSIGNED_INT;
	else if (spelling == "long" || spelling == "long int")
		fundamental = FUND_LONG_INT;
	else if (spelling == "unsigned long" ||
		spelling == "unsigned long int") fundamental = FUND_UNSIGNED_LONG_INT;
	else if (spelling == "long long" || spelling == "long long int")
		fundamental = FUND_LONG_LONG_INT;
	else if (spelling == "unsigned long long" ||
		spelling == "unsigned long long int")
		fundamental = FUND_UNSIGNED_LONG_LONG_INT;
	else if (spelling == "float") fundamental = FUND_FLOAT;
	else if (spelling == "double") fundamental = FUND_DOUBLE;
	else if (spelling == "long double") fundamental = FUND_LONG_DOUBLE;
	else if (spelling == "void") fundamental = FUND_VOID;
	else if (spelling == "wchar_t") fundamental = FUND_WCHAR_T;
	else if (spelling == "char16_t") fundamental = FUND_CHAR16_T;
	else if (spelling == "char32_t") fundamental = FUND_CHAR32_T;
	else known_fundamental = false;
	if (known_fundamental) return program_->types.Fundamental(fundamental);
	const LookupResult found = LookupSpelling(scope, spelling, LOOKUP_TYPE);
	return found.type;
}

bool SemanticAnalyzer::ParseExplicitTemplateArguments(ScopeId scope,
	const std::string& spelling, std::string* base,
	std::vector<TypeId>* arguments)
{
	const std::size_t open = spelling.find('<');
	if (open == std::string::npos || spelling.empty() ||
		spelling[spelling.size() - 1] != '>') return false;
	*base = spelling.substr(0, open);
	arguments->clear();
	std::size_t first = open + 1;
	std::size_t depth = 0;
	for (std::size_t i = first; i < spelling.size() - 1; ++i)
	{
		if (spelling[i] == '<') ++depth;
		else if (spelling[i] == '>')
		{
			if (depth == 0) return false;
			--depth;
		}
		else if (spelling[i] == ',' && depth == 0)
		{
			const TypeId type = ResolveTemplateTypeArgument(scope,
				spelling.substr(first, i - first));
			if (type == kNoType)
				throw std::runtime_error("unknown explicit template type argument");
			arguments->push_back(type);
			first = i + 1;
		}
	}
	if (first < spelling.size() - 1)
	{
		const TypeId type = ResolveTemplateTypeArgument(scope,
			spelling.substr(first, spelling.size() - first - 1));
		if (type == kNoType)
			throw std::runtime_error("unknown explicit template type argument");
		arguments->push_back(type);
	}
	return true;
}

BindingId SemanticAnalyzer::InstantiateFunctionTemplate(std::size_t index,
	const std::vector<TypeId>& arguments)
{
	if (index >= function_templates_.size())
		throw std::logic_error("invalid PA12 function template pattern");
	const FunctionTemplatePattern& pattern = function_templates_[index];
	if (arguments.size() != pattern.type_parameters.size()) return kNoBinding;
	++template_specialization_requests_;
	const TemplateSpecializationKey cache_key(index, arguments);
	const BindingId old = template_instantiations_.Find(cache_key);
	if (old != kNoBinding) ++template_specialization_cache_hits_;
	if (old != kNoBinding) return old;

	const ScopeId template_scope = NewScope(pattern.owner,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(pattern.owner));
	for (std::size_t i = 0; i < arguments.size(); ++i)
		program_->AddBinding(template_scope, BIND_TYPE_ALIAS,
			pattern.type_parameters[i], arguments[i]);
	const SpecInfo spec = BuildSpecifiers(pattern.specifiers, template_scope,
		std::string(), true);
	const DeclaratorInfo parsed = BuildDeclarator(pattern.declarator,
		spec.type, template_scope);
	const BindingId binding = DeclareFunction(pattern.owner, pattern.name,
		parsed.type, parsed.parameters, false, true);
	GetMutableFunction(binding).deferred = true;
	template_instantiations_.Insert(cache_key, binding);
	return binding;
}

void SemanticAnalyzer::DeduceFunctionTemplates(ScopeId scope,
	const std::string& spelling,
	const std::vector<ExpressionInfo>& arguments)
{
	if (spelling.find('<') != std::string::npos) return;
	const std::vector<std::size_t> patterns =
		FindFunctionTemplates(scope, spelling);
	for (std::size_t p = 0; p < patterns.size(); ++p)
	{
		const FunctionTemplatePattern& pattern =
			function_templates_[patterns[p]];
		const NodeId clause = FindChild(pattern.declarator, "parameter-clause");
		if (clause == kNoNode) continue;
		std::vector<NodeId> parameters;
		for (std::uint32_t edge = arena_->FirstEdge(clause); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "parameter-declaration"))
				parameters.push_back(child);
		}
		if (parameters.size() != arguments.size()) continue;
		std::vector<TypeId> deduced(pattern.type_parameters.size(), kNoType);
		bool valid = true;
		for (std::size_t a = 0; a < parameters.size() && valid; ++a)
		{
			const NodeId specifiers =
				FindChild(parameters[a], "decl-specifier-seq");
			if (specifiers == kNoNode || arguments[a].type == kNoType) continue;
			NameId dependent = 0;
			for (std::uint32_t edge = arena_->FirstEdge(specifiers);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
			{
				const std::string type_name =
					PayloadSource(arena_->EdgeChild(edge));
				for (std::size_t t = 0; t < pattern.type_parameters.size(); ++t)
					if (type_name ==
						program_->names.Get(pattern.type_parameters[t]))
						dependent = pattern.type_parameters[t];
			}
			if (dependent == 0) continue;
			TypeId argument = Decay(EffectiveType(arguments[a].type));
			argument = program_->types.RemoveTopCv(argument);
			for (std::size_t t = 0; t < pattern.type_parameters.size(); ++t)
			{
				if (pattern.type_parameters[t] != dependent) continue;
				if (deduced[t] != kNoType && deduced[t] != argument) valid = false;
				else deduced[t] = argument;
			}
		}
		for (std::size_t t = 0; t < deduced.size(); ++t)
			if (deduced[t] == kNoType) valid = false;
		if (valid) InstantiateFunctionTemplate(patterns[p], deduced);
	}
}

void SemanticAnalyzer::DemandFunction(BindingId binding)
{
	if (binding == kNoBinding) return;
	binding = program_->bindings[binding].canonical;
	if (binding >= function_fact_by_binding_.size() ||
		function_fact_by_binding_[binding] == kNoDumpEdge) return;
	FunctionInfo& function = GetMutableFunction(binding);
	if (!function.deferred || function.demand_state != 0) return;
	function.demand_state = 1;
	demanded_functions_.push_back(binding);
	++demand_worklist_pushes_;
}

TypeId SemanticAnalyzer::AdaptMemberFunctionType(BindingId binding)
{
	const FunctionInfo& function = GetFunction(binding);
	if (function.member_owner == kNoType) return function.type;
	const TypeRecord member_type = program_->types.Get(function.type);
	TypeId object = function.member_owner;
	if ((member_type.cv & CV_CONST) != 0)
		object = program_->types.Qualify(object, CV_CONST);
	if ((member_type.cv & CV_VOLATILE) != 0)
		object = program_->types.Qualify(object, CV_VOLATILE);
	std::vector<TypeId> parameters;
	parameters.push_back(program_->types.Pointer(object));
	const TypeId* explicit_parameters =
		program_->types.Parameters(function.type);
	for (std::size_t i = 0; i < member_type.parameter_count; ++i)
		parameters.push_back(explicit_parameters[i]);
	return program_->types.Function(member_type.child, parameters,
		member_type.variadic);
}

void SemanticAnalyzer::EmitDemandedFunction(BindingId binding)
{
	binding = program_->bindings[binding].canonical;
	if (binding >= function_fact_by_binding_.size() ||
		function_fact_by_binding_[binding] == kNoDumpEdge) return;
	FunctionInfo& state = GetMutableFunction(binding);
	if (state.demand_state >= 2) return;
	state.demand_state = 2;
	const FunctionInfo info = GetFunction(binding);
	const bool member = info.member_owner != kNoType;
	const TypeId output_type = member ?
		AdaptMemberFunctionType(info.binding) : info.type;
	const std::uint32_t function = MakeDump(info.defined ?
		DUMP_FUNCTION_DEFINITION : DUMP_FUNCTION_DECLARATION,
		output_type, VALUE_NONE, info.display_name, info.binding);
	dump_.Add(root_, function);
	if (!info.defined && member)
	{
		GetMutableFunction(binding).demand_state = 3;
		++demanded_function_emissions_;
		return;
	}
	const ScopeId function_scope = NewScope(info.owner, SCOPE_FUNCTION,
		program_->bindings[info.binding].name, ScopePrefixId(info.owner));
	if (member)
	{
		const TypeId this_type = program_->types.Parameters(output_type)[0];
		const NameId this_name = program_->names.Intern("this");
		const BindingId this_binding = program_->AddBinding(function_scope,
			BIND_PARAMETER, this_name, this_type);
		dump_.Add(function, MakeDump(DUMP_PARAMETER, this_type,
			VALUE_NONE, this_name, this_binding));
	}
	for (std::size_t i = 0; i < info.parameters.size(); ++i)
	{
		const ParameterInfo& parameter = info.parameters[i];
		const BindingId parameter_binding = program_->AddBinding(function_scope,
			BIND_PARAMETER, parameter.name, parameter.declared_type);
		dump_.Add(function, MakeDump(DUMP_PARAMETER, parameter.function_type,
			VALUE_NONE, parameter.name, parameter_binding));
	}
	if (!info.defined)
	{
		GetMutableFunction(binding).demand_state = 3;
		++demanded_function_emissions_;
		return;
	}
	if (info.defined)
	{
		const TypeId previous_return = current_return_type_;
		const EntityId previous_class = current_class_context_;
		current_return_type_ = program_->types.Get(info.type).child;
		current_class_context_ =
			program_->bindings[info.binding].member_owner;
		if (info.constructor)
		{
			const std::uint32_t constructor_body =
				MakeDump(DUMP_COMPOUND_STATEMENT);
			dump_.Add(function, constructor_body);
			AddConstructorMemberActions(info, function_scope, constructor_body);
			if ((info.implicit_constructor || info.defaulted_constructor) &&
				InitializationActionsAreNonthrowing(constructor_body))
				program_->bindings[info.binding].nonthrowing = true;
			if (info.definition_body != kNoNode)
				AnalyzeCompound(info.definition_body, function_scope,
					constructor_body);
		}
		else if (info.destructor)
		{
			const std::uint32_t destructor_body =
				MakeDump(DUMP_COMPOUND_STATEMENT);
			dump_.Add(function, destructor_body);
			if (info.definition_body != kNoNode)
				AnalyzeCompound(info.definition_body, function_scope,
					destructor_body);
			AddDestructorSubobjectActions(
				program_->bindings[info.binding].member_owner,
				destructor_body);
		}
		else if (info.definition_body != kNoNode)
			AnalyzeCompound(info.definition_body, function_scope, function);
		else dump_.Add(function, MakeDump(DUMP_COMPOUND_STATEMENT));
		current_return_type_ = previous_return;
		current_class_context_ = previous_class;
	}
	GetMutableFunction(binding).demand_state = 3;
	++demanded_function_emissions_;
}

}
}
