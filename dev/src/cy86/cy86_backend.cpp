#include "cy86/cy86_internal.h"

#include "cy86/errors.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sys/stat.h>

namespace cppgm
{
namespace
{

const std::uint64_t kLoadAddress = 0x400000;
const std::size_t kElfHeaderSize = 64;
const std::size_t kProgramHeaderSize = 56;
const std::size_t kContentOffset = kElfHeaderSize + kProgramHeaderSize;

enum X86Register
{
	RAX = 0, RCX = 1, RDX = 2, RBX = 3,
	RSP = 4, RBP = 5, RSI = 6, RDI = 7,
	R8 = 8, R9 = 9, R10 = 10, R11 = 11,
	R12 = 12, R13 = 13, R14 = 14, R15 = 15
};

struct AddressFixup
{
	std::size_t offset;
	Cy86Identifier label;
	std::uint64_t addend;
	unsigned width;
};

class NativeBuffer
{
public:
	void Byte(unsigned value)
	{
		bytes_.push_back(static_cast<unsigned char>(value));
	}
	void Zeros(std::size_t count)
	{
		bytes_.insert(bytes_.end(), count, 0);
	}
	void Little(std::uint64_t value, unsigned bytes)
	{
		for (unsigned i = 0; i < bytes; ++i)
			Byte(static_cast<unsigned>(value >> (i * 8)));
	}
	std::size_t Reserve32()
	{
		const std::size_t offset = bytes_.size();
		Zeros(4);
		return offset;
	}
	void PatchLittle(std::size_t offset, std::uint64_t value, unsigned bytes)
	{
		if (offset + bytes > bytes_.size())
			cy86_errors::ThrowInternal("invalid machine-code patch");
		for (unsigned i = 0; i < bytes; ++i)
			bytes_[offset + i] = static_cast<unsigned char>(value >> (i * 8));
	}
	void PatchRelative32(std::size_t offset, std::size_t destination)
	{
		const std::int64_t delta = static_cast<std::int64_t>(destination) -
			static_cast<std::int64_t>(offset + 4);
		PatchLittle(offset, static_cast<std::uint32_t>(delta), 4);
	}
	void LabelImmediate(Cy86Identifier label, std::uint64_t addend,
		unsigned width)
	{
		AddressFixup fixup;
		fixup.offset = bytes_.size();
		fixup.label = label;
		fixup.addend = addend;
		fixup.width = width;
		fixups_.push_back(fixup);
		Zeros(width / 8);
	}
	std::vector<unsigned char>& Bytes() { return bytes_; }
	const std::vector<unsigned char>& Bytes() const { return bytes_; }
	const std::vector<AddressFixup>& Fixups() const { return fixups_; }
	void ReleaseFixups()
	{
		std::vector<AddressFixup>().swap(fixups_);
	}

private:
	std::vector<unsigned char> bytes_;
	std::vector<AddressFixup> fixups_;
};

int NativeRegister(const Cy86Register& reg)
{
	switch (reg.bank)
	{
	case CY86_REG_SP: return RSP;
	case CY86_REG_BP: return RBP;
	case CY86_REG_X: return R12;
	case CY86_REG_Y: return R13;
	case CY86_REG_Z: return R14;
	case CY86_REG_T: return R15;
	case CY86_REG_INVALID: break;
	}
	cy86_errors::ThrowInternal("invalid CY86 register in backend");
}

void EmitSizePrefix(NativeBuffer& output, unsigned width)
{
	if (width == 16) output.Byte(0x66);
}

void EmitRex(NativeBuffer& output, bool wide, int reg, int rm,
	bool force = false)
{
	const unsigned rex = 0x40 | (wide ? 8 : 0) |
		((static_cast<unsigned>(reg) >> 3) << 2) |
		(static_cast<unsigned>(rm) >> 3);
	if (rex != 0x40 || force) output.Byte(rex);
}

void EmitModRm(NativeBuffer& output, unsigned mod, unsigned reg, unsigned rm)
{
	output.Byte((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

void EmitMoveRegister(NativeBuffer& output, int destination, int source,
	unsigned width)
{
	EmitSizePrefix(output, width);
	EmitRex(output, width == 64, destination, source, width == 8);
	output.Byte(width == 8 ? 0x8a : 0x8b);
	EmitModRm(output, 3, destination, source);
}

void EmitMoveFromMemory(NativeBuffer& output, int destination, int base,
	unsigned width)
{
	EmitSizePrefix(output, width);
	EmitRex(output, width == 64, destination, base, width == 8);
	output.Byte(width == 8 ? 0x8a : 0x8b);
	EmitModRm(output, 0, destination, base);
}

void EmitMoveToMemory(NativeBuffer& output, int base, int source,
	unsigned width)
{
	EmitSizePrefix(output, width);
	EmitRex(output, width == 64, source, base, width == 8);
	output.Byte(width == 8 ? 0x88 : 0x89);
	EmitModRm(output, 0, source, base);
}

void EmitXor32(NativeBuffer& output, int reg)
{
	EmitRex(output, false, reg, reg);
	output.Byte(0x31);
	EmitModRm(output, 3, reg, reg);
}

void EmitImmediate64(NativeBuffer& output, int destination,
	std::uint64_t value)
{
	EmitRex(output, true, 0, destination);
	output.Byte(0xb8 + (destination & 7));
	output.Little(value, 8);
}

void EmitLabel64(NativeBuffer& output, int destination, Cy86Identifier label,
	std::uint64_t addend)
{
	EmitRex(output, true, 0, destination);
	output.Byte(0xb8 + (destination & 7));
	output.LabelImmediate(label, addend, 64);
}

void EmitZeroExtendedRegister(NativeBuffer& output, int destination,
	int source, unsigned width)
{
	if (width < 32) EmitXor32(output, destination);
	EmitMoveRegister(output, destination, source, width);
}

void EmitZeroExtendedMemory(NativeBuffer& output, int destination, int base,
	unsigned width)
{
	if (width < 32) EmitXor32(output, destination);
	EmitMoveFromMemory(output, destination, base, width);
}

void EmitAluRegister(NativeBuffer& output, unsigned opcode, int destination,
	int source)
{
	EmitRex(output, true, source, destination);
	output.Byte(opcode);
	EmitModRm(output, 3, source, destination);
}

void EmitSignExtend(NativeBuffer& output, int reg, unsigned width)
{
	if (width == 64) return;
	EmitRex(output, true, reg, reg);
	if (width == 32)
	{
		output.Byte(0x63);
	}
	else
	{
		output.Byte(0x0f);
		output.Byte(width == 8 ? 0xbe : 0xbf);
	}
	EmitModRm(output, 3, reg, reg);
}

void EmitX87Memory(NativeBuffer& output, unsigned opcode,
	unsigned extension, int base)
{
	EmitRex(output, false, 0, base);
	output.Byte(opcode);
	EmitModRm(output, 0, extension, base);
}

void EmitX87Load(NativeBuffer& output, unsigned width, int base)
{
	if (width == 32) EmitX87Memory(output, 0xd9, 0, base);
	else if (width == 64) EmitX87Memory(output, 0xdd, 0, base);
	else if (width == 80) EmitX87Memory(output, 0xdb, 5, base);
	else cy86_errors::ThrowInternal("invalid x87 load width");
}

void EmitX87StorePop(NativeBuffer& output, unsigned width, int base)
{
	if (width == 32) EmitX87Memory(output, 0xd9, 3, base);
	else if (width == 64) EmitX87Memory(output, 0xdd, 3, base);
	else if (width == 80) EmitX87Memory(output, 0xdb, 7, base);
	else cy86_errors::ThrowInternal("invalid x87 store width");
}

void EmitSetCondition(NativeBuffer& output, unsigned opcode, int destination)
{
	EmitRex(output, false, 0, destination, destination >= RSP);
	output.Byte(0x0f);
	output.Byte(opcode);
	EmitModRm(output, 3, 0, destination);
}

const unsigned char* LiteralData(const Cy86Literal& literal,
	const std::vector<unsigned char>& bytes)
{
	if (literal.offset > bytes.size() || literal.size > bytes.size() - literal.offset)
		cy86_errors::ThrowInternal("invalid CY86 literal storage range");
	return literal.size == 0 ? 0 : &bytes[literal.offset];
}

std::uint64_t RawLiteralBits(const Cy86Literal& literal,
	const std::vector<unsigned char>& bytes, unsigned width)
{
	if (width != 8 && width != 16 && width != 32 && width != 64)
		cy86_errors::ThrowInternal("invalid raw literal width");
	const std::size_t output_size = width / 8;
	const unsigned char* data = LiteralData(literal, bytes);
	const std::size_t byte_count = std::min<std::size_t>(output_size,
		literal.size);
	std::uint64_t result = 0;
	for (std::size_t i = 0; i < byte_count; ++i)
		result |= static_cast<std::uint64_t>(data[i]) << (i * 8);
	if (byte_count < output_size && Cy86LiteralIsIntegral(literal) &&
		Cy86LiteralIsSigned(literal) && byte_count != 0 &&
		(data[byte_count - 1] & 0x80) != 0)
	{
		for (std::size_t i = byte_count; i < output_size; ++i)
			result |= static_cast<std::uint64_t>(0xff) << (i * 8);
	}
	return result;
}

std::array<unsigned char, 10> RawLiteral80(const Cy86Literal& literal,
	const std::vector<unsigned char>& bytes)
{
	const unsigned char* data = LiteralData(literal, bytes);
	const unsigned char extension = Cy86LiteralIsIntegral(literal) &&
		Cy86LiteralIsSigned(literal) && literal.size != 0 &&
		(data[literal.size - 1] & 0x80) != 0 ? 0xff : 0;
	std::array<unsigned char, 10> result;
	result.fill(extension);
	if (literal.size != 0)
		std::copy(data, data + std::min<std::size_t>(result.size(), literal.size),
			result.begin());
	return result;
}

std::uint64_t AdjustmentValue(const Cy86Literal& literal,
	const std::vector<unsigned char>& bytes, int sign)
{
	if (sign == 0) return 0;
	const std::uint64_t value = ConvertCy86LiteralToUnsigned(literal, bytes, 64);
	return sign > 0 ? value : 0 - value;
}

class InstructionEmitter
{
public:
	InstructionEmitter(NativeBuffer& output,
		const std::vector<Cy86Opcode>& opcodes,
		const std::vector<unsigned char>& literal_bytes)
		: output_(output), opcodes_(opcodes), literal_bytes_(literal_bytes) {}

	void Emit(const Cy86Statement& statement)
	{
		const Cy86Opcode& opcode = Opcode(statement);
		switch (opcode.operation)
		{
		case CY86_DATA: EmitData(statement); break;
		case CY86_MOVE: EmitMove(statement); break;
		case CY86_JUMP: EmitIndirectControl(statement.operands[0], false); break;
		case CY86_CALL: EmitIndirectControl(statement.operands[0], true); break;
		case CY86_RET: output_.Byte(0xc3); break;
		case CY86_JUMP_IF: EmitJumpIf(statement); break;
		case CY86_NOT: EmitNot(statement); break;
		case CY86_AND: EmitSimpleBinary(statement, 0x21); break;
		case CY86_OR: EmitSimpleBinary(statement, 0x09); break;
		case CY86_XOR: EmitSimpleBinary(statement, 0x31); break;
		case CY86_IADD: EmitSimpleBinary(statement, 0x01); break;
		case CY86_ISUB: EmitSimpleBinary(statement, 0x29); break;
		case CY86_LSHIFT: EmitShift(statement, 4); break;
		case CY86_SRSHIFT: EmitShift(statement, 7); break;
		case CY86_URSHIFT: EmitShift(statement, 5); break;
		case CY86_MUL: EmitMultiply(statement); break;
		case CY86_DIV: EmitDivide(statement, false); break;
		case CY86_MOD: EmitDivide(statement, true); break;
		case CY86_EQ: case CY86_NE: case CY86_LT: case CY86_GT:
		case CY86_LE: case CY86_GE: EmitComparison(statement); break;
		case CY86_SYSCALL: EmitSyscall(statement); break;
		case CY86_CONVERT: EmitConversion(statement); break;
		case CY86_FADD: case CY86_FSUB: case CY86_FMUL: case CY86_FDIV:
			EmitFloatingArithmetic(statement); break;
		}
	}

private:
	const Cy86Opcode& Opcode(const Cy86Statement& statement) const
	{
		if (statement.opcode == 0 || statement.opcode >= opcodes_.size())
			cy86_errors::ThrowInternal("invalid CY86 opcode identity in backend");
		return opcodes_[statement.opcode];
	}

	void EmitAddress(const Cy86Address& address)
	{
		if (address.base == CY86_ADDRESS_REGISTER)
			EmitMoveRegister(output_, R11, NativeRegister(address.reg), 64);
		else if (address.base == CY86_ADDRESS_LITERAL)
			EmitImmediate64(output_, R11,
				RawLiteralBits(address.literal, literal_bytes_, 64));
		else
			EmitLabel64(output_, R11, address.label, 0);
		if (address.displacement_sign != 0)
		{
			EmitImmediate64(output_, RBX, AdjustmentValue(address.displacement,
				literal_bytes_, address.displacement_sign));
			EmitAluRegister(output_, 0x01, R11, RBX);
		}
	}

	void EmitLoad(const Cy86Operand& operand, unsigned width, int destination)
	{
		if (width == 80)
			cy86_errors::ThrowInternal("80-bit operand requires the floating backend");
		if (operand.kind == CY86_REGISTER_OPERAND)
		{
			EmitZeroExtendedRegister(output_, destination,
				NativeRegister(operand.reg), width);
		}
		else if (operand.kind == CY86_MEMORY_OPERAND)
		{
			EmitAddress(operand.memory);
			EmitZeroExtendedMemory(output_, destination, R11, width);
		}
		else if (operand.immediate.kind == CY86_LITERAL_VALUE)
		{
			EmitImmediate64(output_, destination,
				RawLiteralBits(operand.immediate.literal, literal_bytes_, width));
		}
		else
		{
			EmitLabel64(output_, destination, operand.immediate.label,
				AdjustmentValue(operand.immediate.adjustment,
					literal_bytes_, operand.immediate.adjustment_sign));
		}
	}

	void EmitStore(const Cy86Operand& operand, unsigned width, int source)
	{
		if (operand.kind == CY86_REGISTER_OPERAND)
			EmitMoveRegister(output_, NativeRegister(operand.reg), source, width);
		else if (operand.kind == CY86_MEMORY_OPERAND)
		{
			EmitAddress(operand.memory);
			EmitMoveToMemory(output_, R11, source, width);
		}
		else
			cy86_errors::ThrowInternal("backend received an immediate write operand");
	}

	void EmitScratchAddress(std::int64_t displacement)
	{
		EmitMoveRegister(output_, R11, RSP, 64);
		EmitImmediate64(output_, RBX, static_cast<std::uint64_t>(displacement));
		EmitAluRegister(output_, 0x01, R11, RBX);
	}

	void EmitStoreScratch(int source, unsigned width, std::int64_t displacement)
	{
		EmitScratchAddress(displacement);
		EmitMoveToMemory(output_, R11, source, width);
	}

	void EmitLoadScratch(int destination, unsigned width,
		std::int64_t displacement)
	{
		EmitScratchAddress(displacement);
		EmitZeroExtendedMemory(output_, destination, R11, width);
	}

	void EmitExtendedConstant(std::uint16_t exponent,
		std::int64_t displacement)
	{
		EmitImmediate64(output_, RAX, UINT64_C(0x8000000000000000));
		EmitStoreScratch(RAX, 64, displacement);
		EmitImmediate64(output_, RAX, exponent);
		EmitStoreScratch(RAX, 16, displacement + 8);
	}

	void EmitLiteral80(const Cy86Literal& literal, std::int64_t displacement)
	{
		const std::array<unsigned char, 10> converted =
			RawLiteral80(literal, literal_bytes_);
		std::uint64_t low = 0;
		std::uint16_t high = 0;
		for (unsigned i = 0; i < 8; ++i)
			low |= static_cast<std::uint64_t>(converted[i]) << (i * 8);
		for (unsigned i = 0; i < 2; ++i)
			high |= static_cast<std::uint16_t>(converted[i + 8]) << (i * 8);
		EmitImmediate64(output_, RAX, low);
		EmitStoreScratch(RAX, 64, displacement);
		EmitImmediate64(output_, RAX, high);
		EmitStoreScratch(RAX, 16, displacement + 8);
	}

	void EmitFloatLoad(const Cy86Operand& operand, unsigned width)
	{
		if (operand.kind == CY86_MEMORY_OPERAND)
		{
			EmitAddress(operand.memory);
			EmitX87Load(output_, width, R11);
			return;
		}
		if (width == 80)
		{
			if (operand.kind != CY86_IMMEDIATE_OPERAND)
				cy86_errors::ThrowInternal("invalid 80-bit operand");
			if (operand.immediate.kind == CY86_LITERAL_VALUE)
				EmitLiteral80(operand.immediate.literal, -32);
			else
			{
				EmitLabel64(output_, RAX, operand.immediate.label,
					AdjustmentValue(operand.immediate.adjustment,
						literal_bytes_, operand.immediate.adjustment_sign));
				EmitStoreScratch(RAX, 64, -32);
				EmitImmediate64(output_, RAX, 0);
				EmitStoreScratch(RAX, 16, -24);
			}
		}
		else
		{
			EmitLoad(operand, width, RAX);
			EmitStoreScratch(RAX, width, -32);
		}
		EmitScratchAddress(-32);
		EmitX87Load(output_, width, R11);
	}

	void EmitFloatStore(const Cy86Operand& operand, unsigned width)
	{
		if (operand.kind == CY86_MEMORY_OPERAND)
		{
			EmitAddress(operand.memory);
			EmitX87StorePop(output_, width, R11);
			return;
		}
		if (width == 80)
			cy86_errors::ThrowInternal("80-bit destination must be memory");
		EmitScratchAddress(-32);
		EmitX87StorePop(output_, width, R11);
		EmitLoadScratch(RAX, width, -32);
		EmitStore(operand, width, RAX);
	}

	void EmitFildScratch64()
	{
		EmitScratchAddress(-32);
		EmitX87Memory(output_, 0xdf, 5, R11);
	}

	void EmitFistpScratch64()
	{
		EmitScratchAddress(-32);
		// Integer conversions have cast semantics: truncate toward zero rather
		// than inheriting the caller's x87 rounding-control mode.
		EmitX87Memory(output_, 0xdd, 1, R11);
	}

	std::size_t EmitNearCondition(unsigned opcode)
	{
		output_.Byte(0x0f);
		output_.Byte(opcode);
		return output_.Reserve32();
	}

	std::size_t EmitNearJump()
	{
		output_.Byte(0xe9);
		return output_.Reserve32();
	}

	void NormalizeInput(const Cy86Statement& statement, int reg)
	{
		const Cy86Opcode& opcode = Opcode(statement);
		if (opcode.signed_operation)
			EmitSignExtend(output_, reg, opcode.width);
	}

	void EmitData(const Cy86Statement& statement)
	{
		const Cy86Operand& operand = statement.operands[0];
		const unsigned width = Opcode(statement).width;
		if (operand.immediate.kind == CY86_LABEL_VALUE)
		{
			output_.LabelImmediate(operand.immediate.label,
				AdjustmentValue(operand.immediate.adjustment,
					literal_bytes_, operand.immediate.adjustment_sign), width);
			return;
		}
		output_.Little(RawLiteralBits(operand.immediate.literal, literal_bytes_,
			width), width / 8);
	}

	void EmitMove(const Cy86Statement& statement)
	{
		const unsigned width = Opcode(statement).width;
		if (width == 80)
		{
			EmitMove80(statement);
			return;
		}
		EmitLoad(statement.operands[1], width, RAX);
		EmitStore(statement.operands[0], width, RAX);
	}

	void EmitMove80(const Cy86Statement& statement)
	{
		const Cy86Operand& source = statement.operands[1];
		if (source.kind == CY86_MEMORY_OPERAND)
		{
			EmitAddress(source.memory);
			EmitZeroExtendedMemory(output_, RAX, R11, 64);
			EmitImmediate64(output_, RBX, 8);
			EmitAluRegister(output_, 0x01, R11, RBX);
			EmitZeroExtendedMemory(output_, RCX, R11, 16);
		}
		else if (source.kind == CY86_IMMEDIATE_OPERAND &&
			source.immediate.kind == CY86_LABEL_VALUE)
		{
			EmitLabel64(output_, RAX, source.immediate.label,
				AdjustmentValue(source.immediate.adjustment,
					literal_bytes_, source.immediate.adjustment_sign));
			EmitImmediate64(output_, RCX, 0);
		}
		else if (source.kind == CY86_IMMEDIATE_OPERAND)
		{
			const std::array<unsigned char, 10> converted =
				RawLiteral80(source.immediate.literal, literal_bytes_);
			std::uint64_t low = 0;
			std::uint16_t high = 0;
			for (std::size_t i = 0; i < 8; ++i)
				low |= static_cast<std::uint64_t>(converted[i]) << (i * 8);
			for (std::size_t i = 8; i < 10; ++i)
				high |= static_cast<std::uint16_t>(converted[i]) << ((i - 8) * 8);
			EmitImmediate64(output_, RAX, low);
			EmitImmediate64(output_, RCX, high);
		}
		else
		{
			cy86_errors::ThrowInternal("80-bit source cannot be a register");
		}
		const Cy86Operand& destination = statement.operands[0];
		if (destination.kind != CY86_MEMORY_OPERAND)
			cy86_errors::ThrowInternal("80-bit destination must be memory");
		EmitAddress(destination.memory);
		EmitMoveToMemory(output_, R11, RAX, 64);
		EmitImmediate64(output_, RBX, 8);
		EmitAluRegister(output_, 0x01, R11, RBX);
		EmitMoveToMemory(output_, R11, RCX, 16);
	}

	void EmitIndirectControl(const Cy86Operand& target, bool call)
	{
		EmitLoad(target, 64, RAX);
		output_.Byte(0xff);
		EmitModRm(output_, 3, call ? 2 : 4, RAX);
	}

	void EmitJumpIf(const Cy86Statement& statement)
	{
		EmitLoad(statement.operands[0], 8, RAX);
		output_.Byte(0x84);
		output_.Byte(0xc0);
		output_.Byte(0x0f);
		output_.Byte(0x84);
		const std::size_t skip = output_.Reserve32();
		EmitIndirectControl(statement.operands[1], false);
		output_.PatchRelative32(skip, output_.Bytes().size());
	}

	void EmitNot(const Cy86Statement& statement)
	{
		const unsigned width = Opcode(statement).width;
		EmitLoad(statement.operands[1], width, RAX);
		EmitRex(output_, true, 2, RAX);
		output_.Byte(0xf7);
		EmitModRm(output_, 3, 2, RAX);
		EmitStore(statement.operands[0], width, RAX);
	}

	void EmitSimpleBinary(const Cy86Statement& statement, unsigned opcode)
	{
		const unsigned width = Opcode(statement).width;
		EmitLoad(statement.operands[1], width, RAX);
		EmitLoad(statement.operands[2], width, RCX);
		EmitAluRegister(output_, opcode, RAX, RCX);
		EmitStore(statement.operands[0], width, RAX);
	}

	void EmitShift(const Cy86Statement& statement, unsigned extension)
	{
		const unsigned width = Opcode(statement).width;
		EmitLoad(statement.operands[1], width, RAX);
		NormalizeInput(statement, RAX);
		EmitLoad(statement.operands[2], 8, RCX);
		EmitRex(output_, true, extension, RAX);
		output_.Byte(0xd3);
		EmitModRm(output_, 3, extension, RAX);
		EmitStore(statement.operands[0], width, RAX);
	}

	void EmitMultiply(const Cy86Statement& statement)
	{
		const unsigned width = Opcode(statement).width;
		EmitLoad(statement.operands[1], width, RAX);
		EmitLoad(statement.operands[2], width, RCX);
		EmitRex(output_, true, RAX, RCX);
		output_.Byte(0x0f);
		output_.Byte(0xaf);
		EmitModRm(output_, 3, RAX, RCX);
		EmitStore(statement.operands[0], width, RAX);
	}

	void EmitDivide(const Cy86Statement& statement, bool remainder)
	{
		const Cy86Opcode& opcode = Opcode(statement);
		EmitLoad(statement.operands[1], opcode.width, RAX);
		EmitLoad(statement.operands[2], opcode.width, RCX);
		NormalizeInput(statement, RAX);
		NormalizeInput(statement, RCX);
		if (opcode.signed_operation)
		{
			output_.Byte(0x48);
			output_.Byte(0x99);
		}
		else
			EmitXor32(output_, RDX);
		EmitRex(output_, true, opcode.signed_operation ? 7 : 6, RCX);
		output_.Byte(0xf7);
		EmitModRm(output_, 3,
			opcode.signed_operation ? 7 : 6, RCX);
		EmitStore(statement.operands[0], opcode.width,
			remainder ? RDX : RAX);
	}

	unsigned ComparisonCode(const Cy86Statement& statement) const
	{
		const Cy86Opcode& opcode = Opcode(statement);
		switch (opcode.operation)
		{
		case CY86_EQ: return 0x94;
		case CY86_NE: return 0x95;
		case CY86_LT: return opcode.signed_operation ? 0x9c : 0x92;
		case CY86_GT: return opcode.signed_operation ? 0x9f : 0x97;
		case CY86_LE: return opcode.signed_operation ? 0x9e : 0x96;
		case CY86_GE: return opcode.signed_operation ? 0x9d : 0x93;
		default: break;
		}
		cy86_errors::ThrowInternal("invalid comparison opcode");
	}

	void EmitComparison(const Cy86Statement& statement)
	{
		const Cy86Opcode& opcode = Opcode(statement);
		if (opcode.floating_operation)
		{
			EmitFloatingComparison(statement);
			return;
		}
		EmitLoad(statement.operands[1], opcode.width, RAX);
		EmitLoad(statement.operands[2], opcode.width, RCX);
		NormalizeInput(statement, RAX);
		NormalizeInput(statement, RCX);
		EmitAluRegister(output_, 0x39, RAX, RCX);
		output_.Byte(0x0f);
		output_.Byte(ComparisonCode(statement));
		output_.Byte(0xc0);
		EmitStore(statement.operands[0], 8, RAX);
	}

	void EmitUnsigned64ToFloat(const Cy86Operand& source)
	{
		EmitLoad(source, 64, RAX);
		EmitStoreScratch(RAX, 64, -32);
		EmitFildScratch64();
		output_.Byte(0x48);
		output_.Byte(0x85);
		output_.Byte(0xc0);
		const std::size_t nonnegative = EmitNearCondition(0x89);
		EmitExtendedConstant(0x403f, -48);
		EmitScratchAddress(-48);
		EmitX87Load(output_, 80, R11);
		output_.Byte(0xde);
		output_.Byte(0xc1);
		output_.PatchRelative32(nonnegative, output_.Bytes().size());
	}

	void EmitIntegerToFloat(const Cy86Statement& statement)
	{
		const Cy86Opcode& opcode = Opcode(statement);
		const Cy86OperandConstraint& source_type = opcode.operands[1];
		if (source_type.value_class == CY86_VALUE_UNSIGNED &&
			source_type.width == 64)
		{
			EmitUnsigned64ToFloat(statement.operands[1]);
		}
		else
		{
			EmitLoad(statement.operands[1], source_type.width, RAX);
			if (source_type.value_class == CY86_VALUE_SIGNED)
				EmitSignExtend(output_, RAX, source_type.width);
			EmitStoreScratch(RAX, 64, -32);
			EmitFildScratch64();
		}
		EmitFloatStore(statement.operands[0], opcode.operands[0].width);
	}

	void EmitUnsigned64FromFloat(const Cy86Statement& statement)
	{
		EmitExtendedConstant(0x403e, -48);
		EmitFloatLoad(statement.operands[1], 80);
		EmitScratchAddress(-48);
		EmitX87Load(output_, 80, R11);
		// Compare 2^63 (st0) with the value (st1), preserving both.
		output_.Byte(0xdb);
		output_.Byte(0xf1);
		const std::size_t below_threshold = EmitNearCondition(0x87);
		// st1 = value - 2^63, pop the constant.
		output_.Byte(0xde);
		output_.Byte(0xe9);
		EmitFistpScratch64();
		EmitLoadScratch(RAX, 64, -32);
		EmitImmediate64(output_, RCX, UINT64_C(0x8000000000000000));
		EmitAluRegister(output_, 0x31, RAX, RCX);
		const std::size_t done = EmitNearJump();
		output_.PatchRelative32(below_threshold, output_.Bytes().size());
		// Discard the constant and convert the original value.
		output_.Byte(0xdd);
		output_.Byte(0xd8);
		EmitFistpScratch64();
		EmitLoadScratch(RAX, 64, -32);
		output_.PatchRelative32(done, output_.Bytes().size());
		EmitStore(statement.operands[0], 64, RAX);
	}

	void EmitFloatToInteger(const Cy86Statement& statement)
	{
		const Cy86Opcode& opcode = Opcode(statement);
		const Cy86OperandConstraint& destination_type = opcode.operands[0];
		if (destination_type.value_class == CY86_VALUE_UNSIGNED &&
			destination_type.width == 64)
		{
			EmitUnsigned64FromFloat(statement);
			return;
		}
		EmitFloatLoad(statement.operands[1], opcode.operands[1].width);
		EmitFistpScratch64();
		EmitLoadScratch(RAX, 64, -32);
		EmitStore(statement.operands[0], destination_type.width, RAX);
	}

	void EmitConversion(const Cy86Statement& statement)
	{
		const Cy86Opcode& opcode = Opcode(statement);
		const bool source_float =
			opcode.operands[1].value_class == CY86_VALUE_FLOAT;
		const bool destination_float =
			opcode.operands[0].value_class == CY86_VALUE_FLOAT;
		if (!source_float && destination_float)
			EmitIntegerToFloat(statement);
		else if (source_float && !destination_float)
			EmitFloatToInteger(statement);
		else if (source_float && destination_float)
		{
			EmitFloatLoad(statement.operands[1], opcode.operands[1].width);
			EmitFloatStore(statement.operands[0], opcode.operands[0].width);
		}
		else
			cy86_errors::ThrowInternal("invalid CY86 conversion");
	}

	void EmitFloatingArithmetic(const Cy86Statement& statement)
	{
		const Cy86Opcode& cy86_opcode = Opcode(statement);
		EmitFloatLoad(statement.operands[1], cy86_opcode.width);
		EmitFloatLoad(statement.operands[2], cy86_opcode.width);
		unsigned opcode = 0;
		switch (cy86_opcode.operation)
		{
		case CY86_FADD: opcode = 0xc1; break;
		case CY86_FMUL: opcode = 0xc9; break;
		case CY86_FSUB: opcode = 0xe9; break;
		case CY86_FDIV: opcode = 0xf9; break;
		default: cy86_errors::ThrowInternal("invalid floating arithmetic operation");
		}
		output_.Byte(0xde);
		output_.Byte(opcode);
		EmitFloatStore(statement.operands[0], cy86_opcode.width);
	}

	void EmitFloatingComparison(const Cy86Statement& statement)
	{
		const Cy86Opcode& opcode = Opcode(statement);
		EmitFloatLoad(statement.operands[2], opcode.width);
		EmitFloatLoad(statement.operands[1], opcode.width);
		// Compare st0 with st1, pop st0, then discard the remaining operand.
		output_.Byte(0xdf);
		output_.Byte(0xf1);
		output_.Byte(0xdd);
		output_.Byte(0xd8);
		unsigned condition = 0;
		switch (opcode.operation)
		{
		case CY86_EQ: condition = 0x94; break;
		case CY86_NE: condition = 0x95; break;
		case CY86_LT: condition = 0x92; break;
		case CY86_GT: condition = 0x97; break;
		case CY86_LE: condition = 0x96; break;
		case CY86_GE: condition = 0x93; break;
		default: cy86_errors::ThrowInternal("invalid floating comparison operation");
		}
		EmitSetCondition(output_, condition, RAX);
		if (opcode.operation == CY86_NE)
		{
			EmitSetCondition(output_, 0x9a, RCX);
			output_.Byte(0x08);
			output_.Byte(0xc8);
		}
		else if (opcode.operation == CY86_EQ ||
			opcode.operation == CY86_LT || opcode.operation == CY86_LE)
		{
			EmitSetCondition(output_, 0x9b, RCX);
			output_.Byte(0x20);
			output_.Byte(0xc8);
		}
		EmitStore(statement.operands[0], 8, RAX);
	}

	void EmitSyscall(const Cy86Statement& statement)
	{
		static const int argument_registers[] = { RDI, RSI, RDX, R10, R8, R9 };
		const Cy86Opcode& opcode = Opcode(statement);
		EmitLoad(statement.operands[1], 64, RAX);
		for (unsigned i = 0; i < opcode.syscall_arguments; ++i)
			EmitLoad(statement.operands[i + 2], 64, argument_registers[i]);
		output_.Byte(0x0f);
		output_.Byte(0x05);
		EmitStore(statement.operands[0], 64, RAX);
	}

	NativeBuffer& output_;
	const std::vector<Cy86Opcode>& opcodes_;
	const std::vector<unsigned char>& literal_bytes_;
};

std::size_t StatementAlignment(const Cy86ProgramModel& program,
	const Cy86Statement& statement)
{
	if (statement.kind == CY86_LITERAL_STATEMENT)
		return Cy86LiteralAlignment(statement.literal);
	if (statement.opcode == 0 || statement.opcode >= program.opcodes.size())
		cy86_errors::ThrowInternal("invalid CY86 opcode identity during layout");
	const Cy86Opcode& opcode = program.opcodes[statement.opcode];
	if (opcode.operation == CY86_DATA)
		return opcode.width / 8;
	return 1;
}

void AlignStatement(NativeBuffer& output, std::size_t alignment)
{
	const std::uint64_t address = kLoadAddress + kContentOffset +
		output.Bytes().size();
	const std::size_t padding = static_cast<std::size_t>(
		(alignment - address % alignment) % alignment);
	output.Zeros(padding);
}

void EmitLiteralStatement(NativeBuffer& output,
	const Cy86ProgramModel& program, const Cy86Statement& statement)
{
	const unsigned char* data = LiteralData(statement.literal,
		program.literal_bytes);
	if (statement.literal.size != 0)
		output.Bytes().insert(output.Bytes().end(), data,
			data + statement.literal.size);
}

struct NativeProgram
{
	NativeBuffer content;
	std::uint64_t entry;
	std::size_t fixup_count;

	NativeProgram() : entry(0), fixup_count(0) {}
};

NativeProgram LowerProgram(const Cy86ProgramModel& program)
{
	NativeProgram native;
	// Keep the logical CY86 stream at an address congruent to three modulo four.
	// This also leaves a harmless, unreachable landing pad before every entry.
	native.content.Byte(0x90);
	native.content.Byte(0x90);
	native.content.Byte(0x90);
	InstructionEmitter instructions(native.content, program.opcodes,
		program.literal_bytes);
	std::vector<std::uint64_t> label_addresses(
		program.identifiers.Size() + 1, 0);
	std::uint64_t first_statement = 0;
	for (std::size_t i = 0; i < program.statements.size(); ++i)
	{
		const Cy86Statement& statement = program.statements[i];
		AlignStatement(native.content, StatementAlignment(program, statement));
		const std::uint64_t address = kLoadAddress + kContentOffset +
			native.content.Bytes().size();
		if (i == 0) first_statement = address;
		for (std::size_t j = 0; j < statement.labels.size(); ++j)
		{
			if (statement.labels[j] >= label_addresses.size())
				cy86_errors::ThrowInternal("invalid label identity in CY86 backend");
			label_addresses[statement.labels[j]] = address;
		}
		if (statement.kind == CY86_LITERAL_STATEMENT)
			EmitLiteralStatement(native.content, program, statement);
		else
			instructions.Emit(statement);
	}
	if (program.statements.empty())
	{
		native.entry = kLoadAddress + kContentOffset + native.content.Bytes().size();
		// mov eax, 60; xor edi, edi; syscall
		native.content.Byte(0xb8);
		native.content.Little(60, 4);
		native.content.Byte(0x31);
		native.content.Byte(0xff);
		native.content.Byte(0x0f);
		native.content.Byte(0x05);
	}
	else
	{
		if (program.start_label != 0 &&
			program.start_label < label_addresses.size() &&
			label_addresses[program.start_label] != 0)
			native.entry = label_addresses[program.start_label];
		else
			native.entry = first_statement;
	}
	const std::vector<AddressFixup>& fixups = native.content.Fixups();
	for (std::size_t i = 0; i < fixups.size(); ++i)
	{
		if (fixups[i].label >= label_addresses.size() ||
			label_addresses[fixups[i].label] == 0)
			cy86_errors::ThrowInternal("unresolved label reached CY86 backend");
		native.content.PatchLittle(fixups[i].offset,
			label_addresses[fixups[i].label] + fixups[i].addend,
			fixups[i].width / 8);
	}
	native.fixup_count = fixups.size();
	native.content.ReleaseFixups();
	return native;
}

void AppendLittle(std::vector<unsigned char>* output, std::uint64_t value,
	unsigned bytes)
{
	for (unsigned i = 0; i < bytes; ++i)
		output->push_back(static_cast<unsigned char>(value >> (i * 8)));
}

std::vector<unsigned char> BuildElfHeader(const NativeProgram& program)
{
	const std::uint64_t file_size = kContentOffset + program.content.Bytes().size();
	std::vector<unsigned char> image;
	image.reserve(kContentOffset);
	const unsigned char ident[16] = {
		0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	image.insert(image.end(), ident, ident + 16);
	AppendLittle(&image, 2, 2);
	AppendLittle(&image, 0x3e, 2);
	AppendLittle(&image, 1, 4);
	AppendLittle(&image, program.entry, 8);
	AppendLittle(&image, kElfHeaderSize, 8);
	AppendLittle(&image, 0, 8);
	AppendLittle(&image, 0, 4);
	AppendLittle(&image, kElfHeaderSize, 2);
	AppendLittle(&image, kProgramHeaderSize, 2);
	AppendLittle(&image, 1, 2);
	AppendLittle(&image, 0, 2);
	AppendLittle(&image, 0, 2);
	AppendLittle(&image, 0, 2);
	AppendLittle(&image, 1, 4);
	AppendLittle(&image, 7, 4);
	AppendLittle(&image, 0, 8);
	AppendLittle(&image, kLoadAddress, 8);
	AppendLittle(&image, 0, 8);
	AppendLittle(&image, file_size, 8);
	AppendLittle(&image, file_size, 8);
	AppendLittle(&image, 0x1000, 8);
	if (image.size() != kContentOffset)
		cy86_errors::ThrowInternal("invalid ELF header size");
	return image;
}

}

void WriteCy86Executable(Cy86ProgramModel& program,
	const std::string& path, Cy86Stats* stats)
{
	const std::chrono::steady_clock::time_point lowering_start = stats ?
		std::chrono::steady_clock::now() :
		std::chrono::steady_clock::time_point();
	NativeProgram native = LowerProgram(program);
	program.Clear();
	if (stats)
		stats->lowering_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - lowering_start).count());
	const std::chrono::steady_clock::time_point writing_start = stats ?
		std::chrono::steady_clock::now() :
		std::chrono::steady_clock::time_point();
	const std::vector<unsigned char> header = BuildElfHeader(native);
	std::ofstream output(path.c_str(), std::ios::out | std::ios::binary |
		std::ios::trunc);
	if (!output) cy86_errors::ThrowInputOutput("unable to open output file: " + path);
	output.write(reinterpret_cast<const char*>(header.data()), header.size());
	output.write(reinterpret_cast<const char*>(native.content.Bytes().data()),
		native.content.Bytes().size());
	if (!output) cy86_errors::ThrowInputOutput("unable to write output file: " + path);
	output.close();
	if (::chmod(path.c_str(), 0755) != 0)
		cy86_errors::ThrowInputOutput("unable to make output executable: " +
			std::string(std::strerror(errno)));
	if (stats)
	{
		stats->fixups = native.fixup_count;
		stats->instruction_bytes = native.content.Bytes().size();
		stats->image_bytes = header.size() + native.content.Bytes().size();
		stats->writing_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - writing_start).count());
	}
}

}
