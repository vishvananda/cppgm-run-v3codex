#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <sstream>
#include <vector>

namespace cppgm
{
namespace semantic
{

bool Analyzer::IsStructuredBindingDeclarator(NodeId declarator) const
{
	return declarator != kNoNode &&
		FindChild(declarator, ::cppgm::syntax::STAG_STRUCTURED_BINDING) != kNoNode;
}

ExpressionInfo Analyzer::AnalyzeDesignatedAggregateInit(
	TypeId type, ScopeId scope, std::uint32_t* element_edge)
{
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity || entity >= entity_data_members_.size() ||
		!program_->entities[entity].is_aggregate || !element_edge)
		ThrowInternalCompilerError("invalid designated aggregate target");
	const std::vector<BindingId> members = entity_data_members_[entity];
	const std::uint32_t list = MakeDump(
		DUMP_BRACED_INIT_LIST, type, VALUE_LVALUE);
	std::vector<ConstexprObjectElement> constant_elements;
	constant_elements.reserve(members.size());
	bool constant_object = true;

	const auto selected_member = [this, entity, &members](NodeId syntax)
		-> BindingId
	{
		const NameId name =
			program_->names.UseInterned(arena_->PayloadId(syntax));
		const LookupResult found =
			program_->LookupMember(entity, name, LOOKUP_ORDINARY);
		if (found.ordinary == kNoBinding ||
			found.ordinary >= program_->bindings.size())
			ThrowSemanticError("designated aggregate member not found");
		const BindingRecord& binding = program_->bindings[found.ordinary];
		const std::uint32_t ordinal =
			program_->BindingLayout(binding).member_ordinal;
		if (!binding.non_static_data_member || binding.member_owner != entity ||
			ordinal >= members.size() || members[ordinal] != found.ordinary)
			ThrowSemanticError(
				"designator does not name a direct aggregate member");
		if (!CanAccessMember(found.ordinary, found.naming_class))
			ThrowSemanticError("inaccessible designated aggregate member");
		return found.ordinary;
	};

	const auto append_action = [this, scope, list, &constant_elements,
		&constant_object](BindingId member, NodeId designated)
	{
		const BindingRecord member_record = program_->bindings[member];
		const std::uint32_t action = MakeDump(DUMP_INITIALIZER_ACTION,
			member_record.type, VALUE_NONE, member_record.name, member);
		ExpressionInfo value;
		if (designated == kNoNode)
		{
			std::uint32_t omitted = kNoEdge;
			value = AnalyzeAggregateElement(
				member_record.type, scope, &omitted);
			dump_.nodes[action].value_initialization = true;
		}
		else
		{
			const NodeId initializer = FindChild(designated, ::cppgm::syntax::STAG_INITIALIZER);
			if (initializer == kNoNode)
				ThrowSemanticError(
					"designated member has no initializer");
			std::uint32_t value_edge = arena_->FirstEdge(initializer);
			value = AnalyzeAggregateElement(
				member_record.type, scope, &value_edge);
			if (value_edge != kNoEdge)
				ThrowSemanticError(
					"designated member has excess initializer elements");
		}
		if (value.node != kNoDumpEdge) dump_.Add(action, value.node);
		ConstexprObjectElement element(member,
			ConstexprScalarValue(static_cast<std::int64_t>(0)));
		if (constant_object && BuildConstexprObjectElement(
			member_record.type, member, value, &element))
			constant_elements.push_back(element);
		else constant_object = false;
		dump_.Add(list, action);
		++expression_count_;
	};

	if (program_->entities[entity].flavor == NAMED_UNION)
	{
		const NodeId designated = arena_->EdgeChild(*element_edge);
		const BindingId member = selected_member(designated);
		*element_edge = arena_->NextEdge(*element_edge);
		append_action(member, designated);
	}
	else
	{
		for (std::size_t i = 0; i < members.size(); ++i)
		{
			NodeId designated = kNoNode;
			if (*element_edge != kNoEdge)
			{
				designated = arena_->EdgeChild(*element_edge);
				if (!arena_->IsTag(designated, ::cppgm::syntax::STAG_DESIGNATED_INITIALIZER))
					ThrowSemanticError(
						"cannot mix designated and positional initializers");
				const BindingId selected = selected_member(designated);
				const std::size_t ordinal =
					program_->BindingLayout(
						program_->bindings[selected]).member_ordinal;
				if (ordinal < i)
					ThrowSemanticError(
						"aggregate designators are out of declaration order");
				if (ordinal != i) designated = kNoNode;
				else *element_edge = arena_->NextEdge(*element_edge);
			}
			append_action(members[i], designated);
		}
	}

	ExpressionInfo result;
	result.node = list;
	result.type = type;
	result.category = VALUE_LVALUE;
	if (constant_object && !constant_elements.empty())
		SetExpressionObject(&result,
			InternConstexprObject(type, constant_elements));
	++expression_count_;
	return result;
}

void Analyzer::AnalyzeStructuredBindingDeclaration(
	NodeId item, NodeId declarator, const SpecInfo& spec, ScopeId scope,
	std::uint32_t output_parent, bool local)
{
	if (!spec.placeholder_auto || spec.is_typedef || spec.is_friend ||
		spec.inline_specifier || spec.storage_class == STORAGE_CLASS_EXTERN)
		ThrowSemanticError(
			"invalid structured binding declaration specifiers");
	ExpressionInfo initializer;
	DeclaratorInfo parsed = BuildVariableDeclarator(
		item, declarator, spec, scope, local, &initializer);
	EmitStructuredBindingStorage(item, declarator, spec, parsed, initializer,
		scope, output_parent, local, false);
}

void Analyzer::EmitStructuredBindingStorage(
	NodeId source, NodeId declarator, const SpecInfo& spec,
	DeclaratorInfo parsed, ExpressionInfo initializer, ScopeId scope,
	std::uint32_t output_parent, bool local, bool range_variable)
{
	const NodeId bindings = FindChild(declarator, ::cppgm::syntax::STAG_STRUCTURED_BINDING);
	if (bindings == kNoNode)
		ThrowInternalCompilerError("structured binding has no binding list");
	EnsureClassDefinition(parsed.type);
	const EntityId entity = EntityOf(parsed.type);
	if (entity == kNoEntity || entity >= program_->entities.size() ||
		!program_->entities[entity].is_aggregate ||
		program_->entities[entity].flavor == NAMED_UNION ||
		entity >= entity_data_members_.size())
		ThrowSemanticError(
			"structured binding requires a decomposable aggregate class");

	std::vector<NodeId> binding_syntax;
	for (std::uint32_t edge = arena_->FirstEdge(bindings);
		edge != kNoEdge; edge = arena_->NextEdge(edge))
	{
		const NodeId binding = arena_->EdgeChild(edge);
		if (!arena_->IsTag(binding, ::cppgm::syntax::STAG_BINDING_IDENTIFIER))
			ThrowSemanticError("invalid structured binding identifier");
		binding_syntax.push_back(binding);
	}
	const std::vector<BindingId>& members = entity_data_members_[entity];
	if (binding_syntax.size() != members.size())
		ThrowSemanticError("structured binding element count mismatch");

	std::ostringstream generated;
	generated << "__structured_binding_storage__" << arena_->TokenFirst(source)
		<< '_' << arena_->TokenLast(source);
	const std::string generated_name = generated.str();
	if (stats_)
		RecordGeneratedIdentityRender(
			SEMANTIC_GENERATED_STRUCTURED_BINDING_STORAGE,
			generated_name, 2);
	parsed.name = program_->names.Intern(generated_name);
	// Private storage identity stays out of name lookup, so a user name
	// sharing the generated spelling cannot collide with it.
	const BindingId storage = program_->AddUnindexedBinding(
		scope, BIND_VARIABLE, parsed.name, parsed.type, kNoBinding);
	PublishVariableDeclarationFacts(
		storage, scope, parsed.name, parsed.type, spec, local);
	ApplyVariableObjectAttributes(source, storage);
	PublishVariableInitializer(storage, parsed.type, spec, initializer, false);
	PublishCanonicalBindingConstant(storage);

	bool declaration_only = false;
	const std::uint32_t variable = MakeVariableDeclarationDump(parsed.type,
		parsed.name, storage, local, true, &declaration_only);
	std::uint32_t runtime_initializer = initializer.node;
	std::uint32_t declaration_owner = output_parent;
	if (range_variable)
	{
		declaration_owner = MakeDump(DUMP_SIMPLE_DECLARATION);
		dump_.Add(variable, initializer.node);
		dump_.Add(declaration_owner, variable);
		dump_.Add(output_parent, declaration_owner);
	}
	else runtime_initializer = PublishVariableInitializerActions(variable,
		storage, parsed.type, initializer, true, declaration_only, false);
	if (!range_variable) dump_.Add(output_parent, variable);
	const NameId source_file = arena_->SourceLine(source) == 0 ? 0 :
		program_->names.Intern(arena_->SourceFile(source));
	if (!range_variable)
		StageAutomaticInitializerException(runtime_initializer, variable, scope,
			storage, parsed.type, local && runtime_initializer != kNoDumpEdge);
	RegisterVariableLifetimeAndStorage(scope, local, declaration_only,
		variable, storage, parsed.type, source_file,
		static_cast<std::uint32_t>(arena_->SourceLine(source)),
		static_cast<std::uint32_t>(arena_->SourceColumn(source)),
		static_cast<std::uint32_t>(arena_->TokenFirst(source)),
		static_cast<std::uint32_t>(arena_->TokenLast(source)),
		true, HasConstantInitializerFact(initializer));

	for (std::size_t i = 0; i < binding_syntax.size(); ++i)
	{
		const BindingId member = members[i];
		if (!CanAccessMember(member, entity))
			ThrowSemanticError("inaccessible structured binding member");
		const NameId name = program_->names.Intern(
			arena_->Payload(binding_syntax[i]));
		if (name == 0 || program_->LookupDirect(
			scope, name, LOOKUP_ORDINARY).ordinary != kNoBinding)
			ThrowSemanticError(
				"duplicate or conflicting structured binding name");
		TypeId alias_type = EffectiveType(program_->bindings[member].type);
		const TypeRecord member_shape =
			program_->types.Get(program_->bindings[member].type);
		const TypeRecord storage_shape =
			program_->types.Get(EffectiveType(parsed.type));
		if (member_shape.kind != TYPE_LVALUE_REFERENCE &&
			member_shape.kind != TYPE_RVALUE_REFERENCE &&
			storage_shape.kind == TYPE_QUALIFIED)
			alias_type = program_->types.Qualify(alias_type, storage_shape.cv);
		const BindingId alias = program_->AddBinding(scope, BIND_VARIABLE,
			name, alias_type);
		RegisterInjectedStorageMember(alias, storage, member);
	}

	if (range_variable)
	{
		FinishRangeForLocalInitializer(
			scope, declaration_owner, parsed.type, initializer);
		return;
	}
	if (!local) return;
	const bool control_dependent =
		HasControlDependentTemporary(initializer.node);
	const bool extended_initializer_list = ExtendInitializerListVariableLifetime(
		parsed.type, scope, initializer.node, control_dependent);
	if (program_->types.IsReference(parsed.type) && !control_dependent)
	{
		std::vector<std::uint32_t> temporaries;
		CollectTemporaryObjects(initializer.node, &temporaries);
		if (!temporaries.empty())
		{
			AddTemporaryLifetimeObligation(scope, temporaries.back());
			for (std::size_t i = temporaries.size() - 1; i != 0; --i)
			{
				const std::uint32_t action =
					MakeTemporaryDestructorAction(temporaries[i - 1]);
				if (action != kNoDumpEdge) dump_.Add(output_parent, action);
			}
		}
	}
	else if (!extended_initializer_list &&
		!dump_.nodes[variable].full_expression_staging)
	{
		const std::size_t edge_count = dump_.edges.size();
		const bool static_storage =
			program_->bindings[storage].storage_class == STORAGE_CLASS_STATIC;
		AppendFullExpressionDestructionActions(
			initializer.node, output_parent, static_storage);
		if (control_dependent && dump_.edges.size() != edge_count &&
			RequiresManagedConditionalFullExpression(
				initializer.node, edge_count))
		{
			dump_.nodes[output_parent].full_expression_staging = true;
			MarkFullExpressionCalls(initializer.node);
		}
		if (static_storage &&
			!InitializationActionsAreNonthrowing(initializer.node))
			StageExceptionalFullExpression(
				initializer.node, output_parent, scope, true);
	}
}

}
}
