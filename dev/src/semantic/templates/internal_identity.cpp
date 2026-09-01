#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <vector>

namespace cppgm
{
namespace semantic
{
namespace
{

class InternalIdentityPublisher
{
public:
	explicit InternalIdentityPublisher(Program* program)
		: program_(*program), type_node_count_(program_.types.Size() + 1),
		  entity_offset_(type_node_count_),
		  binding_offset_(entity_offset_ + program_.entities.size()),
		  dependents_(binding_offset_ + program_.bindings.size()),
		  marked_(dependents_.size(), 0), cursor_(0)
	{
	}

	void Publish()
	{
		BuildTypeDependencies();
		BuildEntityDependencies();
		BuildBindingDependencies();
		while (cursor_ < worklist_.size())
		{
			const std::size_t dependency = worklist_[cursor_++];
			for (std::size_t i = 0; i < dependents_[dependency].size(); ++i)
				Mark(dependents_[dependency][i]);
		}
		for (BindingId binding = 0; binding < program_.bindings.size(); ++binding)
			if (marked_[BindingNode(binding)])
				program_.bindings[binding].unnamed_namespace_linkage = true;
	}

private:
	std::size_t TypeNode(TypeId type) const
	{
		if (type == kNoType || type == 0 || type >= type_node_count_)
			ThrowInternalCompilerError("internal identity type is invalid");
		return type;
	}

	std::size_t EntityNode(EntityId entity) const
	{
		if (entity == kNoEntity || entity >= program_.entities.size())
			ThrowInternalCompilerError("internal identity entity is invalid");
		return entity_offset_ + entity;
	}

	std::size_t BindingNode(BindingId binding) const
	{
		if (binding == kNoBinding || binding >= program_.bindings.size())
			ThrowInternalCompilerError("internal identity binding is invalid");
		return binding_offset_ + binding;
	}

	void AddTypeDependency(TypeId dependency, std::size_t dependent)
	{
		if (dependency != kNoType && dependency != 0)
			AddDependency(TypeNode(dependency), dependent);
	}

	void AddEntityDependency(EntityId dependency, std::size_t dependent)
	{
		if (dependency != kNoEntity)
			AddDependency(EntityNode(dependency), dependent);
	}

	void AddBindingDependency(BindingId dependency, std::size_t dependent)
	{
		if (dependency != kNoBinding)
			AddDependency(BindingNode(dependency), dependent);
	}

	void AddDependency(std::size_t dependency, std::size_t dependent)
	{
		dependents_[dependency].push_back(dependent);
	}

	void Mark(std::size_t node)
	{
		if (marked_[node]) return;
		marked_[node] = 1;
		worklist_.push_back(node);
	}

	void AddTemplateArgumentDependencies(std::uint32_t begin,
		std::uint32_t count, std::size_t dependent)
	{
		if (count == 0) return;
		if (begin == kNoBinding || begin > program_.template_arguments.size() ||
			count > program_.template_arguments.size() - begin ||
			begin > program_.canonical_template_arguments.size() ||
			count > program_.canonical_template_arguments.size() - begin)
			ThrowInternalCompilerError("internal identity argument range is invalid");
		for (std::size_t i = 0; i < count; ++i)
		{
			const std::size_t argument = static_cast<std::size_t>(begin) + i;
			AddTypeDependency(program_.template_arguments[argument], dependent);
			AddBindingDependency(
				program_.canonical_template_arguments[argument].value_binding,
				dependent);
		}
	}

	void BuildTypeDependencies()
	{
		for (TypeId type = 1; type < type_node_count_; ++type)
		{
			const std::size_t node = TypeNode(type);
			const TypeRecord& record = program_.types.Get(type);
			AddTypeDependency(record.child, node);
			AddTypeDependency(record.dependent_bound_type, node);
			if (record.kind == TYPE_NAMED ||
				record.kind == TYPE_MEMBER_POINTER)
				AddEntityDependency(record.entity, node);
			if (record.kind == TYPE_MEMBER_POINTER)
				AddTypeDependency(static_cast<TypeId>(record.bound), node);
			if (record.kind == TYPE_FUNCTION)
			{
				const TypeId* parameters = program_.types.Parameters(type);
				for (std::size_t i = 0; i < record.parameter_count; ++i)
					AddTypeDependency(parameters[i], node);
			}
		}
	}

	void BuildEntityDependencies()
	{
		for (EntityId entity = 0; entity < program_.entities.size(); ++entity)
		{
			const std::size_t node = EntityNode(entity);
			const EntityRecord& record = program_.entities[entity];
			if (record.local_context != kNoBinding ||
				program_.HasInternalLinkageScope(record.owner))
				Mark(node);
			AddBindingDependency(record.declaration, node);
			AddEntityDependency(record.enclosing_class, node);
			AddTemplateArgumentDependencies(record.template_argument_begin,
				record.template_argument_count, node);
		}
	}

	void BuildBindingDependencies()
	{
		for (BindingId binding = 0; binding < program_.bindings.size(); ++binding)
		{
			const std::size_t node = BindingNode(binding);
			const BindingRecord& record = program_.bindings[binding];
			if (record.unnamed_namespace_linkage ||
				(record.storage_class == STORAGE_CLASS_STATIC &&
				 record.member_owner == kNoEntity) ||
				program_.HasInternalLinkageScope(record.owner))
				Mark(node);
			AddTypeDependency(record.type, node);
			AddEntityDependency(record.member_owner, node);
			AddTemplateArgumentDependencies(record.template_argument_begin,
				record.template_argument_count, node);
			if (record.canonical != kNoBinding && record.canonical != binding)
				AddBindingDependency(record.canonical, node);
		}
	}

	Program& program_;
	const std::size_t type_node_count_, entity_offset_, binding_offset_;
	std::vector<std::vector<std::size_t> > dependents_;
	std::vector<unsigned char> marked_;
	std::vector<std::size_t> worklist_;
	std::size_t cursor_;
};

}

void PublishInternalIdentityFacts(Program* program)
{
	if (!program) ThrowInternalCompilerError("missing internal identity program");
	InternalIdentityPublisher(program).Publish();
}

}
}
