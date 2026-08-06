#include "pa15_lowering_abi.h"

#include "abi_mangle.h"

#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa15_lowering_abi
{

namespace
{

abi_mangle::AbiType MakeAbiType(const pa11::Program& program,
	pa11::TypeId type)
{
	using namespace abi_mangle;
	using namespace pa11;
	std::vector<AbiTypeModifier> modifiers;
	const TypeRecord* record = &program.types.Get(type);
	while (record->kind == TYPE_QUALIFIED || record->kind == TYPE_POINTER ||
		record->kind == TYPE_LVALUE_REFERENCE ||
		record->kind == TYPE_RVALUE_REFERENCE || record->kind == TYPE_ARRAY)
	{
		AbiTypeModifier modifier;
		if (record->kind == TYPE_QUALIFIED)
		{
			modifier.kind = ABI_TYPE_CV;
			modifier.is_const = (record->cv & CV_CONST) != 0;
			modifier.is_volatile = (record->cv & CV_VOLATILE) != 0;
		}
		else if (record->kind == TYPE_ARRAY)
		{
			modifier.kind = ABI_TYPE_ARRAY;
			modifier.array_bound.kind = ABI_ARRAY_BOUND_VALUE;
			modifier.array_bound.value = std::to_string(record->bound);
		}
		else modifier.kind = record->kind == TYPE_POINTER ? ABI_TYPE_POINTER :
			record->kind == TYPE_LVALUE_REFERENCE ? ABI_TYPE_LVALUE_REFERENCE :
			ABI_TYPE_RVALUE_REFERENCE;
		modifiers.push_back(modifier);
		type = record->child;
		record = &program.types.Get(type);
	}
	AbiType result;
	result.modifiers.swap(modifiers);
	if (record->kind == TYPE_FUNCTION)
	{
		result.kind = ABI_TYPE_FUNCTION;
		result.types.push_back(MakeAbiType(program, record->child));
		const TypeId* parameters = program.types.Parameters(type);
		for (std::size_t i = 0; i < record->parameter_count; ++i)
			result.types.push_back(MakeAbiType(program, parameters[i]));
		result.variadic = record->variadic;
		return result;
	}
	if (record->kind == TYPE_NAMED)
	{
		result.kind = ABI_TYPE_NAMED;
		result.name = program.names.Get(program.entities[record->entity].name);
		return result;
	}
	if (record->kind != TYPE_FUNDAMENTAL)
		throw std::runtime_error("unsupported ABI type in PA15");
	result.kind = ABI_TYPE_BUILTIN;
	switch (record->fundamental)
	{
	case FUND_VOID: result.name = "void"; break;
	case FUND_BOOL: result.name = "bool"; break;
	case FUND_CHAR: result.name = "char"; break;
	case FUND_SIGNED_CHAR: result.name = "schar"; break;
	case FUND_UNSIGNED_CHAR: result.name = "uchar"; break;
	case FUND_SHORT_INT: result.name = "short"; break;
	case FUND_UNSIGNED_SHORT_INT: result.name = "ushort"; break;
	case FUND_INT: result.name = "int"; break;
	case FUND_UNSIGNED_INT: result.name = "uint"; break;
	case FUND_LONG_INT: result.name = "long"; break;
	case FUND_UNSIGNED_LONG_INT: result.name = "ulong"; break;
	case FUND_LONG_LONG_INT: result.name = "longlong"; break;
	case FUND_UNSIGNED_LONG_LONG_INT: result.name = "ulonglong"; break;
	case FUND_FLOAT: result.name = "float"; break;
	case FUND_DOUBLE: result.name = "double"; break;
	case FUND_LONG_DOUBLE: result.name = "longdouble"; break;
	case FUND_WCHAR_T: result.name = "wchar"; break;
	case FUND_CHAR16_T: result.name = "char16"; break;
	case FUND_CHAR32_T: result.name = "char32"; break;
	case FUND_NULLPTR_T: result.name = "ulong"; break;
	}
	return result;
}

}

std::string MangleFunction(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node)
{
	using namespace abi_mangle;
	using namespace pa11;
	const std::string qualified = program.names.Get(node.text);
	if (qualified == "main") return std::string();
	const BindingRecord& binding = program.bindings[node.binding];
	if (binding.language_linkage == LANGUAGE_LINKAGE_C &&
		binding.storage_class != STORAGE_CLASS_STATIC)
		return program.names.Get(binding.name);
	AbiFactFile file;
	file.cases.push_back(AbiFactCase());
	AbiFactRecord target;
	target.set_kind(ABI_FACT_RECORD_TARGET);
	target.target.kind = ABI_TARGET_FACT_FUNCTION;
	target.target.internal_linkage = binding.storage_class == STORAGE_CLASS_STATIC;
	target.target.function.kind = ABI_FUNCTION_TARGET_PATH;
	target.target.function.qualified_name = qualified;
	file.cases[0].records.push_back(target);
	const TypeRecord& function_type = program.types.Get(node.type);
	const TypeId* parameters = program.types.Parameters(node.type);
	const bool member = binding.member_owner != kNoEntity &&
		!binding.static_member_function;
	if (member)
	{
		const TypeRecord& declared_type = program.types.Get(binding.type);
		AbiFactRecord qualifier;
		qualifier.set_kind(ABI_FACT_RECORD_FUNCTION);
		qualifier.function.kind = ABI_FUNCTION_RECORD_QUALIFIER;
		if ((declared_type.cv & CV_CONST) != 0)
			qualifier.function.qualifiers.push_back(
				ABI_FUNCTION_QUALIFIER_CONST);
		if ((declared_type.cv & CV_VOLATILE) != 0)
			qualifier.function.qualifiers.push_back(
				ABI_FUNCTION_QUALIFIER_VOLATILE);
		if (!qualifier.function.qualifiers.empty())
			file.cases[0].records.push_back(qualifier);
	}
	if (binding.constructor)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_TERMINAL;
		terminal.function.terminal = "constructor-complete";
		file.cases[0].records.push_back(terminal);
	}
	const std::size_t first_parameter = member ? 1 : 0;
	for (std::size_t i = first_parameter;
		i < function_type.parameter_count; ++i)
	{
		AbiFactRecord parameter;
		parameter.set_kind(ABI_FACT_RECORD_FUNCTION);
		parameter.function.kind = ABI_FUNCTION_RECORD_PARAMETER;
		parameter.function.type = MakeAbiType(program, parameters[i]);
		file.cases[0].records.push_back(parameter);
	}
	if (function_type.variadic)
	{
		AbiFactRecord variadic;
		variadic.set_kind(ABI_FACT_RECORD_FUNCTION);
		variadic.function.kind = ABI_FUNCTION_RECORD_VARIADIC;
		file.cases[0].records.push_back(variadic);
	}
	std::string result = mangle_fact_file(file);
	if (!result.empty() && result[result.size() - 1] == '\n')
		result.resize(result.size() - 1);
	return result;
}

std::string MangleVariable(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node)
{
	using namespace abi_mangle;
	using namespace pa11;
	const BindingRecord& binding = program.bindings[node.binding];
	if (binding.language_linkage == LANGUAGE_LINKAGE_C &&
		binding.storage_class != STORAGE_CLASS_STATIC)
		return program.names.Get(binding.name);
	AbiFactFile file;
	file.cases.push_back(AbiFactCase());
	AbiFactRecord target;
	target.set_kind(ABI_FACT_RECORD_TARGET);
	target.target.kind = ABI_TARGET_FACT_VARIABLE;
	target.target.internal_linkage = binding.storage_class == STORAGE_CLASS_STATIC;
	target.target.qualified_name = program.names.Get(
		binding.qualified_name != 0 ? binding.qualified_name : node.text);
	file.cases[0].records.push_back(target);
	std::string result = mangle_fact_file(file);
	if (!result.empty() && result[result.size() - 1] == '\n')
		result.resize(result.size() - 1);
	return result;
}

}
}
