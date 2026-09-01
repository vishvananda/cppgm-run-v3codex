#include "semantic/presentation/lambdas.h"

#include "semantic/model/graph.h"
#include "semantic/semantic.h"
#include "support/exceptions.h"

#include <cctype>

namespace cppgm
{
namespace semantic { namespace presentation
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

std::string LambdaTemplateArgumentIdentity(const semantic::Program& program,
	const semantic::TemplateArgument& argument, semantic::Stats* stats)
{
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
			semantic::RenderBindingPresentation(
				program, binding, stats));
	}
	return std::to_string(argument.value);
}

std::string LambdaContextIdentity(const semantic::Program& program,
	semantic::BindingId context, semantic::Stats* stats)
{
	if (context == kNoBinding || context >= program.bindings.size())
		ThrowInternalCompilerError("lambda context binding is invalid");
	const BindingRecord& binding = program.bindings[context];
	// The enclosing closure scope already owns a nested lambda's identity.
	// Repeating the parent's fully rendered synthetic name in the child leaf
	// doubles internal-name size at every nesting level.
	if (binding.member_owner != kNoEntity &&
		binding.member_owner < program.entities.size() &&
		program.entities[binding.member_owner].lambda_closure)
		return "nested";
	std::string result = SanitizeLambdaIdentity(
		semantic::RenderBindingPresentation(
			program, binding, stats));
	const std::size_t first = binding.template_argument_begin;
	const std::size_t count = binding.template_argument_count;
	if (count != 0 &&
		(first > program.canonical_template_arguments.size() ||
		 count > program.canonical_template_arguments.size() - first))
		ThrowInternalCompilerError(
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

std::string RenderLambdaIdentityComponent(const semantic::Program& program,
	semantic::BindingId context, std::size_t token_first,
	std::size_t token_last, std::uint32_t ordinal,
	semantic::Stats* stats)
{
	const std::string context_name =
		LambdaContextIdentity(program, context, stats);
	std::string result;
	result.reserve(context_name.size() + 48);
	result = "__lambda_";
	result += context_name;
	result += "_t";
	result += std::to_string(token_first);
	result += '_';
	result += std::to_string(token_last);
	if (ordinal != 0)
	{
		result += "_n";
		result += std::to_string(ordinal);
	}
	return result;
}

namespace
{

const semantic::EntityRecord& LambdaEntity(const semantic::Program& program,
	semantic::EntityId entity)
{
	if (entity >= program.entities.size() ||
		!program.entities[entity].lambda_closure)
		ThrowInternalCompilerError("lambda presentation entity is invalid");
	const semantic::EntityRecord& record = program.entities[entity];
	if (record.emission_name_form != semantic::ENTITY_EMISSION_LAMBDA)
		ThrowInternalCompilerError("lambda presentation form is invalid");
	return record;
}

std::string RenderWithOwner(const semantic::Program& program,
	semantic::ScopeId owner, const std::string& terminal,
	std::size_t* components)
{
	const EntityId owner_entity = owner == kNoScope ? kNoEntity :
		program.EntityForScope(owner);
	std::string result;
	std::size_t count = 0;
	if (owner_entity != kNoEntity && owner_entity < program.entities.size() &&
		program.entities[owner_entity].lambda_closure)
	{
		result = RenderLambdaEntityEmissionName(
			program, owner_entity, &count);
	}
	else
	{
		std::vector<NameId> path;
		program.BuildEmissionPath(owner, 0, &path);
		if (!path.empty()) path.pop_back();
		count = path.size();
		for (std::size_t i = 0; i < path.size(); ++i)
		{
			if (i != 0) result += "::";
			result += program.names.Get(path[i]);
		}
	}
	if (!result.empty()) result += "::";
	result += terminal;
	if (components) *components = count + 1;
	return result;
}

}

std::string RenderLambdaEntityTerminal(const semantic::Program& program,
	semantic::EntityId entity, semantic::Stats* stats)
{
	const EntityRecord& record = LambdaEntity(program, entity);
	std::string result;
	if (record.local_context == kNoBinding)
		result = "__" + std::to_string(record.lambda_ordinal);
	else result = RenderLambdaIdentityComponent(program, record.local_context,
		record.lambda_token_first, record.lambda_token_last,
		record.lambda_ordinal, stats);
	if (stats)
	{
		++stats->presentation_renders[
			SEMANTIC_PRESENTATION_LAMBDA_IDENTITY];
		stats->presentation_render_components[
			SEMANTIC_PRESENTATION_LAMBDA_IDENTITY] +=
			record.local_context == kNoBinding ? 1 : 4;
		stats->presentation_render_bytes[
			SEMANTIC_PRESENTATION_LAMBDA_IDENTITY] += result.size();
	}
	return result;
}

std::string RenderLambdaEntityEmissionName(const semantic::Program& program,
	semantic::EntityId entity, std::size_t* components,
	semantic::Stats* stats)
{
	const EntityRecord& record = LambdaEntity(program, entity);
	const ScopeId owner = record.local_context == kNoBinding ? record.owner :
		program.bindings.at(record.local_context).owner;
	return RenderWithOwner(program, owner,
		RenderLambdaEntityTerminal(program, entity, stats), components);
}

std::string RenderLambdaMemberTerminal(const semantic::Program& program,
	semantic::EntityId entity, semantic::NameId terminal,
	semantic::Stats* stats)
{
	const semantic::EntityRecord& record = LambdaEntity(program, entity);
	const std::string& spelling = program.names.Get(terminal);
	if (terminal == record.identity_name)
		return RenderLambdaEntityTerminal(program, entity, stats);
	const std::string& identity = program.names.Get(record.identity_name);
	if (spelling.size() == identity.size() + 1 && spelling[0] == '~' &&
		spelling.compare(1, identity.size(), identity) == 0)
		return "~" + RenderLambdaEntityTerminal(program, entity, stats);
	return spelling;
}

std::string RenderLambdaInvocationEmissionName(const semantic::Program& program,
	semantic::EntityId entity, semantic::ScopeId owner, std::size_t* components,
	semantic::Stats* stats)
{
	(void)LambdaEntity(program, entity);
	return RenderWithOwner(program, owner,
		RenderLambdaEntityTerminal(program, entity, stats), components);
}

std::string RenderLambdaSourceIdentityName(const semantic::Program& program,
	semantic::EntityId entity, std::size_t* components,
	semantic::Stats* stats)
{
	const EntityRecord& record = LambdaEntity(program, entity);
	const std::string terminal = record.local_context == kNoBinding ?
		"$_" + std::to_string(record.lambda_ordinal) :
		RenderLambdaEntityTerminal(program, entity, stats);
	return RenderWithOwner(program, record.owner, terminal, components);
}

} }
}
