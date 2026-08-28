#include "pa12_semantic_detail.h"
#include "preprocess/tokens/post_tokenizer.h"

#include <cctype>
#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa12_semantic_detail
{
namespace
{

std::string DecodeAsmString(const SyntaxArena& arena, NodeId node,
	const char* error)
{
	std::string decoded;
	if (!DecodeNarrowStringLiteralSequence(arena.SemanticPayload(node),
		&decoded)) throw std::runtime_error(error);
	return decoded;
}

std::string CompactAsmTemplate(const std::string& source)
{
	std::string result;
	result.reserve(source.size());
	for (std::size_t i = 0; i < source.size(); ++i)
		if (!std::isspace(static_cast<unsigned char>(source[i])))
			result.push_back(source[i]);
	return result;
}

GnuAsmOperation ClassifyAsmTemplate(const std::string& source)
{
	const std::string compact = CompactAsmTemplate(source);
	if (compact.empty()) return GNU_ASM_COMPILER_FENCE;
	if (compact == "nop") return GNU_ASM_NOP;
	if (compact == "pause" || compact == "rep;nop") return GNU_ASM_PAUSE;
	if (compact == "bswap%0") return GNU_ASM_BSWAP;
	if (compact == "lock;notb%0") return GNU_ASM_LOCK_NOT;
	if (compact == "lock;incl%[storage]") return GNU_ASM_LOCK_INCREMENT;
	throw std::runtime_error("unsupported GNU asm template");
}

NodeId AsmOperandExpression(const SyntaxArena& arena, NodeId operand)
{
	for (std::uint32_t edge = arena.FirstEdge(operand); edge != kNoEdge;
		edge = arena.NextEdge(edge))
	{
		const NodeId child = arena.EdgeChild(edge);
		if (!arena.IsTag(child, ::cppgm::syntax::STAG_GNU_ASM_SYMBOL)) return child;
	}
	return kNoNode;
}

}

void SemanticAnalyzer::ApplyFunctionAsmLabel(
	NodeId declarator, BindingId binding)
{
	const NodeId syntax = FindChild(declarator, ::cppgm::syntax::STAG_GNU_ASM_LABEL);
	if (syntax == kNoNode) return;
	const std::string label = DecodeAsmString(
		*arena_, syntax, "invalid GNU asm label");
	if (label.empty()) throw std::runtime_error("empty GNU asm label");
	binding = program_->bindings[binding].canonical;
	BindingRecord& record = program_->bindings[binding];
	const NameId name = program_->names.Intern(label);
	if (record.assembly_name != 0 && record.assembly_name != name)
		throw std::runtime_error("conflicting GNU asm labels");
	record.assembly_name = name;
}

bool SemanticAnalyzer::AnalyzeGnuAsmStatement(
	NodeId node, ScopeId scope, std::uint32_t output_parent)
{
	if (!arena_->IsTag(node, ::cppgm::syntax::STAG_GNU_ASM_STATEMENT)) return false;
	const GnuAsmOperation operation = ClassifyAsmTemplate(DecodeAsmString(
		*arena_, node, "invalid GNU asm template"));
	const std::uint32_t statement = MakeDump(DUMP_GNU_ASM_STATEMENT);
	dump_.nodes[statement].gnu_asm_operation = operation;
	dump_.Add(output_parent, statement);
	std::size_t outputs = 0;
	std::size_t inputs = 0;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId operand = arena_->EdgeChild(edge);
		if (arena_->IsTag(operand, ::cppgm::syntax::STAG_GNU_ASM_CLOBBER))
		{
			(void)DecodeAsmString(*arena_, operand,
				"invalid GNU asm clobber");
			continue;
		}
		if (arena_->IsTag(operand, ::cppgm::syntax::STAG_GNU_ASM_GOTO_LABEL))
			throw std::runtime_error("GNU asm goto is unsupported");
		const bool output = arena_->IsTag(operand, ::cppgm::syntax::STAG_GNU_ASM_OUTPUT);
		if (!output && !arena_->IsTag(operand, ::cppgm::syntax::STAG_GNU_ASM_INPUT))
			throw std::logic_error("invalid GNU asm syntax child");
		const std::string constraint = DecodeAsmString(
			*arena_, operand, "invalid GNU asm constraint");
		if (constraint.empty() || (output && constraint[0] != '+' &&
			constraint[0] != '='))
			throw std::runtime_error("invalid GNU asm operand constraint");
		const NodeId expression = AsmOperandExpression(*arena_, operand);
		if (expression == kNoNode)
			throw std::logic_error("GNU asm operand has no expression");
		ExpressionInfo value = AnalyzeExpression(expression, scope);
		if (output && !IsModifiableLvalue(value))
			throw std::runtime_error("GNU asm output is not a modifiable lvalue");
		dump_.Add(statement, value.node);
		if (output) ++outputs; else ++inputs;
	}
	if ((operation == GNU_ASM_BSWAP || operation == GNU_ASM_LOCK_NOT ||
		 operation == GNU_ASM_LOCK_INCREMENT) && outputs != 1)
		throw std::runtime_error("GNU asm operation requires one output");
	if ((operation == GNU_ASM_NOP || operation == GNU_ASM_PAUSE ||
		 operation == GNU_ASM_COMPILER_FENCE) && outputs != 0)
		throw std::runtime_error("GNU asm operation has unexpected output");
	if (inputs != 0)
		throw std::runtime_error("unsupported GNU asm input operands");
	if (outputs != 0)
	{
		const DumpNode& value = dump_.nodes[
			dump_.edges[dump_.nodes[statement].first_edge].child];
		if (!IsIntegral(value.type, true))
			throw std::runtime_error("GNU asm output requires integral type");
	}
	dump_.nodes[statement].array_count = outputs;
	dump_.nodes[statement].storage_size = inputs;
	return true;
}

}
}
