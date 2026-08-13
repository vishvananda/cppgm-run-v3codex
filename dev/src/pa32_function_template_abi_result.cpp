#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{
namespace
{

FunctionTemplateResultIdentityAtomKind ResultIdentityKind(std::uint64_t atom)
{
	return static_cast<FunctionTemplateResultIdentityAtomKind>(atom >> 56);
}

std::uint64_t ResultIdentityValue(std::uint64_t atom)
{
	return atom & 0x00ffffffffffffffULL;
}

FunctionTemplateAbiTypeId AppendResultType(Program* program,
	const FunctionTemplateAbiType& type)
{
	if (program->function_template_abi_types.size() >=
		kNoFunctionTemplateAbiType)
		throw std::runtime_error("too many function template ABI type nodes");
	const FunctionTemplateAbiTypeId result =
		static_cast<FunctionTemplateAbiTypeId>(
			program->function_template_abi_types.size());
	program->function_template_abi_types.push_back(type);
	return result;
}

void RollBackResultTypes(Program* program, std::size_t size)
{
	program->function_template_abi_types.erase(
		program->function_template_abi_types.begin() + size,
		program->function_template_abi_types.end());
}

FunctionTemplateAbiTypeId PublishParameterQualifiedResult(
	Program* program, const FunctionTemplatePattern& pattern,
	const std::vector<std::uint64_t>& atoms)
{
	if (pattern.shape_type == kNoType ||
		!program->types.IsFunction(pattern.shape_type) || atoms.size() < 3 ||
		ResultIdentityKind(atoms[0]) !=
			FUNCTION_TEMPLATE_RESULT_QUALIFIED_BEGIN ||
		ResultIdentityValue(atoms[0]) != 0 ||
		ResultIdentityKind(atoms[1]) != FUNCTION_TEMPLATE_RESULT_COMPONENT)
		return kNoFunctionTemplateAbiType;
	const NameId root_name = static_cast<NameId>(ResultIdentityValue(atoms[1]));
	std::size_t parameter = 0;
	while (parameter < pattern.parameters.size() &&
		pattern.parameters[parameter].name != root_name) ++parameter;
	if (parameter == pattern.parameters.size() ||
		parameter >= kNoTemplateParameter)
		return kNoFunctionTemplateAbiType;

	const std::size_t original_size = program->function_template_abi_types.size();
	FunctionTemplateAbiTypeId root = AppendResultType(program,
		FunctionTemplateAbiType(FUNCTION_TEMPLATE_ABI_TYPE_PARAMETER,
			kNoFunctionTemplateAbiType, 0, 0,
			static_cast<std::uint32_t>(parameter)));
	std::size_t atom = 2;
	if (atom < atoms.size() &&
		(ResultIdentityKind(atoms[atom]) ==
			FUNCTION_TEMPLATE_RESULT_DECLARATION ||
		 ResultIdentityKind(atoms[atom]) == FUNCTION_TEMPLATE_RESULT_ENTITY))
	{
		RollBackResultTypes(program, original_size);
		return kNoFunctionTemplateAbiType;
	}
	while (atom < atoms.size() &&
		ResultIdentityKind(atoms[atom]) !=
			FUNCTION_TEMPLATE_RESULT_QUALIFIED_END)
	{
		if (ResultIdentityKind(atoms[atom]) !=
				FUNCTION_TEMPLATE_RESULT_COMPONENT ||
			ResultIdentityValue(atoms[atom]) == 0)
		{
			RollBackResultTypes(program, original_size);
			return kNoFunctionTemplateAbiType;
		}
		const NameId name = static_cast<NameId>(ResultIdentityValue(atoms[atom++]));
		if (atom < atoms.size() &&
			(ResultIdentityKind(atoms[atom]) ==
				FUNCTION_TEMPLATE_RESULT_DECLARATION ||
			 ResultIdentityKind(atoms[atom]) ==
				FUNCTION_TEMPLATE_RESULT_ENTITY)) ++atom;
		if (atom < atoms.size() &&
			ResultIdentityKind(atoms[atom]) ==
				FUNCTION_TEMPLATE_RESULT_ARGUMENTS_BEGIN)
		{
			RollBackResultTypes(program, original_size);
			return kNoFunctionTemplateAbiType;
		}
		root = AppendResultType(program,
			FunctionTemplateAbiType(FUNCTION_TEMPLATE_ABI_TYPE_MEMBER, root, name));
	}
	if (atom + 1 != atoms.size() ||
		ResultIdentityKind(atoms[atom]) !=
			FUNCTION_TEMPLATE_RESULT_QUALIFIED_END)
	{
		RollBackResultTypes(program, original_size);
		return kNoFunctionTemplateAbiType;
	}

	struct Modifier
	{
		FunctionTemplateAbiTypeKind kind;
		std::uint64_t bound;
		std::uint32_t parameter;
		std::uint8_t cv;
	};
	std::vector<Modifier> modifiers;
	TypeId source = program->types.Get(pattern.shape_type).child;
	for (;;)
	{
		const TypeRecord& record = program->types.Get(source);
		Modifier modifier = { FUNCTION_TEMPLATE_ABI_TYPE_POINTER, 0,
			kNoTemplateParameter, 0 };
		if (record.kind == TYPE_QUALIFIED)
		{
			modifier.kind = FUNCTION_TEMPLATE_ABI_TYPE_QUALIFIED;
			modifier.cv = record.cv;
		}
		else if (record.kind == TYPE_POINTER)
			modifier.kind = FUNCTION_TEMPLATE_ABI_TYPE_POINTER;
		else if (record.kind == TYPE_LVALUE_REFERENCE)
			modifier.kind = FUNCTION_TEMPLATE_ABI_TYPE_LVALUE_REFERENCE;
		else if (record.kind == TYPE_RVALUE_REFERENCE)
			modifier.kind = FUNCTION_TEMPLATE_ABI_TYPE_RVALUE_REFERENCE;
		else if (record.kind == TYPE_ARRAY)
		{
			modifier.kind = FUNCTION_TEMPLATE_ABI_TYPE_ARRAY;
			modifier.bound = record.bound;
			modifier.parameter = record.dependent_bound_parameter;
		}
		else break;
		modifiers.push_back(modifier);
		source = record.child;
	}
	for (std::vector<Modifier>::const_reverse_iterator modifier =
		modifiers.rbegin(); modifier != modifiers.rend(); ++modifier)
		root = AppendResultType(program, FunctionTemplateAbiType(modifier->kind,
			root, 0, modifier->bound, modifier->parameter, modifier->cv));
	return root;
}

}

void SemanticAnalyzer::PublishFunctionTemplateResultAbiType(
	FunctionTemplatePattern* pattern)
{
	if (!pattern || pattern->expanded_result_identity ==
		kNoFunctionTemplateResultIdentity) return;
	std::vector<std::uint64_t> atoms;
	function_template_result_identities_.CopyAtoms(
		pattern->expanded_result_identity, &atoms);
	pattern->abi_result_type = PublishParameterQualifiedResult(
		program_, *pattern, atoms);
}

}
}
