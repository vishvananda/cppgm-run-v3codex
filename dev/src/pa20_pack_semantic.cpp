#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

ExpressionInfo SemanticAnalyzer::AnalyzeSizeofPackExpression(
	NodeId node, ScopeId scope)
{
	const NameId name = program_->names.Intern(arena_->Payload(node));
	std::vector<TemplateArgument> template_arguments;
	std::vector<BindingId> function_arguments;
	std::size_t count = 0;
	if (LookupTemplateArgumentPack(scope, name, &template_arguments))
		count = template_arguments.size();
	else if (LookupFunctionParameterPack(scope, name, &function_arguments))
		count = function_arguments.size();
	else throw std::runtime_error("sizeof names no parameter pack");
	ExpressionInfo result;
	result.type = program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	result.node = MakeDump(DUMP_SIZEOF_EXPRESSION, result.type, VALUE_PRVALUE);
	result.constant = true;
	result.value = static_cast<std::int64_t>(count);
	RecordExpressionFacts(result);
	++expression_count_;
	return result;
}

void SemanticAnalyzer::InitializeFunctionTemplatePackShape(
	FunctionTemplatePattern* pattern, const DeclaratorInfo& shape)
{
	pattern->function_parameter_pack =
		HasTrailingTemplateParameterPack(pattern->parameters) &&
		program_->types.Get(pattern->shape_type).variadic;
	pattern->required_parameter_count =
		RequiredFunctionParameterCount(shape.parameters);
	if (pattern->function_parameter_pack &&
		pattern->required_parameter_count != 0)
		--pattern->required_parameter_count;
}

void SemanticAnalyzer::BindFunctionParameterPackElement(
	ScopeId scope, NameId pack, BindingId binding)
{
	if (pack == 0) return;
	const std::uint64_t key =
		(static_cast<std::uint64_t>(scope) << 32) | pack;
	function_parameter_pack_bindings_.Ensure(key).Push(binding);
}

bool SemanticAnalyzer::ExpandCallArgumentPacks(
	const std::vector<NodeId>& original, ScopeId scope,
	std::vector<NodeId>* syntax, std::vector<ExpressionInfo>* arguments)
{
	bool has_expansion = false;
	for (std::size_t i = 0; i < original.size(); ++i)
		if (arena_->IsTag(original[i], "pack-expansion-expression"))
			has_expansion = true;
	if (!has_expansion) return false;
	const std::vector<NodeId> input = original;
	syntax->clear();
	arguments->clear();
	for (std::size_t i = 0; i < input.size(); ++i)
	{
		if (!arena_->IsTag(input[i], "pack-expansion-expression"))
		{
			syntax->push_back(input[i]);
			arguments->push_back(AnalyzeExpression(input[i], scope));
			continue;
		}
		const NodeId operand = FirstSemanticChild(input[i]);
		if (operand == kNoNode)
			throw std::runtime_error(
				"unsupported PA20 pack expansion expression");
		if (arena_->IsTag(operand, "id-expression"))
		{
			const NameId name = program_->names.Intern(arena_->Payload(operand));
			std::vector<BindingId> bindings;
			if (!LookupFunctionParameterPack(scope, name, &bindings))
				throw std::runtime_error(
					"pack expansion does not name a function parameter pack");
			for (std::size_t element = 0; element < bindings.size(); ++element)
			{
				const BindingId binding = bindings[element];
				if (binding >= program_->bindings.size())
					throw std::logic_error(
						"function parameter pack binding is invalid");
				const BindingRecord& record = program_->bindings[binding];
				ExpressionInfo expression;
				expression.type = record.type;
				expression.category = VALUE_LVALUE;
				expression.binding = binding;
				expression.node = MakeDump(DUMP_ID_EXPRESSION,
					record.type, VALUE_LVALUE, name, binding);
				syntax->push_back(kNoNode);
				arguments->push_back(expression);
				++expression_count_;
			}
			continue;
		}
		if (arena_->IsTag(operand, "sizeof-expression"))
		{
			const NodeId type_id = FirstSemanticChild(operand);
			const NodeId specifiers = type_id == kNoNode ? kNoNode :
				FindChild(type_id, "type-specifier-seq");
			const NodeId spelling_node = specifiers == kNoNode ? kNoNode :
				FirstSemanticChild(specifiers);
			const NameId name = spelling_node == kNoNode ? 0 :
				program_->names.Intern(PayloadSource(spelling_node));
			std::vector<TemplateArgument> elements;
			if (name == 0 || !LookupTemplateArgumentPack(scope, name, &elements))
				throw std::runtime_error(
					"sizeof expansion does not name a type pack");
			for (std::size_t element = 0; element < elements.size(); ++element)
			{
				if (elements[element].kind != TEMPLATE_ARGUMENT_TYPE)
					throw std::runtime_error(
						"sizeof type expansion contains a value argument");
				const ScopeId element_scope = NewScope(scope,
					SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(scope));
				program_->AddBinding(element_scope, BIND_TYPE_ALIAS,
					name, elements[element].type);
				syntax->push_back(kNoNode);
				arguments->push_back(AnalyzeExpression(operand, element_scope));
			}
			continue;
		}
		throw std::runtime_error("unsupported PA20 pack expansion expression");
	}
	return true;
}

}
}
