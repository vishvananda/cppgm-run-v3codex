#include "lowering/objects/zero_initialization.h"

namespace cppgm
{
namespace lowering
{
namespace zero_initialization
{

using namespace semantic;

bool ContiguousSpanEligible(const semantic::Program& program, TypeId type)
{
	const TypeRecord* record = &program.types.Get(type);
	while (record->kind == TYPE_QUALIFIED || record->kind == TYPE_ARRAY)
	{
		if (record->kind == TYPE_QUALIFIED &&
			(record->cv & (CV_VOLATILE | CV_ATOMIC)) != 0)
			return false;
		type = record->child;
		record = &program.types.Get(type);
	}
	if (record->kind != TYPE_NAMED) return true;
	if (record->entity >= program.entities.size()) return false;
	const EntityRecord& entity = program.entities[record->entity];
	return entity.flavor != NAMED_UNION &&
		!entity.has_volatile_subobject && !entity.has_union_subobject;
}

}  // namespace zero_initialization
}  // namespace lowering
}  // namespace cppgm
