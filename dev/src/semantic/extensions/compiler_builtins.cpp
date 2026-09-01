#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"
#include "semantic/presentation/source_identity.h"

#include <algorithm>
#include <string>
#include <vector>

namespace cppgm
{
namespace semantic
{
namespace
{

std::string QuoteNarrowString(const std::string& value)
{
	std::string result(1, '"');
	for (std::size_t i = 0; i < value.size(); ++i)
	{
		const unsigned char character =
			static_cast<unsigned char>(value[i]);
		if (character == '\\' || character == '"')
		{
			result += '\\';
			result += static_cast<char>(character);
		}
		else if (character == '\n') result += "\\n";
		else if (character == '\r') result += "\\r";
		else if (character == '\t') result += "\\t";
		else if (character >= 0x20 && character < 0x7f)
			result += static_cast<char>(character);
		else result += '?';
	}
	result += '"';
	return result;
}

void AppendTemplateSubstitutions(const Program& program,
	const std::vector<TemplateParameter>& parameters,
	std::uint32_t first, std::uint32_t count,
	std::vector<semantic::presentation::TemplateBinding>* output)
{
	if (count == 0) return;
	if (parameters.empty() || first > program.canonical_template_arguments.size() ||
		count > program.canonical_template_arguments.size() - first)
		ThrowInternalCompilerError("pretty-function template arguments are invalid");
	for (std::size_t i = 0; i < count; ++i)
	{
		const TemplateParameter& parameter =
			TemplateParameterForArgument(parameters, i);
		if (parameter.name != 0)
			output->push_back(semantic::presentation::TemplateBinding(
				parameter.name, program.canonical_template_arguments[first + i]));
	}
}

}

ExpressionInfo Analyzer::AnalyzePredefinedFunctionName(
	NodeId syntax, TypeId target, bool pretty)
{
	if (current_function_context_ == kNoBinding)
		ThrowSemanticError("__func__ outside function scope");
	const BindingId binding =
		program_->bindings[current_function_context_].canonical;
	const FunctionInfo& function = GetFunction(binding);
	std::string name;
	if (!pretty)
	{
		const NameId display = ReadFunctionSourceDisplayName(function);
		name = display == 0 ? std::string() : program_->names.Get(display);
	}
	else
	{
		std::vector<semantic::presentation::TemplateBinding> substitutions;
		const BindingRecord& record = program_->bindings[binding];
		std::vector<EntityId> owners;
		for (EntityId owner = record.member_owner; owner != kNoEntity;)
		{
			if (owner >= program_->entities.size())
				ThrowInternalCompilerError("pretty-function owner is invalid");
			owners.push_back(owner);
			owner = program_->entities[owner].enclosing_class;
		}
		std::reverse(owners.begin(), owners.end());
		for (std::size_t i = 0; i < owners.size(); ++i)
		{
			const EntityId owner = owners[i];
			if (owner >= class_template_pattern_by_entity_.size()) continue;
			const std::uint32_t pattern =
				class_template_pattern_by_entity_[owner];
			if (pattern == kNoDumpEdge) continue;
			if (pattern >= class_templates_.size())
				ThrowInternalCompilerError("pretty-function class pattern is invalid");
			const EntityRecord& entity = program_->entities[owner];
			if (entity.template_argument_begin != kNoBinding)
				AppendTemplateSubstitutions(*program_,
					class_templates_[pattern].parameters,
					entity.template_argument_begin,
					entity.template_argument_count, &substitutions);
		}
		if (function.template_pattern != kNoDumpEdge)
		{
			if (function.template_pattern >= function_templates_.size())
				ThrowInternalCompilerError("pretty-function pattern is invalid");
			AppendTemplateSubstitutions(*program_,
				function_templates_[function.template_pattern].parameters,
				record.template_argument_begin,
				record.template_argument_count, &substitutions);
		}
		name = semantic::presentation::RenderFunction(
			*program_, binding, function.type, substitutions);
	}
	return ApplyTarget(MakeStringLiteral(QuoteNarrowString(name)), target);
}

bool Analyzer::TryAnalyzeCompilerPredefinedValue(
	const std::string& spelling, NodeId syntax, TypeId target,
	ExpressionInfo* result)
{
	if (spelling == "__func__" || spelling == "__PRETTY_FUNCTION__")
	{
		*result = AnalyzePredefinedFunctionName(
			syntax, target, spelling == "__PRETTY_FUNCTION__");
		return true;
	}
	if (spelling != "__null") return false;
	*result = MakeLiteral(program_->types.Fundamental(FUND_NULLPTR_T),
		program_->names.Intern("nullptr"));
	result->constant = true;
	result->value = 0;
	*result = ApplyTarget(*result, target);
	return true;
}

bool Analyzer::TryAnalyzeTypeofFunctionalCast(NodeId callee,
	const std::vector<NodeId>& arguments, ScopeId scope,
	TypeId target, ExpressionInfo* result)
{
	if (!arena_->IsTag(callee, ::cppgm::syntax::STAG_ID_EXPRESSION) ||
		arena_->Payload(callee).compare(0, 8, "__typeof") != 0) return false;
	const NodeId decltype_name = arena_->IsTag(callee, ::cppgm::syntax::STAG_ID_EXPRESSION) ?
		FindChild(callee, ::cppgm::syntax::STAG_DECLTYPE_NAME) : kNoNode;
	if (decltype_name == kNoNode ||
		FindChild(decltype_name, ::cppgm::syntax::STAG_QUALIFIED_NAME) != kNoNode) return false;
	const TypeId cast_type = DecltypeType(
		FirstSemanticChild(decltype_name), scope);
	if (CandidateSubstitutionFailed() || cast_type == kNoType)
	{
		*result = ExpressionInfo();
		return true;
	}
	if (arguments.size() != 1)
		ThrowSemanticError("typeof cast requires one argument");
	*result = ApplyTarget(
		AnalyzeUntypedCallArgument(arguments[0], scope), cast_type);
	if (target != kNoType) *result = ApplyTarget(*result, target);
	return true;
}

ExpressionInfo Analyzer::AnalyzeBuiltinOffsetof(
	NodeId syntax, ScopeId scope, TypeId target)
{
	const std::uint32_t type_edge = arena_->FirstEdge(syntax);
	const std::uint32_t member_edge = type_edge == kNoEdge ? kNoEdge :
		arena_->NextEdge(type_edge);
	if (member_edge == kNoEdge)
		ThrowSemanticError("invalid offsetof expression");
	const NodeId operand = arena_->EdgeChild(type_edge);
	const NodeId type_syntax = FindChild(operand, ::cppgm::syntax::STAG_TYPE_ID);
	const TypeId type = BuildTypeId(type_syntax, scope);
	if (CandidateSubstitutionFailed() || type == kNoType)
		return ExpressionInfo();
	const EntityId entity = EntityOf(
		program_->types.RemoveTopCv(EffectiveType(type)));
	if (entity == kNoEntity)
	{
		if (FunctionTemplateTypeIsDependent(type))
		{
			ExpressionInfo dependent;
			dependent.type = program_->types.Fundamental(
				FUND_UNSIGNED_LONG_INT);
			dependent.category = VALUE_PRVALUE;
			dependent.node = MakeDump(DUMP_SIZEOF_EXPRESSION,
				dependent.type, dependent.category);
			dependent.constant = true;
			dependent.value = 0;
			dump_.nodes[dependent.node].template_parameter_constant = true;
			dump_.nodes[dependent.node].template_layout_constant = true;
			RecordExpressionFacts(dependent);
			++expression_count_;
			return ApplyTarget(dependent, target);
		}
		ThrowSemanticError("offsetof requires a class type");
	}
	EnsureClassDefinition(type);
	const NameId name = program_->names.Intern(
		arena_->Payload(arena_->EdgeChild(member_edge)));
	const LookupResult found =
		program_->LookupMember(entity, name, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding)
		ThrowSemanticError("offsetof names an unknown member");
	const BindingRecord& member = program_->bindings[found.ordinary];
	if (!member.non_static_data_member)
		ThrowSemanticError("offsetof requires a non-static data member");
	if (member.bit_field)
		ThrowSemanticError("offsetof cannot name a bit-field");
	ExpressionInfo result;
	result.type = program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	result.category = VALUE_PRVALUE;
	result.node = MakeDump(DUMP_SIZEOF_EXPRESSION,
		result.type, result.category);
	result.constant = true;
	result.value = static_cast<std::int64_t>(
		program_->BindingLayout(member).member_offset);
	dump_.nodes[result.node].template_layout_constant =
		IsClassTemplateSpecializationContext(entity);
	RecordExpressionFacts(result);
	++expression_count_;
	return ApplyTarget(result, target);
}

bool Analyzer::TryAnalyzeCompilerFunctionBuiltin(
	const std::string& spelling, ScopeId scope,
	const std::vector<NodeId>& argument_syntax, NodeId call_syntax,
	TypeId target, ExpressionInfo* result)
{
	const bool vector_convert = spelling == "__builtin_convertvector";
	const bool vector_reduce_or = spelling == "__builtin_reduce_or";
	const bool bit_cast = spelling == "__builtin_bit_cast";
	if (vector_convert || vector_reduce_or || bit_cast)
	{
		const std::size_t expected = vector_reduce_or ? 1 : 2;
		if (argument_syntax.size() != expected)
			ThrowSemanticError("invalid scalarized vector builtin arity");
		TypeId result_type = kNoType;
		NodeId value_syntax = argument_syntax[0];
		if (vector_convert || bit_cast)
		{
			const NodeId type_syntax = argument_syntax[vector_convert ? 1 : 0];
			value_syntax = argument_syntax[vector_convert ? 0 : 1];
			if (arena_->IsTag(type_syntax, ::cppgm::syntax::STAG_TYPE_ID))
				result_type = BuildTypeId(type_syntax, scope);
			else if (arena_->IsTag(type_syntax, ::cppgm::syntax::STAG_ID_EXPRESSION))
			{
				const LookupResult found =
					LookupSyntaxName(type_syntax, scope, LOOKUP_TYPE);
				result_type = found.type;
			}
			if (CandidateSubstitutionFailed() || result_type == kNoType)
				return true;
		}
		ExpressionInfo value = AnalyzeUntypedCallArgument(value_syntax, scope);
		const TypeId value_type = program_->types.RemoveTopCv(
			EffectiveType(value.type));
		if (vector_reduce_or)
		{
			const TypeRecord& vector = program_->types.Get(value_type);
			if (vector.kind != TYPE_VECTOR ||
				vector.bound != program_->SizeOf(vector.child))
				ThrowSemanticError(
					"__builtin_reduce_or requires a scalarized vector operand");
			result_type = program_->types.Fundamental(FUND_BOOL);
		}
		else if (vector_convert)
		{
			const TypeId converted = program_->types.RemoveTopCv(
				EffectiveType(result_type));
			const TypeRecord& source = program_->types.Get(value_type);
			const TypeRecord& destination = program_->types.Get(converted);
			if (source.kind != TYPE_VECTOR || destination.kind != TYPE_VECTOR ||
				source.bound != program_->SizeOf(source.child) ||
				destination.bound != program_->SizeOf(destination.child) ||
				source.bound != destination.bound)
				ThrowSemanticError(
					"__builtin_convertvector requires equal-width scalarized vectors");
			result_type = converted;
		}
		else if (!IsIntegral(value_type, true) ||
			!IsIntegral(result_type, true) ||
			program_->SizeOf(value_type) != program_->SizeOf(result_type))
			ThrowSemanticError(
				"scalarized __builtin_bit_cast requires equal-width integer types");

		const std::uint32_t cast = MakeDump(
			DUMP_CAST_EXPRESSION, result_type, VALUE_PRVALUE);
		dump_.nodes[cast].operand_type = value_type;
		dump_.Add(cast, value.node);
		result->node = cast;
		result->type = result_type;
		result->category = VALUE_PRVALUE;
		RecordExpressionFacts(*result);
		++expression_count_;
		*result = ApplyTarget(*result, target);
		return true;
	}
	CompilerIntrinsicKind overflow = COMPILER_INTRINSIC_NONE;
	if (spelling == "__builtin_add_overflow")
		overflow = COMPILER_INTRINSIC_ADD_OVERFLOW;
	else if (spelling == "__builtin_sub_overflow")
		overflow = COMPILER_INTRINSIC_SUB_OVERFLOW;
	else if (spelling == "__builtin_mul_overflow")
		overflow = COMPILER_INTRINSIC_MUL_OVERFLOW;
	if (overflow != COMPILER_INTRINSIC_NONE)
	{
		if (argument_syntax.size() != 3)
			ThrowSemanticError("overflow builtin requires three arguments");
		ExpressionInfo left =
			AnalyzeUntypedCallArgument(argument_syntax[0], scope);
		const TypeId value_type = program_->types.RemoveTopCv(
			EffectiveType(left.type));
		if (!IsIntegral(value_type, true) || IntegralWidth(value_type) > 64)
			ThrowSemanticError(
				"overflow builtin requires an integer type up to 64 bits");
		left = ApplyCallArgument(left, value_type);
		ExpressionInfo right = ApplyCallArgument(
			AnalyzeUntypedCallArgument(argument_syntax[1], scope), value_type);
		const TypeId pointer_type = program_->types.Pointer(value_type);
		ExpressionInfo destination = ApplyCallArgument(
			AnalyzeUntypedCallArgument(argument_syntax[2], scope), pointer_type);
		const TypeId result_type = program_->types.Fundamental(FUND_BOOL);
		std::vector<TypeId> parameters;
		parameters.push_back(value_type);
		parameters.push_back(value_type);
		parameters.push_back(pointer_type);
		const TypeId function_type = program_->types.Function(
			result_type, parameters, false);
		const std::uint32_t call = MakeDump(
			DUMP_CALL_EXPRESSION, result_type, VALUE_PRVALUE);
		dump_.nodes[call].compiler_intrinsic = overflow;
		dump_.nodes[call].operand_type = value_type;
		const std::uint32_t callee = MakeDump(DUMP_CALLEE, function_type,
			VALUE_NONE, program_->names.Intern(spelling));
		dump_.Add(call, callee);
		dump_.Add(call, left.node);
		dump_.Add(call, right.node);
		dump_.Add(call, destination.node);
		result->node = call;
		result->type = result_type;
		result->category = VALUE_PRVALUE;
		++expression_count_;
		*result = ApplyTarget(*result, target);
		return true;
	}
	const bool source_string = spelling == "__builtin_FILE" ||
		spelling == "__builtin_FUNCTION";
	const bool source_integer = spelling == "__builtin_LINE" ||
		spelling == "__builtin_COLUMN";
	if (source_string || source_integer)
	{
		if (!argument_syntax.empty())
			ThrowSemanticError("source-location builtin requires no arguments");
		if (source_string)
		{
			std::string value;
			if (spelling == "__builtin_FILE")
				value = arena_->SourceFile(call_syntax);
			else if (current_function_context_ != kNoBinding)
			{
				const FunctionInfo& function = GetFunction(
					program_->bindings[current_function_context_].canonical);
				const NameId display = ReadFunctionSourceDisplayName(function);
				if (display != 0) value = program_->names.Get(display);
			}
			*result = ApplyTarget(
				MakeStringLiteral(QuoteNarrowString(value)), target);
			return true;
		}
		const std::size_t value = spelling == "__builtin_LINE" ?
			arena_->SourceLine(call_syntax) : arena_->SourceColumn(call_syntax);
		*result = MakeLiteral(program_->types.Fundamental(FUND_INT),
			program_->names.Intern(std::to_string(value)));
		SetExpressionScalar(result,
			ConstexprScalarValue(static_cast<std::int64_t>(value)));
		*result = ApplyTarget(*result, target);
		return true;
	}
	if (spelling == "__builtin_is_constant_evaluated")
	{
		if (!argument_syntax.empty())
			ThrowSemanticError(
				"is_constant_evaluated requires no arguments");
		*result = MakeLiteral(program_->types.Fundamental(FUND_BOOL),
			program_->names.Intern("false"));
		SetExpressionScalar(result,
			ConstexprScalarValue(static_cast<std::int64_t>(0)));
		*result = ApplyTarget(*result, target);
		return true;
	}
	if (spelling != "__builtin_addressof") return false;
	if (argument_syntax.size() != 1)
		ThrowSemanticError("addressof requires one argument");
	ExpressionInfo operand = AnalyzeExpression(argument_syntax[0], scope);
	if (operand.category != VALUE_LVALUE)
		ThrowSemanticError("addressof requires an lvalue");
	if (operand.binding != kNoBinding &&
		program_->bindings[operand.binding].bit_field)
		ThrowSemanticError("addressof cannot address a bit-field");
	const TypeId result_type = program_->types.Pointer(EffectiveType(operand.type));
	const std::uint32_t expression = MakeDump(DUMP_UNARY_EXPRESSION,
		result_type, VALUE_PRVALUE, program_->names.Intern("&"));
	dump_.Add(expression, operand.node);
	result->node = expression;
	result->type = result_type;
	result->category = VALUE_PRVALUE;
	++expression_count_;
	*result = ApplyTarget(*result, target);
	return true;
}

bool Analyzer::TryAnalyzeCompilerFunctionAlias(
	const std::string& spelling, ScopeId scope,
	const std::vector<NodeId>& argument_syntax, TypeId target,
	ExpressionInfo* result)
{
	if (spelling.compare(0, 10, "__builtin_") == 0 &&
		!hosted_builtin::FindIntegerIntrinsic(spelling) &&
		!hosted_builtin::FindFloatingIntrinsic(spelling) &&
		!hosted_builtin::FindMemoryIntrinsic(spelling) &&
		!hosted_builtin::FindAtomicIntrinsic(spelling))
	{
		const std::string alias = spelling.substr(10);
		std::vector<BindingId> candidates = FunctionCandidates(
			program_->GlobalScope(), program_->names.Intern(alias), 0, true);
		if (!candidates.empty())
		{
			std::vector<ExpressionInfo> arguments;
			for (std::size_t i = 0; i < argument_syntax.size(); ++i)
				arguments.push_back(
					AnalyzeUntypedCallArgument(argument_syntax[i], scope));
			std::vector<CallConversionFact> conversions;
			const BindingId selected = SelectOverload(scope, argument_syntax,
				arguments, candidates, 0, 0, &conversions);
			if (selected == kNoBinding) return false;
			*result = BuildResolvedCall(selected, scope, argument_syntax,
				arguments, 0, target, kNoEntity, 0, &conversions);
			return true;
		}
	}
	return false;
}

}
}
