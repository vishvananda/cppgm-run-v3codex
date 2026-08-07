#pragma once

#include "pa11_model.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa15_lowir_detail
{

using namespace pa11;

enum LowKind : std::uint8_t
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
	LOW_PTR,
	LOW_OBJECT
};

struct LowType
{
	std::uint64_t width;
	std::uint32_t alignment;
	LowKind kind;
	bool is_signed;

	LowType() : width(0), alignment(0), kind(LOW_INVALID),
		is_signed(false) {}
	LowType(LowKind kind_value, std::size_t width_value,
		std::size_t alignment_value, bool signed_value)
		: width(width_value), alignment(static_cast<std::uint32_t>(alignment_value)),
		  kind(kind_value),
		  is_signed(signed_value) {}
};

inline LowType LowVoid() { return LowType(LOW_VOID, 0, 1, false); }
inline LowType LowI8() { return LowType(LOW_I8, 8, 1, true); }
inline LowType LowU8() { return LowType(LOW_U8, 8, 1, false); }
inline LowType LowI16() { return LowType(LOW_I16, 16, 2, true); }
inline LowType LowU16() { return LowType(LOW_U16, 16, 2, false); }
inline LowType LowI32() { return LowType(LOW_I32, 32, 4, true); }
inline LowType LowU32() { return LowType(LOW_U32, 32, 4, false); }
inline LowType LowI64() { return LowType(LOW_I64, 64, 8, true); }
inline LowType LowU64() { return LowType(LOW_I64, 64, 8, false); }
inline LowType LowF32() { return LowType(LOW_F32, 32, 4, true); }
inline LowType LowF64() { return LowType(LOW_F64, 64, 8, true); }
inline LowType LowF80() { return LowType(LOW_F80, 80, 16, true); }
inline LowType LowPtr() { return LowType(LOW_PTR, 64, 8, false); }
inline LowType LowObject(std::size_t size, std::size_t alignment)
{
	if (size > std::numeric_limits<std::uint64_t>::max() / 8 ||
		alignment > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("PA15 object type exceeds LowIR limits");
	return LowType(LOW_OBJECT, size * 8, alignment, false);
}

inline bool SameType(const LowType& left, const LowType& right)
{
	return left.kind == right.kind && (left.kind != LOW_OBJECT ||
		(left.width == right.width && left.alignment == right.alignment));
}

inline bool IsInteger(const LowType& type)
{
	return type.kind == LOW_I8 || type.kind == LOW_U8 ||
		type.kind == LOW_I16 || type.kind == LOW_U16 ||
		type.kind == LOW_I32 || type.kind == LOW_U32 ||
		type.kind == LOW_I64;
}

inline bool IsFloating(const LowType& type)
{
	return type.kind == LOW_F32 || type.kind == LOW_F64 ||
		type.kind == LOW_F80;
}

const std::uint32_t kNoLowId = std::numeric_limits<std::uint32_t>::max();

template <typename Tag>
class LowId
{
public:
	LowId() : value_(kNoLowId) {}
	LowId(std::uint32_t value) : value_(value) {}
	operator std::uint32_t() const { return value_; }

private:
	std::uint32_t value_;
};

struct SymbolIdTag {};
struct ParameterIdTag {};
struct SlotIdTag {};
struct BlockIdTag {};
struct TempIdTag {};
typedef LowId<SymbolIdTag> SymbolId;
typedef LowId<ParameterIdTag> ParameterId;
typedef LowId<SlotIdTag> SlotId;
typedef LowId<BlockIdTag> BlockId;
typedef LowId<TempIdTag> TempId;

struct Operand
{
	enum Kind : std::uint8_t
	{
		NONE,
		TEMP,
		PARAMETER,
		SLOT,
		GLOBAL,
		FUNCTION,
		INTEGER,
		FLOATING,
		NULL_POINTER
	} kind;
	std::uint32_t id;
	std::int64_t integer_value;
	LowType type;

	Operand() : kind(NONE), id(kNoLowId), integer_value(0) {}
	Operand(TempId id_value, const LowType& type_value)
		: kind(TEMP), id(id_value), integer_value(0), type(type_value) {}
	Operand(ParameterId id_value, const LowType& type_value)
		: kind(PARAMETER), id(id_value), integer_value(0), type(type_value) {}
	Operand(SlotId id_value, const LowType& type_value)
		: kind(SLOT), id(id_value), integer_value(0), type(type_value) {}
	Operand(Kind kind_value, SymbolId id_value, const LowType& type_value)
		: kind(kind_value), id(id_value), integer_value(0), type(type_value)
	{
		if (kind != GLOBAL && kind != FUNCTION)
			throw std::logic_error("invalid PA15 symbol operand kind");
	}
	Operand(std::int64_t value, const LowType& type_value)
		: kind(INTEGER), id(kNoLowId), integer_value(value), type(type_value) {}
	static Operand Floating(InternedStringId spelling, const LowType& type_value)
	{
		Operand result;
		result.kind = FLOATING;
		result.id = spelling;
		result.type = type_value;
		return result;
	}
	static Operand NullPointer(const LowType& type_value)
	{
		Operand result;
		result.kind = NULL_POINTER;
		result.type = type_value;
		return result;
	}
};

enum LowOperation : std::uint8_t
{
	LOW_OP_NONE,
	LOW_OP_NEG,
	LOW_OP_BITNOT,
	LOW_OP_ADD,
	LOW_OP_SUB,
	LOW_OP_MUL,
	LOW_OP_DIV,
	LOW_OP_UDIV,
	LOW_OP_MOD,
	LOW_OP_UMOD,
	LOW_OP_AND,
	LOW_OP_OR,
	LOW_OP_XOR,
	LOW_OP_SHL,
	LOW_OP_SHR,
	LOW_OP_USHR,
	LOW_OP_EQ,
	LOW_OP_NE,
	LOW_OP_LT,
	LOW_OP_ULT,
	LOW_OP_LE,
	LOW_OP_ULE,
	LOW_OP_GT,
	LOW_OP_UGT,
	LOW_OP_GE,
	LOW_OP_UGE,
	LOW_OP_TRUNC,
	LOW_OP_SEXT,
	LOW_OP_ZEXT,
	LOW_OP_SITOFP,
	LOW_OP_UITOFP,
	LOW_OP_FPTOSI,
	LOW_OP_FPTOUI,
	LOW_OP_FPTRUNC,
	LOW_OP_FPEXT,
	LOW_OP_DECAY
};

enum IndexProjection : std::uint8_t
{
	INDEX_PROJECTION_NONE,
	INDEX_PROJECTION_ARRAY_ELEMENT,
	INDEX_PROJECTION_FIELD,
	INDEX_PROJECTION_BASE_SUBOBJECT
};

struct Instruction
{
	enum Kind : std::uint8_t
	{
		CONST,
		COPY,
		ADDR,
		LOAD,
		STORE,
		COPY_OBJECT,
		INDEX,
		UNARY,
		BINARY,
		CMP,
		CONVERT,
		CALL,
		EH_TRY,
		EH_CLEANUP,
		EH_END,
		RESUME,
		JUMP,
		BRANCH,
		SWITCH,
		RETURN_VALUE,
		RETURN_VOID
	};
	LowType type;
	LowType source_type;
	Operand first;
	Operand second;
	TempId dest;
	std::uint32_t extra_first;
	std::uint32_t extra_count;
	BlockId target;
	BlockId alternate;
	Kind kind;
	LowOperation op;
	IndexProjection projection;
	bool indirect;

	explicit Instruction(Kind kind_value)
		: dest(kNoLowId), extra_first(kNoLowId), extra_count(0),
		  target(kNoLowId), alternate(kNoLowId), kind(kind_value),
		  op(LOW_OP_NONE), projection(INDEX_PROJECTION_NONE), indirect(false) {}
};

inline bool IsTerminator(const Instruction& instruction)
{
	return instruction.kind == Instruction::JUMP ||
		instruction.kind == Instruction::BRANCH ||
		instruction.kind == Instruction::SWITCH ||
		instruction.kind == Instruction::RETURN_VALUE ||
		instruction.kind == Instruction::RETURN_VOID ||
		instruction.kind == Instruction::RESUME;
}

struct Block
{
	std::string label;
	std::vector<Instruction> instructions;
	bool terminated;
	bool selected;

	explicit Block(const std::string& label_value)
		: label(label_value), terminated(false), selected(false) {}
};

struct Parameter
{
	enum Capture : std::uint8_t { CAPTURE_DEFAULT, CAPTURE_NOCAPTURE } capture;
	enum Access : std::uint8_t { ACCESS_DEFAULT, ACCESS_READ, ACCESS_WRITE,
		ACCESS_READWRITE } access;
	enum Alias : std::uint8_t { ALIAS_DEFAULT, ALIAS_NOALIAS } alias;
	std::string name;
	LowType type;
	bool reference, indirect_result;

	Parameter() : capture(CAPTURE_DEFAULT), access(ACCESS_DEFAULT),
		alias(ALIAS_DEFAULT), reference(false), indirect_result(false) {}
};

struct Slot
{
	std::string name;
	LowType type;
};

struct Function
{
	SymbolId symbol;
	LowType result;
	std::vector<Parameter> parameters;
	std::vector<Slot> slots;
	std::vector<Block> blocks;
	std::vector<BlockId> block_order;
	bool entry;
	bool initializer;
	bool finalizer;
	bool variadic;

	Function() : symbol(kNoLowId), entry(false), initializer(false),
		finalizer(false), variadic(false) {}
};

struct FunctionDeclaration
{
	SymbolId symbol;
	LowType result;
	std::vector<Parameter> parameters;
	bool variadic;

	FunctionDeclaration() : symbol(kNoLowId), variadic(false) {}
};

struct GlobalDeclaration
{
	SymbolId symbol;
	LowType type;
	bool typed;

	GlobalDeclaration() : symbol(kNoLowId), typed(true) {}
};

struct Global
{
	enum InitializerKind { ZERO, INTEGER_VALUE, FLOATING_VALUE, ADDRESS_VALUE,
		STRUCTURED_VALUE } initializer_kind;
	struct DataItem
	{
		enum Kind { INTEGER_ITEM, FLOATING_ITEM, ADDRESS_ITEM, ZERO_ITEM } kind;
		LowType type;
		std::int64_t integer_value;
		InternedStringId floating_spelling;
		SymbolId symbol;
		std::int64_t offset;
		std::size_t zero_bytes;

		DataItem() : kind(INTEGER_ITEM), integer_value(0), floating_spelling(0),
			symbol(kNoLowId),
			offset(0), zero_bytes(0) {}
	};
	SymbolId symbol;
	LowType type;
	std::int64_t initializer;
	InternedStringId floating_initializer;
	SymbolId address_symbol;
	std::int64_t address_offset;
	std::vector<DataItem> items;

	Global() : initializer_kind(ZERO), symbol(kNoLowId), initializer(0),
		floating_initializer(0),
		address_symbol(kNoLowId), address_offset(0) {}
};

struct DynamicInitializer
{
	SymbolId destination;
	std::uint32_t expression;

	DynamicInitializer(SymbolId destination_value, std::uint32_t expression_value)
		: destination(destination_value), expression(expression_value) {}
};

struct Symbol
{
	enum Kind { FUNCTION_SYMBOL, GLOBAL_SYMBOL } kind;
	enum Effects : std::uint8_t { EFFECTS_DEFAULT, EFFECTS_READNONE,
		EFFECTS_READONLY, EFFECTS_READWRITE } effects;
	std::string name;
	std::string object_name;
	bool c_linkage;
	bool internal_linkage;
	bool nonthrowing;
	bool noreturn;
	bool thread_local_storage;
	SymbolId tls_for_symbol;
	std::uint32_t source_type;
	bool declaration_emitted;
	bool definition_emitted;
	bool referenced;

	Symbol(Kind kind_value, const std::string& name_value,
		const std::string& object_name_value, bool c_linkage_value,
		bool internal_linkage_value, bool nonthrowing_value)
		: kind(kind_value), effects(EFFECTS_DEFAULT), name(name_value),
		  object_name(object_name_value),
		  c_linkage(c_linkage_value), internal_linkage(internal_linkage_value),
		  nonthrowing(nonthrowing_value), noreturn(false),
		  thread_local_storage(false),
		  tls_for_symbol(kNoLowId), source_type(kNoLowId),
		  declaration_emitted(false), definition_emitted(false), referenced(false) {}
};

}
}
