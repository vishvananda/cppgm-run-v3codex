#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <sstream>

namespace cppgm
{
namespace semantic
{

void Analyzer::AnalyzeDeclaratorlessSimpleDeclaration(
	NodeId specifiers, ScopeId scope, std::uint32_t output_parent,
	bool local, const SpecInfo& spec)
{
	const NodeId class_specifier = FindChild(specifiers,
		::cppgm::syntax::STAG_CLASS_SPECIFIER);
	const EntityId entity = EntityOf(spec.type);
	const bool anonymous_union = class_specifier != kNoNode &&
		arena_->Payload(class_specifier).empty() && entity != kNoEntity &&
		program_->entities[entity].flavor == NAMED_UNION;
	if (anonymous_union)
	{
		if (!local && spec.storage_class != STORAGE_CLASS_STATIC)
			ThrowSemanticError(
				"namespace anonymous union must be static");
		DeclareAnonymousUnionObject(class_specifier, scope, output_parent,
			spec.type, local, spec.storage_class);
		return;
	}
	if (local)
	{
		const std::uint32_t empty = MakeDump(DUMP_SIMPLE_DECLARATION);
		dump_.Add(output_parent, empty);
	}
}

void Analyzer::DeclareAnonymousUnionObject(NodeId source,
	ScopeId scope, std::uint32_t output_parent, TypeId type, bool local,
	StorageClass storage_class)
{
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity ||
		program_->entities[entity].flavor != NAMED_UNION)
		ThrowInternalCompilerError("anonymous union object has no union entity");
	std::ostringstream generated;
	generated << "__anonymous_union_storage__" << arena_->TokenFirst(source)
		<< '_' << arena_->TokenLast(source);
	const std::string generated_name = generated.str();
	if (stats_)
		RecordGeneratedIdentityRender(
			SEMANTIC_GENERATED_ANONYMOUS_UNION_STORAGE,
			generated_name, 2);
	const NameId storage_name = program_->names.Intern(generated_name);
	const BindingId storage = program_->AddUnindexedBinding(scope,
		BIND_VARIABLE, storage_name, type, kNoBinding);
	BindingRecord& storage_record = program_->bindings[storage];
	storage_record.anonymous_union_storage = true;
	storage_record.storage_class = storage_class;
	storage_record.language_linkage = current_language_linkage_;

	const std::uint32_t parent = local ?
		MakeDump(DUMP_SIMPLE_DECLARATION) : output_parent;
	const std::uint32_t variable = MakeDump(DUMP_VARIABLE, type,
		VALUE_NONE, storage_name, storage);
	AddDefaultConstructor(variable, storage, type);
	dump_.Add(parent, variable);
	if (local) dump_.Add(output_parent, parent);

	const NameId source_file = arena_->SourceLine(source) == 0 ? 0 :
		program_->names.Intern(arena_->SourceFile(source));
	RegisterVariableLifetimeAndStorage(scope, local, false, variable,
		storage, type, source_file,
		static_cast<std::uint32_t>(arena_->SourceLine(source)),
		static_cast<std::uint32_t>(arena_->SourceColumn(source)),
		static_cast<std::uint32_t>(arena_->TokenFirst(source)),
		static_cast<std::uint32_t>(arena_->TokenLast(source)), false, false);

	const std::vector<BindingId>& members = entity_data_members_[entity];
	for (std::size_t i = 0; i < members.size(); ++i)
	{
		const BindingRecord source_record = program_->bindings[members[i]];
		if (program_->LookupDirect(scope, source_record.name,
			LOOKUP_ORDINARY).ordinary != kNoBinding)
			ThrowSemanticError(
				"anonymous union member conflicts in enclosing scope");
		const BindingId injected = program_->AddBinding(scope, BIND_VARIABLE,
			source_record.name, source_record.type, source_record.constant,
			source_record.value, source_record.display_flavor,
			source_record.display_type_name);
		RegisterInjectedStorageMember(injected, storage, members[i]);
	}
}

}
}
