#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <limits>

namespace cppgm
{
namespace semantic
{

const ClassTemplatePartialSelection*
Analyzer::FindClassTemplatePartialSelection(BindingId binding) const
{
	if (binding >= class_template_partial_selection_indices_.size()) return 0;
	const std::uint32_t stored =
		class_template_partial_selection_indices_[binding];
	if (stored == 0) return 0;
	if (stored > class_template_partial_selections_.size())
		ThrowInternalCompilerError("invalid class template partial selection index");
	return &class_template_partial_selections_[stored - 1];
}

ClassTemplatePartialSelection&
Analyzer::EnsureClassTemplatePartialSelection(BindingId binding)
{
	if (binding == kNoBinding)
		ThrowInternalCompilerError("invalid class template partial selection owner");
	if (class_template_partial_selection_indices_.size() <= binding)
		class_template_partial_selection_indices_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	std::uint32_t& stored = class_template_partial_selection_indices_[binding];
	if (stored == 0)
	{
		if (class_template_partial_selections_.size() >=
			std::numeric_limits<std::uint32_t>::max())
			ThrowSemanticResourceLimit(
				"too many class template partial selections");
		class_template_partial_selections_.push_back(
			ClassTemplatePartialSelection());
		stored = static_cast<std::uint32_t>(
			class_template_partial_selections_.size());
	}
	return class_template_partial_selections_[stored - 1];
}

}
}
