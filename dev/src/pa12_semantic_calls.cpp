#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

BindingId SemanticAnalyzer::EnsureBuiltinFunction(BuiltinFunctionKind kind)
{
	if (kind == BUILTIN_FUNCTION_NONE)
		throw std::logic_error("missing builtin function kind");
	if (builtin_functions_.size() <= static_cast<std::size_t>(kind))
		builtin_functions_.resize(static_cast<std::size_t>(kind) + 1, kNoBinding);
	if (builtin_functions_[kind] != kNoBinding) return builtin_functions_[kind];
	const TypeId character = program_->types.Fundamental(FUND_CHAR);
	const TypeId const_character_pointer = program_->types.Pointer(
		program_->types.Qualify(character, CV_CONST));
	const TypeId void_type = program_->types.Fundamental(FUND_VOID);
	const TypeId pointer = program_->types.Pointer(void_type);
	const TypeId const_pointer = program_->types.Pointer(
		program_->types.Qualify(void_type, CV_CONST));
	const TypeId size = program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	TypeId result = program_->types.Fundamental(FUND_VOID);
	const char* spelling = 0;
	std::vector<TypeId> parameter_types;
	switch (kind)
	{
	case BUILTIN_FUNCTION_STRLEN:
		spelling = "__builtin_strlen"; result = size;
		parameter_types.push_back(const_character_pointer); break;
	case BUILTIN_FUNCTION_UNREACHABLE:
		spelling = "__builtin_unreachable"; break;
	case BUILTIN_FUNCTION_MEMCPY:
	case BUILTIN_FUNCTION_MEMMOVE:
		spelling = kind == BUILTIN_FUNCTION_MEMCPY ?
			"__builtin_memcpy" : "__builtin_memmove";
		result = pointer;
		parameter_types.push_back(pointer);
		parameter_types.push_back(const_pointer);
		parameter_types.push_back(size); break;
	case BUILTIN_FUNCTION_NONE: break;
	}
	if (!spelling) throw std::logic_error("unknown builtin function kind");
	std::vector<ParameterInfo> parameters;
	for (std::size_t i = 0; i < parameter_types.size(); ++i)
		parameters.push_back(ParameterInfo(0, parameter_types[i],
			parameter_types[i]));
	const TypeId type = program_->types.Function(result, parameter_types, false);
	const BindingId binding = DeclareFunction(program_->GlobalScope(),
		program_->names.Intern(spelling), type, parameters, false, false,
		STORAGE_CLASS_NONE, LANGUAGE_LINKAGE_CPP, true, false);
	program_->bindings[binding].builtin_function = kind;
	GetMutableFunction(binding).deferred = true;
	builtin_functions_[kind] = binding;
	return binding;
}

bool SemanticAnalyzer::AnalyzeBuiltinCall(const std::string& spelling,
	ScopeId scope, const std::vector<NodeId>& argument_syntax, TypeId target,
	ExpressionInfo* result)
{
	BuiltinFunctionKind kind = BUILTIN_FUNCTION_NONE;
	if (spelling == "__builtin_strlen") kind = BUILTIN_FUNCTION_STRLEN;
	else if (spelling == "__builtin_unreachable")
		kind = BUILTIN_FUNCTION_UNREACHABLE;
	else if (spelling == "__builtin_memcpy") kind = BUILTIN_FUNCTION_MEMCPY;
	else if (spelling == "__builtin_memmove") kind = BUILTIN_FUNCTION_MEMMOVE;
	if (kind == BUILTIN_FUNCTION_NONE) return false;
	const BindingId binding = EnsureBuiltinFunction(kind);
	const TypeRecord& type = program_->types.Get(GetFunction(binding).type);
	if (argument_syntax.size() != type.parameter_count)
		throw std::runtime_error("invalid builtin function call arity");
	std::vector<ExpressionInfo> arguments;
	for (std::size_t i = 0; i < argument_syntax.size(); ++i)
		arguments.push_back(AnalyzeExpression(argument_syntax[i], scope));
	*result = BuildResolvedCall(binding, scope, argument_syntax,
		arguments, 0, target);
	return true;
}

TypeId SemanticAnalyzer::ResolveFunctionalCastType(ScopeId scope,
	const std::string& spelling)
{
	const LookupResult named = LookupSpelling(scope, spelling, LOOKUP_TYPE);
	if (named.type != kNoType) return named.type;
	if (spelling.size() > 10 && spelling.compare(0, 9, "decltype(") == 0 &&
		spelling[spelling.size() - 1] == ')')
	{
		const LookupResult operand = LookupSpelling(scope,
			spelling.substr(9, spelling.size() - 10), LOOKUP_ORDINARY);
		if (operand.ordinary != kNoBinding)
			return program_->bindings[operand.ordinary].type;
	}
	FundamentalKind kind = FUND_INT;
	if (spelling == "bool") kind = FUND_BOOL;
	else if (spelling == "char") kind = FUND_CHAR;
	else if (spelling == "short" || spelling == "short int")
		kind = FUND_SHORT_INT;
	else if (spelling == "int") kind = FUND_INT;
	else if (spelling == "long" || spelling == "long int")
		kind = FUND_LONG_INT;
	else if (spelling == "unsigned" || spelling == "unsigned int")
		kind = FUND_UNSIGNED_INT;
	else if (spelling == "unsigned long") kind = FUND_UNSIGNED_LONG_INT;
	else if (spelling == "float") kind = FUND_FLOAT;
	else if (spelling == "double") kind = FUND_DOUBLE;
	else if (spelling == "long double") kind = FUND_LONG_DOUBLE;
	else return kNoType;
	return program_->types.Fundamental(kind);
}

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
	const std::string member_spelling = arena_->Payload(identifier);
	const NameId name = ParseNamePath(member_spelling).Last();
	LookupResult found = program_->LookupMember(
		entity, name, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding &&
		member_spelling.compare(0, 8, "operator") == 0)
	{
		std::size_t target_first = 8;
		while (target_first < member_spelling.size() &&
			member_spelling[target_first] == ' ') ++target_first;
		const LookupResult requested = LookupSpelling(scope,
			member_spelling.substr(target_first), LOOKUP_TYPE);
		std::vector<BindingId> conversions;
		AppendConversionFunctions(entity, &conversions);
		for (std::size_t i = 0; requested.type != kNoType &&
			i < conversions.size(); ++i)
			if (GetFunction(conversions[i]).conversion_target == requested.type)
			{
				found.ordinary = conversions[i];
				found.naming_class = program_->bindings[conversions[i]].member_owner;
				break;
			}
	}
	if (found.ordinary == kNoBinding ||
		program_->bindings[found.ordinary].kind != BIND_FUNCTION)
		return false;
	if (!arrow && object.category != VALUE_LVALUE &&
		dump_.nodes[object.node].kind != DUMP_TEMPORARY_OBJECT)
		object = MaterializeTemporary(object);
	const std::vector<BindingId> candidates = FunctionSet(found.ordinary);
	ExpressionInfo object_pointer = object;
	if (!arrow)
	{
		object_pointer = MakeImplicitObjectPointer(object);
	}
	else object_pointer.category = VALUE_LVALUE;
	std::vector<ExpressionInfo> arguments;
	for (std::size_t i = 0; i < argument_syntax.size(); ++i)
		arguments.push_back(AnalyzeExpression(argument_syntax[i], scope));
	ObjectConversionFact object_conversion;
	std::vector<CallConversionFact> argument_conversions;
	const BindingId selected = SelectOverload(scope, argument_syntax,
		arguments, candidates, &object_pointer, &object_conversion,
		&argument_conversions);
	*result = BuildResolvedCall(selected, scope, argument_syntax,
		arguments, &object_pointer, target, found.naming_class,
		&object_conversion, &argument_conversions);
	return true;
}

bool SemanticAnalyzer::AnalyzeExplicitDestructorCall(NodeId callee,
	ScopeId scope, const std::vector<NodeId>& argument_syntax, TypeId target,
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
		throw std::runtime_error("invalid explicit destructor expression");
	const NodeId identifier = arena_->EdgeChild(name_edge);
	const std::string spelling = arena_->Payload(identifier);
	if (spelling.empty() || spelling[0] != '~') return false;
	if (!argument_syntax.empty())
		throw std::runtime_error("explicit destructor call has arguments");

	ExpressionInfo object = AnalyzeExpression(arena_->EdgeChild(object_edge), scope);
	const bool arrow = PayloadSource(callee) == "->";
	TypeId destroyed_type = EffectiveType(object.type);
	if (arrow)
	{
		destroyed_type = program_->types.RemoveTopCv(destroyed_type);
		const TypeRecord pointer = program_->types.Get(destroyed_type);
		if (pointer.kind != TYPE_POINTER)
			throw std::runtime_error(
				"explicit destructor arrow operand is not a pointer");
		destroyed_type = pointer.child;
	}
	destroyed_type = program_->types.RemoveTopCv(EffectiveType(destroyed_type));
	const EntityId entity = EntityOf(destroyed_type);
	if (entity == kNoEntity)
	{
		const LookupResult named = LookupSpelling(scope, spelling.substr(1),
			LOOKUP_TYPE);
		if (named.type == kNoType ||
			program_->types.RemoveTopCv(EffectiveType(named.type)) != destroyed_type)
			throw std::runtime_error("pseudo-destructor type mismatch");
		const TypeId void_type =
			program_->types.Fundamental(FUND_VOID);
		const std::uint32_t discarded = MakeDump(DUMP_CAST_EXPRESSION,
			void_type, VALUE_PRVALUE);
		dump_.Add(discarded, object.node);
		result->node = discarded;
		result->type = void_type;
		result->category = VALUE_PRVALUE;
		++expression_count_;
		*result = ApplyTarget(*result, target);
		return true;
	}

	const BindingId destructor = DestructorForType(destroyed_type);
	if (destructor == kNoBinding ||
		program_->names.Get(program_->bindings[destructor].name) != spelling)
		throw std::runtime_error("class has no matching destructor");
	ExpressionInfo object_pointer = object;
	if (arrow)
	{
		object_pointer.type = program_->types.Pointer(destroyed_type);
		object_pointer.category = VALUE_PRVALUE;
	}
	else
	{
		if (object.category != VALUE_LVALUE)
			throw std::runtime_error(
				"explicit destructor object is not an addressable lvalue");
		object_pointer.type = program_->types.Pointer(destroyed_type);
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
	const std::vector<NodeId> no_syntax;
	const std::vector<ExpressionInfo> no_arguments;
	*result = BuildResolvedCall(destructor, scope, no_syntax,
		no_arguments, &object_pointer, target, entity);
	return true;
}

}
}
