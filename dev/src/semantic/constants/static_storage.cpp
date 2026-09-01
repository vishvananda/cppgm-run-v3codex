#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <limits>

namespace cppgm
{
namespace semantic
{

const Analyzer::StaticConstantInitializerFact*
Analyzer::FindStaticConstantInitializer(BindingId binding) const
{
	if (binding >= static_constant_initializer_indices_.size()) return 0;
	const std::uint32_t stored =
		static_constant_initializer_indices_[binding];
	if (stored == 0) return 0;
	if (stored > static_constant_initializers_.size())
		ThrowInternalCompilerError("invalid static constant initializer index");
	return &static_constant_initializers_[stored - 1];
}

Analyzer::StaticConstantInitializerFact*
Analyzer::FindMutableStaticConstantInitializer(BindingId binding)
{
	if (binding >= static_constant_initializer_indices_.size()) return 0;
	const std::uint32_t stored =
		static_constant_initializer_indices_[binding];
	if (stored == 0) return 0;
	if (stored > static_constant_initializers_.size())
		ThrowInternalCompilerError("invalid static constant initializer index");
	return &static_constant_initializers_[stored - 1];
}

Analyzer::StaticConstantInitializerFact&
Analyzer::EnsureStaticConstantInitializer(BindingId binding)
{
	if (binding == kNoBinding)
		ThrowInternalCompilerError("invalid static constant initializer owner");
	if (static_constant_initializer_indices_.size() <= binding)
		static_constant_initializer_indices_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	std::uint32_t& stored = static_constant_initializer_indices_[binding];
	if (stored == 0)
	{
		if (static_constant_initializers_.size() >=
			std::numeric_limits<std::uint32_t>::max())
			ThrowSemanticResourceLimit("too many static constant initializers");
		static_constant_initializers_.push_back(
			StaticConstantInitializerFact());
		stored = static_cast<std::uint32_t>(
			static_constant_initializers_.size());
	}
	return static_constant_initializers_[stored - 1];
}

}
}
