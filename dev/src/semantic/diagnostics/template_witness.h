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

// Optional sink for the PA19+ template-decision diagnostic.  The driver owns
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

	enum SourceEventKind
	{
		SOURCE_CLASS_USE,
		SOURCE_ALIAS_USE,
		SOURCE_VARIABLE_USE,
		SOURCE_FUNCTION_CALL
	};

	struct SourceEvent
	{
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

		SourceEvent(SourceEventKind kind_value, syntax::NodeId syntax_value,
			std::uint32_t pattern_value, BindingId binding_value,
			const std::vector<TemplateArgument>& argument_values,
			std::size_t explicit_count, std::size_t column_offset,
			std::size_t ordinal);
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
	void RecordFunctionInstantiation(BindingId binding);
	void RecordRequireDefinition(BindingId binding);
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
	std::vector<std::pair<syntax::NodeId, std::uint32_t> >
		dependent_class_uses_;
	std::vector<std::pair<syntax::NodeId, std::uint32_t> >
		dependent_alias_uses_;
	std::vector<BindingId> function_instantiations_;
	std::vector<BindingId> required_definitions_;
	std::vector<EntityId> class_finalizations_;
	std::vector<BindingId> variable_instantiations_;
	std::size_t next_insertion_ordinal_;
};

}
}
