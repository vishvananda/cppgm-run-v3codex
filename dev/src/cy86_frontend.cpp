#include "cy86_internal.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace cppgm
{

Cy86Identifiers::Cy86Identifiers() : bytes_(0)
{
	spellings_.push_back(std::string());
}

Cy86Identifier Cy86Identifiers::Intern(const std::string& spelling)
{
	std::unordered_map<std::string, Cy86Identifier>::const_iterator found =
		index_.find(spelling);
	if (found != index_.end()) return found->second;
	if (spellings_.size() >= std::numeric_limits<Cy86Identifier>::max())
		throw std::runtime_error("too many CY86 identifiers");
	const Cy86Identifier identifier =
		static_cast<Cy86Identifier>(spellings_.size());
	spellings_.push_back(spelling);
	index_.insert(std::make_pair(spellings_.back(), identifier));
	bytes_ += spelling.size();
	return identifier;
}

bool Cy86Identifiers::Find(const std::string& spelling,
	Cy86Identifier* identifier) const
{
	std::unordered_map<std::string, Cy86Identifier>::const_iterator found =
		index_.find(spelling);
	if (found == index_.end()) return false;
	*identifier = found->second;
	return true;
}

const std::string& Cy86Identifiers::Spelling(Cy86Identifier identifier) const
{
	if (identifier >= spellings_.size())
		throw std::logic_error("invalid CY86 identifier");
	return spellings_[identifier];
}

std::size_t Cy86Identifiers::Size() const
{
	return spellings_.size() - 1;
}

std::size_t Cy86Identifiers::Bytes() const
{
	return bytes_;
}

Cy86Register::Cy86Register() : bank(CY86_REG_INVALID), width(0) {}
Cy86Literal::Cy86Literal() : type(FT_VOID), elements(0), array(false) {}
Cy86OperandConstraint::Cy86OperandConstraint()
	: width(0), value_class(CY86_VALUE_BITS), write(false),
	  immediate_only(false) {}
Cy86Opcode::Cy86Opcode()
	: operation(CY86_MOVE), operand_count(0), width(0),
	  signed_operation(false), floating_operation(false),
	  syscall_arguments(0) {}
Cy86Value::Cy86Value()
	: kind(CY86_LITERAL_VALUE), label(0), adjustment_sign(0) {}
Cy86Address::Cy86Address()
	: base(CY86_ADDRESS_LITERAL), label(0), displacement_sign(0) {}
Cy86Operand::Cy86Operand() : kind(CY86_IMMEDIATE_OPERAND) {}
Cy86Statement::Cy86Statement() : kind(CY86_INSTRUCTION_STATEMENT) {}

namespace
{

Cy86OperandConstraint Constraint(unsigned width, Cy86ValueClass value_class,
	bool write = false, bool immediate_only = false)
{
	Cy86OperandConstraint result;
	result.width = width;
	result.value_class = value_class;
	result.write = write;
	result.immediate_only = immediate_only;
	return result;
}

void SetUnary(Cy86Opcode* opcode, Cy86Operation operation, unsigned width,
	Cy86ValueClass value_class)
{
	opcode->operation = operation;
	opcode->width = width;
	opcode->operand_count = 2;
	opcode->operands[0] = Constraint(width, value_class, true);
	opcode->operands[1] = Constraint(width, value_class);
}

void SetBinary(Cy86Opcode* opcode, Cy86Operation operation, unsigned width,
	Cy86ValueClass value_class)
{
	opcode->operation = operation;
	opcode->width = width;
	opcode->operand_count = 3;
	opcode->operands[0] = Constraint(width, value_class, true);
	opcode->operands[1] = Constraint(width, value_class);
	opcode->operands[2] = Constraint(width, value_class);
}

bool SplitWidth(const std::string& spelling, std::string* stem,
	unsigned* width)
{
	static const unsigned widths[] = { 80, 64, 32, 16, 8 };
	for (std::size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i)
	{
		const std::string suffix = std::to_string(widths[i]);
		if (spelling.size() <= suffix.size()) continue;
		if (spelling.compare(spelling.size() - suffix.size(), suffix.size(),
			suffix) != 0) continue;
		*stem = spelling.substr(0, spelling.size() - suffix.size());
		*width = widths[i];
		return true;
	}
	return false;
}

bool LookupFixedOpcode(const std::string& spelling, Cy86Opcode* opcode)
{
	if (spelling == "ret")
	{
		opcode->operation = CY86_RET;
		return true;
	}
	if (spelling == "jump" || spelling == "call")
	{
		opcode->operation = spelling == "jump" ? CY86_JUMP : CY86_CALL;
		opcode->operand_count = 1;
		opcode->operands[0] = Constraint(64, CY86_VALUE_ADDRESS);
		return true;
	}
	if (spelling == "jumpif")
	{
		opcode->operation = CY86_JUMP_IF;
		opcode->operand_count = 2;
		opcode->operands[0] = Constraint(8, CY86_VALUE_BOOLEAN);
		opcode->operands[1] = Constraint(64, CY86_VALUE_ADDRESS);
		return true;
	}
	if (spelling.size() == 8 && spelling.compare(0, 7, "syscall") == 0 &&
		spelling[7] >= '0' && spelling[7] <= '6')
	{
		opcode->operation = CY86_SYSCALL;
		opcode->syscall_arguments = spelling[7] - '0';
		opcode->operand_count = opcode->syscall_arguments + 2;
		for (unsigned i = 0; i < opcode->operand_count; ++i)
			opcode->operands[i] = Constraint(64, CY86_VALUE_BITS, i == 0);
		return true;
	}
	return false;
}

bool LookupConversion(const std::string& spelling, Cy86Opcode* opcode)
{
	const std::size_t marker = spelling.find("conv");
	if (marker == std::string::npos || marker == 0) return false;
	const std::string source = spelling.substr(0, marker);
	const std::string destination = spelling.substr(marker + 4);
	std::string source_stem;
	std::string destination_stem;
	unsigned source_width = 0;
	unsigned destination_width = 0;
	if (!SplitWidth(source, &source_stem, &source_width) ||
		!SplitWidth(destination, &destination_stem, &destination_width))
		return false;
	if ((source_stem != "s" && source_stem != "u" && source_stem != "f") ||
		(destination_stem != "s" && destination_stem != "u" &&
		 destination_stem != "f")) return false;
	if (source_stem == "f" && source_width != 32 && source_width != 64 &&
		source_width != 80) return false;
	if (destination_stem == "f" && destination_width != 32 &&
		destination_width != 64 && destination_width != 80) return false;
	if (source_stem != "f" && source_width == 80) return false;
	if (destination_stem != "f" && destination_width == 80) return false;
	if (source_stem == destination_stem && source_width == destination_width)
		return false;
	opcode->operation = CY86_CONVERT;
	opcode->operand_count = 2;
	opcode->width = destination_width;
	opcode->floating_operation = source_stem == "f" || destination_stem == "f";
	opcode->signed_operation = source_stem == "s";
	const Cy86ValueClass source_class = source_stem == "f" ? CY86_VALUE_FLOAT :
		(source_stem == "s" ? CY86_VALUE_SIGNED : CY86_VALUE_UNSIGNED);
	const Cy86ValueClass destination_class = destination_stem == "f" ?
		CY86_VALUE_FLOAT : (destination_stem == "s" ? CY86_VALUE_SIGNED :
		CY86_VALUE_UNSIGNED);
	opcode->operands[0] = Constraint(destination_width, destination_class, true);
	opcode->operands[1] = Constraint(source_width, source_class);
	return true;
}

bool LookupWidthOpcode(const std::string& stem, unsigned width,
	Cy86Opcode* opcode)
{
	if (stem == "data" && width != 80)
	{
		opcode->operation = CY86_DATA;
		opcode->width = width;
		opcode->operand_count = 1;
		opcode->operands[0] = Constraint(width, CY86_VALUE_BITS, false, true);
		return true;
	}
	if (stem == "move")
	{
		SetUnary(opcode, CY86_MOVE, width, CY86_VALUE_BITS);
		return true;
	}
	if (width == 80) return false;
	if (stem == "not") SetUnary(opcode, CY86_NOT, width, CY86_VALUE_INTEGER);
	else if (stem == "and") SetBinary(opcode, CY86_AND, width, CY86_VALUE_INTEGER);
	else if (stem == "or") SetBinary(opcode, CY86_OR, width, CY86_VALUE_INTEGER);
	else if (stem == "xor") SetBinary(opcode, CY86_XOR, width, CY86_VALUE_INTEGER);
	else if (stem == "iadd") SetBinary(opcode, CY86_IADD, width, CY86_VALUE_INTEGER);
	else if (stem == "isub") SetBinary(opcode, CY86_ISUB, width, CY86_VALUE_INTEGER);
	else return false;
	return true;
}

bool LookupShiftOpcode(const std::string& stem, unsigned width,
	Cy86Opcode* opcode)
{
	Cy86Operation operation;
	if (stem == "lshift") operation = CY86_LSHIFT;
	else if (stem == "srshift") operation = CY86_SRSHIFT;
	else if (stem == "urshift") operation = CY86_URSHIFT;
	else return false;
	SetBinary(opcode, operation, width,
		operation == CY86_SRSHIFT ? CY86_VALUE_SIGNED : CY86_VALUE_INTEGER);
	opcode->operands[2] = Constraint(8, CY86_VALUE_UNSIGNED);
	opcode->signed_operation = operation == CY86_SRSHIFT;
	return true;
}

bool LookupArithmeticOpcode(const std::string& stem, unsigned width,
	Cy86Opcode* opcode)
{
	if (stem.size() < 4) return false;
	const char category = stem[0];
	const std::string name = stem.substr(1);
	Cy86Operation operation;
	if (name == "mul") operation = category == 'f' ? CY86_FMUL : CY86_MUL;
	else if (name == "div") operation = category == 'f' ? CY86_FDIV : CY86_DIV;
	else if (name == "mod" && category != 'f') operation = CY86_MOD;
	else if (name == "add" && category == 'f') operation = CY86_FADD;
	else if (name == "sub" && category == 'f') operation = CY86_FSUB;
	else return false;
	if (category != 's' && category != 'u' && category != 'f') return false;
	if (category == 'f' && width != 32 && width != 64 && width != 80)
		return false;
	if (category != 'f' && width == 80) return false;
	const Cy86ValueClass value_class = category == 'f' ? CY86_VALUE_FLOAT :
		(category == 's' ? CY86_VALUE_SIGNED : CY86_VALUE_UNSIGNED);
	SetBinary(opcode, operation, width, value_class);
	opcode->signed_operation = category == 's';
	opcode->floating_operation = category == 'f';
	return true;
}

bool LookupComparisonOpcode(const std::string& stem, unsigned width,
	Cy86Opcode* opcode)
{
	if (stem.size() < 3) return false;
	const char category = stem[0];
	const std::string name = stem.substr(1);
	Cy86Operation operation;
	if (name == "eq") operation = CY86_EQ;
	else if (name == "ne") operation = CY86_NE;
	else if (name == "lt") operation = CY86_LT;
	else if (name == "gt") operation = CY86_GT;
	else if (name == "le") operation = CY86_LE;
	else if (name == "ge") operation = CY86_GE;
	else return false;
	if ((operation == CY86_EQ || operation == CY86_NE) &&
		category != 'i' && category != 'f') return false;
	if (operation != CY86_EQ && operation != CY86_NE && category != 's' &&
		category != 'u' && category != 'f') return false;
	if (category == 'f' && width != 32 && width != 64 && width != 80)
		return false;
	if (category != 'f' && width == 80) return false;
	const Cy86ValueClass value_class = category == 'f' ? CY86_VALUE_FLOAT :
		(category == 's' ? CY86_VALUE_SIGNED :
		 category == 'u' ? CY86_VALUE_UNSIGNED : CY86_VALUE_INTEGER);
	opcode->operation = operation;
	opcode->width = width;
	opcode->operand_count = 3;
	opcode->operands[0] = Constraint(8, CY86_VALUE_BOOLEAN, true);
	opcode->operands[1] = Constraint(width, value_class);
	opcode->operands[2] = Constraint(width, value_class);
	opcode->signed_operation = category == 's';
	opcode->floating_operation = category == 'f';
	return true;
}

std::size_t ScalarTypeSize(FundamentalType type)
{
	switch (type)
	{
	case FT_SIGNED_CHAR: case FT_UNSIGNED_CHAR: case FT_CHAR: case FT_BOOL:
		return 1;
	case FT_SHORT_INT: case FT_UNSIGNED_SHORT_INT: case FT_CHAR16_T:
		return 2;
	case FT_INT: case FT_UNSIGNED_INT: case FT_WCHAR_T: case FT_CHAR32_T:
	case FT_FLOAT:
		return 4;
	case FT_LONG_INT: case FT_LONG_LONG_INT: case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT: case FT_DOUBLE: case FT_NULLPTR_T:
		return 8;
	case FT_LONG_DOUBLE:
		return 16;
	case FT_VOID:
		break;
	}
	throw std::runtime_error("invalid CY86 literal type");
}

enum ParsedTokenKind { TOKEN_IDENTIFIER, TOKEN_LITERAL, TOKEN_SIMPLE };

struct ParsedToken
{
	ParsedTokenKind kind;
	Cy86Identifier identifier;
	Cy86Literal literal;
	SimpleTokenKind simple;

	ParsedToken() : kind(TOKEN_SIMPLE), identifier(0), simple(OP_SEMICOLON) {}
};

class StatementCursor
{
public:
	explicit StatementCursor(const std::vector<ParsedToken>& tokens)
		: tokens_(tokens), position_(0) {}
	bool End() const { return position_ == tokens_.size(); }
	const ParsedToken& Peek() const
	{
		if (End()) throw std::runtime_error("unexpected end of CY86 statement");
		return tokens_[position_];
	}
	const ParsedToken& Take()
	{
		const ParsedToken& result = Peek();
		++position_;
		return result;
	}
	bool TakeSimple(SimpleTokenKind kind)
	{
		if (End() || Peek().kind != TOKEN_SIMPLE || Peek().simple != kind)
			return false;
		++position_;
		return true;
	}

private:
	const std::vector<ParsedToken>& tokens_;
	std::size_t position_;
};

Cy86Literal CopyLiteral(FundamentalType type, const void* data,
	std::size_t size, bool array, std::size_t elements)
{
	Cy86Literal result;
	result.type = type;
	result.elements = elements;
	result.array = array;
	const unsigned char* begin = static_cast<const unsigned char*>(data);
	result.bytes.assign(begin, begin + size);
	return result;
}

void Require(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

}

bool LookupCy86Register(const std::string& spelling, Cy86Register* reg)
{
	if (spelling == "sp" || spelling == "bp")
	{
		reg->bank = spelling == "sp" ? CY86_REG_SP : CY86_REG_BP;
		reg->width = 64;
		return true;
	}
	if (spelling.size() < 2) return false;
	switch (spelling[0])
	{
	case 'x': reg->bank = CY86_REG_X; break;
	case 'y': reg->bank = CY86_REG_Y; break;
	case 'z': reg->bank = CY86_REG_Z; break;
	case 't': reg->bank = CY86_REG_T; break;
	default: return false;
	}
	const std::string suffix = spelling.substr(1);
	if (suffix == "8") reg->width = 8;
	else if (suffix == "16") reg->width = 16;
	else if (suffix == "32") reg->width = 32;
	else if (suffix == "64") reg->width = 64;
	else return false;
	return true;
}

bool LookupCy86Opcode(const std::string& spelling, Cy86Opcode* opcode)
{
	*opcode = Cy86Opcode();
	if (LookupFixedOpcode(spelling, opcode)) return true;
	if (LookupConversion(spelling, opcode)) return true;
	std::string stem;
	unsigned width = 0;
	if (!SplitWidth(spelling, &stem, &width)) return false;
	return LookupWidthOpcode(stem, width, opcode) ||
		LookupShiftOpcode(stem, width, opcode) ||
		LookupArithmeticOpcode(stem, width, opcode) ||
		LookupComparisonOpcode(stem, width, opcode);
}

bool Cy86LiteralIsIntegral(const Cy86Literal& literal)
{
	return !literal.array && literal.type != FT_FLOAT &&
		literal.type != FT_DOUBLE && literal.type != FT_LONG_DOUBLE &&
		literal.type != FT_VOID;
}

bool Cy86LiteralIsSigned(const Cy86Literal& literal)
{
	switch (literal.type)
	{
	case FT_SIGNED_CHAR: case FT_SHORT_INT: case FT_INT: case FT_LONG_INT:
	case FT_LONG_LONG_INT: case FT_WCHAR_T: case FT_CHAR:
		return true;
	default:
		return false;
	}
}

bool Cy86LiteralIsFloating(const Cy86Literal& literal)
{
	return !literal.array && (literal.type == FT_FLOAT ||
		literal.type == FT_DOUBLE || literal.type == FT_LONG_DOUBLE);
}

std::size_t Cy86LiteralAlignment(const Cy86Literal& literal)
{
	if (literal.array)
	{
		Require(literal.elements != 0 && literal.bytes.size() % literal.elements == 0,
			"invalid literal array representation");
		return literal.bytes.size() / literal.elements;
	}
	return ScalarTypeSize(literal.type);
}

Cy86Literal NegateCy86Literal(const Cy86Literal& literal)
{
	Require(!literal.array && (Cy86LiteralIsIntegral(literal) ||
		Cy86LiteralIsFloating(literal)), "only arithmetic literals may be negated");
	Cy86Literal result = literal;
	if (Cy86LiteralIsIntegral(result))
	{
		unsigned carry = 1;
		for (std::size_t i = 0; i < result.bytes.size(); ++i)
		{
			const unsigned value = static_cast<unsigned>(~result.bytes[i] & 0xff) + carry;
			result.bytes[i] = static_cast<unsigned char>(value);
			carry = value >> 8;
		}
	}
	else
	{
		const std::size_t sign_byte = result.type == FT_FLOAT ? 3 :
			(result.type == FT_DOUBLE ? 7 : 9);
		Require(sign_byte < result.bytes.size(), "invalid floating representation");
		result.bytes[sign_byte] ^= 0x80;
	}
	return result;
}

std::uint64_t ConvertCy86LiteralToUnsigned(const Cy86Literal& literal,
	unsigned width)
{
	Require(Cy86LiteralIsIntegral(literal), "integral literal required");
	Require(width == 8 || width == 16 || width == 32 || width == 64,
		"unsupported integral width");
	const std::size_t output_size = width / 8;
	const std::size_t copied = std::min(output_size, literal.bytes.size());
	std::uint64_t result = 0;
	for (std::size_t i = 0; i < copied; ++i)
		result |= static_cast<std::uint64_t>(literal.bytes[i]) << (i * 8);
	if (copied < output_size && Cy86LiteralIsSigned(literal) && copied != 0 &&
		(literal.bytes[copied - 1] & 0x80) != 0)
	{
		for (std::size_t i = copied; i < output_size; ++i)
			result |= static_cast<std::uint64_t>(0xff) << (i * 8);
	}
	return result;
}

namespace
{

class Cy86TokenParser : public IPostTokenStream
{
public:
	Cy86TokenParser(Cy86ProgramModel& program, Cy86Stats* stats)
		: program_(program), stats_(stats) {}

	void EmitInvalid(const std::string&) { InvalidToken(); }
	void EmitSimple(const std::string&, SimpleTokenKind kind)
	{
		CountToken();
		if (kind == OP_SEMICOLON)
		{
			ParseStatement();
			return;
		}
		ParsedToken token;
		token.kind = TOKEN_SIMPLE;
		token.simple = kind;
		tokens_.push_back(token);
		UpdatePeak();
	}
	void EmitIdentifier(const std::string& source)
	{
		CountToken();
		ParsedToken token;
		token.kind = TOKEN_IDENTIFIER;
		token.identifier = program_.identifiers.Intern(source);
		tokens_.push_back(token);
		UpdatePeak();
	}
	void EmitLiteral(const std::string&, FundamentalType type, const void* data,
		std::size_t size)
	{
		AddLiteral(CopyLiteral(type, data, size, false, 0));
	}
	void EmitLiteralArray(const std::string&, std::size_t elements,
		FundamentalType type, const void* data, std::size_t size)
	{
		AddLiteral(CopyLiteral(type, data, size, true, elements));
	}
	void EmitUserDefinedCharacter(const std::string&, const std::string&,
		FundamentalType, const void*, std::size_t) { InvalidToken(); }
	void EmitUserDefinedString(const std::string&, const std::string&,
		std::size_t, FundamentalType, const void*, std::size_t) { InvalidToken(); }
	void EmitUserDefinedInteger(const std::string&, const std::string&,
		const std::string&) { InvalidToken(); }
	void EmitUserDefinedFloating(const std::string&, const std::string&,
		const std::string&) { InvalidToken(); }
	void EmitEof() {}

	void Finish()
	{
		Require(tokens_.empty(), "unterminated CY86 statement");
		for (std::size_t i = 0; i < referenced_labels_.size(); ++i)
			Require(program_.label_statements.count(referenced_labels_[i]) != 0,
				"undefined CY86 label");
	}

private:
	void InvalidToken() const
	{
		throw std::runtime_error("invalid token in CY86 program");
	}
	void CountToken()
	{
		if (stats_) ++stats_->tokens;
	}
	void UpdatePeak()
	{
		if (stats_) stats_->peak_statement_tokens = std::max(
			stats_->peak_statement_tokens, tokens_.size());
	}
	void AddLiteral(const Cy86Literal& literal)
	{
		CountToken();
		ParsedToken token;
		token.kind = TOKEN_LITERAL;
		token.literal = literal;
		tokens_.push_back(token);
		UpdatePeak();
	}

	Cy86Value ParseParenthesizedValue(StatementCursor& cursor)
	{
		Cy86Value value;
		if (cursor.TakeSimple(OP_MINUS))
		{
			Require(!cursor.End() && cursor.Peek().kind == TOKEN_LITERAL,
				"literal required after minus");
			value.literal = NegateCy86Literal(cursor.Take().literal);
			Require(cursor.TakeSimple(OP_RPAREN), "missing closing parenthesis");
			return value;
		}
		Require(!cursor.End(), "empty immediate expression");
		const ParsedToken& first = cursor.Take();
		if (first.kind == TOKEN_LITERAL)
		{
			value.literal = first.literal;
		}
		else
		{
			Require(first.kind == TOKEN_IDENTIFIER, "invalid immediate expression");
			Cy86Register reg;
			Require(!LookupCy86Register(program_.identifiers.Spelling(first.identifier),
				&reg), "register cannot be used as an immediate label");
			value.kind = CY86_LABEL_VALUE;
			value.label = first.identifier;
			referenced_labels_.push_back(value.label);
			if (cursor.TakeSimple(OP_PLUS)) value.adjustment_sign = 1;
			else if (cursor.TakeSimple(OP_MINUS)) value.adjustment_sign = -1;
			if (value.adjustment_sign != 0)
			{
				Require(!cursor.End() && cursor.Peek().kind == TOKEN_LITERAL &&
					Cy86LiteralIsIntegral(cursor.Peek().literal),
					"integral label adjustment required");
				value.adjustment = cursor.Take().literal;
			}
		}
		Require(cursor.TakeSimple(OP_RPAREN), "missing closing parenthesis");
		return value;
	}

	Cy86Address ParseMemory(StatementCursor& cursor)
	{
		Cy86Address address;
		Require(!cursor.End(), "empty memory expression");
		const ParsedToken& base = cursor.Take();
		if (base.kind == TOKEN_LITERAL)
		{
			Require(Cy86LiteralIsIntegral(base.literal),
				"integral memory address required");
			address.base = CY86_ADDRESS_LITERAL;
			address.literal = base.literal;
		}
		else
		{
			Require(base.kind == TOKEN_IDENTIFIER, "invalid memory address");
			Cy86Register reg;
			if (LookupCy86Register(program_.identifiers.Spelling(base.identifier), &reg))
			{
				Require(reg.width == 64, "memory address register must be 64-bit");
				address.base = CY86_ADDRESS_REGISTER;
				address.reg = reg;
			}
			else
			{
				address.base = CY86_ADDRESS_LABEL;
				address.label = base.identifier;
				referenced_labels_.push_back(address.label);
			}
			if (cursor.TakeSimple(OP_PLUS)) address.displacement_sign = 1;
			else if (cursor.TakeSimple(OP_MINUS)) address.displacement_sign = -1;
			if (address.displacement_sign != 0)
			{
				Require(!cursor.End() && cursor.Peek().kind == TOKEN_LITERAL &&
					Cy86LiteralIsIntegral(cursor.Peek().literal),
					"integral memory displacement required");
				address.displacement = cursor.Take().literal;
			}
		}
		Require(cursor.TakeSimple(OP_RSQUARE), "missing closing square bracket");
		return address;
	}

	Cy86Operand ParseOperand(StatementCursor& cursor)
	{
		Require(!cursor.End(), "missing CY86 operand");
		Cy86Operand operand;
		if (cursor.TakeSimple(OP_LSQUARE))
		{
			operand.kind = CY86_MEMORY_OPERAND;
			operand.memory = ParseMemory(cursor);
			return operand;
		}
		if (cursor.TakeSimple(OP_LPAREN))
		{
			operand.kind = CY86_IMMEDIATE_OPERAND;
			operand.immediate = ParseParenthesizedValue(cursor);
			return operand;
		}
		const ParsedToken& token = cursor.Take();
		if (token.kind == TOKEN_LITERAL)
		{
			operand.kind = CY86_IMMEDIATE_OPERAND;
			operand.immediate.literal = token.literal;
			return operand;
		}
		Require(token.kind == TOKEN_IDENTIFIER, "invalid CY86 operand");
		if (LookupCy86Register(program_.identifiers.Spelling(token.identifier),
			&operand.reg))
		{
			operand.kind = CY86_REGISTER_OPERAND;
		}
		else
		{
			operand.kind = CY86_IMMEDIATE_OPERAND;
			operand.immediate.kind = CY86_LABEL_VALUE;
			operand.immediate.label = token.identifier;
			referenced_labels_.push_back(token.identifier);
		}
		return operand;
	}

	void ValidateOperand(const Cy86Operand& operand,
		const Cy86OperandConstraint& constraint) const
	{
		if (constraint.immediate_only)
			Require(operand.kind == CY86_IMMEDIATE_OPERAND,
				"opcode requires an immediate operand");
		if (constraint.write)
			Require(operand.kind != CY86_IMMEDIATE_OPERAND,
				"write operand cannot be immediate");
		if (operand.kind == CY86_REGISTER_OPERAND)
		{
			Require(operand.reg.width == constraint.width,
				"CY86 register width mismatch");
			return;
		}
		if (operand.kind == CY86_MEMORY_OPERAND) return;
		if (operand.immediate.kind == CY86_LABEL_VALUE)
		{
			Require(constraint.value_class != CY86_VALUE_FLOAT,
				"label cannot be a floating operand");
			return;
		}
		const Cy86Literal& literal = operand.immediate.literal;
		if (constraint.value_class == CY86_VALUE_FLOAT)
			Require(Cy86LiteralIsFloating(literal), "floating literal required");
		else if (constraint.value_class == CY86_VALUE_ADDRESS)
			Require(Cy86LiteralIsIntegral(literal), "integral address required");
		else if (constraint.value_class != CY86_VALUE_BITS)
			Require(Cy86LiteralIsIntegral(literal) || literal.array,
				"integral or raw-byte literal required");
	}

	void ParseInstruction(StatementCursor& cursor, Cy86Statement* statement)
	{
		Require(!cursor.End() && cursor.Peek().kind == TOKEN_IDENTIFIER,
			"CY86 opcode required");
		const Cy86Identifier opcode_id = cursor.Take().identifier;
		const std::string& opcode_spelling =
			program_.identifiers.Spelling(opcode_id);
		if (!LookupCy86Opcode(opcode_spelling, &statement->opcode))
			throw std::runtime_error("unknown CY86 opcode: " + opcode_spelling);
		statement->operands.reserve(statement->opcode.operand_count);
		for (unsigned i = 0; i < statement->opcode.operand_count; ++i)
		{
			statement->operands.push_back(ParseOperand(cursor));
			ValidateOperand(statement->operands.back(), statement->opcode.operands[i]);
		}
		Require(cursor.End(), "too many tokens in CY86 instruction");
	}

	void ParseStatement()
	{
		Require(!tokens_.empty(), "empty CY86 statement");
		StatementCursor cursor(tokens_);
		Cy86Statement statement;
		while (!cursor.End() && cursor.Peek().kind == TOKEN_IDENTIFIER)
		{
			const Cy86Identifier label = cursor.Peek().identifier;
			const ParsedToken& ignored = cursor.Take();
			(void)ignored;
			if (!cursor.TakeSimple(OP_COLON))
			{
				// The identifier belongs to the opcode. Reparse from the beginning of
				// the non-label suffix using a compact temporary suffix.
				std::vector<ParsedToken> suffix;
				suffix.push_back(tokens_[statement.labels.size() * 2]);
				const std::size_t begin = statement.labels.size() * 2 + 1;
				suffix.insert(suffix.end(), tokens_.begin() + begin, tokens_.end());
				StatementCursor instruction_cursor(suffix);
				ParseInstruction(instruction_cursor, &statement);
				CommitStatement(statement);
				tokens_.clear();
				return;
			}
			Cy86Register reg;
			Cy86Opcode opcode;
			const std::string& spelling = program_.identifiers.Spelling(label);
			Require(!LookupCy86Register(spelling, &reg) &&
				!LookupCy86Opcode(spelling, &opcode),
				"label collides with a CY86 register or opcode");
			statement.labels.push_back(label);
		}
		Require(!cursor.End(), "label must name a statement");
		if (cursor.Peek().kind == TOKEN_LITERAL)
		{
			statement.kind = CY86_LITERAL_STATEMENT;
			statement.literal = cursor.Take().literal;
			Require(cursor.End(), "invalid literal statement");
		}
		else if (cursor.TakeSimple(OP_MINUS))
		{
			Require(!cursor.End() && cursor.Peek().kind == TOKEN_LITERAL,
				"literal required after minus");
			statement.kind = CY86_LITERAL_STATEMENT;
			statement.literal = NegateCy86Literal(cursor.Take().literal);
			Require(cursor.End(), "invalid negated literal statement");
		}
		else
		{
			ParseInstruction(cursor, &statement);
		}
		CommitStatement(statement);
		tokens_.clear();
	}

	void CommitStatement(const Cy86Statement& statement)
	{
		const std::size_t statement_index = program_.statements.size();
		for (std::size_t i = 0; i < statement.labels.size(); ++i)
		{
			Require(program_.label_statements.insert(std::make_pair(
				statement.labels[i], statement_index)).second,
				"duplicate CY86 label");
		}
		program_.statements.push_back(statement);
		if (stats_)
		{
			++stats_->statements;
			stats_->labels += statement.labels.size();
		}
	}

	Cy86ProgramModel& program_;
	Cy86Stats* stats_;
	std::vector<ParsedToken> tokens_;
	std::vector<Cy86Identifier> referenced_labels_;
};

}

struct Cy86ParserState
{
	explicit Cy86ParserState(Cy86ProgramModel& program, Cy86Stats* stats)
		: tokens(program, stats) {}
	Cy86TokenParser tokens;
};

Cy86ParserState* CreateCy86Parser(Cy86ProgramModel& program, Cy86Stats* stats)
{
	return new Cy86ParserState(program, stats);
}

void DestroyCy86Parser(Cy86ParserState* parser)
{
	delete parser;
}

void ParseCy86TranslationUnit(Cy86ParserState& parser,
	const std::string& path, const std::string& source,
	const PreprocessingOptions& options)
{
	PreprocessFile(path, source, parser.tokens, options);
}

void FinishCy86Program(Cy86ParserState& parser)
{
	parser.tokens.Finish();
}

}
