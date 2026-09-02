#include "semantic/diagnostics/template_witness.h"

#include "semantic/analysis/analyzer.h"
#include "semantic/presentation/source_identity.h"

#include <algorithm>
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
		"better_candidate_selected";
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
	return spelling;
}

std::string RenderWitnessArgument(const Program& program,
	const TemplateArgument& argument)
{
	if ((argument.kind == TEMPLATE_ARGUMENT_TYPE ||
		 argument.kind == TEMPLATE_ARGUMENT_TEMPLATE) &&
		(argument.type == kNoType || argument.type >= program.types.Size()))
		return "<dependent>";
	return NormalizeWitnessTypeSpelling(
		presentation::RenderTemplateArgument(program, argument));
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

std::size_t FindTemplateNameToken(const SyntaxArena& arena,
	const std::string& primary_source_file, const std::string& name,
	const std::vector<std::uint8_t>& used)
{
	for (std::size_t token = 0; token + 1 < arena.TokenCount(); ++token)
	{
		if (used[token] || arena.TokenSourceFile(token) != primary_source_file ||
			arena.TokenSpelling(token) != name ||
			arena.TokenSpelling(token + 1) != "<")
			continue;
		return token;
	}
	return std::numeric_limits<std::size_t>::max();
}

std::size_t FindFunctionNameToken(const SyntaxArena& arena,
	const std::string& primary_source_file, std::string name,
	const std::vector<std::uint8_t>& used)
{
	if (name.compare(0, 8, "operator") == 0)
	{
		name.erase(0, 8);
		while (!name.empty() && name[0] == ' ') name.erase(0, 1);
	}
	for (std::size_t token = 0; token < arena.TokenCount(); ++token)
		if (!used[token] &&
			arena.TokenSourceFile(token) == primary_source_file &&
			arena.TokenSpelling(token) == name)
		{
			std::size_t first = token;
			while (first >= 2 && arena.TokenSpelling(first - 1) == "::" &&
				arena.TokenSourceFile(first - 2) == primary_source_file)
				first -= 2;
			return first;
		}
	return std::numeric_limits<std::size_t>::max();
}

std::size_t FindTypeUseNameToken(const SyntaxArena& arena,
	const std::string& primary_source_file, const std::string& name,
	const std::vector<std::uint8_t>& used)
{
	for (std::size_t token = 0; token < arena.TokenCount(); ++token)
	{
		if (used[token] || arena.TokenSourceFile(token) != primary_source_file ||
			arena.TokenSpelling(token) != name) continue;
		bool declaration_name = false;
		for (std::size_t prior = token; prior != 0; )
		{
			--prior;
			const std::string& spelling = arena.TokenSpelling(prior);
			if (spelling == ";" || spelling == "{" || spelling == "}") break;
			if (spelling == "typedef" || spelling == "using")
			{
				declaration_name = true;
				break;
			}
		}
		if (declaration_name) continue;
		std::size_t first = token;
		while (first >= 2 && arena.TokenSpelling(first - 1) == "::" &&
			arena.TokenSourceFile(first - 2) == primary_source_file)
			first -= 2;
		return first;
	}
	return std::numeric_limits<std::size_t>::max();
}

std::size_t FindNameTokenInRange(const SyntaxArena& arena,
	syntax::NodeId syntax, const std::string& name,
	const std::vector<std::uint8_t>& used)
{
	if (syntax == syntax::kNoNode) return std::numeric_limits<std::size_t>::max();
	const std::size_t first = arena.TokenFirst(syntax);
	const std::size_t last = arena.TokenLast(syntax);
	if (first >= last || last > arena.TokenCount())
		return std::numeric_limits<std::size_t>::max();
	std::size_t repeated = std::numeric_limits<std::size_t>::max();
	for (std::size_t token = last; token != first; )
	{
		--token;
		if (arena.TokenSpelling(token) != name) continue;
		if (!used[token]) return token;
		if (repeated == std::numeric_limits<std::size_t>::max())
			repeated = token;
	}
	return repeated;
}

bool IsInsideTemplateDeclaration(const SyntaxArena& arena,
	std::size_t token)
{
	for (syntax::NodeId node = 0; node < arena.Nodes(); ++node)
	{
		if (!arena.IsTag(node, ::cppgm::syntax::STAG_TEMPLATE_DECLARATION))
			continue;
		const std::size_t first = arena.TokenFirst(node);
		const std::size_t last = arena.TokenLast(node);
		if (first >= last || last > arena.TokenCount() ||
			arena.TokenSourceFile(first) != arena.TokenSourceFile(token) ||
			arena.TokenSourceFile(last - 1) != arena.TokenSourceFile(token))
			continue;
		if (first <= token && token < last) return true;
	}
	return false;
}

void CollectTemplateParameterNames(const SyntaxArena& arena,
	syntax::NodeId node, std::vector<syntax::TextId>* names)
{
	if (arena.IsTag(node, "identifier") ||
		arena.IsTag(node, ::cppgm::syntax::STAG_ID_EXPRESSION) ||
		arena.IsTag(node, ::cppgm::syntax::STAG_NAME_COMPONENT))
	{
		const syntax::TextId name = arena.SemanticPayloadId(node);
		if (name != 0) names->push_back(name);
	}
	for (std::uint32_t edge = arena.FirstEdge(node); edge != syntax::kNoEdge;
		edge = arena.NextEdge(edge))
		CollectTemplateParameterNames(arena, arena.EdgeChild(edge), names);
}

bool NodeUsesAnyName(const SyntaxArena& arena, syntax::NodeId node,
	const std::vector<syntax::TextId>& names)
{
	if (std::find(names.begin(), names.end(), arena.SemanticPayloadId(node)) !=
		names.end()) return true;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != syntax::kNoEdge;
		edge = arena.NextEdge(edge))
		if (NodeUsesAnyName(arena, arena.EdgeChild(edge), names)) return true;
	return false;
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

bool SourceUseDependsOnEnclosingTemplateParameter(const SyntaxArena& arena,
	syntax::NodeId source, std::size_t token)
{
	for (syntax::NodeId node = 0; node < arena.Nodes(); ++node)
	{
		if (!arena.IsTag(node, ::cppgm::syntax::STAG_TEMPLATE_DECLARATION) ||
			arena.TokenFirst(node) > token || token >= arena.TokenLast(node))
			continue;
		const syntax::NodeId clause = arena.FindDirectChildTag(
			node, ::cppgm::syntax::STAG_TEMPLATE_PARAMETER_CLAUSE);
		if (clause == syntax::kNoNode) continue;
		std::vector<syntax::TextId> names;
		CollectTemplateParameterNames(arena, clause, &names);
		if (NodeUsesAnyName(arena, source, names)) return true;
	}
	return false;
}

}

TemplateWitnessObserver::SourceEvent::SourceEvent(
	SourceEventKind kind_value, syntax::NodeId syntax_value,
	std::uint32_t pattern_value, BindingId binding_value,
	const std::vector<TemplateArgument>& argument_values,
	std::size_t explicit_count, std::size_t column_offset,
	std::size_t ordinal)
	: kind(kind_value), syntax(syntax_value), pattern(pattern_value),
	  binding(binding_value), arguments(argument_values),
	  provenance(argument_values.size(), 2),
	  source_column_offset(column_offset), source_name(0),
	  source_token(std::numeric_limits<std::size_t>::max()),
	  insertion_ordinal(ordinal), suppressed(false),
	  allow_substituted_source(false)
{
	for (std::size_t i = 0; i < explicit_count && i < provenance.size(); ++i)
		provenance[i] = 0;
}

TemplateWitnessObserver::FunctionSpecializationFact::
	FunctionSpecializationFact(BindingId binding_value,
		std::uint32_t pattern_value,
		const std::vector<TemplateArgument>& argument_values,
		const std::vector<TemplateArgument>& requested_values)
	: binding(binding_value), pattern(pattern_value), arguments(argument_values),
	  provenance(argument_values.size(), 1)
{
	for (std::size_t i = 0; i < provenance.size() &&
		i < requested_values.size(); ++i)
		if (requested_values[i].type == kNoType) provenance[i] = 2;
}

TemplateWitnessObserver::TemplateWitnessObserver(bool debug)
	: text_(), debug_text_(), primary_source_file_(), debug_(debug), source_events_(),
	  function_specializations_(), class_specializations_(),
	  overload_selections_(), deduction_drops_(), dependent_class_uses_(),
	  dependent_alias_uses_(), dependent_source_uses_(), function_instantiations_(),
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
	if (!TemplateWitnessSourceUseEnabled() || specifiers == kNoNode ||
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
	for (std::uint32_t edge = arena_->FirstEdge(structure); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId component = arena_->EdgeChild(edge);
		if (arena_->IsTag(component, ::cppgm::syntax::STAG_NAME_COMPONENT) &&
			FindChild(component,
				::cppgm::syntax::STAG_TEMPLATE_TYPE_ARGUMENT_LIST) != kNoNode)
			return;
	}
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
	NameId source_name = arena_->SemanticPayloadId(structure);
	if (source_name == 0) source_name = arena_->PayloadId(structure);
	for (std::uint32_t edge = arena_->FirstEdge(structure); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId component = arena_->EdgeChild(edge);
		if (!arena_->IsTag(component, ::cppgm::syntax::STAG_NAME_COMPONENT))
			continue;
		source_name = arena_->SemanticPayloadId(component);
		if (source_name == 0) source_name = arena_->PayloadId(component);
	}
	template_witness_->RecordDeducedClassUse(structure, pattern,
		record.declaration, StoredTemplateArguments(
			record.template_argument_begin, record.template_argument_count),
		source_name);
}

void Analyzer::RecordFunctionTemplateSourceCall(NodeId syntax,
	BindingId selected, std::size_t explicit_count)
{
	if (!TemplateWitnessSourceUseEnabled() || syntax == kNoNode ||
		selected == kNoBinding) return;
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
	template_witness_->RecordFunctionCall(syntax, pattern,
		selected, arguments, explicit_count);
}

void TemplateWitnessObserver::BeginTranslationUnit(
	const std::string& primary_source_file)
{
	primary_source_file_ = primary_source_file;
	source_events_.clear();
	function_specializations_.clear();
	class_specializations_.clear();
	overload_selections_.clear();
	deduction_drops_.clear();
	dependent_class_uses_.clear();
	dependent_alias_uses_.clear();
	dependent_source_uses_.clear();
	function_instantiations_.clear();
	required_definitions_.clear();
	class_instantiations_.clear();
	class_finalizations_.clear();
	variable_instantiations_.clear();
	next_insertion_ordinal_ = 0;
}

void TemplateWitnessObserver::RecordClassUse(syntax::NodeId syntax,
	std::uint32_t pattern, BindingId binding,
	const std::vector<TemplateArgument>& arguments, std::size_t explicit_count,
	std::size_t source_column_offset)
{
	if (std::find(dependent_source_uses_.begin(), dependent_source_uses_.end(),
		syntax) != dependent_source_uses_.end()) return;
	if (std::find(dependent_class_uses_.begin(), dependent_class_uses_.end(),
		std::make_pair(syntax, pattern)) != dependent_class_uses_.end()) return;
	source_events_.push_back(SourceEvent(SOURCE_CLASS_USE, syntax, pattern,
		binding, arguments, explicit_count, source_column_offset,
		next_insertion_ordinal_++));
}

void TemplateWitnessObserver::NoteDependentSourceUse(syntax::NodeId syntax)
{
	if (syntax == syntax::kNoNode) return;
	if (std::find(dependent_source_uses_.begin(), dependent_source_uses_.end(),
		syntax) == dependent_source_uses_.end())
		dependent_source_uses_.push_back(syntax);
	for (std::size_t i = 0; i < source_events_.size(); ++i)
		if ((source_events_[i].kind == SOURCE_CLASS_USE ||
			 source_events_[i].kind == SOURCE_ALIAS_USE) &&
			source_events_[i].syntax == syntax)
			source_events_[i].suppressed = true;
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
			source_events_[i].pattern == pattern)
			source_events_[i].suppressed = true;
}

void TemplateWitnessObserver::RecordInstantiatedClassUse(
	syntax::NodeId syntax, std::uint32_t pattern, BindingId binding,
	const std::vector<TemplateArgument>& arguments)
{
	source_events_.push_back(SourceEvent(SOURCE_CLASS_USE, syntax, pattern,
		binding, arguments, arguments.size(), 0, next_insertion_ordinal_++));
	source_events_.back().allow_substituted_source = true;
}

void TemplateWitnessObserver::RecordAliasUse(syntax::NodeId syntax,
	std::uint32_t pattern, const std::vector<TemplateArgument>& arguments,
	std::size_t explicit_count)
{
	if (std::find(dependent_source_uses_.begin(), dependent_source_uses_.end(),
		syntax) != dependent_source_uses_.end()) return;
	if (std::find(dependent_alias_uses_.begin(), dependent_alias_uses_.end(),
		std::make_pair(syntax, pattern)) != dependent_alias_uses_.end()) return;
	source_events_.push_back(SourceEvent(SOURCE_ALIAS_USE, syntax, pattern,
		kNoBinding, arguments, explicit_count, 0,
		next_insertion_ordinal_++));
}

void TemplateWitnessObserver::NoteDependentAliasUse(
	syntax::NodeId syntax, std::uint32_t pattern)
{
	const std::pair<syntax::NodeId, std::uint32_t> key(syntax, pattern);
	if (std::find(dependent_alias_uses_.begin(), dependent_alias_uses_.end(),
		key) == dependent_alias_uses_.end())
		dependent_alias_uses_.push_back(key);
	for (std::size_t i = 0; i < source_events_.size(); ++i)
		if (source_events_[i].kind == SOURCE_ALIAS_USE &&
			source_events_[i].syntax == syntax &&
			source_events_[i].pattern == pattern)
			source_events_[i].suppressed = true;
}

void TemplateWitnessObserver::RecordFunctionSpecialization(BindingId binding,
	std::uint32_t pattern,
	const std::vector<TemplateArgument>& arguments,
	const std::vector<TemplateArgument>& requested_arguments)
{
	for (std::size_t i = 0; i < function_specializations_.size(); ++i)
		if (function_specializations_[i].binding == binding) return;
	function_specializations_.push_back(FunctionSpecializationFact(
		binding, pattern, arguments, requested_arguments));
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

void TemplateWitnessObserver::RecordFunctionCall(syntax::NodeId syntax,
	std::uint32_t pattern, BindingId binding,
	const std::vector<TemplateArgument>& arguments, std::size_t explicit_count)
{
	source_events_.push_back(SourceEvent(SOURCE_FUNCTION_CALL, syntax, pattern,
		binding, arguments, 0, 0, next_insertion_ordinal_++));
	SourceEvent& event = source_events_.back();
	for (std::size_t i = 0; i < function_specializations_.size(); ++i)
		if (function_specializations_[i].binding == binding &&
			function_specializations_[i].arguments == arguments)
		{
			event.provenance = function_specializations_[i].provenance;
			break;
		}
	for (std::size_t i = 0; i < explicit_count &&
		i < event.provenance.size(); ++i)
		event.provenance[i] = 0;
	for (std::size_t i = 0; i < deduction_drops_.size(); ++i)
		if (!deduction_drops_[i].consumed &&
			deduction_drops_[i].syntax == syntax)
		{
			event.drops.push_back(SourceEvent::Drop(kNoBinding,
				deduction_drops_[i].pattern, deduction_drops_[i].reason));
			deduction_drops_[i].consumed = true;
		}
	for (std::size_t i = 0; i < overload_selections_.size(); ++i)
		if (!overload_selections_[i].consumed &&
			overload_selections_[i].selected == binding)
		{
			event.drops = overload_selections_[i].drops;
			overload_selections_[i].consumed = true;
			break;
		}
}

void TemplateWitnessObserver::RecordOverloadSelection(BindingId selected,
	const std::vector<BindingId>& candidates,
	const std::vector<std::uint8_t>& reasons)
{
	std::vector<SourceEvent::Drop> drops;
	for (std::size_t i = 0; i < candidates.size() && i < reasons.size(); ++i)
		if (reasons[i] != 0)
			drops.push_back(SourceEvent::Drop(candidates[i], kNoDumpEdge,
				reasons[i]));
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
	if (std::find(variable_instantiations_.begin(),
		variable_instantiations_.end(), binding) == variable_instantiations_.end())
		variable_instantiations_.push_back(binding);
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

void TemplateWitnessObserver::FinishTranslationUnit(const Analyzer& analyzer)
{
	text_ += "translation-unit\n";
	const SyntaxArena& arena = *analyzer.arena_;
	std::vector<std::pair<std::string, std::string> > entity_replacements;
	for (std::size_t i = 0; i < class_specializations_.size(); ++i)
	{
		const ClassSpecializationFact& fact = class_specializations_[i];
		if (fact.binding >= analyzer.program_->bindings.size() ||
			fact.explicit_count >= fact.arguments.size()) continue;
		const EntityId entity = analyzer.EntityOf(
			analyzer.program_->bindings[fact.binding].type);
		if (entity == kNoEntity) continue;
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
				*analyzer.program_, fact.arguments[argument]);
		}
		elided += '>';
		if (full != elided)
			entity_replacements.push_back(
				std::make_pair(full, elided));
	}
	std::sort(entity_replacements.begin(), entity_replacements.end(),
		[](const std::pair<std::string, std::string>& left,
			const std::pair<std::string, std::string>& right) {
			return left.first.size() > right.first.size();
		});
	const auto elide_entities = [&entity_replacements](std::string spelling) {
		spelling = NormalizeWitnessTypeSpelling(spelling);
		for (std::size_t pass = 0; pass <= entity_replacements.size(); ++pass)
		{
			const std::string before = spelling;
			for (std::size_t i = 0; i < entity_replacements.size(); ++i)
				ReplaceAll(&spelling, entity_replacements[i].first,
					entity_replacements[i].second);
			if (before == spelling) break;
		}
		return spelling;
	};
	const auto normalize_entity = [&elide_entities](std::string spelling) {
		spelling = elide_entities(spelling);
		ReplaceAll(&spelling, "unsigned int", "unsigned");
		ReplaceAll(&spelling, "signed int", "int");
		return spelling;
	};
	const auto overload_name = [&analyzer, &normalize_entity](
		const SourceEvent::Drop& drop) {
		const BindingId binding = drop.binding;
		if (binding == kNoBinding)
		{
			if (drop.pattern == kNoDumpEdge ||
				drop.pattern >= analyzer.function_templates_.size())
				return std::string();
			const FunctionTemplatePattern& pattern =
				analyzer.function_templates_[drop.pattern];
			return normalize_entity(presentation::RenderName(
				*analyzer.program_, pattern.owner, pattern.name));
		}
		if (binding == kNoBinding ||
			binding >= analyzer.program_->bindings.size()) return std::string();
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
				*analyzer.program_, visible.member_owner, true) + "::" +
				analyzer.program_->names.Get(terminal);
		else result = presentation::RenderName(
			*analyzer.program_, visible.owner, terminal);
		return normalize_entity(result);
	};
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
	// Default provenance belongs to the canonical specialization.  If one use
	// omits a trailing default, an explicitly spelled equivalent use denotes
	// the same default-origin argument rather than a distinct binding.
	for (std::size_t i = 0; i < source_events_.size(); ++i)
		for (std::size_t j = i + 1; j < source_events_.size(); ++j)
			if (source_events_[i].kind == source_events_[j].kind &&
				source_events_[i].pattern == source_events_[j].pattern &&
				source_events_[i].arguments == source_events_[j].arguments)
				for (std::size_t argument = 0;
					argument < source_events_[i].provenance.size() &&
					argument < source_events_[j].provenance.size(); ++argument)
					if (source_events_[i].provenance[argument] == 2 ||
						source_events_[j].provenance[argument] == 2)
					{
						source_events_[i].provenance[argument] = 2;
						source_events_[j].provenance[argument] = 2;
					}
	std::vector<std::uint8_t> used_tokens(arena.TokenCount(), 0);
	for (std::size_t i = 0; i < source_events_.size(); ++i)
	{
		const SourceEvent& event = source_events_[i];
		if (event.source_name != 0)
		{
			const std::string& source_name =
				analyzer.program_->names.Get(event.source_name);
			std::size_t source_token = FindNameTokenInRange(
				arena, event.syntax, source_name, used_tokens);
			while (source_token != std::numeric_limits<std::size_t>::max() &&
				source_token >= 2 &&
				arena.TokenSpelling(source_token - 1) == "::" &&
				arena.TokenSourceFile(source_token - 2) == primary_source_file_)
				source_token -= 2;
			if (source_token == std::numeric_limits<std::size_t>::max() ||
				arena.TokenSourceFile(source_token) != primary_source_file_)
				source_token = FindTypeUseNameToken(arena,
					primary_source_file_, source_name, used_tokens);
			if (source_token != std::numeric_limits<std::size_t>::max())
			{
				source_events_[i].source_token = source_token;
				used_tokens[source_token] = 1;
				continue;
			}
		}
		NameId name = 0;
		if (event.kind == SOURCE_CLASS_USE &&
			event.pattern < analyzer.class_templates_.size())
			name = analyzer.class_templates_[event.pattern].name;
		else if (event.kind == SOURCE_ALIAS_USE &&
			event.pattern < analyzer.alias_templates_.size())
			name = analyzer.alias_templates_[event.pattern].name;
		else if (event.kind == SOURCE_FUNCTION_CALL &&
			event.pattern < analyzer.function_templates_.size())
			name = analyzer.function_templates_[event.pattern].name;
		if (name != 0 && event.syntax != syntax::kNoNode &&
			event.kind != SOURCE_FUNCTION_CALL)
		{
			const std::size_t name_token = FindNameTokenInRange(arena,
				event.syntax, analyzer.program_->names.Get(name), used_tokens);
			if (name_token != std::numeric_limits<std::size_t>::max() &&
				arena.TokenSourceFile(name_token) == primary_source_file_ &&
				arena.TokenSourceLine(name_token) != 0)
			{
				if (!event.allow_substituted_source &&
					SourceUseDependsOnEnclosingTemplateParameter(
					arena, event.syntax, name_token))
				{
					source_events_[i].suppressed = true;
					continue;
				}
				source_events_[i].source_token = name_token;
				used_tokens[name_token] = 1;
				continue;
			}
		}
		const std::size_t direct_token = event.syntax == syntax::kNoNode ?
			std::numeric_limits<std::size_t>::max() :
			arena.TokenFirst(event.syntax);
		if (event.syntax != syntax::kNoNode &&
			arena.TokenLast(event.syntax) > direct_token &&
			direct_token < arena.TokenCount() &&
			arena.TokenSourceFile(direct_token) == primary_source_file_ &&
			arena.TokenSourceLine(direct_token) != 0)
		{
			if (event.kind == SOURCE_FUNCTION_CALL &&
				IsInsideTemplateDeclaration(arena, direct_token))
			{
				source_events_[i].suppressed = true;
				continue;
			}
			source_events_[i].source_token = direct_token;
			used_tokens[direct_token] = 1;
			continue;
		}
		if (name == 0) continue;
		source_events_[i].source_token = event.kind == SOURCE_FUNCTION_CALL ?
			FindFunctionNameToken(arena, primary_source_file_,
				analyzer.program_->names.Get(name), used_tokens) :
			FindTemplateNameToken(arena, primary_source_file_,
				analyzer.program_->names.Get(name), used_tokens);
		if (event.kind == SOURCE_FUNCTION_CALL &&
			source_events_[i].source_token !=
				std::numeric_limits<std::size_t>::max() &&
			IsInsideTemplateDeclaration(
				arena, source_events_[i].source_token))
			source_events_[i].suppressed = true;
		if (source_events_[i].source_token !=
			std::numeric_limits<std::size_t>::max())
			used_tokens[source_events_[i].source_token] = 1;
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
				<< " suppressed=" << event.suppressed << " token=" << token;
			if (token < arena.TokenCount())
				trace << " file=" << arena.TokenSourceFile(token)
					<< " line=" << arena.TokenSourceLine(token)
					<< " column=" << arena.TokenSourceColumn(token)
					<< " spelling=" << arena.TokenSpelling(token);
			trace << '\n';
		}
		trace << "  dependent-class-uses=" << dependent_class_uses_.size()
			<< " dependent-alias-uses=" << dependent_alias_uses_.size()
			<< " dependent-source-uses=" << dependent_source_uses_.size()
			<< '\n';
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
				event.pattern == prior.pattern && event.binding == prior.binding &&
				event.arguments == prior.arguments)
				continue;
		}
		const bool token_location = event.source_token !=
			std::numeric_limits<std::size_t>::max();
		const std::string& source_file = token_location ?
			arena.TokenSourceFile(event.source_token) :
			arena.SourceFile(event.syntax);
		if (source_file != primary_source_file_) continue;
		prior_rendered = event_index;
		const bool class_use = event.kind == SOURCE_CLASS_USE;
		const bool alias_use = event.kind == SOURCE_ALIAS_USE;
		const bool function_call = event.kind == SOURCE_FUNCTION_CALL;
		if (!class_use && !alias_use && !function_call) continue;
		std::ostringstream output;
		output << "  " << (class_use ? "class-use" :
			alias_use ? "alias-use" : "function-call")
			<< " at " << NormalizeWitnessSourcePath(source_file)
			<< ':' << (token_location ?
				arena.TokenSourceLine(event.source_token) :
				arena.SourceLine(event.syntax))
			<< ':' << ((token_location ?
				arena.TokenSourceColumn(event.source_token) :
				arena.SourceColumn(event.syntax)) +
				event.source_column_offset) << '\n';
		if (class_use)
		{
			if (event.pattern >= analyzer.class_templates_.size()) continue;
			const ClassTemplatePattern& pattern =
				analyzer.class_templates_[event.pattern];
			output << "    template " << presentation::RenderName(
				*analyzer.program_, pattern.owner, pattern.name, true) << '\n';
			const bool explicit_specialization = event.binding != kNoBinding &&
				event.binding <
					analyzer.class_template_explicit_specialization_states_.size() &&
				analyzer.class_template_explicit_specialization_states_[
					event.binding] != 0;
			const ClassTemplatePartialSelection* partial = event.binding == kNoBinding ?
				0 : analyzer.FindClassTemplatePartialSelection(event.binding);
			if (partial && partial->pattern == kNoDumpEdge) partial = 0;
			output << "    selected " << (explicit_specialization ? "explicit" :
				partial ? "partial" : "primary") << '\n';
		}
		else if (alias_use)
		{
			if (event.pattern >= analyzer.alias_templates_.size()) continue;
			const AliasTemplatePattern& pattern =
				analyzer.alias_templates_[event.pattern];
			output << "    template " << presentation::RenderName(
				*analyzer.program_, pattern.owner, pattern.name, true) << '\n';
		}
		else
		{
			if (event.pattern >= analyzer.function_templates_.size() ||
				event.binding == kNoBinding) continue;
			const FunctionTemplatePattern& pattern =
				analyzer.function_templates_[event.pattern];
			const BindingRecord& binding =
				analyzer.program_->bindings[event.binding];
			output << "    callee ";
			if (binding.member_owner != kNoEntity)
				output << presentation::RenderEntity(
					*analyzer.program_, binding.member_owner) << "::"
					<< analyzer.program_->names.Get(pattern.name) << '\n';
			else output << presentation::RenderName(
				*analyzer.program_, pattern.owner, pattern.name) << '\n';
			output << "    selected " <<
				(analyzer.GetFunction(event.binding).explicit_specialization ?
					"explicit_specialization" : "instantiation") << '\n';
		}
		for (std::size_t argument = 0; argument < event.arguments.size();
			++argument)
			output << "    bind #" << argument + 1 << " = "
				<< elide_entities(RenderWitnessArgument(
					*analyzer.program_, event.arguments[argument]))
				<< " source=" << BindingProvenance(
					event.provenance[argument]) << '\n';
		if (class_use && event.binding != kNoBinding)
		{
			const ClassTemplatePartialSelection* partial =
				analyzer.FindClassTemplatePartialSelection(event.binding);
			if (partial && partial->pattern != kNoDumpEdge)
			{
				const FunctionTemplateDeduction& bindings = partial->bindings;
				for (std::size_t parameter = 0;
					parameter < bindings.fixed_arguments.size(); ++parameter)
					output << "    specialize #" << parameter + 1 << " = "
						<< elide_entities(RenderWitnessArgument(
							*analyzer.program_,
							bindings.fixed_arguments[parameter])) << '\n';
			}
		}
		for (std::size_t drop = 0; drop < event.drops.size(); ++drop)
		{
			const std::string name = overload_name(event.drops[drop]);
			if (!name.empty())
				output << "    drop " << name << " reason="
					<< OverloadDropReasonName(event.drops[drop].reason) << '\n';
		}
		text_ += output.str();
	}
	const auto function_context_name = [&analyzer](BindingId binding) {
		binding = analyzer.program_->bindings[binding].canonical;
		const FunctionInfo& function = analyzer.GetFunction(binding);
		const BindingRecord& record = analyzer.program_->bindings[binding];
		const NameId terminal = function.presentation_name_override != 0 ?
			function.presentation_name_override :
			record.presentation_name_override != 0 ?
				record.presentation_name_override : record.name;
		std::string result = record.member_owner == kNoEntity ?
			presentation::RenderName(
				*analyzer.program_, record.owner, terminal) :
			presentation::RenderEntity(
				*analyzer.program_, record.member_owner, true) + "::" +
				analyzer.program_->names.Get(terminal);
		const TypeRecord& type = analyzer.program_->types.Get(function.type);
		result += '(';
		const TypeId* parameters =
			analyzer.program_->types.Parameters(function.type);
		for (std::size_t i = 0; i < type.parameter_count; ++i)
		{
			if (i != 0) result += ", ";
			result += NormalizeWitnessTypeSpelling(
				presentation::RenderType(
					*analyzer.program_, parameters[i]));
		}
		if (type.variadic)
		{
			if (type.parameter_count != 0) result += ", ";
			result += "...";
		}
		return result + ')';
	};
	const auto entity_has_template_context = [&analyzer](EntityId entity) {
		for (std::size_t depth = 0; entity != kNoEntity &&
			depth < analyzer.program_->entities.size(); ++depth)
		{
			const EntityRecord& record = analyzer.program_->entities[entity];
			if (record.template_argument_begin != kNoBinding) return true;
			if (record.local_context != kNoBinding)
			{
				const FunctionInfo& context =
					analyzer.GetFunction(record.local_context);
				if (context.template_specialization) return true;
			}
			entity = record.enclosing_class;
		}
		return false;
	};
	const auto is_template_marker = [&analyzer](EntityId entity) {
		if (entity >= analyzer.class_template_pattern_by_entity_.size())
			return false;
		const std::uint32_t pattern =
			analyzer.class_template_pattern_by_entity_[entity];
		return pattern != kNoDumpEdge &&
			pattern < analyzer.class_templates_.size() &&
			analyzer.class_templates_[pattern].marker_entity == entity;
	};
	const auto generated_source_node = [&analyzer](EntityId entity) {
		for (std::size_t i = 0;
			i < analyzer.generated_type_identities_.size(); ++i)
			if (analyzer.generated_type_identities_[i].entity == entity)
				return analyzer.generated_type_identities_[i].node;
		return syntax::kNoNode;
	};
	const auto generated_has_declarator = [&arena](
		syntax::NodeId node) {
		if (node == syntax::kNoNode) return false;
		std::size_t best_distance =
			std::numeric_limits<std::size_t>::max();
		bool result = false;
		for (syntax::NodeId candidate = 0; candidate < arena.Nodes(); ++candidate)
		{
			if (!arena.IsTag(candidate,
				::cppgm::syntax::STAG_SIMPLE_DECLARATION)) continue;
			const std::size_t distance =
				DescendantDistance(arena, candidate, node);
			if (distance >= best_distance) continue;
			best_distance = distance;
			const syntax::NodeId list = arena.FindDirectChildTag(candidate,
				::cppgm::syntax::STAG_INIT_DECLARATOR_LIST);
			result = list != syntax::kNoNode &&
				arena.FirstEdge(list) != syntax::kNoEdge;
		}
		return result;
	};
	const auto generated_label = [&analyzer, &arena,
		&generated_has_declarator](EntityId entity, syntax::NodeId node,
		bool location) {
		const EntityRecord& record = analyzer.program_->entities[entity];
		std::string result = generated_has_declarator(node) ?
			"(unnamed " : "(anonymous ";
		result += record.flavor == NAMED_UNION ? "union" :
			record.flavor == NAMED_CLASS ? "class" : "struct";
		if (location)
			result += " at " + std::to_string(arena.SourceLine(node)) + ':' +
				std::to_string(arena.SourceColumn(node));
		return result + ')';
	};
	const auto class_entity_name = [&analyzer, &function_context_name,
		&generated_source_node, &generated_label,
		&generated_has_declarator](EntityId entity) {
		const syntax::NodeId leaf_node = generated_source_node(entity);
		if (leaf_node == syntax::kNoNode)
			return presentation::RenderEntity(
				*analyzer.program_, entity, true);
		std::vector<std::pair<EntityId, syntax::NodeId> > chain;
		EntityId current = entity;
		while (current != kNoEntity)
		{
			const syntax::NodeId node = generated_source_node(current);
			if (node == syntax::kNoNode) break;
			chain.push_back(std::make_pair(current, node));
			current = analyzer.program_->entities[current].enclosing_class;
		}
		std::string result;
		const EntityRecord& leaf = analyzer.program_->entities[entity];
		if (chain.size() > 1)
		{
			if (leaf.local_context != kNoBinding)
				result = function_context_name(leaf.local_context);
			else if (current != kNoEntity)
				result = presentation::RenderEntity(
					*analyzer.program_, current, true);
			for (std::size_t i = chain.size(); i > 1; --i)
			{
				if (!result.empty()) result += "::";
				result += generated_label(
					chain[i - 1].first, chain[i - 1].second, false);
			}
		}
		else if (!generated_has_declarator(leaf_node))
		{
			if (leaf.enclosing_class != kNoEntity)
				result = presentation::RenderEntity(
					*analyzer.program_, leaf.enclosing_class, true);
			else if (leaf.local_context != kNoBinding)
				result = function_context_name(leaf.local_context);
		}
		if (!result.empty()) result += "::";
		return result + generated_label(entity, leaf_node, true);
	};
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
			is_template_marker(entity) ||
			!IsClassNamedFlavor(record.flavor) ||
			!entity_has_template_context(entity)) continue;
		rendered_class_entities.push_back(entity);
		if (record.local_context != kNoBinding)
		{
			const BindingId context = analyzer.program_->bindings[
				record.local_context].canonical;
			const EntityId function_owner =
				analyzer.program_->bindings[context].member_owner;
			if (analyzer.GetFunction(context).template_specialization ||
				entity_has_template_context(function_owner))
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
		const std::string entity = normalize_entity(
			class_entity_name(class_finalizations_[i]));
		if (std::find(rendered_class_finalizations.begin(),
			rendered_class_finalizations.end(), entity) ==
			rendered_class_finalizations.end())
			rendered_class_finalizations.push_back(entity);
	}
	for (std::size_t i = 0; i < rendered_class_entities.size(); ++i)
	{
		const std::string entity = normalize_entity(
			class_entity_name(rendered_class_entities[i]));
		if (std::find(rendered_class_instantiations.begin(),
			rendered_class_instantiations.end(), entity) ==
			rendered_class_instantiations.end())
			rendered_class_instantiations.push_back(entity);
	}
	const auto is_template_function = [&analyzer,
		&entity_has_template_context](BindingId binding) {
		if (binding == kNoBinding || binding >= analyzer.program_->bindings.size())
			return false;
		binding = analyzer.program_->bindings[binding].canonical;
		const FunctionInfo& function = analyzer.GetFunction(binding);
		const BindingRecord& record = analyzer.program_->bindings[binding];
		const EntityId owner = record.member_owner;
		const bool local_template_context = owner != kNoEntity &&
			analyzer.program_->entities[owner].local_context != kNoBinding &&
			analyzer.GetFunction(
				analyzer.program_->entities[owner].local_context).
				template_specialization;
		if ((record.compiler_generated ||
			function.inherited_constructor_source != kNoBinding) &&
			!local_template_context) return false;
		return function.template_specialization ||
			entity_has_template_context(owner);
	};
	const auto is_required_template_function = [&analyzer](BindingId binding) {
		if (binding == kNoBinding || binding >= analyzer.program_->bindings.size())
			return false;
		binding = analyzer.program_->bindings[binding].canonical;
		const FunctionInfo& function = analyzer.GetFunction(binding);
		const BindingRecord& record = analyzer.program_->bindings[binding];
		if (record.compiler_generated) return false;
		if (function.inherited_constructor_source != kNoBinding) return false;
		if (function.template_specialization) return true;
		if (record.member_owner != kNoEntity)
		{
			const BindingId owner =
				analyzer.program_->entities[record.member_owner].declaration;
			if (owner < analyzer.class_template_explicit_instantiation_states_.size() &&
				(analyzer.class_template_explicit_instantiation_states_[owner] & 2) != 0)
				return false;
		}
		for (EntityId owner = record.member_owner; owner != kNoEntity; )
		{
			const EntityRecord& entity = analyzer.program_->entities[owner];
			if (entity.template_argument_begin != kNoBinding) return true;
			owner = entity.enclosing_class;
		}
		return false;
	};
	const auto function_entity_name = [&analyzer,
		&function_context_name](BindingId binding) {
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
				result = function_context_name(owner.local_context) + "::";
				result += analyzer.program_->names.Get(owner.identity_name == 0 ?
					owner.emission_name : owner.identity_name);
			}
			else result = presentation::RenderEntity(
				*analyzer.program_, record.member_owner, true);
			result += "::";
			if (function.conversion_function)
			{
				std::string target = NormalizeWitnessTypeSpelling(
					presentation::RenderType(*analyzer.program_,
						function.conversion_target));
				std::string prefix;
				if (target.compare(0, 6, "const ") == 0)
				{
					prefix = "const ";
					target.erase(0, 6);
				}
				result += prefix + "operator " + target;
			}
			else result += analyzer.program_->names.Get(terminal);
		}
		else result = presentation::RenderName(
			*analyzer.program_, record.owner, terminal);
		return NormalizeWitnessTypeSpelling(result);
	};
	std::vector<std::string> rendered_instantiations;
	std::vector<std::string> required_entities;
	for (std::size_t i = 0; i < function_instantiations_.size(); ++i)
		if (is_template_function(function_instantiations_[i]))
		{
			const std::string entity = normalize_entity(
				function_entity_name(function_instantiations_[i]));
			if (std::find(rendered_instantiations.begin(),
				rendered_instantiations.end(), entity) ==
				rendered_instantiations.end())
				rendered_instantiations.push_back(entity);
		}
	for (std::size_t i = 0; i < required_definitions_.size(); ++i)
		if (is_required_template_function(required_definitions_[i]))
		{
			const std::string entity = normalize_entity(
				function_entity_name(required_definitions_[i]));
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
	std::vector<std::string> rendered_variables;
	for (std::size_t i = 0; i < variable_instantiations_.size(); ++i)
	{
		BindingId binding = variable_instantiations_[i];
		if (binding >= analyzer.program_->bindings.size()) continue;
		binding = analyzer.program_->bindings[binding].canonical;
		const BindingRecord& record = analyzer.program_->bindings[binding];
		std::string entity;
		if (record.member_owner != kNoEntity)
			entity = presentation::RenderEntity(
				*analyzer.program_, record.member_owner, true) + "::" +
				analyzer.program_->names.Get(record.name);
		else entity = presentation::RenderName(
			*analyzer.program_, record.owner,
			record.presentation_name_override != 0 ?
				record.presentation_name_override : record.name);
		entity = normalize_entity(entity);
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
	source_events_.clear();
	function_specializations_.clear();
	class_specializations_.clear();
	overload_selections_.clear();
	deduction_drops_.clear();
	dependent_class_uses_.clear();
	dependent_alias_uses_.clear();
	dependent_source_uses_.clear();
	function_instantiations_.clear();
	required_definitions_.clear();
	class_instantiations_.clear();
	class_finalizations_.clear();
	variable_instantiations_.clear();
}

}
}
