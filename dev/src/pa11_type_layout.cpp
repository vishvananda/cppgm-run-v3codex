#include "pa11_model.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa11
{

std::size_t Program::SizeOf(TypeId type) const
{
	std::size_t multiplier = 1;
	while (true)
	{
		const TypeRecord& record = types.Get(type);
		if (record.kind == TYPE_QUALIFIED)
		{
			type = record.child;
			continue;
		}
		if (record.kind == TYPE_ARRAY)
		{
			if (record.dependent_bound_parameter != kNoTemplateParameter ||
				record.bound == 0 ||
				record.bound > std::numeric_limits<std::size_t>::max() ||
				multiplier > std::numeric_limits<std::size_t>::max() /
					static_cast<std::size_t>(record.bound))
				throw std::runtime_error("invalid array size");
			multiplier *= static_cast<std::size_t>(record.bound);
			type = record.child;
			continue;
		}
		std::size_t size = 0;
		switch (record.kind)
		{
		case TYPE_FUNDAMENTAL: size = FundamentalSize(record.fundamental); break;
		case TYPE_POINTER: case TYPE_LVALUE_REFERENCE:
		case TYPE_RVALUE_REFERENCE: size = 8; break;
		case TYPE_MEMBER_POINTER:
			size = types.IsFunction(record.child) ? 16 : 8;
			break;
		case TYPE_VECTOR:
			if (record.bound == 0 ||
				record.bound > std::numeric_limits<std::size_t>::max())
				throw std::runtime_error("invalid GNU vector size");
			size = static_cast<std::size_t>(record.bound);
			break;
		case TYPE_NAMED:
		{
			const EntityRecord& entity = entities[record.entity];
			if (!entity.complete)
				throw std::runtime_error("incomplete named type");
			if (entity.flavor == NAMED_ENUM || entity.flavor == NAMED_ENUM_CLASS)
				size = SizeOf(entity.underlying);
			else
			{
				if (!entity.layout_complete || entity.object_size == 0)
					throw std::runtime_error("class layout is incomplete");
				size = static_cast<std::size_t>(entity.object_size);
			}
			break;
		}
		default: throw std::runtime_error("invalid sizeof operand type");
		}
		if (multiplier > std::numeric_limits<std::size_t>::max() / size)
			throw std::runtime_error("object type is too large");
		return multiplier * size;
	}
}

std::size_t Program::AlignOf(TypeId type) const
{
	const TypeRecord* record = &types.Get(type);
	bool atomic = false;
	while (record->kind == TYPE_QUALIFIED || record->kind == TYPE_ARRAY)
	{
		if (record->kind == TYPE_QUALIFIED &&
			(record->cv & CV_ATOMIC) != 0) atomic = true;
		type = record->child;
		record = &types.Get(type);
	}
	std::size_t alignment = 0;
	if (record->kind == TYPE_POINTER || record->kind == TYPE_LVALUE_REFERENCE ||
		record->kind == TYPE_RVALUE_REFERENCE ||
		record->kind == TYPE_MEMBER_POINTER) alignment = 8;
	if (record->kind == TYPE_FUNDAMENTAL)
		alignment = FundamentalSize(record->fundamental);
	else if (record->kind == TYPE_VECTOR)
	{
		if (record->bound == 0 ||
			record->bound > std::numeric_limits<std::size_t>::max())
			throw std::runtime_error("invalid GNU vector alignment");
		alignment = static_cast<std::size_t>(record->bound);
	}
	else if (record->kind == TYPE_NAMED)
	{
		const EntityRecord& entity = entities[record->entity];
		if (!entity.complete)
			throw std::runtime_error("incomplete named type");
		if (entity.flavor == NAMED_ENUM || entity.flavor == NAMED_ENUM_CLASS)
			alignment = AlignOf(entity.underlying);
		else
		{
			if (!entity.layout_complete || entity.object_alignment == 0)
				throw std::runtime_error("class layout is incomplete");
			alignment = static_cast<std::size_t>(entity.object_alignment);
		}
	}
	if (alignment == 0)
		throw std::runtime_error("invalid alignof operand type");
	if (atomic && SizeOf(type) == 16)
		alignment = std::max<std::size_t>(16, alignment);
	return alignment;
}

}
}
