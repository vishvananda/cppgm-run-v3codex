#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "cy86/cy86_program.h"
#include "preprocess/tokens/post_tokenizer.h"

namespace cppgm
{

typedef std::uint32_t Cy86Identifier;

class Cy86Identifiers
{
public:
	Cy86Identifiers();
	Cy86Identifier Intern(const std::string& spelling);
	const std::string& Spelling(Cy86Identifier identifier) const;
	std::size_t Size() const;
	std::size_t Bytes() const;
	void Clear();

private:
	std::unordered_map<std::string, Cy86Identifier> index_;
	std::vector<const std::string*> spellings_;
	std::size_t bytes_;
};

enum Cy86RegisterBank
{
	CY86_REG_SP,
	CY86_REG_BP,
	CY86_REG_X,
	CY86_REG_Y,
	CY86_REG_Z,
	CY86_REG_T,
	CY86_REG_INVALID
};

struct Cy86Register
{
	Cy86RegisterBank bank;
	unsigned width;

	Cy86Register();
};

struct Cy86Literal
{
	FundamentalType type;
	std::uint32_t offset;
	std::uint32_t size;
	std::uint32_t elements;
	bool array;

	Cy86Literal();
};

enum Cy86Operation
{
	CY86_DATA,
	CY86_MOVE,
	CY86_JUMP,
	CY86_JUMP_IF,
	CY86_CALL,
	CY86_RET,
	CY86_NOT,
	CY86_AND,
	CY86_OR,
	CY86_XOR,
	CY86_LSHIFT,
	CY86_SRSHIFT,
	CY86_URSHIFT,
	CY86_CONVERT,
	CY86_IADD,
	CY86_ISUB,
	CY86_MUL,
	CY86_DIV,
	CY86_MOD,
	CY86_FADD,
	CY86_FSUB,
	CY86_FMUL,
	CY86_FDIV,
	CY86_EQ,
	CY86_NE,
	CY86_LT,
	CY86_GT,
	CY86_LE,
	CY86_GE,
	CY86_SYSCALL
};

enum Cy86ValueClass
{
	CY86_VALUE_BITS,
	CY86_VALUE_INTEGER,
	CY86_VALUE_SIGNED,
	CY86_VALUE_UNSIGNED,
	CY86_VALUE_FLOAT,
	CY86_VALUE_ADDRESS,
	CY86_VALUE_BOOLEAN
};

struct Cy86OperandConstraint
{
	unsigned width;
	Cy86ValueClass value_class;
	bool write;
	bool immediate_only;

	Cy86OperandConstraint();
};

struct Cy86Opcode
{
	Cy86Operation operation;
	std::array<Cy86OperandConstraint, 8> operands;
	unsigned operand_count;
	unsigned width;
	bool signed_operation;
	bool floating_operation;
	unsigned syscall_arguments;

	Cy86Opcode();
};

typedef std::uint16_t Cy86OpcodeId;

enum Cy86ValueKind
{
	CY86_LITERAL_VALUE,
	CY86_LABEL_VALUE
};

struct Cy86Value
{
	Cy86ValueKind kind;
	Cy86Literal literal;
	Cy86Identifier label;
	int adjustment_sign;
	Cy86Literal adjustment;

	Cy86Value();
};

enum Cy86AddressBase
{
	CY86_ADDRESS_REGISTER,
	CY86_ADDRESS_LITERAL,
	CY86_ADDRESS_LABEL
};

struct Cy86Address
{
	Cy86AddressBase base;
	Cy86Register reg;
	Cy86Literal literal;
	Cy86Identifier label;
	int displacement_sign;
	Cy86Literal displacement;

	Cy86Address();
};

enum Cy86OperandKind
{
	CY86_REGISTER_OPERAND,
	CY86_IMMEDIATE_OPERAND,
	CY86_MEMORY_OPERAND
};

struct Cy86Operand
{
	Cy86OperandKind kind;
	Cy86Register reg;
	Cy86Value immediate;
	Cy86Address memory;

	Cy86Operand();
};

enum Cy86StatementKind
{
	CY86_INSTRUCTION_STATEMENT,
	CY86_LITERAL_STATEMENT
};

struct Cy86Statement
{
	Cy86StatementKind kind;
	std::vector<Cy86Identifier> labels;
	Cy86OpcodeId opcode;
	std::vector<Cy86Operand> operands;
	Cy86Literal literal;

	Cy86Statement();
};

struct Cy86ProgramModel
{
	Cy86ProgramModel();
	void Clear();

	Cy86Identifiers identifiers;
	std::vector<Cy86Opcode> opcodes;
	std::vector<unsigned char> literal_bytes;
	std::vector<Cy86Statement> statements;
	Cy86Identifier start_label;
};

bool LookupCy86Register(const std::string& spelling, Cy86Register* reg);
bool LookupCy86Opcode(const std::string& spelling, Cy86Opcode* opcode);
bool Cy86LiteralIsIntegral(const Cy86Literal& literal);
bool Cy86LiteralIsSigned(const Cy86Literal& literal);
bool Cy86LiteralIsFloating(const Cy86Literal& literal);
std::size_t Cy86LiteralAlignment(const Cy86Literal& literal);
Cy86Literal NegateCy86Literal(Cy86ProgramModel& program,
	Cy86Literal literal);
std::uint64_t ConvertCy86LiteralToUnsigned(const Cy86Literal& literal,
	const std::vector<unsigned char>& bytes, unsigned width);

struct Cy86ParserState;

Cy86ParserState* CreateCy86Parser(Cy86ProgramModel& program,
	Cy86Stats* stats);
void DestroyCy86Parser(Cy86ParserState* parser);
void ParseCy86TranslationUnit(Cy86ParserState& parser,
	const std::string& path, const std::string& source,
	const PreprocessingOptions& options);
void FinishCy86Program(Cy86ParserState& parser);
void WriteCy86Executable(Cy86ProgramModel& program,
	const std::string& path, Cy86Stats* stats);

}
