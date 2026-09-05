#include "llvm_ir_model.h"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace cppgm
{
namespace llvm_ir
{

Type Type::Array(std::uint64_t count_value, const Type& element)
{
	Type result(ARRAY);
	result.count = count_value;
	result.elements.push_back(element);
	return result;
}

Type Type::Structure(const std::vector<Type>& elements_value,
	bool packed_value)
{
	Type result(STRUCTURE);
	result.elements = elements_value;
	result.packed = packed_value;
	return result;
}

bool operator==(const Type& left, const Type& right)
{
	return left.kind == right.kind && left.count == right.count &&
		left.elements == right.elements && left.packed == right.packed;
}

bool operator!=(const Type& left, const Type& right)
{
	return !(left == right);
}

std::string RenderType(const Type& type)
{
	switch (type.kind)
	{
	case Type::VOID: return "void";
	case Type::I1: return "i1";
	case Type::I8: return "i8";
	case Type::I16: return "i16";
	case Type::I32: return "i32";
	case Type::I64: return "i64";
	case Type::I128: return "i128";
	case Type::HALF: return "half";
	case Type::FLOAT: return "float";
	case Type::DOUBLE: return "double";
	case Type::X86_FP80: return "x86_fp80";
	case Type::FP128: return "fp128";
	case Type::POINTER: return "ptr";
	case Type::ARRAY:
		if (type.elements.size() != 1)
			throw std::logic_error("LLVM array type has no element type");
		return "[" + std::to_string(type.count) + " x " +
			RenderType(type.elements[0]) + "]";
	case Type::STRUCTURE:
	{
		std::string result = type.packed ? "<{ " : "{ ";
		for (std::size_t i = 0; i < type.elements.size(); ++i)
		{
			if (i != 0) result += ", ";
			result += RenderType(type.elements[i]);
		}
		result += type.packed ? " }>" : " }";
		return result;
	}
	}
	throw std::logic_error("invalid LLVM type kind");
}

Operand Operand::Local(const Type& type, const std::string& name)
{
	return Operand(LOCAL, type, name);
}

Operand Operand::Global(const Type& type, const std::string& name)
{
	return Operand(GLOBAL, type, name);
}

Operand Operand::Integer(const Type& type, const std::string& value)
{
	return Operand(INTEGER, type, value);
}

Operand Operand::Floating(const Type& type, const std::string& value)
{
	return Operand(FLOATING, type, value);
}

Operand Operand::Null(const Type& type)
{
	return Operand(NULL_POINTER, type);
}

Operand Operand::Aggregate(const Type& type,
	const std::vector<Operand>& elements)
{
	Operand result(type.kind == Type::ARRAY ? ARRAY_CONSTANT :
		STRUCTURE_CONSTANT, type);
	result.elements = elements;
	return result;
}

Operand Operand::GetElementPtr(const Type& source_type,
	const Operand& base, const std::vector<Operand>& indices)
{
	Operand result(GETELEMENTPTR_CONSTANT, Type(Type::POINTER));
	result.source_type = source_type;
	result.elements.push_back(base);
	result.elements.insert(result.elements.end(), indices.begin(), indices.end());
	return result;
}

namespace
{

bool IsIdentifierStart(char value)
{
	return std::isalpha(static_cast<unsigned char>(value)) || value == '-' ||
		value == '$' || value == '.' || value == '_';
}

bool IsIdentifierContinue(char value)
{
	return IsIdentifierStart(value) ||
		std::isdigit(static_cast<unsigned char>(value));
}

std::string EscapeIdentifier(const std::string& value, char prefix)
{
	bool simple = !value.empty() && IsIdentifierStart(value[0]);
	for (std::size_t i = 1; simple && i < value.size(); ++i)
		simple = IsIdentifierContinue(value[i]);
	if (simple) return std::string(1, prefix) + value;
	static const char digits[] = "0123456789ABCDEF";
	std::string result;
	result += prefix;
	result += '"';
	for (std::size_t i = 0; i < value.size(); ++i)
	{
		const unsigned char byte = static_cast<unsigned char>(value[i]);
		if (byte == '"' || byte == '\\' || byte < 0x20 || byte >= 0x7f)
		{
			result += '\\';
			result += digits[byte >> 4];
			result += digits[byte & 15];
		}
		else result += static_cast<char>(byte);
	}
	result += '"';
	return result;
}

std::string RenderLocal(const std::string& value)
{
	return EscapeIdentifier(value, '%');
}

std::string RenderGlobal(const std::string& value)
{
	return EscapeIdentifier(value, '@');
}

std::string EscapeString(const std::string& value)
{
	static const char digits[] = "0123456789ABCDEF";
	std::string result = "\"";
	for (std::size_t i = 0; i < value.size(); ++i)
	{
		const unsigned char byte = static_cast<unsigned char>(value[i]);
		if (byte == '"' || byte == '\\' || byte < 0x20 || byte >= 0x7f)
		{
			result += '\\';
			result += digits[byte >> 4];
			result += digits[byte & 15];
		}
		else result += static_cast<char>(byte);
	}
	result += '"';
	return result;
}

std::string RenderTypedOperand(const Operand& operand);

std::string RenderOperandValue(const Operand& operand)
{
	switch (operand.kind)
	{
	case Operand::LOCAL: return RenderLocal(operand.text);
	case Operand::GLOBAL: return RenderGlobal(operand.text);
	case Operand::INTEGER: case Operand::FLOATING: return operand.text;
	case Operand::NULL_POINTER: return "null";
	case Operand::UNDEF: return "undef";
	case Operand::POISON: return "poison";
	case Operand::ZERO_INITIALIZER: return "zeroinitializer";
	case Operand::ARRAY_CONSTANT: case Operand::STRUCTURE_CONSTANT:
	{
		const char open = operand.kind == Operand::ARRAY_CONSTANT ? '[' : '{';
		const char close = operand.kind == Operand::ARRAY_CONSTANT ? ']' : '}';
		std::string result(1, open);
		for (std::size_t i = 0; i < operand.elements.size(); ++i)
		{
			if (i != 0) result += ", ";
			result += RenderTypedOperand(operand.elements[i]);
		}
		result += close;
		return result;
	}
	case Operand::GETELEMENTPTR_CONSTANT:
	{
		if (operand.elements.empty())
			throw std::logic_error("LLVM constant GEP has no base");
		std::string result = "getelementptr (" + RenderType(operand.source_type) +
			", " + RenderTypedOperand(operand.elements[0]);
		for (std::size_t i = 1; i < operand.elements.size(); ++i)
			result += ", " + RenderTypedOperand(operand.elements[i]);
		result += ')';
		return result;
	}
	}
	throw std::logic_error("invalid LLVM operand kind");
}

std::string RenderTypedOperand(const Operand& operand)
{
	return RenderType(operand.type) + " " + RenderOperandValue(operand);
}

const char* RenderLinkage(Linkage linkage)
{
	switch (linkage)
	{
	case Linkage::EXTERNAL: return "";
	case Linkage::INTERNAL: return "internal ";
	case Linkage::PRIVATE: return "private ";
	case Linkage::WEAK: return "weak ";
	case Linkage::WEAK_ODR: return "weak_odr ";
	case Linkage::LINKONCE_ODR: return "linkonce_odr ";
	case Linkage::EXTERNAL_WEAK: return "extern_weak ";
	case Linkage::COMMON: return "common ";
	case Linkage::AVAILABLE_EXTERNALLY: return "available_externally ";
	}
	throw std::logic_error("invalid LLVM linkage");
}

const char* RenderBinaryOperation(Instruction::BinaryOperation operation)
{
	switch (operation)
	{
	case Instruction::ADD: return "add";
	case Instruction::SUB: return "sub";
	case Instruction::MUL: return "mul";
	case Instruction::SDIV: return "sdiv";
	case Instruction::UDIV: return "udiv";
	case Instruction::SREM: return "srem";
	case Instruction::UREM: return "urem";
	case Instruction::SHL: return "shl";
	case Instruction::LSHR: return "lshr";
	case Instruction::ASHR: return "ashr";
	case Instruction::AND: return "and";
	case Instruction::OR: return "or";
	case Instruction::XOR: return "xor";
	case Instruction::FADD: return "fadd";
	case Instruction::FSUB: return "fsub";
	case Instruction::FMUL: return "fmul";
	case Instruction::FDIV: return "fdiv";
	case Instruction::FREM: return "frem";
	case Instruction::BINARY_NONE: break;
	}
	throw std::logic_error("LLVM binary instruction has no operation");
}

const char* RenderPredicate(Instruction::Predicate predicate)
{
	switch (predicate)
	{
	case Instruction::EQ: return "eq";
	case Instruction::NE: return "ne";
	case Instruction::SLT: return "slt";
	case Instruction::SLE: return "sle";
	case Instruction::SGT: return "sgt";
	case Instruction::SGE: return "sge";
	case Instruction::ULT: return "ult";
	case Instruction::ULE: return "ule";
	case Instruction::UGT: return "ugt";
	case Instruction::UGE: return "uge";
	case Instruction::FOEQ: return "oeq";
	case Instruction::FONE: return "one";
	case Instruction::FOLT: return "olt";
	case Instruction::FOLE: return "ole";
	case Instruction::FOGT: return "ogt";
	case Instruction::FOGE: return "oge";
	case Instruction::FUEQ: return "ueq";
	case Instruction::FUNE: return "une";
	case Instruction::PREDICATE_NONE: break;
	}
	throw std::logic_error("LLVM comparison has no predicate");
}

const char* RenderCastOperation(Instruction::CastOperation operation)
{
	switch (operation)
	{
	case Instruction::TRUNC: return "trunc";
	case Instruction::ZEXT: return "zext";
	case Instruction::SEXT: return "sext";
	case Instruction::FPTRUNC: return "fptrunc";
	case Instruction::FPEXT: return "fpext";
	case Instruction::FPTOUI: return "fptoui";
	case Instruction::FPTOSI: return "fptosi";
	case Instruction::UITOFP: return "uitofp";
	case Instruction::SITOFP: return "sitofp";
	case Instruction::PTRTOINT: return "ptrtoint";
	case Instruction::INTTOPTR: return "inttoptr";
	case Instruction::BITCAST: return "bitcast";
	case Instruction::CAST_NONE: break;
	}
	throw std::logic_error("LLVM cast has no operation");
}

std::string RenderAttributes(const std::vector<std::string>& attributes)
{
	std::string result;
	for (std::size_t i = 0; i < attributes.size(); ++i)
	{
		result += ' ';
		result += attributes[i];
	}
	return result;
}

void RenderInstruction(std::ostream& output, const Instruction& instruction)
{
	if (!instruction.result.empty()) output << RenderLocal(instruction.result) << " = ";
	switch (instruction.kind)
	{
	case Instruction::ALLOCA:
		output << "alloca " << RenderType(instruction.type);
		if (instruction.alignment != 0)
			output << ", align " << instruction.alignment;
		break;
	case Instruction::LOAD:
		output << "load " << RenderType(instruction.type) << ", "
			<< RenderTypedOperand(instruction.first);
		if (instruction.alignment != 0)
			output << ", align " << instruction.alignment;
		break;
	case Instruction::STORE:
		output << "store " << RenderTypedOperand(instruction.first) << ", "
			<< RenderTypedOperand(instruction.second);
		if (instruction.alignment != 0)
			output << ", align " << instruction.alignment;
		break;
	case Instruction::BINARY:
		output << RenderBinaryOperation(instruction.binary_operation) << ' '
			<< RenderType(instruction.type) << ' '
			<< RenderOperandValue(instruction.first) << ", "
			<< RenderOperandValue(instruction.second);
		break;
	case Instruction::ICMP: case Instruction::FCMP:
		output << (instruction.kind == Instruction::ICMP ? "icmp " : "fcmp ")
			<< RenderPredicate(instruction.predicate) << ' '
			<< RenderType(instruction.type) << ' '
			<< RenderOperandValue(instruction.first) << ", "
			<< RenderOperandValue(instruction.second);
		break;
	case Instruction::CAST:
		output << RenderCastOperation(instruction.cast_operation) << ' '
			<< RenderTypedOperand(instruction.first) << " to "
			<< RenderType(instruction.type);
		break;
	case Instruction::GETELEMENTPTR:
		output << "getelementptr ";
		if (instruction.inbounds) output << "inbounds ";
		output << RenderType(instruction.source_type) << ", "
			<< RenderTypedOperand(instruction.first);
		for (std::size_t i = 0; i < instruction.operands.size(); ++i)
			output << ", " << RenderTypedOperand(instruction.operands[i]);
		break;
	case Instruction::CALL:
		if (instruction.tail_call) output << "tail ";
		output << "call" << RenderAttributes(instruction.return_attributes)
			<< ' ' << RenderType(instruction.type) << ' ';
		if (instruction.indirect_call)
			output << RenderOperandValue(instruction.first);
		else output << RenderGlobal(instruction.callee);
		output << '(';
		for (std::size_t i = 0; i < instruction.operands.size(); ++i)
		{
			if (i != 0) output << ", ";
			output << RenderType(instruction.operands[i].type);
			if (i < instruction.argument_attributes.size())
				output << RenderAttributes(instruction.argument_attributes[i]);
			output << ' ' << RenderOperandValue(instruction.operands[i]);
		}
		output << ')';
		break;
	case Instruction::PHI:
		if (instruction.operands.size() != instruction.labels.size())
			throw std::logic_error("LLVM phi operand/label count mismatch");
		output << "phi " << RenderType(instruction.type) << ' ';
		for (std::size_t i = 0; i < instruction.operands.size(); ++i)
		{
			if (i != 0) output << ", ";
			output << "[ " << RenderOperandValue(instruction.operands[i])
				<< ", " << RenderLocal(instruction.labels[i]) << " ]";
		}
		break;
	case Instruction::BRANCH:
		output << "br label " << RenderLocal(instruction.target);
		break;
	case Instruction::CONDITIONAL_BRANCH:
		output << "br " << RenderTypedOperand(instruction.first) << ", label "
			<< RenderLocal(instruction.target) << ", label "
			<< RenderLocal(instruction.alternate);
		break;
	case Instruction::SWITCH:
		if (instruction.operands.size() != instruction.labels.size())
			throw std::logic_error("LLVM switch operand/label count mismatch");
		output << "switch " << RenderTypedOperand(instruction.first) << ", label "
			<< RenderLocal(instruction.target) << " [";
		for (std::size_t i = 0; i < instruction.operands.size(); ++i)
			output << "\n      " << RenderTypedOperand(instruction.operands[i])
				<< ", label " << RenderLocal(instruction.labels[i]);
		if (!instruction.operands.empty()) output << '\n' << "    ";
		output << ']';
		break;
	case Instruction::RETURN:
		output << "ret ";
		if (instruction.type.kind == Type::VOID) output << "void";
		else output << RenderTypedOperand(instruction.first);
		break;
	case Instruction::UNREACHABLE:
		output << "unreachable";
		break;
	}
}

void ValidateFunction(const Function& function)
{
	if (function.name.empty())
		throw std::logic_error("LLVM function has no name");
	if (function.declaration)
	{
		if (!function.blocks.empty())
			throw std::logic_error("LLVM declaration has basic blocks");
		return;
	}
	if (function.blocks.empty())
		throw std::logic_error("LLVM definition has no basic blocks");
	std::unordered_set<std::string> block_names;
	std::unordered_set<std::string> results;
	for (std::size_t i = 0; i < function.parameters.size(); ++i)
	{
		if (function.parameters[i].name.empty()) continue;
		if (!results.insert(function.parameters[i].name).second)
			throw std::logic_error("duplicate LLVM parameter name");
	}
	for (std::size_t i = 0; i < function.blocks.size(); ++i)
	{
		const Block& block = function.blocks[i];
		if (block.name.empty() || !block_names.insert(block.name).second)
			throw std::logic_error("invalid or duplicate LLVM block name");
		if (block.instructions.empty() ||
			!IsTerminator(block.instructions.back()))
			throw std::logic_error("LLVM block has no terminator");
		bool saw_non_phi = false;
		for (std::size_t j = 0; j < block.instructions.size(); ++j)
		{
			const Instruction& instruction = block.instructions[j];
			if (j + 1 != block.instructions.size() && IsTerminator(instruction))
				throw std::logic_error("LLVM terminator is not last in its block");
			if (instruction.kind == Instruction::PHI && saw_non_phi)
				throw std::logic_error("LLVM phi follows a non-phi instruction");
			if (instruction.kind != Instruction::PHI) saw_non_phi = true;
			if (instruction.kind == Instruction::CALL &&
				!instruction.argument_attributes.empty() &&
				instruction.argument_attributes.size() !=
					instruction.operands.size())
				throw std::logic_error(
					"LLVM call argument attribute count mismatch");
			if (!instruction.result.empty() &&
				!results.insert(instruction.result).second)
				throw std::logic_error("duplicate LLVM SSA result name");
		}
	}
}

}

bool IsTerminator(const Instruction& instruction)
{
	return instruction.kind == Instruction::BRANCH ||
		instruction.kind == Instruction::CONDITIONAL_BRANCH ||
		instruction.kind == Instruction::SWITCH ||
		instruction.kind == Instruction::RETURN ||
		instruction.kind == Instruction::UNREACHABLE;
}

void ValidateModule(const Module& module)
{
	if (module.target_data_layout.empty() || module.target_triple.empty())
		throw std::logic_error("LLVM module has no target layout/triple");
	std::unordered_set<std::string> symbols;
	for (std::size_t i = 0; i < module.globals.size(); ++i)
	{
		if (module.globals[i].name.empty())
			throw std::logic_error("LLVM global has no name");
		if (!symbols.insert(module.globals[i].name).second)
			throw std::logic_error("duplicate LLVM module symbol");
	}
	for (std::size_t i = 0; i < module.functions.size(); ++i)
	{
		if (!symbols.insert(module.functions[i].name).second)
			throw std::logic_error("duplicate LLVM module symbol");
		ValidateFunction(module.functions[i]);
	}
}

std::string SerializeModule(const Module& module)
{
	ValidateModule(module);
	std::ostringstream output;
	if (!module.source_filename.empty())
		output << "source_filename = " << EscapeString(module.source_filename) << '\n';
	output << "target datalayout = " << EscapeString(module.target_data_layout) << '\n';
	output << "target triple = " << EscapeString(module.target_triple) << "\n\n";
	for (std::size_t i = 0; i < module.globals.size(); ++i)
	{
		const Global& global = module.globals[i];
		output << RenderGlobal(global.name) << " = ";
		if (global.declaration) output << "external ";
		else output << RenderLinkage(global.linkage);
		if (global.dso_local) output << "dso_local ";
		if (global.unnamed_address) output << "unnamed_addr ";
		output << (global.constant ? "constant " : "global ")
			<< RenderType(global.type);
		if (!global.declaration)
			output << ' ' << RenderOperandValue(global.initializer);
		if (global.alignment != 0) output << ", align " << global.alignment;
		output << '\n';
	}
	if (!module.globals.empty()) output << '\n';
	for (std::size_t i = 0; i < module.functions.size(); ++i)
	{
		const Function& function = module.functions[i];
		output << (function.declaration ? "declare " : "define ")
			<< RenderLinkage(function.linkage);
		if (function.dso_local) output << "dso_local ";
		for (std::size_t j = 0; j < function.return_attributes.size(); ++j)
			output << function.return_attributes[j] << ' ';
		output << RenderType(function.result) << ' ' << RenderGlobal(function.name)
			<< '(';
		for (std::size_t j = 0; j < function.parameters.size(); ++j)
		{
			if (j != 0) output << ", ";
			const Parameter& parameter = function.parameters[j];
			output << RenderType(parameter.type)
				<< RenderAttributes(parameter.attributes);
			if (!parameter.name.empty()) output << ' ' << RenderLocal(parameter.name);
		}
		if (function.variadic)
		{
			if (!function.parameters.empty()) output << ", ";
			output << "...";
		}
		output << ')' << RenderAttributes(function.function_attributes);
		if (function.alignment != 0) output << " align " << function.alignment;
		if (function.declaration)
		{
			output << "\n\n";
			continue;
		}
		output << " {\n";
		for (std::size_t j = 0; j < function.blocks.size(); ++j)
		{
			const Block& block = function.blocks[j];
			output << block.name << ":\n";
			for (std::size_t k = 0; k < block.instructions.size(); ++k)
			{
				output << "  ";
				RenderInstruction(output, block.instructions[k]);
				output << '\n';
			}
		}
		output << "}\n\n";
	}
	return output.str();
}

}
}
