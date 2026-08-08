#include "pa15_lowering_abi.h"

#include "abi_mangle.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa15_lowering_abi
{

namespace
{

using namespace pa11;

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
	case FUND_NULLPTR_T: result.name = "nullptr"; break;
	}
	return result;
}

std::string OperatorTerminal(OperatorKind kind, bool member,
	std::size_t parameter_count)
{
	switch (kind)
	{
	case OPERATOR_PLUS: return "plus";
	case OPERATOR_MINUS: return "minus";
	case OPERATOR_STAR:
		return (member ? parameter_count == 0 : parameter_count == 1) ?
			"deref" : "multiply";
	case OPERATOR_AMPERSAND:
		return (member ? parameter_count == 0 : parameter_count == 1) ?
			"address-of" : "bit-and";
	case OPERATOR_DIVIDE: return "divide";
	case OPERATOR_REMAINDER: return "remainder";
	case OPERATOR_BIT_OR: return "bit-or";
	case OPERATOR_BIT_XOR: return "bit-xor";
	case OPERATOR_ASSIGN: return "assign";
	case OPERATOR_PLUS_ASSIGN: return "plus-assign";
	case OPERATOR_MINUS_ASSIGN: return "minus-assign";
	case OPERATOR_MULTIPLY_ASSIGN: return "multiply-assign";
	case OPERATOR_DIVIDE_ASSIGN: return "divide-assign";
	case OPERATOR_REMAINDER_ASSIGN: return "remainder-assign";
	case OPERATOR_AND_ASSIGN: return "and-assign";
	case OPERATOR_OR_ASSIGN: return "or-assign";
	case OPERATOR_XOR_ASSIGN: return "xor-assign";
	case OPERATOR_LEFT_SHIFT: return "left-shift";
	case OPERATOR_RIGHT_SHIFT: return "right-shift";
	case OPERATOR_LEFT_SHIFT_ASSIGN: return "left-shift-assign";
	case OPERATOR_RIGHT_SHIFT_ASSIGN: return "right-shift-assign";
	case OPERATOR_EQUAL: return "equal";
	case OPERATOR_NOT_EQUAL: return "not-equal";
	case OPERATOR_LESS: return "less";
	case OPERATOR_GREATER: return "greater";
	case OPERATOR_LESS_EQUAL: return "less-equal";
	case OPERATOR_GREATER_EQUAL: return "greater-equal";
	case OPERATOR_LOGICAL_NOT: return "logical-not";
	case OPERATOR_LOGICAL_AND: return "logical-and";
	case OPERATOR_LOGICAL_OR: return "logical-or";
	case OPERATOR_INCREMENT: return "increment";
	case OPERATOR_DECREMENT: return "decrement";
	case OPERATOR_COMMA: return "comma";
	case OPERATOR_MEMBER_POINTER: return "member-pointer";
	case OPERATOR_ARROW: return "arrow";
	case OPERATOR_CALL: return "call";
	case OPERATOR_INDEX: return "index";
	case OPERATOR_NEW: return "new";
	case OPERATOR_NEW_ARRAY: return "new-array";
	case OPERATOR_DELETE: return "delete";
	case OPERATOR_DELETE_ARRAY: return "delete-array";
	case OPERATOR_NONE:
	case OPERATOR_LITERAL: return std::string();
	}
	throw std::logic_error("invalid typed operator terminal");
}

}

void ApplyBuiltinSymbolMetadata(pa15_lowir_detail::Symbol* symbol,
	pa11::BuiltinFunctionKind kind)
{
	using namespace pa11;
	using pa15_lowir_detail::Symbol;
	switch (kind)
	{
	case BUILTIN_FUNCTION_STRLEN: symbol->effects = Symbol::EFFECTS_READONLY; break;
	case BUILTIN_FUNCTION_UNREACHABLE:
		symbol->effects = Symbol::EFFECTS_READNONE; symbol->noreturn = true; break;
	case BUILTIN_FUNCTION_MEMCPY:
	case BUILTIN_FUNCTION_MEMMOVE: symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_OPERATOR_NEW:
	case BUILTIN_FUNCTION_OPERATOR_DELETE:
	case BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY:
	case BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY:
		symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_NONE: break;
	}
}

void ApplyBuiltinParameterMetadata(pa15_lowir_detail::Parameter* parameter,
	pa11::BuiltinFunctionKind kind, std::size_t index)
{
	using namespace pa11;
	using pa15_lowir_detail::Parameter;
	if (kind == BUILTIN_FUNCTION_STRLEN && index == 0)
	{
		parameter->capture = Parameter::CAPTURE_NOCAPTURE;
		parameter->access = Parameter::ACCESS_READ;
	}
	else if (kind == BUILTIN_FUNCTION_MEMCPY && index < 2)
	{
		parameter->capture = Parameter::CAPTURE_NOCAPTURE;
		parameter->access = index == 0 ? Parameter::ACCESS_WRITE :
			Parameter::ACCESS_READ;
		parameter->alias = Parameter::ALIAS_NOALIAS;
	}
	else if (kind == BUILTIN_FUNCTION_MEMMOVE && index < 2)
	{
		parameter->capture = Parameter::CAPTURE_NOCAPTURE;
		parameter->access = index == 0 ? Parameter::ACCESS_READWRITE :
			Parameter::ACCESS_READ;
	}
}

std::string MangleFunction(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node)
{
	using namespace abi_mangle;
	using namespace pa11;
	const BindingRecord& binding = program.bindings[node.binding];
	std::string qualified = program.names.Get(
		binding.qualified_name != 0 ? binding.qualified_name : node.text);
	if (binding.conversion_function)
	{
		const std::size_t terminal = qualified.find("::operator");
		if (terminal != std::string::npos)
			qualified.erase(terminal + std::string("::operator").size());
	}
	if (qualified == "main") return std::string();
	if (binding.builtin_function != BUILTIN_FUNCTION_NONE)
	{
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_NEW)
			return "cppgm_builtin_operator_new";
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_DELETE)
			return "cppgm_builtin_operator_delete";
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY)
			return "cppgm_builtin_operator_new_array";
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY)
			return "cppgm_builtin_operator_delete_array";
		return "cppgm_builtin_" + program.names.Get(binding.name).substr(10);
	}
	if (binding.language_linkage == LANGUAGE_LINKAGE_C &&
		binding.storage_class != STORAGE_CLASS_STATIC)
		return program.names.Get(binding.name);
	AbiFactFile file;
	file.cases.push_back(AbiFactCase());
	AbiFactRecord target;
	target.set_kind(ABI_FACT_RECORD_TARGET);
	target.target.kind = ABI_TARGET_FACT_FUNCTION;
	target.target.internal_linkage =
		binding.storage_class == STORAGE_CLASS_STATIC &&
		!binding.unnamed_namespace_linkage;
	target.target.function.kind = ABI_FUNCTION_TARGET_PATH;
	target.target.function.qualified_name = qualified;
	file.cases[0].records.push_back(target);
	const TypeRecord& function_type = program.types.Get(node.type);
	const TypeId* parameters = program.types.Parameters(node.type);
	if (binding.template_argument_count != 0)
	{
		const std::size_t first = binding.template_argument_begin;
		const std::size_t count = binding.template_argument_count;
		if (first > program.binding_template_arguments.size() ||
			count > program.binding_template_arguments.size() - first)
			throw std::logic_error(
				"function template argument range is invalid during mangling");
		for (std::size_t i = 0; i < count; ++i)
		{
			const std::string argument_id =
				"__cppgm_function_template_argument_" + std::to_string(i);
			AbiFactRecord definition;
			definition.set_kind(ABI_FACT_RECORD_DEFINITION);
			definition.definition.id = argument_id;
			definition.definition.set_kind(ABI_DEFINITION_TEMPLATE_ARGUMENT);
			definition.definition.template_argument.kind =
				ABI_TEMPLATE_ARGUMENT_TYPE;
			definition.definition.template_argument.type = MakeAbiType(program,
				program.binding_template_arguments[first + i]);
			file.cases[0].records.push_back(definition);
			AbiFactRecord argument;
			argument.set_kind(ABI_FACT_RECORD_FUNCTION);
			argument.function.kind =
				ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT;
			argument.function.argument_refs.push_back(argument_id);
			file.cases[0].records.push_back(argument);
		}
		AbiFactRecord result;
		result.set_kind(ABI_FACT_RECORD_FUNCTION);
		result.function.kind = ABI_FUNCTION_RECORD_RESULT;
		result.function.type = MakeAbiType(program, function_type.child);
		file.cases[0].records.push_back(result);
	}
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
		if (declared_type.ref_qualifier == FUNCTION_REF_LVALUE)
			qualifier.function.qualifiers.push_back(
				ABI_FUNCTION_QUALIFIER_LVALUE_REFERENCE);
		else if (declared_type.ref_qualifier == FUNCTION_REF_RVALUE)
			qualifier.function.qualifiers.push_back(
				ABI_FUNCTION_QUALIFIER_RVALUE_REFERENCE);
		if (!qualifier.function.qualifiers.empty())
			file.cases[0].records.push_back(qualifier);
	}
	const std::string operator_terminal =
		OperatorTerminal(binding.operator_kind, member,
			program.types.Get(binding.type).parameter_count);
	if (binding.operator_kind == OPERATOR_LITERAL ||
		!operator_terminal.empty())
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_OPERATOR_TERMINAL;
		if (binding.operator_kind == OPERATOR_LITERAL)
		{
			terminal.function.terminal = "literal";
			terminal.function.literal_suffix =
				program.names.Get(binding.operator_literal_suffix);
		}
		else terminal.function.terminal = operator_terminal;
		file.cases[0].records.push_back(terminal);
	}
	else if (binding.conversion_function)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_CONVERSION_TERMINAL;
		terminal.function.type = MakeAbiType(program, binding.conversion_target);
		file.cases[0].records.push_back(terminal);
	}
	else if (binding.constructor)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_TERMINAL;
		terminal.function.terminal = binding.constructor_base_entry ?
			"constructor-base" : "constructor-complete";
		file.cases[0].records.push_back(terminal);
	}
	else if (binding.destructor)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_TERMINAL;
		terminal.function.terminal = binding.destructor_base_entry ?
			"destructor-base" : "destructor-complete";
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
	target.target.internal_linkage =
		binding.storage_class == STORAGE_CLASS_STATIC &&
		binding.member_owner == kNoEntity &&
		!binding.unnamed_namespace_linkage;
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
