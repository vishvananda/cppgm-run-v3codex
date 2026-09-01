#include "lowering/abi/emission_policy.h"

#include "lowering/abi/mangling.h"
#include "lowering/ir/model.h"
#include "lowering/support/errors.h"

#include <string>

namespace cppgm
{
namespace lowering
{
namespace abi
{

namespace
{

bool IsTrivialLifecycleBinding(const semantic::Program& program,
	semantic::BindingId binding)
{
	using namespace semantic;
	if (binding == kNoBinding || binding >= program.bindings.size())
		return false;
	const BindingRecord& record = program.bindings[binding];
	if (record.member_owner == kNoEntity ||
		record.member_owner >= program.entities.size() ||
		record.type == kNoType || !program.types.IsFunction(record.type))
		return false;
	const TypeRecord& function = program.types.Get(record.type);
	if (function.parameter_count != 0) return false;
	return (record.constructor && !record.constructor_base_entry &&
			program.entities[record.member_owner].trivial_default_constructor) ||
		(record.destructor &&
		 program.entities[record.member_owner].trivial_destructor);
}

}

bool IsCompleteBoundaryObject(const semantic::Program& program,
	semantic::TypeId type)
{
	using namespace semantic;
	const TypeRecord* record = &program.types.Get(type);
	while (record->kind == TYPE_QUALIFIED)
	{
		type = record->child;
		record = &program.types.Get(type);
	}
	if (record->kind == TYPE_NAMED)
		return record->entity < program.entities.size() &&
			program.entities[record->entity].complete;
	if (record->kind == TYPE_ARRAY)
		return !record->IsIncompleteArray() &&
			IsCompleteBoundaryObject(program, record->child);
	if (record->kind == TYPE_COMPLEX)
		return IsCompleteBoundaryObject(program, record->child);
	if (record->kind == TYPE_VECTOR || record->kind == TYPE_BITINT)
		return record->dependent_bound_parameter == kNoTemplateParameter;
	return record->kind != TYPE_INVALID && record->kind != TYPE_FUNCTION;
}

void ApplyLifecycleSymbolMetadata(const semantic::Program& program,
	const semantic::DumpNode& node,
	lowering::ir::Program* output,
	lowering::ir::SymbolId symbol,
	abi_mangle::AbiMangleContext* context,
	abi_mangle::AbiMangleStats* stats)
{
	using namespace semantic;
	using namespace lowering::ir;
	const BindingRecord& binding = program.bindings[node.binding];
	const TypeRecord& function = program.types.Get(node.type);
	if (function.kind != TYPE_FUNCTION)
		ThrowLoweringInternal(
			"lifecycle ABI metadata has non-function type");
	const bool trivial_constructor = IsTrivialLifecycleBinding(
		program, node.binding) && binding.constructor &&
		!binding.constructor_base_entry && binding.member_owner != kNoEntity &&
		function.parameter_count == 1;
	const bool trivial_destructor = IsTrivialLifecycleBinding(
		program, node.binding) && binding.destructor;
	Symbol& record = output->symbols[symbol];
	record.lifecycle_base_entry |=
		binding.constructor_base_entry || binding.destructor_base_entry;
	if ((trivial_constructor || trivial_destructor) && !record.no_inline)
		record.force_inline = true;
	if (output->host_object_emission && record.internal_linkage &&
		node.kind == semantic::DUMP_FUNCTION_DEFINITION &&
		(binding.constructor || binding.destructor))
		record.object_output_root = true;
	const bool complete_entry =
		(binding.constructor && !binding.constructor_base_entry) ||
		(binding.destructor && !binding.destructor_base_entry);
	if (complete_entry && binding.member_owner != kNoEntity &&
		program.entities[binding.member_owner].virtual_base_count != 0)
		record.keep_internal_object_alias = false;
	const bool shared_base_entry = complete_entry &&
		binding.member_owner != kNoEntity &&
		program.entities[binding.member_owner].virtual_base_count == 0 &&
		(binding.lifecycle_base_entry == kNoBinding ||
		 binding.lifecycle_base_entry == binding.canonical);
	if (!output->host_object_emission ||
		node.kind != semantic::DUMP_FUNCTION_DEFINITION ||
		!shared_base_entry) return;
	const std::string alias =
		MangleFunction(program, node, true, stats, context);
	if (!alias.empty() && (!record.object_name.valid() ||
		alias != output->strings.get(record.object_name)))
		output->object_aliases.push_back(ObjectAlias(
			output->strings.intern(alias), symbol));
}

bool IsFunctionEmissionDemanded(const semantic::Program& program,
	const semantic::DumpNode& node, bool host_object_emission)
{
	using namespace semantic;
	if (node.binding == kNoBinding) return false;
	const BindingId binding = program.bindings[node.binding].canonical;
	if (program.bindings[binding].builtin_function ==
		BUILTIN_FUNCTION_UNREACHABLE) return false;
	if (node.declaration_only) return true;
	if (host_object_emission &&
		IsTrivialLifecycleBinding(program, binding)) return false;
	return !program.bindings[binding].inline_function ||
		program.bindings[binding].emission_demanded;
}

bool IsFunctionDeclarationBoundaryComplete(const semantic::Program& program,
	const semantic::DumpNode& node)
{
	using namespace semantic;
	if (node.kind != semantic::DUMP_FUNCTION_DECLARATION)
		return true;
	const TypeRecord& function = program.types.Get(node.type);
	if (function.kind != TYPE_FUNCTION) return false;
	const TypeId* parameters = program.types.Parameters(node.type);
	for (std::size_t i = 0; i < function.parameter_count; ++i)
		if (!IsCompleteBoundaryObject(program, parameters[i])) return false;
	return true;
}

bool IsVariableDeclarationOnly(const semantic::Program& program,
	const semantic::DumpNode& node, bool has_initializer)
{
	const semantic::BindingRecord& binding = program.bindings[node.binding];
	return node.declaration_only ||
		program.bindings[binding.canonical].explicit_instantiation_suppressed ||
		(!has_initializer && binding.storage_class == semantic::STORAGE_CLASS_EXTERN);
}

}  // namespace abi
}  // namespace lowering
}  // namespace cppgm
