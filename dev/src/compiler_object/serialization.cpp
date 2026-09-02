#include "compiler_object/serialization.h"
#include "compiler_object/errors.h"
#include "support/exception_types.h"
#include "lowir/io/prepare.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace cppgm
{
namespace compiler_object
{
namespace
{

const char kMagic[] = "CPPGMOBJ";
const std::uint32_t kVersion = 6;
const std::uint64_t kMaxObjectElements = UINT64_C(1) << 28;

class BinaryWriter
{
public:
	BinaryWriter(std::vector<unsigned char>& output, SerializationStats* stats)
		: output_(output), stats_(stats) {}

	void Byte(std::uint8_t value)
	{
		Ensure(1);
		output_.push_back(value);
	}

	void U32(std::uint32_t value)
	{
		unsigned char bytes[4];
		for (unsigned i = 0; i < 4; ++i)
			bytes[i] = static_cast<unsigned char>(value >> (i * 8));
		Append(bytes, sizeof(bytes));
	}

	void U64(std::uint64_t value)
	{
		unsigned char bytes[8];
		for (unsigned i = 0; i < 8; ++i)
			bytes[i] = static_cast<unsigned char>(value >> (i * 8));
		Append(bytes, sizeof(bytes));
	}

	void I64(std::int64_t value) { U64(static_cast<std::uint64_t>(value)); }
	void Bool(bool value) { Byte(value ? 1 : 0); }

	void String(const std::string& value)
	{
		U64(value.size());
		if (!value.empty())
			Append(reinterpret_cast<const unsigned char*>(value.data()),
				value.size());
	}

private:
	void Ensure(std::size_t additional)
	{
		if (additional > output_.max_size() - output_.size())
			ThrowCompilerObjectResourceLimit("compiler object is too large");
		if (output_.size() + additional > output_.capacity() && stats_)
			++stats_->buffer_growths;
	}

	void Append(const unsigned char* bytes, std::size_t size)
	{
		Ensure(size);
		output_.insert(output_.end(), bytes, bytes + size);
	}

	std::vector<unsigned char>& output_;
	SerializationStats* stats_;
};

std::size_t EstimateProgramPayloadSize(const lowir_model::LowirProgram& program)
{
	std::size_t instructions = 0;
	std::size_t arguments = 0;
	for (std::size_t f = 0; f < program.functions.size(); ++f)
		for (std::size_t b = 0; b < program.functions[f].blocks.size(); ++b)
			for (std::size_t i = 0;
				i < program.functions[f].blocks[b].instructions.size(); ++i)
			{
				++instructions;
				arguments += program.functions[f].blocks[b].instructions[i].args.size();
			}
	const std::size_t top_level = program.global_declarations.size() +
		program.globals.size() + program.function_declarations.size() +
		program.functions.size() + program.object_aliases.size() +
		program.exported_symbols.size();
	const std::size_t base = 1024;
	const std::size_t instruction_bytes = 320;
	const std::size_t argument_bytes = 44;
	const std::size_t top_level_bytes = 256;
	if (instructions > (std::numeric_limits<std::size_t>::max() - base) /
		instruction_bytes) return 0;
	std::size_t estimate = base + instructions * instruction_bytes;
	if (arguments > (std::numeric_limits<std::size_t>::max() - estimate) /
		argument_bytes) return 0;
	estimate += arguments * argument_bytes;
	if (top_level > (std::numeric_limits<std::size_t>::max() - estimate) /
		top_level_bytes) return 0;
	return estimate + top_level * top_level_bytes;
}

class BinaryReader
{
public:
	BinaryReader(std::istream& input, std::uint64_t remaining)
		: input_(input), remaining_(remaining) {}

	std::uint8_t Byte()
	{
		Take(1);
		const int value = input_.get();
		if (value == std::char_traits<char>::eof())
			ReadFailure();
		return static_cast<std::uint8_t>(value);
	}

	std::uint32_t U32()
	{
		return static_cast<std::uint32_t>(ReadLittle(4));
	}

	std::uint64_t U64()
	{
		return ReadLittle(8);
	}

	std::int64_t I64() { return static_cast<std::int64_t>(U64()); }

	bool Bool()
	{
		const std::uint8_t value = Byte();
		if (value > 1) ThrowCompilerObjectInputError("invalid compiler object boolean");
		return value != 0;
	}

	std::size_t Size()
	{
		return CheckedSize(U64());
	}

	std::size_t Count(std::size_t minimum_element_bytes)
	{
		const std::size_t value = Size();
		if (minimum_element_bytes &&
			value > remaining_ / minimum_element_bytes)
			ThrowCompilerObjectInputError("invalid compiler object collection size");
		return value;
	}

	std::size_t CheckedSize(std::uint64_t value)
	{
		if (value > kMaxObjectElements ||
			value > std::numeric_limits<std::size_t>::max())
			ThrowCompilerObjectResourceLimit("compiler object collection is too large");
		return static_cast<std::size_t>(value);
	}

	std::string String()
	{
		const std::size_t size = CheckedSize(U64());
		if (size > remaining_)
			ThrowCompilerObjectInputError("truncated compiler object");
		std::string result(size, '\0');
		if (size) Take(size);
		if (size && !input_.read(&result[0], static_cast<std::streamsize>(size)))
			ReadFailure();
		return result;
	}

	void RequireEnd()
	{
		if (remaining_ != 0)
			ThrowCompilerObjectInputError("trailing bytes in compiler object");
		if (input_.bad())
			ThrowCompilerObjectInputOutputError("unable to read compiler object");
	}

private:
	__attribute__((cold, noinline, noreturn))
	void ReadFailure() const
	{
		if (input_.bad())
			ThrowCompilerObjectInputOutputError("unable to read compiler object");
		ThrowCompilerObjectInputError("truncated compiler object");
	}

	void Take(std::uint64_t size)
	{
		if (size > remaining_)
			ThrowCompilerObjectInputError("truncated compiler object");
		remaining_ -= size;
	}

	std::uint64_t ReadLittle(unsigned width)
	{
		Take(width);
		char bytes[8];
		if (!input_.read(bytes, width))
			ReadFailure();
		std::uint64_t value = 0;
		for (unsigned i = 0; i < width; ++i)
			value |= static_cast<std::uint64_t>(
				static_cast<unsigned char>(bytes[i])) << (i * 8);
		return value;
	}

	std::istream& input_;
	std::uint64_t remaining_;
};

template <typename Enum>
void WriteEnum(BinaryWriter& out, Enum value)
{
	out.U32(static_cast<std::uint32_t>(value));
}

template <typename Enum>
Enum ReadEnum(BinaryReader& in)
{
	return static_cast<Enum>(in.U32());
}

void WriteType(BinaryWriter& out, const lowir_model::LowType& value)
{
	out.String(lowir_model::lowir_type_text(value));
	WriteEnum(out, value.kind);
	out.U64(lowir_model::lowir_type_bit_width(value));
	out.U64(value.storage_size);
	out.U64(value.alignment);
}

lowir_model::LowType ReadType(BinaryReader& in)
{
	lowir_model::LowType value;
	(void)in.String();
	value.kind = ReadEnum<lowir_model::LowTypeKind>(in);
	const std::size_t serialized_width = in.Size();
	value.storage_size = in.Size();
	const std::size_t alignment = in.Size();
	if (alignment > std::numeric_limits<std::uint32_t>::max())
		ThrowCompilerObjectInputError("compiler object type alignment is too large");
	value.alignment = static_cast<std::uint32_t>(alignment);
	if (serialized_width != lowir_model::lowir_type_bit_width(value))
		ThrowCompilerObjectInputError("compiler object type width is inconsistent");
	return value;
}

void WriteOperand(BinaryWriter& out, const lowir_model::Operand& value,
	const lowir_model::LowirProgram& program,
	const lowir_model::Function* function = 0)
{
	WriteEnum(out, value.kind);
	if (value.kind == lowir_model::Operand::OP_LABEL)
	{
		if (!function)
			ThrowCompilerObjectInternalError("compiler object block target lacks a function");
		out.String(lowir_model::lowir_block_label(
			program.strings, *function, value.block));
	}
	else if (value.kind == lowir_model::Operand::OP_SLOT)
	{
		if (!function)
			ThrowCompilerObjectInternalError("compiler object slot lacks a function");
		out.String(lowir_model::lowir_slot_name(
			program.strings, *function, value.slot));
	}
	else if (value.kind == lowir_model::Operand::OP_TEMP)
	{
		if (!function)
			ThrowCompilerObjectInternalError("compiler object value lacks a function");
		out.String(lowir_model::lowir_value_name(
			program.strings, *function, value.value));
	}
	else if (value.kind == lowir_model::Operand::OP_GLOBAL)
		out.String(lowir_model::lowir_symbol_name(program, value.symbol));
	else if (value.kind == lowir_model::Operand::OP_INTEGER ||
		value.kind == lowir_model::Operand::OP_FLOAT)
	{
		if (!value.has_spelling &&
			value.kind == lowir_model::Operand::OP_INTEGER &&
			value.literal_type.kind == lowir_model::LTK_I128)
		{
			std::ostringstream text;
			text << "0x" << std::hex << std::setfill('0') << std::setw(16)
				<< value.int_high << std::setw(16)
				<< static_cast<std::uint64_t>(value.int_value);
			out.String(text.str());
		}
		else out.String(lowir_model::lowir_literal_text(
			value, &program.strings));
	}
	else out.String(std::string());
	out.I64(value.kind == lowir_model::Operand::OP_INTEGER && value.has_int_value ?
		value.int_value : 0);
}

lowir_model::Operand ReadOperand(BinaryReader& in,
	lowir_model::StringPool& strings)
{
	lowir_model::Operand value;
	value.kind = ReadEnum<lowir_model::Operand::Kind>(in);
	const std::string spelling = in.String();
	const long long stored_integer = in.I64();
	if (!spelling.empty())
	{
		value.literal = strings.intern(spelling);
		value.has_spelling = true;
	}
	if (value.kind == lowir_model::Operand::OP_INTEGER)
	{
		value.has_int_value = lowir_model::parse_lowir_integer_literal(
			spelling, &value.int_value, &value.int_high);
		if (!value.has_int_value)
		{
			value.int_value = stored_integer;
			value.int_high = stored_integer < 0 ? ~std::uint64_t(0) : 0;
		}
	}
	else if (value.kind == lowir_model::Operand::OP_FLOAT &&
		value.has_spelling)
	{
		value.literal_type = lowir_model::lowir_floating_literal_type(spelling);
		value.has_float_bits = lowir_model::parse_lowir_floating_literal_bits(
			spelling, value.literal_type, &value.literal_low, &value.literal_high);
	}
	return value;
}

void WriteSymbolMetadata(BinaryWriter& out,
	const lowir_model::SymbolMetadata& value,
	const lowir_model::LowirProgram& program)
{
	WriteEnum(out, value.role);
	WriteEnum(out, value.linkage);
	WriteEnum(out, value.binding);
	out.String(value.object_symbol.valid() ?
		program.strings.get(value.object_symbol) : std::string());
	out.String(value.tls_for_symbol_id.valid() ?
		lowir_model::lowir_symbol_name(program, value.tls_for_symbol_id) :
		std::string());
	out.String(value.section_name.valid() ?
		program.strings.get(value.section_name) : std::string());
	out.Bool(value.keep_internal_alias);
	out.Bool(value.prefer_local_object_binding);
	out.Bool(value.object_output_root);
	out.Bool(value.force_inline);
	out.Bool(value.inline_hint);
	out.Bool(value.no_inline);
}

lowir_model::SymbolMetadata ReadSymbolMetadata(
	BinaryReader& in, lowir_model::StringPool& strings)
{
	lowir_model::SymbolMetadata value;
	value.role = ReadEnum<lowir_model::SymbolRole>(in);
	value.linkage = ReadEnum<lowir_model::LanguageLinkageMode>(in);
	value.binding = ReadEnum<lowir_model::SymbolBindingMode>(in);
	const std::string object_symbol = in.String();
	const std::string tls_for = in.String();
	const std::string section_name = in.String();
	if (!object_symbol.empty())
		value.object_symbol = strings.intern(object_symbol);
	if (!tls_for.empty()) value.tls_for_spelling = strings.intern(tls_for);
	if (!section_name.empty())
		value.section_name = strings.intern(section_name);
	value.keep_internal_alias = in.Bool();
	value.prefer_local_object_binding = in.Bool();
	value.object_output_root = in.Bool();
	value.force_inline = in.Bool();
	value.inline_hint = in.Bool();
	value.no_inline = in.Bool();
	return value;
}

void WriteBoundary(BinaryWriter& out,
	const lowir_model::FunctionBoundaryMetadata& value)
{
	WriteEnum(out, value.arity);
	WriteEnum(out, value.effects);
	WriteEnum(out, value.unwind);
	WriteEnum(out, value.returns);
	WriteEnum(out, value.query);
}

lowir_model::FunctionBoundaryMetadata ReadBoundary(BinaryReader& in)
{
	lowir_model::FunctionBoundaryMetadata value;
	value.arity = ReadEnum<lowir_model::CallArityMode>(in);
	value.effects = ReadEnum<lowir_model::CallEffectsMode>(in);
	value.unwind = ReadEnum<lowir_model::CallUnwindMode>(in);
	value.returns = ReadEnum<lowir_model::CallReturnMode>(in);
	value.query = ReadEnum<lowir_model::CallQueryMode>(in);
	return value;
}

void WriteParameter(BinaryWriter& out, const lowir_model::Parameter& value,
	const lowir_model::LowirProgram& program)
{
	out.String(lowir_model::lowir_parameter_name(program, value));
	WriteType(out, value.type);
	WriteEnum(out, value.metadata.passing);
	WriteEnum(out, value.metadata.alias);
	out.U64(value.metadata.object_bytes);
}

lowir_model::Parameter ReadParameter(BinaryReader& in,
	lowir_model::StringPool& strings)
{
	lowir_model::Parameter value;
	value.name = strings.intern(in.String());
	value.type = ReadType(in);
	value.metadata.passing = ReadEnum<lowir_model::ParamPassingMode>(in);
	value.metadata.alias = ReadEnum<lowir_model::ParamAliasMode>(in);
	value.metadata.object_bytes = in.U64();
	return value;
}

void WriteParameters(BinaryWriter& out,
	const std::vector<lowir_model::Parameter>& values,
	const lowir_model::LowirProgram& program)
{
	out.U64(values.size());
	for (std::size_t i = 0; i < values.size(); ++i)
		WriteParameter(out, values[i], program);
}

std::vector<lowir_model::Parameter> ReadParameters(BinaryReader& in,
	lowir_model::StringPool& strings)
{
	std::vector<lowir_model::Parameter> values(in.Count(8));
	for (std::size_t i = 0; i < values.size(); ++i)
		values[i] = ReadParameter(in, strings);
	return values;
}

void WriteDebug(BinaryWriter& out,
	const lowir_model::InstructionDebugLocation& value,
	const lowir_model::LowirProgram& program)
{
	out.String(value.file.valid() ? program.strings.get(value.file) : std::string());
	out.U64(value.line);
	out.U64(value.column);
}

lowir_model::InstructionDebugLocation ReadDebug(BinaryReader& in,
	lowir_model::StringPool& strings)
{
	lowir_model::InstructionDebugLocation value;
	const std::string file = in.String();
	if (!file.empty()) value.file = strings.intern(file);
	value.line = in.Size();
	value.column = in.Size();
	return value;
}

void WriteInstruction(BinaryWriter& out, const lowir_model::Instruction& value,
	const lowir_model::LowirProgram& program,
	const lowir_model::Function& function)
{
	WriteEnum(out, value.kind);
	out.String(value.dest.valid() ?
		lowir_model::lowir_value_name(
			program.strings, function, value.dest) : std::string());
	WriteType(out, value.type);
	WriteType(out, value.source_type);
	out.String(lowir_model::lowir_operation_text(value.op));
	out.U64(value.byte_count);
	out.U64(value.byte_alignment);
	out.Bool(value.has_eh_selector);
	out.I64(value.eh_selector);
	WriteEnum(out, value.index_projection);
	WriteOperand(out, value.first, program, &function);
	WriteOperand(out, value.second, program, &function);
	WriteOperand(out, value.third, program, &function);
	out.U64(value.args.size());
	for (std::size_t i = 0; i < value.args.size(); ++i)
		WriteOperand(out, value.args[i], program, &function);
	out.Bool(value.call_returns_void);
	out.Bool(value.has_call_signature);
	out.Bool(value.copy_elision_candidate);
	WriteParameters(out, value.call_params, program);
	WriteType(out, value.call_return_type);
	WriteBoundary(out, value.call_boundary);
	WriteDebug(out, value.debug_location, program);
}

const lowir_model::LowType& InstructionResultType(
	const lowir_model::Instruction& value)
{
	if (value.kind == lowir_model::Instruction::IK_ADDR ||
		value.kind == lowir_model::Instruction::IK_INDEX)
		return lowir_model::builtin_lowir_type(lowir_model::LTK_PTR);
	if (value.kind == lowir_model::Instruction::IK_CMP ||
		value.kind == lowir_model::Instruction::IK_ATOMIC_COMPARE_EXCHANGE ||
		value.kind == lowir_model::Instruction::IK_EXCEPTION_SELECTOR)
		return lowir_model::builtin_lowir_type(lowir_model::LTK_I64);
	return value.type;
}

lowir_model::Instruction ReadInstruction(BinaryReader& in,
	lowir_model::LowirProgram& program, lowir_model::Function& function)
{
	lowir_model::Instruction value;
	value.kind = ReadEnum<lowir_model::Instruction::Kind>(in);
	const std::string destination = in.String();
	value.type = ReadType(in);
	value.source_type = ReadType(in);
	value.op = lowir_model::parse_lowir_operation(in.String());
	value.byte_count = in.Size();
	value.byte_alignment = in.Size();
	value.has_eh_selector = in.Bool();
	value.eh_selector = in.I64();
	value.index_projection = ReadEnum<lowir_model::IndexProjectionKind>(in);
	value.first = ReadOperand(in, program.strings);
	value.second = ReadOperand(in, program.strings);
	value.third = ReadOperand(in, program.strings);
	value.args.resize(in.Count(4));
	for (std::size_t i = 0; i < value.args.size(); ++i)
		value.args[i] = ReadOperand(in, program.strings);
	value.call_returns_void = in.Bool();
	value.has_call_signature = in.Bool();
	value.copy_elision_candidate = in.Bool();
	value.call_params = ReadParameters(in, program.strings);
	value.call_return_type = ReadType(in);
	value.call_boundary = ReadBoundary(in);
	value.debug_location = ReadDebug(in, program.strings);
	if (!destination.empty())
		value.dest = lowir_model::append_lowir_value(
			function, program.strings.intern(destination),
			InstructionResultType(value),
			destination.compare(0, 5, "%dbg_") == 0);
	return value;
}

typedef std::vector<lowir_model::SymbolId> ReadSymbolIndex;

lowir_model::SymbolId ReadSymbol(BinaryReader& in,
	lowir_model::LowirProgram& program, ReadSymbolIndex& symbols)
{
	const std::string name = in.String();
	const lowir_model::StringId spelling = program.strings.intern(name);
	const std::uint32_t id = spelling;
	if (id >= symbols.size()) symbols.resize(id + 1);
	if (!symbols[id].valid())
		symbols[id] = lowir_model::append_lowir_symbol(program, spelling);
	return symbols[id];
}

void WriteGlobalDeclaration(BinaryWriter& out,
	const lowir_model::GlobalDeclaration& value,
	const lowir_model::LowirProgram& program)
{
	out.String(lowir_model::lowir_symbol_name(program, value.symbol));
	out.Bool(value.has_type);
	WriteType(out, value.type);
	WriteEnum(out, value.storage);
	WriteSymbolMetadata(out, value.metadata, program);
}

lowir_model::GlobalDeclaration ReadGlobalDeclaration(BinaryReader& in,
	lowir_model::LowirProgram& program, ReadSymbolIndex& symbols)
{
	lowir_model::GlobalDeclaration value;
	value.symbol = ReadSymbol(in, program, symbols);
	value.has_type = in.Bool();
	value.type = ReadType(in);
	value.storage = ReadEnum<lowir_model::GlobalStorageMode>(in);
	value.metadata = ReadSymbolMetadata(in, program.strings);
	return value;
}

void WriteGlobal(BinaryWriter& out, const lowir_model::GlobalDefinition& value,
	const lowir_model::LowirProgram& program)
{
	out.String(lowir_model::lowir_symbol_name(program, value.symbol));
	out.Bool(value.structured);
	WriteEnum(out, value.storage);
	WriteType(out, value.type);
	WriteEnum(out, value.init_kind);
	WriteOperand(out, value.init_operand, program);
	out.I64(value.addr_addend);
	out.U64(value.data_items.size());
	for (std::size_t i = 0; i < value.data_items.size(); ++i)
	{
		const lowir_model::GlobalDefinition::DataItem& item = value.data_items[i];
		WriteEnum(out, item.kind);
		WriteType(out, item.type);
		WriteOperand(out, item.literal_operand, program);
		out.String(item.kind == lowir_model::GlobalDefinition::DataItem::ITEM_ADDR ?
			lowir_model::lowir_symbol_name(program, item.symbol_id) :
			item.symbol_spelling.valid() ?
				program.strings.get(item.symbol_spelling) : std::string());
		out.I64(item.addr_addend);
		out.U64(item.zero_bytes);
	}
	WriteSymbolMetadata(out, value.metadata, program);
}

lowir_model::GlobalDefinition ReadGlobal(BinaryReader& in,
	lowir_model::LowirProgram& program, ReadSymbolIndex& symbols)
{
	lowir_model::GlobalDefinition value;
	value.symbol = ReadSymbol(in, program, symbols);
	value.structured = in.Bool();
	value.storage = ReadEnum<lowir_model::GlobalStorageMode>(in);
	value.type = ReadType(in);
	value.init_kind = ReadEnum<lowir_model::GlobalDefinition::InitKind>(in);
	value.init_operand = ReadOperand(in, program.strings);
	value.addr_addend = in.I64();
	value.data_items.resize(in.Count(4));
	for (std::size_t i = 0; i < value.data_items.size(); ++i)
	{
		lowir_model::GlobalDefinition::DataItem& item = value.data_items[i];
		item.kind = ReadEnum<lowir_model::GlobalDefinition::DataItem::Kind>(in);
		item.type = ReadType(in);
		item.literal_operand = ReadOperand(in, program.strings);
		const std::string symbol = in.String();
		if (!symbol.empty()) item.symbol_spelling = program.strings.intern(symbol);
		item.addr_addend = in.I64();
		item.zero_bytes = in.Size();
	}
	value.metadata = ReadSymbolMetadata(in, program.strings);
	return value;
}

void WriteFunctionDeclaration(BinaryWriter& out,
	const lowir_model::FunctionDeclaration& value,
	const lowir_model::LowirProgram& program)
{
	out.String(lowir_model::lowir_symbol_name(program, value.symbol));
	WriteParameters(out, value.params, program);
	WriteType(out, value.return_type);
	WriteBoundary(out, value.boundary);
	WriteSymbolMetadata(out, value.metadata, program);
}

lowir_model::FunctionDeclaration ReadFunctionDeclaration(BinaryReader& in,
	lowir_model::LowirProgram& program, ReadSymbolIndex& symbols)
{
	lowir_model::FunctionDeclaration value;
	value.symbol = ReadSymbol(in, program, symbols);
	value.params = ReadParameters(in, program.strings);
	value.return_type = ReadType(in);
	value.boundary = ReadBoundary(in);
	value.metadata = ReadSymbolMetadata(in, program.strings);
	return value;
}

void WriteFunction(BinaryWriter& out, const lowir_model::Function& value,
	const lowir_model::LowirProgram& program)
{
	out.String(lowir_model::lowir_symbol_name(program, value.symbol));
	WriteParameters(out, value.params, program);
	WriteType(out, value.return_type);
	out.U64(value.slots.size());
	for (std::size_t i = 0; i < value.slots.size(); ++i)
	{
		out.String(lowir_model::lowir_slot_name(
			program.strings, value, value.slots[i]));
		WriteType(out, lowir_model::lowir_slot_type(value, value.slots[i]));
	}
	out.U64(value.blocks.size());
	for (std::size_t i = 0; i < value.blocks.size(); ++i)
	{
		out.String(lowir_model::lowir_block_label(
			program.strings, value, value.blocks[i].id));
		out.U64(value.blocks[i].instructions.size());
		for (std::size_t j = 0; j < value.blocks[i].instructions.size(); ++j)
			WriteInstruction(out, value.blocks[i].instructions[j], program, value);
	}
	WriteDebug(out, value.debug_location, program);
	WriteBoundary(out, value.boundary);
	WriteSymbolMetadata(out, value.metadata, program);
}

lowir_model::Function ReadFunction(BinaryReader& in,
	lowir_model::LowirProgram& program, ReadSymbolIndex& symbols)
{
	lowir_model::Function value;
	value.symbol = ReadSymbol(in, program, symbols);
	value.params = ReadParameters(in, program.strings);
	value.return_type = ReadType(in);
	for (std::size_t i = 0; i < value.params.size(); ++i)
		value.params[i].value = lowir_model::append_lowir_value(
			value, value.params[i].name,
			value.params[i].type);
	const std::size_t slot_count = in.Count(8);
	for (std::size_t i = 0; i < slot_count; ++i)
	{
		const std::string name = in.String();
		lowir_model::append_lowir_slot(
			value, program.strings.intern(name), ReadType(in));
	}
	value.blocks.resize(in.Count(8));
	for (std::size_t i = 0; i < value.blocks.size(); ++i)
	{
		value.blocks[i].id =
			lowir_model::allocate_lowir_block_id(
				value, program.strings.intern(in.String()));
		value.blocks[i].instructions.resize(in.Count(4));
		for (std::size_t j = 0; j < value.blocks[i].instructions.size(); ++j)
			value.blocks[i].instructions[j] = ReadInstruction(in, program, value);
	}
	lowir_model::resolve_lowir_function_operands(value, program.strings);
	value.debug_location = ReadDebug(in, program.strings);
	value.boundary = ReadBoundary(in);
	value.metadata = ReadSymbolMetadata(in, program.strings);
	return value;
}

void WriteExport(BinaryWriter& out, const lowir_model::ExportedSymbol& value,
	const lowir_model::LowirProgram& program)
{
	out.String(lowir_model::lowir_symbol_name(program, value.internal_symbol));
	out.String(value.object_symbol.valid() ?
		program.strings.get(value.object_symbol) : std::string());
	out.String(value.thread_local_wrapper_object_symbol.valid() ?
		program.strings.get(value.thread_local_wrapper_object_symbol) :
		std::string());
	out.Bool(value.keep_internal_alias);
	out.Bool(value.prefer_local_object_binding);
	WriteEnum(out, value.linkage);
}

lowir_model::ExportedSymbol ReadExport(BinaryReader& in,
	lowir_model::LowirProgram& program, ReadSymbolIndex& symbols)
{
	lowir_model::ExportedSymbol value;
	value.internal_symbol = ReadSymbol(in, program, symbols);
	const std::string object_symbol = in.String();
	if (!object_symbol.empty())
		value.object_symbol = program.strings.intern(object_symbol);
	const std::string wrapper_symbol = in.String();
	if (!wrapper_symbol.empty())
		value.thread_local_wrapper_object_symbol =
			program.strings.intern(wrapper_symbol);
	value.keep_internal_alias = in.Bool();
	value.prefer_local_object_binding = in.Bool();
	value.linkage = ReadEnum<ir_model::SymbolLinkage>(in);
	return value;
}

void WriteProgram(BinaryWriter& out, const lowir_model::LowirProgram& value)
{
	out.U64(value.global_declarations.size());
	for (std::size_t i = 0; i < value.global_declarations.size(); ++i)
		WriteGlobalDeclaration(out, value.global_declarations[i], value);
	out.U64(value.globals.size());
	for (std::size_t i = 0; i < value.globals.size(); ++i)
		WriteGlobal(out, value.globals[i], value);
	out.U64(value.function_declarations.size());
	for (std::size_t i = 0; i < value.function_declarations.size(); ++i)
		WriteFunctionDeclaration(out, value.function_declarations[i], value);
	out.U64(value.functions.size());
	for (std::size_t i = 0; i < value.functions.size(); ++i)
		WriteFunction(out, value.functions[i], value);
	out.U64(value.object_aliases.size());
	for (std::size_t i = 0; i < value.object_aliases.size(); ++i)
	{
		out.String(value.strings.get(value.object_aliases[i].object_symbol));
		out.String(lowir_model::lowir_symbol_name(
			value, value.object_aliases[i].target_id));
	}
	out.U64(value.exported_symbols.size());
	for (std::size_t i = 0; i < value.exported_symbols.size(); ++i)
		WriteExport(out, value.exported_symbols[i], value);
	out.U64(value.source_bytes);
	out.U64(value.token_count);
}

lowir_model::LowirProgram ReadProgram(BinaryReader& in)
{
	lowir_model::LowirProgram value;
	ReadSymbolIndex symbols(1);
	value.global_declarations.resize(in.Count(8));
	for (std::size_t i = 0; i < value.global_declarations.size(); ++i)
		value.global_declarations[i] =
			ReadGlobalDeclaration(in, value, symbols);
	value.globals.resize(in.Count(8));
	for (std::size_t i = 0; i < value.globals.size(); ++i)
		value.globals[i] = ReadGlobal(in, value, symbols);
	value.function_declarations.resize(in.Count(8));
	for (std::size_t i = 0; i < value.function_declarations.size(); ++i)
		value.function_declarations[i] =
			ReadFunctionDeclaration(in, value, symbols);
	value.functions.resize(in.Count(8));
	for (std::size_t i = 0; i < value.functions.size(); ++i)
		value.functions[i] = ReadFunction(in, value, symbols);
	value.object_aliases.resize(in.Count(16));
	for (std::size_t i = 0; i < value.object_aliases.size(); ++i)
	{
		value.object_aliases[i].object_symbol = value.strings.intern(in.String());
		value.object_aliases[i].target_spelling =
			value.strings.intern(in.String());
	}
	value.exported_symbols.resize(in.Count(8));
	for (std::size_t i = 0; i < value.exported_symbols.size(); ++i)
		value.exported_symbols[i] = ReadExport(in, value, symbols);
	value.source_bytes = in.Size();
	value.token_count = in.Size();
	lowir_model::resolve_lowir_program_symbols(value);
	return value;
}

std::uint64_t ReadHeaderLittle(const unsigned char* bytes, unsigned width)
{
	std::uint64_t value = 0;
	for (unsigned i = 0; i < width; ++i)
		value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
	return value;
}

struct CompilerPayloadLocation
{
	std::uint64_t offset;
	std::uint64_t size;
	bool found;

	CompilerPayloadLocation() : offset(0), size(0), found(false) {}
};

bool ReadAt(std::ifstream& input, std::uint64_t offset,
	unsigned char* bytes, std::size_t size)
{
	input.clear();
	input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
	return static_cast<bool>(input.read(reinterpret_cast<char*>(bytes),
		static_cast<std::streamsize>(size)));
}

CompilerPayloadLocation FindCompilerPayload(std::ifstream& input)
{
	CompilerPayloadLocation result;
	input.seekg(0, std::ios::end);
	const std::streampos end = input.tellg();
	if (end < 0) return result;
	const std::uint64_t file_size = static_cast<std::uint64_t>(end);
	unsigned char header[64];
	if (file_size < sizeof(kMagic) - 1 ||
		!ReadAt(input, 0, header,
			static_cast<std::size_t>(std::min<std::uint64_t>(file_size, 64))))
		return result;
	if (std::equal(header, header + sizeof(kMagic) - 1,
		reinterpret_cast<const unsigned char*>(kMagic)))
	{
		result.offset = 0;
		result.size = file_size;
		result.found = true;
		return result;
	}
	if (file_size < 64 || header[0] != 0x7f || header[1] != 'E' ||
		header[2] != 'L' || header[3] != 'F' || header[4] != 2 ||
		header[5] != 1)
		return result;
	const std::uint64_t section_offset = ReadHeaderLittle(header + 40, 8);
	const std::uint64_t section_entry_size = ReadHeaderLittle(header + 58, 2);
	const std::uint64_t section_count = ReadHeaderLittle(header + 60, 2);
	const std::uint64_t name_index = ReadHeaderLittle(header + 62, 2);
	if (section_entry_size != 64 || !section_count ||
		name_index >= section_count || section_count > kMaxObjectElements ||
		section_offset > file_size ||
		section_count > (file_size - section_offset) / section_entry_size)
		return result;
	unsigned char section[64];
	if (!ReadAt(input, section_offset + name_index * section_entry_size,
		section, sizeof(section))) return result;
	const std::uint64_t names_offset = ReadHeaderLittle(section + 24, 8);
	const std::uint64_t names_size = ReadHeaderLittle(section + 32, 8);
	if (names_offset > file_size || names_size > file_size - names_offset ||
		names_size > kMaxObjectElements) return result;
	std::string names(static_cast<std::size_t>(names_size), '\0');
	if (names_size && !ReadAt(input, names_offset,
		reinterpret_cast<unsigned char*>(&names[0]), names.size())) return result;
	for (std::uint64_t i = 1; i < section_count; ++i)
	{
		if (!ReadAt(input, section_offset + i * section_entry_size,
			section, sizeof(section))) return CompilerPayloadLocation();
		const std::uint64_t name_offset = ReadHeaderLittle(section, 4);
		if (name_offset >= names.size()) continue;
		const std::size_t name_end = names.find('\0',
			static_cast<std::size_t>(name_offset));
		if (name_end == std::string::npos ||
			names.substr(static_cast<std::size_t>(name_offset),
				name_end - static_cast<std::size_t>(name_offset)) !=
				".cppgm_object") continue;
		result.offset = ReadHeaderLittle(section + 24, 8);
		result.size = ReadHeaderLittle(section + 32, 8);
		if (result.offset > file_size || result.size > file_size - result.offset ||
			result.size < sizeof(kMagic) - 1)
			return CompilerPayloadLocation();
		unsigned char magic[sizeof(kMagic) - 1];
		if (!ReadAt(input, result.offset, magic, sizeof(magic)) ||
			!std::equal(magic, magic + sizeof(magic),
				reinterpret_cast<const unsigned char*>(kMagic)))
			return CompilerPayloadLocation();
		result.found = true;
		return result;
	}
	return result;
}

}
bool UsesPrivateFormat(const std::string& path)
{
	static const std::string suffix = ".obj";
	return path.size() >= suffix.size() &&
		path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void Write(const std::string& path,
	const Object& object, SerializationStats* stats)
{
	const std::vector<unsigned char> bytes =
		Serialize(object, stats);
	std::ofstream output(path.c_str(),
		std::ios::out | std::ios::binary | std::ios::trunc);
	if (!output)
		ThrowCompilerObjectInputOutputError("unable to open object output: " + path);
	if (!bytes.empty()) output.write(reinterpret_cast<const char*>(&bytes[0]),
		static_cast<std::streamsize>(bytes.size()));
	if (!output)
		ThrowCompilerObjectInputOutputError("unable to write object output: " + path);
}

std::vector<unsigned char> Serialize(
	const Object& object, SerializationStats* stats)
{
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	if (stats) *stats = SerializationStats();
	std::vector<unsigned char> output;
	const std::size_t reserve = EstimateProgramPayloadSize(object.lowir);
	if (reserve) output.reserve(reserve);
	if (stats) stats->reserved_bytes = reserve;
	output.insert(output.end(), kMagic, kMagic + sizeof(kMagic) - 1);
	BinaryWriter payload(output, stats);
	payload.U32(kVersion);
	payload.String(object.target);
	WriteProgram(payload, object.lowir);
	if (stats)
	{
		stats->output_bytes = output.size();
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count());
	}
	return output;
}

bool IsObject(const std::string& path)
{
	std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
	if (!input) return false;
	return FindCompilerPayload(input).found;
}

Object Read(const std::string& path)
{
	std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
	if (!file)
		ThrowCompilerObjectInputOutputError("unable to open object file: " + path);
	const CompilerPayloadLocation location = FindCompilerPayload(file);
	if (!location.found)
	{
		if (file.bad())
			ThrowCompilerObjectInputOutputError("unable to read object file: " + path);
		ThrowCompilerObjectInputError("not a cppgm compiler object: " + path);
	}
	file.clear();
	file.seekg(static_cast<std::streamoff>(location.offset), std::ios::beg);
	if (!file)
		ThrowCompilerObjectInputOutputError("unable to seek object file: " + path);
	char magic[sizeof(kMagic) - 1];
	file.read(magic, sizeof(magic));
	if (file.gcount() != static_cast<std::streamsize>(sizeof(magic)) ||
		!std::equal(magic, magic + sizeof(magic), kMagic))
	{
		if (file.bad())
			ThrowCompilerObjectInputOutputError("unable to read object file: " + path);
		ThrowCompilerObjectInputError("not a cppgm compiler object: " + path);
	}
	BinaryReader input(file, location.size - (sizeof(kMagic) - 1));
	if (input.U32() != kVersion)
		ThrowCompilerObjectInputError("unsupported cppgm object version");
	Object result;
	result.target = input.String();
	try
	{
		result.lowir = ReadProgram(input);
	}
	catch (const SerializedInputError& error)
	{
		ThrowCompilerObjectInputError(error.what());
	}
	input.RequireEnd();
	return result;
}

}
}
