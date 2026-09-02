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

std::size_t FindNameTokenInRange(const SyntaxArena& arena,
	syntax::NodeId syntax, const std::string& name)
{
	if (syntax == syntax::kNoNode) return std::numeric_limits<std::size_t>::max();
	const std::size_t first = arena.TokenFirst(syntax);
	const std::size_t last = arena.TokenLast(syntax);
	if (first >= last || last > arena.TokenCount())
		return std::numeric_limits<std::size_t>::max();
	for (std::size_t token = last; token != first; )
	{
		--token;
		if (arena.TokenSpelling(token) == name) return token;
	}
	return std::numeric_limits<std::size_t>::max();
}

bool IsInsideTemplateDeclaration(const SyntaxArena& arena,
	std::size_t token)
{
	for (syntax::NodeId node = 0; node < arena.Nodes(); ++node)
		if (arena.IsTag(node, ::cppgm::syntax::STAG_TEMPLATE_DECLARATION) &&
			arena.TokenFirst(node) <= token && token < arena.TokenLast(node))
			return true;
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
	  source_column_offset(column_offset),
	  source_token(std::numeric_limits<std::size_t>::max()),
	  insertion_ordinal(ordinal), suppressed(false),
	  allow_substituted_source(false)
{
	for (std::size_t i = 0; i < explicit_count && i < provenance.size(); ++i)
		provenance[i] = 0;
}

TemplateWitnessObserver::FunctionSpecializationFact::
	FunctionSpecializationFact(BindingId binding_value,
		const std::vector<TemplateArgument>& argument_values,
		const std::vector<TemplateArgument>& requested_values)
	: binding(binding_value), arguments(argument_values),
	  provenance(argument_values.size(), 1)
{
	for (std::size_t i = 0; i < provenance.size() &&
		i < requested_values.size(); ++i)
		if (requested_values[i].type == kNoType) provenance[i] = 2;
}

TemplateWitnessObserver::TemplateWitnessObserver()
	: text_(), primary_source_file_(), source_events_(),
	  function_specializations_(), dependent_class_uses_(),
	  dependent_alias_uses_(), function_instantiations_(),
	  required_definitions_(), class_finalizations_(),
	  variable_instantiations_(), next_insertion_ordinal_(0) {}

const std::string& TemplateWitnessObserver::Text() const
{
	return text_;
}

bool Analyzer::TemplateWitnessSourceUseEnabled() const
{
	if (!template_witness_ || class_template_member_replay_depth_ != 0)
		return false;
	if (current_function_context_ == kNoBinding) return true;
	const BindingId current =
		program_->bindings[current_function_context_].canonical;
	const FunctionInfo& function = GetFunction(current);
	if (function.template_specialization) return false;
	const EntityId owner = program_->bindings[current].member_owner;
	return owner == kNoEntity ||
		program_->entities[owner].template_argument_begin == kNoBinding;
}

void Analyzer::RecordFunctionTemplateSourceCall(NodeId syntax,
	BindingId selected, std::size_t explicit_count)
{
	if (!TemplateWitnessSourceUseEnabled() || syntax == kNoNode ||
		selected == kNoBinding) return;
	selected = program_->bindings[selected].canonical;
	const FunctionInfo& function = GetFunction(selected);
	if (!function.template_specialization ||
		function.template_pattern == kNoDumpEdge ||
		function.template_pattern >= function_templates_.size()) return;
	const BindingRecord& record = program_->bindings[selected];
	if (record.template_argument_begin == kNoBinding) return;
	const std::vector<TemplateArgument> arguments = StoredTemplateArguments(
		record.template_argument_begin, record.template_argument_count);
	template_witness_->RecordFunctionCall(syntax, function.template_pattern,
		selected, arguments, explicit_count);
}

void TemplateWitnessObserver::BeginTranslationUnit(
	const std::string& primary_source_file)
{
	primary_source_file_ = primary_source_file;
	source_events_.clear();
	function_specializations_.clear();
	dependent_class_uses_.clear();
	dependent_alias_uses_.clear();
	function_instantiations_.clear();
	required_definitions_.clear();
	class_finalizations_.clear();
	variable_instantiations_.clear();
	next_insertion_ordinal_ = 0;
}

void TemplateWitnessObserver::RecordClassUse(syntax::NodeId syntax,
	std::uint32_t pattern, BindingId binding,
	const std::vector<TemplateArgument>& arguments, std::size_t explicit_count,
	std::size_t source_column_offset)
{
	if (std::find(dependent_class_uses_.begin(), dependent_class_uses_.end(),
		std::make_pair(syntax, pattern)) != dependent_class_uses_.end()) return;
	source_events_.push_back(SourceEvent(SOURCE_CLASS_USE, syntax, pattern,
		binding, arguments, explicit_count, source_column_offset,
		next_insertion_ordinal_++));
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
	const std::vector<TemplateArgument>& arguments,
	const std::vector<TemplateArgument>& requested_arguments)
{
	for (std::size_t i = 0; i < function_specializations_.size(); ++i)
		if (function_specializations_[i].binding == binding) return;
	function_specializations_.push_back(FunctionSpecializationFact(
		binding, arguments, requested_arguments));
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
	std::vector<std::uint8_t> used_tokens(arena.TokenCount(), 0);
	for (std::size_t i = 0; i < source_events_.size(); ++i)
	{
		const SourceEvent& event = source_events_[i];
		NameId name = 0;
		if (event.kind == SOURCE_CLASS_USE &&
			event.pattern < analyzer.class_templates_.size())
			name = analyzer.class_templates_[event.pattern].name;
		else if (event.kind == SOURCE_ALIAS_USE &&
			event.pattern < analyzer.alias_templates_.size())
			name = analyzer.alias_templates_[event.pattern].name;
		if (name != 0 && event.syntax != syntax::kNoNode)
		{
			const std::size_t name_token = FindNameTokenInRange(arena,
				event.syntax, analyzer.program_->names.Get(name));
			if (name_token != std::numeric_limits<std::size_t>::max())
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
		source_events_[i].source_token = FindTemplateNameToken(arena,
			primary_source_file_, analyzer.program_->names.Get(name), used_tokens);
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
				<< RenderWitnessArgument(
					*analyzer.program_, event.arguments[argument])
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
						<< RenderWitnessArgument(
							*analyzer.program_,
							bindings.fixed_arguments[parameter]) << '\n';
			}
		}
		text_ += output.str();
	}
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
	std::vector<EntityId> class_instantiations;
	for (EntityId entity = 0; entity < analyzer.program_->entities.size();
		++entity)
	{
		const EntityRecord& record = analyzer.program_->entities[entity];
		if (!record.complete || !record.layout_complete || record.lambda_closure ||
			record.template_argument_begin != kNoBinding ||
			is_template_marker(entity) ||
			!IsClassNamedFlavor(record.flavor) ||
			!entity_has_template_context(entity)) continue;
		class_instantiations.push_back(entity);
		if (record.local_context != kNoBinding &&
			analyzer.GetFunction(record.local_context).template_specialization)
			if (std::find(class_finalizations_.begin(),
				class_finalizations_.end(), entity) ==
				class_finalizations_.end())
				class_finalizations_.push_back(entity);
	}
	std::vector<std::string> rendered_class_finalizations;
	std::vector<std::string> rendered_class_instantiations;
	for (std::size_t i = 0; i < class_finalizations_.size(); ++i)
	{
		const std::string entity = presentation::RenderEntity(
			*analyzer.program_, class_finalizations_[i], true);
		if (std::find(rendered_class_finalizations.begin(),
			rendered_class_finalizations.end(), entity) ==
			rendered_class_finalizations.end())
			rendered_class_finalizations.push_back(entity);
	}
	for (std::size_t i = 0; i < class_instantiations.size(); ++i)
	{
		const std::string entity = presentation::RenderEntity(
			*analyzer.program_, class_instantiations[i], true);
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
		if (function.template_specialization) return true;
		for (EntityId owner = record.member_owner; owner != kNoEntity; )
		{
			const EntityRecord& entity = analyzer.program_->entities[owner];
			if (entity.template_argument_begin != kNoBinding) return true;
			owner = entity.enclosing_class;
		}
		return false;
	};
	const auto function_context_name = [&analyzer](BindingId binding) {
		binding = analyzer.program_->bindings[binding].canonical;
		const FunctionInfo& function = analyzer.GetFunction(binding);
		const BindingRecord& record = analyzer.program_->bindings[binding];
		const NameId terminal = function.presentation_name_override != 0 ?
			function.presentation_name_override :
			record.presentation_name_override != 0 ?
				record.presentation_name_override : record.name;
		std::string result = presentation::RenderName(
			*analyzer.program_, record.owner, terminal);
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
				const TypeRecord& function_type =
					analyzer.program_->types.Get(function.type);
				if ((function_type.cv & CV_CONST) != 0) result += "const ";
				result += "operator ";
				result += NormalizeWitnessTypeSpelling(
					presentation::RenderType(*analyzer.program_,
						function.conversion_target));
			}
			else result += analyzer.program_->names.Get(terminal);
		}
		else result = presentation::RenderName(
			*analyzer.program_, record.owner, terminal);
		return result;
	};
	std::vector<std::string> rendered_instantiations;
	std::vector<std::string> required_entities;
	for (std::size_t i = 0; i < function_instantiations_.size(); ++i)
		if (is_template_function(function_instantiations_[i]))
		{
			const std::string entity =
				function_entity_name(function_instantiations_[i]);
			if (std::find(rendered_instantiations.begin(),
				rendered_instantiations.end(), entity) ==
				rendered_instantiations.end())
				rendered_instantiations.push_back(entity);
		}
	for (std::size_t i = 0; i < required_definitions_.size(); ++i)
		if (is_required_template_function(required_definitions_[i]))
		{
			const std::string entity =
				function_entity_name(required_definitions_[i]);
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
		if (std::find(rendered_variables.begin(), rendered_variables.end(),
			entity) == rendered_variables.end())
			rendered_variables.push_back(entity);
	}
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
	dependent_class_uses_.clear();
	dependent_alias_uses_.clear();
	function_instantiations_.clear();
	required_definitions_.clear();
	class_finalizations_.clear();
	variable_instantiations_.clear();
}

}
}
