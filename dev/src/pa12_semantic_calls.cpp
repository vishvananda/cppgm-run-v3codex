#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::AnalyzeDirectMemberCall(NodeId callee, ScopeId scope,
	const std::vector<NodeId>& argument_syntax, TypeId target,
	ExpressionInfo* result)
{
	while (arena_->IsTag(callee, "parenthesized-expression"))
		callee = FirstSemanticChild(callee);
	if (callee == kNoNode || !arena_->IsTag(callee, "member-expression"))
		return false;
	const std::uint32_t object_edge = arena_->FirstEdge(callee);
	const std::uint32_t name_edge = object_edge == kNoEdge ? kNoEdge :
		arena_->NextEdge(object_edge);
	if (name_edge == kNoEdge)
		throw std::runtime_error("invalid member call expression");
	ExpressionInfo object = AnalyzeExpression(arena_->EdgeChild(object_edge), scope);
	const bool arrow = PayloadSource(callee) == "->";
	TypeId owner_type = EffectiveType(object.type);
	if (arrow)
	{
		owner_type = program_->types.RemoveTopCv(owner_type);
		const TypeRecord pointer = program_->types.Get(owner_type);
		if (pointer.kind != TYPE_POINTER)
			throw std::runtime_error("arrow operand is not a pointer");
		owner_type = pointer.child;
	}
	const EntityId entity = EntityOf(owner_type);
	if (entity == kNoEntity ||
		program_->entities[entity].member_scope == kNoScope)
		throw std::runtime_error("member call on non-class object");
	const NodeId identifier = arena_->EdgeChild(name_edge);
	const NameId name = program_->names.Intern(arena_->Payload(identifier));
	const LookupResult found = program_->LookupMember(
		entity, name, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding ||
		program_->bindings[found.ordinary].kind != BIND_FUNCTION)
		return false;
	const std::vector<BindingId> candidates = FunctionSet(found.ordinary);
	ExpressionInfo object_pointer = object;
	if (!arrow)
	{
		if (object.category != VALUE_LVALUE)
			throw std::runtime_error(
				"member call object is not an addressable lvalue");
		object_pointer.type = program_->types.Pointer(owner_type);
		object_pointer.category = VALUE_PRVALUE;
		object_pointer.binding = kNoBinding;
		const std::uint32_t address = MakeDump(DUMP_UNARY_EXPRESSION,
			object_pointer.type, VALUE_PRVALUE,
			program_->names.Intern("OP_AMP:&"));
		dump_.Add(address, object.node);
		object_pointer.node = address;
		object_pointer.constant = false;
		++expression_count_;
	}
	std::vector<ExpressionInfo> arguments;
	for (std::size_t i = 0; i < argument_syntax.size(); ++i)
		arguments.push_back(AnalyzeExpression(argument_syntax[i], scope));
	ObjectConversionFact object_conversion;
	const BindingId selected = SelectOverload(scope, argument_syntax,
		arguments, candidates, &object_pointer, &object_conversion);
	*result = BuildResolvedCall(selected, scope, argument_syntax,
		arguments, &object_pointer, target, found.naming_class,
		&object_conversion);
	return true;
}

}
}
