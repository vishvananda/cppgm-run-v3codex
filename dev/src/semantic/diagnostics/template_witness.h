#pragma once

#include "semantic/model/program.h"
#include "syntax/model/arena.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cppgm
{
namespace semantic
{

class Analyzer;
class RetainedTemplateValidator;

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
	enum OverloadDropReason
	{
		OVERLOAD_DROP_NONE,
		OVERLOAD_DROP_TOO_FEW_ARGUMENTS,
		OVERLOAD_DROP_TOO_MANY_ARGUMENTS,
		OVERLOAD_DROP_BAD_CONVERSION,
		OVERLOAD_DROP_WORSE_CONVERSION,
		OVERLOAD_DROP_BETTER_CANDIDATE_SELECTED
	};

	struct SourceEvent
	{
		struct Drop
		{
			BindingId binding;
			std::uint32_t pattern;
			std::uint8_t reason;
			Drop(BindingId binding_value, std::uint32_t pattern_value,
				std::uint8_t reason_value)
				: binding(binding_value), pattern(pattern_value),
				  reason(reason_value) {}
		};
		SourceEventKind kind;
		syntax::NodeId syntax;
		std::uint32_t pattern;
		BindingId binding;
		std::vector<TemplateArgument> arguments;
		std::vector<std::uint8_t> provenance;
		std::size_t source_column_offset;
		NameId source_name;
		std::size_t source_token;
		std::size_t insertion_ordinal;
		bool suppressed;
		bool allow_substituted_source;
		std::vector<Drop> drops;

		SourceEvent(SourceEventKind kind_value, syntax::NodeId syntax_value,
			std::uint32_t pattern_value, BindingId binding_value,
			const std::vector<TemplateArgument>& argument_values,
			std::size_t explicit_count, std::size_t column_offset,
			std::size_t ordinal);
	};
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

		FunctionSpecializationFact(BindingId binding_value,
			std::uint32_t pattern_value,
			const std::vector<TemplateArgument>& argument_values,
			const std::vector<TemplateArgument>& requested_values);
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

	void BeginTranslationUnit(const std::string& primary_source_file);
	void RecordClassUse(syntax::NodeId syntax, std::uint32_t pattern,
		BindingId binding, const std::vector<TemplateArgument>& arguments,
		std::size_t explicit_count, std::size_t source_column_offset = 0);
	void RecordDeducedClassUse(syntax::NodeId syntax, std::uint32_t pattern,
		BindingId binding, const std::vector<TemplateArgument>& arguments,
		NameId source_name = 0);
	void NoteDependentClassUse(syntax::NodeId syntax, std::uint32_t pattern);
	void NoteDependentSourceUse(syntax::NodeId syntax);
	void RecordInstantiatedClassUse(syntax::NodeId syntax,
		std::uint32_t pattern, BindingId binding,
		const std::vector<TemplateArgument>& arguments);
	void RecordAliasUse(syntax::NodeId syntax, std::uint32_t pattern,
		const std::vector<TemplateArgument>& arguments,
		std::size_t explicit_count);
	void NoteDependentAliasUse(syntax::NodeId syntax, std::uint32_t pattern);
	void RecordFunctionSpecialization(BindingId binding, std::uint32_t pattern,
		const std::vector<TemplateArgument>& arguments,
		const std::vector<TemplateArgument>& requested_arguments);
	void RecordClassSpecialization(BindingId binding,
		const std::vector<TemplateArgument>& arguments,
		std::size_t explicit_count);
	void RecordFunctionCall(syntax::NodeId syntax, std::uint32_t pattern,
		BindingId binding, const std::vector<TemplateArgument>& arguments,
		std::size_t explicit_count);
	void RecordOverloadSelection(BindingId selected,
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
	std::size_t SourceEventMark() const;
	void DiscardSourceEvents(std::size_t mark);
	std::size_t ClosureEventMark() const;
	void DiscardRequireDefinitionEvents(std::size_t mark);
	void FinishTranslationUnit(const Analyzer& analyzer);

	std::string text_;
	std::string debug_text_;
	std::string primary_source_file_;
	bool debug_;
	std::vector<SourceEvent> source_events_;
	std::vector<FunctionSpecializationFact> function_specializations_;
	std::vector<ClassSpecializationFact> class_specializations_;
	std::vector<OverloadSelectionFact> overload_selections_;
	std::vector<DeductionDropFact> deduction_drops_;
	std::vector<std::pair<syntax::NodeId, std::uint32_t> >
		dependent_class_uses_;
	std::vector<std::pair<syntax::NodeId, std::uint32_t> >
		dependent_alias_uses_;
	std::vector<syntax::NodeId> dependent_source_uses_;
	std::vector<BindingId> function_instantiations_;
	std::vector<BindingId> required_definitions_;
	std::vector<EntityId> class_instantiations_;
	std::vector<EntityId> class_finalizations_;
	std::vector<BindingId> variable_instantiations_;
	std::size_t next_insertion_ordinal_;
};

}
}
