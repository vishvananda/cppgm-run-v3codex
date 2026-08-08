#include "pa12_semantic_detail.h"

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::CompleteOutOfClassDefaultedConstructor(EntityId entity,
	BindingId constructor)
{
	FunctionInfo& info = GetMutableFunction(constructor);
	const std::size_t required_parameters =
		info.special_member == SPECIAL_MEMBER_NONE ? 0 : 1;
	if (info.parameters.size() != required_parameters)
		throw std::runtime_error(
			"explicitly defaulted constructor has the wrong type");
	for (std::size_t i = 0; i < info.parameters.size(); ++i)
		if (info.parameters[i].default_argument != kNoNode)
			throw std::runtime_error(
				"explicitly defaulted function has a default argument");
	if (info.special_member != SPECIAL_MEMBER_NONE)
	{
		bool deleted = false;
		bool trivial = false;
		bool nonthrowing = false;
		EvaluateSynthesizedConstructor(entity, info.special_member,
			&deleted, &trivial, &nonthrowing);
		info.deleted_constructor = deleted;
		info.deleted_special_member = deleted;
		info.trivial_special_member = false;
		info.synthesized_storage_copy = trivial;
		program_->bindings[constructor].nonthrowing = nonthrowing;
	}
	else CompleteDefaultedDefaultConstructor(entity, constructor);
	if (info.deleted_constructor)
		throw std::runtime_error(
			"out-of-class defaulted constructor is deleted");
}

void SemanticAnalyzer::CompleteDefaultedDefaultConstructor(EntityId entity,
	BindingId constructor)
{
	bool deleted = false;
	bool trivial = true;
	const EntityRecord& owner = program_->entities[entity];
	if (owner.direct_base != kNoEntity)
	{
		const EntityRecord& base = program_->entities[owner.direct_base];
		deleted = !base.default_constructible;
		trivial = base.trivial_default_constructor;
	}
	if (entity < entity_data_members_.size())
		for (std::size_t i = 0; i < entity_data_members_[entity].size(); ++i)
		{
			const BindingRecord& member =
				program_->bindings[entity_data_members_[entity][i]];
			if (member.has_default_member_initializer)
			{
				trivial = false;
				continue;
			}
			TypeId member_type = member.type;
			const TypeRecord* record = &program_->types.Get(member_type);
			bool const_member = false;
			while (record->kind == TYPE_ARRAY ||
				record->kind == TYPE_QUALIFIED)
			{
				if (record->kind == TYPE_QUALIFIED &&
					(record->cv & CV_CONST) != 0)
					const_member = true;
				member_type = record->child;
				record = &program_->types.Get(member_type);
			}
			if (record->kind == TYPE_LVALUE_REFERENCE ||
				record->kind == TYPE_RVALUE_REFERENCE)
			{
				deleted = true;
				trivial = false;
			}
			else if (record->kind == TYPE_NAMED)
			{
				const EntityRecord& subobject =
					program_->entities[record->entity];
				if (subobject.flavor == NAMED_STRUCT ||
					subobject.flavor == NAMED_CLASS ||
					subobject.flavor == NAMED_UNION)
				{
					deleted = deleted || !subobject.default_constructible;
					trivial = trivial &&
						subobject.trivial_default_constructor;
				}
				else if (const_member) deleted = true;
			}
			else if (const_member) deleted = true;
		}
	FunctionInfo& info = GetMutableFunction(constructor);
	info.deleted_constructor = deleted;
	info.deleted_special_member = deleted;
	program_->entities[entity].default_constructible = !deleted;
	program_->entities[entity].trivial_default_constructor =
		!deleted && trivial;
	program_->bindings[constructor].nonthrowing = !deleted && trivial;
}

void SemanticAnalyzer::CompleteDefaultedDestructor(EntityId entity,
	BindingId destructor)
{
	bool deleted = false;
	bool nonthrowing = true;
	const EntityRecord& owner = program_->entities[entity];
	const auto visit = [this, entity, &deleted, &nonthrowing](
		TypeId type, bool variant)
	{
		const EntityId subobject = DestructedEntity(type);
		if (subobject == kNoEntity) return;
		const BindingId selected = DestructorForType(type);
		if (selected == kNoBinding ||
			!program_->entities[subobject].destructible ||
			GetFunction(selected).deleted_destructor ||
			!CanAccessMember(selected, subobject, entity))
			deleted = true;
		if (selected == kNoBinding ||
			!program_->bindings[selected].nonthrowing)
			nonthrowing = false;
		if (variant && !program_->entities[subobject].trivial_destructor)
			deleted = true;
	};
	if (owner.direct_base != kNoEntity)
		visit(program_->entities[owner.direct_base].type, false);
	if (entity < entity_data_members_.size())
		for (std::size_t i = 0; i < entity_data_members_[entity].size(); ++i)
			visit(program_->bindings[entity_data_members_[entity][i]].type,
				owner.flavor == NAMED_UNION);
	FunctionInfo& info = GetMutableFunction(destructor);
	info.deleted_destructor = deleted;
	program_->entities[entity].destructible = !deleted;
	program_->entities[entity].trivial_destructor = false;
	program_->bindings[destructor].nonthrowing = !deleted && nonthrowing;
}

}
}
