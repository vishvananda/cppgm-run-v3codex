#include "semantic/analysis/analyzer.h"

#include "support/containers/flat_hash_map.h"

#include <vector>

namespace cppgm
{
namespace semantic
{

bool TypeContainsDependentTemplateShape(const Program& program, TypeId type,
	std::size_t)
{
	if (type == kNoType) return true;
	const std::size_t type_count = program.types.Size();
	std::vector<TypeId> pending(1, type);
	detail::FlatHashMap<TypeId, unsigned char> visited;
	while (!pending.empty())
	{
		type = pending.back();
		pending.pop_back();
		if (type == kNoType || type == 0 || type > type_count) return true;
		type = program.types.RemoveTopCv(type);
		if (type == 0 || type > type_count) return true;
		if (visited.Find(type)) continue;
		visited.Insert(type, 1);
		const TypeRecord& record = program.types.Get(type);
		switch (record.kind)
		{
		case TYPE_POINTER:
		case TYPE_LVALUE_REFERENCE:
		case TYPE_RVALUE_REFERENCE:
		case TYPE_BLOCK_POINTER:
		case TYPE_COMPLEX:
			pending.push_back(record.child);
			break;
		case TYPE_ARRAY:
		case TYPE_VECTOR:
			if (record.dependent_bound_parameter != kNoTemplateParameter)
				return true;
			pending.push_back(record.child);
			if (record.dependent_bound_type != kNoType)
				pending.push_back(record.dependent_bound_type);
			break;
		case TYPE_BITINT:
			if (record.dependent_bound_parameter != kNoTemplateParameter)
				return true;
			if (record.dependent_bound_type != kNoType)
				pending.push_back(record.dependent_bound_type);
			break;
		case TYPE_FUNCTION:
		{
			pending.push_back(record.child);
			const TypeId* parameters = program.types.Parameters(type);
			for (std::size_t i = 0; i < record.parameter_count; ++i)
				pending.push_back(parameters[i]);
			break;
		}
		case TYPE_MEMBER_POINTER:
			pending.push_back(record.child);
			if (record.bound > type_count) return true;
			pending.push_back(static_cast<TypeId>(record.bound));
			break;
		case TYPE_NAMED:
		{
			if (record.entity >= program.entities.size()) return true;
			const EntityRecord& entity = program.entities[record.entity];
			if (entity.flavor == NAMED_TYPENAME_PARAMETER ||
				entity.flavor == NAMED_TEMPLATE_PARAMETER) return true;
			const std::size_t first = entity.template_argument_begin;
			const std::size_t count = entity.template_argument_count;
			if (first == kNoBinding) break;
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
			break;
		}
		default:
			break;
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
