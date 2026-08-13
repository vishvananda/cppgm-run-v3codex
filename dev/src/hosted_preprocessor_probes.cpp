#include "hosted_preprocessor_probes.h"

#include "hosted_builtin_registry.h"

#include <cstddef>

namespace cppgm
{
namespace
{

bool IsSortedMetadataEntry(const std::string& value,
	const char* const* entries, std::size_t count)
{
	std::size_t first = 0;
	while (first < count)
	{
		const std::size_t middle = first + (count - first) / 2;
		const int order = value.compare(entries[middle]);
		if (order < 0)
			count = middle;
		else if (order > 0)
			first = middle + 1;
		else
			return true;
	}
	return false;
}

}

bool IsSupportedFeatureProbe(const std::string& value)
{
	static const char* const entries[] = {
		"__cxx_binary_literals__",
		"__cxx_variable_templates__",
		"c_atomic",
		"cxx_alias_templates",
		"cxx_alignas",
		"cxx_alignof",
		"cxx_atomic",
		"cxx_auto_type",
		"cxx_constexpr",
		"cxx_decltype",
		"cxx_decltype_incomplete_return_types",
		"cxx_default_function_template_args",
		"cxx_defaulted_functions",
		"cxx_deleted_functions",
		"cxx_explicit_conversions",
		"cxx_generalized_initializers",
		"cxx_inline_namespaces",
		"cxx_lambdas",
		"cxx_local_type_template_args",
		"cxx_noexcept",
		"cxx_nullptr",
		"cxx_override_control",
		"cxx_range_for",
		"cxx_raw_string_literals",
		"cxx_reference_qualified_functions",
		"cxx_rvalue_references",
		"cxx_static_assert",
		"cxx_strong_enums",
		"cxx_trailing_return",
		"cxx_unicode_literals",
		"cxx_unrestricted_unions",
		"cxx_user_literals",
		"cxx_variadic_templates"
	};
	return IsSortedMetadataEntry(value, entries,
		sizeof(entries) / sizeof(entries[0]));
}

bool IsSupportedBuiltinProbe(const std::string& value)
{
	if (hosted_builtin::FindIntegerIntrinsic(value) ||
		hosted_builtin::FindMemoryIntrinsic(value)) return true;
	static const char* const entries[] = {
		"__integer_pack",
		"__is_trivially_destructible",
		"__reference_binds_to_temporary",
		"__reference_constructs_from_temporary",
		"__remove_reference_t"
	};
	return IsSortedMetadataEntry(value, entries,
		sizeof(entries) / sizeof(entries[0]));
}

bool IsSupportedAttributeProbe(const std::string& value)
{
	static const char* const entries[] = {"__using_if_exists__"};
	return IsSortedMetadataEntry(value, entries,
		sizeof(entries) / sizeof(entries[0]));
}

}
