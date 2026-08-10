#include "pa12_semantic_detail.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

std::vector<NodeId> SemanticAnalyzer::CollectCallArgumentSyntax(
	NodeId call, NodeId* arguments_node) const
{
	*arguments_node = FindChild(call, "argument-list");
	if (*arguments_node == kNoNode)
		*arguments_node = FindChild(call, "braced-init-list");
	if (*arguments_node == kNoNode)
	{
		std::uint32_t edge = arena_->FirstEdge(call);
		if (edge != kNoEdge) edge = arena_->NextEdge(edge);
		if (edge != kNoEdge) *arguments_node = arena_->EdgeChild(edge);
	}
	std::vector<NodeId> result;
	if (*arguments_node != kNoNode)
		for (std::uint32_t argument = arena_->FirstEdge(*arguments_node);
			argument != kNoEdge; argument = arena_->NextEdge(argument))
			result.push_back(arena_->EdgeChild(argument));
	return result;
}

bool SemanticAnalyzer::CandidateSubstitutionActive() const
{
	return !candidate_substitution_failures_.empty();
}

bool SemanticAnalyzer::CandidateSubstitutionFailed() const
{
	return CandidateSubstitutionActive() &&
		candidate_substitution_failures_.back() != 0;
}

void SemanticAnalyzer::RecordCandidateSubstitutionFailure()
{
	if (!CandidateSubstitutionActive())
		throw std::logic_error("candidate substitution failure has no owner");
	candidate_substitution_failures_.back() = 1;
}

ExpressionInfo SemanticAnalyzer::CandidateSubstitutionFailure()
{
	RecordCandidateSubstitutionFailure();
	return ExpressionInfo();
}

BindingId SemanticAnalyzer::CandidateOverloadFailure(const char* message)
{
	if (CandidateSubstitutionActive())
	{
		RecordCandidateSubstitutionFailure();
		return kNoBinding;
	}
	throw std::runtime_error(message);
}

ExpressionInfo SemanticAnalyzer::CandidateExpressionFailure(
	const char* message)
{
	if (CandidateSubstitutionActive()) return CandidateSubstitutionFailure();
	throw std::runtime_error(message);
}

namespace
{

class CandidateIdentitySet
{
public:
	CandidateIdentitySet() : slots_(8, 0), size_(0) {}
	bool Insert(BindingId binding)
	{
		if ((size_ + 1) * 10 > slots_.size() * 7)
			Rehash(slots_.size() * 2);
		const std::size_t mask = slots_.size() - 1;
		std::size_t slot = MixHash(0, binding) & mask;
		while (slots_[slot] != 0)
		{
			if (slots_[slot] - 1 == binding) return false;
			slot = (slot + 1) & mask;
		}
		slots_[slot] = binding + 1;
		++size_;
		return true;
	}

private:
	void Rehash(std::size_t capacity)
	{
		std::vector<std::uint32_t> replacement(capacity, 0);
		const std::size_t mask = capacity - 1;
		for (std::size_t i = 0; i < slots_.size(); ++i)
		{
			if (slots_[i] == 0) continue;
			const BindingId binding = slots_[i] - 1;
			std::size_t slot = MixHash(0, binding) & mask;
			while (replacement[slot] != 0) slot = (slot + 1) & mask;
			replacement[slot] = binding + 1;
		}
		slots_.swap(replacement);
	}

	std::vector<std::uint32_t> slots_;
	std::size_t size_;
};

}

ExpressionInfo SemanticAnalyzer::AnalyzeBuiltinInvoke(ScopeId scope,
	const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>* analyzed_arguments, TypeId target)
{
	if (argument_syntax.empty())
		return CandidateExpressionFailure(
			"__builtin_invoke requires a callable argument");
	if (analyzed_arguments &&
		analyzed_arguments->size() != argument_syntax.size())
		throw std::logic_error("preanalyzed invoke argument shape is invalid");
	std::vector<ExpressionInfo> values;
	if (analyzed_arguments) values = *analyzed_arguments;
	else
	{
		values.reserve(argument_syntax.size());
		for (std::size_t i = 0; i < argument_syntax.size(); ++i)
			values.push_back(AnalyzeExpression(argument_syntax[i], scope));
	}
	if (CandidateSubstitutionFailed()) return ExpressionInfo();

	ExpressionInfo callable_expression = values.front();
	std::vector<NodeId> operand_syntax(
		argument_syntax.begin() + 1, argument_syntax.end());
	std::vector<ExpressionInfo> operands(values.begin() + 1, values.end());
	ExpressionInfo class_call;
	if (TryAnalyzeCallOperator(scope, callable_expression, operand_syntax,
		&operands, target, &class_call)) return class_call;
	if (TryAnalyzeCallSurrogate(scope, callable_expression, operands,
		target, &class_call)) return class_call;

	TypeId function_type = program_->types.RemoveTopCv(
		EffectiveType(callable_expression.type));
	TypeRecord callable = program_->types.Get(function_type);
	if (callable.kind == TYPE_POINTER)
	{
		function_type = callable.child;
		callable = program_->types.Get(function_type);
	}
	if (callable.kind != TYPE_FUNCTION)
		return CandidateExpressionFailure(
			"__builtin_invoke argument is not callable");
	if (operands.size() < callable.parameter_count ||
		(!callable.variadic && operands.size() != callable.parameter_count))
		return CandidateExpressionFailure("__builtin_invoke arity mismatch");
	const TypeId* parameter_data = program_->types.Parameters(function_type);
	const TypeId result_type = callable.child;
	const TypeRecord returned = program_->types.Get(result_type);
	const ValueCategory category = returned.kind == TYPE_LVALUE_REFERENCE ?
		VALUE_LVALUE : returned.kind == TYPE_RVALUE_REFERENCE ?
		VALUE_XVALUE : VALUE_PRVALUE;
	const std::uint32_t call = MakeDump(
		DUMP_CALL_EXPRESSION, result_type, category);
	dump_.Add(call, callable_expression.node);
	for (std::size_t argument = 0; argument < operands.size(); ++argument)
	{
		ExpressionInfo converted = operands[argument];
		if (argument < callable.parameter_count)
			converted = ApplyCallArgument(converted, parameter_data[argument]);
		if (CandidateSubstitutionFailed()) return ExpressionInfo();
		dump_.Add(call, converted.node);
	}
	ExpressionInfo result;
	result.node = call;
	result.type = result_type;
	result.category = category;
	++expression_count_;
	return ApplyTarget(result, target);
}

bool SemanticAnalyzer::TryAnalyzeImmediateBuiltinCall(
	const std::string& spelling, ScopeId scope,
	const std::vector<NodeId>& argument_syntax, TypeId target,
	ExpressionInfo* result)
{
	if (spelling == "__builtin_expect")
	{
		if (argument_syntax.size() != 2)
			throw std::runtime_error("invalid __builtin_expect call");
		*result = AnalyzeExpression(argument_syntax[0], scope);
		const ExpressionInfo prediction =
			AnalyzeExpression(argument_syntax[1], scope);
		if (!prediction.constant || !IsIntegral(prediction.type, true))
			throw std::runtime_error(
				"__builtin_expect prediction is not constant");
		*result = ApplyTarget(*result, target);
		return true;
	}
	if (AnalyzeBuiltinCall(
		spelling, scope, argument_syntax, target, result)) return true;
	if (spelling == "__builtin_constant_p")
	{
		if (argument_syntax.size() != 1)
			throw std::runtime_error("invalid __builtin_constant_p call");
		const ExpressionInfo operand =
			AnalyzeExpression(argument_syntax[0], scope);
		*result = MakeLiteral(program_->types.Fundamental(FUND_INT),
			program_->names.Intern(operand.constant ? "1" : "0"));
		result->constant = true;
		result->value = operand.constant ? 1 : 0;
		*result = ApplyTarget(*result, target);
		return true;
	}
	if (spelling != "__builtin_abort") return false;
	if (!argument_syntax.empty())
		throw std::runtime_error("invalid __builtin_abort call");
	const TypeId function_type = program_->types.Function(
		program_->types.Fundamental(FUND_VOID), std::vector<TypeId>(), false);
	const std::uint32_t call = MakeDump(DUMP_CALL_EXPRESSION,
		program_->types.Fundamental(FUND_VOID), VALUE_PRVALUE);
	const std::uint32_t callee = MakeDump(DUMP_CALLEE, function_type,
		VALUE_NONE, program_->names.Intern("__builtin_abort"));
	dump_.Add(call, callee);
	result->node = call;
	result->type = program_->types.Fundamental(FUND_VOID);
	++expression_count_;
	*result = ApplyTarget(*result, target);
	return true;
}

bool SemanticAnalyzer::TryAnalyzeClassExpressionInitializer(
	NodeId expression, ScopeId scope, TypeId type,
	ExpressionInfo* initializer)
{
	const EntityId entity = EntityOf(type);
	if (expression == kNoNode || entity == kNoEntity ||
		program_->entities[entity].is_aggregate) return false;
	*initializer = AnalyzeExpression(expression, scope);
	if (program_->types.RemoveTopCv(EffectiveType(initializer->type)) ==
		program_->types.RemoveTopCv(type) &&
		initializer->category == VALUE_PRVALUE &&
		dump_.nodes[initializer->node].kind == DUMP_CALL_EXPRESSION &&
		!dump_.nodes[initializer->node].explicit_user_conversion_call)
	{
		const BindingId selected =
			ValidateClassValueConstruction(type, *initializer);
		*initializer = BuildDirectClassValueTransfer(
			*initializer, type, selected);
	}
	else initializer->node =
		BuildClassValueConstructorAction(type, *initializer);
	return true;
}

TypeId SemanticAnalyzer::ResolveArrowOperand(
	ExpressionInfo* object, ScopeId scope, NodeId object_syntax)
{
	TypeId owner_type = EffectiveType(object->type);
	std::vector<TypeId> arrow_types;
	while (program_->types.Get(
		program_->types.RemoveTopCv(owner_type)).kind != TYPE_POINTER)
	{
		const TypeId arrow_type = program_->types.RemoveTopCv(owner_type);
		if (EntityOf(arrow_type) == kNoEntity ||
			std::find(arrow_types.begin(), arrow_types.end(), arrow_type) !=
				arrow_types.end())
			throw std::runtime_error(
				"arrow operand has no terminating operator-> chain");
		arrow_types.push_back(arrow_type);
		std::vector<NodeId> syntax(1, object_syntax);
		std::vector<ExpressionInfo> operands(1, *object);
		ExpressionInfo converted;
		if (!TryAnalyzeOverloadedOperator("->", scope, syntax, operands,
			true, kNoType, &converted))
			throw std::runtime_error("arrow operand is not a pointer");
		*object = converted;
		owner_type = EffectiveType(object->type);
	}
	const TypeId pointer_type = program_->types.RemoveTopCv(owner_type);
	return program_->types.Get(pointer_type).child;
}

bool SemanticAnalyzer::RefQualifierViable(const ExpressionInfo& object,
	const TypeRecord& function_type) const
{
	if (function_type.ref_qualifier == FUNCTION_REF_NONE) return true;
	if (function_type.ref_qualifier == FUNCTION_REF_RVALUE)
		return object.category != VALUE_LVALUE;
	if (object.category == VALUE_LVALUE) return true;
	return (function_type.cv & CV_CONST) != 0 &&
		(function_type.cv & CV_VOLATILE) == 0;
}

int SemanticAnalyzer::CompareImplicitObjectBindings(ValueCategory category,
	const TypeRecord& left, const TypeRecord& right) const
{
	if (category != VALUE_LVALUE &&
		left.ref_qualifier != right.ref_qualifier)
	{
		if (left.ref_qualifier == FUNCTION_REF_RVALUE) return 1;
		if (right.ref_qualifier == FUNCTION_REF_RVALUE) return -1;
	}
	if (left.cv != right.cv && (left.cv & ~right.cv) == 0) return 1;
	if (left.cv != right.cv && (right.cv & ~left.cv) == 0) return -1;
	return 0;
}

ConversionRank SemanticAnalyzer::MemberCandidateSelectionRank(
	const ExpressionInfo& object, BindingId candidate,
	ConversionRank actual, std::size_t* base_distance) const
{
	if (actual == CONVERSION_INVALID) return actual;
	const BindingRecord& declaration = program_->bindings[candidate];
	if (declaration.canonical == candidate ||
		declaration.access_owner == kNoEntity)
		return actual;
	TypeId owner_type = program_->entities[declaration.access_owner].type;
	const TypeRecord& function_type =
		program_->types.Get(GetFunction(candidate).type);
	if ((function_type.cv & CV_CONST) != 0)
		owner_type = program_->types.Qualify(owner_type, CV_CONST);
	if ((function_type.cv & CV_VOLATILE) != 0)
		owner_type = program_->types.Qualify(owner_type, CV_VOLATILE);
	const TypeId target = program_->types.Pointer(owner_type);
	const ConversionRank selection =
		MemberObjectConversion(object, target, candidate);
	if (selection == CONVERSION_DERIVED_TO_BASE && base_distance)
		*base_distance = BaseConversionDistance(object.type, target);
	return selection;
}

int SemanticAnalyzer::CompareReferenceBindings(
	const ExpressionInfo& argument, TypeId left, TypeId right) const
{
	const TypeKind left_kind = program_->types.Get(left).kind;
	const TypeKind right_kind = program_->types.Get(right).kind;
	const bool left_reference = left_kind == TYPE_LVALUE_REFERENCE ||
		left_kind == TYPE_RVALUE_REFERENCE;
	const bool right_reference = right_kind == TYPE_LVALUE_REFERENCE ||
		right_kind == TYPE_RVALUE_REFERENCE;
	if (argument.category == VALUE_LVALUE &&
		left_reference && right_reference)
	{
		const TypeId source = EffectiveType(argument.type);
		const TypeId left_target = left_kind == TYPE_LVALUE_REFERENCE ||
			left_kind == TYPE_RVALUE_REFERENCE ?
			program_->types.Get(left).child : left;
		const TypeId right_target = right_kind == TYPE_LVALUE_REFERENCE ||
			right_kind == TYPE_RVALUE_REFERENCE ?
			program_->types.Get(right).child : right;
		const bool left_similar = SimilarUnqualified(source, left_target);
		const bool right_similar = SimilarUnqualified(source, right_target);
		if (left_similar && right_similar)
		{
			const auto qualification_subset = [this](TypeId less,
				TypeId more) -> bool
			{
				while (true)
				{
					TypeRecord less_record = program_->types.Get(less);
					TypeRecord more_record = program_->types.Get(more);
					const std::uint8_t less_cv =
						less_record.kind == TYPE_QUALIFIED ?
						less_record.cv : CV_NONE;
					const std::uint8_t more_cv =
						more_record.kind == TYPE_QUALIFIED ?
						more_record.cv : CV_NONE;
					if ((less_cv & ~more_cv) != 0) return false;
					if (less_record.kind == TYPE_QUALIFIED)
					{
						less = less_record.child;
						less_record = program_->types.Get(less);
					}
					if (more_record.kind == TYPE_QUALIFIED)
					{
						more = more_record.child;
						more_record = program_->types.Get(more);
					}
					if (less_record.kind != more_record.kind) return false;
					if (less_record.kind == TYPE_ARRAY &&
						less_record.bound != more_record.bound) return false;
					if (less_record.kind != TYPE_POINTER &&
						less_record.kind != TYPE_ARRAY)
						return less == more;
					less = less_record.child;
					more = more_record.child;
				}
			};
			const bool left_less_qualified =
				qualification_subset(left_target, right_target);
			const bool right_less_qualified =
				qualification_subset(right_target, left_target);
			if (left_less_qualified && !right_less_qualified) return 1;
			if (right_less_qualified && !left_less_qualified) return -1;
		}
		if (left_similar || right_similar) return 0;
	}
	if (left_kind == TYPE_RVALUE_REFERENCE &&
		right_kind == TYPE_LVALUE_REFERENCE)
		return 1;
	if (left_kind == TYPE_LVALUE_REFERENCE &&
		right_kind == TYPE_RVALUE_REFERENCE)
		return -1;
	return 0;
}

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
	const char* display = 0;
	bool nonthrowing = true;
	bool ordinary_visible = false;
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
	case BUILTIN_FUNCTION_OPERATOR_NEW:
	case BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY:
		spelling = kind == BUILTIN_FUNCTION_OPERATOR_NEW ?
			"operatornew" : "operatornew[]";
		display = kind == BUILTIN_FUNCTION_OPERATOR_NEW ?
			"operator_new" : "operator_new__";
		result = pointer;
		nonthrowing = false;
		ordinary_visible = true;
		parameter_types.push_back(size); break;
	case BUILTIN_FUNCTION_OPERATOR_DELETE:
	case BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY:
		spelling = kind == BUILTIN_FUNCTION_OPERATOR_DELETE ?
			"operatordelete" : "operatordelete[]";
		display = kind == BUILTIN_FUNCTION_OPERATOR_DELETE ?
			"operator_delete" : "operator_delete__";
		parameter_types.push_back(pointer);
		ordinary_visible = true; break;
	case BUILTIN_FUNCTION_NONE: break;
	}
	if (!spelling) throw std::logic_error("unknown builtin function kind");
	std::vector<ParameterInfo> parameters;
	for (std::size_t i = 0; i < parameter_types.size(); ++i)
		parameters.push_back(ParameterInfo(display ? program_->names.Intern(
			"arg" + std::to_string(i)) : 0, parameter_types[i],
			parameter_types[i]));
	const TypeId type = program_->types.Function(result, parameter_types, false);
	const BindingId binding = DeclareFunction(program_->GlobalScope(),
		program_->names.Intern(spelling), type, parameters, false, false,
		STORAGE_CLASS_NONE, LANGUAGE_LINKAGE_CPP, nonthrowing, ordinary_visible);
	program_->bindings[binding].builtin_function = kind;
	if (display)
		GetMutableFunction(binding).display_name = program_->names.Intern(display);
	GetMutableFunction(binding).deferred = !GetFunction(binding).defined;
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
	else if (spelling == "::operator new")
		kind = BUILTIN_FUNCTION_OPERATOR_NEW;
	else if (spelling == "::operator delete")
		kind = BUILTIN_FUNCTION_OPERATOR_DELETE;
	else if (spelling == "::operator new[]")
		kind = BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY;
	else if (spelling == "::operator delete[]")
		kind = BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY;
	if (kind == BUILTIN_FUNCTION_NONE) return false;
	const BindingId binding = EnsureBuiltinFunction(kind);
	const TypeRecord& type = program_->types.Get(GetFunction(binding).type);
	if (argument_syntax.size() != type.parameter_count)
		throw std::runtime_error("invalid builtin function call arity");
	std::vector<ExpressionInfo> arguments;
	for (std::size_t i = 0; i < argument_syntax.size(); ++i)
		arguments.push_back(
			AnalyzeUntypedCallArgument(argument_syntax[i], scope));
	*result = BuildResolvedCall(binding, scope, argument_syntax,
		arguments, 0, target);
	return true;
}

TypeId SemanticAnalyzer::ResolveFunctionalCastType(ScopeId scope,
	const std::string& spelling, NodeId syntax)
{
	const NodeId structure = syntax == kNoNode ? kNoNode :
		FindChild(syntax, "structured-type-name");
	const TypeId specialization = structure == kNoNode ? kNoType :
		ResolveStructuredTypeName(structure, scope);
	if (specialization != kNoType) return specialization;
	if (structure != kNoNode) return kNoType;
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
		owner_type = ResolveArrowOperand(
			&object, scope, arena_->EdgeChild(object_edge));
	EnsureClassDefinition(owner_type);
	const EntityId entity = EntityOf(owner_type);
	if (entity == kNoEntity ||
		program_->entities[entity].member_scope == kNoScope)
		throw std::runtime_error("member call on non-class object");
	const NodeId identifier = arena_->EdgeChild(name_edge);
	const std::string member_spelling = arena_->Payload(identifier);
	const NodeId member_structure = FindChild(
		identifier, "structured-type-name");
	const NamePath member_path = member_structure == kNoNode ?
		ParseNamePath(member_spelling) : StructuredNamePath(member_structure);
	const NameId name = member_path.Last();
	const bool explicitly_qualified =
		(member_path.global || member_path.Size() > 1) &&
		member_spelling.compare(0, 8, "operator") != 0;
	LookupResult found = explicitly_qualified ? LookupStructuredName(
		identifier, scope, LOOKUP_ORDINARY) :
		program_->LookupMember(entity, name, LOOKUP_ORDINARY);
	const LookupResult template_found = explicitly_qualified ?
		LookupStructuredName(identifier, scope, LOOKUP_FUNCTION_TEMPLATE) :
		program_->LookupMember(entity, name, LOOKUP_FUNCTION_TEMPLATE);
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
	std::vector<std::size_t> template_patterns;
	for (std::size_t owner = 0;
		owner < template_found.FunctionTemplateOwnerCount(); ++owner)
	{
		const ScopeId template_owner =
			template_found.FunctionTemplateOwnerAt(owner);
		const std::uint64_t key =
			(static_cast<std::uint64_t>(template_owner) << 32) | name;
		const CompactIndexSequence* indexed = template_function_sets_.Find(key);
		if (!indexed) continue;
		for (std::size_t pattern = 0; pattern < indexed->Size(); ++pattern)
			template_patterns.push_back((*indexed)[pattern]);
	}
	const bool ordinary_functions = found.ordinary != kNoBinding &&
		program_->bindings[found.ordinary].kind == BIND_FUNCTION;
	if (!ordinary_functions && template_patterns.empty()) return false;
	if (!arrow && object.category == VALUE_PRVALUE &&
		dump_.nodes[object.node].kind != DUMP_TEMPORARY_OBJECT)
		object = MaterializeTemporary(object);
	std::vector<BindingId> candidates = ordinary_functions ?
		FunctionSet(found.ordinary, !template_patterns.empty()) :
		std::vector<BindingId>();
	ExpressionInfo object_pointer = object;
	if (!arrow)
	{
		object_pointer = MakeImplicitObjectPointer(object);
	}
	else object_pointer.category = VALUE_LVALUE;
	std::vector<ExpressionInfo> arguments;
	for (std::size_t i = 0; i < argument_syntax.size(); ++i)
		arguments.push_back(
			AnalyzeUntypedCallArgument(argument_syntax[i], scope));
	if (!template_patterns.empty())
	{
		NamePath syntax_base;
		std::vector<NodeId> explicit_syntax;
		const bool has_explicit_syntax = CollectExplicitTemplateArguments(
			identifier, &syntax_base, &explicit_syntax);
		if (has_explicit_syntax)
			candidates.clear();
		std::vector<BindingId> specializations;
		if (has_explicit_syntax)
			DeduceFunctionTemplatePatternsWithExplicitSyntax(template_patterns,
				arguments, explicit_syntax, scope, &specializations,
				&argument_syntax);
		else DeduceFunctionTemplatePatterns(template_patterns, arguments,
			&specializations, 0, 0, scope, &argument_syntax);
		for (std::size_t i = 0; i < specializations.size(); ++i)
		{
			const BindingId canonical =
				program_->bindings[specializations[i]].canonical;
			bool present = false;
			for (std::size_t prior = 0; prior < candidates.size(); ++prior)
				if (program_->bindings[candidates[prior]].canonical == canonical)
					present = true;
			if (!present) candidates.push_back(specializations[i]);
		}
	}
	if (candidates.empty()) return false;
	ObjectConversionFact object_conversion;
	std::vector<CallConversionFact> argument_conversions;
	const BindingId selected = SelectOverload(scope, argument_syntax,
		arguments, candidates, &object_pointer, &object_conversion,
		&argument_conversions);
	if (selected == kNoBinding)
	{
		*result = ExpressionInfo();
		return true;
	}
	*result = BuildResolvedCall(selected, scope, argument_syntax,
		arguments, &object_pointer, target, found.naming_class,
		&object_conversion, &argument_conversions,
		explicitly_qualified);
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
		destroyed_type = ResolveArrowOperand(
			&object, scope, arena_->EdgeChild(object_edge));
	destroyed_type = program_->types.RemoveTopCv(EffectiveType(destroyed_type));
	EnsureClassDefinition(destroyed_type);
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
	LookupResult destructor_type = LookupSpelling(
		scope, spelling.substr(1), LOOKUP_TYPE);
	if (destructor_type.type == kNoType)
		destructor_type = program_->LookupMember(entity,
			ParseNamePath(spelling.substr(1)).Last(), LOOKUP_TYPE);
	if (destructor == kNoBinding || destructor_type.type == kNoType ||
		program_->types.RemoveTopCv(EffectiveType(destructor_type.type)) !=
			destroyed_type)
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
		object_pointer.constexpr_object = kNoConstexprObject;
		object_pointer.constexpr_complete_object = kNoConstexprObject;
		++expression_count_;
	}
	const std::vector<NodeId> no_syntax;
	const std::vector<ExpressionInfo> no_arguments;
	*result = BuildResolvedCall(destructor, scope, no_syntax,
		no_arguments, &object_pointer, target, entity, 0, 0, true);
	return true;
}

std::vector<BindingId> SemanticAnalyzer::FunctionCandidates(ScopeId scope,
	const std::string& spelling, EntityId* naming_class, NodeId syntax,
	bool exclude_template_specializations)
{
	if (naming_class) *naming_class = kNoEntity;
	std::string lookup_name = spelling;
	NamePath structured_base;
	std::vector<NodeId> explicit_syntax;
	const bool structured_explicit = CollectExplicitTemplateArguments(
		syntax, &structured_base, &explicit_syntax);
	if (structured_explicit)
	{
		std::vector<BindingId> explicit_candidates;
		std::vector<std::size_t> patterns =
			FindStructuredFunctionTemplates(syntax, scope);
		if (patterns.empty())
			patterns = FindFunctionTemplates(scope, structured_base);
		for (std::size_t i = 0; i < patterns.size(); ++i)
		{
			const FunctionTemplatePattern& pattern =
				function_templates_[patterns[i]];
			bool has_template_parameter_pack = false;
			for (std::size_t parameter = 0;
				parameter < pattern.parameters.size(); ++parameter)
				if (pattern.parameters[parameter].pack)
					has_template_parameter_pack = true;
			std::vector<TemplateArgument> arguments;
			candidate_substitution_failures_.push_back(0);
			const bool built_arguments = !has_template_parameter_pack &&
				BuildTemplateArguments(pattern.parameters, explicit_syntax,
					scope, pattern.lexical_scope, &arguments);
			const BindingId candidate = built_arguments ?
				InstantiateFunctionTemplate(patterns[i], arguments) : kNoBinding;
			const bool substitution_failed = CandidateSubstitutionFailed();
			candidate_substitution_failures_.pop_back();
			if (built_arguments && !substitution_failed)
			{
				if (candidate != kNoBinding &&
					std::find(explicit_candidates.begin(),
						explicit_candidates.end(), candidate) ==
						explicit_candidates.end())
					explicit_candidates.push_back(candidate);
			}
			else
			{
				NamePath type_base;
				std::vector<TypeId> explicit_arguments;
				if (!ParseExplicitTemplateArguments(
					syntax, scope, &type_base, &explicit_arguments)) continue;
				if (pattern.specialization_argument_offsets.size() !=
					pattern.specialization_bindings.size())
					throw std::logic_error(
						"function template specialization argument range is invalid");
				for (std::size_t specialization = 0;
					specialization < pattern.specialization_bindings.size();
					++specialization)
				{
					const std::size_t first =
						pattern.specialization_argument_offsets[specialization];
					const std::size_t last = specialization + 1 <
						pattern.specialization_argument_offsets.size() ?
						pattern.specialization_argument_offsets[specialization + 1] :
						pattern.specialization_arguments.size();
					if (first > last || last >
						pattern.specialization_arguments.size() ||
						explicit_arguments.size() > last - first)
						continue;
					bool matches = true;
					for (std::size_t argument = 0;
						argument < explicit_arguments.size(); ++argument)
						if (pattern.specialization_arguments[
							first + argument] != TemplateArgument(
								TEMPLATE_ARGUMENT_TYPE,
								explicit_arguments[argument]))
							matches = false;
					if (!matches) continue;
					const BindingId candidate =
						pattern.specialization_bindings[specialization];
					if (std::find(explicit_candidates.begin(),
						explicit_candidates.end(), candidate) ==
						explicit_candidates.end())
						explicit_candidates.push_back(candidate);
				}
			}
		}
		return explicit_candidates;
	}
	const LookupResult found = syntax != kNoNode &&
		FindChild(syntax, "structured-type-name") != kNoNode ?
		LookupStructuredName(syntax, scope, LOOKUP_ORDINARY) :
		LookupSpelling(scope, lookup_name, LOOKUP_ORDINARY);
	if (naming_class) *naming_class = found.naming_class;
	if (found.ordinary == kNoBinding) return std::vector<BindingId>();
	if (program_->bindings[found.ordinary].kind != BIND_FUNCTION)
		return std::vector<BindingId>();
	std::vector<BindingId> collected;
	for (std::size_t i = 0; i < found.OrdinaryCount(); ++i)
		AppendFunctionSet(found.OrdinaryAt(i), &collected,
			exclude_template_specializations);
	CandidateIdentitySet seen;
	std::vector<BindingId> result;
	result.reserve(collected.size());
	for (std::size_t i = 0; i < collected.size(); ++i)
		if (seen.Insert(program_->bindings[collected[i]].canonical))
			result.push_back(collected[i]);
	return result;
}

std::vector<BindingId> SemanticAnalyzer::FunctionCallCandidates(
	ScopeId scope, const std::string& spelling, EntityId* naming_class,
	NodeId syntax, bool exclude_template_specializations)
{
	std::vector<BindingId> result =
		FunctionCandidates(scope, spelling, naming_class, syntax,
			exclude_template_specializations);
	result.erase(std::remove_if(result.begin(), result.end(),
		[this](BindingId candidate) {
			return GetFunction(candidate).constructor;
		}), result.end());
	return result;
}
TypeId SemanticAnalyzer::ParameterBindingType(
	const ParameterInfo& parameter) const
{
	const TypeKind kind = program_->types.Get(parameter.declared_type).kind;
	return kind == TYPE_ARRAY || kind == TYPE_FUNCTION ?
		parameter.function_type : parameter.declared_type;
}

ExpressionInfo SemanticAnalyzer::AnalyzeMember(NodeId node, ScopeId scope)
{
	const std::uint32_t first = arena_->FirstEdge(node);
	const std::uint32_t second = first == kNoEdge ? kNoEdge :
		arena_->NextEdge(first);
	if (second == kNoEdge) throw std::runtime_error("invalid member expression");
	ExpressionInfo object = AnalyzeExpression(arena_->EdgeChild(first), scope);
	const std::string source_operation = PayloadSource(node);
	if (source_operation == "." && object.category == VALUE_PRVALUE &&
		EntityOf(object.type) != kNoEntity &&
		dump_.nodes[object.node].kind != DUMP_TEMPORARY_OBJECT)
		object = MaterializeTemporary(object);
	TypeId owner_type = EffectiveType(object.type);
	if (source_operation == "->")
		owner_type = ResolveArrowOperand(
			&object, scope, arena_->EdgeChild(first));
	EnsureClassDefinition(owner_type);
	const EntityId entity = EntityOf(owner_type);
	if (entity == kNoEntity || program_->entities[entity].member_scope == kNoScope)
	{
		if (CandidateSubstitutionActive())
			return CandidateSubstitutionFailure();
		throw std::runtime_error("member access on non-class object");
	}
	const NodeId identifier = arena_->EdgeChild(second);
	const NodeId member_structure = FindChild(
		identifier, "structured-type-name");
	const NameId name = member_structure == kNoNode ?
		ParseNamePath(arena_->Payload(identifier)).Last() :
		StructuredNamePath(member_structure).Last();
	const LookupResult found = program_->LookupMember(
		entity, name, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding)
	{
		if (CandidateSubstitutionActive())
			return CandidateSubstitutionFailure();
		throw std::runtime_error("unknown class member");
	}
	if (!CanAccessMember(found.ordinary, found.naming_class, entity))
	{
		if (CandidateSubstitutionActive())
			return CandidateSubstitutionFailure();
		throw std::runtime_error("inaccessible class member");
	}
	const EntityId member_owner =
		program_->bindings[found.ordinary].member_owner;
	const bool member_function =
		program_->bindings[found.ordinary].kind == BIND_FUNCTION;
	if (member_function)
	{
		if (!program_->bindings[found.ordinary].static_member_function)
			throw std::runtime_error(
				"non-static member function requires a call expression");
		DemandFunction(found.ordinary);
	}
	const BindingRecord& member_binding =
		program_->bindings[found.ordinary];
	TypeId type = member_binding.type;
	if (IsConst(owner_type) && !member_binding.mutable_member)
		type = program_->types.Qualify(type, CV_CONST);
	if (!member_binding.non_static_data_member && !member_function)
		EnsureStaticMemberStorage(found.ordinary);
	std::string operation = arena_->Payload(node);
	const std::size_t colon = operation.find(':');
	if (colon != std::string::npos) operation.erase(colon + 1);
	operation += program_->names.Get(name);
	ValueCategory member_category = VALUE_LVALUE;
	if (source_operation != "->" && member_binding.non_static_data_member &&
		object.category != VALUE_LVALUE &&
		!program_->types.IsReference(member_binding.type))
		member_category = VALUE_XVALUE;
	const std::uint32_t expression = MakeDump(DUMP_MEMBER_EXPRESSION,
		type, member_category, program_->names.Intern(operation), found.ordinary);
	if (member_owner != kNoEntity)
	{
		const std::size_t projections = BaseProjectionCount(owner_type,
			program_->entities[member_owner].type);
		if (projections == std::numeric_limits<std::size_t>::max() ||
			projections > std::numeric_limits<std::uint32_t>::max())
			throw std::logic_error("member has no bounded base path");
		dump_.nodes[expression].base_projection_count =
			static_cast<std::uint32_t>(projections);
	}
	dump_.Add(expression, object.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = type;
	result.category = member_category;
	result.binding = found.ordinary;
	if (member_binding.non_static_data_member)
	{
		std::uint32_t object_address = source_operation == "->" ?
			ExpressionAddress(object) : LvalueAddress(&object);
		if (object_address != kNoConstexprAddress &&
			member_binding.member_offset <=
				static_cast<std::uint64_t>(
					std::numeric_limits<std::int64_t>::max()))
		{
			const std::uint32_t member_address = OffsetConstexprAddress(
				object_address,
				static_cast<std::int64_t>(member_binding.member_offset), true,
				static_cast<std::int64_t>(program_->SizeOf(
					EffectiveType(member_binding.type))));
			if (member_address != kNoConstexprAddress)
				SetExpressionLvalueAddress(&result, member_address);
		}
	}
	const BindingRecord& canonical = program_->bindings[
		program_->bindings[found.ordinary].canonical];
	if ((constant_expression_required_depth_ != 0 ||
		constexpr_evaluation_depth_ != 0) &&
		member_binding.non_static_data_member &&
		object.constexpr_object != kNoConstexprObject)
	{
		const ConstexprObjectElement* element = ConstexprClassMemberAt(
			object.constexpr_object, found.ordinary);
		if (element)
			SetExpressionObjectElement(&result, *element);
	}
	else if (canonical.constant)
		SetExpressionBindingConstant(&result,
			program_->bindings[found.ordinary].canonical);
	dump_.nodes[expression].constant = result.constant &&
		result.constexpr_object == kNoConstexprObject &&
		result.constexpr_address == kNoConstexprAddress;
	if (!result.floating_constant &&
		result.constexpr_object == kNoConstexprObject &&
		result.constexpr_address == kNoConstexprAddress)
		dump_.nodes[expression].constant_value = result.value;
	++expression_count_;
	return result;
}

std::vector<BindingId> SemanticAnalyzer::FunctionSet(BindingId binding,
	bool exclude_template_specializations)
{
	std::vector<BindingId> result;
	AppendFunctionSet(binding, &result, exclude_template_specializations);
	return result;
}

void SemanticAnalyzer::AppendFunctionSet(BindingId binding,
	std::vector<BindingId>* result, bool exclude_template_specializations)
{
	if (binding == kNoBinding || binding >= program_->bindings.size() ||
		program_->bindings[binding].kind != BIND_FUNCTION)
		return;
	const BindingRecord& record = program_->bindings[binding];
	const std::uint64_t key = (static_cast<std::uint64_t>(record.owner) << 32) |
		record.name;
	const CompactIndexSequence* set = exclude_template_specializations ?
		ordinary_nontemplate_function_sets_.Find(key) :
		ordinary_function_sets_.Find(key);
	if (!set) return;
	if (result->empty()) result->reserve(set->Size());
	for (std::size_t i = 0; i < set->Size(); ++i)
	{
		++function_candidate_index_visits_;
		const BindingId candidate = static_cast<BindingId>((*set)[i]);
		const BindingRecord& candidate_record = program_->bindings[candidate];
		if (candidate_record.access_owner != kNoEntity &&
			candidate_record.canonical != candidate)
		{
			const FunctionInfo& function = GetFunction(candidate);
			const FunctionSignatureKey signature_key(record.owner, record.name,
				function.signature);
			++function_signature_lookups_;
			if (function_declarations_.Find(signature_key) != kNoBinding)
				continue;
		}
		result->push_back(candidate);
	}
}

}
}
