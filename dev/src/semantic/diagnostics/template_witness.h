#pragma once

#include "semantic/model/program.h"
#include "semantic/presentation/source_identity.h"
#include "syntax/model/arena.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace cppgm
{
namespace semantic
{

class Analyzer;
class RetainedTemplateValidator;
struct TemplateParameter;

// Optional sink for the staged template-decision diagnostic.  The driver owns
// the observer.  Analyzer publishes into it synchronously while the syntax
// arena and template model are both alive.
class TemplateWitnessObserver
{
public:
	explicit TemplateWitnessObserver(bool debug = false);
	const std::string& Text() const;
	const std::string& DebugText() const;

private:
	friend class Analyzer;
	friend class RetainedTemplateValidator;

	enum SourceEventKind
	{
		SOURCE_CLASS_USE,
		SOURCE_ALIAS_USE,
		SOURCE_VARIABLE_USE,
		SOURCE_FUNCTION_CALL
	};
	enum SemanticSourceKind
	{
		SEMANTIC_SOURCE_CLASS_TEMPLATE,
		SEMANTIC_SOURCE_CLASS_OBJECT_TYPE,
		SEMANTIC_SOURCE_ALIAS_TEMPLATE,
		SEMANTIC_SOURCE_VARIABLE_TEMPLATE,
		SEMANTIC_SOURCE_FUNCTION_TEMPLATE
	};
	enum SemanticSourceResolution
	{
		SEMANTIC_SOURCE_REPLAY_REQUIRED,
		SEMANTIC_SOURCE_CURRENT_PARTIAL,
		SEMANTIC_SOURCE_DECLARATION_COMPLETE
	};
	enum SourceOccurrenceRole
	{
		SOURCE_OCCURRENCE_EVALUATED = 1,
		SOURCE_OCCURRENCE_UNEVALUATED_DECLARATION = 2,
		SOURCE_OCCURRENCE_DEFERRED_DEFINITION = 4,
		SOURCE_OCCURRENCE_SPECIALIZATION_REPLAY = 8,
		SOURCE_OCCURRENCE_DEPENDENT = 16
	};
	struct SourceOccurrenceFact
	{
		syntax::NodeId syntax;
		std::uint8_t roles;

		SourceOccurrenceFact(syntax::NodeId syntax_value,
			SourceOccurrenceRole role)
			: syntax(syntax_value), roles(static_cast<std::uint8_t>(role)) {}
	};
	static_assert(sizeof(SourceOccurrenceFact) == 8,
		"source occurrence roles must remain compact");
	enum VariableOccurrenceEvaluation
	{
		VARIABLE_OCCURRENCE_EVALUATED,
		VARIABLE_OCCURRENCE_CONSTANT_EVALUATED,
		VARIABLE_OCCURRENCE_UNEVALUATED
	};
	enum VariableOccurrencePhase
	{
		VARIABLE_OCCURRENCE_SOURCE,
		VARIABLE_OCCURRENCE_DEFERRED_DEFINITION,
		VARIABLE_OCCURRENCE_DEFINITION_DEMAND,
		VARIABLE_OCCURRENCE_SPECIALIZATION_REPLAY
	};
	enum VariableOccurrenceProperty
	{
		VARIABLE_OCCURRENCE_CONSTANT_BINDING = 1,
		VARIABLE_OCCURRENCE_VARIABLE_TEMPLATE = 2,
		VARIABLE_OCCURRENCE_REFERENCE_TYPE = 4
	};
	struct VariableOccurrenceFact
	{
		syntax::NodeId syntax;
		BindingId binding;
		std::uint8_t evaluation;
		std::uint8_t phase;
		std::uint8_t owning_class_context;
		std::uint8_t properties;

		VariableOccurrenceFact(syntax::NodeId syntax_value,
			BindingId binding_value, VariableOccurrenceEvaluation evaluation_value,
			VariableOccurrencePhase phase_value, bool owning_class_context_value,
			std::uint8_t properties_value)
			: syntax(syntax_value), binding(binding_value),
			  evaluation(static_cast<std::uint8_t>(evaluation_value)),
			  phase(static_cast<std::uint8_t>(phase_value)),
			  owning_class_context(owning_class_context_value ? 1 : 0),
			  properties(properties_value) {}
		bool Has(VariableOccurrenceProperty property) const
		{
			return (properties & static_cast<std::uint8_t>(property)) != 0;
		}
	};
	static_assert(sizeof(VariableOccurrenceFact) == 12,
		"variable occurrence fact grew");
	struct SemanticSourceFact
	{
		syntax::NodeId owner;
		syntax::NodeId syntax;
		std::uint32_t semantic_index;
		std::uint8_t kind;
		std::uint8_t resolution;
		std::uint16_t explicit_count;

		SemanticSourceFact(syntax::NodeId owner_value,
			syntax::NodeId syntax_value, std::uint32_t semantic_index_value,
			SemanticSourceKind kind_value,
			SemanticSourceResolution resolution_value,
			std::size_t explicit_count_value)
			: owner(owner_value), syntax(syntax_value),
			  semantic_index(semantic_index_value),
			  kind(static_cast<std::uint8_t>(kind_value)),
			  resolution(static_cast<std::uint8_t>(resolution_value)),
			  explicit_count(static_cast<std::uint16_t>(
				explicit_count_value > 65535 ? 65535 : explicit_count_value)) {}
	};
	struct RetainedMemberSourceFact
	{
		syntax::NodeId owner;
		syntax::NodeId source;
		NameId member_name;
		std::uint32_t pattern;
		std::uint32_t partial_pattern;
		BindingId concrete_owner;

		RetainedMemberSourceFact(syntax::NodeId owner_value,
			syntax::NodeId source_value, NameId member_name_value,
			std::uint32_t pattern_value, std::uint32_t partial_pattern_value,
			BindingId concrete_owner_value)
			: owner(owner_value), source(source_value),
			  member_name(member_name_value), pattern(pattern_value),
			  partial_pattern(partial_pattern_value),
			  concrete_owner(concrete_owner_value) {}
	};
	static_assert(sizeof(RetainedMemberSourceFact) == 24,
		"retained member source facts must stay compact");
	struct RetainedAliasQualifierFact
	{
		syntax::NodeId owner;
		syntax::NodeId alias_source;
		syntax::NodeId qualifier_source;
		std::uint32_t pattern;
		std::uint32_t partial_pattern;

		RetainedAliasQualifierFact(syntax::NodeId owner_value,
			syntax::NodeId alias_source_value,
			syntax::NodeId qualifier_source_value,
			std::uint32_t pattern_value)
			: owner(owner_value), alias_source(alias_source_value),
			  qualifier_source(qualifier_source_value), pattern(pattern_value),
			  partial_pattern(std::numeric_limits<std::uint32_t>::max()) {}
	};
	static_assert(sizeof(RetainedAliasQualifierFact) == 20,
		"retained alias qualifier facts must stay compact");
	enum OverloadDropReason
	{
		OVERLOAD_DROP_NONE,
		OVERLOAD_DROP_TOO_FEW_ARGUMENTS,
		OVERLOAD_DROP_TOO_MANY_ARGUMENTS,
		OVERLOAD_DROP_BAD_CONVERSION,
		OVERLOAD_DROP_WORSE_CONVERSION,
		OVERLOAD_DROP_BETTER_CANDIDATE_SELECTED,
		OVERLOAD_DROP_INCONSISTENT,
		OVERLOAD_DROP_NON_DEDUCED_MISMATCH,
		OVERLOAD_DROP_SUBSTITUTION_FAILURE,
		OVERLOAD_DROP_EXPLICIT_NOT_ALLOWED
	};
	enum CandidateDeclarationOriginKind
	{
		CANDIDATE_ORIGIN_TEMPLATE_PATTERN,
		CANDIDATE_ORIGIN_BINDING,
		CANDIDATE_ORIGIN_IMPLICIT_CONSTRUCTOR_FAMILY,
		CANDIDATE_ORIGIN_IMPLICIT_ASSIGNMENT_FAMILY
	};

	struct SourceEvent
	{
		struct Drop
		{
			BindingId binding;
			std::uint32_t declaration_origin;
			std::uint8_t reason;
			std::uint8_t origin_kind;
			Drop(BindingId binding_value, std::uint32_t declaration_value,
				CandidateDeclarationOriginKind origin_kind_value,
				std::uint8_t reason_value)
				: binding(binding_value), declaration_origin(declaration_value),
				  reason(reason_value),
				  origin_kind(static_cast<std::uint8_t>(origin_kind_value)) {}
		};
		static_assert(sizeof(Drop) == 12,
			"candidate origins must reuse the compact drop record padding");
		SourceEventKind kind;
		syntax::NodeId syntax, component_syntax;
		std::uint32_t pattern;
		BindingId binding;
		EntityId qualifier_entity;
		std::uint32_t qualifier_pattern;
		std::uint32_t qualifier_partial_pattern;
		std::vector<TemplateArgument> arguments;
		std::vector<std::uint8_t> provenance;
		std::vector<std::uint32_t> parameter_offsets;
		std::vector<TemplateArgument> specialization_arguments;
		std::vector<std::uint32_t> specialization_offsets;
		std::vector<std::uint8_t> specialization_packs;
		std::size_t source_column_offset;
		NameId source_name;
		std::size_t source_token;
		std::size_t insertion_ordinal;
		bool suppressed;
		bool allow_substituted_source;
		bool complete_at_source;
		std::uint8_t selection_kind;
		std::vector<Drop> drops;

		SourceEvent(SourceEventKind kind_value, syntax::NodeId syntax_value,
			std::uint32_t pattern_value, BindingId binding_value,
			const std::vector<TemplateArgument>& argument_values,
			std::size_t explicit_count, std::size_t column_offset,
			std::size_t ordinal);
	};
	static_assert(sizeof(SourceEvent) == 240,
		"source events must keep typed qualifier provenance compact");
	struct DeductionDropFact
	{
		syntax::NodeId syntax;
		std::uint32_t pattern;
		std::uint8_t reason;
		bool consumed;
		DeductionDropFact(syntax::NodeId syntax_value,
			std::uint32_t pattern_value, std::uint8_t reason_value)
			: syntax(syntax_value), pattern(pattern_value),
			  reason(reason_value), consumed(false) {}
	};
	struct OverloadSelectionFact
	{
		BindingId selected;
		std::vector<SourceEvent::Drop> drops;
		bool consumed;
		OverloadSelectionFact(BindingId selected_value,
			const std::vector<SourceEvent::Drop>& drop_values)
			: selected(selected_value), drops(drop_values), consumed(false) {}
	};
	struct FunctionSpecializationFact
	{
		BindingId binding;
		std::uint32_t pattern;
		std::vector<TemplateArgument> arguments;
		std::vector<std::uint8_t> provenance;
		std::vector<std::uint32_t> parameter_offsets;

		FunctionSpecializationFact(BindingId binding_value,
			std::uint32_t pattern_value,
			const std::vector<TemplateArgument>& argument_values,
			const std::vector<TemplateArgument>& requested_values,
			const std::vector<std::uint32_t>& parameter_offset_values);
	};
	struct ClassSpecializationFact
	{
		BindingId binding;
		std::vector<TemplateArgument> arguments;
		std::size_t explicit_count;

		ClassSpecializationFact(BindingId binding_value,
			const std::vector<TemplateArgument>& argument_values,
			std::size_t explicit_count_value)
			: binding(binding_value), arguments(argument_values),
			  explicit_count(explicit_count_value) {}
	};
	struct ClassTemplateSourceFact
	{
		syntax::NodeId syntax;
		std::uint32_t pattern;
		BindingId binding;
		std::uint32_t selected_partial;
		std::uint16_t presentation_arity;
		bool replayed;
		std::vector<syntax::NodeId> argument_syntax;

		ClassTemplateSourceFact(syntax::NodeId syntax_value,
			std::uint32_t pattern_value, BindingId binding_value,
			std::uint32_t selected_partial_value,
			std::size_t presentation_arity_value,
			const std::vector<syntax::NodeId>& argument_syntax_value,
			bool replayed_value)
			: syntax(syntax_value), pattern(pattern_value), binding(binding_value),
			  selected_partial(selected_partial_value),
			  presentation_arity(static_cast<std::uint16_t>(
				presentation_arity_value > 65535 ? 65535 :
				presentation_arity_value)), replayed(replayed_value),
			  argument_syntax(argument_syntax_value) {}
	};
	static_assert(sizeof(ClassTemplateSourceFact) == 48,
		"class template source facts must stay compact");
	struct VariableSpecializationFact
	{
		BindingId binding;
		std::uint32_t primary_pattern;
		std::vector<TemplateArgument> arguments;
		std::vector<TemplateArgument> specialization_arguments;
		std::vector<std::uint32_t> specialization_offsets;
		std::vector<std::uint8_t> specialization_packs;
		std::uint8_t selection_kind;

		VariableSpecializationFact(BindingId binding_value,
			std::uint32_t primary_pattern_value,
			const std::vector<TemplateArgument>& argument_values,
			const std::vector<TemplateArgument>& specialization_values,
			const std::vector<std::uint32_t>& specialization_offset_values,
			const std::vector<std::uint8_t>& specialization_pack_values,
			std::uint8_t selection_kind_value)
			: binding(binding_value), primary_pattern(primary_pattern_value),
			  arguments(argument_values),
			  specialization_arguments(specialization_values),
			  specialization_offsets(specialization_offset_values),
			  specialization_packs(specialization_pack_values),
			  selection_kind(selection_kind_value) {}
	};
	typedef std::vector<std::pair<std::string, std::string> >
		EntityReplacements;

	std::string ElideEntities(std::string spelling,
		const EntityReplacements& replacements) const;
	std::string NormalizeEntity(std::string spelling,
		const EntityReplacements& replacements) const;
	std::string OverloadName(const Analyzer& analyzer,
		const SourceEvent::Drop& drop,
		const EntityReplacements& replacements) const;
	std::string FunctionContextName(
		const Analyzer& analyzer, BindingId binding) const;
	bool EntityHasTemplateContext(
		const Analyzer& analyzer, EntityId entity) const;
	bool IsTemplateMarker(const Analyzer& analyzer, EntityId entity) const;
	syntax::NodeId GeneratedSourceNode(
		const Analyzer& analyzer, EntityId entity) const;
	bool GeneratedHasDeclarator(
		const syntax::SyntaxArena& arena, syntax::NodeId node) const;
	std::string GeneratedLabel(const Analyzer& analyzer,
		const syntax::SyntaxArena& arena, EntityId entity,
		syntax::NodeId node, bool location) const;
	std::string ClassEntityName(const Analyzer& analyzer,
		const syntax::SyntaxArena& arena, EntityId entity) const;
	bool OwnerIsExplicitSpecialization(
		const Analyzer& analyzer, EntityId owner) const;
	bool IsTemplateFunction(
		const Analyzer& analyzer, BindingId binding) const;
	bool IsRequiredTemplateFunction(
		const Analyzer& analyzer, BindingId binding) const;
	std::string FunctionEntityName(
		const Analyzer& analyzer, BindingId binding) const;
	std::string SourceDistinguishedClassName(const Analyzer& analyzer,
		const syntax::SyntaxArena& arena, EntityId entity) const;
	void PrepareSourceEvents(
		const Analyzer& analyzer, EntityReplacements* replacements);
	void RenderSourceEvents(const Analyzer& analyzer,
		const EntityReplacements& replacements);
	std::string RenderSourceSelection(const Analyzer& analyzer,
		const syntax::SyntaxArena& arena, const SourceEvent& event,
		const EntityReplacements& replacements,
		const std::vector<TemplateParameter>** parameters) const;
	std::string RenderSourceBindings(const Analyzer& analyzer,
		const syntax::SyntaxArena& arena, const SourceEvent& event,
		const EntityReplacements& replacements,
		const std::vector<TemplateParameter>* parameters) const;
	std::string RenderSourceSpecializations(const Analyzer& analyzer,
		const SourceEvent& event,
		const EntityReplacements& replacements) const;
	std::string RenderSourceDrops(const Analyzer& analyzer,
		const SourceEvent& event,
		const EntityReplacements& replacements) const;
	void RenderClosureEvents(const Analyzer& analyzer,
		const EntityReplacements& replacements);

	void BeginTranslationUnit(const std::string& primary_source_file);
	void NoteSemanticSourceFact(syntax::NodeId owner,
		syntax::NodeId syntax, std::uint32_t semantic_index,
		SemanticSourceKind kind, SemanticSourceResolution resolution,
		std::size_t explicit_count);
	bool IsRetainedDependentSourceUse(syntax::NodeId syntax) const;
	void NoteRetainedMemberSource(syntax::NodeId owner,
		syntax::NodeId source, NameId member_name, std::uint32_t pattern,
		std::uint32_t partial_pattern, BindingId concrete_owner);
	void NoteRetainedAliasQualifier(syntax::NodeId owner,
		syntax::NodeId alias_source, syntax::NodeId qualifier_source,
		std::uint32_t pattern);
	void ResolveRetainedAliasQualifierPartial(syntax::NodeId owner,
		std::uint32_t pattern, std::uint32_t partial_pattern);
	void ApplyRetainedAliasQualifier(SourceEvent* event,
		std::uint32_t alias_owner_pattern) const;
	void RecordRetainedMemberClassUses(NameId member_name,
		std::uint32_t pattern, BindingId specialization,
		std::uint32_t selected_partial,
		const std::vector<TemplateArgument>& arguments);
	void RecordSemanticCurrentClassUses(syntax::NodeId owner,
		std::uint32_t pattern,
		const std::vector<TemplateArgument>& arguments);
	void RecordClassUse(syntax::NodeId syntax, std::uint32_t pattern,
		BindingId binding, const std::vector<TemplateArgument>& arguments,
		std::size_t explicit_count, std::size_t source_column_offset = 0,
		bool replayed = false);
	void RecordDeducedClassUse(syntax::NodeId syntax, std::uint32_t pattern,
		BindingId binding, const std::vector<TemplateArgument>& arguments,
		NameId source_name = 0);
	void NoteDependentClassUse(syntax::NodeId syntax, std::uint32_t pattern);
	void NoteDependentSourceUse(syntax::NodeId syntax);
	void NoteSourceOccurrence(syntax::NodeId syntax, SourceOccurrenceRole role);
	bool HasSourceOccurrenceRole(syntax::NodeId syntax,
		std::uint8_t roles) const;
	void NoteResolvedSourceUse(syntax::NodeId syntax);
	void RecordInstantiatedClassUse(syntax::NodeId syntax,
		std::uint32_t pattern, BindingId binding,
		const std::vector<TemplateArgument>& arguments,
		std::size_t explicit_count);
	void RecordRetainedClassOwnerUse(syntax::NodeId syntax,
		std::uint32_t pattern,
		const std::vector<TemplateArgument>& arguments);
	void RecordCurrentClassUse(syntax::NodeId syntax, std::uint32_t pattern,
		BindingId binding, const std::vector<TemplateArgument>& arguments,
		std::size_t explicit_count);
	void RecordAliasUse(syntax::NodeId syntax, std::uint32_t pattern,
		const std::vector<TemplateArgument>& arguments,
		std::size_t explicit_count, EntityId qualifier_entity,
		std::uint32_t alias_owner_pattern);
	void NoteDependentAliasUse(syntax::NodeId syntax, std::uint32_t pattern,
		const std::vector<TemplateArgument>& arguments,
		std::size_t explicit_count, EntityId qualifier_entity,
		std::uint32_t alias_owner_pattern);
	void RecordFunctionSpecialization(BindingId binding, std::uint32_t pattern,
		const std::vector<TemplateArgument>& arguments,
		const std::vector<TemplateArgument>& requested_arguments,
		const std::vector<std::uint32_t>& parameter_offsets);
	void RecordClassSpecialization(BindingId binding,
		const std::vector<TemplateArgument>& arguments,
		std::size_t explicit_count);
	void NoteClassTemplateSource(syntax::NodeId syntax,
		std::uint32_t pattern, BindingId binding,
		std::uint32_t selected_partial,
		std::size_t presentation_arity,
		const std::vector<syntax::NodeId>& argument_syntax, bool replayed);
	void RecordVariableSpecialization(BindingId binding,
		std::uint32_t primary_pattern,
		const std::vector<TemplateArgument>& arguments,
		const std::vector<TemplateArgument>& specialization_arguments,
		const std::vector<std::uint32_t>& specialization_offsets,
		const std::vector<std::uint8_t>& specialization_packs,
		std::uint8_t selection_kind);
	void RecordVariableUse(syntax::NodeId syntax, BindingId binding,
		std::size_t explicit_count);
	void RecordFunctionCall(syntax::NodeId syntax,
		syntax::NodeId component_syntax, std::uint32_t pattern,
		BindingId binding, const std::vector<TemplateArgument>& arguments,
		std::size_t explicit_count, SourceOccurrenceRole role);
	void RecordOverloadSelection(const Analyzer& analyzer, BindingId selected,
		const std::vector<BindingId>& candidates,
		const std::vector<std::uint8_t>& reasons);
	void RecordDeductionDrop(syntax::NodeId syntax, std::uint32_t pattern,
		std::uint8_t reason);
	void DiscardOverloadSelection(BindingId selected);
	void RecordFunctionInstantiation(BindingId binding);
	void RecordRequireDefinition(BindingId binding);
	void RecordClassInstantiation(EntityId entity);
	void RecordClassFinalization(EntityId entity);
	void RecordVariableInstantiation(BindingId binding);
	void RecordVariableOccurrence(const syntax::SyntaxArena& arena,
		syntax::NodeId syntax, BindingId binding,
		bool owning_class_context, VariableOccurrenceEvaluation evaluation,
		VariableOccurrencePhase phase, std::uint8_t properties,
		bool filter_publication_to_source);
	std::size_t SourceEventMark() const;
	void DiscardSourceEvents(std::size_t mark);
	std::size_t ClosureEventMark() const;
	void DiscardRequireDefinitionEvents(std::size_t mark);
	void FinishTranslationUnit(const Analyzer& analyzer);

	std::string text_;
	std::string debug_text_;
	std::string primary_source_file_;
	bool debug_;
	std::vector<SemanticSourceFact> semantic_source_facts_;
	std::vector<RetainedMemberSourceFact> retained_member_source_facts_;
	std::vector<RetainedAliasQualifierFact> retained_alias_qualifier_facts_;
	std::vector<SourceEvent> source_events_;
	std::vector<FunctionSpecializationFact> function_specializations_;
	std::vector<ClassSpecializationFact> class_specializations_;
	std::vector<ClassTemplateSourceFact> class_template_source_facts_;
	std::vector<presentation::TemplateEntityArgumentLimit>
		entity_argument_limits_;
	std::vector<VariableSpecializationFact> variable_specializations_;
	std::vector<OverloadSelectionFact> overload_selections_;
	std::vector<DeductionDropFact> deduction_drops_;
	std::vector<std::pair<syntax::NodeId, std::uint32_t> >
		dependent_class_uses_;
	std::vector<std::pair<syntax::NodeId, std::uint32_t> >
		dependent_alias_uses_;
	std::vector<syntax::NodeId> dependent_source_uses_;
	std::vector<SourceOccurrenceFact> source_occurrences_;
	std::vector<VariableOccurrenceFact> variable_occurrences_;
	std::vector<syntax::NodeId> resolved_source_uses_;
	std::vector<BindingId> function_instantiations_;
	std::vector<BindingId> required_definitions_;
	std::vector<EntityId> class_instantiations_;
	std::vector<EntityId> class_finalizations_;
	std::vector<BindingId> variable_instantiations_;
	std::size_t next_insertion_ordinal_;
};

}
}
