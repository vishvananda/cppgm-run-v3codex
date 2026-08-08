#include "pa12_semantic_detail.h"

namespace cppgm
{
namespace pa12_semantic_detail
{

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

}
}
