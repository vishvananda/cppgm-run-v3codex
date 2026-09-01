// Enum declaration analysis: builds enum entities, their enumerators, and
// their underlying-type facts from syntax.
#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"
#include "support/scoped_state.h"
#include <string>
#include <vector>

namespace cppgm { namespace semantic {

BindingId LocalTypeContext(const Program& program, ScopeId owner,
	BindingId current_function);

void Analyzer::BuildEnumDeclarationNamePath(NodeId node,
	const std::string& hint, std::string* spelling, NamePath* path,
	bool* generated_identity)
{
	*generated_identity = false;
	*spelling = arena_->Payload(node);
	if (spelling->empty()) *spelling = hint;
	if (spelling->empty())
	{
		++anonymous_enum_count_;
		*spelling = "__anonymous_enum" +
			std::to_string(anonymous_enum_count_);
		*generated_identity = true;
		if (stats_)
			RecordGeneratedIdentityRender(SEMANTIC_GENERATED_ANONYMOUS_ENUM,
				*spelling, 1);
	}
	if (!arena_->Payload(node).empty() &&
		spelling->find("::") == std::string::npos)
		path->Push(program_->names.UseInterned(arena_->PayloadId(node)));
	else if (spelling->find("::") == std::string::npos)
		path->Push(program_->names.Intern(*spelling));
	else *path = ParseNamePath(
		*spelling, NAME_PATH_PARSE_DECLARATION_ENUM);
}

TypeId Analyzer::AnalyzeEnum(NodeId node, ScopeId scope, const std::string& hint, bool elaborated)
{
	std::string spelling;
	NamePath path;
	bool generated_identity = false;
	BuildEnumDeclarationNamePath(node, hint, &spelling, &path,
		&generated_identity);
	const bool scoped = FindChild(node, ::cppgm::syntax::STAG_ENUM_KEY) != kNoNode;
	const NamedFlavor flavor = scoped ? NAMED_ENUM_CLASS : NAMED_ENUM;
	const bool qualified = path.global || path.Size() > 1;
	const bool definition =
		(arena_->Flags(node) & SYNTAX_FLAG_DEFINITION) != 0;
	const NameId name = path.Last();
	const ScopeId owner = ResolveOwner(scope, path);
	if (owner == kNoScope) ThrowSemanticError("enum owner not found");
	const NodeId underlying_node = FindChild(node, ::cppgm::syntax::STAG_TYPE_ID);
	TypeId underlying = underlying_node == kNoNode ?
		program_->types.Fundamental(FUND_INT) :
		BuildTypeId(underlying_node, owner);
	// A generated anonymous identity names a fresh enum: it never matches a
	// source declaration and must not enter ordinary type lookup, where it
	// could collide with a user type of the same spelling.
	const LookupResult old = generated_identity ? LookupResult() :
		path.global || path.Size() > 1 ?
		program_->LookupDirect(owner, name, LOOKUP_TYPE) :
		(elaborated ? program_->LookupName(scope, name, LOOKUP_TYPE) :
		 program_->LookupDirect(owner, name, LOOKUP_TYPE));
	if (elaborated)
	{
		if (old.type == kNoType) ThrowSemanticError("unknown enum type");
		return old.type;
	}
	if (!definition && !scoped && underlying_node == kNoNode)
		ThrowSemanticError(
			"opaque unscoped enum requires an underlying type");
	EntityId entity = kNoEntity;
	bool created_entity = false;
	if (old.type != kNoType)
	{
		const TypeRecord named = program_->types.Get(
			program_->types.RemoveTopCv(old.type));
		if (named.kind != TYPE_NAMED)
			ThrowSemanticError("enum redeclared as non-enum");
		entity = named.entity;
		if (program_->entities[entity].flavor != flavor)
			ThrowSemanticError("incompatible enum redeclaration");
		if (underlying_node != kNoNode &&
			program_->entities[entity].underlying != underlying)
			ThrowSemanticError("enum underlying type changed");
	}
	else
	{
		entity = program_->NewEntity(name, flavor, true, underlying, owner);
		created_entity = true;
		program_->entities[entity].local_context = LocalTypeContext(
			*program_, owner, current_function_context_);
		RegisterLocalTypeAbiIdentity(entity);
		if (!generated_identity)
			program_->SetTypeName(owner, name,
				program_->entities[entity].type);
		if (arena_->Payload(node).size() != 0)
			program_->AddBinding(owner, BIND_TYPE, name,
				program_->entities[entity].type, false, 0, flavor);
	}
	const TypeId type = program_->entities[entity].type;
	if (source_type_view_ && created_entity &&
		arena_->Payload(node).empty() && !hint.empty())
	{
		const NameId presentation_name = program_->names.Intern(hint);
		program_->entities[entity].emission_name = presentation_name;
		program_->AddOutputTypeBinding(
			owner, presentation_name, type, flavor);
	}
	ScopeId value_scope = owner;
	if (scoped)
	{
		value_scope = program_->entities[entity].member_scope;
		if (value_scope == kNoScope)
		{
			value_scope = NewNamedScope(
				owner, SCOPE_ENUM, name, owner, name);
			program_->SetEntityScope(entity, value_scope);
		}
	}
	ScopeId source_output_scope = kNoScope;
	if (source_type_view_ && qualified && scoped && definition)
	{
		const BindingId output_type = program_->AddOutputTypeBinding(
			scope, name, type, flavor);
		program_->bindings[output_type].source_view_qualified_name = true;
		program_->bindings[output_type].source_view_qualified_type = true;
		source_output_scope = program_->NewScope(
			owner, SCOPE_ENUM, name, entity, scope);
		program_->SetSourceViewQualifiedScope(source_output_scope);
	}
	std::int64_t next = 0;
	std::int64_t minimum = 0;
	std::int64_t maximum = 0;
	std::vector<BindingId> enumerators;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId enumerator = arena_->EdgeChild(edge);
		if (!arena_->IsTag(enumerator, ::cppgm::syntax::STAG_ENUMERATOR)) continue;
		const NodeId initializer = FirstSemanticChild(enumerator);
		std::int64_t value = next;
		if (initializer != kNoNode)
		{
			// A specialization demanded from a discarded expression arm still
			// analyzes its enumerators as independent constant-expression roots.
			ExpressionInfo expression;
			{
				ScopedValueRestore<std::size_t> suppression(
					&constant_evaluation_suppressed_depth_, 0);
				expression = AnalyzeExpression(initializer, value_scope);
			}
			if (!expression.constant)
				ThrowSemanticError("nonconstant enumerator");
			value = expression.value;
		}
		const NameId enumerator_name =
			program_->names.UseInterned(arena_->PayloadId(enumerator));
		const BindingId binding = program_->AddBinding(value_scope,
			BIND_ENUMERATOR, enumerator_name, underlying, true, value);
		enumerators.push_back(binding);
		if (source_output_scope != kNoScope)
		{
			program_->bindings[binding].source_view_suppressed = true;
			const BindingId output = program_->AddUnindexedBinding(
				source_output_scope, BIND_ENUMERATOR,
				enumerator_name, type, binding);
			program_->bindings[output].constant = true;
			program_->bindings[output].value = value;
			program_->bindings[output].source_view_qualified_type = true;
		}
		if (value < minimum) minimum = value;
		if (value > maximum) maximum = value;
		if (value == INT64_MAX) ThrowSemanticError("enumerator overflow");
		next = value + 1;
	}
	if (underlying_node == kNoNode && !scoped)
	{
		if (minimum >= std::numeric_limits<std::int32_t>::min() &&
			maximum <= std::numeric_limits<std::int32_t>::max())
			underlying = program_->types.Fundamental(FUND_INT);
		else if (minimum >= 0 &&
			static_cast<std::uint64_t>(maximum) <=
				std::numeric_limits<std::uint32_t>::max())
			underlying = program_->types.Fundamental(FUND_UNSIGNED_INT);
		else underlying = program_->types.Fundamental(FUND_LONG_LONG_INT);
		program_->entities[entity].underlying = underlying;
	}
	for (std::size_t i = 0; i < enumerators.size(); ++i)
		program_->bindings[enumerators[i]].type = type;
	return type;
}

} }
