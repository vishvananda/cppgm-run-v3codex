#include "pa12_semantic_detail.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
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
OperatorKind ClassifyOperator(const std::string& name,
	std::string* literal_suffix)
{
	if (name.compare(0, 8, "operator") != 0) return OPERATOR_NONE;
	const std::string operation = name.substr(8);
	if (operation == "+") return OPERATOR_PLUS;
	if (operation == "-") return OPERATOR_MINUS;
	if (operation == "*") return OPERATOR_STAR;
	if (operation == "&") return OPERATOR_AMPERSAND;
	if (operation == "/") return OPERATOR_DIVIDE;
	if (operation == "%") return OPERATOR_REMAINDER;
	if (operation == "|") return OPERATOR_BIT_OR;
	if (operation == "^") return OPERATOR_BIT_XOR;
	if (operation == "=") return OPERATOR_ASSIGN;
	if (operation == "+=") return OPERATOR_PLUS_ASSIGN;
	if (operation == "-=") return OPERATOR_MINUS_ASSIGN;
	if (operation == "*=") return OPERATOR_MULTIPLY_ASSIGN;
	if (operation == "/=") return OPERATOR_DIVIDE_ASSIGN;
	if (operation == "%=") return OPERATOR_REMAINDER_ASSIGN;
	if (operation == "&=") return OPERATOR_AND_ASSIGN;
	if (operation == "|=") return OPERATOR_OR_ASSIGN;
	if (operation == "^=") return OPERATOR_XOR_ASSIGN;
	if (operation == "<<") return OPERATOR_LEFT_SHIFT;
	if (operation == ">>") return OPERATOR_RIGHT_SHIFT;
	if (operation == "<<=") return OPERATOR_LEFT_SHIFT_ASSIGN;
	if (operation == ">>=") return OPERATOR_RIGHT_SHIFT_ASSIGN;
	if (operation == "==") return OPERATOR_EQUAL;
	if (operation == "!=") return OPERATOR_NOT_EQUAL;
	if (operation == "<") return OPERATOR_LESS;
	if (operation == ">") return OPERATOR_GREATER;
	if (operation == "<=") return OPERATOR_LESS_EQUAL;
	if (operation == ">=") return OPERATOR_GREATER_EQUAL;
	if (operation == "!") return OPERATOR_LOGICAL_NOT;
	if (operation == "&&") return OPERATOR_LOGICAL_AND;
	if (operation == "||") return OPERATOR_LOGICAL_OR;
	if (operation == "++") return OPERATOR_INCREMENT;
	if (operation == "--") return OPERATOR_DECREMENT;
	if (operation == ",") return OPERATOR_COMMA;
	if (operation == "->*") return OPERATOR_MEMBER_POINTER;
	if (operation == "->") return OPERATOR_ARROW;
	if (operation == "()") return OPERATOR_CALL;
	if (operation == "[]") return OPERATOR_INDEX;
	if (operation == " new" || operation == "new") return OPERATOR_NEW;
	if (operation == " new[]" || operation == "new[]")
		return OPERATOR_NEW_ARRAY;
	if (operation == " delete" || operation == "delete")
		return OPERATOR_DELETE;
	if (operation == " delete[]" || operation == "delete[]")
		return OPERATOR_DELETE_ARRAY;
	if (operation.compare(0, 2, "\"\"") == 0 && operation.size() > 2)
	{
		*literal_suffix = operation.substr(2);
		return OPERATOR_LITERAL;
	}
	return OPERATOR_NONE;
}

BindingId LocalTypeContext(const Program& program, ScopeId owner,
	BindingId current_function)
{
	if (current_function == kNoBinding) return kNoBinding;
	for (ScopeId scope = owner; scope != kNoScope;
		scope = program.ParentScope(scope))
	{
		if (program.KindOfScope(scope) == SCOPE_FUNCTION)
			return current_function;
		if (program.KindOfScope(scope) == SCOPE_NAMESPACE)
			return kNoBinding;
	}
	return kNoBinding;
}
}
TypeId SemanticAnalyzer::AnalyzeClass(NodeId node, ScopeId scope,
	const std::string& hint, bool elaborated,
	const std::string& specialization_name, ScopeId specialization_owner,
	NameId specialization_identity, bool complete_definition,
	NameId specialization_lookup_name, NameId specialization_emission_name)
{
	const NodeId key = FindChild(node, "class-key");
	if (key == kNoNode) throw std::runtime_error("class without class-key");
	const std::string key_text = PayloadSource(key);
	const NamedFlavor flavor = key_text == "struct" ? NAMED_STRUCT :
		key_text == "class" ? NAMED_CLASS :
		key_text == "union" ? NAMED_UNION : NAMED_NONE;
	if (flavor == NAMED_NONE) throw std::runtime_error("invalid class-key");
	std::string spelling = specialization_name.empty() ?
		arena_->Payload(node) : specialization_name;
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
	const NameId lookup_name = specialization_lookup_name == 0 ?
		name : specialization_lookup_name;
	const ScopeId owner = specialization_owner == kNoScope ?
		ResolveOwner(scope, path) : specialization_owner;
	if (owner == kNoScope) throw std::runtime_error("class owner not found");
	const LookupResult old = path.global || path.Size() > 1 ?
		program_->LookupDirect(owner, lookup_name, LOOKUP_TYPE) :
		(elaborated && specialization_lookup_name == 0 ?
		 program_->LookupName(scope, lookup_name, LOOKUP_TYPE) :
		 program_->LookupDirect(owner, lookup_name, LOOKUP_TYPE));
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
		const NameId entity_name = EmissionName(owner, name);
		entity = program_->NewEntity(entity_name, flavor, false,
			kNoType, owner, specialization_identity == 0 ?
				name : specialization_identity);
		program_->entities[entity].local_context = LocalTypeContext(
			*program_, owner, current_function_context_);
		program_->SetTypeName(owner, lookup_name,
			program_->entities[entity].type);
	}
	const TypeId type = program_->entities[entity].type;
	if (entity_data_members_.size() <= entity)
		entity_data_members_.resize(static_cast<std::size_t>(entity) + 1);
	if (entity_layout_members_.size() <= entity)
		entity_layout_members_.resize(static_cast<std::size_t>(entity) + 1);
	if (entity_constructors_.size() <= entity)
		entity_constructors_.resize(static_cast<std::size_t>(entity) + 1);
	if (entity_member_functions_.size() <= entity)
		entity_member_functions_.resize(static_cast<std::size_t>(entity) + 1);
	if (class_polymorphism_.size() <= entity)
		class_polymorphism_.resize(static_cast<std::size_t>(entity) + 1);
	if (class_special_members_.size() <= entity)
		class_special_members_.resize(static_cast<std::size_t>(entity) + 1);
	if (implicit_constructor_by_entity_.size() <= entity)
		implicit_constructor_by_entity_.resize(
			static_cast<std::size_t>(entity) + 1, kNoBinding);
	if (entity_destructor_by_entity_.size() <= entity)
		entity_destructor_by_entity_.resize(
			static_cast<std::size_t>(entity) + 1, kNoBinding);
	if (old.type == kNoType && arena_->Payload(node).size() != 0)
		program_->AddBinding(owner, BIND_TYPE, lookup_name, type,
			false, 0, flavor);
	const std::size_t requested_alignment = RequestedAlignment(node, scope);
	if (requested_alignment != 0)
		program_->entities[entity].requested_alignment = std::max(
			program_->entities[entity].requested_alignment,
			static_cast<std::uint64_t>(requested_alignment));
	if (complete_definition &&
		(arena_->Flags(node) & SYNTAX_FLAG_DEFINITION) != 0)
		(void)CompleteClassDefinition(node, scope, type, entity, flavor, owner,
			name, lookup_name, specialization_owner,
			specialization_identity, specialization_emission_name == 0 ?
				program_->names.Intern(spelling) : specialization_emission_name);
	return type;
}

bool SemanticAnalyzer::CollectClassDirectBases(NodeId clause, ScopeId scope,
	EntityId entity, NamedFlavor flavor,
	std::vector<DirectBaseEdge>* direct_bases)
{
	bool saw_base_specifier = false;
	for (std::uint32_t edge = arena_->FirstEdge(clause);
		edge != kNoEdge; edge = arena_->NextEdge(edge))
	{
		const NodeId base_specifier = arena_->EdgeChild(edge);
		if (!arena_->IsTag(base_specifier, "base-specifier")) continue;
		saw_base_specifier = true;
		const NodeId base_name = FindChild(base_specifier, "base-name");
		if (base_name == kNoNode)
			throw std::runtime_error("base specifier has no base name");
		AccessKind base_access = flavor == NAMED_CLASS ?
			ACCESS_PRIVATE : ACCESS_PUBLIC;
		const NodeId access = FindChild(base_specifier, "access-specifier");
		if (access != kNoNode)
		{
			const std::string access_text = PayloadSource(access);
			base_access = access_text == "private" ? ACCESS_PRIVATE :
				access_text == "protected" ? ACCESS_PROTECTED : ACCESS_PUBLIC;
		}
		std::vector<ScopeId> base_scopes;
		if (FindChild(base_specifier, "pack-expansion") != kNoNode)
		{
			if (!ExpandPackElementScopes(base_name, scope, &base_scopes))
			{
				const NameId pack_name = program_->names.Intern(
					PayloadSource(base_name));
				if (entity >= class_template_pattern_by_entity_.size() ||
					class_template_pattern_by_entity_[entity] == kNoDumpEdge)
					throw std::runtime_error(
						"pack expansion contains no unexpanded pack");
				const std::size_t pattern_index =
					class_template_pattern_by_entity_[entity];
				if (pattern_index >= class_templates_.size())
					throw std::logic_error(
						"invalid class template base-pack owner");
				const ClassTemplatePattern& pattern =
					class_templates_[pattern_index];
				std::size_t parameter_index = pattern.parameters.size();
				for (std::size_t i = 0; i < pattern.parameters.size(); ++i)
					if (pattern.parameters[i].pack &&
						pattern.parameters[i].name == pack_name)
						parameter_index = i;
				const EntityRecord& specialization = program_->entities[entity];
				if (parameter_index == pattern.parameters.size() ||
					specialization.template_argument_begin == kNoBinding ||
					parameter_index > specialization.template_argument_count)
					throw std::runtime_error(
						"pack expansion contains no bound class pack");
				const std::vector<TemplateArgument> arguments =
					StoredTemplateArguments(specialization.template_argument_begin,
						specialization.template_argument_count);
				for (std::size_t i = parameter_index; i < arguments.size(); ++i)
				{
					const ScopeId element_scope = NewScope(scope,
						SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(scope));
					TemplateParameter parameter = pattern.parameters[parameter_index];
					parameter.pack = false;
					BindTemplateArgument(element_scope, parameter, arguments[i]);
					base_scopes.push_back(element_scope);
				}
			}
		}
		else base_scopes.push_back(scope);
		for (std::size_t element = 0; element < base_scopes.size(); ++element)
		{
			LookupResult base_lookup;
			const NodeId structured_base = FindChild(
				base_name, "structured-type-name");
			base_lookup.type = structured_base == kNoNode ? kNoType :
				ResolveStructuredTypeName(structured_base, base_scopes[element]);
			if (base_lookup.type == kNoType && structured_base == kNoNode)
				base_lookup = LookupSpelling(base_scopes[element],
					arena_->Payload(base_name), LOOKUP_TYPE);
			EnsureClassDefinition(base_lookup.type);
			const EntityId base = EntityOf(base_lookup.type);
			if (base == kNoEntity || !program_->entities[base].complete ||
				program_->entities[base].flavor == NAMED_UNION)
			{
				if (base != kNoEntity &&
					FunctionTemplateTypeIsDependent(base_lookup.type))
					return false;
				throw std::runtime_error(
					"direct base must name a complete non-union class");
			}
			if (base_lookup.type_declaration != kNoBinding &&
				!CanAccessMember(base_lookup.type_declaration,
					base_lookup.naming_class))
				throw std::runtime_error("inaccessible direct base type");
			direct_bases->push_back(DirectBaseEdge(base, base_access));
		}
	}
	if (!saw_base_specifier)
		throw std::runtime_error("base clause has no base type");
	return true;
}

bool SemanticAnalyzer::CompleteClassDefinition(NodeId node, ScopeId scope,
	TypeId type, EntityId entity, NamedFlavor flavor, ScopeId owner,
	NameId name, NameId lookup_name, ScopeId specialization_owner,
	NameId specialization_identity, NameId emission_name)
{
		if (program_->entities[entity].complete)
			throw std::runtime_error("duplicate class definition");
		program_->entities[entity].packing_alignment = current_pack_alignment_;
		const NodeId base_clause = FindChild(node, "base-clause");
		if (base_clause != kNoNode)
		{
			std::vector<DirectBaseEdge> direct_bases;
			if (!CollectClassDirectBases(
				base_clause, scope, entity, flavor, &direct_bases))
			{
				program_->entities[entity].deferred_template_completion = true;
				return false;
			}
			program_->SetDirectBases(entity, direct_bases);
		}
		ScopeId member_scope = program_->entities[entity].member_scope;
		if (member_scope == kNoScope)
		{
			const std::string prefix =
				program_->names.Get(DisplayName(owner, name)) + "::";
			const ScopeId lexical_owner = specialization_owner == kNoScope ?
				owner : scope;
			member_scope = NewScope(lexical_owner, SCOPE_CLASS, lookup_name,
				program_->names.Intern(prefix));
			// Semantic lookup may use an internal specialization slot while
			// emission follows the caller's separate presentation policy.
			if (specialization_owner != kNoScope)
				program_->SetScopeEmissionName(member_scope, emission_name);
			program_->SetEntityScope(entity, member_scope);
			program_->SetTypeName(member_scope, name, type);
			const BindingId injected = program_->AddBinding(member_scope,
				BIND_TYPE, name, type, false, 0, flavor);
			program_->bindings[injected].member_owner = entity;
			program_->bindings[injected].access = ACCESS_PUBLIC;
			if (specialization_identity != 0 &&
				specialization_identity != name)
			{
				program_->SetTypeName(member_scope,
					specialization_identity, type);
				const BindingId primary_injected = program_->AddBinding(
					member_scope, BIND_TYPE, specialization_identity, type,
					false, 0, flavor);
				program_->bindings[primary_injected].member_owner = entity;
				program_->bindings[primary_injected].access = ACCESS_PUBLIC;
			}
		}
		// The stable class scope owns indexed field/function identities even
		// though class declarations are not part of the PA12 output view.
		const EntityId previous_class_context = current_class_context_;
		current_class_context_ = entity;
		AccessKind member_access = flavor == NAMED_CLASS ?
			ACCESS_PRIVATE : ACCESS_PUBLIC;
		std::vector<std::pair<BindingId, BindingId> > anonymous_alias_storage;
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
			else if (arena_->IsTag(member, "template-declaration"))
				AnalyzeTemplate(member, member_scope, member_access);
			else if (arena_->IsTag(member, "bit-field-declaration"))
				AnalyzeBitField(member, member_scope, type, member_access);
			else if (arena_->IsTag(member, "special-member-declaration") ||
				arena_->IsTag(member, "special-member-definition"))
				AnalyzeSpecialMember(member, member_scope, type, member_access);
			else if (arena_->IsTag(member, "class-specifier") ||
				arena_->IsTag(member, "class-forward-declaration"))
			{
				const bool deferred_definition =
					arena_->IsTag(member, "class-specifier") &&
					current_class_context_ <
						class_template_pattern_by_entity_.size() &&
					class_template_pattern_by_entity_[current_class_context_] !=
						kNoDumpEdge &&
					!ClassTemplateSpecializationArgumentsComplete(
						current_class_context_);
				const TypeId nested_type = AnalyzeClass(member, member_scope,
					std::string(),
					arena_->IsTag(member, "class-forward-declaration"),
					std::string(), kNoScope, 0, !deferred_definition);
				const EntityId nested = EntityOf(nested_type);
				if (nested == kNoEntity)
					throw std::logic_error("nested class has no entity");
				if (deferred_definition)
				{
					if (deferred_class_definition_by_entity_.size() <= nested)
					{
						deferred_class_definition_by_entity_.resize(
							static_cast<std::size_t>(nested) + 1, kNoNode);
						deferred_class_scope_by_entity_.resize(
							static_cast<std::size_t>(nested) + 1, kNoScope);
					}
					deferred_class_definition_by_entity_[nested] = member;
					deferred_class_scope_by_entity_[nested] = member_scope;
				}
				const BindingId declaration =
					program_->entities[nested].declaration;
				if (declaration != kNoBinding)
				{
					program_->bindings[declaration].member_owner = entity;
					program_->bindings[declaration].access = member_access;
				}
				if (arena_->Payload(member).empty() &&
					program_->entities[nested].flavor == NAMED_UNION)
				{
					std::ostringstream generated;
					generated << "__anonymous_union_storage__"
						<< arena_->TokenFirst(member) << '_'
						<< arena_->TokenLast(member);
					const NameId storage_name =
						program_->names.Intern(generated.str());
					const BindingId storage = program_->AddBinding(member_scope,
						BIND_VARIABLE, storage_name, nested_type);
					BindingRecord& storage_record = program_->bindings[storage];
					storage_record.member_owner = entity;
					storage_record.access = member_access;
					storage_record.non_static_data_member = true;
					storage_record.member_ordinal = static_cast<std::uint32_t>(
						entity_data_members_[entity].size());
					entity_data_members_[entity].push_back(storage);
					entity_layout_members_[entity].push_back(
						ClassLayoutMember(storage, nested_type));
					const std::vector<BindingId>& variants =
						entity_data_members_[nested];
					for (std::size_t i = 0; i < variants.size(); ++i)
					{
						const BindingRecord source =
							program_->bindings[variants[i]];
						if (program_->LookupDirect(member_scope, source.name,
							LOOKUP_ORDINARY).ordinary != kNoBinding)
							throw std::runtime_error(
								"anonymous union member conflicts in class scope");
						const BindingId alias = program_->AddBinding(member_scope,
							BIND_VARIABLE, source.name, source.type,
							source.constant, source.value, source.display_flavor,
							source.display_type_name);
						BindingRecord& alias_record = program_->bindings[alias];
						alias_record.member_owner = entity;
						alias_record.access = member_access;
						alias_record.non_static_data_member = true;
						alias_record.mutable_member = source.mutable_member;
						alias_record.bit_field = source.bit_field;
						alias_record.member_offset = source.member_offset;
						alias_record.bit_offset = source.bit_offset;
						alias_record.bit_width = source.bit_width;
						alias_record.bit_storage_bits = source.bit_storage_bits;
						anonymous_alias_storage.push_back(
							std::make_pair(alias, storage));
					}
				}
			}
			else if (arena_->IsTag(member, "using-declaration") ||
				arena_->IsTag(member, "alias-declaration"))
				AnalyzeUsing(member, member_scope, root_, false, member_access);
			else if (arena_->IsTag(member, "static-assert-declaration"))
				AnalyzeStaticAssert(member, member_scope);
		}
		CompleteClassPolymorphism(entity);
		CompleteClassLayout(entity);
		if (entity < entity_constructors_.size())
			for (std::size_t i = 0;
				i < entity_constructors_[entity].size(); ++i)
			{
				const BindingId constructor =
					entity_constructors_[entity][i];
				const FunctionInfo& info = GetFunction(constructor);
				if (info.defaulted_constructor &&
					info.special_member == SPECIAL_MEMBER_NONE)
					CompleteDefaultedDefaultConstructor(
						entity, constructor);
				ValidateConstexprConstructorDefinition(info);
			}
		for (std::size_t i = 0; i < anonymous_alias_storage.size(); ++i)
		{
			BindingRecord& alias =
				program_->bindings[anonymous_alias_storage[i].first];
			const BindingRecord& storage =
				program_->bindings[anonymous_alias_storage[i].second];
			alias.member_offset += storage.member_offset;
		}
		CompleteClassSpecialMembers(entity);
		if (!program_->entities[entity].has_user_declared_constructor &&
			program_->entities[entity].default_constructible)
			EnsureImplicitConstructor(entity);
		if (!program_->entities[entity].has_user_declared_destructor)
			EnsureImplicitDestructor(entity);
		program_->entities[entity].complete = true;
		ValidateConstexprClassDeclarations(entity);
		ValidateOrdinaryMemberFunctionBodies(entity);
		current_class_context_ = previous_class_context;
		return true;
}
EntityId SemanticAnalyzer::ZeroOffsetClassEntity(TypeId type) const
{
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

bool SemanticAnalyzer::VisitZeroOffsetSubobjects(EntityId root,
	std::uint32_t marker, std::uint32_t conflict_marker)
{
	zero_offset_subobject_scratch_.clear();
	zero_offset_subobject_scratch_.push_back(root);
	while (!zero_offset_subobject_scratch_.empty())
	{
		const EntityId entity = zero_offset_subobject_scratch_.back();
		zero_offset_subobject_scratch_.pop_back();
		++class_zero_offset_subobject_visits_;
		if (zero_offset_subobject_marks_[entity] == marker) continue;
		if (zero_offset_subobject_marks_[entity] == conflict_marker) return true;
		zero_offset_subobject_marks_[entity] = marker;
		const EntityRecord& record = program_->entities[entity];
		for (std::size_t base_index = 0;
			base_index < record.direct_base_count; ++base_index)
		{
			const DirectBaseEdge& base = program_->DirectBase(entity, base_index);
			if (base.offset == 0)
				zero_offset_subobject_scratch_.push_back(base.entity);
		}
		if (entity >= entity_layout_members_.size()) continue;
		const std::vector<ClassLayoutMember>& members =
			entity_layout_members_[entity];
		for (std::size_t i = 0; i < members.size(); ++i)
		{
			const ClassLayoutMember& member = members[i];
			if (member.bit_field || member.binding == kNoBinding ||
				program_->bindings[member.binding].member_offset != 0)
				continue;
			const EntityId child = ZeroOffsetClassEntity(member.type);
			if (child != kNoEntity)
				zero_offset_subobject_scratch_.push_back(child);
		}
	}
	return false;
}

bool SemanticAnalyzer::ZeroOffsetSubobjectConflict(EntityId base,
	TypeId member_type)
{
	const EntityId member = ZeroOffsetClassEntity(member_type);
	if (base == kNoEntity || member == kNoEntity) return false;
	if (zero_offset_subobject_marks_.size() < program_->entities.size())
		zero_offset_subobject_marks_.resize(program_->entities.size(), 0);
	if (zero_offset_subobject_generation_ >
		std::numeric_limits<std::uint32_t>::max() - 2)
	{
		std::fill(zero_offset_subobject_marks_.begin(),
			zero_offset_subobject_marks_.end(), 0);
		zero_offset_subobject_generation_ = 0;
	}
	const std::uint32_t base_marker = ++zero_offset_subobject_generation_;
	const std::uint32_t member_marker = ++zero_offset_subobject_generation_;
	(void)VisitZeroOffsetSubobjects(base, base_marker, base_marker);
	return VisitZeroOffsetSubobjects(member, member_marker, base_marker);
}

const EntityRecord* SemanticAnalyzer::InitializeClassBaseLayout(
	EntityId entity, std::size_t packing_alignment, std::size_t* size,
	std::size_t* alignment, std::size_t* natural_alignment)
{
	EntityRecord& owner = program_->entities[entity];
	if (!owner.has_user_declared_destructor)
	{
		owner.destructible = true;
		owner.trivial_destructor = true;
	}
	if (owner.direct_base_count == 0) return 0;
	const EntityRecord* primary = 0;
	for (std::size_t base_index = 0;
		base_index < owner.direct_base_count; ++base_index)
	{
		DirectBaseEdge& edge = program_->MutableDirectBase(entity, base_index);
		const EntityRecord* base = &program_->entities[edge.entity];
		if (!base->layout_complete)
			throw std::runtime_error("direct base layout is incomplete");
		if (base_index == 0) primary = base;
		const std::size_t base_alignment =
			static_cast<std::size_t>(base->object_alignment);
		const std::size_t effective_base_alignment = packing_alignment == 0 ?
			base_alignment : std::min(base_alignment, packing_alignment);
		std::size_t offset = 0;
		if (!base->empty_class)
		{
			if (base_index == 0 && owner.polymorphic_class &&
				!base->polymorphic_class)
				offset = AlignUp(std::max<std::size_t>(*size, 8),
					effective_base_alignment);
			else offset = AlignUp(*size, effective_base_alignment);
			if (offset > std::numeric_limits<std::size_t>::max() -
				static_cast<std::size_t>(base->object_size))
				throw std::runtime_error("class layout is too large");
			*size = offset + static_cast<std::size_t>(base->object_size);
		}
		edge.offset = offset;
		if (base_index == 0) owner.direct_base_offset = offset;
		*natural_alignment = std::max(*natural_alignment, base_alignment);
		*alignment = std::max(*alignment, effective_base_alignment);
		if (!base->destructible) owner.destructible = false;
		if (!base->trivial_destructor) owner.trivial_destructor = false;
		const BindingId destructor = DestructorForType(base->type);
		if (destructor == kNoBinding ||
			!CanAccessMember(destructor, edge.entity))
			owner.destructible = false;
	}
	return primary;
}

void SemanticAnalyzer::CompleteClassMemberDestructionFacts(EntityId entity,
	bool is_union, bool defaulted_destructor)
{
	EntityRecord& owner = program_->entities[entity];
	const std::vector<BindingId>& members = entity_data_members_[entity];
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
}

bool SemanticAnalyzer::ClassBasesAreEmpty(EntityId entity) const
{
	const EntityRecord& owner = program_->entities[entity];
	for (std::size_t base_index = 0;
		base_index < owner.direct_base_count; ++base_index)
		if (!program_->entities[
			program_->DirectBase(entity, base_index).entity].empty_class)
			return false;
	return true;
}

void SemanticAnalyzer::InitializeImplicitBaseConstructorFacts(EntityId entity)
{
	EntityRecord& owner = program_->entities[entity];
	owner.default_constructible = true;
	owner.trivial_default_constructor = !owner.polymorphic_class;
	for (std::size_t base_index = 0;
		base_index < owner.direct_base_count; ++base_index)
	{
		const EntityRecord& base = program_->entities[
			program_->DirectBase(entity, base_index).entity];
		if (!base.default_constructible)
			owner.default_constructible = false;
		if (!base.trivial_default_constructor)
			owner.trivial_default_constructor = false;
	}
}

void SemanticAnalyzer::CompleteClassLayout(EntityId entity)
{
	EntityRecord& owner = program_->entities[entity];
	if (owner.layout_complete) return;
	++class_layouts_;
	std::size_t size = 0;
	std::size_t alignment = 1;
	std::size_t natural_alignment = 1;
	const std::size_t packing_alignment =
		static_cast<std::size_t>(owner.packing_alignment);
	const EntityRecord* base = InitializeClassBaseLayout(entity,
		packing_alignment, &size, &alignment, &natural_alignment);
	const bool is_union = owner.flavor == NAMED_UNION;
	bool empty_class = ClassBasesAreEmpty(entity);
	if (owner.polymorphic_class)
	{
		empty_class = false;
		natural_alignment = std::max<std::size_t>(natural_alignment, 8);
		alignment = std::max<std::size_t>(alignment,
			packing_alignment == 0 ? 8 : std::min<std::size_t>(8,
				packing_alignment));
		if (!base || !base->polymorphic_class) size = std::max<std::size_t>(size, 8);
	}
	bool defaulted_destructor = !owner.has_user_declared_destructor;
	if (owner.has_user_declared_destructor)
	{
		const BindingId destructor = DestructorForType(owner.type);
		defaulted_destructor = destructor != kNoBinding &&
			GetFunction(destructor).defaulted_destructor;
	}
	const std::vector<ClassLayoutMember>& layout_members =
		entity_layout_members_[entity];
	const bool implicit_default_constructor =
		!owner.has_user_declared_constructor;
	owner.is_aggregate = !owner.has_user_provided_constructor &&
		!owner.has_direct_base && !owner.polymorphic_class;
	if (implicit_default_constructor)
		InitializeImplicitBaseConstructorFacts(entity);
	bool active_bit_unit = false;
	std::size_t active_bit_offset = 0;
	std::size_t active_bit_size = 0;
	std::size_t active_bit_alignment = 0;
	std::size_t active_bit_used = 0;
	for (std::size_t i = 0; i < layout_members.size(); ++i)
	{
		++class_layout_member_visits_;
		const ClassLayoutMember& layout = layout_members[i];
		EnsureClassDefinition(layout.type);
		BindingRecord* member = layout.binding == kNoBinding ? 0 :
			&program_->bindings[layout.binding];
		if (is_union && member && member->has_default_member_initializer)
		{
			if (owner.union_default_member != kNoBinding &&
				owner.union_default_member != layout.binding)
				throw std::runtime_error(
					"union has multiple default member initializers");
			owner.union_default_member = layout.binding;
		}
		if (member && (member->has_default_member_initializer ||
			member->access != ACCESS_PUBLIC))
			owner.is_aggregate = false;
		const std::size_t member_size = program_->SizeOf(layout.type);
		const std::size_t type_alignment = program_->AlignOf(layout.type);
		const std::size_t requested_member_alignment = member ?
			static_cast<std::size_t>(member->requested_alignment) : 0;
		const std::size_t required_alignment = std::max(type_alignment,
			requested_member_alignment);
		std::size_t member_alignment = packing_alignment == 0 ? type_alignment :
			std::min(type_alignment, packing_alignment);
		member_alignment = std::max(member_alignment,
			requested_member_alignment);
		if (layout.bit_field)
		{
			const std::size_t unit_bits = member_size * 8;
			if (layout.bit_width == 0)
			{
				active_bit_unit = false;
				if (!is_union) size = AlignUp(size, member_alignment);
				continue;
			}
			empty_class = false;
			natural_alignment = std::max(natural_alignment,
				required_alignment);
			alignment = std::max(alignment, member_alignment);
			const std::size_t declared_width = layout.bit_width;
			const std::size_t allocation_size = declared_width <= unit_bits ?
				member_size : (declared_width + 7) / 8;
			if (is_union)
			{
				if (member)
				{
					member->member_offset = 0;
					member->bit_offset = 0;
					member->bit_storage_bits =
						static_cast<std::uint32_t>(unit_bits);
				}
				size = std::max(size, allocation_size);
				continue;
			}
			if (declared_width > unit_bits)
			{
				active_bit_unit = false;
				const std::size_t offset = AlignUp(size, member_alignment);
				if (offset > std::numeric_limits<std::size_t>::max() -
					allocation_size)
					throw std::runtime_error("class layout is too large");
				if (member)
				{
					member->member_offset = offset;
					member->bit_offset = 0;
					member->bit_storage_bits =
						static_cast<std::uint32_t>(unit_bits);
				}
				size = offset + allocation_size;
				continue;
			}
			const bool reuse = active_bit_unit &&
				active_bit_size == member_size &&
				active_bit_alignment == member_alignment &&
				active_bit_used + layout.bit_width <= unit_bits;
			if (!reuse)
			{
				active_bit_offset = AlignUp(size, member_alignment);
				if (active_bit_offset >
					std::numeric_limits<std::size_t>::max() - member_size)
					throw std::runtime_error("class layout is too large");
				size = active_bit_offset + member_size;
				active_bit_size = member_size;
				active_bit_alignment = member_alignment;
				active_bit_used = 0;
				active_bit_unit = true;
			}
			if (member)
			{
				member->member_offset = active_bit_offset;
				member->bit_offset = static_cast<std::uint32_t>(active_bit_used);
				member->bit_storage_bits = static_cast<std::uint32_t>(unit_bits);
			}
			active_bit_used += layout.bit_width;
			continue;
		}
		natural_alignment = std::max(natural_alignment, required_alignment);
		alignment = std::max(alignment, member_alignment);
		active_bit_unit = false;
		if (!member)
			throw std::logic_error("ordinary layout member has no binding");
		empty_class = false;
		if (is_union)
		{
			member->member_offset = 0;
			size = std::max(size, member_size);
		}
		else
		{
			if (size == 0)
				for (std::size_t base_index = 0;
					base_index < owner.direct_base_count; ++base_index)
					if (program_->DirectBase(entity, base_index).offset == 0 &&
						ZeroOffsetSubobjectConflict(program_->DirectBase(
							entity, base_index).entity, layout.type))
					{
						size = 1;
						break;
					}
			size = AlignUp(size, member_alignment);
			member->member_offset = size;
			if (size > std::numeric_limits<std::size_t>::max() - member_size)
				throw std::runtime_error("class layout is too large");
			size += member_size;
		}
		if (!implicit_default_constructor) continue;
		if (member->has_default_member_initializer)
		{
			owner.trivial_default_constructor = false;
			continue;
		}
		TypeId member_type = member->type;
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
	CompleteClassMemberDestructionFacts(entity, is_union,
		defaulted_destructor);
	if (owner.requested_alignment != 0 &&
		owner.requested_alignment < natural_alignment)
		throw std::runtime_error(
			"requested alignment is weaker than the natural class alignment");
	alignment = std::max(alignment,
		static_cast<std::size_t>(owner.requested_alignment));
	if (size == 0) size = 1;
	size = AlignUp(size, alignment);
	owner.object_size = size;
	owner.object_alignment = alignment;
	owner.natural_alignment = natural_alignment;
	owner.empty_class = empty_class;
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
	binding.inline_function = true;
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
	bool friend_specifier = false;
	for (std::uint32_t edge = arena_->FirstEdge(specifiers); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (PayloadSource(arena_->EdgeChild(edge)) == "friend")
			friend_specifier = true;
	if (friend_specifier &&
		FindChild(specifiers, "class-forward-declaration") != kNoNode)
	{
		AnalyzeFriendClass(node, scope, owner_type);
		return;
	}
	const SpecInfo spec = BuildSpecifiers(specifiers, scope, std::string(), true);
	if (spec.is_friend)
	{
		const NodeId declarators = FindChild(node, "init-declarator-list");
		if (arena_->IsTag(node, "simple-declaration") &&
			(declarators == kNoNode ||
			 FirstSemanticChild(declarators) == kNoNode))
		{
			const EntityId owner = EntityOf(owner_type);
			const EntityId friend_entity = EntityOf(spec.type);
			if (owner == kNoEntity || friend_entity == kNoEntity)
				throw std::runtime_error(
					"friend type declaration does not name a class");
			const std::uint64_t key =
				(static_cast<std::uint64_t>(owner) << 32) | friend_entity;
			CompactIndexSequence& grants = friend_class_grants_.Ensure(key);
			if (grants.Size() == 0) grants.Push(0);
			return;
		}
		AnalyzeFriendFunction(node, scope, owner_type, spec);
		return;
	}
	if (arena_->IsTag(node, "function-definition"))
	{
		if (spec.thread_local_storage)
			throw std::runtime_error("thread_local member function");
		const NodeId declarator = FindChild(node, "declarator");
		DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, scope,
			false, spec.storage_class != STORAGE_CLASS_STATIC);
		const EntityId owner_entity = EntityOf(owner_type);
		if (spec.is_constexpr)
			parsed.type = ApplyConstexprMemberFunctionType(parsed.type,
				owner_entity, spec.storage_class == STORAGE_CLASS_STATIC);
		const bool constexpr_function = spec.is_constexpr &&
			(!IsClassTemplateSpecializationContext(owner_entity) ||
			 IsConstexprCallableType(parsed.type, false));
		if (spec.is_constexpr && constexpr_function)
			ValidateConstexprCallableType(parsed.type, false);
		const BindingId function = DeclareFunction(scope, parsed.name,
			parsed.type, parsed.parameters, true, false, STORAGE_CLASS_NONE,
			current_language_linkage_, IsNonthrowing(declarator, scope));
		FunctionInfo& info = GetMutableFunction(function);
		info.constexpr_function = info.constexpr_function || constexpr_function;
		BindingRecord& binding = program_->bindings[function];
		binding.member_owner = owner_entity;
		binding.access = access;
		binding.static_member_function =
			spec.storage_class == STORAGE_CLASS_STATIC ||
			binding.operator_kind == OPERATOR_NEW ||
			binding.operator_kind == OPERATOR_NEW_ARRAY ||
			binding.operator_kind == OPERATOR_DELETE ||
			binding.operator_kind == OPERATOR_DELETE_ARRAY;
		if (!binding.static_member_function) info.member_owner = owner_type;
		ValidateFunctionRefQualifier(function);
		ConfigureVirtualFunction(function, spec, declarator, kNoNode);
		info.definition_body =
			FindChild(node, "compound-statement");
		info.deferred = true;
		info.definition_in_class = true;
		ConfigureAssignmentSpecialMember(function, kNoNode);
		RegisterClassMemberFunction(owner_entity, function);
		PublishInlineFunctionFacts(function, true);
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
		DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, scope,
			false, spec.storage_class != STORAGE_CLASS_STATIC &&
				FindChild(declarator, "parameter-clause") != kNoNode);
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
			const EntityId owner_entity = EntityOf(owner_type);
			if (spec.is_constexpr)
				parsed.type = ApplyConstexprMemberFunctionType(parsed.type,
					owner_entity, spec.storage_class == STORAGE_CLASS_STATIC);
			const bool constexpr_function = spec.is_constexpr &&
				(!IsClassTemplateSpecializationContext(owner_entity) ||
				 IsConstexprCallableType(parsed.type, false));
			if (spec.is_constexpr && constexpr_function)
				ValidateConstexprCallableType(parsed.type, false);
			const BindingId function = DeclareFunction(scope, parsed.name,
				parsed.type, parsed.parameters, false, false, STORAGE_CLASS_NONE,
				current_language_linkage_, IsNonthrowing(declarator, scope));
			BindingRecord& binding = program_->bindings[function];
			binding.member_owner = EntityOf(owner_type);
			binding.access = access;
			binding.static_member_function =
				spec.storage_class == STORAGE_CLASS_STATIC ||
				binding.operator_kind == OPERATOR_NEW ||
				binding.operator_kind == OPERATOR_NEW_ARRAY ||
				binding.operator_kind == OPERATOR_DELETE ||
				binding.operator_kind == OPERATOR_DELETE_ARRAY;
			if (!binding.static_member_function)
				GetMutableFunction(function).member_owner = owner_type;
			GetMutableFunction(function).constexpr_function =
				GetFunction(function).constexpr_function || constexpr_function;
			ValidateFunctionRefQualifier(function);
			ConfigureVirtualFunction(function, spec, declarator,
				FindChild(item, "initializer"));
			if (binding.operator_kind >= OPERATOR_NEW &&
				binding.operator_kind <= OPERATOR_DELETE_ARRAY)
				GetMutableFunction(function).deferred = true;
			ConfigureAssignmentSpecialMember(
				function, FindChild(item, "initializer"));
			RegisterClassMemberFunction(EntityOf(owner_type), function);
			PublishInlineFunctionFacts(
				function, spec.inline_specifier || constexpr_function);
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
			if (spec.is_constexpr && !IsConstexprLiteralType(member_type))
				throw std::runtime_error(
					"constexpr static data member does not have literal type");
			const LookupResult occupied =
				program_->LookupDirect(scope, parsed.name, LOOKUP_ORDINARY);
			if (parsed.name != 0 && occupied.ordinary != kNoBinding)
				throw std::runtime_error("duplicate or conflicting class member");
			const BindingId member = program_->AddBinding(scope, BIND_VARIABLE,
				parsed.name, member_type, false, 0, NAMED_NONE, 0, kNoBinding,
				false);
			const std::size_t requested_alignment =
				RequestedAlignment(node, scope);
			const bool non_static_data_member =
				spec.storage_class == STORAGE_CLASS_NONE;
			const bool has_default_member_initializer =
				non_static_data_member &&
				FindChild(item, "initializer") != kNoNode;
			{
				BindingRecord& binding = program_->bindings[member];
				binding.storage_class = spec.storage_class;
				binding.thread_local_storage = spec.thread_local_storage;
				binding.mutable_member = spec.mutable_member;
				binding.member_owner = EntityOf(owner_type);
				binding.access = access;
				binding.requested_alignment = requested_alignment;
				binding.non_static_data_member = non_static_data_member;
				binding.has_default_member_initializer =
					has_default_member_initializer;
			}
			if (!non_static_data_member &&
				FindChild(item, "initializer") != kNoNode)
			{
				if (!spec.is_constexpr &&
					!(IsConst(member_type) && IsIntegral(member_type, true)))
					throw std::runtime_error(
						"invalid in-class static data member initializer");
				const ExpressionInfo value = AnalyzeInClassStaticInitializer(
					FindChild(item, "initializer"), scope, member_type);
				if (!value.constant)
					throw std::runtime_error(
						"nonconstant in-class static data member initializer");
				const TypeRecord declared_array = program_->types.Get(
					program_->types.RemoveTopCv(member_type));
				const TypeRecord completed_array = program_->types.Get(
					program_->types.RemoveTopCv(value.type));
				if (declared_array.kind == TYPE_ARRAY &&
					declared_array.bound == 0 && completed_array.kind == TYPE_ARRAY &&
					completed_array.bound != 0)
				{
					member_type = spec.is_constexpr ?
						program_->types.Qualify(value.type, CV_CONST) : value.type;
					program_->bindings[member].type = member_type;
				}
				PublishConstantVariableInitializer(
					member, member_type, spec, value);
			}
			else if (!non_static_data_member && spec.is_constexpr)
				throw std::runtime_error(
					"constexpr static data member requires initializer");
			if (member_initializer_by_binding_.size() <= member)
				member_initializer_by_binding_.resize(
					static_cast<std::size_t>(member) + 1, kNoNode);
			if (has_default_member_initializer)
				member_initializer_by_binding_[member] =
					FindChild(item, "initializer");
			const EntityId entity = EntityOf(owner_type);
			if (non_static_data_member && entity_data_members_.size() <= entity)
				entity_data_members_.resize(static_cast<std::size_t>(entity) + 1);
			if (non_static_data_member)
			{
				program_->bindings[member].member_ordinal = static_cast<std::uint32_t>(
					entity_data_members_[entity].size());
				entity_data_members_[entity].push_back(member);
				if (entity_layout_members_.size() <= entity)
					entity_layout_members_.resize(
						static_cast<std::size_t>(entity) + 1);
				entity_layout_members_[entity].push_back(
					ClassLayoutMember(member, member_type));
			}
		}
	}
}

void SemanticAnalyzer::AnalyzeBitField(NodeId node, ScopeId scope,
	TypeId owner_type, AccessKind access)
{
	const NodeId specifiers = FindChild(node, "decl-specifier-seq");
	const SpecInfo spec = BuildSpecifiers(specifiers, scope, std::string(), true);
	if (!IsIntegral(spec.type, true) || spec.storage_class != STORAGE_CLASS_NONE)
		throw std::runtime_error("invalid bit-field type or storage class");
	const EntityId entity = EntityOf(owner_type);
	if (entity == kNoEntity)
		throw std::logic_error("bit-field has no class owner");
	if (entity_layout_members_.size() <= entity)
		entity_layout_members_.resize(static_cast<std::size_t>(entity) + 1);
	if (entity_data_members_.size() <= entity)
		entity_data_members_.resize(static_cast<std::size_t>(entity) + 1);
	if (FindChild(node, "alignment-specifier") != kNoNode)
		throw std::runtime_error("alignment specifier cannot apply to a bit-field");
	const TypeId value_type = program_->types.RemoveTopCv(spec.type);
	const TypeRecord& value_record = program_->types.Get(value_type);
	const bool boolean_field = value_record.kind == TYPE_FUNDAMENTAL &&
		value_record.fundamental == FUND_BOOL;
	const std::size_t value_bits = boolean_field ? 1 :
		program_->SizeOf(spec.type) * 8;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId field = arena_->EdgeChild(edge);
		if (!arena_->IsTag(field, "bit-field-declarator")) continue;
		const NodeId declarator = FindChild(field, "declarator");
		NodeId width_node = kNoNode;
		for (std::uint32_t child_edge = arena_->FirstEdge(field);
			child_edge != kNoEdge; child_edge = arena_->NextEdge(child_edge))
		{
			const NodeId child = arena_->EdgeChild(child_edge);
			if (child != declarator) width_node = child;
		}
		if (width_node == kNoNode)
			throw std::runtime_error("bit-field has no width");
		const ExpressionInfo width_expression =
			AnalyzeExpression(width_node, scope);
		if (!width_expression.constant || width_expression.value < 0 ||
			static_cast<std::uint64_t>(width_expression.value) >
				std::numeric_limits<std::uint32_t>::max())
			throw std::runtime_error("invalid bit-field width");
		const std::uint32_t width =
			static_cast<std::uint32_t>(width_expression.value);
		BindingId binding_id = kNoBinding;
		NameId name = 0;
		if (declarator != kNoNode)
		{
			const DeclaratorInfo parsed =
				BuildDeclarator(declarator, spec.type, scope);
			name = parsed.name;
			if (name == 0 || parsed.type != spec.type)
				throw std::runtime_error("invalid bit-field declarator");
		}
		if (name != 0)
		{
			if (width == 0)
				throw std::runtime_error("named zero-width bit-field");
			const LookupResult occupied =
				program_->LookupDirect(scope, name, LOOKUP_ORDINARY);
			if (occupied.ordinary != kNoBinding)
				throw std::runtime_error("duplicate or conflicting class member");
			binding_id = program_->AddBinding(scope, BIND_VARIABLE, name,
				spec.type, false, 0, NAMED_NONE, 0, kNoBinding, false);
			BindingRecord& binding = program_->bindings[binding_id];
			binding.member_owner = entity;
			binding.access = access;
			binding.non_static_data_member = true;
			binding.bit_field = true;
			binding.bit_width = static_cast<std::uint32_t>(std::min(
				static_cast<std::size_t>(width), value_bits));
			binding.member_ordinal = static_cast<std::uint32_t>(
				entity_data_members_[entity].size());
			entity_data_members_[entity].push_back(binding_id);
		}
		entity_layout_members_[entity].push_back(
			ClassLayoutMember(binding_id, spec.type, width, true));
	}
}

void SemanticAnalyzer::PublishVariableDeclarationFacts(BindingId binding,
	ScopeId declaration_scope, NameId name, TypeId type,
	const SpecInfo& spec, bool local)
{
	BindingRecord& record = program_->bindings[binding];
	record.language_linkage = current_language_linkage_;
	record.storage_class = spec.storage_class;
	if (!local && HasInternalLinkageScope(declaration_scope))
	{
		record.storage_class = STORAGE_CLASS_STATIC;
		record.unnamed_namespace_linkage = true;
	}
	record.thread_local_storage = spec.thread_local_storage;
	const TypeRecord top_type = program_->types.Get(type);
	if (!local && record.storage_class == STORAGE_CLASS_NONE &&
		top_type.kind == TYPE_QUALIFIED && (top_type.cv & CV_CONST) != 0)
		record.storage_class = STORAGE_CLASS_STATIC;
	InheritVariableRedeclarationFacts(binding);
	BindingRecord& canonical = program_->bindings[record.canonical];
	canonical.unnamed_namespace_linkage =
		canonical.unnamed_namespace_linkage || record.unnamed_namespace_linkage;
	canonical.language_linkage = record.language_linkage;
	if (canonical.storage_class == STORAGE_CLASS_NONE ||
		record.storage_class == STORAGE_CLASS_STATIC)
		canonical.storage_class = record.storage_class;
	if (record.canonical != binding && canonical.thread_local_storage !=
		record.thread_local_storage)
		throw std::runtime_error("thread_local redeclaration mismatch");
	canonical.thread_local_storage = record.thread_local_storage;
	if (!local) record.qualified_name = EmissionName(declaration_scope, name);
}

void SemanticAnalyzer::AnalyzeSpecialMember(NodeId node, ScopeId scope,
	TypeId owner_type, AccessKind access)
{
	const EntityId entity = EntityOf(owner_type);
	if (entity == kNoEntity) throw std::logic_error("special member has no class");
	const NodeId declarator = FindChild(node, "declarator");
	if (declarator != kNoNode &&
		FindChild(declarator, "conversion-type-id") != kNoNode)
	{
		AnalyzeConversionFunction(node, scope, owner_type, access);
		return;
	}
	const std::string special_name = arena_->Payload(node);
	const std::string class_name =
		program_->names.Get(program_->entities[entity].identity_name);
	const NodeId member_specifiers = FindChild(node, "member-specifiers");
	bool virtual_member_specifier = false;
	if (member_specifiers != kNoNode)
		for (std::uint32_t edge = arena_->FirstEdge(member_specifiers);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
			if (PayloadSource(arena_->EdgeChild(edge)) == "virtual")
				virtual_member_specifier = true;
	if (special_name == "~" + class_name)
	{
		EntityRecord& class_record = program_->entities[entity];
		class_record.has_user_declared_destructor = true;
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
		binding.inline_function = source_definition || defaulted;
		const NodeId specifiers = member_specifiers;
		if (specifiers != kNoNode)
			for (std::uint32_t edge = arena_->FirstEdge(specifiers);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
				if (PayloadSource(arena_->EdgeChild(edge)) == "inline")
					binding.inline_function = true;
		FunctionInfo& info = GetMutableFunction(destructor);
		info.member_owner = owner_type;
		info.destructor = true;
		ValidateFunctionRefQualifier(destructor);
		SpecInfo virtual_spec;
		if (specifiers != kNoNode)
			for (std::uint32_t edge = arena_->FirstEdge(specifiers);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
				if (PayloadSource(arena_->EdgeChild(edge)) == "virtual")
					virtual_spec.virtual_specifier = true;
		ConfigureVirtualFunction(destructor, virtual_spec, declarator,
			initializer);
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
	if (virtual_member_specifier ||
		(declarator != kNoNode &&
		 FindChild(declarator, "virt-specifier") != kNoNode))
		throw std::runtime_error("constructor cannot have a virtual specifier");

	EntityRecord& class_record = program_->entities[entity];
	class_record.has_user_declared_constructor = true;
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
	if (info.complete_constructor == kNoBinding)
		info.complete_constructor = info.binding;
	ValidateFunctionRefQualifier(constructor);
	info.defaulted_constructor = info.defaulted_constructor || defaulted;
	info.deleted_constructor = info.deleted_constructor || deleted;
	const NodeId specifiers = FindChild(node, "member-specifiers");
	if (specifiers != kNoNode)
		for (std::uint32_t edge = arena_->FirstEdge(specifiers); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const std::string value = PayloadSource(arena_->EdgeChild(edge));
			if (value == "explicit") info.explicit_constructor = true;
			else if (value == "constexpr") info.constexpr_function = true;
			else if (value == "inline") binding.inline_function = true;
		}
	if (info.constexpr_function)
	{
		if (IsClassTemplateSpecializationContext(entity) &&
			!IsConstexprCallableType(info.type, true))
			info.constexpr_function = false;
		else ValidateConstexprCallableType(info.type, true);
	}
	PublishInlineFunctionFacts(constructor, source_definition || defaulted ||
		info.constexpr_function || binding.inline_function);
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
	RegisterClassSpecialMember(constructor);
}
TypeId SemanticAnalyzer::AnalyzeEnum(NodeId node, ScopeId scope, const std::string& hint, bool elaborated)
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
		program_->entities[entity].local_context = LocalTypeContext(
			*program_, owner, current_function_context_);
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
	const std::string& hint, bool has_declarators, bool type_id_context,
	TypeId deferred_type)
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
			if (arena_->IsTag(child, "class-forward-declaration"))
			{
				const NodeId structured = FindChild(
					child, "structured-type-name");
				result.type = structured == kNoNode ? kNoType :
					ResolveStructuredTypeName(structured, scope);
			}
			if (result.type == kNoType)
				result.type = AnalyzeClass(child, scope, hint,
					(has_declarators || type_id_context) &&
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
		const NodeId structured_name =
			FindChild(child, "structured-type-name");
		if (structured_name != kNoNode)
		{
			const NamePath structured_path =
				StructuredNamePath(structured_name);
			if (deferred_type != kNoType &&
				(structured_path.global || structured_path.Size() > 1))
			{
				result.type = deferred_type;
				continue;
			}
			const LookupResult found = LookupStructuredName(
				structured_name, scope, LOOKUP_TYPE);
			result.type = found.type;
			if (result.type == kNoType)
				throw std::runtime_error(
					"structured template type was not found: " +
					PayloadSource(child));
			if (found.type_declaration != kNoBinding &&
				!CanAccessMember(found.type_declaration, found.naming_class))
				throw std::runtime_error("inaccessible member type");
			continue;
		}
		if (arena_->IsTag(child, "decltype-specifier") ||
			(arena_->IsTag(child, "decl-specifier") &&
			 FirstSemanticChild(child) != kNoNode))
		{
			if (deferred_type != kNoType)
			{ result.type = deferred_type; continue; }
			result.type = DecltypeType(FirstSemanticChild(child), scope);
			const NodeId qualified = FindChild(child, "qualified-type-name");
			if (qualified != kNoNode)
			{
				const ScopeId carrier = program_->ScopeForType(
					EffectiveType(result.type));
				if (carrier == kNoScope)
					throw std::runtime_error(
						"decltype qualifier does not name a class type");
				const LookupResult found = LookupStructuredName(
					qualified, carrier, LOOKUP_TYPE);
				if (found.type == kNoType)
					throw std::runtime_error(
						"qualified decltype type was not found");
				if (found.type_declaration != kNoBinding &&
					!CanAccessMember(found.type_declaration,
						found.naming_class))
					throw std::runtime_error(
						"inaccessible qualified decltype type");
				result.type = found.type;
			}
			continue;
		}
		if (!arena_->IsTag(child, "cv-qualifier") &&
			!arena_->IsTag(child, "decl-specifier") &&
			!arena_->IsTag(child, "type-specifier") &&
			!arena_->IsTag(child, "type-name")) continue;
		const std::string spelling = PayloadSource(child);
		if (spelling == "typedef") result.is_typedef = true;
		else if (spelling == "constexpr") result.is_constexpr = true;
		else if (spelling == "friend") result.is_friend = true;
		else if (spelling == "inline") result.inline_specifier = true;
		else if (spelling == "extern") result.storage_class = STORAGE_CLASS_EXTERN;
		else if (spelling == "static") result.storage_class = STORAGE_CLASS_STATIC;
		else if (spelling == "thread_local")
			result.thread_local_storage = true;
		else if (spelling == "auto") result.placeholder_auto = true;
		else if (spelling == "mutable") result.mutable_member = true;
		else if (spelling == "virtual") result.virtual_specifier = true;
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
		else if (spelling != "explicit")
		{
			if (deferred_type != kNoType)
			{ result.type = deferred_type; continue; }
			const LookupResult found = LookupSpelling(scope, spelling, LOOKUP_TYPE);
			if (found.type == kNoType)
				throw std::runtime_error("unknown type name: " + spelling);
			const TypeRecord& named = program_->types.Get(
				program_->types.RemoveTopCv(found.type));
			if (named.kind == TYPE_NAMED &&
				program_->entities[named.entity].flavor ==
					NAMED_TEMPLATE_PARAMETER)
				throw std::runtime_error(
					"class template name requires template arguments");
			if (found.type_declaration != kNoBinding &&
				!CanAccessMember(found.type_declaration,
					found.naming_class))
				throw std::runtime_error("inaccessible member type");
			result.type = found.type;
		}
	}
	if (result.type == kNoType)
	{
		if (result.placeholder_auto)
		{
			result.type = program_->types.Fundamental(FUND_VOID);
			return result;
		}
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
	// Cv-qualifiers applied through a typedef-name (including a bound template
	// type parameter) do not create a cv-qualified function type.
	if (!program_->types.IsFunction(result.type))
		result.type = program_->types.Qualify(result.type, cv);
	return result;
}

TypeId SemanticAnalyzer::BuildTypeId(NodeId node, ScopeId scope)
{
	if (node == kNoNode) throw std::runtime_error("missing type-id");
	NodeId specifiers = FindChild(node, "type-specifier-seq");
	if (specifiers == kNoNode)
		specifiers = FindChild(node, "decl-specifier-seq");
	const SpecInfo spec = BuildSpecifiers(
		specifiers, scope, std::string(), false, true);
	NodeId declarator = FindChild(node, "abstract-declarator");
	if (declarator == kNoNode) declarator = FindChild(node, "declarator");
	return declarator == kNoNode ? spec.type :
		BuildDeclarator(declarator, spec.type, scope,
			spec.placeholder_auto).type;
}

NamePath SemanticAnalyzer::DeclaratorNamePath(NodeId node)
{
	const NodeId identifier = FindChild(node, "identifier");
	if (identifier != kNoNode)
	{
		const NodeId structure = FindChild(
			identifier, "structured-type-name");
		return structure == kNoNode ?
			ParseNamePath(arena_->Payload(identifier)) :
			StructuredNamePath(structure);
	}
	const NodeId nested = FindChild(node, "nested-declarator");
	return nested == kNoNode ? NamePath() :
		DeclaratorNamePath(FirstSemanticChild(nested));
}

NodeId SemanticAnalyzer::DeclaratorNameStructure(NodeId node) const
{
	const NodeId identifier = FindChild(node, "identifier");
	if (identifier != kNoNode)
		return FindChild(identifier, "structured-type-name");
	const NodeId nested = FindChild(node, "nested-declarator");
	return nested == kNoNode ? kNoNode :
		DeclaratorNameStructure(FirstSemanticChild(nested));
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
	const ScopeId parameter_scope = NewScope(scope, SCOPE_FUNCTION, 0,
		ScopePrefixId(scope));
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
		const bool declared_pack = declarator != kNoNode &&
			FindChild(declarator, "parameter-pack") != kNoNode;
		if (declared_pack)
		{
			std::vector<ScopeId> element_scopes;
			if (ExpandPackElementScopes(child, parameter_scope, &element_scopes))
			{
				const NameId parameter_pack_name = DeclaratorName(declarator);
				const std::string parameter_pack_spelling = parameter_pack_name == 0 ?
					std::string() : program_->names.Get(parameter_pack_name);
				for (std::size_t i = 0; i < element_scopes.size(); ++i)
				{
					const SpecInfo element_spec = BuildSpecifiers(
						specifiers, element_scopes[i], std::string(), true);
					const DeclaratorInfo parsed = BuildDeclarator(
						declarator, element_spec.type, element_scopes[i]);
					const NameId element_name = parameter_pack_name == 0 ? 0 :
						i == 0 ? parameter_pack_name : program_->names.Intern(
							parameter_pack_spelling + "__pack" +
							std::to_string(i + 1));
					ParameterInfo parameter(element_name, parsed.type,
						AdjustParameterType(parsed.type));
					parameter.pack_name = parameter_pack_name;
					result.push_back(parameter);
				}
				continue;
			}
		}
		const SpecInfo spec = BuildSpecifiers(specifiers, parameter_scope,
			std::string(), declarator != kNoNode);
		NameId name = 0;
		TypeId declared = spec.type;
		if (declarator != kNoNode)
		{
			bool parenthesized_parameter_name = false;
			if (DeclaratorName(declarator) == 0)
			{
				const NodeId clause = FindChild(declarator, "parameter-clause");
				const std::uint32_t first_edge = clause == kNoNode ? kNoEdge :
					arena_->FirstEdge(clause);
				const NodeId provisional = first_edge == kNoEdge ? kNoNode :
					arena_->EdgeChild(first_edge);
				if (provisional != kNoNode &&
					arena_->NextEdge(first_edge) == kNoEdge &&
					arena_->IsTag(provisional, "parameter-declaration") &&
					FindChild(provisional, "declarator") == kNoNode)
				{
					const NodeId provisional_specifiers =
						FindChild(provisional, "decl-specifier-seq");
					const std::uint32_t spelling_edge =
						provisional_specifiers == kNoNode ? kNoEdge :
						arena_->FirstEdge(provisional_specifiers);
					const NodeId spelling_node = spelling_edge == kNoEdge ?
						kNoNode : arena_->EdgeChild(spelling_edge);
					if (spelling_node != kNoNode &&
						arena_->NextEdge(spelling_edge) == kNoEdge)
					{
						const std::string spelling = PayloadSource(spelling_node);
						const bool identifier = !spelling.empty() &&
							(std::isalpha(static_cast<unsigned char>(spelling[0])) ||
							 spelling[0] == '_');
						const LookupResult type_name = identifier ?
							LookupSpelling(parameter_scope, spelling, LOOKUP_TYPE) :
							LookupResult();
						if (identifier && type_name.type == kNoType)
						{
							name = program_->names.Intern(spelling);
							parenthesized_parameter_name = true;
						}
					}
				}
			}
			if (!parenthesized_parameter_name)
			{
				const DeclaratorInfo parsed =
					BuildDeclarator(declarator, spec.type, parameter_scope,
						spec.placeholder_auto);
				name = parsed.name;
				declared = parsed.type;
			}
			if (declared_pack)
				*variadic = true;
		}
		result.push_back(ParameterInfo(name, declared,
			AdjustParameterType(declared)));
		if (declared_pack) result.back().pack_name = name;
		if (name != 0)
			program_->AddBinding(parameter_scope, BIND_PARAMETER,
				name, declared);
		const NodeId default_node = FindChild(child, "default-argument");
		if (default_node != kNoNode)
		{
			NodeId default_expression = FirstSemanticChild(default_node);
			if (default_expression != kNoNode &&
				arena_->IsTag(default_expression, "initializer"))
				default_expression = FirstSemanticChild(default_expression);
			result.back().default_argument = default_expression;
			result.back().default_scope = parameter_scope;
		}
	}
	if (result.size() == 1 && result[0].name == 0 &&
		IsVoid(result[0].declared_type)) result.clear();
	return result;
}

DeclaratorInfo SemanticAnalyzer::BuildDeclarator(NodeId node, TypeId base,
	ScopeId scope, bool placeholder_auto, bool member_implicit_object,
	bool defer_trailing_return)
{
	DeclaratorInfo result;
	result.name = DeclaratorName(node);
	TypeId type = base;
	const NodeId trailing = FindChild(node, "trailing-return-type");
	std::vector<NodeId> suffixes;
	NodeId nested = kNoNode;
	std::uint8_t function_cv = CV_NONE;
	std::uint8_t function_ref = FUNCTION_REF_NONE;
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
		else if (arena_->IsTag(child, "ref-qualifier"))
		{
			if (function_ref != FUNCTION_REF_NONE)
				throw std::runtime_error("duplicate function ref-qualifier");
			function_ref = PayloadSource(child) == "&" ?
				FUNCTION_REF_LVALUE : FUNCTION_REF_RVALUE;
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
	NodeId trailing_parameter_clause = kNoNode;
	std::vector<ParameterInfo> trailing_parameters;
	bool trailing_variadic = false;
	if (trailing != kNoNode)
	{
		for (std::size_t i = 0; i < suffixes.size(); ++i)
			if (arena_->IsTag(suffixes[i], "parameter-clause"))
			{
				trailing_parameter_clause = suffixes[i];
				trailing_parameters = BuildParameters(suffixes[i], scope,
					&trailing_variadic);
				break;
			}
		ScopeId return_scope = scope;
		if (trailing_parameter_clause != kNoNode)
		{
			return_scope = NewScope(scope, SCOPE_FUNCTION, result.name,
				ScopePrefixId(scope));
			for (std::size_t i = 0; i < trailing_parameters.size(); ++i)
				if (trailing_parameters[i].name != 0)
					program_->AddBinding(return_scope, BIND_PARAMETER,
						trailing_parameters[i].name,
						ParameterBindingType(trailing_parameters[i]));
			if (member_implicit_object && current_class_context_ != kNoEntity)
			{
				TypeId object =
					program_->entities[current_class_context_].type;
				if (function_cv != CV_NONE)
					object = program_->types.Qualify(object, function_cv);
				program_->AddBinding(return_scope, BIND_PARAMETER,
					program_->names.Intern("this"),
					program_->types.Pointer(object));
			}
		}
		if (!defer_trailing_return)
		{
			const NodeId return_type = FindChild(trailing, "type-id");
			if (return_type == kNoNode)
				throw std::runtime_error(
					"trailing return type is missing its type-id");
			type = BuildTypeId(return_type, return_scope);
		}
	}
	else if (placeholder_auto)
		throw std::runtime_error("auto requires a trailing return type in PA16");
	for (std::size_t i = suffixes.size(); i != 0; --i)
	{
		const NodeId suffix = suffixes[i - 1];
		if (arena_->IsTag(suffix, "array-suffix"))
		{
			const NodeId bound_node = FirstSemanticChild(suffix);
			std::uint64_t bound = 0;
			if (bound_node != kNoNode)
			{
				++constant_expression_required_depth_;
				ExpressionInfo expression;
				try
				{
					expression = AnalyzeExpression(bound_node, scope);
					--constant_expression_required_depth_;
				}
				catch (...)
				{
					--constant_expression_required_depth_;
					throw;
				}
				if (!expression.constant)
				{
					if (expression.binding == kNoBinding ||
						expression.binding >= program_->bindings.size())
						throw std::runtime_error("invalid array bound");
					const BindingRecord& binding =
						program_->bindings[expression.binding];
					if (binding.kind != BIND_PARAMETER || binding.constant ||
						program_->KindOfScope(binding.owner) !=
							SCOPE_TEMPLATE_PARAMETERS || binding.value < 0 ||
						static_cast<std::uint64_t>(binding.value) >=
							kNoTemplateParameter)
						throw std::runtime_error("invalid array bound");
					type = program_->types.DependentArray(type,
						program_->types.RemoveTopCv(expression.type),
						static_cast<std::uint32_t>(binding.value));
					continue;
				}
				if (expression.value <= 0)
					throw std::runtime_error("invalid array bound");
				bound = static_cast<std::uint64_t>(expression.value);
			}
			type = program_->types.Array(type, bound);
		}
		else
		{
			bool variadic = false;
			std::vector<ParameterInfo> parameters;
			if (suffix == trailing_parameter_clause)
			{
				parameters = trailing_parameters;
				variadic = trailing_variadic;
			}
			else parameters = BuildParameters(suffix, scope, &variadic);
			std::vector<TypeId> function_parameters;
			for (std::size_t p = 0; p < parameters.size(); ++p)
				function_parameters.push_back(parameters[p].function_type);
			type = program_->types.Function(type, function_parameters, variadic,
				function_cv, function_ref);
			result.parameters = parameters;
		}
	}
	if (nested != kNoNode)
	{
		DeclaratorInfo inner = BuildDeclarator(nested, type, scope,
			false, member_implicit_object, defer_trailing_return);
		if (!result.parameters.empty()) inner.parameters = result.parameters;
		return inner;
	}
	result.type = type;
	return result;
}

BindingId SemanticAnalyzer::DeclareFunction(ScopeId owner, NameId name,
	TypeId type, const std::vector<ParameterInfo>& parameters, bool definition,
	bool template_specialization, StorageClass storage_class,
	LanguageLinkage language_linkage, bool nonthrowing,
	bool ordinary_visible)
{
	if (HasInternalLinkageScope(owner)) storage_class = STORAGE_CLASS_STATIC;
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
	{
		const TypeId parameter =
			program_->types.RemoveTopCv(declared_parameters[i]);
		const TypeRecord& shape = program_->types.Get(parameter);
		if (shape.kind == TYPE_NAMED &&
			program_->entities[shape.entity].abstract_class)
			throw std::runtime_error(
				"function parameter has abstract class type");
		signature_parameters.push_back(declared_parameters[i]);
	}
	const TypeId signature = program_->types.Function(
		program_->types.Fundamental(FUND_VOID), signature_parameters,
		declared_type.variadic, declared_type.cv,
		declared_type.ref_qualifier);
	const FunctionSignatureKey signature_key(owner, name, signature);
	BindingId previous = kNoBinding;
	BindingId imported = kNoBinding;
	if (!template_specialization)
	{
		++function_signature_lookups_;
		previous = function_declarations_.Find(signature_key);
		++function_signature_lookups_;
		imported = using_function_declarations_.Find(signature_key);
	}
	if (previous == kNoBinding && imported != kNoBinding &&
		program_->KindOfScope(owner) != SCOPE_CLASS)
		throw std::runtime_error(
			"function conflicts with using-declaration");
	const bool was_ordinary_visible = previous != kNoBinding &&
		GetFunction(previous).ordinary_visible;
	const std::uint64_t key = (static_cast<std::uint64_t>(owner) << 32) | name;
	CompactIndexSequence& overloads = function_sets_.Ensure(key);
	BindingId canonical = kNoBinding;
	if (previous != kNoBinding)
	{
		const FunctionInfo& existing = GetFunction(previous);
		const TypeRecord old_type = program_->types.Get(existing.type);
		if (old_type.child != declared_type.child)
			throw std::runtime_error("conflicting function return type");
		if (program_->bindings[existing.binding].nonthrowing != nonthrowing)
			throw std::runtime_error(
				"conflicting function exception specification");
		canonical = existing.binding;
		if (definition && existing.defined)
			throw std::runtime_error("duplicate function definition");
	}
	const BindingId declaration = program_->AddBinding(owner, BIND_FUNCTION,
		name, type, false, 0, NAMED_NONE, 0, canonical,
		false);
	BindingRecord& declaration_record = program_->bindings[declaration];
	declaration_record.storage_class = storage_class;
	declaration_record.language_linkage = language_linkage;
	declaration_record.nonthrowing = nonthrowing;
	declaration_record.unnamed_namespace_linkage =
		HasInternalLinkageScope(owner);
	declaration_record.qualified_name = EmissionName(owner, name);
	if (canonical == kNoBinding)
	{
		FunctionInfo info;
		info.binding = declaration;
		info.owner = owner;
		info.type = type;
		info.signature = signature;
		info.display_name = DisplayName(owner, name);
		info.lexical_scope = owner;
		info.parameters = parameters;
		info.defined = definition;
		info.template_specialization = template_specialization;
		info.ordinary_visible = ordinary_visible;
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
		merged.ordinary_visible = merged.ordinary_visible || ordinary_visible;
		if (merged.parameters.empty() && !parameters.empty())
			merged.parameters = parameters;
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
	canonical_record.unnamed_namespace_linkage =
		canonical_record.unnamed_namespace_linkage ||
		declaration_record.unnamed_namespace_linkage;
	canonical_record.qualified_name = declaration_record.qualified_name;
	if (previous != kNoBinding &&
		canonical_record.language_linkage == LANGUAGE_LINKAGE_C)
		language_linkage = LANGUAGE_LINKAGE_C;
	if (canonical_record.storage_class == STORAGE_CLASS_NONE ||
		storage_class == STORAGE_CLASS_STATIC)
		canonical_record.storage_class = storage_class;
	canonical_record.language_linkage = language_linkage;
	canonical_record.nonthrowing = canonical_record.nonthrowing || nonthrowing;
	std::string literal_suffix;
	canonical_record.operator_kind = ClassifyOperator(
		program_->names.Get(canonical_record.name), &literal_suffix);
	canonical_record.operator_literal_suffix = literal_suffix.empty() ? 0 :
		program_->names.Intern(literal_suffix);
	if (GetFunction(canonical).ordinary_visible && !was_ordinary_visible)
	{
		ordinary_function_sets_.Ensure(key).Push(canonical);
		IndexEnumOperatorCandidate(canonical);
	}
	return canonical;
}
void SemanticAnalyzer::AnalyzeUsing(NodeId node, ScopeId scope,
	std::uint32_t output_parent, bool local, AccessKind access)
{
	const EntityId class_owner = program_->EntityForScope(scope);
	if (arena_->IsTag(node, "alias-declaration"))
	{
		const TypeId type = BuildTypeId(FindChild(node, "type-id"), scope);
		const NameId name = program_->names.Intern(arena_->Payload(node));
		const BindingId binding =
			program_->AddBinding(scope, BIND_TYPE_ALIAS, name, type);
		if (class_owner != kNoEntity)
		{
			program_->bindings[binding].member_owner = class_owner;
			program_->bindings[binding].access = access;
		}
		const std::uint32_t alias = MakeDump(DUMP_TYPE_ALIAS, type,
			VALUE_NONE, name);
		dump_.Add(output_parent, alias);
		return;
	}
	const NodeId target_node = FindChild(node, "target");
	if (target_node == kNoNode) throw std::runtime_error("missing using target");
	const std::string target = arena_->Payload(target_node);
	if (arena_->IsTag(node, "namespace-alias-definition"))
	{
		const ScopeId target_scope = ResolveScopeSpelling(scope, target);
		if (target_scope == kNoScope)
			throw std::runtime_error("namespace alias target not found");
		program_->AddNamespaceAlias(scope,
			program_->names.Intern(arena_->Payload(node)), target_scope);
		return;
	}
	if (arena_->IsTag(node, "using-directive"))
	{
		const ScopeId target_scope = ResolveScopeSpelling(scope, target);
		if (target_scope == kNoScope)
			throw std::runtime_error("using namespace target not found");
		program_->AddUsingEdge(scope, target_scope);
		return;
	}
	const NamePath path = ParseNamePath(target);
	const NameId name = path.Last();
	const NodeId target_structure = FindChild(
		target_node, "structured-type-name");
	const LookupResult ordinary = target_structure != kNoNode ?
		LookupStructuredName(target_node, scope, LOOKUP_ORDINARY) :
		LookupPath(scope, path, LOOKUP_ORDINARY);
	const LookupResult type = ordinary.ordinary == kNoBinding ?
		(target_structure != kNoNode ?
			LookupStructuredName(target_node, scope, LOOKUP_TYPE) :
			LookupPath(scope, path, LOOKUP_TYPE)) : LookupResult();
	if (ordinary.ordinary == kNoBinding && type.type != kNoType)
	{
		if (type.type_declaration != kNoBinding &&
			!CanAccessMember(type.type_declaration, type.naming_class))
			throw std::runtime_error("inaccessible using type");
		const BindingId alias = program_->AddBinding(scope,
			program_->types.IsNamed(type.type) ? BIND_TYPE : BIND_TYPE_ALIAS,
			name, type.type, false, 0, NAMED_NONE, 0,
			type.type_declaration_canonical);
		if (class_owner != kNoEntity)
			PublishUsingAccess(alias, type.type_declaration, access);
		return;
	}
	ScopeId using_target_owner = kNoScope;
	bool names_owner_alias = false;
	std::vector<BindingId> functions = UsingFunctionCandidates(
		scope, path, target, &using_target_owner, &names_owner_alias,
		target_node);
	const std::vector<std::size_t> template_patterns =
		target_structure != kNoNode ? FindFunctionTemplates(
			scope, StructuredNamePath(target_structure)) :
			FindFunctionTemplates(scope, target);
	if (!functions.empty() || !template_patterns.empty())
	{
		if (!functions.empty() && template_patterns.empty() &&
			class_owner != kNoEntity &&
			program_->entities[class_owner].direct_base != kNoEntity)
		{
			const EntityId base = program_->entities[class_owner].direct_base;
			bool constructor_set = using_target_owner ==
				program_->entities[base].member_scope &&
				(name == program_->entities[base].identity_name ||
				 names_owner_alias);
			for (std::size_t i = 0; i < functions.size(); ++i)
				constructor_set = constructor_set &&
					GetFunction(functions[i]).constructor;
			if (constructor_set)
			{
				InheritConstructors(class_owner, functions);
				return;
			}
		}
		const std::uint64_t key =
			(static_cast<std::uint64_t>(scope) << 32) | name;
		CompactIndexSequence& template_aliases =
			template_function_sets_.Ensure(key);
		for (std::size_t i = 0; i < template_patterns.size(); ++i)
			if (!template_aliases.Contains(template_patterns[i]))
				template_aliases.Push(template_patterns[i]);
		if (!template_patterns.empty())
			program_->PublishFunctionTemplateName(scope, name);
		CompactIndexSequence& aliases = function_sets_.Ensure(key);
		CompactIndexSequence& ordinary_aliases =
			ordinary_function_sets_.Ensure(key);
		for (std::size_t i = 0; i < functions.size(); ++i)
		{
			const FunctionInfo& function = GetFunction(functions[i]);
			if (!CanAccessMember(functions[i], ordinary.naming_class))
				throw std::runtime_error("inaccessible using function");
			const FunctionSignatureKey signature_key(scope, name,
				function.signature);
			++function_signature_lookups_;
			if (function_declarations_.Find(signature_key) != kNoBinding)
			{
				if (class_owner != kNoEntity) continue;
				throw std::runtime_error(
					"using function conflicts with declaration");
			}
			++function_signature_lookups_;
			if (using_function_declarations_.Find(signature_key) != kNoBinding)
				throw std::runtime_error("duplicate using function");
			const BindingId alias = program_->AddBinding(scope, BIND_FUNCTION,
				name, function.type, false, 0, NAMED_NONE, 0, function.binding);
			if (class_owner != kNoEntity)
				PublishUsingAccess(alias, functions[i], access);
			if (!aliases.Contains(alias)) aliases.Push(alias);
			if (!ordinary_aliases.Contains(alias)) ordinary_aliases.Push(alias);
			IndexEnumOperatorCandidate(alias);
			using_function_declarations_.Insert(signature_key, alias);
		}
		return;
	}
	if (ordinary.ordinary == kNoBinding)
		throw std::runtime_error("using-declaration target not found");
	if (!CanAccessMember(ordinary.ordinary, ordinary.naming_class))
		throw std::runtime_error("inaccessible using declaration");
	const BindingRecord source = program_->bindings[ordinary.ordinary];
	const BindingId alias = program_->AddBinding(scope, source.kind, name,
		source.type, source.constant, source.value, source.display_flavor,
		source.display_type_name, source.canonical);
	if (class_owner != kNoEntity)
		PublishUsingAccess(alias, ordinary.ordinary, access);
	(void)local;
}
BindingId SemanticAnalyzer::EnsureDestructorBaseEntry(BindingId destructor)
{
	destructor = program_->bindings[destructor].canonical;
	if (program_->bindings[destructor].destructor_base_entry)
		return destructor;
	if (destructor_base_entry_by_binding_.size() <= destructor)
		destructor_base_entry_by_binding_.resize(
			static_cast<std::size_t>(destructor) + 1, kNoBinding);
	if (destructor_base_entry_by_binding_[destructor] != kNoBinding)
		return destructor_base_entry_by_binding_[destructor];
	if (program_->bindings[destructor].inline_function &&
		!program_->bindings[destructor].virtual_function)
	{
		destructor_base_entry_by_binding_[destructor] = destructor;
		return destructor;
	}

	const BindingRecord source_binding = program_->bindings[destructor];
	const FunctionInfo source_info = GetFunction(destructor);
	if (!source_binding.destructor || !source_info.destructor)
		throw std::logic_error(
			"destructor base entry requested for non-destructor");
	const NameId generated_name = program_->names.Intern(
		program_->names.Get(source_binding.name) + "__base_entry");
	const BindingId base_entry = program_->AddBinding(source_binding.owner,
		BIND_FUNCTION, generated_name, source_binding.type, false, 0,
		NAMED_NONE, 0, kNoBinding, false);
	BindingRecord& binding = program_->bindings[base_entry];
	binding.member_owner = source_binding.member_owner;
	binding.access_owner = source_binding.access_owner;
	binding.overload_ordinal = source_binding.overload_ordinal;
	binding.language_linkage = source_binding.language_linkage;
	binding.storage_class = source_binding.storage_class;
	binding.access = source_binding.access;
	binding.nonthrowing = source_binding.nonthrowing;
	binding.destructor = true;
	binding.destructor_base_entry = true;

	FunctionInfo info = source_info;
	info.binding = base_entry;
	info.ordinary_visible = false;
	info.demand_state = 0;
	if (function_fact_by_binding_.size() <= base_entry)
		function_fact_by_binding_.resize(
			static_cast<std::size_t>(base_entry) + 1, kNoDumpEdge);
	function_fact_by_binding_[base_entry] =
		static_cast<std::uint32_t>(functions_.size());
	functions_.push_back(info);
	destructor_base_entry_by_binding_[destructor] = base_entry;
	return base_entry;
}
void SemanticAnalyzer::EnsureStaticMemberStorage(BindingId member, bool constant_storage)
{
	member = program_->bindings[member].canonical;
	BindingRecord& binding = program_->bindings[member];
	if (binding.kind != BIND_VARIABLE ||
		binding.member_owner == kNoEntity || binding.non_static_data_member)
		return;
	// An in-class integral constant does not require storage until an
	// out-of-class definition supplies it. Pure constant-expression uses keep
	// the canonical member fact and must not manufacture an undefined global.
	if (binding.constant && !constant_storage) return;
	// Preserve the already-instantiated class spelling before replaying a
	// dependent out-of-class definition, whose substituted expression spelling
	// may be equivalent but less canonical (for example, `1` versus `true`).
	if (binding.qualified_name == 0)
		binding.qualified_name = EmissionName(binding.owner, binding.name);
	const EntityId member_owner = binding.member_owner;
	DemandClassTemplateMemberDefinitions(member_owner);
	const BindingRecord& completed_binding = program_->bindings[member];
	if (static_member_storage_by_binding_.size() <= member)
		static_member_storage_by_binding_.resize(
			static_cast<std::size_t>(member) + 1, kNoDumpEdge);
	if (static_member_storage_by_binding_[member] != kNoDumpEdge) return;
	DemandStaticConstantInitializerDependencies(member);
	if (root_ == kNoDumpEdge)
		throw std::logic_error("static member storage has no translation unit");
	const std::uint32_t declaration = MakeDump(DUMP_VARIABLE,
		completed_binding.type, VALUE_NONE, completed_binding.name, member);
	dump_.nodes[declaration].declaration_only = true;
	dump_.Add(root_, declaration);
	static_member_storage_by_binding_[member] = declaration;
}

void SemanticAnalyzer::PublishUsingAccess(BindingId alias,
	BindingId source, AccessKind access)
{
	if (alias == kNoBinding || source == kNoBinding)
		throw std::logic_error("using declaration has no binding identity");
	BindingRecord& target = program_->bindings[alias];
	const BindingRecord original = program_->bindings[source];
	target.member_owner = original.member_owner;
	target.access_owner = program_->EntityForScope(target.owner);
	target.member_offset = original.member_offset;
	target.requested_alignment = original.requested_alignment;
	target.bit_offset = original.bit_offset;
	target.bit_width = original.bit_width;
	target.bit_storage_bits = original.bit_storage_bits;
	target.member_ordinal = original.member_ordinal;
	target.access = access;
	target.storage_class = original.storage_class;
	target.non_static_data_member = original.non_static_data_member;
	target.mutable_member = original.mutable_member;
	target.bit_field = original.bit_field;
	target.static_member_function = original.static_member_function;
	target.constructor = original.constructor;
	target.constructor_base_entry = original.constructor_base_entry;
	target.destructor = original.destructor;
	target.destructor_base_entry = original.destructor_base_entry;
	target.inline_function = original.inline_function;
}

void SemanticAnalyzer::ValidateNonmemberOperator(BindingId binding) const
{
	const BindingRecord& record = program_->bindings[binding];
	const FunctionInfo& function = GetFunction(binding);
	if (function.member_owner != kNoType) return;
	if (record.operator_kind == OPERATOR_NONE ||
		record.operator_kind == OPERATOR_LITERAL ||
		record.operator_kind == OPERATOR_NEW ||
		record.operator_kind == OPERATOR_NEW_ARRAY ||
		record.operator_kind == OPERATOR_DELETE ||
		record.operator_kind == OPERATOR_DELETE_ARRAY) return;
	const TypeRecord function_type = program_->types.Get(function.type);
	const TypeId* parameters = program_->types.Parameters(function.type);
	for (std::size_t i = 0; i < function_type.parameter_count; ++i)
	{
		TypeId type = parameters[i];
		const TypeRecord* shape = &program_->types.Get(type);
		if (shape->kind == TYPE_LVALUE_REFERENCE ||
			shape->kind == TYPE_RVALUE_REFERENCE)
		{
			type = shape->child;
			shape = &program_->types.Get(type);
		}
		if (shape->kind == TYPE_QUALIFIED)
		{
			type = shape->child;
			shape = &program_->types.Get(type);
		}
		if (shape->kind != TYPE_NAMED) continue;
		const NamedFlavor flavor = program_->entities[shape->entity].flavor;
		if (flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
			flavor == NAMED_UNION || flavor == NAMED_ENUM ||
			flavor == NAMED_ENUM_CLASS) return;
	}
	throw std::runtime_error(
		"nonmember operator requires a class or enumeration parameter");
}

void SemanticAnalyzer::ValidateFunctionRefQualifier(BindingId binding)
{
	const FunctionInfo& function = GetFunction(binding);
	const BindingRecord& record = program_->bindings[function.binding];
	const TypeRecord& type = program_->types.Get(function.type);
	const bool class_member = record.member_owner != kNoEntity;
	const bool nonstatic_member = function.member_owner != kNoType;
	if (type.ref_qualifier != FUNCTION_REF_NONE &&
		(!nonstatic_member || record.static_member_function ||
		 record.constructor || record.destructor))
		throw std::runtime_error(
			"ref-qualifier requires an ordinary non-static member function");
	if (!class_member) return;

	const TypeId* parameters = program_->types.Parameters(function.type);
	std::vector<TypeId> parameter_shape;
	if (type.parameter_count != 0)
		parameter_shape.assign(parameters, parameters + type.parameter_count);
	const TypeId shape = program_->types.Function(
		program_->types.Fundamental(FUND_VOID), parameter_shape,
		type.variadic, CV_NONE, FUNCTION_REF_NONE);
	const FunctionSignatureKey key(record.owner, record.name, shape);
	++function_signature_lookups_;
	const BindingId prior = member_ref_qualifier_shapes_.Find(key);
	if (prior == kNoBinding)
	{
		member_ref_qualifier_shapes_.Insert(key, function.binding);
		return;
	}
	const TypeRecord& prior_type =
		program_->types.Get(GetFunction(prior).type);
	if ((type.ref_qualifier == FUNCTION_REF_NONE) !=
		(prior_type.ref_qualifier == FUNCTION_REF_NONE))
		throw std::runtime_error(
			"member overload set mixes ref-qualified and unqualified declarations");
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
void SemanticAnalyzer::DemandFunction(BindingId binding)
{
	if (binding == kNoBinding || unevaluated_depth_ != 0 ||
		constexpr_evaluation_depth_ != 0) return;
	binding = program_->bindings[binding].canonical;
	DemandClassTemplateMemberDefinitions(program_->bindings[binding].member_owner);
	program_->bindings[binding].emission_demanded |= program_->bindings[binding].inline_function;
	if ((program_->bindings[binding].constructor ||
		 program_->bindings[binding].destructor) &&
		program_->bindings[binding].member_owner != kNoEntity &&
		program_->entities[program_->bindings[binding].member_owner].
			polymorphic_class)
		MarkVtableDemand(program_->bindings[binding].member_owner);
	if (binding < constructor_base_entry_by_binding_.size())
	{
		const BindingId base_entry =
			constructor_base_entry_by_binding_[binding];
		if (base_entry != kNoBinding && base_entry != binding)
			DemandFunction(base_entry);
	}
	if (binding < destructor_base_entry_by_binding_.size())
	{
		const BindingId base_entry =
			destructor_base_entry_by_binding_[binding];
		if (base_entry != kNoBinding && base_entry != binding)
			DemandFunction(base_entry);
	}
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
	const bool emit_definition = info.defined &&
		!program_->bindings[binding].explicit_instantiation_suppressed;
	const bool member = info.member_owner != kNoType;
	const TypeId output_type = member ?
		AdaptMemberFunctionType(info.binding) : info.type;
	const std::uint32_t function = MakeDump(emit_definition ?
		DUMP_FUNCTION_DEFINITION : DUMP_FUNCTION_DECLARATION,
		output_type, VALUE_NONE, info.display_name, info.binding);
	dump_.Add(root_, function);
	if (!emit_definition && (member ||
		program_->bindings[binding].explicit_instantiation_suppressed))
	{
		GetMutableFunction(binding).demand_state = 3;
		++demanded_function_emissions_;
		return;
	}
	const ScopeId function_scope = NewScope(info.lexical_scope, SCOPE_FUNCTION,
		program_->bindings[info.binding].name, ScopePrefixId(info.owner));
	std::vector<BindingId> parameter_bindings;
	BindFunctionParameterPackElement(
		function_scope, info.parameter_pack_name, kNoBinding);
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
			BIND_PARAMETER, parameter.name, ParameterBindingType(parameter));
		parameter_bindings.push_back(parameter_binding);
		BindFunctionParameterPackElement(
			function_scope, parameter.pack_name, parameter_binding);
		dump_.Add(function, MakeDump(DUMP_PARAMETER, parameter.function_type,
			VALUE_NONE, parameter.name, parameter_binding));
		AddLifetimeObligation(function_scope, parameter_binding,
			parameter.function_type, false);
	}
	if (!emit_definition)
	{
		GetMutableFunction(binding).demand_state = 3;
		++demanded_function_emissions_;
		return;
	}
	if (emit_definition)
	{
		const TypeId previous_return = current_return_type_;
		const EntityId previous_class = current_class_context_;
		const BindingId previous_function = current_function_context_;
		current_return_type_ = program_->types.Get(info.type).child;
		current_class_context_ = info.friend_of != kNoEntity ? info.friend_of :
			program_->bindings[info.binding].member_owner;
		current_function_context_ =
			program_->bindings[info.binding].canonical;
		if (info.constructor)
		{
			const std::uint32_t constructor_body =
				MakeDump(DUMP_COMPOUND_STATEMENT);
			dump_.Add(function, constructor_body);
			if ((info.special_member == SPECIAL_MEMBER_COPY_CONSTRUCTOR ||
				 info.special_member == SPECIAL_MEMBER_MOVE_CONSTRUCTOR) &&
				(info.implicit_special_member || info.defaulted_special_member))
				AddSynthesizedConstructorBody(info, parameter_bindings,
					constructor_body);
			else AddConstructorMemberActions(info, function_scope,
				parameter_bindings, constructor_body);
			if (info.special_member == SPECIAL_MEMBER_NONE &&
				(info.implicit_constructor || info.defaulted_constructor) &&
				InitializationActionsAreNonthrowing(constructor_body))
				program_->bindings[info.binding].nonthrowing = true;
			if (info.definition_body != kNoNode)
				AnalyzeCompound(info.definition_body, function_scope,
					constructor_body);
		}
		else if ((info.special_member == SPECIAL_MEMBER_COPY_ASSIGNMENT ||
			info.special_member == SPECIAL_MEMBER_MOVE_ASSIGNMENT) &&
			(info.implicit_special_member || info.defaulted_special_member))
		{
			const std::uint32_t assignment_body =
				MakeDump(DUMP_COMPOUND_STATEMENT);
			dump_.Add(function, assignment_body);
			AddSynthesizedAssignmentBody(info, parameter_bindings,
				assignment_body);
		}
		else if (info.destructor)
		{
			const std::uint32_t destructor_body =
				MakeDump(DUMP_COMPOUND_STATEMENT);
			dump_.Add(function, destructor_body);
			const EntityId entity =
				program_->bindings[info.binding].member_owner;
			if (entity != kNoEntity &&
				program_->entities[entity].polymorphic_class)
				dump_.Add(destructor_body,
					MakeDump(DUMP_VPTR_INITIALIZATION_ACTION,
						program_->entities[entity].type));
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
		FinalizeNamedReturnSlot(function);
		current_return_type_ = previous_return;
		current_class_context_ = previous_class;
		current_function_context_ = previous_function;
		DemandMaterializedConstructorActions(function);
	}
	GetMutableFunction(binding).demand_state = 3;
	++demanded_function_emissions_;
}

}
}
