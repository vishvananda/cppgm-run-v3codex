#include "pa12_semantic_detail.h"
#include "post_tokenizer.h"

#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::ApplyVariableObjectAttributes(
	NodeId declaration, BindingId binding)
{
	BindingRecord& record = program_->bindings[binding];
	BindingRecord& canonical = program_->bindings[record.canonical];
	for (std::uint32_t edge = arena_->FirstEdge(declaration); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId attribute = arena_->EdgeChild(edge);
		if (!arena_->IsTag(attribute, "gnu-attribute")) continue;
		const std::string name = arena_->SemanticPayload(attribute);
		if (name == "weak" || name == "__weak__")
		{
			record.weak_symbol = true;
			canonical.weak_symbol = true;
			continue;
		}
		if (name != "section" && name != "__section__") continue;
		NodeId argument = kNoNode;
		for (std::uint32_t argument_edge = arena_->FirstEdge(attribute);
			argument_edge != kNoEdge;
			argument_edge = arena_->NextEdge(argument_edge))
		{
			const NodeId child = arena_->EdgeChild(argument_edge);
			if (arena_->IsTag(child, "gnu-attribute-nonliteral-argument"))
				throw std::runtime_error("invalid section attribute argument");
			if (!arena_->IsTag(child, "gnu-attribute-argument")) continue;
			if (argument != kNoNode)
				throw std::runtime_error("section attribute has multiple arguments");
			argument = child;
		}
		if (argument == kNoNode)
			throw std::runtime_error("section attribute requires a string");
		std::string section;
		if (!DecodeNarrowStringLiteralSequence(arena_->SemanticPayload(argument),
			&section) || section.empty())
			throw std::runtime_error("invalid section attribute name");
		record.object_section_name = program_->names.Intern(section);
		canonical.object_section_name = record.object_section_name;
	}
}

std::uint32_t SemanticAnalyzer::MakeVariableDeclarationDump(
	TypeId type, NameId name, BindingId binding, bool local,
	bool has_initializer, bool* declaration_only)
{
	if (!declaration_only)
		throw std::logic_error("missing variable declaration classification");
	*declaration_only = !local && !has_initializer &&
		(program_->bindings[binding].storage_class == STORAGE_CLASS_EXTERN ||
		 direct_linkage_declaration_depth_ != 0);
	const std::uint32_t variable = MakeDump(
		DUMP_VARIABLE, type, VALUE_NONE, name, binding);
	return variable;
}

}
}
