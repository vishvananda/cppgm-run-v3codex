// Enum declaration analysis: builds enum entities, their enumerators, and
// their underlying-type facts from syntax.
#include "pa12_semantic_detail.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm { namespace pa12_semantic_detail {

BindingId LocalTypeContext(const Program& program, ScopeId owner,
	BindingId current_function);

TypeId SemanticAnalyzer::AnalyzeEnum(NodeId node, ScopeId scope, const std::string& hint, bool elaborated)
{
	std::string spelling;
	NamePath path;
	bool generated_identity = false;
	BuildEnumDeclarationNamePath(node, hint, &spelling, &path,
		&generated_identity);
	const bool scoped = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_ENUM_KEY) != kNoNode;
	const NamedFlavor flavor = scoped ? NAMED_ENUM_CLASS : NAMED_ENUM;
	const NameId name = path.Last();
	const ScopeId owner = ResolveOwner(scope, path);
	if (owner == kNoScope) throw std::runtime_error("enum owner not found");
	const NodeId underlying_node = FindChild(node, ::cppgm::pa10_syntax_detail::STAG_TYPE_ID);
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
		if (old.type == kNoType) throw std::runtime_error("unknown enum type");
		return old.type;
	}
	EntityId entity = kNoEntity;
	if (old.type != kNoType)
	{
		const TypeRecord named = program_->types.Get(
			program_->types.RemoveTopCv(old.type));
		if (named.kind != TYPE_NAMED)
			throw std::runtime_error("enum redeclared as non-enum");
		entity = named.entity;
		if (program_->entities[entity].flavor != flavor)
			throw std::runtime_error("incompatible enum redeclaration");
	}
	else
	{
		entity = program_->NewEntity(name, flavor, true, underlying, owner);
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
	std::int64_t next = 0;
	std::int64_t minimum = 0;
	std::int64_t maximum = 0;
	std::vector<BindingId> enumerators;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId enumerator = arena_->EdgeChild(edge);
		if (!arena_->IsTag(enumerator, ::cppgm::pa10_syntax_detail::STAG_ENUMERATOR)) continue;
		const NodeId initializer = FirstSemanticChild(enumerator);
		std::int64_t value = next;
		if (initializer != kNoNode)
		{
			// A specialization demanded from a discarded expression arm still
			// analyzes its enumerators as independent constant-expression roots.
			const std::size_t outer_suppression =
				constant_evaluation_suppressed_depth_;
			constant_evaluation_suppressed_depth_ = 0;
			ExpressionInfo expression;
			try
			{
				expression = AnalyzeExpression(initializer, value_scope);
			}
			catch (...)
			{
				constant_evaluation_suppressed_depth_ = outer_suppression;
				throw;
			}
			constant_evaluation_suppressed_depth_ = outer_suppression;
			if (!expression.constant)
				throw std::runtime_error("nonconstant enumerator");
			value = expression.value;
		}
		const NameId enumerator_name =
			program_->names.UseInterned(arena_->PayloadId(enumerator));
		const BindingId binding = program_->AddBinding(value_scope,
			BIND_ENUMERATOR, enumerator_name, underlying, true, value);
		enumerators.push_back(binding);
		if (value < minimum) minimum = value;
		if (value > maximum) maximum = value;
		if (value == INT64_MAX) throw std::runtime_error("enumerator overflow");
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
