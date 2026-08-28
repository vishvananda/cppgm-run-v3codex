#include "lowering/objects/storage_facts.h"

#include "semantic/semantic.h"
#include "semantic/model/graph.h"

namespace cppgm
{
namespace lowering
{

bool NeedsAggregateStorageAddress(bool namespace_object, bool has_leaf,
	const semantic::BindingRecord& binding)
{
	return (!namespace_object && has_leaf) ||
		(namespace_object && binding.variable_template_specialization);
}

semantic::EntityId LambdaClosureEntity(
	const semantic::Program& program, semantic::TypeId type)
{
	type = program.types.RemoveTopCv(type);
	const semantic::TypeRecord& record = program.types.Get(type);
	if (record.kind != semantic::TYPE_NAMED ||
		record.entity >= program.entities.size() ||
		!program.entities[record.entity].lambda_closure)
		return semantic::kNoEntity;
	return record.entity;
}

bool IsLambdaCaptureMember(
	const semantic::Program& program, semantic::BindingId binding)
{
	const semantic::BindingRecord& member = program.bindings[binding];
	return program.BindingLayout(member).member_offset == 0 &&
		member.member_owner != semantic::kNoEntity &&
		member.member_owner < program.entities.size() &&
		program.entities[member.member_owner].lambda_closure;
}

std::string MissingStorageBindingDetail(
	const semantic::Program& program, semantic::BindingId binding)
{
	std::string detail = std::to_string(binding);
	if (binding >= program.bindings.size()) return detail;
	const semantic::BindingRecord& missing = program.bindings[binding];
	detail += " name=" + program.names.Get(missing.name);
	if (missing.name != 0)
		detail += " presentation=" +
			semantic::RenderBindingPresentation(program, missing);
	detail += " kind=" +
		std::to_string(static_cast<unsigned>(missing.kind));
	return detail;
}

}  // namespace lowering
}  // namespace cppgm
