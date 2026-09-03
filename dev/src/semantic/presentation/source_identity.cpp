#include "semantic/presentation/source_identity.h"

#include "semantic/presentation/lambdas.h"
#include "support/exceptions.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace cppgm
{
namespace semantic { namespace presentation
{
namespace
{


std::string RenderTypeAt(const Program& program, TypeId type,
	std::size_t depth);

std::string ScopePrefix(const Program& program, ScopeId owner)
{
	std::vector<NameId> components;
	while (owner != kNoScope && owner != program.GlobalScope())
	{
		const ScopeKind kind = program.KindOfScope(owner);
		if ((kind == SCOPE_NAMESPACE && !program.IsInlineNamespace(owner)) ||
			kind == SCOPE_CLASS || kind == SCOPE_ENUM)
		{
			const NameId name = program.NameOfScope(owner);
			if (name != 0) components.push_back(name);
		}
		owner = program.ParentScope(owner);
	}
	std::reverse(components.begin(), components.end());
	std::string result;
	for (std::size_t i = 0; i < components.size(); ++i)
	{
		if (!result.empty()) result += "::";
		result += program.names.Get(components[i]);
	}
	return result;
}

std::string RenderTemplateArgument(const Program& program,
	const TemplateArgument& argument, std::size_t depth)
{
	if (argument.kind == TEMPLATE_ARGUMENT_TYPE ||
		argument.kind == TEMPLATE_ARGUMENT_TEMPLATE)
		return RenderTypeAt(program, argument.type, depth + 1);
	if (argument.IsDependent())
		return "dependent(" + std::to_string(argument.dependent_parameter) + ')';
	if (argument.value_binding != kNoBinding)
	{
		if (argument.value_binding >= program.bindings.size())
			ThrowInternalCompilerError("source identity argument binding is invalid");
		const BindingRecord& binding =
			program.bindings[argument.value_binding];
		std::string result = ScopePrefix(program, binding.owner);
		if (!result.empty()) result += "::";
		result += program.names.Get(binding.name);
		const TypeRecord& value_type = program.types.Get(
			program.types.RemoveTopCv(argument.type));
		if (value_type.kind == TYPE_POINTER) result.insert(result.begin(), '&');
		return result;
	}
	const TypeRecord& value_type = program.types.Get(
		program.types.RemoveTopCv(argument.type));
	if (value_type.kind == TYPE_FUNDAMENTAL &&
		value_type.fundamental == FUND_BOOL)
		return argument.value == 0 ? "false" : "true";
	return std::to_string(argument.value);
}

std::string RenderEntityAt(const Program& program, EntityId entity,
	std::size_t depth)
{
	if (entity == kNoEntity || entity >= program.entities.size())
		ThrowInternalCompilerError("source identity entity is invalid");
	if (depth > program.entities.size() + program.types.Size())
		ThrowInternalCompilerError("source identity entity graph is cyclic");
	const EntityRecord& record = program.entities[entity];
	if (record.lambda_closure)
		return semantic::presentation::RenderLambdaSourceIdentityName(
			program, entity);
	std::string result;
	if (record.enclosing_class != kNoEntity)
		result = RenderEntityAt(program, record.enclosing_class, depth + 1);
	else result = ScopePrefix(program, record.owner);
	if (!result.empty()) result += "::";
	result += program.names.Get(
		record.identity_name == 0 ? record.emission_name : record.identity_name);
	if (record.template_argument_begin != kNoBinding)
	{
		const std::size_t first = record.template_argument_begin;
		const std::size_t count = record.template_argument_count;
		if (first > program.canonical_template_arguments.size() ||
			count > program.canonical_template_arguments.size() - first)
			ThrowInternalCompilerError(
				"source identity template arguments are invalid");
		result += '<';
		for (std::size_t i = 0; i < count; ++i)
		{
			if (i != 0) result += ", ";
			result += RenderTemplateArgument(program,
				program.canonical_template_arguments[first + i], depth + 1);
		}
		result += '>';
	}
	return result;
}

std::string CvSuffix(std::uint8_t cv)
{
	std::string result;
	if ((cv & CV_CONST) != 0) result += "const";
	if ((cv & CV_VOLATILE) != 0)
	{
		if (!result.empty()) result += ' ';
		result += "volatile";
	}
	if ((cv & CV_ATOMIC) != 0)
	{
		if (!result.empty()) result += ' ';
		result += "_Atomic";
	}
	return result;
}

std::string StripTypePrefix(std::string spelling)
{
	const char* prefixes[] = {"struct ", "class ", "union ", "enum "};
	for (std::size_t i = 0; i < 4; ++i)
	{
		const std::size_t length = std::char_traits<char>::length(prefixes[i]);
		if (spelling.compare(0, length, prefixes[i]) == 0)
		{
			spelling.erase(0, length);
			break;
		}
	}
	return spelling;
}

// Renders a type in declarator form so that pointer, reference, array,
// function, and member-pointer operators compose in source order and
// cv-qualifiers attach to the operator they qualify.
std::string RenderDeclaratorType(const Program& program, TypeId type,
	std::string declarator, std::size_t depth, std::uint8_t pointer_cv)
{
	if (type == kNoType || type >= program.types.Size())
		ThrowInternalCompilerError("source identity type is invalid");
	if (depth > program.entities.size() + program.types.Size())
		ThrowInternalCompilerError("source identity type graph is cyclic");
	const TypeRecord& record = program.types.Get(type);
	switch (record.kind)
	{
	case TYPE_QUALIFIED:
	{
		const TypeRecord& child = program.types.Get(record.child);
		if (child.kind == TYPE_POINTER || child.kind == TYPE_BLOCK_POINTER ||
			child.kind == TYPE_MEMBER_POINTER)
			return RenderDeclaratorType(program, record.child, declarator,
				depth + 1, static_cast<std::uint8_t>(pointer_cv | record.cv));
		std::string prefix = CvSuffix(record.cv);
		std::string result = RenderDeclaratorType(program, record.child,
			declarator, depth + 1, pointer_cv);
		return prefix.empty() ? result : prefix + ' ' + result;
	}
	case TYPE_POINTER:
	case TYPE_BLOCK_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
	{
		std::string operation = record.kind == TYPE_POINTER ? "*" :
			record.kind == TYPE_BLOCK_POINTER ? "^" :
			record.kind == TYPE_LVALUE_REFERENCE ? "&" : "&&";
		if (record.kind == TYPE_POINTER || record.kind == TYPE_BLOCK_POINTER)
			operation += CvSuffix(pointer_cv);
		if (!declarator.empty() && !operation.empty() &&
			std::isalnum(static_cast<unsigned char>(operation.back())))
			operation += ' ';
		declarator = operation + declarator;
		return RenderDeclaratorType(program, record.child, declarator,
			depth + 1, 0);
	}
	case TYPE_ARRAY:
		declarator += '[' + (record.dependent_bound_parameter ==
			kNoTemplateParameter ? std::to_string(record.bound) :
			std::string("dependent")) + ']';
		return RenderDeclaratorType(program, record.child, declarator,
			depth + 1, 0);
	case TYPE_FUNCTION:
	{
		if (!declarator.empty()) declarator = '(' + declarator + ')';
		declarator += '(';
		const TypeId* parameters = program.types.Parameters(type);
		for (std::size_t i = 0; i < record.parameter_count; ++i)
		{
			if (i != 0) declarator += ", ";
			declarator += RenderDeclaratorType(program, parameters[i],
				std::string(), depth + 1, 0);
		}
		if (record.variadic)
		{
			if (record.parameter_count != 0) declarator += ", ";
			declarator += "...";
		}
		declarator += ')';
		return RenderDeclaratorType(program, record.child, declarator,
			depth + 1, 0);
	}
	case TYPE_MEMBER_POINTER:
	{
		std::string operation = RenderDeclaratorType(program,
			static_cast<TypeId>(record.bound), std::string(), depth + 1, 0) +
			"::*" + CvSuffix(pointer_cv);
		declarator = operation + declarator;
		return RenderDeclaratorType(program, record.child, declarator,
			depth + 1, 0);
	}
	case TYPE_NAMED:
	{
		const std::string base = RenderEntityAt(
			program, record.entity, depth + 1);
		return declarator.empty() ? base : base +
			(declarator[0] == '[' ? std::string() : std::string(" ")) +
			declarator;
	}
	default:
	{
		const std::string base = StripTypePrefix(program.RenderType(type));
		return declarator.empty() ? base : base +
			(declarator[0] == '[' ? std::string() : std::string(" ")) +
			declarator;
	}
	}
}

std::string RenderTypeAt(const Program& program, TypeId type,
	std::size_t depth)
{
	return RenderDeclaratorType(program, type, std::string(), depth, 0);
}

}

std::string RenderType(const Program& program, TypeId type)
{
	return RenderTypeAt(program, type, 0);
}

std::string RenderEntity(const Program& program, EntityId entity)
{
	return RenderEntityAt(program, entity, 0);
}

std::string RenderFunction(const Program& program, BindingId binding,
	TypeId type, const std::vector<TemplateBinding>& substitutions)
{
	if (binding == kNoBinding || binding >= program.bindings.size() ||
		type == kNoType || !program.types.IsFunction(type))
		ThrowInternalCompilerError("source function identity is invalid");
	const BindingRecord& function = program.bindings[binding];
	const TypeRecord& callable = program.types.Get(type);
	std::string result;
	if (!function.constructor && !function.destructor)
		result = RenderType(program, callable.child) + ' ';
	if (function.member_owner != kNoEntity)
		result += RenderEntity(program, function.member_owner) + "::";
	else
	{
		const std::string prefix = ScopePrefix(program, function.owner);
		if (!prefix.empty()) result += prefix + "::";
	}
	result += program.names.Get(function.name) + '(';
	const TypeId* parameters = program.types.Parameters(type);
	for (std::size_t i = 0; i < callable.parameter_count; ++i)
	{
		if (i != 0) result += ", ";
		result += RenderType(program, parameters[i]);
	}
	if (callable.variadic)
	{
		if (callable.parameter_count != 0) result += ", ";
		result += "...";
	}
	result += ')';
	if (!substitutions.empty())
	{
		result += " [";
		for (std::size_t i = 0; i < substitutions.size(); ++i)
		{
			if (i != 0) result += "; ";
			result += program.names.Get(substitutions[i].name) + " = " +
				RenderTemplateArgument(program, substitutions[i].argument, 0);
		}
		result += ']';
	}
	return result;
}

} }
}
