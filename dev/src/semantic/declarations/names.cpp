#include "semantic/analysis/analyzer.h"

#include <sstream>
#include <string>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::BuildClassDeclarationNamePath(NodeId node,
	const std::string& hint, const std::string& specialization_name,
	std::string* spelling, NamePath* path, bool* generated_identity)
{
	*generated_identity = false;
	*spelling = specialization_name.empty() ?
		arena_->Payload(node) : specialization_name;
	if (spelling->empty() && !hint.empty())
	{
		++local_type_count_;
		*spelling = "__local_type" + std::to_string(local_type_count_);
		*generated_identity = true;
		if (stats_)
			RecordGeneratedIdentityRender(SEMANTIC_GENERATED_LOCAL_TYPE,
				*spelling, 1);
	}
	if (spelling->empty())
	{
		std::ostringstream generated;
		generated << "__anonymous_union_type__" << arena_->TokenFirst(node)
			<< '_' << arena_->TokenLast(node);
		*spelling = generated.str();
		*generated_identity = true;
		if (stats_)
			RecordGeneratedIdentityRender(
				SEMANTIC_GENERATED_ANONYMOUS_UNION_TYPE, *spelling, 2);
	}

	if (specialization_name.empty())
	{
		const NodeId structure = FindChild(
			node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
		if (structure != kNoNode)
		{
			*path = StructuredNamePath(structure);
			return;
		}
		if (!arena_->Payload(node).empty())
		{
			path->Push(program_->names.UseInterned(arena_->PayloadId(node)));
			return;
		}
	}
	path->Push(program_->names.Intern(*spelling));
}

void SemanticAnalyzer::BuildEnumDeclarationNamePath(NodeId node,
	const std::string& hint, std::string* spelling, NamePath* path,
	bool* generated_identity)
{
	*generated_identity = false;
	*spelling = arena_->Payload(node);
	if (spelling->empty()) *spelling = hint;
	if (spelling->empty())
	{
		++anonymous_enum_count_;
		*spelling = "__anonymous_enum" +
			std::to_string(anonymous_enum_count_);
		*generated_identity = true;
		if (stats_)
			RecordGeneratedIdentityRender(SEMANTIC_GENERATED_ANONYMOUS_ENUM,
				*spelling, 1);
	}
	if (!arena_->Payload(node).empty() &&
		spelling->find("::") == std::string::npos)
		path->Push(program_->names.UseInterned(arena_->PayloadId(node)));
	else if (spelling->find("::") == std::string::npos)
		path->Push(program_->names.Intern(*spelling));
	else *path = ParseNamePath(
		*spelling, NAME_PATH_PARSE_DECLARATION_ENUM);
}

}
}
