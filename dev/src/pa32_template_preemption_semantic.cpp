#include "semantic/analysis/analyzer.h"

#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{
namespace
{

bool TypesContainLocalContext(const Program& program,
	const std::vector<TemplateArgument>& arguments)
{
	std::vector<TypeId> pending;
	const auto push_type = [&pending](TypeId type) {
		// Dependent template argument placeholders can deliberately have no
		// concrete type yet.  They cannot contribute a local concrete owner.
		if (type != kNoType) pending.push_back(type);
	};
	for (std::size_t argument = arguments.size(); argument != 0; --argument)
		if (arguments[argument - 1].kind == TEMPLATE_ARGUMENT_TYPE ||
			arguments[argument - 1].kind == TEMPLATE_ARGUMENT_TEMPLATE)
			push_type(arguments[argument - 1].type);
	std::vector<unsigned char> visited(program.types.Size() + 1, 0);
	while (!pending.empty())
	{
		const TypeId type = pending.back();
		pending.pop_back();
		if (type >= visited.size())
			throw std::logic_error("template emission type identity is invalid: " +
				std::to_string(type) + " >= " + std::to_string(visited.size()));
		if (visited[type]) continue;
		visited[type] = 1;
		const TypeRecord& record = program.types.Get(type);
		if (record.kind == TYPE_NAMED)
		{
			if (record.entity >= program.entities.size())
				throw std::logic_error(
					"template emission entity identity is invalid");
			const EntityRecord& entity = program.entities[record.entity];
			if (entity.local_context != kNoBinding) return true;
			if (program.HasInternalLinkageScope(entity.owner)) return true;
			const std::size_t first = entity.template_argument_begin;
			if (first == kNoBinding) continue;
			if (first > program.template_arguments.size() ||
				entity.template_argument_count >
					program.template_arguments.size() - first)
				throw std::logic_error(
					"template emission argument range is invalid");
			for (std::size_t argument = 0;
				argument < entity.template_argument_count; ++argument)
				if (first + argument >=
						program.canonical_template_arguments.size() ||
					program.canonical_template_arguments[first + argument].kind ==
						TEMPLATE_ARGUMENT_TYPE)
					push_type(program.template_arguments[first + argument]);
			continue;
		}
		if (record.kind == TYPE_FUNCTION)
		{
			push_type(record.child);
			const TypeId* parameters = program.types.Parameters(type);
			for (std::size_t parameter = 0;
				parameter < record.parameter_count; ++parameter)
				push_type(parameters[parameter]);
			continue;
		}
		if (record.kind == TYPE_MEMBER_POINTER)
			push_type(static_cast<TypeId>(record.bound));
		if (record.kind == TYPE_QUALIFIED || record.kind == TYPE_POINTER ||
			record.kind == TYPE_LVALUE_REFERENCE ||
			record.kind == TYPE_RVALUE_REFERENCE || record.kind == TYPE_ARRAY ||
			record.kind == TYPE_MEMBER_POINTER)
			push_type(record.child);
	}
	return false;
}

}

bool TemplateArgumentsNeedInternalEmission(const Program& program,
	const std::vector<TemplateArgument>& arguments)
{
	return TypesContainLocalContext(program, arguments);
}

void PublishFunctionTemplateInternalEmission(Program* program,
	BindingId binding, BindingId canonical,
	const std::vector<TemplateArgument>& arguments)
{
	if (!TemplateArgumentsNeedInternalEmission(*program, arguments)) return;
	program->bindings[binding].unnamed_namespace_linkage = true;
	program->bindings[canonical].unnamed_namespace_linkage = true;
}

void SemanticAnalyzer::RegisterClassStaticDataMember(
	EntityId entity, BindingId member)
{
	if (entity_static_data_members_.size() <= entity)
		entity_static_data_members_.resize(static_cast<std::size_t>(entity) + 1);
	entity_static_data_members_[entity].push_back(member);
}

void SemanticAnalyzer::RegisterClassDataMember(
	EntityId entity, BindingId member, TypeId member_type)
{
	if (entity_data_members_.size() <= entity)
		entity_data_members_.resize(static_cast<std::size_t>(entity) + 1);
	program_->MutableBindingLayout(
		program_->bindings[member]).member_ordinal =
		static_cast<std::uint32_t>(entity_data_members_[entity].size());
	entity_data_members_[entity].push_back(member);
	if (entity_layout_members_.size() <= entity)
		entity_layout_members_.resize(static_cast<std::size_t>(entity) + 1);
	entity_layout_members_[entity].push_back(
		ClassLayoutMember(member, member_type));
}

void SemanticAnalyzer::SetClassExplicitInstantiationSuppression(
	EntityId entity, bool suppressed)
{
	program_->entities[entity].explicit_instantiation_suppressed = suppressed;
	const auto mark = [this, suppressed](BindingId binding) {
		if (binding == kNoBinding) return;
		binding = program_->bindings[binding].canonical;
		if (suppressed && program_->bindings[binding].kind == BIND_FUNCTION)
		{
			const FunctionInfo& function = GetFunction(binding);
			// An explicit instantiation covers only ordinary members; a
			// member template's specializations must still be produced by
			// each translation unit that uses them.
			if (function.template_pattern != kNoDumpEdge) return;
			if (function.definition_in_class ||
				(program_->bindings[binding].inline_function &&
				 (function.defaulted_constructor || function.defaulted_destructor ||
				  function.defaulted_special_member))) return;
		}
		program_->bindings[binding].explicit_instantiation_suppressed = suppressed;
		const BindingId peer = program_->bindings[binding].lifecycle_base_entry;
		if (peer != kNoBinding && peer < program_->bindings.size())
			program_->bindings[peer].explicit_instantiation_suppressed = suppressed;
	};
	const std::vector<std::vector<BindingId> >* indexes[] = {
		&entity_member_functions_, &entity_conversion_functions_,
		&entity_constructors_, &entity_static_data_members_
	};
	for (std::size_t index = 0;
		index < sizeof(indexes) / sizeof(indexes[0]); ++index)
		if (entity < indexes[index]->size())
			for (std::size_t member = 0;
				member < (*indexes[index])[entity].size(); ++member)
				mark((*indexes[index])[entity][member]);
	if (entity < entity_destructor_by_entity_.size())
		mark(entity_destructor_by_entity_[entity]);
}

void SemanticAnalyzer::EnsureStaticMemberStorage(
	BindingId member, bool constant_storage)
{
	member = program_->bindings[member].canonical;
	BindingRecord& binding = program_->bindings[member];
	if (binding.kind != BIND_VARIABLE ||
		binding.member_owner == kNoEntity || binding.non_static_data_member)
		return;
	// Value-use storage is retained only when a matching template definition
	// says that this constant needs an addressable object.
	if (binding.constant && !constant_storage)
	{
		if (constant_expression_required_depth_ != 0) return;
		if (member < explicit_static_member_specialization_states_.size() &&
			explicit_static_member_specialization_states_[member] != 0) return;
		bool retained_definition = false;
		bool value_use_requires_storage = false;
		for (EntityId entity = binding.member_owner; entity != kNoEntity;
			entity = program_->entities[entity].enclosing_class)
		{
			if (entity >= class_template_pattern_by_entity_.size()) continue;
			const std::size_t pattern = class_template_pattern_by_entity_[entity];
			if (pattern == kNoDumpEdge) continue;
			if (pattern >= class_templates_.size())
				throw std::logic_error("static member definition owner is invalid");
			const BindingId specialization =
				program_->entities[entity].declaration;
			if (specialization <
				class_template_explicit_specialization_states_.size() &&
				class_template_explicit_specialization_states_[specialization] != 0)
				break;
			if (pattern > std::numeric_limits<std::uint32_t>::max())
				throw std::logic_error("static member definition index overflow");
			const std::uint64_t key =
				(static_cast<std::uint64_t>(pattern) << 32) | binding.name;
			const CompactIndexSequence* definitions =
				demanded_static_member_definitions_.Find(key);
			retained_definition = definitions && definitions->Size() != 0;
			for (std::size_t i = 0; definitions && i < definitions->Size(); ++i)
			{
				const std::size_t index = (*definitions)[i];
				if (index >= class_templates_[pattern].
					demanded_member_definitions.size())
					throw std::logic_error(
						"static member definition index is invalid");
				if (class_templates_[pattern].demanded_member_definitions[index].
					value_use_requires_storage)
					value_use_requires_storage = true;
			}
			break;
		}
		if (!retained_definition || !value_use_requires_storage) return;
	}
	DemandClassTemplateMemberDefinitions(binding.member_owner);
	const BindingRecord& completed_binding = program_->bindings[member];
	if (static_member_storage_by_binding_.size() <= member)
		static_member_storage_by_binding_.resize(
			static_cast<std::size_t>(member) + 1, kNoDumpEdge);
	DemandStaticConstantInitializerDependencies(member);
	if (static_member_storage_by_binding_[member] != kNoDumpEdge) return;
	if (root_ == kNoDumpEdge)
		throw std::logic_error("static member storage has no translation unit");
	const std::uint32_t declaration = MakeDump(DUMP_VARIABLE,
		completed_binding.type, VALUE_NONE, completed_binding.name, member);
	dump_.nodes[declaration].declaration_only = true;
	dump_.Add(root_, declaration);
	static_member_storage_by_binding_[member] = declaration;
}

}
}
