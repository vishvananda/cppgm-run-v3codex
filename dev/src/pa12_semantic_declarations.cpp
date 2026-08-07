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
		const NameId entity_name = EmissionName(owner, name);
		entity = program_->NewEntity(entity_name, flavor, false,
			kNoType, owner, name);
		program_->SetTypeName(owner, name, program_->entities[entity].type);
	}
	const TypeId type = program_->entities[entity].type;
	if (entity_data_members_.size() <= entity)
		entity_data_members_.resize(static_cast<std::size_t>(entity) + 1);
	if (entity_layout_members_.size() <= entity)
		entity_layout_members_.resize(static_cast<std::size_t>(entity) + 1);
	if (entity_constructors_.size() <= entity)
		entity_constructors_.resize(static_cast<std::size_t>(entity) + 1);
	if (class_special_members_.size() <= entity)
		class_special_members_.resize(static_cast<std::size_t>(entity) + 1);
	if (implicit_constructor_by_entity_.size() <= entity)
		implicit_constructor_by_entity_.resize(
			static_cast<std::size_t>(entity) + 1, kNoBinding);
	if (entity_destructor_by_entity_.size() <= entity)
		entity_destructor_by_entity_.resize(
			static_cast<std::size_t>(entity) + 1, kNoBinding);
	if (old.type == kNoType && arena_->Payload(node).size() != 0)
		program_->AddBinding(owner, BIND_TYPE, name, type, false, 0, flavor);
	const std::size_t requested_alignment = RequestedAlignment(node, scope);
	if (requested_alignment != 0)
		program_->entities[entity].requested_alignment = std::max(
			program_->entities[entity].requested_alignment,
			static_cast<std::uint64_t>(requested_alignment));
	if ((arena_->Flags(node) & SYNTAX_FLAG_DEFINITION) != 0)
	{
		if (program_->entities[entity].complete)
			throw std::runtime_error("duplicate class definition");
		program_->entities[entity].packing_alignment = current_pack_alignment_;
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
			else if (arena_->IsTag(member, "bit-field-declaration"))
				AnalyzeBitField(member, member_scope, type, member_access);
			else if (arena_->IsTag(member, "special-member-declaration") ||
				arena_->IsTag(member, "special-member-definition"))
				AnalyzeSpecialMember(member, member_scope, type, member_access);
			else if (arena_->IsTag(member, "class-specifier") ||
				arena_->IsTag(member, "class-forward-declaration"))
			{
				const TypeId nested_type = AnalyzeClass(member, member_scope,
					std::string(),
					arena_->IsTag(member, "class-forward-declaration"));
				const EntityId nested = EntityOf(nested_type);
				if (nested == kNoEntity)
					throw std::logic_error("nested class has no entity");
				const BindingId declaration =
					program_->entities[nested].declaration;
				if (declaration != kNoBinding)
				{
					program_->bindings[declaration].member_owner = entity;
					program_->bindings[declaration].access = member_access;
				}
			}
			else if (arena_->IsTag(member, "using-declaration") ||
				arena_->IsTag(member, "alias-declaration"))
				AnalyzeUsing(member, member_scope, root_, false, member_access);
		}
		CompleteClassLayout(entity);
		CompleteClassSpecialMembers(entity);
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

std::size_t SemanticAnalyzer::RequestedAlignment(NodeId node, ScopeId scope)
{
	std::size_t result = 0;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId alignment = arena_->EdgeChild(edge);
		if (!arena_->IsTag(alignment, "alignment-specifier")) continue;
		const NodeId operand = FirstSemanticChild(alignment);
		if (operand == kNoNode)
			throw std::runtime_error("empty alignment specifier");
		std::uint64_t value = 0;
		if (arena_->IsTag(operand, "type-id"))
			value = program_->AlignOf(BuildTypeId(operand, scope));
		else
		{
			const ExpressionInfo expression = AnalyzeExpression(operand, scope);
			if (!expression.constant || expression.value < 0)
				throw std::runtime_error("nonconstant alignment specifier");
			value = static_cast<std::uint64_t>(expression.value);
		}
		if (value == 0) continue;
		if ((value & (value - 1)) != 0 ||
			value > std::numeric_limits<std::size_t>::max())
			throw std::runtime_error("invalid requested alignment");
		result = std::max(result, static_cast<std::size_t>(value));
	}
	return result;
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
		if (record.direct_base != kNoEntity)
			zero_offset_subobject_scratch_.push_back(record.direct_base);
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
		size = base->empty_class ? 0 :
			static_cast<std::size_t>(base->object_size);
		const std::size_t base_alignment =
			static_cast<std::size_t>(base->object_alignment);
		natural_alignment = std::max(natural_alignment, base_alignment);
		alignment = packing_alignment == 0 ? base_alignment :
			std::min(base_alignment, packing_alignment);
		if (!base->destructible) owner.destructible = false;
		if (!base->trivial_destructor) owner.trivial_destructor = false;
		const BindingId destructor = DestructorForType(base->type);
		if (destructor == kNoBinding ||
			!CanAccessMember(destructor, owner.direct_base))
			owner.destructible = false;
	}
	const bool is_union = owner.flavor == NAMED_UNION;
	bool empty_class = base == 0 || base->empty_class;
	bool defaulted_destructor = !owner.has_user_declared_destructor;
	if (owner.has_user_declared_destructor)
	{
		const BindingId destructor = DestructorForType(owner.type);
		defaulted_destructor = destructor != kNoBinding &&
			GetFunction(destructor).defaulted_destructor;
	}
	const std::vector<BindingId>& members = entity_data_members_[entity];
	const std::vector<ClassLayoutMember>& layout_members =
		entity_layout_members_[entity];
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
	bool active_bit_unit = false;
	std::size_t active_bit_offset = 0;
	std::size_t active_bit_size = 0;
	std::size_t active_bit_alignment = 0;
	std::size_t active_bit_used = 0;
	for (std::size_t i = 0; i < layout_members.size(); ++i)
	{
		++class_layout_member_visits_;
		const ClassLayoutMember& layout = layout_members[i];
		BindingRecord* member = layout.binding == kNoBinding ? 0 :
			&program_->bindings[layout.binding];
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
			if (size == 0 && ZeroOffsetSubobjectConflict(
				owner.direct_base, layout.type))
				size = 1;
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
		AnalyzeFriendFunction(node, scope, owner_type, spec);
		return;
	}
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
		ValidateFunctionRefQualifier(function);
		info.definition_body =
			FindChild(node, "compound-statement");
		info.deferred = true;
		ConfigureAssignmentSpecialMember(function, kNoNode);
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
			ValidateFunctionRefQualifier(function);
			ConfigureAssignmentSpecialMember(
				function, FindChild(item, "initializer"));
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
			binding.mutable_member = spec.mutable_member;
			binding.member_owner = EntityOf(owner_type);
			binding.access = access;
			binding.requested_alignment = RequestedAlignment(node, scope);
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
				if (entity_layout_members_.size() <= entity)
					entity_layout_members_.resize(
						static_cast<std::size_t>(entity) + 1);
				entity_layout_members_[entity].push_back(
					ClassLayoutMember(member, binding.type));
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
	if (record.canonical != binding && canonical.constant)
	{
		record.constant = true;
		record.value = canonical.value;
	}
	if (!local) record.qualified_name = EmissionName(declaration_scope, name);
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
		binding.inline_function = source_definition || defaulted;
		const NodeId specifiers = FindChild(node, "member-specifiers");
		if (specifiers != kNoNode)
			for (std::uint32_t edge = arena_->FirstEdge(specifiers);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
				if (PayloadSource(arena_->EdgeChild(edge)) == "inline")
					binding.inline_function = true;
		FunctionInfo& info = GetMutableFunction(destructor);
		info.member_owner = owner_type;
		info.destructor = true;
		ValidateFunctionRefQualifier(destructor);
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
	ValidateFunctionRefQualifier(constructor);
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
	RegisterClassSpecialMember(constructor);
}

void SemanticAnalyzer::AnalyzeOutOfClassSpecialMember(NodeId node,
	ScopeId scope)
{
	const NodeId declarator = FindChild(node, "declarator");
	if (declarator == kNoNode)
		throw std::runtime_error(
			"out-of-class special member is missing its declarator");
	const NamePath path = DeclaratorNamePath(declarator);
	if (!path.global && path.Size() <= 1)
		throw std::runtime_error(
			"unqualified special member definition outside a class");
	const ScopeId owner = ResolveOwner(scope, path);
	const EntityId entity = owner == kNoScope ? kNoEntity :
		program_->EntityForScope(owner);
	if (entity == kNoEntity)
		throw std::runtime_error("special member owner is not a class");
	const std::string terminal = program_->names.Get(path.Last());
	const std::string class_name = program_->names.Get(
		program_->entities[entity].identity_name);
	const bool constructor_definition = terminal == class_name;
	const bool destructor_definition = terminal == "~" + class_name;
	if (!constructor_definition && !destructor_definition)
		throw std::runtime_error(
			"qualified special member definition has an invalid name");

	const EntityId previous_class = current_class_context_;
	current_class_context_ = entity;
	const DeclaratorInfo parsed = BuildDeclarator(declarator,
		program_->types.Fundamental(FUND_VOID), owner);
	const BindingId special = DeclareFunction(owner, path.Last(),
		parsed.type, parsed.parameters, true, false, STORAGE_CLASS_NONE,
		current_language_linkage_, IsNonthrowing(declarator, owner));
	FunctionInfo& info = GetMutableFunction(special);
	if (info.member_owner != program_->entities[entity].type ||
		(constructor_definition && !info.constructor) ||
		(destructor_definition && !info.destructor))
		throw std::runtime_error(
			"qualified special member definition has no matching declaration");
	ValidateFunctionRefQualifier(special);
	const NodeId initializer = FindChild(node, "initializer");
	const NodeId special_initializer = initializer == kNoNode ? kNoNode :
		FindChild(initializer, "special-initializer");
	const bool defaulted = special_initializer != kNoNode &&
		arena_->Payload(special_initializer) == "default";
	const bool deleted = special_initializer != kNoNode &&
		arena_->Payload(special_initializer) == "delete";
	info.definition_body = FindChild(node, "compound-statement");
	if (constructor_definition)
	{
		info.constructor_initializer = FindChild(node, "ctor-initializer");
		info.defaulted_constructor = info.defaulted_constructor || defaulted;
		info.deleted_constructor = info.deleted_constructor || deleted;
		info.defaulted_special_member =
			info.defaulted_special_member || defaulted;
		info.deleted_special_member =
			info.deleted_special_member || deleted;
		if (defaulted && info.special_member != SPECIAL_MEMBER_NONE)
		{
			bool implicitly_deleted = false;
			bool trivial = false;
			bool nonthrowing = false;
			EvaluateSynthesizedConstructor(entity, info.special_member,
				&implicitly_deleted, &trivial, &nonthrowing);
			info.deleted_constructor = implicitly_deleted;
			info.deleted_special_member = implicitly_deleted;
			info.trivial_special_member = false;
			program_->bindings[special].nonthrowing = nonthrowing;
		}
	}
	else
	{
		info.defaulted_destructor = info.defaulted_destructor || defaulted;
		info.deleted_destructor = info.deleted_destructor || deleted;
		if (defaulted) program_->bindings[special].nonthrowing = true;
	}
	info.deferred = !(constructor_definition ? info.deleted_constructor :
		info.deleted_destructor);
	if (constructor_definition)
	{
		program_->entities[entity].has_user_provided_constructor = true;
		(void)EnsureConstructorBaseEntry(special);
	}
	else
	{
		program_->entities[entity].has_user_declared_destructor = true;
		program_->entities[entity].trivial_destructor = false;
		(void)EnsureDestructorBaseEntry(special);
	}
	current_class_context_ = previous_class;
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
			const NodeId qualified = FindChild(child, "qualified-type-name");
			if (qualified != kNoNode)
			{
				const ScopeId carrier = program_->ScopeForType(
					EffectiveType(result.type));
				if (carrier == kNoScope)
					throw std::runtime_error(
						"decltype qualifier does not name a class type");
				const LookupResult found = program_->LookupQualified(carrier,
					ParseNamePath(arena_->Payload(qualified)), LOOKUP_TYPE);
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
		else if (spelling == "extern") result.storage_class = STORAGE_CLASS_EXTERN;
		else if (spelling == "static") result.storage_class = STORAGE_CLASS_STATIC;
		else if (spelling == "thread_local")
			result.thread_local_storage = true;
		else if (spelling == "auto") result.placeholder_auto = true;
		else if (spelling == "mutable") result.mutable_member = true;
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
			spelling != "virtual" &&
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
		BuildDeclarator(declarator, spec.type, scope,
			spec.placeholder_auto).type;
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
				BuildDeclarator(declarator, spec.type, scope,
					spec.placeholder_auto);
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
	ScopeId scope, bool placeholder_auto)
{
	DeclaratorInfo result;
	result.name = DeclaratorName(node);
	TypeId type = base;
	const NodeId trailing = FindChild(node, "trailing-return-type");
	if (trailing != kNoNode)
	{
		const NodeId return_type = FindChild(trailing, "type-id");
		if (return_type == kNoNode)
			throw std::runtime_error("trailing return type is missing its type-id");
		type = BuildTypeId(return_type, scope);
	}
	else if (placeholder_auto)
		throw std::runtime_error("auto requires a trailing return type in PA16");
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
				function_cv, function_ref);
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
		signature_parameters.push_back(declared_parameters[i]);
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
		ordinary_function_sets_.Ensure(key).Push(canonical);
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
	const LookupResult ordinary =
		program_->Lookup(scope, path, LOOKUP_ORDINARY);
	const LookupResult type = ordinary.ordinary == kNoBinding ?
		program_->Lookup(scope, path, LOOKUP_TYPE) : LookupResult();
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
	const std::vector<BindingId> functions = FunctionCandidates(scope, target);
	if (!functions.empty())
	{
		if (class_owner != kNoEntity &&
			program_->entities[class_owner].direct_base != kNoEntity)
		{
			const EntityId base = program_->entities[class_owner].direct_base;
			const ScopeId target_owner = ResolveOwner(scope, path);
			bool constructor_set = target_owner ==
				program_->entities[base].member_scope &&
				name == program_->entities[base].identity_name;
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

void SemanticAnalyzer::InheritConstructors(EntityId entity,
	const std::vector<BindingId>& constructors)
{
	EntityRecord& derived = program_->entities[entity];
	derived.has_user_declared_constructor = true;
	derived.has_user_provided_constructor = true;
	derived.is_aggregate = false;
	if (entity_constructors_.size() <= entity)
		entity_constructors_.resize(static_cast<std::size_t>(entity) + 1);
	for (std::size_t i = 0; i < constructors.size(); ++i)
	{
		const FunctionInfo source = GetFunction(constructors[i]);
		if (!source.constructor) continue;
		const FunctionSignatureKey signature_key(derived.member_scope,
			derived.identity_name, source.signature);
		++function_signature_lookups_;
		if (function_declarations_.Find(signature_key) != kNoBinding)
			continue;
		const BindingRecord source_binding =
			program_->bindings[source.binding];
		const BindingId source_base_entry =
			EnsureConstructorBaseEntry(source.binding);
		const BindingId inherited = DeclareFunction(derived.member_scope,
			derived.identity_name, source.type, source.parameters, true, false,
			STORAGE_CLASS_NONE, source_binding.language_linkage,
			source_binding.nonthrowing);
		BindingRecord& binding = program_->bindings[inherited];
		binding.member_owner = entity;
		binding.access_owner = source_binding.access_owner != kNoEntity ?
			source_binding.access_owner : source_binding.member_owner;
		binding.access = source_binding.access;
		binding.constructor = true;
		FunctionInfo& info = GetMutableFunction(inherited);
		info.member_owner = derived.type;
		info.constructor = true;
		info.explicit_constructor = source.explicit_constructor;
		info.deleted_constructor = source.deleted_constructor;
		info.inherited_constructor_source = source_base_entry;
		info.deferred = !info.deleted_constructor;
		entity_constructors_[entity].push_back(inherited);
		std::size_t required = info.parameters.size();
		while (required != 0 &&
			info.parameters[required - 1].default_argument != kNoNode) --required;
		if (!info.deleted_constructor && required == 0)
			derived.default_constructible = true;
	}
}

BindingId SemanticAnalyzer::EnsureConstructorBaseEntry(BindingId constructor)
{
	constructor = program_->bindings[constructor].canonical;
	if (program_->bindings[constructor].constructor_base_entry)
		return constructor;
	if (constructor_base_entry_by_binding_.size() <= constructor)
		constructor_base_entry_by_binding_.resize(
			static_cast<std::size_t>(constructor) + 1, kNoBinding);
	if (constructor_base_entry_by_binding_[constructor] != kNoBinding)
		return constructor_base_entry_by_binding_[constructor];

	const BindingRecord source_binding = program_->bindings[constructor];
	const FunctionInfo source_info = GetFunction(constructor);
	if (!source_binding.constructor || !source_info.constructor)
		throw std::logic_error("constructor base entry requested for non-constructor");
	const NameId generated_name = program_->names.Intern(
		"__cppgm_constructor_base_" + std::to_string(constructor));
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
	binding.static_member_function = source_binding.static_member_function;
	binding.constructor = true;
	binding.constructor_base_entry = true;

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
	constructor_base_entry_by_binding_[constructor] = base_entry;
	return base_entry;
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
	if (program_->bindings[destructor].inline_function)
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

void SemanticAnalyzer::EnsureStaticMemberStorage(BindingId member)
{
	member = program_->bindings[member].canonical;
	BindingRecord& binding = program_->bindings[member];
	if (binding.kind != BIND_VARIABLE ||
		binding.member_owner == kNoEntity || binding.non_static_data_member)
		return;
	if (static_member_storage_by_binding_.size() <= member)
		static_member_storage_by_binding_.resize(
			static_cast<std::size_t>(member) + 1, kNoDumpEdge);
	if (static_member_storage_by_binding_[member] != kNoDumpEdge) return;
	if (root_ == kNoDumpEdge)
		throw std::logic_error("static member storage has no translation unit");
	if (binding.qualified_name == 0)
		binding.qualified_name = EmissionName(binding.owner, binding.name);
	const std::uint32_t declaration = MakeDump(DUMP_VARIABLE, binding.type,
		VALUE_NONE, binding.name, member);
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

void SemanticAnalyzer::AnalyzeFriendFunction(NodeId node,
	ScopeId class_scope, TypeId owner_type, const SpecInfo& spec)
{
	const EntityId owner_entity = EntityOf(owner_type);
	if (owner_entity == kNoEntity)
		throw std::logic_error("friend declaration has no class owner");
	ScopeId friend_owner = program_->entities[owner_entity].owner;
	while (friend_owner != kNoScope &&
		program_->KindOfScope(friend_owner) != SCOPE_NAMESPACE)
		friend_owner = program_->ParentScope(friend_owner);
	if (friend_owner == kNoScope)
		throw std::runtime_error("friend function has no namespace owner");

	std::vector<NodeId> declarators;
	if (arena_->IsTag(node, "function-definition"))
	{
		const NodeId declarator = FindChild(node, "declarator");
		if (declarator != kNoNode) declarators.push_back(declarator);
	}
	else
	{
		const NodeId list = FindChild(node, "init-declarator-list");
		if (list != kNoNode)
			for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
				edge = arena_->NextEdge(edge))
			{
				const NodeId declarator =
					FindChild(arena_->EdgeChild(edge), "declarator");
				if (declarator != kNoNode) declarators.push_back(declarator);
			}
	}
	if (declarators.empty())
		throw std::runtime_error("friend declaration has no function declarator");
	const bool definition = arena_->IsTag(node, "function-definition");
	for (std::size_t i = 0; i < declarators.size(); ++i)
	{
		const DeclaratorInfo parsed = BuildDeclarator(
			declarators[i], spec.type, class_scope);
		if (!program_->types.IsFunction(parsed.type))
			throw std::runtime_error("friend declaration is not a function");
		const NamePath declared_name = DeclaratorNamePath(declarators[i]);
		const bool qualified_friend = declared_name.global ||
			declared_name.Size() > 1;
		const ScopeId declared_owner = qualified_friend ?
			ResolveOwner(class_scope, declared_name) : friend_owner;
		if (declared_owner == kNoScope)
			throw std::runtime_error("qualified friend owner not found");
		if (qualified_friend)
		{
			const TypeRecord declared_type = program_->types.Get(parsed.type);
			std::vector<TypeId> parameters;
			const TypeId* parameter_data =
				program_->types.Parameters(parsed.type);
			if (declared_type.parameter_count != 0)
				parameters.assign(parameter_data,
					parameter_data + declared_type.parameter_count);
			const TypeId signature = program_->types.Function(
				program_->types.Fundamental(FUND_VOID), parameters,
				declared_type.variadic, declared_type.cv,
				declared_type.ref_qualifier);
			++function_signature_lookups_;
			const BindingId prior = function_declarations_.Find(
				FunctionSignatureKey(declared_owner, parsed.name, signature));
			if (prior == kNoBinding || !GetFunction(prior).ordinary_visible)
				throw std::runtime_error(
					"qualified friend function was not declared");
		}
		const BindingId binding = DeclareFunction(declared_owner, parsed.name,
			parsed.type, parsed.parameters, definition, false,
			STORAGE_CLASS_NONE, current_language_linkage_,
			IsNonthrowing(declarators[i], class_scope), false);
		FunctionInfo& info = GetMutableFunction(binding);
		if (info.friend_of == kNoEntity) info.friend_of = owner_entity;
		ValidateFunctionRefQualifier(binding);
		const std::uint64_t access_key =
			(static_cast<std::uint64_t>(owner_entity) << 32) | binding;
		CompactIndexSequence& grants =
			friend_function_grants_.Ensure(access_key);
		if (grants.Size() == 0) grants.Push(0);
		if (!qualified_friend)
		{
			const std::uint64_t friend_key =
				(static_cast<std::uint64_t>(owner_entity) << 32) | parsed.name;
			CompactIndexSequence& hidden = hidden_friend_sets_.Ensure(friend_key);
			if (!hidden.Contains(binding)) hidden.Push(binding);
		}
		info.lexical_scope = class_scope;
		if (definition)
		{
			info.definition_body = FindChild(node, "compound-statement");
			info.deferred = true;
			if (!qualified_friend)
			{
				if (hidden_friend_anchor_by_entity_.size() <= owner_entity)
					hidden_friend_anchor_by_entity_.resize(
						static_cast<std::size_t>(owner_entity) + 1, kNoBinding);
				if (hidden_friend_anchor_by_entity_[owner_entity] == kNoBinding)
				{
					hidden_friend_anchor_by_entity_[owner_entity] =
						program_->bindings[binding].canonical;
					DemandFunction(binding);
				}
			}
		}
		ValidateNonmemberOperator(binding);
	}
}

void SemanticAnalyzer::AnalyzeFriendClass(NodeId node,
	ScopeId class_scope, TypeId owner_type)
{
	const EntityId owner = EntityOf(owner_type);
	if (owner == kNoEntity)
		throw std::logic_error("friend class declaration has no owner");
	const NodeId specifiers = FindChild(node, "decl-specifier-seq");
	const NodeId declaration = specifiers == kNoNode ? kNoNode :
		FindChild(specifiers, "class-forward-declaration");
	if (declaration == kNoNode)
		throw std::runtime_error("friend class declaration has no class");
	const std::string spelling = arena_->Payload(declaration);
	const NamePath path = ParseNamePath(spelling);
	const LookupResult found = LookupSpelling(class_scope, spelling, LOOKUP_TYPE);
	TypeId friend_type = found.type;
	if (friend_type != kNoType && found.type_declaration != kNoBinding &&
		!CanAccessMember(found.type_declaration, found.naming_class))
		throw std::runtime_error("inaccessible friend class");
	if (friend_type == kNoType)
	{
		ScopeId namespace_owner = program_->entities[owner].owner;
		while (namespace_owner != kNoScope &&
			program_->KindOfScope(namespace_owner) != SCOPE_NAMESPACE)
			namespace_owner = program_->ParentScope(namespace_owner);
		if (namespace_owner == kNoScope)
			throw std::runtime_error("friend class has no namespace owner");
		friend_type = AnalyzeClass(declaration,
			!path.global && path.Size() == 1 ?
				namespace_owner : class_scope,
			std::string(), true);
	}
	const EntityId friend_entity = EntityOf(friend_type);
	if (friend_entity == kNoEntity)
		throw std::runtime_error("friend declaration does not name a class");
	const std::uint64_t key =
		(static_cast<std::uint64_t>(owner) << 32) | friend_entity;
	CompactIndexSequence& grants = friend_class_grants_.Ensure(key);
	if (grants.Size() == 0) grants.Push(0);
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

std::vector<BindingId> SemanticAnalyzer::FunctionSet(BindingId binding)
{
	if (binding == kNoBinding || binding >= program_->bindings.size() ||
		program_->bindings[binding].kind != BIND_FUNCTION)
		return std::vector<BindingId>();
	const BindingRecord& record = program_->bindings[binding];
	const std::uint64_t key = (static_cast<std::uint64_t>(record.owner) << 32) |
		record.name;
	const CompactIndexSequence* set = ordinary_function_sets_.Find(key);
	if (!set) return std::vector<BindingId>();
	std::vector<BindingId> result;
	result.reserve(set->Size());
	for (std::size_t i = 0; i < set->Size(); ++i)
	{
		const BindingId candidate = static_cast<BindingId>((*set)[i]);
		const BindingRecord& candidate_record = program_->bindings[candidate];
		if (candidate_record.access_owner != kNoEntity &&
			candidate_record.canonical != candidate)
		{
			const FunctionInfo& function = GetFunction(candidate);
			const FunctionSignatureKey signature_key(record.owner, record.name,
				function.signature);
			++function_signature_lookups_;
			if (function_declarations_.Find(signature_key) != kNoBinding)
				continue;
		}
		result.push_back(candidate);
	}
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
	if (binding == kNoBinding || unevaluated_depth_ != 0) return;
	binding = program_->bindings[binding].canonical;
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
	const ScopeId function_scope = NewScope(info.lexical_scope, SCOPE_FUNCTION,
		program_->bindings[info.binding].name, ScopePrefixId(info.owner));
	std::vector<BindingId> parameter_bindings;
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
		parameter_bindings.push_back(parameter_binding);
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
		current_function_context_ = previous_function;
	}
	GetMutableFunction(binding).demand_state = 3;
	++demanded_function_emissions_;
}

}
}
