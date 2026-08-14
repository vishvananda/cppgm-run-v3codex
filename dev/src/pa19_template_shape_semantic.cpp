#include "pa12_semantic_detail.h"

#include "flat_hash_map.h"

#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool TypeContainsDependentTemplateShape(const Program& program, TypeId type,
	std::size_t)
{
	if (type == kNoType) return true;
	std::vector<TypeId> pending(1, type);
	detail::FlatHashMap<TypeId, unsigned char> visited;
	while (!pending.empty())
	{
		type = pending.back();
		pending.pop_back();
		if (type == kNoType || type >= program.types.Size()) return true;
		type = program.types.RemoveTopCv(type);
		if (type >= program.types.Size()) return true;
		if (visited.Find(type)) continue;
		visited.Insert(type, 1);
		const TypeRecord& record = program.types.Get(type);
		if (record.kind == TYPE_POINTER ||
			record.kind == TYPE_LVALUE_REFERENCE ||
			record.kind == TYPE_RVALUE_REFERENCE || record.kind == TYPE_ARRAY ||
			record.kind == TYPE_MEMBER_POINTER)
		{
			pending.push_back(record.child);
			continue;
		}
		if (record.kind != TYPE_NAMED) continue;
		if (record.entity >= program.entities.size()) return true;
		const EntityRecord& entity = program.entities[record.entity];
		if (entity.flavor == NAMED_TYPENAME_PARAMETER ||
			entity.flavor == NAMED_TEMPLATE_PARAMETER) return true;
		const std::size_t first = entity.template_argument_begin;
		const std::size_t count = entity.template_argument_count;
		if (first == kNoBinding) continue;
		if (first > program.canonical_template_arguments.size() ||
			count > program.canonical_template_arguments.size() - first)
			return true;
		for (std::size_t i = 0; i < count; ++i)
		{
			const TemplateArgument& argument =
				program.canonical_template_arguments[first + i];
			if (argument.IsDependent()) return true;
			if (argument.kind == TEMPLATE_ARGUMENT_TYPE)
				pending.push_back(argument.type);
		}
	}
	return false;
}

bool ClassTemplateArgumentsHaveDependentShape(const Program& program,
	const std::vector<TemplateArgument>& arguments)
{
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		if (arguments[i].IsDependent()) return true;
		if (arguments[i].kind == TEMPLATE_ARGUMENT_TYPE &&
			TypeContainsDependentTemplateShape(
				program, arguments[i].type, 0)) return true;
	}
	return false;
}

}
}
