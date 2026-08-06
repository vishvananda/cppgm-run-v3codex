#include "pa15_lowering.h"

#include "abi_mangle.h"
#include "pa11_model.h"
#include "pa12_semantic.h"
#include "pa12_semantic_model.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace cppgm
{
namespace
{

using namespace pa11;
using namespace pa12_semantic_detail;

enum LowKind
{
	LOW_INVALID,
	LOW_VOID,
	LOW_I8,
	LOW_U8,
	LOW_I16,
	LOW_U16,
	LOW_I32,
	LOW_U32,
	LOW_I64,
	LOW_F32,
	LOW_F64,
	LOW_F80,
	LOW_PTR
};

struct LowType
{
	LowKind kind;
	const char* text;
	std::size_t width;
	bool is_signed;

	LowType() : kind(LOW_INVALID), text("<invalid>"), width(0),
		is_signed(false) {}
	LowType(LowKind kind_value, const char* text_value,
		std::size_t width_value, bool signed_value)
		: kind(kind_value), text(text_value), width(width_value),
		  is_signed(signed_value) {}
};

LowType LowVoid() { return LowType(LOW_VOID, "void", 0, false); }
LowType LowI8() { return LowType(LOW_I8, "i8", 8, true); }
LowType LowU8() { return LowType(LOW_U8, "u8", 8, false); }
LowType LowI16() { return LowType(LOW_I16, "i16", 16, true); }
LowType LowU16() { return LowType(LOW_U16, "u16", 16, false); }
LowType LowI32() { return LowType(LOW_I32, "i32", 32, true); }
LowType LowU32() { return LowType(LOW_U32, "u32", 32, false); }
LowType LowI64() { return LowType(LOW_I64, "i64", 64, true); }
LowType LowU64() { return LowType(LOW_I64, "i64", 64, false); }
LowType LowF32() { return LowType(LOW_F32, "f32", 32, true); }
LowType LowF64() { return LowType(LOW_F64, "f64", 64, true); }
LowType LowF80() { return LowType(LOW_F80, "f80", 80, true); }
LowType LowPtr() { return LowType(LOW_PTR, "ptr", 64, false); }

bool SameType(const LowType& left, const LowType& right)
{
	return left.kind == right.kind;
}

bool IsInteger(const LowType& type)
{
	return type.kind == LOW_I8 || type.kind == LOW_U8 ||
		type.kind == LOW_I16 || type.kind == LOW_U16 ||
		type.kind == LOW_I32 || type.kind == LOW_U32 ||
		type.kind == LOW_I64;
}

bool IsFloating(const LowType& type)
{
	return type.kind == LOW_F32 || type.kind == LOW_F64 ||
		type.kind == LOW_F80;
}

struct Operand
{
	enum Kind { NONE, TEMP, SLOT, GLOBAL, INTEGER, FLOATING } kind;
	std::string text;
	LowType type;

	Operand() : kind(NONE) {}
	Operand(Kind kind_value, const std::string& text_value,
		const LowType& type_value)
		: kind(kind_value), text(text_value), type(type_value) {}
};

struct Instruction
{
	enum Kind
	{
		ADDR,
		LOAD,
		STORE,
		UNARY,
		BINARY,
		CMP,
		CONVERT,
		CALL,
		JUMP,
		BRANCH,
		RETURN_VALUE,
		RETURN_VOID
	} kind;
	std::string dest;
	std::string op;
	LowType type;
	LowType source_type;
	Operand first;
	Operand second;
	std::vector<Operand> arguments;
	std::string target;
	std::string alternate;
	bool indirect;

	explicit Instruction(Kind kind_value) : kind(kind_value), indirect(false) {}
};

bool IsTerminator(const Instruction& instruction)
{
	return instruction.kind == Instruction::JUMP ||
		instruction.kind == Instruction::BRANCH ||
		instruction.kind == Instruction::RETURN_VALUE ||
		instruction.kind == Instruction::RETURN_VOID;
}

struct Block
{
	std::string label;
	std::vector<Instruction> instructions;
	bool terminated;

	explicit Block(const std::string& label_value)
		: label(label_value), terminated(false) {}
};

struct Parameter
{
	std::string name;
	LowType type;
	bool reference;
};

struct Slot
{
	std::string name;
	LowType type;
};

struct Function
{
	std::string name;
	std::string object_name;
	LowType result;
	std::vector<Parameter> parameters;
	std::vector<Slot> slots;
	std::vector<Block> blocks;
	bool entry;
	bool variadic;

	Function() : entry(false), variadic(false) {}
};

struct FunctionDeclaration
{
	std::string name;
	std::string object_name;
	LowType result;
	std::vector<Parameter> parameters;
	bool variadic;

	FunctionDeclaration() : variadic(false) {}
};

struct Global
{
	std::string name;
	std::string object_name;
	LowType type;
	std::string initializer;
};

struct TypedProgram
{
	std::vector<FunctionDeclaration> declarations;
	std::vector<Global> globals;
	std::vector<Function> functions;
};

std::string StripOperationPrefix(const std::string& operation)
{
	const std::size_t colon = operation.rfind(':');
	return colon == std::string::npos ? operation : operation.substr(colon + 1);
}

std::string SanitizeSymbol(const std::string& name)
{
	std::string result;
	result.reserve(name.size() + 8);
	for (std::size_t i = 0; i < name.size(); ++i)
	{
		if (i + 1 < name.size() && name[i] == ':' && name[i + 1] == ':')
		{
			result += "__";
			++i;
		}
		else
		{
			const unsigned char c = static_cast<unsigned char>(name[i]);
			result += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '_' ? static_cast<char>(c) : '_';
		}
	}
	if (result.empty()) result = "anonymous";
	return result;
}

std::int64_t ParseIntegerSpelling(const std::string& spelling)
{
	const std::size_t quote = spelling.find('\'');
	if (quote != std::string::npos)
	{
		const std::size_t close = spelling.rfind('\'');
		if (close <= quote + 1) throw std::runtime_error("invalid character literal");
		unsigned long long value = 0;
		for (std::size_t i = quote + 1; i < close; ++i)
		{
			unsigned int character = static_cast<unsigned char>(spelling[i]);
			if (spelling[i] == '\\')
			{
				if (++i >= close) throw std::runtime_error("invalid character escape");
				const char escaped = spelling[i];
				if (escaped == 'a') character = 7;
				else if (escaped == 'b') character = 8;
				else if (escaped == 'f') character = 12;
				else if (escaped == 'n') character = 10;
				else if (escaped == 'r') character = 13;
				else if (escaped == 't') character = 9;
				else if (escaped == 'v') character = 11;
				else if (escaped == 'x')
				{
					character = 0;
					std::size_t digits = 0;
					while (i + 1 < close)
					{
						const char digit = spelling[i + 1];
						const int nibble = digit >= '0' && digit <= '9' ? digit - '0' :
							digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 :
							digit >= 'A' && digit <= 'F' ? digit - 'A' + 10 : -1;
						if (nibble < 0) break;
						character = (character << 4) | static_cast<unsigned int>(nibble);
						++i;
						++digits;
					}
					if (digits == 0) throw std::runtime_error("empty hexadecimal escape");
				}
				else if (escaped >= '0' && escaped <= '7')
				{
					character = static_cast<unsigned int>(escaped - '0');
					for (std::size_t digits = 1; digits < 3 && i + 1 < close &&
						spelling[i + 1] >= '0' && spelling[i + 1] <= '7'; ++digits)
						character = (character << 3) |
							static_cast<unsigned int>(spelling[++i] - '0');
				}
				else character = static_cast<unsigned char>(escaped);
			}
			value = (value << 8) | (character & 0xffU);
		}
		return static_cast<std::int64_t>(value);
	}
	std::size_t last = spelling.size();
	while (last != 0 && (spelling[last - 1] == 'u' || spelling[last - 1] == 'U' ||
		spelling[last - 1] == 'l' || spelling[last - 1] == 'L')) --last;
	const std::string digits = spelling.substr(0, last);
	errno = 0;
	char* end = 0;
	const unsigned long long value = std::strtoull(digits.c_str(), &end, 0);
	if (errno == ERANGE || end == digits.c_str() || *end != '\0' ||
		value > static_cast<unsigned long long>(INT64_MAX))
		throw std::runtime_error("integer literal outside PA15 range");
	return static_cast<std::int64_t>(value);
}

std::string CanonicalLiteral(const std::string& spelling, const LowType& type)
{
	if (spelling == "true") return "1";
	if (spelling == "false" || spelling == "nullptr") return "0";
	if (IsFloating(type)) return spelling;
	return std::to_string(ParseIntegerSpelling(spelling));
}

class GraphLowerer
{
public:
	GraphLowerer(const SemanticGraphView& graph, TypedProgram& output,
		LowIRLoweringStats* stats)
		: graph_(graph), program_(graph.program), arena_(graph.arena),
		  output_(output), stats_(stats), function_(0), current_block_(0),
		  current_result_(LowVoid()), temp_counter_(0), block_counter_(0)
	{
		function_symbols_.resize(program_.bindings.size());
		global_symbols_.resize(program_.bindings.size());
		function_definition_.resize(program_.bindings.size(), kNoDumpEdge);
		function_declaration_.resize(program_.bindings.size(), kNoDumpEdge);
		global_node_.resize(program_.bindings.size(), kNoDumpEdge);
	}

	void Lower()
	{
		ScanTop(graph_.root);
		EmitTop(graph_.root);
	}

private:
	std::vector<std::uint32_t> Children(std::uint32_t node) const
	{
		std::vector<std::uint32_t> result;
		for (std::uint32_t edge = arena_.nodes[node].first_edge;
			edge != kNoDumpEdge; edge = arena_.edges[edge].next)
			result.push_back(arena_.edges[edge].child);
		return result;
	}

	LowType LowerType(TypeId type) const
	{
		const TypeRecord* record = &program_.types.Get(type);
		while (record->kind == TYPE_QUALIFIED)
		{
			type = record->child;
			record = &program_.types.Get(type);
		}
		if (record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE || record->kind == TYPE_POINTER ||
			record->kind == TYPE_ARRAY || record->kind == TYPE_FUNCTION ||
			record->kind == TYPE_MEMBER_POINTER) return LowPtr();
		if (record->kind == TYPE_NAMED)
		{
			const EntityRecord& entity = program_.entities[record->entity];
			if ((entity.flavor == NAMED_ENUM || entity.flavor == NAMED_ENUM_CLASS) &&
				entity.underlying != kNoType) return LowerType(entity.underlying);
			throw std::runtime_error("PA15 scalar checkpoint cannot lower class type");
		}
		if (record->kind != TYPE_FUNDAMENTAL)
			throw std::runtime_error("invalid PA15 scalar type");
		switch (record->fundamental)
		{
		case FUND_BOOL: return LowU8();
		case FUND_CHAR: case FUND_SIGNED_CHAR: return LowI8();
		case FUND_UNSIGNED_CHAR: return LowU8();
		case FUND_SHORT_INT: return LowI16();
		case FUND_UNSIGNED_SHORT_INT: return LowU16();
		case FUND_INT: return LowI32();
		case FUND_UNSIGNED_INT: return LowU32();
		case FUND_LONG_INT: case FUND_LONG_LONG_INT:
		case FUND_WCHAR_T: case FUND_CHAR16_T: case FUND_CHAR32_T:
			return LowI64();
		case FUND_UNSIGNED_LONG_INT: case FUND_UNSIGNED_LONG_LONG_INT:
			return LowU64();
		case FUND_FLOAT: return LowF32();
		case FUND_DOUBLE: return LowF64();
		case FUND_LONG_DOUBLE: return LowF80();
		case FUND_VOID: return LowVoid();
		case FUND_NULLPTR_T: return LowPtr();
		}
		throw std::runtime_error("unsupported PA15 fundamental type");
	}

	bool IsReferenceType(TypeId type) const
	{
		const TypeRecord& record = program_.types.Get(type);
		return record.kind == TYPE_LVALUE_REFERENCE ||
			record.kind == TYPE_RVALUE_REFERENCE;
	}

	abi_mangle::AbiType MakeAbiType(TypeId type) const
	{
		using namespace abi_mangle;
		const TypeRecord& record = program_.types.Get(type);
		abi_mangle::AbiType result;
		if (record.kind == TYPE_QUALIFIED)
		{
			result.kind = ABI_TYPE_CV;
			result.is_const = (record.cv & CV_CONST) != 0;
			result.is_volatile = (record.cv & CV_VOLATILE) != 0;
			result.types.push_back(MakeAbiType(record.child));
			return result;
		}
		if (record.kind == TYPE_POINTER || record.kind == TYPE_LVALUE_REFERENCE ||
			record.kind == TYPE_RVALUE_REFERENCE)
		{
			result.kind = record.kind == TYPE_POINTER ? ABI_TYPE_POINTER :
				record.kind == TYPE_LVALUE_REFERENCE ? ABI_TYPE_LVALUE_REFERENCE :
				ABI_TYPE_RVALUE_REFERENCE;
			result.types.push_back(MakeAbiType(record.child));
			return result;
		}
		if (record.kind == TYPE_ARRAY)
		{
			result.kind = ABI_TYPE_ARRAY;
			result.array_bound.kind = ABI_ARRAY_BOUND_VALUE;
			result.array_bound.value = std::to_string(record.bound);
			result.types.push_back(MakeAbiType(record.child));
			return result;
		}
		if (record.kind == TYPE_FUNCTION)
		{
			result.kind = ABI_TYPE_FUNCTION;
			result.types.push_back(MakeAbiType(record.child));
			const TypeId* parameters = program_.types.Parameters(type);
			for (std::size_t i = 0; i < record.parameter_count; ++i)
				result.types.push_back(MakeAbiType(parameters[i]));
			result.variadic = record.variadic;
			return result;
		}
		if (record.kind == TYPE_NAMED)
		{
			result.kind = ABI_TYPE_NAMED;
			result.name = program_.names.Get(program_.entities[record.entity].name);
			return result;
		}
		if (record.kind != TYPE_FUNDAMENTAL)
			throw std::runtime_error("unsupported ABI type in PA15");
		result.kind = ABI_TYPE_BUILTIN;
		switch (record.fundamental)
		{
		case FUND_VOID: result.name = "void"; break;
		case FUND_BOOL: result.name = "bool"; break;
		case FUND_CHAR: result.name = "char"; break;
		case FUND_SIGNED_CHAR: result.name = "schar"; break;
		case FUND_UNSIGNED_CHAR: result.name = "uchar"; break;
		case FUND_SHORT_INT: result.name = "short"; break;
		case FUND_UNSIGNED_SHORT_INT: result.name = "ushort"; break;
		case FUND_INT: result.name = "int"; break;
		case FUND_UNSIGNED_INT: result.name = "uint"; break;
		case FUND_LONG_INT: result.name = "long"; break;
		case FUND_UNSIGNED_LONG_INT: result.name = "ulong"; break;
		case FUND_LONG_LONG_INT: result.name = "longlong"; break;
		case FUND_UNSIGNED_LONG_LONG_INT: result.name = "ulonglong"; break;
		case FUND_FLOAT: result.name = "float"; break;
		case FUND_DOUBLE: result.name = "double"; break;
		case FUND_LONG_DOUBLE: result.name = "longdouble"; break;
		case FUND_WCHAR_T: result.name = "wchar"; break;
		case FUND_CHAR16_T: result.name = "char16"; break;
		case FUND_CHAR32_T: result.name = "char32"; break;
		case FUND_NULLPTR_T: result.name = "ulong"; break;
		}
		return result;
	}

	std::string MangleFunction(const DumpNode& node) const
	{
		using namespace abi_mangle;
		const std::string qualified = program_.names.Get(node.text);
		if (qualified == "main") return std::string();
		AbiFactFile file;
		file.cases.push_back(AbiFactCase());
		AbiFactRecord target;
		target.set_kind(ABI_FACT_RECORD_TARGET);
		target.target.kind = ABI_TARGET_FACT_FUNCTION;
		target.target.function.kind = ABI_FUNCTION_TARGET_PATH;
		target.target.function.qualified_name = qualified;
		file.cases[0].records.push_back(target);
		const TypeRecord& function_type = program_.types.Get(node.type);
		const TypeId* parameters = program_.types.Parameters(node.type);
		for (std::size_t i = 0; i < function_type.parameter_count; ++i)
		{
			AbiFactRecord parameter;
			parameter.set_kind(ABI_FACT_RECORD_FUNCTION);
			parameter.function.kind = ABI_FUNCTION_RECORD_PARAMETER;
			parameter.function.type = MakeAbiType(parameters[i]);
			file.cases[0].records.push_back(parameter);
		}
		if (function_type.variadic)
		{
			AbiFactRecord variadic;
			variadic.set_kind(ABI_FACT_RECORD_FUNCTION);
			variadic.function.kind = ABI_FUNCTION_RECORD_VARIADIC;
			file.cases[0].records.push_back(variadic);
		}
		std::string result = mangle_fact_file(file);
		if (!result.empty() && result[result.size() - 1] == '\n') result.resize(result.size() - 1);
		return result;
	}

	std::string MangleVariable(const DumpNode& node) const
	{
		using namespace abi_mangle;
		AbiFactFile file;
		file.cases.push_back(AbiFactCase());
		AbiFactRecord target;
		target.set_kind(ABI_FACT_RECORD_TARGET);
		target.target.kind = ABI_TARGET_FACT_VARIABLE;
		const BindingRecord& binding = program_.bindings[node.binding];
		target.target.qualified_name = program_.names.Get(
			binding.qualified_name != 0 ? binding.qualified_name : node.text);
		file.cases[0].records.push_back(target);
		std::string result = mangle_fact_file(file);
		if (!result.empty() && result[result.size() - 1] == '\n') result.resize(result.size() - 1);
		return result;
	}

	void RegisterFunction(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.binding == kNoBinding) return;
		if (function_symbols_[record.binding].empty())
		{
			const std::string base = SanitizeSymbol(program_.names.Get(record.text));
			std::size_t& count = overload_counts_[base];
			++count;
			function_symbols_[record.binding] = count == 1 ? base :
				base + "__ov" + std::to_string(count);
		}
		if (record.kind == DUMP_FUNCTION_DEFINITION)
			function_definition_[record.binding] = node;
		else if (function_declaration_[record.binding] == kNoDumpEdge)
			function_declaration_[record.binding] = node;
	}

	void ScanTop(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.kind == DUMP_FUNCTION_DEFINITION ||
			record.kind == DUMP_FUNCTION_DECLARATION)
		{
			RegisterFunction(node);
			return;
		}
		if (record.kind == DUMP_VARIABLE && record.binding != kNoBinding)
		{
			if (global_symbols_[record.binding].empty())
			{
				const BindingRecord& binding = program_.bindings[record.binding];
				global_symbols_[record.binding] = SanitizeSymbol(program_.names.Get(
					binding.qualified_name != 0 ? binding.qualified_name : record.text));
			}
			global_node_[record.binding] = node;
			return;
		}
		if (record.kind != DUMP_TRANSLATION_UNIT && record.kind != DUMP_NAMESPACE)
			return;
		const std::vector<std::uint32_t> children = Children(node);
		for (std::size_t i = 0; i < children.size(); ++i) ScanTop(children[i]);
	}

	void EmitTop(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.kind == DUMP_FUNCTION_DECLARATION)
		{
			if (record.binding != kNoBinding &&
				function_definition_[record.binding] == kNoDumpEdge &&
				function_declaration_[record.binding] == node)
				output_.declarations.push_back(LowerDeclaration(node));
			return;
		}
		if (record.kind == DUMP_FUNCTION_DEFINITION)
		{
			if (record.binding != kNoBinding &&
				function_definition_[record.binding] == node)
				output_.functions.push_back(LowerFunction(node));
			return;
		}
		if (record.kind == DUMP_VARIABLE)
		{
			if (record.binding != kNoBinding && global_node_[record.binding] == node)
				output_.globals.push_back(LowerGlobal(node));
			return;
		}
		if (record.kind != DUMP_TRANSLATION_UNIT && record.kind != DUMP_NAMESPACE)
			return;
		const std::vector<std::uint32_t> children = Children(node);
		for (std::size_t i = 0; i < children.size(); ++i) EmitTop(children[i]);
	}

	void FillBoundary(std::uint32_t node, std::vector<Parameter>* parameters,
		LowType* result, bool* variadic) const
	{
		const DumpNode& record = arena_.nodes[node];
		const TypeRecord& function_type = program_.types.Get(record.type);
		*result = LowerType(function_type.child);
		*variadic = function_type.variadic;
		const std::vector<std::uint32_t> children = Children(node);
		std::size_t parameter_index = 0;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& child = arena_.nodes[children[i]];
			if (child.kind != DUMP_PARAMETER) continue;
			Parameter parameter;
			parameter.name = child.text == 0 ? std::string() : program_.names.Get(child.text);
			if (parameter.name.empty()) parameter.name = "__param" +
				std::to_string(parameter_index);
			parameter.type = LowerType(child.type);
			const TypeId* source_parameters = program_.types.Parameters(record.type);
			parameter.reference = parameter_index < function_type.parameter_count &&
				IsReferenceType(source_parameters[parameter_index]);
			parameters->push_back(parameter);
			++parameter_index;
		}
		const TypeId* source_parameters = program_.types.Parameters(record.type);
		while (parameter_index < function_type.parameter_count)
		{
			Parameter parameter;
			parameter.name = "__param" + std::to_string(parameter_index);
			parameter.type = LowerType(source_parameters[parameter_index]);
			parameter.reference = IsReferenceType(source_parameters[parameter_index]);
			parameters->push_back(parameter);
			++parameter_index;
		}
	}

	FunctionDeclaration LowerDeclaration(std::uint32_t node) const
	{
		const DumpNode& record = arena_.nodes[node];
		FunctionDeclaration declaration;
		declaration.name = function_symbols_[record.binding];
		declaration.object_name = MangleFunction(record);
		FillBoundary(node, &declaration.parameters, &declaration.result,
			&declaration.variadic);
		return declaration;
	}

	Global LowerGlobal(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		const TypeRecord& source_type = program_.types.Get(record.type);
		if (source_type.kind == TYPE_ARRAY ||
			source_type.kind == TYPE_LVALUE_REFERENCE ||
			source_type.kind == TYPE_RVALUE_REFERENCE)
			throw std::runtime_error("aggregate global lowering is outside the active checkpoint");
		Global global;
		global.name = global_symbols_[record.binding];
		global.object_name = MangleVariable(record);
		global.type = LowerType(record.type);
		const std::vector<std::uint32_t> children = Children(node);
		if (children.empty()) global.initializer = "zero";
		else global.initializer = std::to_string(EvaluateIntegerConstant(children[0]));
		if (stats_) ++stats_->globals;
		return global;
	}

	std::int64_t EvaluateIntegerConstant(std::uint32_t node) const
	{
		const DumpNode& record = arena_.nodes[node];
		const std::vector<std::uint32_t> children = Children(node);
		if (record.kind == DUMP_LITERAL)
			return ParseIntegerSpelling(program_.names.Get(record.text));
		if (record.kind == DUMP_ID_EXPRESSION && record.binding != kNoBinding &&
			program_.bindings[record.binding].constant)
			return program_.bindings[record.binding].value;
		if (record.kind == DUMP_CAST_EXPRESSION && children.size() == 1)
			return EvaluateIntegerConstant(children[0]);
		if (record.kind == DUMP_UNARY_EXPRESSION && children.size() == 1)
		{
			const std::int64_t value = EvaluateIntegerConstant(children[0]);
			const std::string op = StripOperationPrefix(program_.names.Get(record.text));
			if (op == "+") return value;
			if (op == "-") return -value;
			if (op == "~") return ~value;
			if (op == "!") return !value;
		}
		if (record.kind == DUMP_BINARY_EXPRESSION && children.size() == 2)
		{
			const std::int64_t left = EvaluateIntegerConstant(children[0]);
			const std::int64_t right = EvaluateIntegerConstant(children[1]);
			const std::string op = StripOperationPrefix(program_.names.Get(record.text));
			if (op == "+") return left + right;
			if (op == "-") return left - right;
			if (op == "*") return left * right;
			if (op == "/") return left / right;
			if (op == "%") return left % right;
			if (op == "|") return left | right;
			if (op == "&") return left & right;
			if (op == "^") return left ^ right;
			if (op == "<<") return left << right;
			if (op == ">>") return left >> right;
			if (op == "==") return left == right;
			if (op == "!=") return left != right;
			if (op == "<") return left < right;
			if (op == "<=") return left <= right;
			if (op == ">") return left > right;
			if (op == ">=") return left >= right;
		}
		if (record.kind == DUMP_CONDITIONAL_EXPRESSION && children.size() == 3)
			return EvaluateIntegerConstant(children[0]) ?
				EvaluateIntegerConstant(children[1]) : EvaluateIntegerConstant(children[2]);
		throw std::runtime_error("global initializer is not a PA15 integer constant");
	}

	std::string UniqueSlotName(const std::string& requested)
	{
		std::string base = requested.empty() ? "__slot" : requested;
		std::size_t& count = slot_name_counts_[base];
		++count;
		std::string candidate = count == 1 ? base :
			base + "__shadow" + std::to_string(count);
		while (assigned_names_[candidate])
		{
			++count;
			candidate = base + "__shadow" + std::to_string(count);
		}
		assigned_names_[candidate] = true;
		used_names_[candidate] = true;
		return candidate;
	}

	std::string GeneratedSlotName(const std::string& prefix)
	{
		std::size_t& next = generated_slot_counts_[prefix];
		while (true)
		{
			const std::string candidate = prefix + "__" + std::to_string(++next);
			if (!used_names_[candidate])
			{
				used_names_[candidate] = true;
				return candidate;
			}
		}
	}

	void CollectSourceNames(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if ((record.kind == DUMP_PARAMETER || record.kind == DUMP_VARIABLE) &&
			record.text != 0)
			used_names_[program_.names.Get(record.text)] = true;
		const std::vector<std::uint32_t> children = Children(node);
		for (std::size_t i = 0; i < children.size(); ++i) CollectSourceNames(children[i]);
	}

	void CollectSlots(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if ((record.kind == DUMP_PARAMETER || record.kind == DUMP_VARIABLE) &&
			record.binding != kNoBinding)
		{
			if (binding_slots_[record.binding].empty())
			{
				std::string requested = record.text == 0 ? std::string() :
					program_.names.Get(record.text);
				if (record.kind == DUMP_PARAMETER && requested.empty())
					requested = parameter_slot_index_ < function_->parameters.size() ?
						function_->parameters[parameter_slot_index_].name : "__param";
				const std::string name = UniqueSlotName(requested);
				binding_slots_[record.binding] = name;
				Slot slot;
				slot.name = name;
				slot.type = LowerType(record.type);
				function_->slots.push_back(slot);
			}
			if (record.kind == DUMP_PARAMETER) ++parameter_slot_index_;
		}
		const std::vector<std::uint32_t> children = Children(node);
		for (std::size_t i = 0; i < children.size(); ++i) CollectSlots(children[i]);
	}

	void CollectGeneratedSlots(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.kind == DUMP_CONDITIONAL_EXPRESSION)
		{
			const std::string name = GeneratedSlotName("cond");
			generated_slots_[node] = name;
			Slot slot;
			slot.name = name;
			slot.type = LowerType(record.type);
			function_->slots.push_back(slot);
		}
		const std::vector<std::uint32_t> children = Children(node);
		for (std::size_t i = 0; i < children.size(); ++i)
			CollectGeneratedSlots(children[i]);
	}

	Function LowerFunction(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		Function result;
		result.name = function_symbols_[record.binding];
		result.object_name = MangleFunction(record);
		result.entry = program_.names.Get(record.text) == "main";
		FillBoundary(node, &result.parameters, &result.result, &result.variadic);
		function_ = &result;
		current_result_ = result.result;
		temp_counter_ = 0;
		block_counter_ = 0;
		binding_slots_.assign(program_.bindings.size(), std::string());
		generated_slots_.assign(arena_.nodes.size(), std::string());
		used_names_.clear();
		assigned_names_.clear();
		slot_name_counts_.clear();
		generated_slot_counts_.clear();
		parameter_slot_index_ = 0;
		CollectSourceNames(node);
		CollectSlots(node);
		CollectGeneratedSlots(node);
		NewBlock("entry");

		const std::vector<std::uint32_t> children = Children(node);
		std::size_t parameter_index = 0;
		std::uint32_t body = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& child = arena_.nodes[children[i]];
			if (child.kind == DUMP_PARAMETER)
			{
				Instruction store(Instruction::STORE);
				store.type = result.parameters[parameter_index].type;
				store.first = Operand(Operand::TEMP,
					"%" + result.parameters[parameter_index].name, store.type);
				store.second = Operand(Operand::SLOT,
					"$" + binding_slots_[child.binding], store.type);
				Emit(store);
				++parameter_index;
			}
			else if (child.kind == DUMP_COMPOUND_STATEMENT) body = children[i];
		}
		if (body != kNoDumpEdge) LowerStatement(body);
		if (!CurrentBlock().terminated)
		{
			if (result.entry)
			{
				Instruction instruction(Instruction::RETURN_VALUE);
				instruction.type = result.result;
				instruction.first = Operand(Operand::INTEGER, "0", result.result);
				Emit(instruction);
			}
			else if (result.result.kind == LOW_VOID)
				Emit(Instruction(Instruction::RETURN_VOID));
			else throw std::runtime_error("non-void function has no return");
		}
		if (stats_)
		{
			++stats_->functions;
			stats_->blocks += result.blocks.size();
		}
		function_ = 0;
		return result;
	}

	Block& CurrentBlock() { return function_->blocks[current_block_]; }

	void NewBlock(const std::string& label)
	{
		function_->blocks.push_back(Block(label));
		current_block_ = function_->blocks.size() - 1;
	}

	std::string NewLabel(const std::string& prefix)
	{
		return prefix + "_" + std::to_string(++block_counter_);
	}

	std::string NewTemp()
	{
		while (true)
		{
			const std::string candidate = "t" + std::to_string(++temp_counter_);
			if (!used_names_[candidate]) return candidate;
		}
	}

	Operand Temp(const LowType& type)
	{
		return Operand(Operand::TEMP, "%" + NewTemp(), type);
	}

	void Emit(const Instruction& instruction)
	{
		if (CurrentBlock().terminated)
			throw std::runtime_error("PA15 attempted to emit after a terminator");
		CurrentBlock().instructions.push_back(instruction);
		if (IsTerminator(instruction)) CurrentBlock().terminated = true;
		if (stats_) ++stats_->instructions;
	}

	Operand StorageFor(BindingId binding, const LowType& type)
	{
		if (stats_) ++stats_->binding_index_probes;
		if (binding < binding_slots_.size() && !binding_slots_[binding].empty())
			return Operand(Operand::SLOT, "$" + binding_slots_[binding], type);
		if (binding < global_symbols_.size() && !global_symbols_[binding].empty())
			return Operand(Operand::GLOBAL, "@" + global_symbols_[binding], type);
		throw std::runtime_error("PA15 binding has no lowered storage");
	}

	Operand LowerStorage(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.kind == DUMP_ID_EXPRESSION && record.binding != kNoBinding)
			return StorageFor(record.binding, LowerType(record.type));
		throw std::runtime_error("expression does not designate scalar storage");
	}

	Operand Convert(Operand value, const LowType& target,
		bool canonicalize_immediate = true)
	{
		if (SameType(value.type, target))
		{
			value.type = target;
			return value;
		}
		if (IsInteger(value.type) && IsInteger(target) &&
			value.type.width == target.width)
		{
			value.type = target;
			return value;
		}
		if (canonicalize_immediate && value.kind == Operand::INTEGER &&
			IsInteger(value.type) && IsInteger(target))
		{
			value.type = target;
			return value;
		}
		Instruction instruction(Instruction::CONVERT);
		instruction.type = target;
		instruction.source_type = value.type;
		if (IsInteger(value.type) && IsInteger(target))
			instruction.op = target.width < value.type.width ? "trunc" :
				value.type.is_signed ? "sext" : "zext";
		else if (IsInteger(value.type) && IsFloating(target))
			instruction.op = value.type.is_signed ? "sitofp" : "uitofp";
		else if (IsFloating(value.type) && IsInteger(target))
			instruction.op = target.is_signed ? "fptosi" : "fptoui";
		else if (IsFloating(value.type) && IsFloating(target))
			instruction.op = target.width < value.type.width ? "fptrunc" : "fpext";
		else throw std::runtime_error("unsupported PA15 scalar conversion");
		const Operand result = Temp(target);
		instruction.dest = result.text;
		instruction.first = value;
		Emit(instruction);
		return result;
	}

	LowType CommonBinaryType(std::uint32_t left_node,
		std::uint32_t right_node) const
	{
		const LowType left = LowerType(arena_.nodes[left_node].type);
		const LowType right = LowerType(arena_.nodes[right_node].type);
		if (left.kind == LOW_PTR || right.kind == LOW_PTR) return LowPtr();
		if (IsFloating(left) || IsFloating(right))
		{
			if (left.kind == LOW_F80 || right.kind == LOW_F80) return LowF80();
			if (left.kind == LOW_F64 || right.kind == LOW_F64) return LowF64();
			return LowF32();
		}
		if (!IsInteger(left) || !IsInteger(right))
			throw std::runtime_error("invalid scalar binary operands");
		const LowType promoted_left = left.width < 32 ? LowI32() : left;
		const LowType promoted_right = right.width < 32 ? LowI32() : right;
		if (promoted_left.width > promoted_right.width) return promoted_left;
		if (promoted_right.width > promoted_left.width) return promoted_right;
		if (!promoted_left.is_signed || !promoted_right.is_signed)
		{
			if (promoted_left.width == 64) return LowU64();
			if (promoted_left.width == 32) return LowU32();
		}
		return promoted_left;
	}

	Operand LowerValue(std::uint32_t node, const LowType& expected = LowType())
	{
		if (stats_) ++stats_->lowered_nodes;
		const DumpNode& record = arena_.nodes[node];
		const std::vector<std::uint32_t> children = Children(node);
		Operand result;
		if (record.kind == DUMP_LITERAL)
		{
			const LowType type = LowerType(record.type);
			const std::string spelling = program_.names.Get(record.text);
			result = Operand(IsFloating(type) ? Operand::FLOATING : Operand::INTEGER,
				CanonicalLiteral(spelling, type), type);
		}
		else if (record.kind == DUMP_ID_EXPRESSION)
		{
			if (record.binding != kNoBinding && record.binding < function_symbols_.size() &&
				!function_symbols_[record.binding].empty())
			{
				const Operand address = Temp(LowPtr());
				Instruction instruction(Instruction::ADDR);
				instruction.dest = address.text;
				instruction.first = Operand(Operand::GLOBAL,
					"@" + function_symbols_[record.binding], LowPtr());
				Emit(instruction);
				result = address;
			}
			else
			{
				const LowType type = LowerType(record.type);
				const Operand storage = LowerStorage(node);
				result = Temp(type);
				Instruction load(Instruction::LOAD);
				load.dest = result.text;
				load.type = type;
				load.first = storage;
				Emit(load);
			}
		}
		else if (record.kind == DUMP_BINARY_EXPRESSION)
			result = LowerBinary(record, children);
		else if (record.kind == DUMP_ASSIGNMENT_EXPRESSION)
			result = LowerAssignment(record, children);
		else if (record.kind == DUMP_UNARY_EXPRESSION ||
			record.kind == DUMP_POSTFIX_EXPRESSION)
			result = LowerUnary(record, children);
		else if (record.kind == DUMP_CALL_EXPRESSION)
			result = LowerCall(record, children);
		else if (record.kind == DUMP_CAST_EXPRESSION)
		{
			if (children.size() != 1) throw std::runtime_error("invalid semantic cast");
			result = Convert(LowerValue(children[0]), LowerType(record.type));
		}
		else if (record.kind == DUMP_CONDITIONAL_EXPRESSION)
			result = LowerConditional(node, record, children);
		else if (record.kind == DUMP_BRACED_INIT_LIST && children.empty())
			result = Operand(Operand::INTEGER, "0", LowerType(record.type));
		else throw std::runtime_error("semantic expression is outside the active PA15 checkpoint");
		return expected.kind == LOW_INVALID ? result : Convert(result, expected);
	}

	Operand LowerBinary(const DumpNode& record,
		const std::vector<std::uint32_t>& children)
	{
		if (children.size() != 2) throw std::runtime_error("invalid semantic binary");
		const std::string op = StripOperationPrefix(program_.names.Get(record.text));
		if (op == "&&" || op == "||" || op == ",")
			throw std::runtime_error("short-circuit/comma lowering is outside the active checkpoint");
		const bool comparison = op == "==" || op == "!=" || op == "<" ||
			op == "<=" || op == ">" || op == ">=";
		LowType operand_type = comparison ? CommonBinaryType(children[0], children[1]) :
			LowerType(record.type);
		Operand left = Convert(LowerValue(children[0]), operand_type, false);
		Operand right = Convert(LowerValue(children[1]), operand_type, false);
		const LowType result_type = LowerType(record.type);
		const Operand result = Temp(result_type);
		Instruction instruction(comparison ? Instruction::CMP : Instruction::BINARY);
		instruction.dest = result.text;
		instruction.type = operand_type;
		instruction.first = left;
		instruction.second = right;
		if (comparison)
		{
			instruction.op = op == "==" ? "eq" : op == "!=" ? "ne" :
				op == "<" ? (operand_type.is_signed ? "lt" : "ult") :
				op == "<=" ? (operand_type.is_signed ? "le" : "ule") :
				op == ">" ? (operand_type.is_signed ? "gt" : "ugt") :
				(operand_type.is_signed ? "ge" : "uge");
		}
		else
		{
			instruction.op = op == "+" ? "add" : op == "-" ? "sub" :
				op == "*" ? "mul" : op == "/" ?
					(operand_type.is_signed || IsFloating(operand_type) ? "div" : "udiv") :
				op == "%" ? (operand_type.is_signed ? "mod" : "umod") :
				op == "&" ? "and" : op == "|" ? "or" : op == "^" ? "xor" :
				op == "<<" ? "shl" : op == ">>" ?
					(operand_type.is_signed ? "shr" : "ushr") : std::string();
			if (instruction.op.empty()) throw std::runtime_error("unsupported binary operator");
		}
		Emit(instruction);
		return result;
	}

	Operand LowerAssignment(const DumpNode& record,
		const std::vector<std::uint32_t>& children)
	{
		if (children.size() != 2) throw std::runtime_error("invalid semantic assignment");
		const std::string op = StripOperationPrefix(program_.names.Get(record.text));
		const Operand storage = LowerStorage(children[0]);
		const LowType type = LowerType(record.type);
		Operand value;
		if (op == "=") value = Convert(LowerValue(children[1]), type, false);
		else
		{
			Operand left = Temp(type);
			Instruction load(Instruction::LOAD);
			load.dest = left.text;
			load.type = type;
			load.first = storage;
			Emit(load);
			const LowType operation_type = IsFloating(type) ? type :
				CommonBinaryType(children[0], children[1]);
			left = Convert(left, operation_type, false);
			const Operand right = Convert(LowerValue(children[1]), operation_type, false);
			value = Temp(operation_type);
			Instruction binary(Instruction::BINARY);
			binary.dest = value.text;
			binary.type = operation_type;
			binary.first = left;
			binary.second = right;
			binary.op = op == "+=" ? "add" : op == "-=" ? "sub" :
				op == "*=" ? "mul" : op == "/=" ?
					(operation_type.is_signed || IsFloating(operation_type) ? "div" : "udiv") :
				op == "%=" ? (operation_type.is_signed ? "mod" : "umod") :
				op == "&=" ? "and" : op == "|=" ? "or" : op == "^=" ? "xor" :
				op == "<<=" ? "shl" : op == ">>=" ?
					(operation_type.is_signed ? "shr" : "ushr") : std::string();
			if (binary.op.empty())
				throw std::runtime_error("unsupported PA15 compound assignment");
			Emit(binary);
			value = Convert(value, type, false);
		}
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = value;
		store.second = storage;
		Emit(store);
		return value;
	}

	Operand LowerUnary(const DumpNode& record,
		const std::vector<std::uint32_t>& children)
	{
		if (children.size() != 1) throw std::runtime_error("invalid semantic unary");
		const std::string op = StripOperationPrefix(program_.names.Get(record.text));
		const LowType type = LowerType(record.type);
		Operand value = LowerValue(children[0], type);
		if (op == "+") return value;
		if (op == "!")
		{
			const Operand result = Temp(type);
			Instruction compare(Instruction::CMP);
			compare.dest = result.text;
			compare.op = "eq";
			compare.type = value.type;
			compare.first = value;
			compare.second = Operand(Operand::INTEGER, "0", value.type);
			Emit(compare);
			return result;
		}
		const Operand result = Temp(type);
		Instruction instruction(Instruction::UNARY);
		instruction.dest = result.text;
		instruction.op = op == "-" ? "neg" : op == "~" ? "bitnot" : std::string();
		if (instruction.op.empty())
			throw std::runtime_error("increment/address unary lowering is outside the active checkpoint");
		instruction.type = type;
		instruction.first = value;
		Emit(instruction);
		return result;
	}

	Operand LowerCall(const DumpNode& record,
		const std::vector<std::uint32_t>& children)
	{
		if (children.empty()) throw std::runtime_error("semantic call has no callee");
		const DumpNode& callee = arena_.nodes[children[0]];
		if (stats_) ++stats_->binding_index_probes;
		if (callee.kind != DUMP_CALLEE || callee.binding == kNoBinding ||
			callee.binding >= function_symbols_.size() ||
			function_symbols_[callee.binding].empty())
			throw std::runtime_error("indirect call lowering is outside the active checkpoint");
		const TypeRecord& function_type = program_.types.Get(callee.type);
		const TypeId* parameters = program_.types.Parameters(callee.type);
		Instruction call(Instruction::CALL);
		call.type = LowerType(record.type);
		call.first = Operand(Operand::GLOBAL,
			"@" + function_symbols_[callee.binding], LowPtr());
		for (std::size_t i = 1; i < children.size(); ++i)
		{
			const LowType expected = i - 1 < function_type.parameter_count ?
				LowerType(parameters[i - 1]) : LowerType(arena_.nodes[children[i]].type);
			call.arguments.push_back(Convert(LowerValue(children[i]), expected, false));
		}
		if (call.type.kind == LOW_VOID)
		{
			Emit(call);
			return Operand(Operand::INTEGER, "0", LowVoid());
		}
		const Operand result = Temp(call.type);
		call.dest = result.text;
		Emit(call);
		return result;
	}

	Operand LowerConditional(std::uint32_t node, const DumpNode& record,
		const std::vector<std::uint32_t>& children)
	{
		if (children.size() != 3) throw std::runtime_error("invalid semantic conditional");
		const std::string then_label = NewLabel("cond_then");
		const std::string else_label = NewLabel("cond_else");
		const std::string end_label = NewLabel("cond_end");
		Instruction branch(Instruction::BRANCH);
		branch.first = LowerValue(children[0]);
		branch.target = then_label;
		branch.alternate = else_label;
		Emit(branch);
		const LowType type = LowerType(record.type);
		const Operand slot(Operand::SLOT, "$" + generated_slots_[node], type);
		NewBlock(then_label);
		Instruction yes_store(Instruction::STORE);
		yes_store.type = type;
		yes_store.first = LowerValue(children[1], type);
		yes_store.second = slot;
		Emit(yes_store);
		Instruction yes_jump(Instruction::JUMP);
		yes_jump.target = end_label;
		Emit(yes_jump);
		NewBlock(else_label);
		Instruction no_store(Instruction::STORE);
		no_store.type = type;
		no_store.first = LowerValue(children[2], type);
		no_store.second = slot;
		Emit(no_store);
		Instruction no_jump(Instruction::JUMP);
		no_jump.target = end_label;
		Emit(no_jump);
		NewBlock(end_label);
		const Operand result = Temp(type);
		Instruction load(Instruction::LOAD);
		load.dest = result.text;
		load.type = type;
		load.first = slot;
		Emit(load);
		return result;
	}

	void LowerStatement(std::uint32_t node)
	{
		if (stats_) ++stats_->lowered_nodes;
		const DumpNode& record = arena_.nodes[node];
		const std::vector<std::uint32_t> children = Children(node);
		if (record.kind == DUMP_COMPOUND_STATEMENT ||
			record.kind == DUMP_SIMPLE_DECLARATION ||
			record.kind == DUMP_THEN || record.kind == DUMP_ELSE)
		{
			for (std::size_t i = 0; i < children.size(); ++i)
			{
				if (CurrentBlock().terminated) break;
				LowerStatement(children[i]);
			}
			return;
		}
		if (record.kind == DUMP_VARIABLE)
		{
			if (!children.empty())
			{
				const LowType type = LowerType(record.type);
				Instruction store(Instruction::STORE);
				store.type = type;
				store.first = Convert(LowerValue(children[0]), type, false);
				store.second = StorageFor(record.binding, type);
				Emit(store);
			}
			return;
		}
		if (record.kind == DUMP_RETURN_STATEMENT)
		{
			if (children.empty()) Emit(Instruction(Instruction::RETURN_VOID));
			else
			{
				Instruction instruction(Instruction::RETURN_VALUE);
				instruction.type = current_result_;
				instruction.first = LowerValue(children[0], current_result_);
				Emit(instruction);
			}
			return;
		}
		if (record.kind == DUMP_EXPRESSION_STATEMENT)
		{
			if (!children.empty()) (void)LowerValue(children[0]);
			return;
		}
		if (record.kind == DUMP_IF_STATEMENT)
		{
			LowerIf(children);
			return;
		}
		throw std::runtime_error("statement is outside the active PA15 checkpoint");
	}

	void LowerIf(const std::vector<std::uint32_t>& children)
	{
		std::uint32_t condition = kNoDumpEdge;
		std::uint32_t then_node = kNoDumpEdge;
		std::uint32_t else_node = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpKind kind = arena_.nodes[children[i]].kind;
			if (kind == DUMP_CONDITION) condition = children[i];
			else if (kind == DUMP_THEN) then_node = children[i];
			else if (kind == DUMP_ELSE) else_node = children[i];
		}
		if (condition == kNoDumpEdge || then_node == kNoDumpEdge)
			throw std::runtime_error("invalid semantic if statement");
		const std::vector<std::uint32_t> condition_children = Children(condition);
		if (condition_children.size() != 1)
			throw std::runtime_error("condition declarations are outside the active checkpoint");
		const std::string then_label = NewLabel("if_then");
		const std::string else_label = NewLabel("if_else");
		Instruction branch(Instruction::BRANCH);
		branch.first = LowerValue(condition_children[0]);
		branch.target = then_label;
		branch.alternate = else_label;
		Emit(branch);
		NewBlock(then_label);
		LowerStatement(then_node);
		const std::size_t then_block = current_block_;
		NewBlock(else_label);
		if (else_node != kNoDumpEdge) LowerStatement(else_node);
		const std::size_t else_block = current_block_;
		const bool then_terminated = function_->blocks[then_block].terminated;
		const bool else_terminated = function_->blocks[else_block].terminated;
		if (then_terminated && else_terminated) return;
		const std::string end_label = NewLabel("if_end");
		if (!then_terminated)
		{
			Instruction then_jump(Instruction::JUMP);
			then_jump.target = end_label;
			current_block_ = then_block;
			Emit(then_jump);
		}
		if (!else_terminated)
		{
			Instruction else_jump(Instruction::JUMP);
			else_jump.target = end_label;
			current_block_ = else_block;
			Emit(else_jump);
		}
		NewBlock(end_label);
	}

	const SemanticGraphView& graph_;
	const Program& program_;
	const DumpArena& arena_;
	TypedProgram& output_;
	LowIRLoweringStats* stats_;
	std::vector<std::string> function_symbols_;
	std::vector<std::string> global_symbols_;
	std::vector<std::uint32_t> function_definition_;
	std::vector<std::uint32_t> function_declaration_;
	std::vector<std::uint32_t> global_node_;
	std::unordered_map<std::string, std::size_t> overload_counts_;
	Function* function_;
	std::size_t current_block_;
	LowType current_result_;
	std::size_t temp_counter_;
	std::size_t block_counter_;
	std::vector<std::string> binding_slots_;
	std::vector<std::string> generated_slots_;
	std::unordered_map<std::string, bool> used_names_;
	std::unordered_map<std::string, bool> assigned_names_;
	std::unordered_map<std::string, std::size_t> slot_name_counts_;
	std::unordered_map<std::string, std::size_t> generated_slot_counts_;
	std::size_t parameter_slot_index_;
};

void WriteParameter(std::ostream& output, const Parameter& parameter)
{
	output << '%' << parameter.name << " : " << parameter.type.text;
	if (parameter.reference) output << " [pass=reference]";
}

void WriteBoundary(std::ostream& output,
	const std::vector<Parameter>& parameters, const LowType& result,
	bool variadic)
{
	output << '(';
	for (std::size_t i = 0; i < parameters.size(); ++i)
	{
		if (i != 0) output << ", ";
		WriteParameter(output, parameters[i]);
	}
	output << ") -> " << result.text;
	if (variadic) output << " [arity=variadic]";
}

void WriteInstruction(std::ostream& output, const Instruction& instruction)
{
	switch (instruction.kind)
	{
	case Instruction::ADDR:
		output << instruction.dest << " = addr " << instruction.first.text; break;
	case Instruction::LOAD:
		output << instruction.dest << " = load " << instruction.type.text << ' '
			<< instruction.first.text; break;
	case Instruction::STORE:
		output << "store " << instruction.type.text << ' ' << instruction.first.text
			<< ", " << instruction.second.text; break;
	case Instruction::UNARY:
		output << instruction.dest << " = unary " << instruction.op << ' '
			<< instruction.type.text << ' ' << instruction.first.text; break;
	case Instruction::BINARY:
		output << instruction.dest << " = binary " << instruction.op << ' '
			<< instruction.type.text << ' ' << instruction.first.text << ", "
			<< instruction.second.text; break;
	case Instruction::CMP:
		output << instruction.dest << " = cmp " << instruction.op << ' '
			<< instruction.type.text << ' ' << instruction.first.text << ", "
			<< instruction.second.text; break;
	case Instruction::CONVERT:
		output << instruction.dest << " = convert " << instruction.op << ' '
			<< instruction.type.text << ' ' << instruction.source_type.text << ' '
			<< instruction.first.text; break;
	case Instruction::CALL:
		if (!instruction.dest.empty()) output << instruction.dest << " = ";
		output << "call " << instruction.type.text << ' ' << instruction.first.text << '(';
		for (std::size_t i = 0; i < instruction.arguments.size(); ++i)
		{
			if (i != 0) output << ", ";
			output << instruction.arguments[i].text;
		}
		output << ')';
		break;
	case Instruction::JUMP: output << "jump ^" << instruction.target; break;
	case Instruction::BRANCH:
		output << "branch " << instruction.first.text << ", ^" << instruction.target
			<< ", ^" << instruction.alternate; break;
	case Instruction::RETURN_VALUE:
		output << "return " << instruction.type.text << ' ' << instruction.first.text; break;
	case Instruction::RETURN_VOID: output << "return void"; break;
	}
}

void WriteFunctionMetadata(std::ostream& output, const std::string& object_name,
	bool entry)
{
	if (entry) output << " [role=entry, binding=strong, keep_alias=yes]";
	else
	{
		output << " [binding=strong";
		if (!object_name.empty()) output << ", object=" << object_name;
		output << ']';
	}
}

void RenderProgram(const TypedProgram& program, std::ostream& output)
{
	bool wrote = false;
	for (std::size_t i = 0; i < program.declarations.size(); ++i)
	{
		const FunctionDeclaration& declaration = program.declarations[i];
		if (wrote) output << '\n';
		output << "declare function @" << declaration.name;
		WriteBoundary(output, declaration.parameters, declaration.result,
			declaration.variadic);
		WriteFunctionMetadata(output, declaration.object_name, false);
		output << '\n';
		wrote = true;
	}
	for (std::size_t i = 0; i < program.globals.size(); ++i)
	{
		const Global& global = program.globals[i];
		if (wrote) output << '\n';
		output << "global @" << global.name << " : " << global.type.text
			<< " [binding=strong";
		if (!global.object_name.empty()) output << ", object=" << global.object_name;
		output << "] = " << global.initializer << '\n';
		wrote = true;
	}
	for (std::size_t i = 0; i < program.functions.size(); ++i)
	{
		const Function& function = program.functions[i];
		if (wrote) output << '\n';
		output << "function @" << function.name;
		WriteBoundary(output, function.parameters, function.result, function.variadic);
		WriteFunctionMetadata(output, function.object_name, function.entry);
		output << " {\n";
		for (std::size_t s = 0; s < function.slots.size(); ++s)
			output << "  slot $" << function.slots[s].name << " : "
				<< function.slots[s].type.text << '\n';
		if (!function.slots.empty()) output << '\n';
		for (std::size_t b = 0; b < function.blocks.size(); ++b)
		{
			if (b != 0) output << '\n';
			output << "  block ^" << function.blocks[b].label << ":\n";
			for (std::size_t j = 0; j < function.blocks[b].instructions.size(); ++j)
			{
				output << "    ";
				WriteInstruction(output, function.blocks[b].instructions[j]);
				output << '\n';
			}
		}
		output << "}\n";
		wrote = true;
	}
}

std::size_t TypedStorageBytes(const TypedProgram& program)
{
	std::size_t bytes = program.declarations.capacity() * sizeof(FunctionDeclaration) +
		program.globals.capacity() * sizeof(Global) +
		program.functions.capacity() * sizeof(Function);
	for (std::size_t i = 0; i < program.functions.size(); ++i)
	{
		const Function& function = program.functions[i];
		bytes += function.parameters.capacity() * sizeof(Parameter) +
			function.slots.capacity() * sizeof(Slot) +
			function.blocks.capacity() * sizeof(Block);
		for (std::size_t b = 0; b < function.blocks.size(); ++b)
			bytes += function.blocks[b].instructions.capacity() * sizeof(Instruction);
	}
	return bytes;
}

class GraphConsumer : public SemanticGraphConsumer
{
public:
	GraphConsumer(TypedProgram& program, LowIRLoweringStats* stats)
		: program_(program), stats_(stats) {}

	void Consume(const SemanticGraphView& graph)
	{
		const std::chrono::steady_clock::time_point started =
			std::chrono::steady_clock::now();
		GraphLowerer(graph, program_, stats_).Lower();
		if (stats_)
			stats_->lowering_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started).count());
	}

private:
	TypedProgram& program_;
	LowIRLoweringStats* stats_;
};

}

LowIRLoweringStats::LowIRLoweringStats()
	: source_bytes(0), tokens(0), semantic_nodes(0), semantic_edges(0),
	  lowered_nodes(0), functions(0), globals(0), blocks(0), instructions(0),
	  binding_index_probes(0), typed_storage_bytes(0), output_bytes(0),
	  semantic_nanoseconds(0), lowering_nanoseconds(0), render_nanoseconds(0)
{
}

void WriteLowIRProgram(const std::vector<LowIRSource>& sources,
	const PreprocessingOptions& options, std::ostream& output,
	LowIRLoweringStats* stats)
{
	if (sources.empty()) throw std::runtime_error("no PA15 source inputs");
	if (stats) *stats = LowIRLoweringStats();
	TypedProgram program;
	GraphConsumer consumer(program, stats);
	for (std::size_t i = 0; i < sources.size(); ++i)
	{
		SemanticAnalysisStats semantic_stats;
		ConsumeSemanticTranslationUnit(sources[i].path, sources[i].source,
			options, consumer, stats ? &semantic_stats : 0);
		if (stats)
		{
			stats->source_bytes += sources[i].source.size();
			stats->tokens += semantic_stats.tokens;
			stats->semantic_nodes += semantic_stats.semantic_nodes;
			stats->semantic_edges += semantic_stats.semantic_edges;
			stats->semantic_nanoseconds += semantic_stats.analysis_nanoseconds;
		}
	}
	const std::chrono::steady_clock::time_point render_started =
		std::chrono::steady_clock::now();
	std::ostringstream rendered;
	RenderProgram(program, rendered);
	const std::string text = rendered.str();
	output.write(text.data(), static_cast<std::streamsize>(text.size()));
	if (!output) throw std::runtime_error("unable to write LowIR output");
	if (stats)
	{
		stats->typed_storage_bytes = TypedStorageBytes(program);
		stats->output_bytes = text.size();
		stats->render_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - render_started).count());
	}
}

}
