#include "semantic/analysis/analyzer.h"

#include <ostream>

namespace cppgm
{
namespace semantic
{

NameId Analyzer::DumpFunctionText(const DumpNode& node)
{
	// Function presentation is demanded here, at the dump output boundary,
	// rather than being rendered when the node is created.
	if (node.text != 0 || node.binding == kNoBinding ||
		node.binding >= program_->bindings.size())
		return node.text;
	return ReadFunctionDisplayName(GetFunction(node.binding));
}

void Analyzer::RenderLine(const DumpNode& node, std::size_t depth)
{
	for (std::size_t i = 0; i < depth; ++i) output_ << "  ";
	const char* category = node.category == VALUE_LVALUE ? "lvalue" :
		node.category == VALUE_XVALUE ? "xvalue" : "prvalue";
	switch (node.kind)
	{
	case DUMP_TRANSLATION_UNIT: output_ << "translation-unit"; break;
	case DUMP_NAMESPACE:
		output_ << "namespace-definition " << program_->names.Get(node.text); break;
	case DUMP_TYPE_ALIAS:
		output_ << "type-alias " << program_->names.Get(node.text) << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_VARIABLE:
		output_ << "variable " << program_->names.Get(node.text) << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_FUNCTION_DECLARATION:
		output_ << "function-declaration "
			<< program_->names.Get(DumpFunctionText(node))
			<< ' ' << program_->RenderType(node.type); break;
	case DUMP_FUNCTION_DEFINITION:
		output_ << "function-definition "
			<< program_->names.Get(DumpFunctionText(node))
			<< ' ' << program_->RenderType(node.type); break;
	case DUMP_PARAMETER:
		output_ << "parameter " << program_->names.Get(node.text) << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_COMPOUND_STATEMENT: output_ << "compound-statement"; break;
	case DUMP_SIMPLE_DECLARATION: output_ << "simple-declaration"; break;
	case DUMP_RETURN_STATEMENT: output_ << "return-statement"; break;
	case DUMP_EXPRESSION_STATEMENT: output_ << "expression-statement"; break;
	case DUMP_STATEMENT_EXPRESSION: output_ << "statement-expression "
		<< category << ' ' << program_->RenderType(node.type); break;
	case DUMP_STATEMENT_EXPRESSION_RESULT:
		output_ << "statement-expression-result"; break;
	case DUMP_IF_STATEMENT: output_ << "if-statement"; break;
	case DUMP_SWITCH_STATEMENT: output_ << "switch-statement"; break;
	case DUMP_WHILE_STATEMENT: output_ << "while-statement"; break;
	case DUMP_DO_STATEMENT: output_ << "do-statement"; break;
	case DUMP_FOR_STATEMENT: output_ << "for-statement"; break;
	case DUMP_BREAK_STATEMENT: output_ << "break-statement"; break;
	case DUMP_CONTINUE_STATEMENT: output_ << "continue-statement"; break;
	case DUMP_CONDITION: output_ << "condition"; break;
	case DUMP_CONDITION_DECLARATION: output_ << "condition-declaration"; break;
	case DUMP_FOR_INIT_STATEMENT: output_ << "for-init-statement"; break;
	case DUMP_ITERATION: output_ << "iteration"; break;
	case DUMP_THEN: output_ << "then"; break;
	case DUMP_ELSE: output_ << "else"; break;
	case DUMP_CASE_STATEMENT: output_ << "case-statement"; break;
	case DUMP_DEFAULT_STATEMENT: output_ << "default-statement"; break;
	case DUMP_LABELED_STATEMENT:
		output_ << "labeled-statement " << program_->names.Get(node.text); break;
	case DUMP_GOTO_STATEMENT:
		output_ << "goto-statement " << program_->names.Get(node.text); break;
	case DUMP_GNU_ASM_STATEMENT:
		output_ << "gnu-asm-statement " << static_cast<unsigned>(
			node.gnu_asm_operation); break;
	case DUMP_CALL_EXPRESSION:
		output_ << "call-expression " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_CALLEE:
		output_ << "callee " << program_->names.Get(DumpFunctionText(node))
			<< ' ' << program_->RenderType(node.type); break;
	case DUMP_ID_EXPRESSION:
		output_ << "id-expression " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_LITERAL:
		output_ << "literal " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_UNARY_EXPRESSION:
		output_ << "unary-expression " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_POSTFIX_EXPRESSION:
		output_ << "postfix-expression " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_BINARY_EXPRESSION:
		output_ << "binary-expression " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_SUBSCRIPT_EXPRESSION:
		output_ << "subscript-expression " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_CONDITIONAL_EXPRESSION:
		output_ << "conditional-expression " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_CONDITIONAL_ARM:
		output_ << "conditional-arm " << program_->RenderType(node.type); break;
	case DUMP_SIZEOF_EXPRESSION:
		output_ << "sizeof-expression " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_ASSIGNMENT_EXPRESSION:
		output_ << "assignment-expression " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_CAST_EXPRESSION:
		output_ << "cast-expression " << category << ' '
			<< program_->RenderType(node.type);
		if (node.text != 0) output_ << ' ' << program_->names.Get(node.text);
		break;
	case DUMP_TYPEID_EXPRESSION:
		output_ << "typeid-expression " << category << ' '
			<< program_->RenderType(node.type) << " queried="
			<< program_->RenderType(node.operand_type);
		break;
	case DUMP_DYNAMIC_CAST_EXPRESSION:
		output_ << "dynamic-cast-expression " << category << ' '
			<< program_->RenderType(node.type) << " source="
			<< program_->RenderType(node.operand_type);
		break;
	case DUMP_THROW_EXPRESSION:
		output_ << "throw-expression " << category;
		if (node.operand_type != kNoType)
			output_ << " object=" << program_->RenderType(node.operand_type);
		break;
	case DUMP_TRY_STATEMENT: output_ << "try-statement"; break;
	case DUMP_HANDLER:
		output_ << "handler";
		if (node.operand_type != kNoType)
			output_ << " match=" << program_->RenderType(node.operand_type);
		break;
	case DUMP_INITIALIZER_LIST:
		output_ << "initializer-list " << category << ' '
			<< program_->RenderType(node.type) << " element="
			<< program_->RenderType(node.operand_type) << " count="
			<< node.array_count;
		break;
	case DUMP_INITIALIZER_LIST_BEGIN:
		output_ << "initializer-list-begin " << category << ' '
			<< program_->RenderType(node.type);
		break;
	case DUMP_INITIALIZER_LIST_SIZE:
		output_ << "initializer-list-size " << category << ' '
			<< program_->RenderType(node.type);
		break;
	case DUMP_BRACED_INIT_LIST:
		output_ << "braced-init-list " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_INITIALIZER_ACTION:
		output_ << "initializer-action " << program_->names.Get(node.text)
			<< ' ' << program_->RenderType(node.type); break;
	case DUMP_BASE_INITIALIZER_ACTION:
		output_ << "base-initializer-action "
			<< program_->RenderType(node.type); break;
	case DUMP_VPTR_INITIALIZATION_ACTION:
		output_ << "vptr-initialization-action "
			<< program_->RenderType(node.type); break;
	case DUMP_DELEGATING_INITIALIZER_ACTION:
		output_ << "delegating-initializer-action "
			<< program_->RenderType(node.type); break;
	case DUMP_MEMBER_EXPRESSION:
		output_ << "member-expression " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	case DUMP_NEW_EXPRESSION: output_ << "new-expression " << category << ' '
		<< program_->RenderType(node.type); break;
	case DUMP_DELETE_EXPRESSION: output_ << "delete-expression " << category
		<< ' ' << program_->RenderType(node.type); break;
	case DUMP_TEMPORARY_OBJECT: output_ << "temporary-object " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_AGGREGATE_CONSTRUCTION_ACTION:
		output_ << "aggregate-construction-action "
			<< program_->RenderType(node.type) << " helper="
			<< node.aggregate_helper; break;
	case DUMP_CLASS_VALUE_TRANSFER:
		output_ << "class-value-transfer " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_SPECIAL_MEMBER_CONSTRUCTION_ACTION:
		output_ << "special-member-construction "
			<< program_->RenderType(node.type); break;
	case DUMP_SPECIAL_MEMBER_ASSIGNMENT_ACTION:
		output_ << "special-member-assignment " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION:
		output_ << "special-member-subobject "
			<< program_->RenderType(node.type); break;
	case DUMP_CONSTRUCTOR_ACTION:
		output_ << "constructor-action "
			<< program_->names.Get(DumpFunctionText(node)); break;
	case DUMP_CONSTRUCTOR_ARRAY_ACTION:
		output_ << "constructor-array-action "
			<< program_->RenderType(node.operand_type); break;
	case DUMP_DESTRUCTOR_ACTION:
		output_ << "destructor-action "
			<< program_->names.Get(DumpFunctionText(node))
			<< ' ' << program_->RenderType(node.operand_type); break;
	case DUMP_COMPLEX_CONSTRUCTION:
		output_ << "complex-construction " << category << ' '
			<< program_->RenderType(node.type); break;
	case DUMP_COMPLEX_COMPONENT:
		output_ << "complex-component " << category << ' '
			<< program_->RenderType(node.type) << ' '
			<< program_->names.Get(node.text); break;
	}
	output_ << '\n';
}

}
}
