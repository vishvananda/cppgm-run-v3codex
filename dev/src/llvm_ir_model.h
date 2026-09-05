#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cppgm
{
namespace llvm_ir
{

struct Type
{
	enum Kind : std::uint8_t
	{
		VOID,
		I1,
		I8,
		I16,
		I32,
		I64,
		I128,
		HALF,
		FLOAT,
		DOUBLE,
		X86_FP80,
		FP128,
		POINTER,
		ARRAY,
		STRUCTURE
	};

	Kind kind;
	std::uint64_t count;
	std::vector<Type> elements;
	bool packed;

	explicit Type(Kind kind_value = VOID)
		: kind(kind_value), count(0), packed(false) {}
	static Type Array(std::uint64_t count, const Type& element);
	static Type Structure(const std::vector<Type>& elements, bool packed = false);
};

bool operator==(const Type& left, const Type& right);
bool operator!=(const Type& left, const Type& right);
std::string RenderType(const Type& type);

struct Operand
{
	enum Kind : std::uint8_t
	{
		LOCAL,
		GLOBAL,
		INTEGER,
		FLOATING,
		NULL_POINTER,
		UNDEF,
		POISON,
		ZERO_INITIALIZER,
		ARRAY_CONSTANT,
		STRUCTURE_CONSTANT,
		GETELEMENTPTR_CONSTANT
	};

	Kind kind;
	Type type;
	Type source_type;
	std::string text;
	std::vector<Operand> elements;

	Operand(Kind kind_value = UNDEF, const Type& type_value = Type(),
		const std::string& text_value = std::string())
		: kind(kind_value), type(type_value), text(text_value) {}
	static Operand Local(const Type& type, const std::string& name);
	static Operand Global(const Type& type, const std::string& name);
	static Operand Integer(const Type& type, const std::string& value);
	static Operand Floating(const Type& type, const std::string& value);
	static Operand Null(const Type& type = Type(Type::POINTER));
	static Operand Aggregate(const Type& type,
		const std::vector<Operand>& elements);
	static Operand GetElementPtr(const Type& source_type,
		const Operand& base, const std::vector<Operand>& indices);
};

enum class Linkage : std::uint8_t
{
	EXTERNAL,
	INTERNAL,
	PRIVATE,
	WEAK,
	WEAK_ODR,
	LINKONCE_ODR,
	EXTERNAL_WEAK,
	COMMON,
	AVAILABLE_EXTERNALLY
};

struct Parameter
{
	Type type;
	std::string name;
	std::vector<std::string> attributes;
};

struct Instruction
{
	enum Kind : std::uint8_t
	{
		ALLOCA,
		LOAD,
		STORE,
		BINARY,
		ICMP,
		FCMP,
		CAST,
		GETELEMENTPTR,
		CALL,
		PHI,
		BRANCH,
		CONDITIONAL_BRANCH,
		SWITCH,
		RETURN,
		UNREACHABLE
	};

	enum BinaryOperation : std::uint8_t
	{
		BINARY_NONE,
		ADD,
		SUB,
		MUL,
		SDIV,
		UDIV,
		SREM,
		UREM,
		SHL,
		LSHR,
		ASHR,
		AND,
		OR,
		XOR,
		FADD,
		FSUB,
		FMUL,
		FDIV,
		FREM
	};

	enum Predicate : std::uint8_t
	{
		PREDICATE_NONE,
		EQ,
		NE,
		SLT,
		SLE,
		SGT,
		SGE,
		ULT,
		ULE,
		UGT,
		UGE,
		FOEQ,
		FONE,
		FOLT,
		FOLE,
		FOGT,
		FOGE,
		FUEQ,
		FUNE
	};

	enum CastOperation : std::uint8_t
	{
		CAST_NONE,
		TRUNC,
		ZEXT,
		SEXT,
		FPTRUNC,
		FPEXT,
		FPTOUI,
		FPTOSI,
		UITOFP,
		SITOFP,
		PTRTOINT,
		INTTOPTR,
		BITCAST
	};

	Kind kind;
	std::string result;
	Type type;
	Type source_type;
	Operand first;
	Operand second;
	std::vector<Operand> operands;
	std::vector<std::vector<std::string> > argument_attributes;
	std::vector<std::string> return_attributes;
	std::vector<std::string> labels;
	std::string callee;
	std::string target;
	std::string alternate;
	std::size_t alignment;
	BinaryOperation binary_operation;
	Predicate predicate;
	CastOperation cast_operation;
	bool inbounds;
	bool indirect_call;
	bool tail_call;

	explicit Instruction(Kind kind_value)
		: kind(kind_value), alignment(0),
		  binary_operation(BINARY_NONE), predicate(PREDICATE_NONE),
		  cast_operation(CAST_NONE), inbounds(false), indirect_call(false),
		  tail_call(false) {}
};

bool IsTerminator(const Instruction& instruction);

struct Block
{
	std::string name;
	std::vector<Instruction> instructions;
};

struct Function
{
	std::string name;
	Type result;
	std::vector<Parameter> parameters;
	std::vector<std::string> return_attributes;
	std::vector<std::string> function_attributes;
	std::vector<Block> blocks;
	Linkage linkage;
	bool declaration;
	bool variadic;
	bool dso_local;
	std::size_t alignment;

	Function()
		: linkage(Linkage::EXTERNAL), declaration(false), variadic(false),
		  dso_local(false), alignment(0) {}
};

struct Global
{
	std::string name;
	Type type;
	Operand initializer;
	Linkage linkage;
	bool declaration;
	bool constant;
	bool dso_local;
	bool unnamed_address;
	std::size_t alignment;

	Global()
		: initializer(Operand::ZERO_INITIALIZER), linkage(Linkage::EXTERNAL),
		  declaration(false), constant(false), dso_local(false),
		  unnamed_address(false), alignment(0) {}
};

struct Module
{
	std::string source_filename;
	std::string target_data_layout;
	std::string target_triple;
	std::vector<Global> globals;
	std::vector<Function> functions;
};

void ValidateModule(const Module& module);
std::string SerializeModule(const Module& module);

}
}
