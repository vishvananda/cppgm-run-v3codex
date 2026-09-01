#include "semantic/analysis/switch.h"
#include "support/exceptions.h"

#include <limits>

namespace cppgm
{
namespace semantic
{

namespace
{

bool TypeAllowsUninitializedTransfer(const Program& program, TypeId type)
{
	type = program.types.RemoveTopCv(type);
	const TypeRecord& record = program.types.Get(type);
	if (record.kind == TYPE_ARRAY)
		return TypeAllowsUninitializedTransfer(program, record.child);
	if (record.kind == TYPE_FUNDAMENTAL)
		return record.fundamental != FUND_VOID;
	if (record.kind == TYPE_POINTER || record.kind == TYPE_MEMBER_POINTER ||
		record.kind == TYPE_BITINT || record.kind == TYPE_COMPLEX ||
		record.kind == TYPE_BLOCK_POINTER || record.kind == TYPE_VECTOR)
		return true;
	if (record.kind != TYPE_NAMED || record.entity == kNoEntity)
		return false;
	const EntityRecord& entity = program.entities[record.entity];
	if (IsEnumNamedFlavor(entity.flavor))
		return true;
	return IsClassNamedFlavor(entity.flavor) &&
		entity.trivial_default_constructor && entity.trivial_destructor;
}

}

void RegisterSwitchEntryDeclaration(const Program& program, ScopeId scope,
	bool local, bool declaration_only, BindingId object, TypeId type,
	bool has_initializer, std::vector<std::uint32_t>* barriers)
{
	if (!local || declaration_only ||
		program.bindings[object].storage_class != STORAGE_CLASS_NONE ||
		program.bindings[object].thread_local_storage ||
		(!has_initializer &&
		 TypeAllowsUninitializedTransfer(program, type)))
		return;
	if (barriers->size() <= scope)
		barriers->resize(
			static_cast<std::size_t>(scope) + 1, 0);
	if ((*barriers)[scope] ==
		std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit("too many automatic declarations in scope");
	++(*barriers)[scope];
}

void ValidateSwitchLabelEntry(ScopeId scope,
	const std::vector<ScopeId>& scope_parents,
	const std::vector<std::uint32_t>& barriers,
	const std::vector<ScopeId>& switch_boundaries)
{
	if (switch_boundaries.empty())
		ThrowInternalCompilerError("switch label has no entry boundary");
	const ScopeId boundary = switch_boundaries.back();
	while (scope != boundary)
	{
		if (scope == kNoScope || scope >= scope_parents.size())
			ThrowInternalCompilerError("switch label scope is outside its switch");
		if (scope < barriers.size() && barriers[scope] != 0)
			ThrowSemanticError(
				"case or default label bypasses variable initialization");
		scope = scope_parents[scope];
	}
}

}
}
