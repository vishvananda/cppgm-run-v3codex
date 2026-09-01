#pragma once

#include "lowir/model/identity.h"
#include "lowering/support/errors.h"
#include "semantic/model/program.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace cppgm
{
namespace lowering
{
namespace ir
{

using namespace semantic;

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
	LOW_I128,
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
inline LowType LowI128() { return LowType(LOW_I128, 128, 8, true); }
inline LowType LowU128() { return LowType(LOW_I128, 128, 8, false); }
inline LowType LowF32() { return LowType(LOW_F32, 32, 4, true); }
inline LowType LowF64() { return LowType(LOW_F64, 64, 8, true); }
inline LowType LowF80() { return LowType(LOW_F80, 80, 16, true); }
inline LowType LowPtr() { return LowType(LOW_PTR, 64, 8, false); }
inline LowType LowObject(std::size_t size, std::size_t alignment)
{
	if (size > std::numeric_limits<std::uint64_t>::max() / 8 ||
		alignment > std::numeric_limits<std::uint32_t>::max())
		ThrowLoweringResourceLimit("PA15 object type exceeds LowIR limits");
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
		type.kind == LOW_I64 || type.kind == LOW_I128;
}

inline bool IsFloating(const LowType& type)
{
	return type.kind == LOW_F32 || type.kind == LOW_F64 ||
		type.kind == LOW_F80;
}

const std::uint32_t kNoLowId = lowir_model::kInvalidCompactId;

template <typename Tag>
using LowId = lowir_model::CompactId<Tag>;

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
	union
	{
		std::int64_t integer_value;
		std::uint64_t floating_low;
	};
	std::uint64_t integer_high;
	LowType type;

	Operand() : kind(NONE), id(kNoLowId), integer_value(0), integer_high(0) {}
	Operand(TempId id_value, const LowType& type_value)
		: kind(TEMP), id(id_value), integer_value(0), integer_high(0), type(type_value) {}
	Operand(ParameterId id_value, const LowType& type_value)
		: kind(PARAMETER), id(id_value), integer_value(0), integer_high(0), type(type_value) {}
	Operand(SlotId id_value, const LowType& type_value)
		: kind(SLOT), id(id_value), integer_value(0), integer_high(0), type(type_value) {}
	Operand(Kind kind_value, SymbolId id_value, const LowType& type_value)
		: kind(kind_value), id(id_value), integer_value(0), integer_high(0), type(type_value)
	{
		if (kind != GLOBAL && kind != FUNCTION)
			ThrowLoweringInternal("invalid PA15 symbol operand kind");
	}
	Operand(std::int64_t value, const LowType& type_value)
		: kind(INTEGER), id(kNoLowId), integer_value(value),
		  integer_high(value < 0 ? ~std::uint64_t(0) : 0), type(type_value) {}
	Operand(std::int64_t low, std::uint64_t high, const LowType& type_value)
		: kind(INTEGER), id(kNoLowId), integer_value(low),
		  integer_high(high), type(type_value) {}
	static Operand Floating(lowir_model::StringId spelling,
		const LowType& type_value, std::uint64_t low, std::uint64_t high)
	{
		Operand result;
		result.kind = FLOATING;
		result.id = spelling;
		result.floating_low = low;
		result.integer_high = high;
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
	LOW_OP_BSWAP,
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
	LOW_OP_FPEXT
};

enum IndexProjection : std::uint8_t
{
	INDEX_PROJECTION_NONE,
	INDEX_PROJECTION_ARRAY_ELEMENT,
	INDEX_PROJECTION_FIELD
};

struct Instruction
{
	enum CallPassing : std::uint8_t
	{
		CALL_PASS_VALUE,
		CALL_PASS_REFERENCE,
		CALL_PASS_BY_ADDRESS,
		CALL_PASS_INDIRECT_RESULT
	};
	enum Kind : std::uint8_t
	{
		CONST,
		COPY,
		ADDR,
		LOAD,
		ATOMIC_LOAD,
		STORE,
		ATOMIC_STORE,
		ATOMIC_EXCHANGE,
		COPY_OBJECT,
		ZERO_OBJECT,
		INDEX,
		UNARY,
		BINARY,
		CMP,
		CONVERT,
		ATOMIC_ADD_FETCH,
		ATOMIC_COMPARE_EXCHANGE,
		ATOMIC_THREAD_FENCE,
		ATOMIC_SIGNAL_FENCE,
		STACK_ALLOC,
		VA_START,
		VA_ARG,
		CALL,
		EH_TRY,
		EH_CLEANUP,
		EH_CATCH,
		EH_FILTER,
		EH_CATCH_ALL,
		EH_END,
		EXCEPTION,
		EXCEPTION_SELECTOR,
		RESUME,
		JUMP,
		BRANCH,
		SWITCH,
		RETURN_VALUE,
		RETURN_VOID,
		UNREACHABLE
	};
	LowType type;
	LowType source_type;
	Operand first;
	Operand second;
	Operand third;
	TempId dest;
	std::uint32_t extra_first;
	std::uint32_t extra_count;
	std::uint32_t virtual_base_argument_count;
	BlockId target;
	BlockId alternate;
	Kind kind;
	LowOperation op;
	IndexProjection projection;
	std::uint8_t atomic_order;
	std::uint8_t atomic_failure_order;
	bool indirect;
	// Volatile accesses are observable behavior: no pass may remove, merge,
	// reorder, or forward them, and their slots may not be promoted away.
	bool volatile_access;
	// Serialized source-language permission for a later LowIR copy-elision
	// pass.  It never changes the ordinary O0 meaning of the call.
	bool copy_elision_candidate;

	explicit Instruction(Kind kind_value)
		: dest(kNoLowId), extra_first(kNoLowId), extra_count(0),
		  virtual_base_argument_count(0),
		  target(kNoLowId), alternate(kNoLowId), kind(kind_value),
		  op(LOW_OP_NONE), projection(INDEX_PROJECTION_NONE), atomic_order(0),
		  atomic_failure_order(0), indirect(false), volatile_access(false),
		  copy_elision_candidate(false) {}
};

inline bool IsTerminator(const Instruction& instruction)
{
	return instruction.kind == Instruction::JUMP ||
		instruction.kind == Instruction::BRANCH ||
		instruction.kind == Instruction::SWITCH ||
		instruction.kind == Instruction::RETURN_VALUE ||
		instruction.kind == Instruction::RETURN_VOID ||
		instruction.kind == Instruction::UNREACHABLE ||
		instruction.kind == Instruction::RESUME;
}

struct Block
{
	lowir_model::StringId label;
	std::vector<Instruction> instructions;
	bool terminated;
	bool selected;

	explicit Block(lowir_model::StringId label_value)
		: label(label_value), terminated(false), selected(false) {}
};

// Object-only lowering keeps a generated block spelling as its pooled prefix
// and numeric suffix until its lexical rank has been recorded.  Exact names
// use an invalid ordinal and retain only their pooled spelling.
struct BlockPresentationName
{
	lowir_model::StringId text;
	std::uint32_t ordinal;

	BlockPresentationName() : ordinal(kNoLowId) {}
	explicit BlockPresentationName(lowir_model::StringId text_value)
		: text(text_value), ordinal(kNoLowId) {}
	BlockPresentationName(lowir_model::StringId prefix_value,
		std::uint32_t ordinal_value)
		: text(prefix_value), ordinal(ordinal_value) {}
	bool generated() const { return ordinal != kNoLowId; }
};

struct Parameter
{
	enum Alias : std::uint8_t { ALIAS_DEFAULT, ALIAS_NOALIAS } alias;
	lowir_model::StringId name;
	LowType type;
	std::size_t object_bytes;
	bool reference, indirect_result, by_address;

	Parameter() : alias(ALIAS_DEFAULT), object_bytes(0), reference(false), indirect_result(false),
		by_address(false) {}
};

struct Slot
{
	lowir_model::StringId name;
	LowType type;
	// The source boundary parameter whose object storage this slot owns.
	// Invalid for ordinary locals and compiler-created temporaries.
	ParameterId parameter_origin;
};

struct Function
{
	SymbolId symbol;
	LowType result;
	std::vector<Parameter> parameters;
	std::vector<Slot> slots;
	std::vector<Block> blocks;
	std::vector<BlockId> block_order;
	std::vector<BlockPresentationName> block_presentations;
	std::vector<std::uint32_t> block_presentation_order;
	lowir_model::GeneratedNameReservations generated_name_reservations;
	std::uint32_t temporary_limit;
	bool entry;
	bool initializer;
	bool finalizer;
	bool variadic;

	Function() : symbol(kNoLowId), temporary_limit(0), entry(false),
		initializer(false), finalizer(false), variadic(false) {}
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
	enum StorageMode : std::uint8_t { STORAGE_DEFAULT, STORAGE_READONLY } storage;
	enum InitializerKind { ZERO, INTEGER_VALUE, FLOATING_VALUE, ADDRESS_VALUE,
		STRUCTURED_VALUE } initializer_kind;
	struct DataItem
	{
		enum Kind { INTEGER_ITEM, FLOATING_ITEM, ADDRESS_ITEM, ZERO_ITEM } kind;
		LowType type;
		union
		{
			std::int64_t integer_value;
			std::uint64_t floating_low;
		};
		std::uint64_t integer_high;
		lowir_model::StringId floating_spelling;
		SymbolId symbol;
		std::int64_t offset;
		std::size_t zero_bytes;

		DataItem() : kind(INTEGER_ITEM), integer_value(0), integer_high(0),
			floating_spelling(0),
			symbol(kNoLowId),
			offset(0), zero_bytes(0) {}
	};
	SymbolId symbol;
	LowType type;
	union
	{
		std::int64_t initializer;
		std::uint64_t floating_initializer_low;
	};
	std::uint64_t initializer_high;
	lowir_model::StringId floating_initializer;
	SymbolId address_symbol;
	std::int64_t address_offset;
	std::vector<DataItem> items;

	Global() : storage(STORAGE_DEFAULT), initializer_kind(ZERO),
		symbol(kNoLowId), initializer(0),
		initializer_high(0),
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
	enum RuntimeRole : std::uint8_t
	{
		RUNTIME_ROLE_NONE,
		RUNTIME_ROLE_EH_RESUME,
		RUNTIME_ROLE_EH_ALLOCATE_EXCEPTION,
		RUNTIME_ROLE_EH_BEGIN_CATCH,
		RUNTIME_ROLE_EH_END_CATCH,
		RUNTIME_ROLE_EH_RETHROW,
		RUNTIME_ROLE_EH_THROW,
		RUNTIME_ROLE_EH_PERSONALITY,
		RUNTIME_ROLE_ALLOCATE_MEMORY,
		RUNTIME_ROLE_FREE_MEMORY,
		RUNTIME_ROLE_TERMINATE,
		RUNTIME_ROLE_PURE_VIRTUAL,
		RUNTIME_ROLE_DYNAMIC_CAST,
		RUNTIME_ROLE_BAD_CAST,
		RUNTIME_ROLE_BAD_TYPEID,
		RUNTIME_ROLE_RTTI_CLASS,
		RUNTIME_ROLE_RTTI_SI,
		RUNTIME_ROLE_RTTI_VMI,
		RUNTIME_ROLE_RTTI_DATA
	} runtime_role;
	lowir_model::StringId name;
	lowir_model::StringId object_name;
	lowir_model::StringId section_name;
	bool c_linkage;
	bool internal_linkage;
	bool weak_linkage;
	bool prefer_local_object_binding;
	bool keep_internal_object_alias;
	bool nonthrowing;
	bool noreturn;
	bool thread_local_storage;
	SymbolId tls_for_symbol;
	std::uint32_t source_type;
	std::uint16_t demand_reason_mask;
	bool declaration_emitted;
	bool definition_emitted;
	bool referenced;
	bool object_output_root;
	bool lifecycle_base_entry;
	bool force_inline;
	bool inline_hint;
	bool no_inline;
	bool stable_prefix_query;

	Symbol(Kind kind_value, lowir_model::StringId name_value,
		lowir_model::StringId object_name_value, bool c_linkage_value,
		bool internal_linkage_value, bool nonthrowing_value)
		: kind(kind_value), effects(EFFECTS_DEFAULT),
		  runtime_role(RUNTIME_ROLE_NONE), name(name_value),
		  object_name(object_name_value),
		  c_linkage(c_linkage_value), internal_linkage(internal_linkage_value),
		  weak_linkage(false), prefer_local_object_binding(false),
		  keep_internal_object_alias(true),
		  nonthrowing(nonthrowing_value), noreturn(false),
		  thread_local_storage(false),
		  tls_for_symbol(kNoLowId), source_type(kNoLowId),
		  demand_reason_mask(0),
		  declaration_emitted(false), definition_emitted(false), referenced(false),
		  object_output_root(false), lifecycle_base_entry(false),
		  force_inline(false),
		  inline_hint(false), no_inline(false), stable_prefix_query(false) {}
};

}
}
}
