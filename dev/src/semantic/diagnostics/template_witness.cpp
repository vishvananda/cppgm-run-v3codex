#include "semantic/diagnostics/template_witness.h"

#include "semantic/analysis/analyzer.h"
#include "semantic/presentation/source_identity.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>

namespace cppgm
{
namespace semantic
{

namespace
{

std::string NormalizeWitnessSourcePath(const std::string& path)
{
	const std::string marker = "/tests/";
	const std::size_t found = path.rfind(marker);
	return found == std::string::npos ? path : path.substr(found + 1);
}

const char* BindingProvenance(std::uint8_t provenance)
{
	return provenance == 0 ? "explicit" :
		provenance == 1 ? "deduced" : "defaulted";
}

const char* OverloadDropReasonName(std::uint8_t reason)
{
	return reason == 1 ? "too_few_arguments" :
		reason == 2 ? "too_many_arguments" :
		reason == 3 ? "bad_conversion" :
		reason == 4 ? "worse_conversion" :
		reason == 5 ? "better_candidate_selected" :
		reason == 6 ? "inconsistent" :
		reason == 7 ? "non_deduced_mismatch" :
		reason == 8 ? "substitution_failure" :
		"explicit_not_allowed";
}

void ReplaceAll(std::string* text, const std::string& from,
	const std::string& to)
{
	for (std::size_t at = text->find(from); at != std::string::npos;
		at = text->find(from, at + to.size()))
		text->replace(at, from.size(), to);
}

std::string NormalizeWitnessTypeSpelling(std::string spelling)
{
	ReplaceAll(&spelling, "unsigned long long int", "unsigned long long");
	ReplaceAll(&spelling, "long long int", "long long");
	ReplaceAll(&spelling, "unsigned long int", "unsigned long");
	ReplaceAll(&spelling, "long int", "long");
	ReplaceAll(&spelling, "unsigned short int", "unsigned short");
	ReplaceAll(&spelling, "short int", "short");
	const std::string retained = "__retained_template_parameter_shape_";
	for (std::size_t at = spelling.find(retained); at != std::string::npos;
		at = spelling.find(retained, at + 1))
	{
		std::size_t end = at + retained.size();
		while (end < spelling.size() && spelling[end] >= '0' &&
			spelling[end] <= '9') ++end;
		if (end == at + retained.size()) continue;
		spelling.replace(at, retained.size(), "type-parameter-0-");
	}
	return spelling;
}

std::string RenderWitnessArgument(const Program& program,
	const TemplateArgument& argument,
	const presentation::TemplateArgumentElision* elision = 0,
	bool show_anonymous_namespace = false)
{
	if ((argument.kind == TEMPLATE_ARGUMENT_TYPE ||
		 argument.kind == TEMPLATE_ARGUMENT_TEMPLATE) &&
		(argument.type == kNoType || argument.type >= program.types.Size()))
		return "<dependent>";
	if (argument.kind == TEMPLATE_ARGUMENT_INTEGRAL &&
		(argument.type == kNoType || argument.type >= program.types.Size()))
		return argument.IsDependent() ? "<dependent>" :
			std::to_string(argument.value);
	if (argument.kind == TEMPLATE_ARGUMENT_INTEGRAL &&
		argument.type != kNoType && argument.type < program.types.Size())
	{
		const TypeRecord& type = program.types.Get(
			program.types.RemoveTopCv(argument.type));
		if (type.kind == TYPE_FUNDAMENTAL &&
			(type.fundamental == FUND_CHAR ||
			 type.fundamental == FUND_SIGNED_CHAR ||
			 type.fundamental == FUND_UNSIGNED_CHAR))
		{
			const unsigned char value = static_cast<unsigned char>(argument.value);
			if (value == '\a') return "'\\a'";
			if (value == '\b') return "'\\b'";
			if (value == '\f') return "'\\f'";
			if (value == '\n') return "'\\n'";
			if (value == '\r') return "'\\r'";
			if (value == '\t') return "'\\t'";
			if (value == '\v') return "'\\v'";
			if (value == '\\') return "'\\\\'";
			if (value == '\'') return "'\\\''";
			if (value >= 32 && value < 127)
				return std::string("'") + static_cast<char>(value) + "'";
			std::ostringstream escaped;
			escaped << "'\\x" << std::hex << std::setfill('0') <<
				std::setw(2) << static_cast<unsigned>(value) << "'";
			return escaped.str();
		}
		if (type.kind == TYPE_FUNDAMENTAL &&
			(type.fundamental == FUND_UNSIGNED_SHORT_INT ||
			 type.fundamental == FUND_UNSIGNED_INT ||
			 type.fundamental == FUND_UNSIGNED_LONG_INT ||
			 type.fundamental == FUND_UNSIGNED_LONG_LONG_INT))
			return std::to_string(static_cast<std::uint64_t>(argument.value));
	}
	std::string spelling = NormalizeWitnessTypeSpelling(
		elision ? presentation::RenderTemplateArgument(
			program, argument, *elision, show_anonymous_namespace) :
		presentation::RenderTemplateArgument(
			program, argument, show_anonymous_namespace));
	if (argument.kind == TEMPLATE_ARGUMENT_TYPE &&
		argument.type != kNoType && argument.type < program.types.Size() &&
		program.types.Get(program.types.RemoveTopCv(argument.type)).IsIncompleteArray())
		ReplaceAll(&spelling, "[0]", "[]");
	return spelling;
}

std::string RenderFunctionTypeSourceIdentity(const Program& program,
	const TemplateArgument& argument, const std::string& source,
	const presentation::TemplateArgumentElision* elision = 0,
	bool show_anonymous_namespace = false)
{
	const std::string canonical = RenderWitnessArgument(
		program, argument, elision, show_anonymous_namespace);
	if (argument.kind != TEMPLATE_ARGUMENT_TYPE) return canonical;
	const std::size_t close = source.rfind(')');
	if (close == std::string::npos) return canonical;
	const std::string suffix = source.substr(close + 1);
	if (suffix == " const" || suffix == " volatile" ||
		suffix == " const volatile" || suffix == " volatile const" ||
		suffix == " &" || suffix == " &&" || suffix == " const &" ||
		suffix == " volatile &" || suffix == " const volatile &" ||
		suffix == " volatile const &")
		return canonical + suffix;
	return canonical;
}

std::string RenderSimpleDependentArgument(const SyntaxArena& arena,
	syntax::NodeId node)
{
	if (node == syntax::kNoNode) return std::string();
	if (arena.IsTag(node, ::cppgm::syntax::STAG_PACK_EXPANSION_EXPRESSION))
	{
		for (std::uint32_t edge = arena.FirstEdge(node); edge != syntax::kNoEdge;
			edge = arena.NextEdge(edge))
		{
			const std::string operand = RenderSimpleDependentArgument(
				arena, arena.EdgeChild(edge));
			if (!operand.empty()) return operand + "...";
		}
		return std::string();
	}
	if (arena.IsTag(node, ::cppgm::syntax::STAG_ID_EXPRESSION) ||
		arena.IsTag(node, ::cppgm::syntax::STAG_TYPE_NAME) ||
		arena.IsTag(node, ::cppgm::syntax::STAG_NAME_COMPONENT) ||
		arena.IsTag(node, ::cppgm::syntax::STAG_IDENTIFIER))
	{
		std::string name = arena.SemanticPayload(node);
		if (name.empty()) name = arena.Payload(node);
		if (name.compare(0, 14, "TT_IDENTIFIER:") == 0) name.erase(0, 14);
		if (!name.empty()) return name;
	}
	std::string only;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != syntax::kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const std::string child = RenderSimpleDependentArgument(
			arena, arena.EdgeChild(edge));
		if (child.empty()) continue;
		if (!only.empty()) return std::string();
		only = child;
	}
	return only;
}

bool SyntaxUsesName(const SyntaxArena& arena, syntax::NodeId node, NameId name)
{
	if (node == syntax::kNoNode) return false;
	if (arena.SemanticPayloadId(node) == name || arena.PayloadId(node) == name)
		return true;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != syntax::kNoEdge;
		edge = arena.NextEdge(edge))
		if (SyntaxUsesName(arena, arena.EdgeChild(edge), name)) return true;
	return false;
}

bool CollectExplicitTemplateArgumentSyntax(const SyntaxArena& arena,
	syntax::NodeId node, std::vector<syntax::NodeId>* result)
{
	if (node == syntax::kNoNode) return false;
	if (arena.IsTag(node,
		::cppgm::syntax::STAG_TEMPLATE_TYPE_ARGUMENT_LIST))
	{
		result->clear();
		for (std::uint32_t edge = arena.FirstEdge(node);
			edge != syntax::kNoEdge; edge = arena.NextEdge(edge))
			result->push_back(arena.EdgeChild(edge));
		return true;
	}
	if (arena.IsTag(node, ::cppgm::syntax::STAG_NAME_COMPONENT))
	{
		for (std::uint32_t edge = arena.FirstEdge(node);
			edge != syntax::kNoEdge; edge = arena.NextEdge(edge))
			if (arena.IsTag(arena.EdgeChild(edge),
				::cppgm::syntax::STAG_TEMPLATE_TYPE_ARGUMENT_LIST))
				return CollectExplicitTemplateArgumentSyntax(
					arena, arena.EdgeChild(edge), result);
		return false;
	}
	if (arena.IsTag(node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME))
	{
		bool found = false;
		for (std::uint32_t edge = arena.FirstEdge(node);
			edge != syntax::kNoEdge; edge = arena.NextEdge(edge))
		{
			const syntax::NodeId child = arena.EdgeChild(edge);
			if (arena.IsTag(child, ::cppgm::syntax::STAG_NAME_COMPONENT) &&
				CollectExplicitTemplateArgumentSyntax(arena, child, result))
				found = true;
		}
		return found;
	}
	for (std::uint32_t edge = arena.FirstEdge(node); edge != syntax::kNoEdge;
		edge = arena.NextEdge(edge))
		if (CollectExplicitTemplateArgumentSyntax(
			arena, arena.EdgeChild(edge), result)) return true;
	return false;
}

std::string RenderSourceTokenRange(const SyntaxArena& arena,
	std::size_t first, std::size_t last)
{
	if (first >= last || last > arena.TokenCount()) return std::string();
	std::string result;
	std::size_t prior_line = 0;
	std::size_t prior_end = 0;
	for (std::size_t token = first; token < last; ++token)
	{
		const std::string& spelling = arena.TokenSpelling(token);
		const std::size_t line = arena.TokenSourceLine(token);
		const std::size_t column = arena.TokenSourceColumn(token);
		if (!result.empty() &&
			(line != prior_line || column > prior_end)) result += ' ';
		result += spelling;
		prior_line = line;
		prior_end = column + spelling.size();
	}
	ReplaceAll(&result, "> >", ">>");
	ReplaceAll(&result, " ,", ",");
	ReplaceAll(&result, ",", ", ");
	while (result.find("  ") != std::string::npos)
		ReplaceAll(&result, "  ", " ");
	ReplaceAll(&result, "( ", "(");
	ReplaceAll(&result, " )", ")");
	ReplaceAll(&result, "[ ", "[");
	ReplaceAll(&result, " ]", "]");
	ReplaceAll(&result, " {", "{");
	ReplaceAll(&result, " }", "}");
	ReplaceAll(&result, " :: ", "::");
	ReplaceAll(&result, " ::", "::");
	ReplaceAll(&result, ":: ", "::");
	return result;
}

std::string RenderSourceSyntax(const SyntaxArena& arena, syntax::NodeId node)
{
	return node == syntax::kNoNode ? std::string() :
		RenderSourceTokenRange(arena, arena.TokenFirst(node), arena.TokenLast(node));
}

struct CvSpellingShape
{
	std::vector<std::string> non_cv_tokens;
	std::size_t postfix_qualifiers;

	CvSpellingShape() : non_cv_tokens(), postfix_qualifiers(0) {}
};

CvSpellingShape ClassifyCvSpelling(const std::string& spelling)
{
	CvSpellingShape result;
	std::string prior;
	for (std::size_t at = 0; at < spelling.size(); )
	{
		const unsigned char c = static_cast<unsigned char>(spelling[at]);
		if (std::isspace(c))
		{
			++at;
			continue;
		}
		std::string token;
		if (std::isalnum(c) || spelling[at] == '_')
		{
			std::size_t end = at + 1;
			while (end < spelling.size())
			{
				const unsigned char next =
					static_cast<unsigned char>(spelling[end]);
				if (!std::isalnum(next) && spelling[end] != '_') break;
				++end;
			}
			token = spelling.substr(at, end - at);
			at = end;
		}
		else
		{
			token.assign(1, spelling[at]);
			++at;
		}
		const bool cv = token == "const" || token == "volatile" ||
			token == "_Atomic";
		const bool prior_cv = prior == "const" || prior == "volatile" ||
			prior == "_Atomic";
		if (cv && !prior.empty() && !prior_cv)
		{
			const unsigned char first = static_cast<unsigned char>(prior[0]);
			if (std::isalnum(first) || prior[0] == '_' || prior == ">" ||
				prior == "]") ++result.postfix_qualifiers;
		}
		else if (!cv) result.non_cv_tokens.push_back(token);
		prior = token;
	}
	return result;
}

std::string NormalizeSourceArgumentSpelling(const Program& program,
	std::string spelling, const TemplateArgument& argument)
{
	if (argument.kind != TEMPLATE_ARGUMENT_TYPE &&
		argument.kind != TEMPLATE_ARGUMENT_TEMPLATE) return spelling;
	for (std::size_t at = 0; at < spelling.size(); ++at)
		if (spelling[at] == '*' && at != 0 && spelling[at - 1] != ' ')
		{
			spelling.insert(at, 1, ' ');
			++at;
		}
	ReplaceAll(&spelling, "* const", "*const");
	ReplaceAll(&spelling, "* volatile", "*volatile");
	if (spelling.find('*') == std::string::npos)
	{
		const std::size_t cv = spelling.find(" const");
		if (cv != std::string::npos &&
			(cv + 6 == spelling.size() || spelling[cv + 6] == ' ' ||
			 spelling[cv + 6] == '&'))
		{
			spelling.erase(cv, 6);
			spelling = "const " + spelling;
		}
	}
	const std::string canonical = RenderWitnessArgument(program, argument);
	const CvSpellingShape source_shape = ClassifyCvSpelling(spelling);
	const CvSpellingShape canonical_shape = ClassifyCvSpelling(canonical);
	if (source_shape.non_cv_tokens == canonical_shape.non_cv_tokens &&
		canonical_shape.postfix_qualifiers < source_shape.postfix_qualifiers)
		return canonical;
	return spelling;
}

void CollectExplicitTemplateArgumentSpellings(const SyntaxArena& arena,
	syntax::NodeId node, const std::string& name, std::size_t name_token,
	std::vector<std::string>* result)
{
	result->clear();
	if (node == syntax::kNoNode) return;
	const std::size_t first = arena.TokenFirst(node);
	const std::size_t last = arena.TokenLast(node);
	if (first >= last || last > arena.TokenCount()) return;
	std::size_t opening = last;
	if (name_token >= first && name_token + 1 < last &&
		arena.TokenSpelling(name_token) == name &&
		arena.TokenSpelling(name_token + 1) == "<")
		opening = name_token + 1;
	else
		for (std::size_t token = first; token + 1 < last; ++token)
			if (arena.TokenSpelling(token) == name &&
				arena.TokenSpelling(token + 1) == "<") opening = token + 1;
	if (opening == last) return;
	std::size_t argument_first = opening + 1;
	std::size_t angle = 1, paren = 0, bracket = 0, brace = 0;
	for (std::size_t token = argument_first; token < last; ++token)
	{
		const std::string& spelling = arena.TokenSpelling(token);
		if (spelling == "(") ++paren;
		else if (spelling == ")" && paren != 0) --paren;
		else if (spelling == "[") ++bracket;
		else if (spelling == "]" && bracket != 0) --bracket;
		else if (spelling == "{") ++brace;
		else if (spelling == "}" && brace != 0) --brace;
		else if (spelling == "<" && paren == 0 && bracket == 0 && brace == 0)
			++angle;
		else if (spelling == ">" && paren == 0 && bracket == 0 && brace == 0)
		{
			if (--angle == 0)
			{
				if (argument_first != token)
					result->push_back(RenderSourceTokenRange(
						arena, argument_first, token));
				return;
			}
		}
		else if (spelling == "," && angle == 1 && paren == 0 &&
			bracket == 0 && brace == 0)
		{
			result->push_back(RenderSourceTokenRange(
				arena, argument_first, token));
			argument_first = token + 1;
		}
	}
}

std::string RenderWitnessArgumentAtSource(const Program& program,
	const SyntaxArena& arena, const TemplateArgument& argument,
	syntax::NodeId source, bool prefer_source = false,
	const presentation::TemplateArgumentElision* elision = 0,
	bool show_anonymous_namespace = false)
{
	if (source != syntax::kNoNode && prefer_source)
	{
		const std::string spelling = RenderSourceSyntax(arena, source);
		if (!spelling.empty()) return spelling;
	}
	if (source != syntax::kNoNode &&
		argument.kind == TEMPLATE_ARGUMENT_TYPE &&
		argument.type != kNoType && argument.type < program.types.Size() &&
		program.types.Get(
			program.types.RemoveTopCv(argument.type)).kind == TYPE_FUNCTION)
	{
		const std::string spelling = RenderSourceSyntax(arena, source);
		if (!spelling.empty())
			return RenderFunctionTypeSourceIdentity(
				program, argument, spelling, elision,
				show_anonymous_namespace);
	}
	if (source != syntax::kNoNode && argument.IsDependent())
	{
		std::string spelling = RenderSimpleDependentArgument(arena, source);
		if (!spelling.empty())
		{
			if (argument.pack_expansion &&
				(spelling.size() < 3 ||
				 spelling.compare(spelling.size() - 3, 3, "...") != 0))
				spelling += "...";
			return spelling;
		}
	}
	if (source != syntax::kNoNode &&
		argument.kind == TEMPLATE_ARGUMENT_INTEGRAL)
	{
		for (BindingId binding = 0; binding < program.bindings.size(); ++binding)
		{
			const BindingRecord& candidate = program.bindings[binding];
			if (candidate.kind != BIND_ENUMERATOR ||
				candidate.source_view_suppressed ||
				candidate.value != argument.value ||
				!SyntaxUsesName(arena, source, candidate.name)) continue;
			if (argument.type != kNoType && candidate.type != argument.type)
				continue;
			return presentation::RenderName(
				program, candidate.owner, candidate.name);
		}
		if (arena.IsTag(source, ::cppgm::syntax::STAG_CAST_EXPRESSION))
			return "(" + NormalizeWitnessTypeSpelling(
				presentation::RenderType(program, argument.type)) + ")" +
				RenderWitnessArgument(program, argument, elision);
	}
	return RenderWitnessArgument(
		program, argument, elision, show_anonymous_namespace);
}

std::size_t FinalTemplateOpening(const std::string& spelling)
{
	if (spelling.empty() || spelling[spelling.size() - 1] != '>')
		return std::string::npos;
	std::size_t depth = 0;
	for (std::size_t at = spelling.size(); at != 0; )
	{
		const char c = spelling[--at];
		if (c == '>') ++depth;
		else if (c == '<' && --depth == 0) return at;
	}
	return std::string::npos;
}

std::string RenderDependentPartialOwnerIdentity(const Program& program,
	const ClassTemplatePattern& owner,
	const ClassTemplatePartialPattern& partial)
{
	if (partial.canonical_argument_state != 1) return std::string();
	std::string result = presentation::RenderName(
		program, owner.owner, owner.name, true) + '<';
	const presentation::TemplateParameterIdentity identity(0);
	for (std::size_t argument = 0;
		argument < partial.canonical_arguments.size(); ++argument)
	{
		if (argument != 0) result += ", ";
		result += presentation::RenderTemplateArgument(
			program, partial.canonical_arguments[argument], identity);
	}
	return result + '>';
}

std::size_t FirstLocatedDescendantToken(const SyntaxArena& arena,
	syntax::NodeId syntax, const std::string& primary_source_file)
{
	if (syntax == syntax::kNoNode)
		return std::numeric_limits<std::size_t>::max();
	const std::size_t first = arena.TokenFirst(syntax);
	if (arena.TokenLast(syntax) > first && first < arena.TokenCount() &&
		arena.TokenSourceFile(first) == primary_source_file &&
		arena.TokenSourceLine(first) != 0)
		return first;
	for (std::uint32_t edge = arena.FirstEdge(syntax);
		edge != syntax::kNoEdge; edge = arena.NextEdge(edge))
	{
		const std::size_t child = FirstLocatedDescendantToken(
			arena, arena.EdgeChild(edge), primary_source_file);
		if (child != std::numeric_limits<std::size_t>::max()) return child;
	}
	return std::numeric_limits<std::size_t>::max();
}

syntax::NodeId FindDescendantTag(const SyntaxArena& arena,
	syntax::NodeId node, syntax::SyntaxTagCode tag)
{
	if (arena.IsTag(node, tag)) return node;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != syntax::kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const syntax::NodeId found = FindDescendantTag(
			arena, arena.EdgeChild(edge), tag);
		if (found != syntax::kNoNode) return found;
	}
	return syntax::kNoNode;
}

std::size_t DescendantDistance(const SyntaxArena& arena,
	syntax::NodeId root, syntax::NodeId target)
{
	if (root == target) return 0;
	for (std::uint32_t edge = arena.FirstEdge(root); edge != syntax::kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const std::size_t child = DescendantDistance(
			arena, arena.EdgeChild(edge), target);
		if (child != std::numeric_limits<std::size_t>::max())
			return child + 1;
	}
	return std::numeric_limits<std::size_t>::max();
}

bool IsWithinSyntaxTag(const SyntaxArena& arena, syntax::NodeId target,
	syntax::SyntaxTagCode tag)
{
	for (syntax::NodeId node = 0; node < arena.Nodes(); ++node)
		if (arena.IsTag(node, tag) &&
			DescendantDistance(arena, node, target) !=
				std::numeric_limits<std::size_t>::max())
			return true;
	return false;
}

}

TemplateWitnessObserver::SourceEvent::SourceEvent(
	SourceEventKind kind_value, syntax::NodeId syntax_value,
	std::uint32_t pattern_value, BindingId binding_value,
	const std::vector<TemplateArgument>& argument_values,
	std::size_t explicit_count, std::size_t column_offset,
	std::size_t ordinal)
	: kind(kind_value), syntax(syntax_value), component_syntax(syntax_value),
	  pattern(pattern_value),
	  binding(binding_value), qualifier_entity(kNoEntity),
	  qualifier_pattern(kNoDumpEdge),
	  qualifier_partial_pattern(kNoDumpEdge),
	  arguments(argument_values),
	  provenance(argument_values.size(), 2), parameter_offsets(),
	  specialization_arguments(), specialization_offsets(),
	  specialization_packs(),
	  source_column_offset(column_offset), source_name(0),
	  source_token(std::numeric_limits<std::size_t>::max()),
	  insertion_ordinal(ordinal), suppressed(false),
	  allow_substituted_source(false), complete_at_source(false),
	  selection_kind(0)
{
	for (std::size_t i = 0; i < explicit_count && i < provenance.size(); ++i)
		provenance[i] = 0;
}

TemplateWitnessObserver::FunctionSpecializationFact::
	FunctionSpecializationFact(BindingId binding_value,
	std::uint32_t pattern_value,
	const std::vector<TemplateArgument>& argument_values,
	const std::vector<TemplateArgument>& requested_values,
	const std::vector<std::uint32_t>& parameter_offset_values)
	: binding(binding_value), pattern(pattern_value), arguments(argument_values),
	  provenance(argument_values.size(), 1),
	  parameter_offsets(parameter_offset_values)
{
	for (std::size_t i = 0; i < provenance.size() &&
		i < requested_values.size(); ++i)
		if (requested_values[i].type == kNoType) provenance[i] = 2;
}

TemplateWitnessObserver::TemplateWitnessObserver(bool debug)
	: text_(), debug_text_(), primary_source_file_(), debug_(debug),
	  semantic_source_facts_(), retained_member_source_facts_(),
	  retained_alias_qualifier_facts_(), source_events_(),
	  function_specializations_(), class_specializations_(),
	  class_template_source_facts_(),
	  entity_argument_limits_(), variable_specializations_(),
	  overload_selections_(), deduction_drops_(), dependent_class_uses_(),
	  dependent_alias_uses_(), dependent_source_uses_(),
	  source_occurrences_(), variable_occurrences_(), resolved_source_uses_(),
	  function_instantiations_(),
	  required_definitions_(), class_instantiations_(), class_finalizations_(),
	  variable_instantiations_(), next_insertion_ordinal_(0) {}

const std::string& TemplateWitnessObserver::Text() const
{
	return text_;
}

const std::string& TemplateWitnessObserver::DebugText() const
{
	return debug_text_;
}

bool Analyzer::TemplateWitnessSourceUseEnabled() const
{
	return template_witness_ != 0;
}

void Analyzer::RecordDeducedClassObjectUse(NodeId specifiers, TypeId type)
{
	if (!template_witness_) return;
	if (template_witness_->debug_)
		template_witness_->debug_text_ += "object-use syntax=" +
			std::to_string(specifiers) + " type=" + std::to_string(type) +
			" enabled=" + std::to_string(TemplateWitnessSourceUseEnabled()) +
			" class=" + std::to_string(IsClassObjectType(type)) + "\n";
	if (class_template_member_replay_depth_ != 0 ||
		!TemplateWitnessSourceUseEnabled() || specifiers == kNoNode ||
		!IsClassObjectType(type)) return;
	NodeId structure = FindDescendantTag(
		*arena_, specifiers, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
	if (structure == kNoNode)
	{
		if (template_witness_->debug_)
			template_witness_->debug_text_ +=
				"  unqualified-type-spelling\n";
		structure = FindDescendantTag(
			*arena_, specifiers, ::cppgm::syntax::STAG_TYPE_NAME);
		if (structure == kNoNode)
			for (std::uint32_t edge = arena_->FirstEdge(specifiers);
				edge != kNoEdge; edge = arena_->NextEdge(edge))
			{
				const NodeId child = arena_->EdgeChild(edge);
				if (arena_->Payload(child).compare(
						0, 14, "TT_IDENTIFIER:") == 0)
					structure = child;
			}
		if (structure == kNoNode) structure = specifiers;
	}
	NodeId terminal_component = kNoNode;
	bool dependent_qualifier = false;
	for (std::uint32_t edge = arena_->FirstEdge(structure); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (arena_->IsTag(arena_->EdgeChild(edge),
			::cppgm::syntax::STAG_NAME_COMPONENT))
		{
			if (terminal_component != kNoNode &&
				template_witness_->IsRetainedDependentSourceUse(
					terminal_component))
				dependent_qualifier = true;
			terminal_component = arena_->EdgeChild(edge);
		}
	if (dependent_qualifier) return;
	if (terminal_component != kNoNode && FindChild(terminal_component,
		::cppgm::syntax::STAG_TEMPLATE_TYPE_ARGUMENT_LIST) != kNoNode)
		return;
	const EntityId entity = EntityOf(type);
	if (template_witness_->debug_)
		template_witness_->debug_text_ += "  structure=" +
			std::to_string(structure) + " entity=" + std::to_string(entity) +
			" patterns=" +
			std::to_string(class_template_pattern_by_entity_.size()) + "\n";
	if (entity == kNoEntity ||
		entity >= class_template_pattern_by_entity_.size()) return;
	const std::uint32_t pattern = class_template_pattern_by_entity_[entity];
	const EntityRecord& record = program_->entities[entity];
	if (pattern == kNoDumpEdge || pattern >= class_templates_.size() ||
		record.template_argument_begin == kNoBinding ||
		record.declaration == kNoBinding) return;
	const NodeId source_component = terminal_component == kNoNode ?
		structure : terminal_component;
	NameId source_name = arena_->SemanticPayloadId(source_component);
	if (source_name == 0) source_name = arena_->PayloadId(source_component);
	template_witness_->RecordDeducedClassUse(
		structure, pattern,
		record.declaration, StoredTemplateArguments(
			record.template_argument_begin, record.template_argument_count),
		source_name);
}

void Analyzer::RecordFunctionTemplateSourceCall(NodeId syntax,
	BindingId selected, std::size_t explicit_count)
{
	if (!TemplateWitnessSourceUseEnabled() || syntax == kNoNode ||
		selected == kNoBinding) return;
	const NodeId component_syntax = arena_->TerminalNameComponent(syntax);
	selected = program_->bindings[selected].canonical;
	const FunctionInfo& function = GetFunction(selected);
	if (!function.template_specialization) return;
	std::uint32_t pattern = function.template_pattern;
	if (pattern == kNoDumpEdge || pattern >= function_templates_.size())
		for (std::size_t i = 0;
			i < template_witness_->function_specializations_.size(); ++i)
			if (template_witness_->function_specializations_[i].binding == selected)
			{
				pattern = template_witness_->function_specializations_[i].pattern;
				break;
			}
	if (pattern == kNoDumpEdge || pattern >= function_templates_.size()) return;
	const BindingRecord& record = program_->bindings[selected];
	if (record.template_argument_begin == kNoBinding) return;
	const std::vector<TemplateArgument> arguments = StoredTemplateArguments(
		record.template_argument_begin, record.template_argument_count);
	TemplateWitnessObserver::SourceOccurrenceRole occurrence =
		TemplateWitnessObserver::SOURCE_OCCURRENCE_EVALUATED;
	if (class_template_member_replay_depth_ != 0 ||
		explicit_member_template_replay_depth_ != 0 ||
		(current_function_context_ != kNoBinding &&
		 GetFunction(current_function_context_).template_specialization))
		occurrence =
			TemplateWitnessObserver::SOURCE_OCCURRENCE_SPECIALIZATION_REPLAY;
	template_witness_->RecordFunctionCall(syntax, component_syntax, pattern,
		selected, arguments, explicit_count, occurrence);
}

void Analyzer::RecordFunctionTemplateSourceAction(
	NodeId syntax, std::uint32_t action)
{
	if (!template_witness_ || syntax == kNoNode ||
		action == kNoDumpEdge || action >= dump_.nodes.size()) return;
	for (std::size_t depth = 0; depth != 4; ++depth)
	{
		const DumpNode& node = dump_.nodes[action];
		if (node.kind == DUMP_CONSTRUCTOR_ACTION)
		{
			RecordFunctionTemplateSourceCall(syntax, node.binding, 0);
			return;
		}
		if ((node.kind != DUMP_TEMPORARY_OBJECT &&
			 node.kind != DUMP_CLASS_VALUE_TRANSFER) ||
			node.first_edge == kNoDumpEdge ||
			dump_.edges[node.first_edge].next != kNoDumpEdge) return;
		action = dump_.edges[node.first_edge].child;
	}
}

void Analyzer::RecordStaticMemberTemplateWitness(BindingId binding)
{
	if (!template_witness_ || binding == kNoBinding ||
		binding >= program_->bindings.size()) return;
	binding = program_->bindings[binding].canonical;
	for (EntityId entity = program_->bindings[binding].member_owner;
		entity != kNoEntity; entity = program_->entities[entity].enclosing_class)
	{
		if (entity >= program_->entities.size()) return;
		if (entity >= class_template_pattern_by_entity_.size()) continue;
		const std::uint32_t pattern =
			class_template_pattern_by_entity_[entity];
		if (pattern == kNoDumpEdge || pattern >= class_templates_.size()) continue;
		const EntityRecord& owner = program_->entities[entity];
		const BindingId specialization = owner.declaration;
		if (specialization == kNoBinding ||
			owner.template_argument_begin == kNoBinding) continue;
		const ClassTemplatePartialSelection* selection =
			FindClassTemplatePartialSelection(specialization);
		const std::uint32_t selected_partial = selection ?
			selection->pattern : kNoDumpEdge;
		template_witness_->RecordRetainedMemberClassUses(
			program_->bindings[binding].name, pattern, specialization,
			selected_partial, StoredTemplateArguments(
				owner.template_argument_begin, owner.template_argument_count));
	}
}

void Analyzer::RecordTemplateVariableOccurrence(NodeId syntax,
	BindingId binding, bool filter_publication_to_source)
{
	if (!template_witness_ || syntax == kNoNode || binding == kNoBinding ||
		binding >= program_->bindings.size()) return;
	const BindingRecord& variable = program_->bindings[binding];
	if (variable.kind != BIND_VARIABLE || variable.member_owner == kNoEntity ||
		variable.non_static_data_member) return;
	EntityId owner = variable.member_owner;
	for (; owner != kNoEntity; owner = program_->entities[owner].enclosing_class)
		if (owner < class_template_pattern_by_entity_.size() &&
			class_template_pattern_by_entity_[owner] != kNoDumpEdge) break;
	if (owner == kNoEntity) return;
	const TemplateWitnessObserver::VariableOccurrenceEvaluation evaluation =
		unevaluated_depth_ != 0 ?
			TemplateWitnessObserver::VARIABLE_OCCURRENCE_UNEVALUATED :
			constant_expression_required_depth_ != 0 ?
			TemplateWitnessObserver::VARIABLE_OCCURRENCE_CONSTANT_EVALUATED :
			TemplateWitnessObserver::VARIABLE_OCCURRENCE_EVALUATED;
	const bool replayed = class_template_member_replay_depth_ != 0 ||
		explicit_member_template_replay_depth_ != 0 ||
		(current_function_context_ != kNoBinding &&
		 GetFunction(current_function_context_).template_specialization);
	TemplateWitnessObserver::VariableOccurrencePhase phase = replayed ?
		TemplateWitnessObserver::VARIABLE_OCCURRENCE_SPECIALIZATION_REPLAY :
		TemplateWitnessObserver::VARIABLE_OCCURRENCE_SOURCE;
	if (!replayed && current_function_context_ != kNoBinding)
		phase = FunctionObjectDefinitionRequired(current_function_context_) ?
			TemplateWitnessObserver::VARIABLE_OCCURRENCE_DEFINITION_DEMAND :
			TemplateWitnessObserver::VARIABLE_OCCURRENCE_DEFERRED_DEFINITION;
	std::uint8_t properties = 0;
	if (variable.constant)
		properties |= TemplateWitnessObserver::
			VARIABLE_OCCURRENCE_CONSTANT_BINDING;
	if (variable.variable_template_specialization)
		properties |= TemplateWitnessObserver::
			VARIABLE_OCCURRENCE_VARIABLE_TEMPLATE;
	if (program_->types.IsReference(variable.type))
		properties |= TemplateWitnessObserver::
			VARIABLE_OCCURRENCE_REFERENCE_TYPE;
	template_witness_->RecordVariableOccurrence(*arena_, syntax,
		variable.canonical, variable.member_owner == current_class_context_,
		evaluation, phase, properties, filter_publication_to_source);
}

void TemplateWitnessObserver::BeginTranslationUnit(
	const std::string& primary_source_file)
{
	primary_source_file_ = primary_source_file;
	semantic_source_facts_.clear();
	retained_member_source_facts_.clear();
	retained_alias_qualifier_facts_.clear();
	source_events_.clear();
	function_specializations_.clear();
	class_specializations_.clear();
	class_template_source_facts_.clear();
	entity_argument_limits_.clear();
	variable_specializations_.clear();
	overload_selections_.clear();
	deduction_drops_.clear();
	dependent_class_uses_.clear();
	dependent_alias_uses_.clear();
	dependent_source_uses_.clear();
	source_occurrences_.clear();
	variable_occurrences_.clear();
	resolved_source_uses_.clear();
	function_instantiations_.clear();
	required_definitions_.clear();
	class_instantiations_.clear();
	class_finalizations_.clear();
	variable_instantiations_.clear();
	next_insertion_ordinal_ = 0;
}

void TemplateWitnessObserver::NoteSemanticSourceFact(
	syntax::NodeId owner, syntax::NodeId syntax, std::uint32_t semantic_index,
	SemanticSourceKind kind, SemanticSourceResolution resolution,
	std::size_t explicit_count)
{
	if (owner == syntax::kNoNode || syntax == syntax::kNoNode) return;
	for (std::size_t i = 0; i < semantic_source_facts_.size(); ++i)
	{
		SemanticSourceFact& prior = semantic_source_facts_[i];
		if (prior.owner != owner || prior.syntax != syntax ||
			prior.semantic_index != semantic_index || prior.kind != kind)
			continue;
		if (prior.resolution < resolution) prior.resolution = resolution;
		if (prior.explicit_count < explicit_count)
			prior.explicit_count = static_cast<std::uint16_t>(
				explicit_count > 65535 ? 65535 : explicit_count);
		return;
	}
	semantic_source_facts_.push_back(SemanticSourceFact(
		owner, syntax, semantic_index, kind, resolution, explicit_count));
}

bool TemplateWitnessObserver::IsRetainedDependentSourceUse(
	syntax::NodeId syntax) const
{
	return std::find(dependent_source_uses_.begin(),
		dependent_source_uses_.end(), syntax) != dependent_source_uses_.end();
}

void TemplateWitnessObserver::NoteRetainedMemberSource(
	syntax::NodeId owner, syntax::NodeId source, NameId member_name,
	std::uint32_t pattern, std::uint32_t partial_pattern,
	BindingId concrete_owner)
{
	if (owner == syntax::kNoNode || source == syntax::kNoNode ||
		member_name == 0) return;
	for (std::size_t i = 0; i < retained_member_source_facts_.size(); ++i)
	{
		const RetainedMemberSourceFact& prior =
			retained_member_source_facts_[i];
		if (prior.owner == owner && prior.source == source &&
			prior.member_name == member_name && prior.pattern == pattern &&
			prior.partial_pattern == partial_pattern &&
			prior.concrete_owner == concrete_owner) return;
	}
	retained_member_source_facts_.push_back(RetainedMemberSourceFact(
		owner, source, member_name, pattern, partial_pattern, concrete_owner));
}

void TemplateWitnessObserver::ApplyRetainedAliasQualifier(
	SourceEvent* event, std::uint32_t alias_owner_pattern) const
{
	if (!event || event->kind != SOURCE_ALIAS_USE) return;
	for (std::size_t i = 0; i < retained_alias_qualifier_facts_.size(); ++i)
	{
		const RetainedAliasQualifierFact& fact =
			retained_alias_qualifier_facts_[i];
		if (fact.alias_source != event->syntax ||
			fact.pattern != alias_owner_pattern ||
			fact.partial_pattern == kNoDumpEdge) continue;
		event->qualifier_pattern = fact.pattern;
		event->qualifier_partial_pattern = fact.partial_pattern;
		return;
	}
}

void TemplateWitnessObserver::NoteRetainedAliasQualifier(
	syntax::NodeId owner, syntax::NodeId alias_source,
	syntax::NodeId qualifier_source, std::uint32_t pattern)
{
	if (owner == syntax::kNoNode || alias_source == syntax::kNoNode ||
		qualifier_source == syntax::kNoNode) return;
	for (std::size_t i = 0; i < retained_alias_qualifier_facts_.size(); ++i)
	{
		const RetainedAliasQualifierFact& prior =
			retained_alias_qualifier_facts_[i];
		if (prior.owner == owner && prior.alias_source == alias_source &&
			prior.qualifier_source == qualifier_source &&
			prior.pattern == pattern) return;
	}
	retained_alias_qualifier_facts_.push_back(RetainedAliasQualifierFact(
		owner, alias_source, qualifier_source, pattern));
}

void TemplateWitnessObserver::ResolveRetainedAliasQualifierPartial(
	syntax::NodeId owner, std::uint32_t pattern,
	std::uint32_t partial_pattern)
{
	for (std::size_t i = 0; i < retained_alias_qualifier_facts_.size(); ++i)
	{
		RetainedAliasQualifierFact& fact = retained_alias_qualifier_facts_[i];
		if (fact.owner == owner && fact.pattern == pattern)
			fact.partial_pattern = partial_pattern;
	}
}

void TemplateWitnessObserver::RecordRetainedMemberClassUses(
	NameId member_name, std::uint32_t pattern, BindingId specialization,
	std::uint32_t selected_partial,
	const std::vector<TemplateArgument>& arguments)
{
	for (std::size_t i = 0; i < retained_member_source_facts_.size(); ++i)
	{
		const RetainedMemberSourceFact& member =
			retained_member_source_facts_[i];
		if (member.member_name != member_name || member.pattern != pattern ||
			member.partial_pattern != selected_partial ||
			(member.concrete_owner != kNoBinding &&
			 member.concrete_owner != specialization)) continue;
		if (selected_partial == kNoDumpEdge)
			RecordInstantiatedClassUse(member.source, pattern,
				specialization, arguments, arguments.size());
		for (std::size_t fact_index = 0;
			fact_index < semantic_source_facts_.size(); ++fact_index)
		{
			const SemanticSourceFact& fact =
				semantic_source_facts_[fact_index];
			if (fact.owner != member.owner ||
				(fact.kind != SEMANTIC_SOURCE_CLASS_TEMPLATE &&
				 fact.kind != SEMANTIC_SOURCE_CLASS_OBJECT_TYPE) ||
				fact.semantic_index != pattern ||
				fact.resolution == SEMANTIC_SOURCE_CURRENT_PARTIAL) continue;
			if (fact.kind == SEMANTIC_SOURCE_CLASS_OBJECT_TYPE)
				RecordDeducedClassUse(fact.syntax, pattern,
					specialization, arguments);
			else RecordInstantiatedClassUse(fact.syntax, pattern,
				specialization, arguments, fact.explicit_count);
		}
	}
}

void TemplateWitnessObserver::RecordSemanticCurrentClassUses(
	syntax::NodeId owner, std::uint32_t pattern,
	const std::vector<TemplateArgument>& arguments)
{
	for (std::size_t i = 0; i < semantic_source_facts_.size(); ++i)
	{
		const SemanticSourceFact& fact = semantic_source_facts_[i];
		if (fact.owner != owner || fact.semantic_index != pattern ||
			fact.kind != SEMANTIC_SOURCE_CLASS_TEMPLATE ||
			fact.resolution != SEMANTIC_SOURCE_CURRENT_PARTIAL)
			continue;
		bool duplicate = false;
		for (std::size_t event = 0; event < source_events_.size(); ++event)
			if (source_events_[event].kind == SOURCE_CLASS_USE &&
				source_events_[event].syntax == fact.syntax &&
				source_events_[event].pattern == pattern)
				duplicate = true;
		if (!duplicate)
			RecordCurrentClassUse(fact.syntax, pattern, kNoBinding,
				arguments, fact.explicit_count);
	}
}

void TemplateWitnessObserver::RecordClassUse(syntax::NodeId syntax,
	std::uint32_t pattern, BindingId binding,
	const std::vector<TemplateArgument>& arguments, std::size_t explicit_count,
	std::size_t source_column_offset, bool replayed)
{
	const bool resolved = std::find(resolved_source_uses_.begin(),
		resolved_source_uses_.end(), syntax) != resolved_source_uses_.end();
	const bool dependent_source = std::find(dependent_source_uses_.begin(),
		dependent_source_uses_.end(), syntax) != dependent_source_uses_.end();
	const bool dependent_class = std::find(dependent_class_uses_.begin(),
		dependent_class_uses_.end(), std::make_pair(syntax, pattern)) !=
		dependent_class_uses_.end();
	if (replayed && !resolved) return;
	if (!resolved && (dependent_source || dependent_class)) return;
	source_events_.push_back(SourceEvent(SOURCE_CLASS_USE, syntax, pattern,
		binding, arguments, explicit_count, source_column_offset,
		next_insertion_ordinal_++));
	source_events_.back().allow_substituted_source = resolved;
}

void TemplateWitnessObserver::NoteDependentSourceUse(syntax::NodeId syntax)
{
	if (syntax == syntax::kNoNode) return;
	if (std::find(dependent_source_uses_.begin(), dependent_source_uses_.end(),
		syntax) == dependent_source_uses_.end())
		dependent_source_uses_.push_back(syntax);
	for (std::size_t i = 0; i < source_events_.size(); ++i)
		if (source_events_[i].kind == SOURCE_CLASS_USE &&
			source_events_[i].syntax == syntax &&
			!source_events_[i].complete_at_source)
			source_events_[i].suppressed = true;
}

void TemplateWitnessObserver::NoteSourceOccurrence(
	syntax::NodeId syntax, SourceOccurrenceRole role)
{
	if (syntax == syntax::kNoNode) return;
	for (std::size_t i = 0; i < source_occurrences_.size(); ++i)
		if (source_occurrences_[i].syntax == syntax)
		{
			source_occurrences_[i].roles |= static_cast<std::uint8_t>(role);
			return;
		}
	source_occurrences_.push_back(SourceOccurrenceFact(syntax, role));
}

bool TemplateWitnessObserver::HasSourceOccurrenceRole(
	syntax::NodeId syntax, std::uint8_t roles) const
{
	for (std::size_t i = 0; i < source_occurrences_.size(); ++i)
		if (source_occurrences_[i].syntax == syntax)
			return (source_occurrences_[i].roles & roles) != 0;
	return false;
}

void TemplateWitnessObserver::NoteResolvedSourceUse(syntax::NodeId syntax)
{
	if (syntax == syntax::kNoNode) return;
	if (std::find(resolved_source_uses_.begin(), resolved_source_uses_.end(),
		syntax) == resolved_source_uses_.end())
		resolved_source_uses_.push_back(syntax);
	dependent_source_uses_.erase(std::remove(dependent_source_uses_.begin(),
		dependent_source_uses_.end(), syntax), dependent_source_uses_.end());
	for (std::size_t i = 0; i < source_events_.size(); ++i)
		if (source_events_[i].kind == SOURCE_CLASS_USE &&
			source_events_[i].syntax == syntax)
		{
			source_events_[i].suppressed = false;
			source_events_[i].allow_substituted_source = true;
		}
}

void TemplateWitnessObserver::RecordDeducedClassUse(syntax::NodeId syntax,
	std::uint32_t pattern, BindingId binding,
	const std::vector<TemplateArgument>& arguments, NameId source_name)
{
	if (std::find(dependent_source_uses_.begin(), dependent_source_uses_.end(),
		syntax) != dependent_source_uses_.end()) return;
	if (std::find(dependent_class_uses_.begin(), dependent_class_uses_.end(),
		std::make_pair(syntax, pattern)) != dependent_class_uses_.end()) return;
	source_events_.push_back(SourceEvent(SOURCE_CLASS_USE, syntax, pattern,
		binding, arguments, 0, 0, next_insertion_ordinal_++));
	source_events_.back().source_name = source_name;
	std::fill(source_events_.back().provenance.begin(),
		source_events_.back().provenance.end(), 1);
}

void TemplateWitnessObserver::NoteDependentClassUse(
	syntax::NodeId syntax, std::uint32_t pattern)
{
	const std::pair<syntax::NodeId, std::uint32_t> key(syntax, pattern);
	if (std::find(dependent_class_uses_.begin(), dependent_class_uses_.end(),
		key) == dependent_class_uses_.end())
		dependent_class_uses_.push_back(key);
	for (std::size_t i = 0; i < source_events_.size(); ++i)
		if (source_events_[i].kind == SOURCE_CLASS_USE &&
			source_events_[i].syntax == syntax &&
			source_events_[i].pattern == pattern &&
			!source_events_[i].complete_at_source)
			source_events_[i].suppressed = true;
}

void TemplateWitnessObserver::RecordInstantiatedClassUse(
	syntax::NodeId syntax, std::uint32_t pattern, BindingId binding,
	const std::vector<TemplateArgument>& arguments,
	std::size_t explicit_count)
{
	source_events_.push_back(SourceEvent(SOURCE_CLASS_USE, syntax, pattern,
		binding, arguments, explicit_count, 0, next_insertion_ordinal_++));
	source_events_.back().allow_substituted_source = true;
}

void TemplateWitnessObserver::RecordRetainedClassOwnerUse(
	syntax::NodeId syntax, std::uint32_t pattern,
	const std::vector<TemplateArgument>& arguments)
{
	if (syntax == syntax::kNoNode) return;
	source_events_.push_back(SourceEvent(SOURCE_CLASS_USE, syntax, pattern,
		kNoBinding, arguments, arguments.size(), 0,
		next_insertion_ordinal_++));
	SourceEvent& event = source_events_.back();
	event.allow_substituted_source = true;
	event.complete_at_source = true;
	event.selection_kind = 2;
}

void TemplateWitnessObserver::RecordCurrentClassUse(
	syntax::NodeId syntax, std::uint32_t pattern, BindingId binding,
	const std::vector<TemplateArgument>& arguments, std::size_t explicit_count)
{
	if (syntax == syntax::kNoNode) return;
	source_events_.push_back(SourceEvent(SOURCE_CLASS_USE, syntax, pattern,
		binding, arguments, explicit_count, 0, next_insertion_ordinal_++));
	SourceEvent& event = source_events_.back();
	event.allow_substituted_source = true;
	event.complete_at_source = true;
	event.selection_kind = 2;
}

void TemplateWitnessObserver::RecordAliasUse(syntax::NodeId syntax,
	std::uint32_t pattern, const std::vector<TemplateArgument>& arguments,
	std::size_t explicit_count, EntityId qualifier_entity,
	std::uint32_t alias_owner_pattern)
{
	if (std::find(dependent_source_uses_.begin(), dependent_source_uses_.end(),
		syntax) != dependent_source_uses_.end())
	{
		// Retained syntax supplies the written arguments while concrete replay
		// supplies the resolved alias identity.  Join those two authoritative
		// facts without replacing the source spelling with one replay's values.
		for (std::size_t i = 0; i < source_events_.size(); ++i)
			if (source_events_[i].kind == SOURCE_ALIAS_USE &&
				source_events_[i].syntax == syntax &&
				source_events_[i].pattern == pattern)
			{
				if (source_events_[i].qualifier_entity == kNoEntity)
					source_events_[i].qualifier_entity = qualifier_entity;
				ApplyRetainedAliasQualifier(
					&source_events_[i], alias_owner_pattern);
				return;
			}
		source_events_.push_back(SourceEvent(SOURCE_ALIAS_USE, syntax, pattern,
			kNoBinding, std::vector<TemplateArgument>(), 0, 0,
			next_insertion_ordinal_++));
		source_events_.back().allow_substituted_source = true;
		source_events_.back().qualifier_entity = qualifier_entity;
		ApplyRetainedAliasQualifier(
			&source_events_.back(), alias_owner_pattern);
		return;
	}
	if (std::find(dependent_alias_uses_.begin(), dependent_alias_uses_.end(),
		std::make_pair(syntax, pattern)) != dependent_alias_uses_.end()) return;
	for (std::size_t i = 0; i < source_events_.size(); ++i)
		if (source_events_[i].kind == SOURCE_ALIAS_USE &&
			source_events_[i].syntax == syntax &&
			source_events_[i].pattern == pattern &&
			source_events_[i].arguments.empty())
		{
			source_events_[i].arguments = arguments;
			if (source_events_[i].qualifier_entity == kNoEntity)
				source_events_[i].qualifier_entity = qualifier_entity;
			ApplyRetainedAliasQualifier(
				&source_events_[i], alias_owner_pattern);
			source_events_[i].provenance.assign(arguments.size(), 2);
			for (std::size_t argument = 0;
				argument < explicit_count && argument < arguments.size(); ++argument)
				source_events_[i].provenance[argument] = 0;
			return;
		}
	source_events_.push_back(SourceEvent(SOURCE_ALIAS_USE, syntax, pattern,
		kNoBinding, arguments, explicit_count, 0,
		next_insertion_ordinal_++));
	source_events_.back().qualifier_entity = qualifier_entity;
	ApplyRetainedAliasQualifier(&source_events_.back(), alias_owner_pattern);
}

void TemplateWitnessObserver::NoteDependentAliasUse(
	syntax::NodeId syntax, std::uint32_t pattern,
	const std::vector<TemplateArgument>& arguments,
	std::size_t explicit_count, EntityId qualifier_entity,
	std::uint32_t alias_owner_pattern)
{
	const std::pair<syntax::NodeId, std::uint32_t> key(syntax, pattern);
	if (std::find(dependent_alias_uses_.begin(), dependent_alias_uses_.end(),
		key) != dependent_alias_uses_.end()) return;
	dependent_alias_uses_.push_back(key);
	for (std::size_t i = 0; i < source_events_.size(); ++i)
		if (source_events_[i].kind == SOURCE_ALIAS_USE &&
			source_events_[i].syntax == syntax &&
			source_events_[i].pattern == pattern)
			source_events_[i].suppressed = true;
	source_events_.push_back(SourceEvent(SOURCE_ALIAS_USE, syntax, pattern,
		kNoBinding, arguments, explicit_count, 0, next_insertion_ordinal_++));
	source_events_.back().allow_substituted_source = true;
	source_events_.back().qualifier_entity = qualifier_entity;
	ApplyRetainedAliasQualifier(&source_events_.back(), alias_owner_pattern);
}

void TemplateWitnessObserver::RecordFunctionSpecialization(BindingId binding,
	std::uint32_t pattern,
	const std::vector<TemplateArgument>& arguments,
	const std::vector<TemplateArgument>& requested_arguments,
	const std::vector<std::uint32_t>& parameter_offsets)
{
	for (std::size_t i = 0; i < function_specializations_.size(); ++i)
		if (function_specializations_[i].binding == binding) return;
	function_specializations_.push_back(FunctionSpecializationFact(
		binding, pattern, arguments, requested_arguments, parameter_offsets));
}

void TemplateWitnessObserver::RecordClassSpecialization(BindingId binding,
	const std::vector<TemplateArgument>& arguments, std::size_t explicit_count)
{
	for (std::size_t i = 0; i < class_specializations_.size(); ++i)
		if (class_specializations_[i].binding == binding &&
			class_specializations_[i].arguments == arguments)
		{
			class_specializations_[i].explicit_count = std::min(
				class_specializations_[i].explicit_count, explicit_count);
			return;
		}
	class_specializations_.push_back(ClassSpecializationFact(
		binding, arguments, explicit_count));
}

void TemplateWitnessObserver::NoteClassTemplateSource(
	syntax::NodeId syntax, std::uint32_t pattern, BindingId binding,
	std::uint32_t selected_partial,
	std::size_t presentation_arity,
	const std::vector<syntax::NodeId>& argument_syntax, bool replayed)
{
	if (syntax == syntax::kNoNode || binding == kNoBinding) return;
	for (std::size_t i = 0; i < class_template_source_facts_.size(); ++i)
	{
		const ClassTemplateSourceFact& fact = class_template_source_facts_[i];
		if (fact.syntax == syntax && fact.pattern == pattern &&
			fact.binding == binding && fact.selected_partial == selected_partial &&
			fact.presentation_arity == presentation_arity &&
			fact.argument_syntax == argument_syntax && fact.replayed == replayed)
			return;
	}
	class_template_source_facts_.push_back(ClassTemplateSourceFact(
		syntax, pattern, binding, selected_partial, presentation_arity,
		argument_syntax, replayed));
}

void TemplateWitnessObserver::RecordVariableSpecialization(BindingId binding,
	std::uint32_t primary_pattern,
	const std::vector<TemplateArgument>& arguments,
	const std::vector<TemplateArgument>& specialization_arguments,
	const std::vector<std::uint32_t>& specialization_offsets,
	const std::vector<std::uint8_t>& specialization_packs,
	std::uint8_t selection_kind)
{
	for (std::size_t i = 0; i < variable_specializations_.size(); ++i)
		if (variable_specializations_[i].binding == binding) return;
	variable_specializations_.push_back(VariableSpecializationFact(
		binding, primary_pattern, arguments, specialization_arguments,
		specialization_offsets, specialization_packs, selection_kind));
}

void TemplateWitnessObserver::RecordVariableUse(syntax::NodeId syntax,
	BindingId binding, std::size_t explicit_count)
{
	for (std::size_t i = 0; i < variable_specializations_.size(); ++i)
	{
		const VariableSpecializationFact& fact = variable_specializations_[i];
		if (fact.binding != binding) continue;
		source_events_.push_back(SourceEvent(SOURCE_VARIABLE_USE, syntax,
			fact.primary_pattern, binding, fact.arguments, 0, 0,
			next_insertion_ordinal_++));
		SourceEvent& event = source_events_.back();
		event.selection_kind = fact.selection_kind;
		event.specialization_arguments = fact.specialization_arguments;
		event.specialization_offsets = fact.specialization_offsets;
		event.specialization_packs = fact.specialization_packs;
		for (std::size_t argument = 0;
			argument < explicit_count && argument < event.provenance.size();
			++argument)
			event.provenance[argument] = fact.selection_kind == 2 ? 0 : 1;
		return;
	}
}

void TemplateWitnessObserver::RecordFunctionCall(syntax::NodeId syntax,
	syntax::NodeId component_syntax, std::uint32_t pattern, BindingId binding,
	const std::vector<TemplateArgument>& arguments, std::size_t explicit_count,
	SourceOccurrenceRole role)
{
	const bool retained = HasSourceOccurrenceRole(component_syntax,
		SOURCE_OCCURRENCE_UNEVALUATED_DECLARATION |
		SOURCE_OCCURRENCE_DEFERRED_DEFINITION);
	const bool dependent = HasSourceOccurrenceRole(
		component_syntax, SOURCE_OCCURRENCE_DEPENDENT);
	NoteSourceOccurrence(component_syntax, role);
	if (retained && (role != SOURCE_OCCURRENCE_SPECIALIZATION_REPLAY ||
		dependent)) return;
	source_events_.push_back(SourceEvent(SOURCE_FUNCTION_CALL, syntax, pattern,
		binding, arguments, 0, 0, next_insertion_ordinal_++));
	SourceEvent& event = source_events_.back();
	event.component_syntax = component_syntax;
	for (std::size_t i = 0; i < function_specializations_.size(); ++i)
		if (function_specializations_[i].binding == binding &&
			function_specializations_[i].arguments == arguments)
		{
			event.provenance = function_specializations_[i].provenance;
			event.parameter_offsets =
				function_specializations_[i].parameter_offsets;
			break;
		}
	for (std::size_t i = 0; i < explicit_count &&
		i < event.provenance.size(); ++i)
		event.provenance[i] = 0;
	if (!event.parameter_offsets.empty() &&
		explicit_count + 1 >= event.parameter_offsets.size())
		std::fill(event.provenance.begin(), event.provenance.end(), 0);
	for (std::size_t i = 0; i < deduction_drops_.size(); ++i)
		if (!deduction_drops_[i].consumed &&
			deduction_drops_[i].syntax == syntax)
		{
			event.drops.push_back(SourceEvent::Drop(kNoBinding,
				deduction_drops_[i].pattern,
				CANDIDATE_ORIGIN_TEMPLATE_PATTERN,
				deduction_drops_[i].reason));
			deduction_drops_[i].consumed = true;
		}
	for (std::size_t i = 0; i < overload_selections_.size(); ++i)
		if (!overload_selections_[i].consumed &&
			overload_selections_[i].selected == binding)
		{
			event.drops.insert(event.drops.end(),
				overload_selections_[i].drops.begin(),
				overload_selections_[i].drops.end());
			overload_selections_[i].consumed = true;
			break;
	}
}

void TemplateWitnessObserver::RecordOverloadSelection(
	const Analyzer& analyzer, BindingId selected,
	const std::vector<BindingId>& candidates,
	const std::vector<std::uint8_t>& reasons)
{
	std::vector<SourceEvent::Drop> drops;
	for (std::size_t i = 0; i < candidates.size() && i < reasons.size(); ++i)
		if (reasons[i] != 0)
		{
			std::uint32_t declaration = kNoBinding;
			CandidateDeclarationOriginKind origin_kind =
				CANDIDATE_ORIGIN_BINDING;
			if (candidates[i] < analyzer.program_->bindings.size())
			{
				const BindingRecord& visible =
					analyzer.program_->bindings[candidates[i]];
				const BindingId canonical =
					visible.canonical;
				declaration = canonical;
				if (canonical < analyzer.program_->bindings.size())
				{
					const FunctionInfo& function = analyzer.GetFunction(canonical);
					if (function.template_pattern != kNoDumpEdge)
					{
						declaration = function.template_pattern;
						origin_kind = CANDIDATE_ORIGIN_TEMPLATE_PATTERN;
					}
					else if (function.implicit_special_member)
					{
						const bool assignment =
							function.special_member == SPECIAL_MEMBER_COPY_ASSIGNMENT ||
							function.special_member == SPECIAL_MEMBER_MOVE_ASSIGNMENT;
						declaration = visible.member_owner;
						origin_kind = assignment ?
							CANDIDATE_ORIGIN_IMPLICIT_ASSIGNMENT_FAMILY :
							CANDIDATE_ORIGIN_IMPLICIT_CONSTRUCTOR_FAMILY;
					}
				}
			}
			const SourceEvent::Drop drop(
				candidates[i], declaration, origin_kind, reasons[i]);
			bool duplicate = false;
			for (std::size_t prior = 0; prior < drops.size(); ++prior)
				if (drops[prior].binding == drop.binding &&
					drops[prior].reason == drop.reason) duplicate = true;
			if (!duplicate) drops.push_back(drop);
		}
	if (!drops.empty())
		overload_selections_.push_back(
			OverloadSelectionFact(selected, drops));
}

void TemplateWitnessObserver::RecordDeductionDrop(NodeId syntax,
	std::uint32_t pattern, std::uint8_t reason)
{
	if (syntax == kNoNode || reason == OVERLOAD_DROP_NONE) return;
	deduction_drops_.push_back(DeductionDropFact(syntax, pattern, reason));
}

void TemplateWitnessObserver::DiscardOverloadSelection(BindingId selected)
{
	for (std::size_t i = overload_selections_.size(); i != 0; --i)
		if (!overload_selections_[i - 1].consumed &&
			overload_selections_[i - 1].selected == selected)
		{
			overload_selections_.erase(overload_selections_.begin() + i - 1);
			return;
		}
}

void TemplateWitnessObserver::RecordFunctionInstantiation(BindingId binding)
{
	if (std::find(function_instantiations_.begin(),
		function_instantiations_.end(), binding) == function_instantiations_.end())
		function_instantiations_.push_back(binding);
}

void TemplateWitnessObserver::RecordRequireDefinition(BindingId binding)
{
	if (std::find(required_definitions_.begin(), required_definitions_.end(),
		binding) == required_definitions_.end())
		required_definitions_.push_back(binding);
}

void TemplateWitnessObserver::RecordClassInstantiation(EntityId entity)
{
	if (std::find(class_instantiations_.begin(), class_instantiations_.end(),
		entity) == class_instantiations_.end())
		class_instantiations_.push_back(entity);
}

void TemplateWitnessObserver::RecordClassFinalization(EntityId entity)
{
	if (std::find(class_finalizations_.begin(), class_finalizations_.end(),
		entity) == class_finalizations_.end())
		class_finalizations_.push_back(entity);
}

void TemplateWitnessObserver::RecordVariableInstantiation(BindingId binding)
{
	for (std::size_t i = 0; i < variable_specializations_.size(); ++i)
		if (variable_specializations_[i].binding == binding &&
			variable_specializations_[i].selection_kind == 2)
			return;
	if (std::find(variable_instantiations_.begin(),
		variable_instantiations_.end(), binding) == variable_instantiations_.end())
		variable_instantiations_.push_back(binding);
}

void TemplateWitnessObserver::RecordVariableOccurrence(
	const syntax::SyntaxArena& arena, syntax::NodeId syntax, BindingId binding,
	bool owning_class_context, VariableOccurrenceEvaluation evaluation,
	VariableOccurrencePhase phase, std::uint8_t properties,
	bool filter_publication_to_source)
{
	if (syntax == syntax::kNoNode) return;
	bool duplicate = false;
	for (std::size_t i = 0; i < variable_occurrences_.size(); ++i)
		if (variable_occurrences_[i].syntax == syntax &&
			variable_occurrences_[i].binding == binding &&
			variable_occurrences_[i].evaluation == evaluation &&
			variable_occurrences_[i].phase == phase &&
			variable_occurrences_[i].properties == properties &&
			variable_occurrences_[i].owning_class_context ==
				(owning_class_context ? 1 : 0)) duplicate = true;
	if (!duplicate) variable_occurrences_.push_back(VariableOccurrenceFact(
		syntax, binding, evaluation, phase, owning_class_context, properties));
	if (!filter_publication_to_source)
	{
		RecordVariableInstantiation(binding);
		return;
	}
	const std::size_t token = arena.TokenFirst(syntax);
	if (token >= arena.TokenCount() ||
		arena.TokenSourceFile(token) != primary_source_file_ ||
		(owning_class_context &&
		 (IsWithinSyntaxTag(arena, syntax,
			::cppgm::syntax::STAG_FUNCTION_DEFINITION) ||
		  IsWithinSyntaxTag(arena, syntax,
			::cppgm::syntax::STAG_SPECIAL_MEMBER_DEFINITION))))
		return;
	RecordVariableInstantiation(binding);
}

std::size_t TemplateWitnessObserver::SourceEventMark() const
{
	return source_events_.size();
}

void TemplateWitnessObserver::DiscardSourceEvents(std::size_t mark)
{
	if (mark < source_events_.size())
		source_events_.erase(source_events_.begin() + mark,
			source_events_.end());
}

std::size_t TemplateWitnessObserver::ClosureEventMark() const
{
	return required_definitions_.size();
}

void TemplateWitnessObserver::DiscardRequireDefinitionEvents(std::size_t mark)
{
	if (mark < required_definitions_.size())
		required_definitions_.erase(required_definitions_.begin() + mark,
			required_definitions_.end());
}

std::string TemplateWitnessObserver::ElideEntities(std::string spelling,
	const EntityReplacements& replacements) const
{
	spelling = NormalizeWitnessTypeSpelling(spelling);
	for (std::size_t pass = 0; pass <= replacements.size(); ++pass)
	{
		const std::string before = spelling;
		for (std::size_t i = 0; i < replacements.size(); ++i)
		{
			if (replacements[i].cross_entity_conflict) continue;
			ReplaceAll(&spelling, replacements[i].canonical,
				replacements[i].presented);
		}
		if (before == spelling) break;
	}
	return spelling;
}

std::string TemplateWitnessObserver::NormalizeEntity(std::string spelling,
	const EntityReplacements& replacements) const
{
	spelling = ElideEntities(spelling, replacements);
	ReplaceAll(&spelling, "unsigned int", "unsigned");
	ReplaceAll(&spelling, "signed int", "int");
	return spelling;
}

std::string TemplateWitnessObserver::OverloadName(const Analyzer& analyzer,
	const SourceEvent::Drop& drop,
	const EntityReplacements& replacements) const
{
	const presentation::TemplateArgumentElision argument_elision(
		entity_argument_limits_);
	const BindingId binding = drop.binding;
	if (binding == kNoBinding)
	{
		if (drop.origin_kind != CANDIDATE_ORIGIN_TEMPLATE_PATTERN ||
			drop.declaration_origin >= analyzer.function_templates_.size())
			return std::string();
		const FunctionTemplatePattern& pattern =
			analyzer.function_templates_[drop.declaration_origin];
		return NormalizeEntity(presentation::RenderName(
			*analyzer.program_, pattern.owner, pattern.name), replacements);
	}
	if (binding >= analyzer.program_->bindings.size()) return std::string();
	const BindingRecord& visible = analyzer.program_->bindings[binding];
	const BindingId canonical = visible.canonical;
	const FunctionInfo& function = analyzer.GetFunction(canonical);
	const NameId terminal = function.presentation_name_override != 0 ?
		function.presentation_name_override :
		visible.presentation_name_override != 0 ?
			visible.presentation_name_override : visible.name;
	std::string result;
	if (visible.member_owner != kNoEntity)
		result = presentation::RenderEntity(
			*analyzer.program_, visible.member_owner,
			argument_elision, true) + "::" +
			analyzer.program_->names.Get(terminal);
	else result = presentation::RenderName(
		*analyzer.program_, visible.owner, terminal);
	return NormalizeEntity(result, replacements);
}

std::string TemplateWitnessObserver::FunctionContextName(
	const Analyzer& analyzer, BindingId binding) const
{
	const presentation::TemplateArgumentElision argument_elision(
		entity_argument_limits_);
	binding = analyzer.program_->bindings[binding].canonical;
	const FunctionInfo& function = analyzer.GetFunction(binding);
	const BindingRecord& record = analyzer.program_->bindings[binding];
	const NameId terminal = function.presentation_name_override != 0 ?
		function.presentation_name_override :
		record.presentation_name_override != 0 ?
			record.presentation_name_override : record.name;
	std::string result = record.member_owner == kNoEntity ?
		presentation::RenderName(*analyzer.program_, record.owner, terminal) :
		presentation::RenderEntity(
			*analyzer.program_, record.member_owner,
			argument_elision, true) + "::" +
			analyzer.program_->names.Get(terminal);
	const TypeRecord& type = analyzer.program_->types.Get(function.type);
	result += '(';
	const TypeId* parameters = analyzer.program_->types.Parameters(function.type);
	for (std::size_t i = 0; i < type.parameter_count; ++i)
	{
		if (i != 0) result += ", ";
		result += NormalizeWitnessTypeSpelling(
			presentation::RenderType(*analyzer.program_, parameters[i]));
	}
	if (type.variadic)
	{
		if (type.parameter_count != 0) result += ", ";
		result += "...";
	}
	return result + ')';
}

bool TemplateWitnessObserver::EntityHasTemplateContext(
	const Analyzer& analyzer, EntityId entity) const
{
	for (std::size_t depth = 0; entity != kNoEntity &&
		depth < analyzer.program_->entities.size(); ++depth)
	{
		const EntityRecord& record = analyzer.program_->entities[entity];
		if (record.template_argument_begin != kNoBinding) return true;
		if (record.local_context != kNoBinding &&
			analyzer.GetFunction(record.local_context).template_specialization)
			return true;
		entity = record.enclosing_class;
	}
	return false;
}

bool TemplateWitnessObserver::IsTemplateMarker(
	const Analyzer& analyzer, EntityId entity) const
{
	if (entity >= analyzer.class_template_pattern_by_entity_.size()) return false;
	const std::uint32_t pattern =
		analyzer.class_template_pattern_by_entity_[entity];
	return pattern != kNoDumpEdge && pattern < analyzer.class_templates_.size() &&
		analyzer.class_templates_[pattern].marker_entity == entity;
}

syntax::NodeId TemplateWitnessObserver::GeneratedSourceNode(
	const Analyzer& analyzer, EntityId entity) const
{
	for (std::size_t i = 0; i < analyzer.generated_type_identities_.size(); ++i)
		if (analyzer.generated_type_identities_[i].entity == entity)
			return analyzer.generated_type_identities_[i].node;
	return syntax::kNoNode;
}

bool TemplateWitnessObserver::GeneratedHasDeclarator(
	const SyntaxArena& arena, syntax::NodeId node) const
{
	if (node == syntax::kNoNode) return false;
	std::size_t best_distance = std::numeric_limits<std::size_t>::max();
	bool result = false;
	for (syntax::NodeId candidate = 0; candidate < arena.Nodes(); ++candidate)
	{
		if (!arena.IsTag(candidate,
			::cppgm::syntax::STAG_SIMPLE_DECLARATION)) continue;
		const std::size_t distance = DescendantDistance(arena, candidate, node);
		if (distance >= best_distance) continue;
		best_distance = distance;
		const syntax::NodeId list = arena.FindDirectChildTag(candidate,
			::cppgm::syntax::STAG_INIT_DECLARATOR_LIST);
		result = list != syntax::kNoNode &&
			arena.FirstEdge(list) != syntax::kNoEdge;
	}
	return result;
}

std::string TemplateWitnessObserver::GeneratedLabel(const Analyzer& analyzer,
	const SyntaxArena& arena, EntityId entity, syntax::NodeId node,
	bool location) const
{
	const EntityRecord& record = analyzer.program_->entities[entity];
	std::string result = GeneratedHasDeclarator(arena, node) ?
		"(unnamed " : "(anonymous ";
	result += record.flavor == NAMED_UNION ? "union" :
		record.flavor == NAMED_CLASS ? "class" : "struct";
	if (location)
		result += " at " + std::to_string(arena.SourceLine(node)) + ':' +
			std::to_string(arena.SourceColumn(node));
	return result + ')';
}

std::string TemplateWitnessObserver::ClassEntityName(const Analyzer& analyzer,
	const SyntaxArena& arena, EntityId entity) const
{
	const presentation::TemplateArgumentElision argument_elision(
		entity_argument_limits_);
	const syntax::NodeId leaf_node = GeneratedSourceNode(analyzer, entity);
	if (leaf_node == syntax::kNoNode)
		return presentation::RenderEntity(
			*analyzer.program_, entity, argument_elision, true);
	std::vector<std::pair<EntityId, syntax::NodeId> > chain;
	EntityId current = entity;
	while (current != kNoEntity)
	{
		const syntax::NodeId node = GeneratedSourceNode(analyzer, current);
		if (node == syntax::kNoNode) break;
		chain.push_back(std::make_pair(current, node));
		current = analyzer.program_->entities[current].enclosing_class;
	}
	std::string result;
	const EntityRecord& leaf = analyzer.program_->entities[entity];
	if (chain.size() > 1)
	{
		if (leaf.local_context != kNoBinding)
			result = FunctionContextName(analyzer, leaf.local_context);
		else if (current != kNoEntity)
			result = presentation::RenderEntity(
				*analyzer.program_, current, argument_elision, true);
		for (std::size_t i = chain.size(); i > 1; --i)
		{
			if (!result.empty()) result += "::";
			result += GeneratedLabel(analyzer, arena,
				chain[i - 1].first, chain[i - 1].second, false);
		}
	}
	else if (!GeneratedHasDeclarator(arena, leaf_node))
	{
		if (leaf.enclosing_class != kNoEntity)
			result = presentation::RenderEntity(
				*analyzer.program_, leaf.enclosing_class,
				argument_elision, true);
		else if (leaf.local_context != kNoBinding)
			result = FunctionContextName(analyzer, leaf.local_context);
	}
	if (!result.empty()) result += "::";
	return result + GeneratedLabel(analyzer, arena, entity, leaf_node, true);
}

bool TemplateWitnessObserver::OwnerIsExplicitSpecialization(
	const Analyzer& analyzer, EntityId owner) const
{
	if (owner == kNoEntity || owner >= analyzer.program_->entities.size())
		return false;
	const BindingId declaration = analyzer.program_->entities[owner].declaration;
	return declaration != kNoBinding && declaration <
		analyzer.class_template_explicit_specialization_states_.size() &&
		analyzer.class_template_explicit_specialization_states_[declaration] != 0;
}

bool TemplateWitnessObserver::IsTemplateFunction(
	const Analyzer& analyzer, BindingId binding) const
{
	const TemplateFunctionLifecycleFact lifecycle =
		analyzer.InspectTemplateFunctionLifecycle(binding);
	if (lifecycle.binding == kNoBinding ||
		lifecycle.Has(TEMPLATE_FUNCTION_EXPLICIT_INSTANTIATION_SUPPRESSED) ||
		lifecycle.Has(TEMPLATE_FUNCTION_EXPLICIT_SPECIALIZATION) ||
		(lifecycle.Has(TEMPLATE_FUNCTION_OWNER_EXPLICIT_SPECIALIZATION) &&
		 !lifecycle.Has(TEMPLATE_FUNCTION_DIRECT_SPECIALIZATION))) return false;
	if ((lifecycle.Has(TEMPLATE_FUNCTION_COMPILER_GENERATED) ||
		 lifecycle.Has(TEMPLATE_FUNCTION_INHERITED_CONSTRUCTOR)) &&
		!lifecycle.Has(TEMPLATE_FUNCTION_LOCAL_CONTEXT)) return false;
	if (lifecycle.owner_explicit_instantiation_state == 1 &&
		lifecycle.definition_state == FUNCTION_DEFINITION_COMPLETE &&
		lifecycle.Has(TEMPLATE_FUNCTION_INLINE) &&
		lifecycle.Has(TEMPLATE_FUNCTION_DEFAULTED) &&
		!lifecycle.Has(TEMPLATE_FUNCTION_OBJECT_OUTPUT_ROOT)) return false;
	return lifecycle.Has(TEMPLATE_FUNCTION_DIRECT_SPECIALIZATION) ||
		lifecycle.Has(TEMPLATE_FUNCTION_OWNER_CONTEXT) ||
		lifecycle.Has(TEMPLATE_FUNCTION_FRIEND_CONTEXT);
}

bool TemplateWitnessObserver::IsRequiredTemplateFunction(
	const Analyzer& analyzer, BindingId binding) const
{
	const TemplateFunctionLifecycleFact lifecycle =
		analyzer.InspectTemplateFunctionLifecycle(binding);
	if (lifecycle.binding == kNoBinding ||
		lifecycle.Has(TEMPLATE_FUNCTION_EXPLICIT_INSTANTIATION_SUPPRESSED) ||
		lifecycle.Has(TEMPLATE_FUNCTION_EXPLICIT_SPECIALIZATION) ||
		(lifecycle.Has(TEMPLATE_FUNCTION_OWNER_EXPLICIT_SPECIALIZATION) &&
		 !lifecycle.Has(TEMPLATE_FUNCTION_DIRECT_SPECIALIZATION)) ||
		lifecycle.Has(TEMPLATE_FUNCTION_COMPILER_GENERATED) ||
		lifecycle.Has(TEMPLATE_FUNCTION_INHERITED_CONSTRUCTOR)) return false;
	if (lifecycle.Has(TEMPLATE_FUNCTION_DIRECT_SPECIALIZATION) ||
		lifecycle.Has(TEMPLATE_FUNCTION_FRIEND_CONTEXT)) return true;
	if ((lifecycle.owner_explicit_instantiation_state & 2) != 0) return false;
	return lifecycle.Has(TEMPLATE_FUNCTION_OWNER_ARGUMENT_CONTEXT);
}

std::string TemplateWitnessObserver::FunctionEntityName(
	const Analyzer& analyzer, BindingId binding) const
{
	const presentation::TemplateArgumentElision argument_elision(
		entity_argument_limits_);
	binding = analyzer.program_->bindings[binding].canonical;
	const FunctionInfo& function = analyzer.GetFunction(binding);
	const BindingRecord& record = analyzer.program_->bindings[binding];
	const NameId terminal = function.presentation_name_override != 0 ?
		function.presentation_name_override :
		record.presentation_name_override != 0 ?
			record.presentation_name_override : record.name;
	std::string result;
	if (record.member_owner != kNoEntity)
	{
		const EntityRecord& owner =
			analyzer.program_->entities[record.member_owner];
		if (owner.local_context != kNoBinding)
		{
			result = FunctionContextName(analyzer, owner.local_context) + "::";
			result += analyzer.program_->names.Get(owner.identity_name == 0 ?
				owner.emission_name : owner.identity_name);
		}
		else result = presentation::RenderEntity(
			*analyzer.program_, record.member_owner, argument_elision, true);
		result += "::";
		if (function.conversion_function)
		{
			std::string target = NormalizeWitnessTypeSpelling(
				presentation::RenderType(
					*analyzer.program_, function.conversion_target));
			std::string prefix;
			if (target.compare(0, 6, "const ") == 0)
			{
				prefix = "const ";
				target.erase(0, 6);
			}
			const TypeId target_type = analyzer.program_->types.RemoveTopCv(
				function.conversion_target);
			const TypeRecord& target_record =
				analyzer.program_->types.Get(target_type);
			if (target_record.kind == TYPE_NAMED &&
				target_record.entity <
					analyzer.class_template_pattern_by_entity_.size())
			{
				const std::uint32_t target_pattern =
					analyzer.class_template_pattern_by_entity_[target_record.entity];
				if (target_pattern < analyzer.class_templates_.size())
					target = analyzer.program_->names.Get(
						analyzer.class_templates_[target_pattern].name);
			}
			result += prefix + "operator " + target;
		}
		else result += analyzer.program_->names.Get(terminal);
	}
	else result = presentation::RenderName(
		*analyzer.program_, record.owner, terminal);
	return NormalizeWitnessTypeSpelling(result);
}

std::string TemplateWitnessObserver::SourceDistinguishedClassName(
	const Analyzer& analyzer, const SyntaxArena& arena, EntityId entity) const
{
	const presentation::TemplateArgumentElision argument_elision(
		entity_argument_limits_);
	const std::string canonical = presentation::RenderEntity(
		*analyzer.program_, entity, argument_elision, true);
	const std::size_t opening = FinalTemplateOpening(canonical);
	if (opening == std::string::npos) return canonical;
	for (std::size_t i = 0; i < source_events_.size(); ++i)
	{
		const SourceEvent& event = source_events_[i];
		if (event.kind != SOURCE_CLASS_USE || event.suppressed ||
			event.binding == kNoBinding ||
			event.binding >= analyzer.program_->bindings.size() ||
			analyzer.EntityOf(analyzer.program_->bindings[event.binding].type) !=
				entity || event.pattern >= analyzer.class_templates_.size())
			continue;
		std::vector<std::string> spellings;
		CollectExplicitTemplateArgumentSpellings(arena, event.component_syntax,
			analyzer.program_->names.Get(
				analyzer.class_templates_[event.pattern].name),
			event.source_token, &spellings);
		bool distinguished = false;
		std::string presented = canonical.substr(0, opening + 1);
		for (std::size_t argument = 0; argument < event.arguments.size();
			++argument)
		{
			if (argument != 0) presented += ", ";
			const std::string ordinary = RenderWitnessArgument(
				*analyzer.program_, event.arguments[argument], &argument_elision,
				true);
			const std::string source = argument < spellings.size() ?
				RenderFunctionTypeSourceIdentity(*analyzer.program_,
					event.arguments[argument], spellings[argument],
					&argument_elision, true) : ordinary;
			if (source != ordinary) distinguished = true;
			presented += source;
		}
		presented += '>';
		if (distinguished) return presented;
	}
	return canonical;
}

void TemplateWitnessObserver::PrepareSourceEvents(const Analyzer& analyzer,
	EntityReplacements* replacements)
{
	const SyntaxArena& arena = *analyzer.arena_;
	EntityReplacements& entity_replacements = *replacements;
	entity_argument_limits_.clear();
	for (std::size_t i = 0; i < class_specializations_.size(); ++i)
	{
		const ClassSpecializationFact& fact = class_specializations_[i];
		if (fact.binding >= analyzer.program_->bindings.size()) continue;
		const EntityId entity = analyzer.EntityOf(
			analyzer.program_->bindings[fact.binding].type);
		if (entity == kNoEntity || entity >= analyzer.program_->entities.size())
			continue;
		const EntityRecord& record = analyzer.program_->entities[entity];
		const std::size_t first = record.template_argument_begin;
		const std::size_t count = record.template_argument_count;
		if (fact.explicit_count >= count || fact.arguments.size() != count ||
			first > analyzer.program_->canonical_template_arguments.size() ||
			count > analyzer.program_->canonical_template_arguments.size() - first)
			continue;
		bool same = true;
		for (std::size_t argument = 0; argument < count; ++argument)
			if (!(fact.arguments[argument] ==
				analyzer.program_->canonical_template_arguments[first + argument]))
				same = false;
		if (!same) continue;
		if (entity < analyzer.class_template_pattern_by_entity_.size())
		{
			const std::uint32_t pattern =
				analyzer.class_template_pattern_by_entity_[entity];
			if (pattern < analyzer.class_templates_.size() &&
				HasTrailingTemplateParameterPack(
					analyzer.class_templates_[pattern].parameters))
				continue;
		}
		bool found = false;
		for (std::size_t limit = 0;
			limit < entity_argument_limits_.size(); ++limit)
			if (entity_argument_limits_[limit].entity == entity)
			{
				entity_argument_limits_[limit].count = std::min(
					entity_argument_limits_[limit].count,
					fact.explicit_count);
				found = true;
				break;
			}
		if (!found) entity_argument_limits_.push_back(
			presentation::TemplateEntityArgumentLimit(
				entity, fact.explicit_count));
	}
	for (std::size_t i = 0; i < class_template_source_facts_.size(); ++i)
	{
		const ClassTemplateSourceFact& fact = class_template_source_facts_[i];
		if (fact.binding >= analyzer.program_->bindings.size() ||
			fact.pattern >= analyzer.class_templates_.size() ||
			HasTrailingTemplateParameterPack(
				analyzer.class_templates_[fact.pattern].parameters)) continue;
		const EntityId entity = analyzer.EntityOf(
			analyzer.program_->bindings[fact.binding].type);
		if (entity == kNoEntity || entity >= analyzer.program_->entities.size())
			continue;
		const std::size_t argument_count =
			analyzer.program_->entities[entity].template_argument_count;
		if (fact.presentation_arity >= argument_count) continue;
		bool found = false;
		for (std::size_t limit = 0;
			limit < entity_argument_limits_.size(); ++limit)
			if (entity_argument_limits_[limit].entity == entity)
			{
				entity_argument_limits_[limit].count = std::min(
					entity_argument_limits_[limit].count,
					static_cast<std::size_t>(fact.presentation_arity));
				found = true;
				break;
			}
		if (!found) entity_argument_limits_.push_back(
			presentation::TemplateEntityArgumentLimit(
				entity, fact.presentation_arity));
	}
	for (std::size_t i = 0; i < class_specializations_.size(); ++i)
	{
		const ClassSpecializationFact& fact = class_specializations_[i];
		if (fact.binding >= analyzer.program_->bindings.size() ||
			fact.explicit_count >= fact.arguments.size()) continue;
		const EntityId entity = analyzer.EntityOf(
			analyzer.program_->bindings[fact.binding].type);
		if (entity == kNoEntity) continue;
		if (entity < analyzer.class_template_pattern_by_entity_.size())
		{
			const std::uint32_t pattern =
				analyzer.class_template_pattern_by_entity_[entity];
			if (pattern != kNoDumpEdge &&
				pattern < analyzer.class_templates_.size() &&
				HasTrailingTemplateParameterPack(
					analyzer.class_templates_[pattern].parameters))
				continue;
		}
		const std::string full = NormalizeWitnessTypeSpelling(
			presentation::RenderEntity(*analyzer.program_, entity, true));
		const std::size_t opening = FinalTemplateOpening(full);
		if (opening == std::string::npos) continue;
		std::string elided = full.substr(0, opening + 1);
		for (std::size_t argument = 0;
			argument < fact.explicit_count; ++argument)
		{
			if (argument != 0) elided += ", ";
			elided += RenderWitnessArgument(
				*analyzer.program_, fact.arguments[argument], 0, true);
		}
		elided += '>';
		if (full != elided)
			entity_replacements.push_back(
				EntityReplacement(entity, full, elided));
	}
	for (std::size_t i = 0; i < source_events_.size(); ++i)
	{
		SourceEvent& event = source_events_[i];
		if (event.kind != SOURCE_CLASS_USE || event.binding == kNoBinding ||
			event.binding >= analyzer.program_->bindings.size()) continue;
		const EntityId entity = analyzer.EntityOf(
			analyzer.program_->bindings[event.binding].type);
		if (entity == kNoEntity) continue;
		const std::string full = NormalizeWitnessTypeSpelling(
			presentation::RenderEntity(*analyzer.program_, entity, true));
		const std::size_t opening = FinalTemplateOpening(full);
		if (opening == std::string::npos) continue;
		std::vector<syntax::NodeId> argument_syntax;
		CollectExplicitTemplateArgumentSyntax(
			arena, event.component_syntax, &argument_syntax);
		std::string presented = full.substr(0, opening + 1);
		for (std::size_t argument = 0;
			argument < event.arguments.size(); ++argument)
		{
			if (argument != 0) presented += ", ";
			presented += RenderWitnessArgumentAtSource(*analyzer.program_, arena,
				event.arguments[argument], argument < argument_syntax.size() ?
					argument_syntax[argument] : syntax::kNoNode, false, 0, true);
		}
		presented += '>';
		if (full != presented)
			entity_replacements.push_back(
				EntityReplacement(entity, full, presented));
	}
	for (std::size_t left = 0; left < entity_replacements.size(); ++left)
		for (std::size_t right = left + 1;
			right < entity_replacements.size(); ++right)
			if (entity_replacements[left].entity !=
					entity_replacements[right].entity &&
				entity_replacements[left].canonical ==
					entity_replacements[right].canonical &&
				entity_replacements[left].presented !=
					entity_replacements[right].presented)
			{
				entity_replacements[left].cross_entity_conflict = true;
				entity_replacements[right].cross_entity_conflict = true;
			}
	std::sort(entity_replacements.begin(), entity_replacements.end(),
		[](const EntityReplacement& left,
			const EntityReplacement& right) {
			return left.canonical.size() > right.canonical.size();
		});
	for (std::size_t event = 0; event < source_events_.size(); ++event)
		if (source_events_[event].kind == SOURCE_CLASS_USE &&
			source_events_[event].binding != kNoBinding &&
			source_events_[event].binding <
				analyzer.program_->bindings.size())
			for (std::size_t fact = 0; fact < class_specializations_.size(); ++fact)
				if (analyzer.program_->bindings[
						source_events_[event].binding].canonical ==
					class_specializations_[fact].binding &&
					source_events_[event].arguments ==
					class_specializations_[fact].arguments)
					for (std::size_t argument =
						class_specializations_[fact].explicit_count;
						argument < source_events_[event].provenance.size();
						++argument)
						source_events_[event].provenance[argument] = 2;
	for (std::size_t event = 0; event < source_events_.size(); ++event)
	{
		SourceEvent& source_event = source_events_[event];
		if (source_event.kind != SOURCE_CLASS_USE ||
			source_event.binding == kNoBinding ||
			source_event.binding >= analyzer.program_->bindings.size()) continue;
		const BindingId canonical =
			analyzer.program_->bindings[source_event.binding].canonical;
		for (std::size_t fact = 0;
			fact < class_template_source_facts_.size(); ++fact)
		{
			const ClassTemplateSourceFact& source_fact =
				class_template_source_facts_[fact];
			if (source_fact.syntax != source_event.syntax ||
				source_fact.pattern != source_event.pattern ||
				source_fact.binding != canonical) continue;
			for (std::size_t argument = source_fact.presentation_arity;
				argument < source_event.provenance.size(); ++argument)
				source_event.provenance[argument] = 2;
		}
	}
	for (std::size_t i = 0; i < source_events_.size(); ++i)
	{
		SourceEvent& event = source_events_[i];
		if (event.source_name != 0)
		{
			const ClassTemplatePartialSelection* source_partial =
				event.binding == kNoBinding ? 0 :
				analyzer.FindClassTemplatePartialSelection(event.binding);
			if (event.kind == SOURCE_CLASS_USE &&
				event.pattern < analyzer.class_templates_.size() &&
				event.source_name !=
					analyzer.class_templates_[event.pattern].name &&
				source_partial != 0 &&
				source_partial->pattern != kNoDumpEdge)
			{
				event.suppressed = true;
				continue;
			}
		}
		if (event.syntax == syntax::kNoNode || !arena.HasTokenRange(event.syntax))
		{
			event.suppressed = true;
			continue;
		}
		event.source_token = arena.TokenFirst(event.syntax);
		if (event.source_token >= arena.TokenCount() ||
			arena.TokenSourceFile(event.source_token) != primary_source_file_ ||
			arena.TokenSourceLine(event.source_token) == 0)
			event.suppressed = true;
		if (event.kind == SOURCE_ALIAS_USE &&
			event.pattern < analyzer.alias_templates_.size() &&
			arena.SemanticPayloadId(event.component_syntax) !=
				analyzer.alias_templates_[event.pattern].name)
			event.suppressed = true;
	}
	std::stable_sort(source_events_.begin(), source_events_.end(),
		[&arena](const SourceEvent& left, const SourceEvent& right) {
			const std::size_t left_token = left.source_token ==
				std::numeric_limits<std::size_t>::max() ?
				arena.TokenFirst(left.syntax) : left.source_token;
			const std::size_t right_token = right.source_token ==
				std::numeric_limits<std::size_t>::max() ?
				arena.TokenFirst(right.syntax) : right.source_token;
			return left_token != right_token ? left_token < right_token :
				left.insertion_ordinal < right.insertion_ordinal;
		});
	if (debug_)
	{
		std::ostringstream trace;
		trace << text_ << "witness-debug-source-events\n";
		for (std::size_t i = 0; i < source_events_.size(); ++i)
		{
			const SourceEvent& event = source_events_[i];
			const std::size_t token = event.source_token ==
				std::numeric_limits<std::size_t>::max() ?
				arena.TokenFirst(event.syntax) : event.source_token;
			trace << "  event kind=" << static_cast<unsigned>(event.kind)
				<< " syntax=" << event.syntax << " pattern=" << event.pattern
				<< " component=" << event.component_syntax
				<< " suppressed=" << event.suppressed
				<< " syntax-file=" << arena.SourceFile(event.syntax)
				<< " syntax-line=" << arena.SourceLine(event.syntax)
				<< " syntax-column=" << arena.SourceColumn(event.syntax)
				<< " token-first=" << arena.TokenFirst(event.syntax)
				<< " token-last=" << arena.TokenLast(event.syntax)
				<< " token=" << token;
			if (token < arena.TokenCount())
				trace << " file=" << arena.TokenSourceFile(token)
					<< " line=" << arena.TokenSourceLine(token)
					<< " column=" << arena.TokenSourceColumn(token)
					<< " spelling=" << arena.TokenSpelling(token);
			trace << '\n';
		}
		for (std::size_t i = 0; i < class_template_source_facts_.size(); ++i)
		{
			const ClassTemplateSourceFact& fact =
				class_template_source_facts_[i];
			trace << "  class-template-source syntax=" << fact.syntax
				<< " pattern=" << fact.pattern << " binding=" << fact.binding
				<< " partial=" << fact.selected_partial
				<< " arguments=" << fact.argument_syntax.size()
				<< " presentation-arity=" << fact.presentation_arity
				<< " replayed=" << fact.replayed
				<< " line=" << arena.SourceLine(fact.syntax)
				<< " column=" << arena.SourceColumn(fact.syntax) << '\n';
		}
		trace << "  dependent-class-uses=" << dependent_class_uses_.size()
			<< " dependent-alias-uses=" << dependent_alias_uses_.size()
			<< " dependent-source-uses=" << dependent_source_uses_.size()
			<< " source-occurrences=" << source_occurrences_.size()
			<< " variable-occurrences=" << variable_occurrences_.size()
			<< '\n';
		for (std::size_t i = 0; i < source_occurrences_.size(); ++i)
		{
			const syntax::NodeId node = source_occurrences_[i].syntax;
			trace << "    source-occurrence syntax=" << node
				<< " roles=" << static_cast<unsigned>(
					source_occurrences_[i].roles)
				<< " tag=" << arena.Tag(node)
				<< " payload=" << arena.Payload(node)
				<< " line=" << arena.SourceLine(node)
				<< " column=" << arena.SourceColumn(node) << '\n';
		}
		for (std::size_t i = 0; i < variable_occurrences_.size(); ++i)
		{
			const VariableOccurrenceFact& fact = variable_occurrences_[i];
			trace << "    variable-occurrence syntax=" << fact.syntax
				<< " binding=" << fact.binding
				<< " evaluation=" << static_cast<unsigned>(fact.evaluation)
				<< " phase=" << static_cast<unsigned>(fact.phase)
				<< " owning=" << static_cast<unsigned>(
					fact.owning_class_context)
				<< " properties=" << static_cast<unsigned>(fact.properties)
				<< " line=" << arena.SourceLine(fact.syntax)
				<< " column=" << arena.SourceColumn(fact.syntax) << '\n';
		}
		for (std::size_t i = 0; i < dependent_class_uses_.size(); ++i)
			trace << "    dependent-class syntax=" <<
				dependent_class_uses_[i].first << " pattern=" <<
				dependent_class_uses_[i].second << '\n';
		for (std::size_t i = 0; i < dependent_source_uses_.size(); ++i)
		{
			const syntax::NodeId node = dependent_source_uses_[i];
			trace << "    dependent-source syntax=" << node
				<< " tag=" << arena.Tag(node)
				<< " payload=" << arena.Payload(node)
				<< " line=" << arena.SourceLine(node)
				<< " column=" << arena.SourceColumn(node) << '\n';
		}
		debug_text_ += trace.str();
	}
}

std::string TemplateWitnessObserver::RenderSourceSelection(
	const Analyzer& analyzer, const SyntaxArena& arena,
	const SourceEvent& event, const EntityReplacements& replacements,
	const std::vector<TemplateParameter>** parameters) const
{
	const bool class_use = event.kind == SOURCE_CLASS_USE;
	const bool alias_use = event.kind == SOURCE_ALIAS_USE;
	const bool variable_use = event.kind == SOURCE_VARIABLE_USE;
	const bool function_call = event.kind == SOURCE_FUNCTION_CALL;
	if (!class_use && !alias_use && !variable_use && !function_call)
		return std::string();
	const presentation::TemplateArgumentElision argument_elision(
		entity_argument_limits_);
	const bool token_location = event.source_token !=
		std::numeric_limits<std::size_t>::max();
	const std::string& source_file = token_location ?
		arena.TokenSourceFile(event.source_token) : arena.SourceFile(event.syntax);
	std::ostringstream output;
	output << "  " << (class_use ? "class-use" :
		alias_use ? "alias-use" : variable_use ? "variable-use" :
			"function-call")
		<< " at " << NormalizeWitnessSourcePath(source_file)
		<< ':' << (token_location ? arena.TokenSourceLine(event.source_token) :
			arena.SourceLine(event.syntax))
		<< ':' << ((token_location ?
			arena.TokenSourceColumn(event.source_token) :
			arena.SourceColumn(event.syntax)) + event.source_column_offset) << '\n';
	if (class_use)
	{
		if (event.pattern >= analyzer.class_templates_.size())
			return std::string();
		const ClassTemplatePattern& pattern =
			analyzer.class_templates_[event.pattern];
		*parameters = &pattern.parameters;
		std::string template_name = presentation::RenderName(
			*analyzer.program_, pattern.owner, pattern.name, true);
		if (template_name.find("__cppgm_class_template_") !=
				std::string::npos && event.binding != kNoBinding &&
			event.binding < analyzer.program_->bindings.size())
		{
			const EntityId specialization = analyzer.EntityOf(
				analyzer.program_->bindings[event.binding].type);
			if (specialization != kNoEntity)
			{
				const std::string entity_name = NormalizeEntity(
					presentation::RenderEntity(
						*analyzer.program_, specialization,
						argument_elision, true), replacements);
				const std::size_t opening = FinalTemplateOpening(entity_name);
				if (opening != std::string::npos)
					template_name = entity_name.substr(0, opening);
			}
		}
		output << "    template " << template_name << '\n';
		const bool explicit_specialization = event.selection_kind == 2 ||
			(event.binding != kNoBinding && event.binding <
				analyzer.class_template_explicit_specialization_states_.size() &&
			 analyzer.class_template_explicit_specialization_states_[
				event.binding] != 0);
		const ClassTemplatePartialSelection* partial = event.binding == kNoBinding ?
			0 : analyzer.FindClassTemplatePartialSelection(event.binding);
		if (partial && partial->pattern == kNoDumpEdge) partial = 0;
		output << "    selected " << (explicit_specialization ? "explicit" :
			partial ? "partial" : "primary") << '\n';
	}
	else if (alias_use)
	{
		if (event.pattern >= analyzer.alias_templates_.size())
			return std::string();
		const AliasTemplatePattern& pattern =
			analyzer.alias_templates_[event.pattern];
		*parameters = &pattern.parameters;
		std::string template_name = presentation::RenderName(
			*analyzer.program_, pattern.owner, pattern.name, true);
		if (event.qualifier_pattern < analyzer.class_templates_.size())
		{
			const ClassTemplatePattern& owner =
				analyzer.class_templates_[event.qualifier_pattern];
			if (event.qualifier_partial_pattern <
				owner.partial_specializations.size())
			{
				const std::string owner_name =
					RenderDependentPartialOwnerIdentity(*analyzer.program_, owner,
						owner.partial_specializations[
							event.qualifier_partial_pattern]);
				if (!owner_name.empty()) template_name = owner_name + "::" +
					analyzer.program_->names.Get(pattern.name);
			}
		}
		if (event.qualifier_entity != kNoEntity &&
			!event.allow_substituted_source)
			template_name = NormalizeEntity(presentation::RenderEntity(
				*analyzer.program_, event.qualifier_entity,
				argument_elision, true), replacements) +
				"::" + analyzer.program_->names.Get(pattern.name);
		if (template_name.find("__cppgm_class_template_") != std::string::npos)
		{
			const EntityId owner =
				analyzer.program_->EntityForScope(pattern.owner);
			if (owner != kNoEntity && owner <
				analyzer.class_template_pattern_by_entity_.size())
			{
				const std::uint32_t owner_pattern =
					analyzer.class_template_pattern_by_entity_[owner];
				if (owner_pattern != kNoDumpEdge &&
					owner_pattern < analyzer.class_templates_.size())
				{
					const ClassTemplatePattern& owner_template =
						analyzer.class_templates_[owner_pattern];
					template_name = presentation::RenderName(
						*analyzer.program_, owner_template.owner,
						owner_template.name, true) + "::" +
						analyzer.program_->names.Get(pattern.name);
				}
			}
		}
		output << "    template " << template_name << '\n';
	}
	else if (variable_use)
	{
		if (event.pattern >= analyzer.variable_templates_.size())
			return std::string();
		const VariableTemplatePattern& pattern =
			analyzer.variable_templates_[event.pattern];
		*parameters = &pattern.parameters;
		output << "    template " << presentation::RenderName(
			*analyzer.program_, pattern.owner, pattern.name, true) << '\n';
		output << "    selected " << (event.selection_kind == 2 ? "explicit" :
			event.selection_kind == 1 ? "partial" : "primary") << '\n';
	}
	else
	{
		if (event.pattern >= analyzer.function_templates_.size() ||
			event.binding == kNoBinding) return std::string();
		const FunctionTemplatePattern& pattern =
			analyzer.function_templates_[event.pattern];
		*parameters = &pattern.parameters;
		const BindingRecord& binding = analyzer.program_->bindings[event.binding];
		output << "    callee ";
		if (binding.member_owner != kNoEntity)
			output << NormalizeWitnessTypeSpelling(presentation::RenderEntity(
				*analyzer.program_, binding.member_owner,
				argument_elision)) << "::"
				<< analyzer.program_->names.Get(pattern.name) << '\n';
		else output << presentation::RenderName(
			*analyzer.program_, pattern.owner, pattern.name) << '\n';
		output << "    selected " <<
			(analyzer.GetFunction(event.binding).explicit_specialization ?
				"explicit_specialization" : "instantiation") << '\n';
	}
	return output.str();
}

std::string TemplateWitnessObserver::RenderSourceBindings(
	const Analyzer& analyzer, const SyntaxArena& arena,
	const SourceEvent& event, const EntityReplacements& replacements,
	const std::vector<TemplateParameter>* parameters) const
{
	const bool class_use = event.kind == SOURCE_CLASS_USE;
	const bool alias_use = event.kind == SOURCE_ALIAS_USE;
	const bool variable_use = event.kind == SOURCE_VARIABLE_USE;
	std::vector<syntax::NodeId> explicit_argument_syntax;
	CollectExplicitTemplateArgumentSyntax(
		arena, event.component_syntax, &explicit_argument_syntax);
	NameId event_template_name = class_use ?
		analyzer.class_templates_[event.pattern].name : alias_use ?
		analyzer.alias_templates_[event.pattern].name : variable_use ?
		analyzer.variable_templates_[event.pattern].name :
		analyzer.function_templates_[event.pattern].name;
	std::vector<std::string> explicit_argument_spellings;
	CollectExplicitTemplateArgumentSpellings(arena, event.component_syntax,
		analyzer.program_->names.Get(event_template_name), event.source_token,
		&explicit_argument_spellings);
	const presentation::TemplateArgumentElision argument_elision(
		entity_argument_limits_);
	const auto render_argument = [&analyzer, &arena, &event,
		&explicit_argument_syntax, &explicit_argument_spellings,
		&replacements, &argument_elision, this](std::size_t argument) {
		if (event.kind == SOURCE_ALIAS_USE &&
			argument < explicit_argument_spellings.size())
			return ElideEntities(NormalizeSourceArgumentSpelling(
				*analyzer.program_, explicit_argument_spellings[argument],
				event.arguments[argument]),
				replacements);
		if (argument < explicit_argument_spellings.size())
		{
			const std::string source_identity = RenderFunctionTypeSourceIdentity(
				*analyzer.program_, event.arguments[argument],
				explicit_argument_spellings[argument], &argument_elision);
			if (source_identity != RenderWitnessArgument(
					*analyzer.program_, event.arguments[argument],
					&argument_elision))
				return ElideEntities(source_identity, replacements);
		}
		return ElideEntities(RenderWitnessArgumentAtSource(
			*analyzer.program_, arena, event.arguments[argument],
			argument < explicit_argument_syntax.size() ?
				explicit_argument_syntax[argument] : syntax::kNoNode,
			event.kind == SOURCE_ALIAS_USE, &argument_elision), replacements);
	};
	std::ostringstream output;
	const bool source_only_alias = alias_use && event.arguments.empty() &&
		!explicit_argument_spellings.empty();
	if (source_only_alias && parameters)
	{
		std::size_t argument = 0;
		for (std::size_t parameter = 0; parameter < parameters->size() &&
			argument < explicit_argument_spellings.size(); ++parameter)
		{
			output << "    bind #" << parameter + 1 << " = "
				<< explicit_argument_spellings[argument]
				<< " source=explicit\n";
			if ((*parameters)[parameter].pack)
				argument = explicit_argument_spellings.size();
			else ++argument;
		}
	}
	std::vector<std::uint32_t> parameter_offsets = event.parameter_offsets;
	if (!source_only_alias && parameters &&
		(parameter_offsets.size() != parameters->size() + 1 ||
		 parameter_offsets.empty() || parameter_offsets.front() != 0 ||
		 parameter_offsets.back() != event.arguments.size()))
	{
		parameter_offsets.clear();
		std::size_t argument = 0;
		for (std::size_t parameter = 0; parameter < parameters->size();
			++parameter)
		{
			parameter_offsets.push_back(static_cast<std::uint32_t>(argument));
			if ((*parameters)[parameter].pack)
				argument = event.arguments.size();
			else if (argument < event.arguments.size()) ++argument;
		}
		parameter_offsets.push_back(static_cast<std::uint32_t>(argument));
	}
	if (parameters && parameter_offsets.size() == parameters->size() + 1)
		for (std::size_t parameter = 0; parameter < parameters->size();
			++parameter)
		{
			const std::size_t first = parameter_offsets[parameter];
			const std::size_t last = parameter_offsets[parameter + 1];
			std::string value;
			if (alias_use && (*parameters)[parameter].pack)
			{
				if (first >= last || first >= event.arguments.size()) continue;
				value = render_argument(first);
			}
			else if ((*parameters)[parameter].pack)
			{
				value = "<";
				for (std::size_t argument = first; argument < last; ++argument)
				{
					if (argument != first) value += ", ";
					value += render_argument(argument);
				}
				value += ">";
			}
			else if (first < last && first < event.arguments.size())
				value = render_argument(first);
			else continue;
			const std::uint8_t provenance = first < last &&
				first < event.provenance.size() ? event.provenance[first] : 1;
			output << "    bind #" << parameter + 1 << " = " << value
				<< " source=" << BindingProvenance(provenance) << '\n';
		}
	else if (!source_only_alias)
		for (std::size_t argument = 0; argument < event.arguments.size();
			++argument)
			output << "    bind #" << argument + 1 << " = "
				<< render_argument(argument) << " source="
				<< BindingProvenance(event.provenance[argument]) << '\n';
	return output.str();
}

std::string TemplateWitnessObserver::RenderSourceSpecializations(
	const Analyzer& analyzer, const SourceEvent& event,
	const EntityReplacements& replacements) const
{
	std::ostringstream output;
	const presentation::TemplateArgumentElision argument_elision(
		entity_argument_limits_);
	if (event.kind == SOURCE_CLASS_USE && event.binding != kNoBinding)
	{
		const ClassTemplatePartialSelection* partial =
			analyzer.FindClassTemplatePartialSelection(event.binding);
		if (partial && partial->pattern != kNoDumpEdge)
		{
			const FunctionTemplateDeduction& bindings = partial->bindings;
			const ClassTemplatePartialPattern& selected =
				analyzer.class_templates_[event.pattern].
					partial_specializations[partial->pattern];
			for (std::size_t parameter = 0;
				parameter < selected.parameters.size(); ++parameter)
			{
				output << "    specialize #" << parameter + 1 << " = ";
				if (selected.parameters[parameter].pack)
				{
					const std::vector<TemplateArgument>& pack =
						bindings.pack_arguments[parameter];
					std::size_t count = pack.size();
					std::size_t stride = 1;
					const ClassTemplatePattern& primary =
						analyzer.class_templates_[event.pattern];
					if (HasTrailingTemplateParameterPack(primary.parameters))
					{
						const std::size_t fixed =
							FixedTemplateParameterCount(primary.parameters);
						const std::size_t logical = event.arguments.size() > fixed ?
							event.arguments.size() - fixed : 0;
						if (logical != 0 && count > logical && count % logical == 0)
						{
							stride = count / logical;
							count = logical;
						}
					}
					output << '<';
					for (std::size_t argument = 0; argument < count; ++argument)
					{
						if (argument != 0) output << ", ";
						output << ElideEntities(RenderWitnessArgument(
							*analyzer.program_, pack[argument * stride],
							&argument_elision), replacements);
					}
					output << '>';
				}
				else output << ElideEntities(RenderWitnessArgument(
					*analyzer.program_, bindings.fixed_arguments[parameter],
					&argument_elision),
					replacements);
				output << " source=deduced\n";
			}
		}
	}
	if (event.kind == SOURCE_VARIABLE_USE && event.selection_kind == 1)
		for (std::size_t parameter = 0;
			parameter + 1 < event.specialization_offsets.size(); ++parameter)
		{
			const std::size_t first = event.specialization_offsets[parameter];
			const std::size_t last = event.specialization_offsets[parameter + 1];
			if (last > event.specialization_arguments.size()) continue;
			const bool pack = parameter < event.specialization_packs.size() &&
				event.specialization_packs[parameter] != 0;
			if (!pack && first >= last) continue;
			output << "    specialize #" << parameter + 1 << " = ";
			if (pack)
			{
				output << '<';
				for (std::size_t argument = first; argument < last; ++argument)
				{
					if (argument != first) output << ", ";
					output << ElideEntities(RenderWitnessArgument(
						*analyzer.program_, event.specialization_arguments[argument],
						&argument_elision),
						replacements);
				}
				output << '>';
			}
			else output << ElideEntities(RenderWitnessArgument(
				*analyzer.program_, event.specialization_arguments[first],
				&argument_elision),
				replacements);
			output << " source=deduced\n";
		}
	return output.str();
}

std::string TemplateWitnessObserver::RenderSourceDrops(
	const Analyzer& analyzer, const SourceEvent& event,
	const EntityReplacements& replacements) const
{
	std::ostringstream output;
	struct RenderedDrop
	{
		std::uint32_t declaration;
		std::uint8_t reason;
		std::uint8_t origin_kind;
		RenderedDrop(std::uint32_t declaration_value,
			std::uint8_t reason_value, std::uint8_t origin_kind_value)
			: declaration(declaration_value), reason(reason_value),
			  origin_kind(origin_kind_value) {}
	};
	std::vector<RenderedDrop> rendered_drops;
	for (std::size_t drop = 0; drop < event.drops.size(); ++drop)
	{
		const SourceEvent::Drop& candidate = event.drops[drop];
		const std::string name = OverloadName(
			analyzer, candidate, replacements);
		if (name.empty()) continue;
		// A source occurrence selects one declaration origin. Re-entrant
		// deduction may have formed and rejected another specialization from
		// that same declaration, but that is not a distinct public candidate.
		if (candidate.origin_kind == CANDIDATE_ORIGIN_TEMPLATE_PATTERN &&
			candidate.declaration_origin == event.pattern) continue;
		bool duplicate = false;
		for (std::size_t prior = 0; prior < rendered_drops.size(); ++prior)
			if (rendered_drops[prior].declaration ==
					candidate.declaration_origin &&
				rendered_drops[prior].reason == candidate.reason &&
				rendered_drops[prior].origin_kind == candidate.origin_kind)
				duplicate = true;
		if (duplicate) continue;
		const RenderedDrop rendered(
			candidate.declaration_origin, candidate.reason,
			candidate.origin_kind);
		rendered_drops.push_back(rendered);
		output << "    drop " << name << " reason="
			<< OverloadDropReasonName(candidate.reason) << '\n';
	}
	return output.str();
}

void TemplateWitnessObserver::RenderSourceEvents(const Analyzer& analyzer,
	const EntityReplacements& entity_replacements)
{
	const SyntaxArena& arena = *analyzer.arena_;
	std::size_t prior_rendered = std::numeric_limits<std::size_t>::max();
	for (std::size_t event_index = 0;
		event_index < source_events_.size(); ++event_index)
	{
		const SourceEvent& event = source_events_[event_index];
		if (event.suppressed) continue;
		if (prior_rendered != std::numeric_limits<std::size_t>::max())
		{
			const SourceEvent& prior = source_events_[prior_rendered];
			if (event.kind == prior.kind && event.source_token == prior.source_token &&
				((event.kind == SOURCE_ALIAS_USE &&
				  event.pattern < analyzer.alias_templates_.size() &&
				  prior.pattern < analyzer.alias_templates_.size() &&
				  analyzer.alias_templates_[event.pattern].name ==
					analyzer.alias_templates_[prior.pattern].name) ||
				 (event.pattern == prior.pattern && event.binding == prior.binding &&
				  (event.arguments == prior.arguments ||
				   (event.kind == SOURCE_ALIAS_USE &&
					event.allow_substituted_source &&
					prior.allow_substituted_source)))))
				continue;
		}
		const bool token_location = event.source_token !=
			std::numeric_limits<std::size_t>::max();
		const std::string& source_file = token_location ?
			arena.TokenSourceFile(event.source_token) :
			arena.SourceFile(event.syntax);
		if (source_file != primary_source_file_) continue;
		prior_rendered = event_index;
		const std::vector<TemplateParameter>* event_parameters = 0;
		std::string output = RenderSourceSelection(analyzer, arena, event,
			entity_replacements, &event_parameters);
		if (output.empty()) continue;
		output += RenderSourceBindings(analyzer, arena, event,
			entity_replacements, event_parameters);
		output += RenderSourceSpecializations(
			analyzer, event, entity_replacements);
		output += RenderSourceDrops(analyzer, event, entity_replacements);
		text_ += output;
	}
}

void TemplateWitnessObserver::RenderClosureEvents(const Analyzer& analyzer,
	const EntityReplacements& entity_replacements)
{
	const SyntaxArena& arena = *analyzer.arena_;
	std::vector<EntityId> rendered_class_entities;
	for (std::size_t i = 0; i < class_instantiations_.size(); ++i)
	{
		const EntityId entity = class_instantiations_[i];
		if (entity >= analyzer.program_->entities.size()) continue;
		const EntityRecord& record = analyzer.program_->entities[entity];
		if (debug_)
		{
			std::ostringstream trace;
			trace << "class-demand entity=" << entity
				<< " name=" << presentation::RenderEntity(
					*analyzer.program_, entity, true)
				<< " enclosing=" << record.enclosing_class
				<< " local-context=" << record.local_context
				<< " unnamed=" << record.unnamed_class
				<< " flavor=" << static_cast<unsigned>(record.flavor);
			for (std::size_t generated = 0;
				generated < analyzer.generated_type_identities_.size(); ++generated)
				if (analyzer.generated_type_identities_[generated].entity == entity)
				{
					const syntax::NodeId node =
						analyzer.generated_type_identities_[generated].node;
					trace << " generated-node=" << node
						<< " tag=" << arena.Tag(node)
						<< " tokens=" << arena.TokenFirst(node) << ':'
						<< arena.TokenLast(node)
						<< " line=" << arena.SourceLine(node)
						<< " column=" << arena.SourceColumn(node);
					break;
				}
			trace << '\n';
			debug_text_ += trace.str();
		}
		if (!record.complete || !record.layout_complete || record.lambda_closure ||
			record.template_argument_begin != kNoBinding ||
			IsTemplateMarker(analyzer, entity) ||
			!IsClassNamedFlavor(record.flavor) ||
			!EntityHasTemplateContext(analyzer, entity)) continue;
		rendered_class_entities.push_back(entity);
		if (record.local_context != kNoBinding)
		{
			const BindingId context = analyzer.program_->bindings[
				record.local_context].canonical;
			const EntityId function_owner =
				analyzer.program_->bindings[context].member_owner;
			if (analyzer.GetFunction(context).template_specialization ||
				EntityHasTemplateContext(analyzer, function_owner))
			if (std::find(class_finalizations_.begin(),
				class_finalizations_.end(), entity) ==
				class_finalizations_.end())
				class_finalizations_.push_back(entity);
		}
	}
	std::vector<std::string> rendered_class_finalizations;
	std::vector<std::string> rendered_class_instantiations;
	for (std::size_t i = 0; i < class_finalizations_.size(); ++i)
	{
		const std::string entity = NormalizeEntity(
			ClassEntityName(analyzer, arena, class_finalizations_[i]),
			entity_replacements);
		if (std::find(rendered_class_finalizations.begin(),
			rendered_class_finalizations.end(), entity) ==
			rendered_class_finalizations.end())
			rendered_class_finalizations.push_back(entity);
	}
	for (std::size_t i = 0; i < rendered_class_entities.size(); ++i)
	{
		const std::string entity = NormalizeEntity(
			ClassEntityName(analyzer, arena, rendered_class_entities[i]),
			entity_replacements);
		if (std::find(rendered_class_instantiations.begin(),
			rendered_class_instantiations.end(), entity) ==
			rendered_class_instantiations.end())
			rendered_class_instantiations.push_back(entity);
	}
	if (debug_)
	{
		for (std::size_t i = 0; i < function_instantiations_.size(); ++i)
		{
			const BindingId binding = analyzer.program_->bindings[
				function_instantiations_[i]].canonical;
			const FunctionInfo& function = analyzer.GetFunction(binding);
			const BindingRecord& record = analyzer.program_->bindings[binding];
			debug_text_ += "closure-function binding=" +
				std::to_string(binding) + " name=" +
				analyzer.program_->names.Get(record.name) + " template=" +
				std::to_string(function.template_specialization) +
				" explicit=" + std::to_string(function.explicit_specialization) +
				" generated=" + std::to_string(record.compiler_generated) +
				" friend-of=" + std::to_string(function.friend_of) +
				" member-owner=" + std::to_string(record.member_owner) + "\n";
		}
		for (std::size_t i = 0; i < required_definitions_.size(); ++i)
		{
			const BindingId binding = analyzer.program_->bindings[
				required_definitions_[i]].canonical;
			const FunctionInfo& function = analyzer.GetFunction(binding);
			const BindingRecord& record = analyzer.program_->bindings[binding];
			debug_text_ += "closure-require binding=" +
				std::to_string(binding) + " name=" +
				analyzer.program_->names.Get(record.name) + " template=" +
				std::to_string(function.template_specialization) +
				" explicit=" + std::to_string(function.explicit_specialization) +
				" generated=" + std::to_string(record.compiler_generated) +
				" friend-of=" + std::to_string(function.friend_of) +
				" member-owner=" + std::to_string(record.member_owner) + "\n";
		}
	}
	std::vector<std::string> rendered_instantiations;
	std::vector<std::string> required_entities;
	for (std::size_t i = 0; i < function_instantiations_.size(); ++i)
		if (IsTemplateFunction(analyzer, function_instantiations_[i]))
		{
			const std::string entity = NormalizeEntity(
				FunctionEntityName(analyzer, function_instantiations_[i]),
				entity_replacements);
			if (std::find(rendered_instantiations.begin(),
				rendered_instantiations.end(), entity) ==
				rendered_instantiations.end())
				rendered_instantiations.push_back(entity);
		}
	for (std::size_t i = 0; i < required_definitions_.size(); ++i)
		if (IsRequiredTemplateFunction(analyzer, required_definitions_[i]))
		{
			const std::string entity = NormalizeEntity(
				FunctionEntityName(analyzer, required_definitions_[i]),
				entity_replacements);
			if (std::find(required_entities.begin(), required_entities.end(),
				entity) == required_entities.end())
				required_entities.push_back(entity);
		}
	std::vector<std::string> rendered_requirements;
	for (std::size_t i = 0; i < rendered_instantiations.size(); ++i)
		if (std::find(required_entities.begin(), required_entities.end(),
			rendered_instantiations[i]) != required_entities.end())
			rendered_requirements.push_back(rendered_instantiations[i]);
	for (std::size_t i = 0; i < required_entities.size(); ++i)
		if (std::find(rendered_requirements.begin(), rendered_requirements.end(),
			required_entities[i]) == rendered_requirements.end())
			rendered_requirements.push_back(required_entities[i]);
	std::sort(rendered_class_finalizations.begin(),
		rendered_class_finalizations.end());
	std::sort(rendered_class_instantiations.begin(),
		rendered_class_instantiations.end());
	std::sort(rendered_instantiations.begin(), rendered_instantiations.end());
	std::sort(rendered_requirements.begin(), rendered_requirements.end());
	for (std::size_t i = 0; i < variable_occurrences_.size(); ++i)
	{
		const VariableOccurrenceFact& occurrence = variable_occurrences_[i];
		if (occurrence.evaluation == VARIABLE_OCCURRENCE_EVALUATED &&
			occurrence.phase == VARIABLE_OCCURRENCE_DEFINITION_DEMAND &&
			occurrence.owning_class_context != 0)
			RecordVariableInstantiation(occurrence.binding);
	}
	std::vector<std::string> rendered_variables;
	for (std::size_t i = 0; i < variable_instantiations_.size(); ++i)
	{
		BindingId binding = variable_instantiations_[i];
		if (binding >= analyzer.program_->bindings.size()) continue;
		binding = analyzer.program_->bindings[binding].canonical;
		bool only_unevaluated_reference_occurrences = false;
		for (std::size_t occurrence_index = 0;
			occurrence_index < variable_occurrences_.size(); ++occurrence_index)
		{
			const VariableOccurrenceFact& occurrence =
				variable_occurrences_[occurrence_index];
			if (occurrence.binding != binding) continue;
			if (occurrence.evaluation == VARIABLE_OCCURRENCE_UNEVALUATED &&
				(occurrence.properties & VARIABLE_OCCURRENCE_REFERENCE_TYPE) != 0 &&
				(occurrence.properties & (VARIABLE_OCCURRENCE_CONSTANT_BINDING |
					VARIABLE_OCCURRENCE_VARIABLE_TEMPLATE)) == 0)
			{
				only_unevaluated_reference_occurrences = true;
				continue;
			}
			only_unevaluated_reference_occurrences = false;
			break;
		}
		if (only_unevaluated_reference_occurrences) continue;
		const BindingRecord& record = analyzer.program_->bindings[binding];
		if (OwnerIsExplicitSpecialization(analyzer, record.member_owner)) continue;
		std::string entity;
		if (record.member_owner != kNoEntity)
			entity = SourceDistinguishedClassName(
				analyzer, arena, record.member_owner) + "::" +
				analyzer.program_->names.Get(record.name);
		else entity = presentation::RenderName(
			*analyzer.program_, record.owner,
			record.presentation_name_override != 0 ?
				record.presentation_name_override : record.name);
		entity = NormalizeEntity(entity, entity_replacements);
		if (std::find(rendered_variables.begin(), rendered_variables.end(),
			entity) == rendered_variables.end())
			rendered_variables.push_back(entity);
	}
	std::sort(rendered_variables.begin(), rendered_variables.end());
	if (!rendered_class_finalizations.empty() ||
		!rendered_class_instantiations.empty() ||
		!rendered_instantiations.empty() || !rendered_requirements.empty() ||
		!rendered_variables.empty())
		text_ += "template-closure-events\n";
	for (std::size_t i = 0; i < rendered_class_finalizations.size(); ++i)
		text_ += "  class-finalization\n    entity " +
			rendered_class_finalizations[i] + '\n';
	for (std::size_t i = 0; i < rendered_class_instantiations.size(); ++i)
		text_ += "  class-instantiation\n    entity " +
			rendered_class_instantiations[i] + '\n';
	for (std::size_t i = 0; i < rendered_instantiations.size(); ++i)
		text_ += "  function-instantiation\n    entity " +
			rendered_instantiations[i] + '\n';
	for (std::size_t i = 0; i < rendered_requirements.size(); ++i)
		text_ += "  require-definition\n    entity " +
			rendered_requirements[i] + '\n';
	for (std::size_t i = 0; i < rendered_variables.size(); ++i)
		text_ += "  variable-instantiation\n    entity " +
			rendered_variables[i] + '\n';
}

void TemplateWitnessObserver::FinishTranslationUnit(const Analyzer& analyzer)
{
	text_ += "translation-unit\n";
	EntityReplacements replacements;
	PrepareSourceEvents(analyzer, &replacements);
	RenderSourceEvents(analyzer, replacements);
	RenderClosureEvents(analyzer, replacements);
	semantic_source_facts_.clear();
	retained_member_source_facts_.clear();
	source_events_.clear();
	function_specializations_.clear();
	class_specializations_.clear();
	class_template_source_facts_.clear();
	entity_argument_limits_.clear();
	variable_specializations_.clear();
	overload_selections_.clear();
	deduction_drops_.clear();
	dependent_class_uses_.clear();
	dependent_alias_uses_.clear();
	dependent_source_uses_.clear();
	source_occurrences_.clear();
	variable_occurrences_.clear();
	resolved_source_uses_.clear();
	function_instantiations_.clear();
	required_definitions_.clear();
	class_instantiations_.clear();
	class_finalizations_.clear();
	variable_instantiations_.clear();
}

}
}
