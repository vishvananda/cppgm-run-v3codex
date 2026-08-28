#include "preprocess/hosted/builtin_registry.h"

#include <cstddef>
#include <stdexcept>

namespace cppgm
{
namespace hosted_builtin
{
namespace
{

const IntegerIntrinsic kIntegerIntrinsics[] = {
	{"__builtin_bswap16", INTEGER_INTRINSIC_BSWAP16,
		INTEGER_OPERATION_BSWAP, INTEGER_ARGUMENT_UNSIGNED_SHORT, 1},
	{"__builtin_bswap32", INTEGER_INTRINSIC_BSWAP32,
		INTEGER_OPERATION_BSWAP, INTEGER_ARGUMENT_UNSIGNED_INT, 1},
	{"__builtin_bswap64", INTEGER_INTRINSIC_BSWAP64,
		INTEGER_OPERATION_BSWAP, INTEGER_ARGUMENT_UNSIGNED_LONG_LONG, 1},
	{"__builtin_clz", INTEGER_INTRINSIC_CLZ,
		INTEGER_OPERATION_CLZ, INTEGER_ARGUMENT_UNSIGNED_INT, 1},
	{"__builtin_clzg", INTEGER_INTRINSIC_CLZG,
		INTEGER_OPERATION_CLZ, INTEGER_ARGUMENT_GENERIC_UNSIGNED, 2},
	{"__builtin_clzl", INTEGER_INTRINSIC_CLZL,
		INTEGER_OPERATION_CLZ, INTEGER_ARGUMENT_UNSIGNED_LONG, 1},
	{"__builtin_clzll", INTEGER_INTRINSIC_CLZLL,
		INTEGER_OPERATION_CLZ, INTEGER_ARGUMENT_UNSIGNED_LONG_LONG, 1},
	{"__builtin_ctz", INTEGER_INTRINSIC_CTZ,
		INTEGER_OPERATION_CTZ, INTEGER_ARGUMENT_UNSIGNED_INT, 1},
	{"__builtin_ctzl", INTEGER_INTRINSIC_CTZL,
		INTEGER_OPERATION_CTZ, INTEGER_ARGUMENT_UNSIGNED_LONG, 1},
	{"__builtin_ctzll", INTEGER_INTRINSIC_CTZLL,
		INTEGER_OPERATION_CTZ, INTEGER_ARGUMENT_UNSIGNED_LONG_LONG, 1},
	{"__builtin_popcount", INTEGER_INTRINSIC_POPCOUNT,
		INTEGER_OPERATION_POPCOUNT, INTEGER_ARGUMENT_UNSIGNED_INT, 1},
	{"__builtin_popcountg", INTEGER_INTRINSIC_POPCOUNTG,
		INTEGER_OPERATION_POPCOUNT, INTEGER_ARGUMENT_GENERIC_UNSIGNED, 1},
	{"__builtin_popcountl", INTEGER_INTRINSIC_POPCOUNTL,
		INTEGER_OPERATION_POPCOUNT, INTEGER_ARGUMENT_UNSIGNED_LONG, 1},
	{"__builtin_popcountll", INTEGER_INTRINSIC_POPCOUNTLL,
		INTEGER_OPERATION_POPCOUNT, INTEGER_ARGUMENT_UNSIGNED_LONG_LONG, 1}
};

const FloatingIntrinsic kFloatingIntrinsics[] = {
	{"__builtin_ceil", FLOATING_INTRINSIC_CEIL,
		FLOATING_OPERATION_EXTERNAL_CEIL, FLOATING_FORMAT_DOUBLE, 1},
	{"__builtin_ceilf", FLOATING_INTRINSIC_CEILF,
		FLOATING_OPERATION_EXTERNAL_CEIL, FLOATING_FORMAT_FLOAT, 1},
	{"__builtin_ceill", FLOATING_INTRINSIC_CEILL,
		FLOATING_OPERATION_EXTERNAL_CEIL, FLOATING_FORMAT_LONG_DOUBLE, 1},
	{"__builtin_flt_rounds", FLOATING_INTRINSIC_FLT_ROUNDS,
		FLOATING_OPERATION_ROUNDING_MODE, FLOATING_FORMAT_NONE, 0},
	{"__builtin_fpclassify", FLOATING_INTRINSIC_FPCLASSIFY,
		FLOATING_OPERATION_CLASSIFY, FLOATING_FORMAT_NONE, 6},
	{"__builtin_huge_val", FLOATING_INTRINSIC_HUGE_VAL,
		FLOATING_OPERATION_INFINITY, FLOATING_FORMAT_DOUBLE, 0},
	{"__builtin_huge_valf", FLOATING_INTRINSIC_HUGE_VALF,
		FLOATING_OPERATION_INFINITY, FLOATING_FORMAT_FLOAT, 0},
	{"__builtin_huge_vall", FLOATING_INTRINSIC_HUGE_VALL,
		FLOATING_OPERATION_INFINITY, FLOATING_FORMAT_LONG_DOUBLE, 0},
	{"__builtin_inf", FLOATING_INTRINSIC_INF,
		FLOATING_OPERATION_INFINITY, FLOATING_FORMAT_DOUBLE, 0},
	{"__builtin_inff", FLOATING_INTRINSIC_INFF,
		FLOATING_OPERATION_INFINITY, FLOATING_FORMAT_FLOAT, 0},
	{"__builtin_infl", FLOATING_INTRINSIC_INFL,
		FLOATING_OPERATION_INFINITY, FLOATING_FORMAT_LONG_DOUBLE, 0},
	{"__builtin_isfinite", FLOATING_INTRINSIC_ISFINITE,
		FLOATING_OPERATION_ISFINITE, FLOATING_FORMAT_NONE, 1},
	{"__builtin_isinf", FLOATING_INTRINSIC_ISINF,
		FLOATING_OPERATION_ISINF, FLOATING_FORMAT_NONE, 1},
	{"__builtin_isnan", FLOATING_INTRINSIC_ISNAN,
		FLOATING_OPERATION_ISNAN, FLOATING_FORMAT_NONE, 1},
	{"__builtin_isnormal", FLOATING_INTRINSIC_ISNORMAL,
		FLOATING_OPERATION_ISNORMAL, FLOATING_FORMAT_NONE, 1},
	{"__builtin_nan", FLOATING_INTRINSIC_NAN,
		FLOATING_OPERATION_NAN, FLOATING_FORMAT_DOUBLE, 1},
	{"__builtin_nanf", FLOATING_INTRINSIC_NANF,
		FLOATING_OPERATION_NAN, FLOATING_FORMAT_FLOAT, 1},
	{"__builtin_nanl", FLOATING_INTRINSIC_NANL,
		FLOATING_OPERATION_NAN, FLOATING_FORMAT_LONG_DOUBLE, 1},
	{"__builtin_nans", FLOATING_INTRINSIC_NANS,
		FLOATING_OPERATION_SNAN, FLOATING_FORMAT_DOUBLE, 1},
	{"__builtin_nansf", FLOATING_INTRINSIC_NANSF,
		FLOATING_OPERATION_SNAN, FLOATING_FORMAT_FLOAT, 1},
	{"__builtin_nansl", FLOATING_INTRINSIC_NANSL,
		FLOATING_OPERATION_SNAN, FLOATING_FORMAT_LONG_DOUBLE, 1},
	{"__builtin_signbit", FLOATING_INTRINSIC_SIGNBIT,
		FLOATING_OPERATION_SIGNBIT, FLOATING_FORMAT_NONE, 1}
};

const MemoryIntrinsic kMemoryIntrinsics[] = {
	{"__builtin_assume_aligned", MEMORY_INTRINSIC_ASSUME_ALIGNED, 2, 3,
		MEMORY_EFFECT_READNONE, MEMORY_LOWER_IDENTITY},
	{"__builtin_bzero", MEMORY_INTRINSIC_BZERO, 2, 2,
		MEMORY_EFFECT_READWRITE, MEMORY_LOWER_EXTERNAL},
	{"__builtin_memchr", MEMORY_INTRINSIC_MEMCHR, 3, 3,
		MEMORY_EFFECT_READONLY, MEMORY_LOWER_EXTERNAL},
	{"__builtin_memcmp", MEMORY_INTRINSIC_MEMCMP, 3, 3,
		MEMORY_EFFECT_READONLY, MEMORY_LOWER_EXTERNAL},
	{"__builtin_memcpy", MEMORY_INTRINSIC_MEMCPY, 3, 3,
		MEMORY_EFFECT_READWRITE, MEMORY_LOWER_EXTERNAL},
	{"__builtin_memmove", MEMORY_INTRINSIC_MEMMOVE, 3, 3,
		MEMORY_EFFECT_READWRITE, MEMORY_LOWER_EXTERNAL},
	{"__builtin_memset", MEMORY_INTRINSIC_MEMSET, 3, 3,
		MEMORY_EFFECT_READWRITE, MEMORY_LOWER_EXTERNAL},
	{"__builtin_prefetch", MEMORY_INTRINSIC_PREFETCH, 1, 3,
		MEMORY_EFFECT_READNONE, MEMORY_LOWER_NOOP},
	{"__builtin_strchr", MEMORY_INTRINSIC_STRCHR, 2, 2,
		MEMORY_EFFECT_READONLY, MEMORY_LOWER_EXTERNAL},
	{"__builtin_strcmp", MEMORY_INTRINSIC_STRCMP, 2, 2,
		MEMORY_EFFECT_READONLY, MEMORY_LOWER_EXTERNAL},
	{"__builtin_strlen", MEMORY_INTRINSIC_STRLEN, 1, 1,
		MEMORY_EFFECT_READONLY, MEMORY_LOWER_EXTERNAL}
};

const VectorIntrinsic kVectorIntrinsics[] = {
	{"__builtin_ia32_vec_ext_v2si", VECTOR_INTRINSIC_IA32_VEC_EXT_V2SI,
		VECTOR_OPERATION_EXTRACT, VECTOR_ELEMENT_I32, 2},
	{"__builtin_ia32_vec_init_v2si",
		VECTOR_INTRINSIC_IA32_VEC_INIT_V2SI,
		VECTOR_OPERATION_INIT, VECTOR_ELEMENT_I32, 2},
	{"__builtin_ia32_vec_init_v4hi",
		VECTOR_INTRINSIC_IA32_VEC_INIT_V4HI,
		VECTOR_OPERATION_INIT, VECTOR_ELEMENT_I16, 4},
	{"__builtin_ia32_vec_init_v8qi",
		VECTOR_INTRINSIC_IA32_VEC_INIT_V8QI,
		VECTOR_OPERATION_INIT, VECTOR_ELEMENT_I8, 8}
};

const AtomicIntrinsic kAtomicIntrinsics[] = {
	{"__atomic_add_fetch", ATOMIC_INTRINSIC_ADD_FETCH,
		ATOMIC_SHAPE_FETCH_UPDATE, ATOMIC_UPDATE_ADD, 3, true, false},
	{"__atomic_always_lock_free", ATOMIC_INTRINSIC_ALWAYS_LOCK_FREE,
		ATOMIC_SHAPE_LOCK_FREE, ATOMIC_UPDATE_NONE, 2, false, false},
	{"__atomic_clear", ATOMIC_INTRINSIC_CLEAR,
		ATOMIC_SHAPE_CLEAR, ATOMIC_UPDATE_NONE, 2, false, false},
	{"__atomic_compare_exchange_n", ATOMIC_INTRINSIC_COMPARE_EXCHANGE_N,
		ATOMIC_SHAPE_COMPARE_EXCHANGE, ATOMIC_UPDATE_NONE, 6, false, false},
	{"__atomic_exchange_n", ATOMIC_INTRINSIC_EXCHANGE_N,
		ATOMIC_SHAPE_EXCHANGE, ATOMIC_UPDATE_NONE, 3, false, false},
	{"__atomic_fetch_add", ATOMIC_INTRINSIC_FETCH_ADD,
		ATOMIC_SHAPE_FETCH_UPDATE, ATOMIC_UPDATE_ADD, 3, false, false},
	{"__atomic_fetch_and", ATOMIC_INTRINSIC_FETCH_AND,
		ATOMIC_SHAPE_FETCH_UPDATE, ATOMIC_UPDATE_AND, 3, false, false},
	{"__atomic_fetch_or", ATOMIC_INTRINSIC_FETCH_OR,
		ATOMIC_SHAPE_FETCH_UPDATE, ATOMIC_UPDATE_OR, 3, false, false},
	{"__atomic_fetch_sub", ATOMIC_INTRINSIC_FETCH_SUB,
		ATOMIC_SHAPE_FETCH_UPDATE, ATOMIC_UPDATE_SUB, 3, false, false},
	{"__atomic_fetch_xor", ATOMIC_INTRINSIC_FETCH_XOR,
		ATOMIC_SHAPE_FETCH_UPDATE, ATOMIC_UPDATE_XOR, 3, false, false},
	{"__atomic_is_lock_free", ATOMIC_INTRINSIC_IS_LOCK_FREE,
		ATOMIC_SHAPE_LOCK_FREE, ATOMIC_UPDATE_NONE, 2, false, false},
	{"__atomic_load", ATOMIC_INTRINSIC_LOAD,
		ATOMIC_SHAPE_LOAD_OUT, ATOMIC_UPDATE_NONE, 3, false, false},
	{"__atomic_load_n", ATOMIC_INTRINSIC_LOAD_N,
		ATOMIC_SHAPE_LOAD, ATOMIC_UPDATE_NONE, 2, false, false},
	{"__atomic_signal_fence", ATOMIC_INTRINSIC_SIGNAL_FENCE,
		ATOMIC_SHAPE_FENCE, ATOMIC_UPDATE_NONE, 1, false, false},
	{"__atomic_store", ATOMIC_INTRINSIC_STORE,
		ATOMIC_SHAPE_STORE_FROM, ATOMIC_UPDATE_NONE, 3, false, false},
	{"__atomic_store_n", ATOMIC_INTRINSIC_STORE_N,
		ATOMIC_SHAPE_STORE, ATOMIC_UPDATE_NONE, 3, false, false},
	{"__atomic_test_and_set", ATOMIC_INTRINSIC_TEST_AND_SET,
		ATOMIC_SHAPE_TEST_AND_SET, ATOMIC_UPDATE_NONE, 2, false, false},
	{"__atomic_thread_fence", ATOMIC_INTRINSIC_THREAD_FENCE,
		ATOMIC_SHAPE_FENCE, ATOMIC_UPDATE_NONE, 1, false, false},
	{"__c11_atomic_compare_exchange_strong",
		ATOMIC_INTRINSIC_C11_COMPARE_EXCHANGE_STRONG,
		ATOMIC_SHAPE_COMPARE_EXCHANGE, ATOMIC_UPDATE_NONE, 5, false, true},
	{"__c11_atomic_compare_exchange_weak",
		ATOMIC_INTRINSIC_C11_COMPARE_EXCHANGE_WEAK,
		ATOMIC_SHAPE_COMPARE_EXCHANGE, ATOMIC_UPDATE_NONE, 5, false, true},
	{"__c11_atomic_exchange", ATOMIC_INTRINSIC_C11_EXCHANGE,
		ATOMIC_SHAPE_EXCHANGE, ATOMIC_UPDATE_NONE, 3, false, true},
	{"__c11_atomic_fetch_add", ATOMIC_INTRINSIC_C11_FETCH_ADD,
		ATOMIC_SHAPE_FETCH_UPDATE, ATOMIC_UPDATE_ADD, 3, false, true},
	{"__c11_atomic_fetch_and", ATOMIC_INTRINSIC_C11_FETCH_AND,
		ATOMIC_SHAPE_FETCH_UPDATE, ATOMIC_UPDATE_AND, 3, false, true},
	{"__c11_atomic_fetch_or", ATOMIC_INTRINSIC_C11_FETCH_OR,
		ATOMIC_SHAPE_FETCH_UPDATE, ATOMIC_UPDATE_OR, 3, false, true},
	{"__c11_atomic_fetch_sub", ATOMIC_INTRINSIC_C11_FETCH_SUB,
		ATOMIC_SHAPE_FETCH_UPDATE, ATOMIC_UPDATE_SUB, 3, false, true},
	{"__c11_atomic_fetch_xor", ATOMIC_INTRINSIC_C11_FETCH_XOR,
		ATOMIC_SHAPE_FETCH_UPDATE, ATOMIC_UPDATE_XOR, 3, false, true},
	{"__c11_atomic_init", ATOMIC_INTRINSIC_C11_INIT,
		ATOMIC_SHAPE_STORE, ATOMIC_UPDATE_NONE, 2, false, true},
	{"__c11_atomic_is_lock_free", ATOMIC_INTRINSIC_C11_IS_LOCK_FREE,
		ATOMIC_SHAPE_LOCK_FREE, ATOMIC_UPDATE_NONE, 1, false, false},
	{"__c11_atomic_load", ATOMIC_INTRINSIC_C11_LOAD,
		ATOMIC_SHAPE_LOAD, ATOMIC_UPDATE_NONE, 2, false, true},
	{"__c11_atomic_signal_fence", ATOMIC_INTRINSIC_C11_SIGNAL_FENCE,
		ATOMIC_SHAPE_FENCE, ATOMIC_UPDATE_NONE, 1, false, false},
	{"__c11_atomic_store", ATOMIC_INTRINSIC_C11_STORE,
		ATOMIC_SHAPE_STORE, ATOMIC_UPDATE_NONE, 3, false, true},
	{"__c11_atomic_thread_fence", ATOMIC_INTRINSIC_C11_THREAD_FENCE,
		ATOMIC_SHAPE_FENCE, ATOMIC_UPDATE_NONE, 1, false, false},
	{"__sync_fetch_and_add", ATOMIC_INTRINSIC_SYNC_FETCH_AND_ADD,
		ATOMIC_SHAPE_FETCH_UPDATE, ATOMIC_UPDATE_ADD, 2, false, false},
	{"__sync_lock_release", ATOMIC_INTRINSIC_SYNC_LOCK_RELEASE,
		ATOMIC_SHAPE_CLEAR, ATOMIC_UPDATE_NONE, 1, false, false},
	{"__sync_lock_test_and_set", ATOMIC_INTRINSIC_SYNC_LOCK_TEST_AND_SET,
		ATOMIC_SHAPE_EXCHANGE, ATOMIC_UPDATE_NONE, 2, false, false},
	{"__sync_synchronize", ATOMIC_INTRINSIC_SYNC_SYNCHRONIZE,
		ATOMIC_SHAPE_FENCE, ATOMIC_UPDATE_NONE, 0, false, false},
	{"__sync_val_compare_and_swap", ATOMIC_INTRINSIC_SYNC_VAL_COMPARE_AND_SWAP,
		ATOMIC_SHAPE_SYNC_COMPARE_EXCHANGE, ATOMIC_UPDATE_NONE, 3, false, false}
};

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
		{"__array_rank", TYPE_TRAIT_ARRAY_RANK},
		{"__has_nothrow_copy", TYPE_TRAIT_HAS_NOTHROW_COPY},
		{"__has_trivial_constructor", TYPE_TRAIT_HAS_TRIVIAL_CONSTRUCTOR},
		{"__has_trivial_destructor", TYPE_TRAIT_IS_TRIVIALLY_DESTRUCTIBLE},
		{"__has_virtual_destructor", TYPE_TRAIT_HAS_VIRTUAL_DESTRUCTOR},
		{"__is_abstract", TYPE_TRAIT_IS_ABSTRACT},
		{"__is_aggregate", TYPE_TRAIT_IS_AGGREGATE},
		{"__is_assignable", TYPE_TRAIT_IS_ASSIGNABLE},
		{"__is_base_of", TYPE_TRAIT_IS_BASE_OF},
		{"__is_class", TYPE_TRAIT_IS_CLASS},
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
		{"__is_union", TYPE_TRAIT_IS_UNION},
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

const IntegerIntrinsic* FindIntegerIntrinsic(const std::string& spelling)
{
	std::size_t first = 0;
	std::size_t count = sizeof(kIntegerIntrinsics) /
		sizeof(kIntegerIntrinsics[0]);
	while (first < count)
	{
		const std::size_t middle = first + (count - first) / 2;
		const int order = spelling.compare(kIntegerIntrinsics[middle].spelling);
		if (order < 0) count = middle;
		else if (order > 0) first = middle + 1;
		else return &kIntegerIntrinsics[middle];
	}
	return 0;
}

const IntegerIntrinsic& GetIntegerIntrinsic(IntegerIntrinsicKind kind)
{
	if (kind <= INTEGER_INTRINSIC_NONE || kind >= INTEGER_INTRINSIC_COUNT)
		throw std::logic_error("invalid hosted integer intrinsic kind");
	return kIntegerIntrinsics[static_cast<std::size_t>(kind) - 1];
}

const FloatingIntrinsic* FindFloatingIntrinsic(const std::string& spelling)
{
	std::size_t first = 0;
	std::size_t count = sizeof(kFloatingIntrinsics) /
		sizeof(kFloatingIntrinsics[0]);
	while (first < count)
	{
		const std::size_t middle = first + (count - first) / 2;
		const int order = spelling.compare(kFloatingIntrinsics[middle].spelling);
		if (order < 0) count = middle;
		else if (order > 0) first = middle + 1;
		else return &kFloatingIntrinsics[middle];
	}
	return 0;
}

const FloatingIntrinsic& GetFloatingIntrinsic(FloatingIntrinsicKind kind)
{
	if (kind <= FLOATING_INTRINSIC_NONE || kind >= FLOATING_INTRINSIC_COUNT)
		throw std::logic_error("invalid hosted floating intrinsic kind");
	return kFloatingIntrinsics[static_cast<std::size_t>(kind) - 1];
}

const MemoryIntrinsic* FindMemoryIntrinsic(const std::string& spelling)
{
	std::size_t first = 0;
	std::size_t count = sizeof(kMemoryIntrinsics) /
		sizeof(kMemoryIntrinsics[0]);
	while (first < count)
	{
		const std::size_t middle = first + (count - first) / 2;
		const int order = spelling.compare(kMemoryIntrinsics[middle].spelling);
		if (order < 0) count = middle;
		else if (order > 0) first = middle + 1;
		else return &kMemoryIntrinsics[middle];
	}
	return 0;
}

const MemoryIntrinsic& GetMemoryIntrinsic(MemoryIntrinsicKind kind)
{
	if (kind <= MEMORY_INTRINSIC_NONE || kind >= MEMORY_INTRINSIC_COUNT)
		throw std::logic_error("invalid hosted memory intrinsic kind");
	return kMemoryIntrinsics[static_cast<std::size_t>(kind) - 1];
}

const VectorIntrinsic* FindVectorIntrinsic(const std::string& spelling)
{
	std::size_t first = 0;
	std::size_t count = sizeof(kVectorIntrinsics) /
		sizeof(kVectorIntrinsics[0]);
	while (first < count)
	{
		const std::size_t middle = first + (count - first) / 2;
		const int order = spelling.compare(kVectorIntrinsics[middle].spelling);
		if (order < 0) count = middle;
		else if (order > 0) first = middle + 1;
		else return &kVectorIntrinsics[middle];
	}
	return 0;
}

const VectorIntrinsic& GetVectorIntrinsic(VectorIntrinsicKind kind)
{
	if (kind <= VECTOR_INTRINSIC_NONE || kind >= VECTOR_INTRINSIC_COUNT)
		throw std::logic_error("invalid hosted vector intrinsic kind");
	return kVectorIntrinsics[static_cast<std::size_t>(kind) - 1];
}

const AtomicIntrinsic* FindAtomicIntrinsic(const std::string& spelling)
{
	std::size_t first = 0;
	std::size_t count = sizeof(kAtomicIntrinsics) /
		sizeof(kAtomicIntrinsics[0]);
	while (first < count)
	{
		const std::size_t middle = first + (count - first) / 2;
		const int order = spelling.compare(kAtomicIntrinsics[middle].spelling);
		if (order < 0) count = middle;
		else if (order > 0) first = middle + 1;
		else return &kAtomicIntrinsics[middle];
	}
	return 0;
}

const AtomicIntrinsic& GetAtomicIntrinsic(AtomicIntrinsicKind kind)
{
	if (kind <= ATOMIC_INTRINSIC_NONE || kind >= ATOMIC_INTRINSIC_COUNT)
		throw std::logic_error("invalid hosted atomic intrinsic kind");
	return kAtomicIntrinsics[static_cast<std::size_t>(kind) - 1];
}

}
}
