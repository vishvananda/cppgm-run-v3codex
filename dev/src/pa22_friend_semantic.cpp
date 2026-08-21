#include "pa12_semantic_detail.h"
#include "pa33_function_control_attributes.h"

#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::AnalyzeFriendFunction(NodeId node,
	ScopeId class_scope, TypeId owner_type, const SpecInfo& spec)
{
	const EntityId owner_entity = EntityOf(owner_type);
	if (owner_entity == kNoEntity)
		throw std::logic_error("friend declaration has no class owner");
	ScopeId friend_owner = program_->entities[owner_entity].owner;
	while (friend_owner != kNoScope &&
		program_->KindOfScope(friend_owner) != SCOPE_NAMESPACE)
		friend_owner = program_->ParentScope(friend_owner);
	if (friend_owner == kNoScope)
		throw std::runtime_error("friend function has no namespace owner");
	std::vector<NodeId> declarators;
	if (arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_FUNCTION_DEFINITION))
	{
		const NodeId declarator = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_DECLARATOR);
		if (declarator != kNoNode) declarators.push_back(declarator);
	}
	else
	{
		const NodeId list = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_INIT_DECLARATOR_LIST);
		if (list != kNoNode)
			for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
				edge = arena_->NextEdge(edge))
			{
				const NodeId declarator =
					FindChild(arena_->EdgeChild(edge), ::cppgm::pa10_syntax_detail::STAG_DECLARATOR);
				if (declarator != kNoNode) declarators.push_back(declarator);
			}
	}
	if (declarators.empty())
		throw std::runtime_error("friend declaration has no function declarator");
	const bool definition = arena_->IsTag(node, ::cppgm::pa10_syntax_detail::STAG_FUNCTION_DEFINITION);
	for (std::size_t i = 0; i < declarators.size(); ++i)
	{
		const DeclaratorInfo parsed = BuildDeclarator(
			declarators[i], spec.type, class_scope);
		if (!program_->types.IsFunction(parsed.type))
			throw std::runtime_error("friend declaration is not a function");
		if (spec.is_constexpr)
			ValidateConstexprCallableType(parsed.type, false);
		const NamePath declared_name = DeclaratorNamePath(declarators[i]);
		const bool qualified_friend = declared_name.global ||
			declared_name.Size() > 1;
		const ScopeId declared_owner = qualified_friend ?
			ResolveOwner(class_scope, declared_name) : friend_owner;
		if (declared_owner == kNoScope)
			throw std::runtime_error("qualified friend owner not found");
		const NodeId identifier = FindChild(declarators[i], ::cppgm::pa10_syntax_detail::STAG_IDENTIFIER);
		NamePath template_base;
		std::vector<NodeId> explicit_arguments;
		if (CollectExplicitTemplateArguments(
			identifier, &template_base, &explicit_arguments))
		{
			const std::vector<BindingId> targets =
				FunctionTemplateTargetCandidates(class_scope,
					program_->names.Get(template_base.Last()), parsed.type,
					identifier);
			if (targets.size() != 1)
				throw std::runtime_error(
					"friend template-id does not select one specialization");
			const BindingId binding =
				program_->bindings[targets[0]].canonical;
			FunctionInfo& info = GetMutableFunction(binding);
			if (info.friend_of == kNoEntity) info.friend_of = owner_entity;
			const std::uint64_t access_key =
				(static_cast<std::uint64_t>(owner_entity) << 32) | binding;
			CompactIndexSequence& grants =
				friend_function_grants_.Ensure(access_key);
			if (grants.Size() == 0) grants.Push(0);
			continue;
		}
		if (qualified_friend)
		{
			const TypeRecord declared_type = program_->types.Get(parsed.type);
			std::vector<TypeId> parameters;
			const TypeId* parameter_data =
				program_->types.Parameters(parsed.type);
			if (declared_type.parameter_count != 0)
				parameters.assign(parameter_data,
					parameter_data + declared_type.parameter_count);
			const TypeId signature = program_->types.Function(
				program_->types.Fundamental(FUND_VOID), parameters,
				declared_type.variadic, declared_type.cv,
				declared_type.ref_qualifier);
			++function_signature_lookups_;
			const BindingId prior = function_declarations_.Find(
				FunctionSignatureKey(declared_owner, parsed.name, signature));
			if (prior == kNoBinding || !GetFunction(prior).ordinary_visible)
				throw std::runtime_error(
					"qualified friend function was not declared");
		}
		const BindingId binding = DeclareFunction(declared_owner, parsed.name,
			parsed.type, parsed.parameters, definition, false,
			STORAGE_CLASS_NONE, current_language_linkage_,
			IsNonthrowing(declarators[i], parsed.parameter_scope), false);
		ConfigureFunctionExceptionSpecification(
			binding, declarators[i], parsed.parameter_scope);
		ApplyFunctionControlAttributes(program_, binding,
			FunctionControlAttributeMask(*arena_, node));
		ApplyFunctionControlAttributes(program_, binding,
			FunctionControlAttributeMask(*arena_, declarators[i]));
		FunctionInfo& info = GetMutableFunction(binding);
		info.constexpr_function = info.constexpr_function || spec.is_constexpr;
		PublishInlineFunctionFacts(
			binding, definition || spec.inline_specifier || spec.is_constexpr);
		if (info.friend_of == kNoEntity) info.friend_of = owner_entity;
		ValidateFunctionRefQualifier(binding);
		const std::uint64_t access_key =
			(static_cast<std::uint64_t>(owner_entity) << 32) | binding;
		CompactIndexSequence& grants =
			friend_function_grants_.Ensure(access_key);
		if (grants.Size() == 0) grants.Push(0);
		if (!qualified_friend)
		{
			const std::uint64_t friend_key =
				(static_cast<std::uint64_t>(owner_entity) << 32) | parsed.name;
			CompactIndexSequence& hidden = hidden_friend_sets_.Ensure(friend_key);
			if (!hidden.Contains(binding)) hidden.Push(binding);
		}
		info.lexical_scope = class_scope;
		if (definition)
		{
			info.definition_body = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_COMPOUND_STATEMENT);
			info.deferred = true;
			if (!qualified_friend)
			{
				if (hidden_friend_anchor_by_entity_.size() <= owner_entity)
					hidden_friend_anchor_by_entity_.resize(
						static_cast<std::size_t>(owner_entity) + 1, kNoBinding);
				if (hidden_friend_anchor_by_entity_[owner_entity] == kNoBinding)
					hidden_friend_anchor_by_entity_[owner_entity] =
						program_->bindings[binding].canonical;
			}
		}
		ValidateNonmemberOperator(binding);
	}
}

void SemanticAnalyzer::AnalyzeFriendClass(NodeId node,
	ScopeId class_scope, TypeId owner_type)
{
	const EntityId owner = EntityOf(owner_type);
	if (owner == kNoEntity)
		throw std::logic_error("friend class declaration has no owner");
	const NodeId specifiers = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_DECL_SPECIFIER_SEQ);
	const NodeId declaration = specifiers == kNoNode ? kNoNode :
		FindChild(specifiers, ::cppgm::pa10_syntax_detail::STAG_CLASS_FORWARD_DECLARATION);
	if (declaration == kNoNode)
		throw std::runtime_error("friend class declaration has no class");
	const std::string spelling = arena_->Payload(declaration);
	const NodeId structure = FindChild(declaration, ::cppgm::pa10_syntax_detail::STAG_STRUCTURED_TYPE_NAME);
	NamePath path;
	if (structure != kNoNode) path = StructuredNamePath(structure);
	else path.Push(program_->names.UseInterned(arena_->PayloadId(declaration)));
	LookupResult found;
	if (structure != kNoNode)
	{
		// Friendship needs the canonical class identity, not its definition or
		// layout. In particular, mutually befriending class specializations may
		// contain one another after the first specialization completes.
		++class_template_completion_suppressed_depth_;
		try
		{
			found = LookupStructuredName(
				declaration, class_scope, LOOKUP_TYPE);
		}
		catch (...)
		{
			--class_template_completion_suppressed_depth_;
			throw;
		}
		--class_template_completion_suppressed_depth_;
	}
	else found = LookupPath(class_scope, path, LOOKUP_TYPE);
	TypeId friend_type = found.type;
	if (friend_type != kNoType && found.type_declaration != kNoBinding &&
		!CanAccessMember(found.type_declaration, found.naming_class))
		throw std::runtime_error("inaccessible friend class");
	if (friend_type == kNoType)
	{
		ScopeId namespace_owner = program_->entities[owner].owner;
		while (namespace_owner != kNoScope &&
			program_->KindOfScope(namespace_owner) != SCOPE_NAMESPACE)
			namespace_owner = program_->ParentScope(namespace_owner);
		if (namespace_owner == kNoScope)
			throw std::runtime_error("friend class has no namespace owner");
		friend_type = AnalyzeClass(declaration,
			!path.global && path.Size() == 1 ?
				namespace_owner : class_scope,
			std::string(), true);
	}
	const EntityId friend_entity = EntityOf(friend_type);
	if (friend_entity == kNoEntity)
		throw std::runtime_error("friend declaration does not name a class");
	const std::uint64_t key =
		(static_cast<std::uint64_t>(owner) << 32) | friend_entity;
	CompactIndexSequence& grants = friend_class_grants_.Ensure(key);
	if (grants.Size() == 0) grants.Push(0);
}

}
}
