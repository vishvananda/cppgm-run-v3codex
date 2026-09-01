#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"


namespace cppgm
{
namespace semantic
{

TypeId Analyzer::BuildComplexSpecifierType(TypeId element)
{
	const TypeId result = program_->types.TryComplex(
		program_->types.RemoveTopCv(element));
	if (result == kNoType)
		ThrowSemanticError("invalid _Complex element type");
	return result;
}

ExpressionInfo Analyzer::AnalyzeComplexConstruction(ScopeId scope,
	const std::vector<NodeId>& arguments, TypeId target)
{
	if (arguments.size() != 2)
		ThrowSemanticError("__builtin_complex requires two operands");
	ExpressionInfo real = AnalyzeUntypedCallArgument(arguments[0], scope);
	ExpressionInfo imaginary = AnalyzeUntypedCallArgument(arguments[1], scope);
	const TypeId real_type = program_->types.RemoveTopCv(
		EffectiveType(real.type));
	const TypeId imaginary_type = program_->types.RemoveTopCv(
		EffectiveType(imaginary.type));
	if (real_type != imaginary_type ||
		(!IsIntegral(real_type) && !IsFloating(real_type)))
		ThrowSemanticError(
			"__builtin_complex operands must have the same real arithmetic type");
	const TypeId type = program_->types.TryComplex(real_type);
	if (type == kNoType)
		ThrowSemanticError("invalid __builtin_complex element type");
	const std::uint32_t node = MakeDump(
		DUMP_COMPLEX_CONSTRUCTION, type, VALUE_PRVALUE);
	dump_.Add(node, real.node);
	dump_.Add(node, imaginary.node);
	ExpressionInfo result;
	result.node = node;
	result.type = type;
	result.category = VALUE_PRVALUE;
	++expression_count_;
	return ApplyTarget(result, target);
}

ExpressionInfo Analyzer::AnalyzeComplexComponent(
	const std::string& operation, const ExpressionInfo& operand, TypeId target)
{
	const TypeId complex_type = program_->types.RemoveTopCv(
		EffectiveType(operand.type));
	const TypeRecord& complex = program_->types.Get(complex_type);
	if (complex.kind != TYPE_COMPLEX)
		ThrowSemanticError("complex component operator requires _Complex");
	const ValueCategory category = operand.category == VALUE_PRVALUE ?
		VALUE_PRVALUE : operand.category;
	const std::uint32_t node = MakeDump(DUMP_COMPLEX_COMPONENT,
		complex.child, category, program_->names.Intern(operation));
	dump_.Add(node, operand.node);
	ExpressionInfo result;
	result.node = node;
	result.type = complex.child;
	result.category = category;
	++expression_count_;
	return ApplyTarget(result, target);
}

}
}
