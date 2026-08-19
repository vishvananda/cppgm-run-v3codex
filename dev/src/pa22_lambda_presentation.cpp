#include "pa22_lambda_presentation.h"

#include "pa12_semantic_model.h"

#include <cctype>
#include <sstream>
#include <stdexcept>

namespace cppgm
{
namespace pa22_lambda_presentation
{
namespace
{

std::string SanitizeLambdaIdentity(const std::string& source)
{
	std::string result;
	result.reserve(source.size());
	for (std::size_t i = 0; i < source.size(); ++i)
	{
		const unsigned char value = static_cast<unsigned char>(source[i]);
		result += std::isalnum(value) || value == '_' ?
			static_cast<char>(value) : '_';
	}
	return result;
}

std::string LambdaTemplateArgumentIdentity(const pa11::Program& program,
	const pa11::TemplateArgument& argument, SemanticAnalysisStats* stats)
{
	using namespace pa11;
	if ((argument.kind == TEMPLATE_ARGUMENT_TYPE ||
		 argument.kind == TEMPLATE_ARGUMENT_TEMPLATE) &&
		argument.type != kNoType)
		return SanitizeLambdaIdentity(program.RenderType(argument.type));
	if (argument.value_binding != kNoBinding &&
		argument.value_binding < program.bindings.size())
	{
		const BindingRecord& binding =
			program.bindings[argument.value_binding];
		return SanitizeLambdaIdentity(
			pa12_semantic_detail::RenderBindingPresentation(
				program, binding, stats));
	}
	return std::to_string(argument.value);
}

std::string LambdaContextIdentity(const pa11::Program& program,
	pa11::BindingId context, SemanticAnalysisStats* stats)
{
	using namespace pa11;
	if (context == kNoBinding || context >= program.bindings.size())
		throw std::logic_error("lambda context binding is invalid");
	const BindingRecord& binding = program.bindings[context];
	// The enclosing closure scope already owns a nested lambda's identity.
	// Repeating the parent's fully rendered synthetic name in the child leaf
	// doubles internal-name size at every nesting level.
	if (binding.member_owner != kNoEntity &&
		binding.member_owner < program.entities.size() &&
		program.entities[binding.member_owner].lambda_closure)
		return "nested";
	std::string result = SanitizeLambdaIdentity(
		pa12_semantic_detail::RenderBindingPresentation(
			program, binding, stats));
	const std::size_t first = binding.template_argument_begin;
	const std::size_t count = binding.template_argument_count;
	if (count != 0 &&
		(first > program.canonical_template_arguments.size() ||
		 count > program.canonical_template_arguments.size() - first))
		throw std::logic_error(
			"lambda context template arguments are invalid");
	for (std::size_t i = 0; i < count; ++i)
	{
		result += "__";
		result += LambdaTemplateArgumentIdentity(program,
			program.canonical_template_arguments[first + i], stats);
	}
	return result;
}

}

std::string RenderLambdaIdentityComponent(const pa11::Program& program,
	pa11::BindingId context, std::size_t token_first,
	std::size_t token_last, std::uint32_t ordinal,
	SemanticAnalysisStats* stats)
{
	std::ostringstream result;
	result << "__lambda_" << LambdaContextIdentity(program, context, stats)
		<< "_t" << token_first << '_' << token_last;
	if (ordinal != 0) result << "_n" << ordinal;
	return result.str();
}

}
}
