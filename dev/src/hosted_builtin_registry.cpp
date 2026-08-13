#include "hosted_builtin_registry.h"

#include <cstddef>

namespace cppgm
{
namespace hosted_builtin
{
namespace
{

template <class Kind>
struct Entry
{
	const char* spelling;
	Kind kind;
};

template <class Kind, std::size_t Size>
Kind FindEntry(const std::string& spelling, const Entry<Kind> (&entries)[Size],
	Kind missing)
{
	std::size_t first = 0;
	std::size_t count = Size;
	while (first < count)
	{
		const std::size_t middle = first + (count - first) / 2;
		const int order = spelling.compare(entries[middle].spelling);
		if (order < 0) count = middle;
		else if (order > 0) first = middle + 1;
		else return entries[middle].kind;
	}
	return missing;
}

}

TypeTraitKind FindTypeTrait(const std::string& spelling)
{
	static const Entry<TypeTraitKind> entries[] = {
		{"__has_nothrow_copy", TYPE_TRAIT_HAS_NOTHROW_COPY},
		{"__has_trivial_constructor", TYPE_TRAIT_HAS_TRIVIAL_CONSTRUCTOR},
		{"__has_virtual_destructor", TYPE_TRAIT_HAS_VIRTUAL_DESTRUCTOR},
		{"__is_abstract", TYPE_TRAIT_IS_ABSTRACT},
		{"__is_aggregate", TYPE_TRAIT_IS_AGGREGATE},
		{"__is_assignable", TYPE_TRAIT_IS_ASSIGNABLE},
		{"__is_base_of", TYPE_TRAIT_IS_BASE_OF},
		{"__is_complete_or_unbounded", TYPE_TRAIT_IS_COMPLETE_OR_UNBOUNDED},
		{"__is_constructible", TYPE_TRAIT_IS_CONSTRUCTIBLE},
		{"__is_convertible", TYPE_TRAIT_IS_CONVERTIBLE},
		{"__is_destructible", TYPE_TRAIT_IS_DESTRUCTIBLE},
		{"__is_empty", TYPE_TRAIT_IS_EMPTY},
		{"__is_enum", TYPE_TRAIT_IS_ENUM},
		{"__is_final", TYPE_TRAIT_IS_FINAL},
		{"__is_floating_point", TYPE_TRAIT_IS_FLOATING_POINT},
		{"__is_function", TYPE_TRAIT_IS_FUNCTION},
		{"__is_integral", TYPE_TRAIT_IS_INTEGRAL},
		{"__is_invocable", TYPE_TRAIT_IS_INVOCABLE},
		{"__is_literal_type", TYPE_TRAIT_IS_LITERAL_TYPE},
		{"__is_member_function_pointer", TYPE_TRAIT_IS_MEMBER_FUNCTION_POINTER},
		{"__is_member_object_pointer", TYPE_TRAIT_IS_MEMBER_OBJECT_POINTER},
		{"__is_member_pointer", TYPE_TRAIT_IS_MEMBER_POINTER},
		{"__is_nothrow_assignable", TYPE_TRAIT_IS_NOTHROW_ASSIGNABLE},
		{"__is_nothrow_constructible", TYPE_TRAIT_IS_NOTHROW_CONSTRUCTIBLE},
		{"__is_nothrow_invocable", TYPE_TRAIT_IS_NOTHROW_INVOCABLE},
		{"__is_pod", TYPE_TRAIT_IS_POD},
		{"__is_pointer", TYPE_TRAIT_IS_POINTER},
		{"__is_polymorphic", TYPE_TRAIT_IS_POLYMORPHIC},
		{"__is_reference", TYPE_TRAIT_IS_REFERENCE},
		{"__is_same", TYPE_TRAIT_IS_SAME},
		{"__is_scalar", TYPE_TRAIT_IS_SCALAR},
		{"__is_signed", TYPE_TRAIT_IS_SIGNED},
		{"__is_standard_layout", TYPE_TRAIT_IS_STANDARD_LAYOUT},
		{"__is_trivial", TYPE_TRAIT_IS_TRIVIAL},
		{"__is_trivially_assignable", TYPE_TRAIT_IS_TRIVIALLY_ASSIGNABLE},
		{"__is_trivially_constructible", TYPE_TRAIT_IS_TRIVIALLY_CONSTRUCTIBLE},
		{"__is_trivially_copyable", TYPE_TRAIT_IS_TRIVIALLY_COPYABLE},
		{"__is_trivially_destructible", TYPE_TRAIT_IS_TRIVIALLY_DESTRUCTIBLE},
		{"__reference_binds_to_temporary", TYPE_TRAIT_REFERENCE_BINDS_TO_TEMPORARY},
		{"__reference_constructs_from_temporary", TYPE_TRAIT_REFERENCE_CONSTRUCTS_FROM_TEMPORARY}
	};
	return FindEntry(spelling, entries, TYPE_TRAIT_NONE);
}

TypeTransformKind FindTypeTransform(const std::string& spelling)
{
	static const Entry<TypeTransformKind> entries[] = {
		{"__add_lvalue_reference", TYPE_TRANSFORM_ADD_LVALUE_REFERENCE},
		{"__add_pointer", TYPE_TRANSFORM_ADD_POINTER},
		{"__add_rvalue_reference", TYPE_TRANSFORM_ADD_RVALUE_REFERENCE},
		{"__decay", TYPE_TRANSFORM_DECAY},
		{"__make_signed", TYPE_TRANSFORM_MAKE_SIGNED},
		{"__make_unsigned", TYPE_TRANSFORM_MAKE_UNSIGNED},
		{"__remove_all_extents", TYPE_TRANSFORM_REMOVE_ALL_EXTENTS},
		{"__remove_const", TYPE_TRANSFORM_REMOVE_CONST},
		{"__remove_cv", TYPE_TRANSFORM_REMOVE_CV},
		{"__remove_cvref", TYPE_TRANSFORM_REMOVE_CVREF},
		{"__remove_pointer", TYPE_TRANSFORM_REMOVE_POINTER},
		{"__remove_reference_t", TYPE_TRANSFORM_REMOVE_REFERENCE},
		{"__remove_volatile", TYPE_TRANSFORM_REMOVE_VOLATILE},
		{"__underlying_type", TYPE_TRANSFORM_UNDERLYING_TYPE}
	};
	return FindEntry(spelling, entries, TYPE_TRANSFORM_NONE);
}

}
}
