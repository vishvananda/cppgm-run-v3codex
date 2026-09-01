#include "semantic/presentation/templates.h"

#include "semantic/semantic.h"
#include "semantic/model/graph.h"
#include "support/exceptions.h"

#include <cctype>
#include <sstream>

namespace cppgm
{
namespace semantic { namespace presentation
{
namespace
{

std::string TemplateArgumentTypeName(const std::string& source)
{
	std::string spelling = source;
	const char* prefixes[] = {"struct ", "class ", "union ", "enum "};
	for (std::size_t prefix = 0; prefix < 4; ++prefix)
	{
		const std::size_t length =
			std::char_traits<char>::length(prefixes[prefix]);
		if (spelling.compare(0, length, prefixes[prefix]) == 0)
		{
			spelling.erase(0, length);
			break;
		}
	}
	return spelling;
}

std::string RenderTemplateArgument(const semantic::Program& program,
	const semantic::TemplateArgument& argument, semantic::Stats* stats)
{
	if (argument.kind == TEMPLATE_ARGUMENT_TYPE ||
		argument.kind == TEMPLATE_ARGUMENT_TEMPLATE)
	{
		const TypeRecord& type = program.types.Get(argument.type);
		if (type.kind == TYPE_QUALIFIED)
		{
			std::string result = TemplateArgumentTypeName(
				program.RenderType(type.child));
			if ((type.cv & CV_CONST) != 0) result += " const";
			if ((type.cv & CV_VOLATILE) != 0) result += " volatile";
			return result;
		}
		if (type.kind == TYPE_FUNCTION)
		{
			std::string result = TemplateArgumentTypeName(
				program.RenderType(type.child)) + "(";
			const TypeId* parameters = program.types.Parameters(argument.type);
			for (std::size_t i = 0; i < type.parameter_count; ++i)
			{
				if (i != 0) result += ", ";
				result += TemplateArgumentTypeName(
					program.RenderType(parameters[i]));
			}
			if (type.variadic)
			{
				if (type.parameter_count != 0) result += ", ";
				result += "...";
			}
			return result + ')';
		}
		return TemplateArgumentTypeName(program.RenderType(argument.type));
	}
	if (argument.IsDependent())
	{
		std::ostringstream result;
		result << "dependent(" << TemplateArgumentTypeName(
			program.RenderType(argument.type)) << ", "
			<< argument.dependent_parameter << ')';
		return result.str();
	}
	if (argument.value_binding != kNoBinding)
	{
		if (argument.value_binding >= program.bindings.size())
			ThrowInternalCompilerError("template argument binding is invalid");
		const BindingRecord& binding = program.bindings[argument.value_binding];
		std::string result = semantic::RenderBindingPresentation(
			program, binding, stats);
		const TypeRecord& type = program.types.Get(
			program.types.RemoveTopCv(argument.type));
		if (type.kind == TYPE_POINTER) result.insert(result.begin(), '&');
		return result;
	}
	const TypeId type = program.types.RemoveTopCv(argument.type);
	const TypeRecord& record = program.types.Get(type);
	if (record.kind == TYPE_FUNDAMENTAL &&
		record.fundamental == FUND_BOOL)
		return argument.value == 0 ? "false" : "true";
	if (record.kind == TYPE_NAMED && record.entity != kNoEntity &&
		program.entities[record.entity].flavor == NAMED_ENUM)
		return "(" + program.RenderEntityEmissionName(record.entity) +
			")" + std::to_string(argument.value);
	return std::to_string(argument.value);
}

}

std::string RenderClassTemplateSpecializationName(
	const semantic::Program& program, semantic::NameId primary,
	const semantic::TemplateArgument* arguments, std::size_t argument_count,
	semantic::Stats* stats)
{
	std::string source = program.names.Get(primary) + "<";
	for (std::size_t i = 0; i < argument_count; ++i)
	{
		if (i != 0) source += ", ";
		source += RenderTemplateArgument(program, arguments[i], stats);
	}
	source += '>';
	std::string result;
	result.reserve(source.size());
	for (std::size_t i = 0; i < source.size(); ++i)
	{
		const unsigned char character =
			static_cast<unsigned char>(source[i]);
		result += std::isalnum(character) || character == '_' ?
			static_cast<char>(character) : '_';
	}
	if (stats)
	{
		++stats->presentation_renders[
			SEMANTIC_PRESENTATION_CLASS_SPECIALIZATION];
		stats->presentation_render_components[
			SEMANTIC_PRESENTATION_CLASS_SPECIALIZATION] += argument_count + 1;
		stats->presentation_render_bytes[
			SEMANTIC_PRESENTATION_CLASS_SPECIALIZATION] += result.size();
	}
	return result;
}

} }
}
