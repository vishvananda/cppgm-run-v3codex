#include "pa12_semantic_detail.h"
#include "post_tokenizer.h"

#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

bool IsTokenSafeElfSectionName(const std::string& name)
{
	if (name.empty()) return false;
	for (std::size_t i = 0; i < name.size(); ++i)
	{
		const unsigned char c = static_cast<unsigned char>(name[i]);
		const bool alphanumeric = (c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
		if (!alphanumeric && c != '_' && c != '.') return false;
	}
	return true;
}

}

void SemanticAnalyzer::ApplyVariableObjectAttributes(
	NodeId declaration, BindingId binding)
{
	BindingRecord& record = program_->bindings[binding];
	BindingRecord& canonical = program_->bindings[record.canonical];
	for (std::uint32_t edge = arena_->FirstEdge(declaration); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId attribute = arena_->EdgeChild(edge);
		if (!arena_->IsTag(attribute, ::cppgm::pa10_syntax_detail::STAG_GNU_ATTRIBUTE)) continue;
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
			if (arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_GNU_ATTRIBUTE_NONLITERAL_ARGUMENT))
				throw std::runtime_error("invalid section attribute argument");
			if (!arena_->IsTag(child, ::cppgm::pa10_syntax_detail::STAG_GNU_ATTRIBUTE_ARGUMENT)) continue;
			if (argument != kNoNode)
				throw std::runtime_error("section attribute has multiple arguments");
			argument = child;
		}
		if (argument == kNoNode)
			throw std::runtime_error("section attribute requires a string");
		std::string section;
		if (!DecodeNarrowStringLiteralSequence(arena_->SemanticPayload(argument),
			&section) || !IsTokenSafeElfSectionName(section))
			throw std::runtime_error("invalid section attribute name");
		const NameId section_name = program_->names.Intern(section);
		if ((record.object_section_name != 0 &&
				record.object_section_name != section_name) ||
			(canonical.object_section_name != 0 &&
				canonical.object_section_name != section_name))
			throw std::runtime_error("conflicting section attributes");
		record.object_section_name = section_name;
		canonical.object_section_name = section_name;
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
