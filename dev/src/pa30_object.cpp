#include "pa30_object.h"
#include "lowir_float_literal.h"
#include "lowir_prepare.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cppgm
{
namespace pa30
{
namespace
{

const char kMagic[] = "CPPGMOBJ";
const std::uint32_t kVersion = 2;
const std::uint64_t kMaxObjectElements = UINT64_C(1) << 28;

class Writer
{
public:
	Writer(std::vector<unsigned char>& output, ObjectSerializationStats* stats)
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
			throw std::runtime_error("compiler object is too large");
		if (output_.size() + additional > output_.capacity() && stats_)
			++stats_->buffer_growths;
	}

	void Append(const unsigned char* bytes, std::size_t size)
	{
		Ensure(size);
		output_.insert(output_.end(), bytes, bytes + size);
	}

	std::vector<unsigned char>& output_;
	ObjectSerializationStats* stats_;
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

class Reader
{
public:
	Reader(std::istream& input, std::uint64_t remaining)
		: input_(input), remaining_(remaining) {}

	std::uint8_t Byte()
	{
		Take(1);
		const int value = input_.get();
		if (value == std::char_traits<char>::eof())
			throw std::runtime_error("truncated compiler object");
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
		if (value > 1) throw std::runtime_error("invalid compiler object boolean");
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
			throw std::runtime_error("invalid compiler object collection size");
		return value;
	}

	std::size_t CheckedSize(std::uint64_t value)
	{
		if (value > kMaxObjectElements ||
			value > std::numeric_limits<std::size_t>::max())
			throw std::runtime_error("compiler object collection is too large");
		return static_cast<std::size_t>(value);
	}

	std::string String()
	{
		const std::size_t size = CheckedSize(U64());
		if (size > remaining_)
			throw std::runtime_error("truncated compiler object");
		std::string result(size, '\0');
		if (size) Take(size);
		if (size && !input_.read(&result[0], static_cast<std::streamsize>(size)))
			throw std::runtime_error("truncated compiler object");
		return result;
	}

	void RequireEnd()
	{
		if (remaining_ != 0)
			throw std::runtime_error("trailing bytes in compiler object");
		if (input_.bad()) throw std::runtime_error("unable to read compiler object");
	}

private:
	void Take(std::uint64_t size)
	{
		if (size > remaining_)
			throw std::runtime_error("truncated compiler object");
		remaining_ -= size;
	}

	std::uint64_t ReadLittle(unsigned width)
	{
		Take(width);
		char bytes[8];
		if (!input_.read(bytes, width))
			throw std::runtime_error("truncated compiler object");
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
void WriteEnum(Writer& out, Enum value)
{
	out.U32(static_cast<std::uint32_t>(value));
}

template <typename Enum>
Enum ReadEnum(Reader& in)
{
	return static_cast<Enum>(in.U32());
}

void WriteType(Writer& out, const lowir_model::LowType& value)
{
	out.String(lowir_model::lowir_type_text(value));
	WriteEnum(out, value.kind);
	out.U64(lowir_model::lowir_type_bit_width(value));
	out.U64(value.storage_size);
	out.U64(value.alignment);
}

lowir_model::LowType ReadType(Reader& in)
{
	lowir_model::LowType value;
	(void)in.String();
	value.kind = ReadEnum<lowir_model::LowTypeKind>(in);
	const std::size_t serialized_width = in.Size();
	value.storage_size = in.Size();
	const std::size_t alignment = in.Size();
	if (alignment > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("compiler object type alignment is too large");
	value.alignment = static_cast<std::uint32_t>(alignment);
	if (serialized_width != lowir_model::lowir_type_bit_width(value))
		throw std::runtime_error("compiler object type width is inconsistent");
	return value;
}

void WriteOperand(Writer& out, const lowir_model::Operand& value,
	const lowir_model::LowirProgram& program,
	const lowir_model::Function* function = 0)
{
	WriteEnum(out, value.kind);
	if (value.kind == lowir_model::Operand::OP_LABEL)
	{
		if (!function)
			throw std::logic_error("compiler object block target lacks a function");
		out.String(lowir_model::lowir_block_label(
			program.strings, *function, value.block));
	}
	else if (value.kind == lowir_model::Operand::OP_SLOT)
	{
		if (!function)
			throw std::logic_error("compiler object slot lacks a function");
		out.String(lowir_model::lowir_slot_name(
			program.strings, *function, value.slot));
	}
	else if (value.kind == lowir_model::Operand::OP_TEMP)
	{
		if (!function)
			throw std::logic_error("compiler object value lacks a function");
		out.String(lowir_model::lowir_value_name(
			program.strings, *function, value.value));
	}
	else if (value.kind == lowir_model::Operand::OP_GLOBAL)
		out.String(lowir_model::lowir_symbol_name(program, value.symbol));
	else out.String(value.has_spelling ?
		program.strings.get(value.literal) : std::string());
	out.I64(value.kind == lowir_model::Operand::OP_INTEGER && value.has_int_value ?
		value.int_value : 0);
}

lowir_model::Operand ReadOperand(Reader& in,
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
		lowir_model::parse_lowir_floating_literal(
			spelling, &value.float_value);
	return value;
}

void WriteSymbolMetadata(Writer& out,
	const lowir_model::SymbolMetadata& value)
{
	WriteEnum(out, value.role);
	WriteEnum(out, value.linkage);
	WriteEnum(out, value.binding);
	out.String(value.object_symbol);
	out.String(value.tls_for_symbol);
	out.String(value.section_segment);
	out.String(value.section_name);
	out.Bool(value.keep_internal_alias);
	out.Bool(value.prefer_local_object_binding);
	out.Bool(value.object_output_root);
	out.Bool(value.object_trivial_lifecycle);
	out.Bool(value.force_inline);
	out.Bool(value.no_inline);
}

lowir_model::SymbolMetadata ReadSymbolMetadata(Reader& in)
{
	lowir_model::SymbolMetadata value;
	value.role = ReadEnum<lowir_model::SymbolRole>(in);
	value.linkage = ReadEnum<lowir_model::LanguageLinkageMode>(in);
	value.binding = ReadEnum<lowir_model::SymbolBindingMode>(in);
	value.object_symbol = in.String();
	value.tls_for_symbol = in.String();
	value.section_segment = in.String();
	value.section_name = in.String();
	value.keep_internal_alias = in.Bool();
	value.prefer_local_object_binding = in.Bool();
	value.object_output_root = in.Bool();
	value.object_trivial_lifecycle = in.Bool();
	value.force_inline = in.Bool();
	value.no_inline = in.Bool();
	return value;
}

void WriteBoundary(Writer& out,
	const lowir_model::FunctionBoundaryMetadata& value)
{
	WriteEnum(out, value.arity);
	WriteEnum(out, value.effects);
	WriteEnum(out, value.unwind);
	WriteEnum(out, value.returns);
}

lowir_model::FunctionBoundaryMetadata ReadBoundary(Reader& in)
{
	lowir_model::FunctionBoundaryMetadata value;
	value.arity = ReadEnum<lowir_model::CallArityMode>(in);
	value.effects = ReadEnum<lowir_model::CallEffectsMode>(in);
	value.unwind = ReadEnum<lowir_model::CallUnwindMode>(in);
	value.returns = ReadEnum<lowir_model::CallReturnMode>(in);
	return value;
}

void WriteParameter(Writer& out, const lowir_model::Parameter& value,
	const lowir_model::LowirProgram& program)
{
	out.String(lowir_model::lowir_parameter_name(program, value));
	WriteType(out, value.type);
	WriteEnum(out, value.metadata.passing);
	WriteEnum(out, value.metadata.capture);
	WriteEnum(out, value.metadata.access);
	WriteEnum(out, value.metadata.alias);
}

lowir_model::Parameter ReadParameter(Reader& in,
	lowir_model::StringPool& strings)
{
	lowir_model::Parameter value;
	value.name = strings.intern(in.String());
	value.type = ReadType(in);
	value.metadata.passing = ReadEnum<lowir_model::ParamPassingMode>(in);
	value.metadata.capture = ReadEnum<lowir_model::ParamCaptureMode>(in);
	value.metadata.access = ReadEnum<lowir_model::ParamAccessMode>(in);
	value.metadata.alias = ReadEnum<lowir_model::ParamAliasMode>(in);
	return value;
}

void WriteParameters(Writer& out,
	const std::vector<lowir_model::Parameter>& values,
	const lowir_model::LowirProgram& program)
{
	out.U64(values.size());
	for (std::size_t i = 0; i < values.size(); ++i)
		WriteParameter(out, values[i], program);
}

std::vector<lowir_model::Parameter> ReadParameters(Reader& in,
	lowir_model::StringPool& strings)
{
	std::vector<lowir_model::Parameter> values(in.Count(8));
	for (std::size_t i = 0; i < values.size(); ++i)
		values[i] = ReadParameter(in, strings);
	return values;
}

void WriteDebug(Writer& out,
	const lowir_model::InstructionDebugLocation& value,
	const lowir_model::LowirProgram& program)
{
	out.String(value.file.valid() ? program.strings.get(value.file) : std::string());
	out.U64(value.line);
	out.U64(value.column);
}

lowir_model::InstructionDebugLocation ReadDebug(Reader& in,
	lowir_model::StringPool& strings)
{
	lowir_model::InstructionDebugLocation value;
	const std::string file = in.String();
	if (!file.empty()) value.file = strings.intern(file);
	value.line = in.Size();
	value.column = in.Size();
	return value;
}

void WriteInstruction(Writer& out, const lowir_model::Instruction& value,
	const lowir_model::LowirProgram& program,
	const lowir_model::Function& function)
{
	WriteEnum(out, value.kind);
	out.String(value.dest.valid() ?
		lowir_model::lowir_value_name(
			program.strings, function, value.dest) : std::string());
	WriteType(out, value.type);
	WriteType(out, value.source_type);
	out.String(value.op);
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

lowir_model::Instruction ReadInstruction(Reader& in,
	lowir_model::LowirProgram& program, lowir_model::Function& function)
{
	lowir_model::Instruction value;
	value.kind = ReadEnum<lowir_model::Instruction::Kind>(in);
	const std::string destination = in.String();
	value.type = ReadType(in);
	value.source_type = ReadType(in);
	value.op = in.String();
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

void WriteGlobalDeclaration(Writer& out,
	const lowir_model::GlobalDeclaration& value)
{
	out.String(value.name);
	out.Bool(value.has_type);
	WriteType(out, value.type);
	WriteEnum(out, value.storage);
	WriteSymbolMetadata(out, value.metadata);
}

lowir_model::GlobalDeclaration ReadGlobalDeclaration(Reader& in)
{
	lowir_model::GlobalDeclaration value;
	value.name = in.String();
	value.has_type = in.Bool();
	value.type = ReadType(in);
	value.storage = ReadEnum<lowir_model::GlobalStorageMode>(in);
	value.metadata = ReadSymbolMetadata(in);
	return value;
}

void WriteGlobal(Writer& out, const lowir_model::GlobalDefinition& value,
	const lowir_model::LowirProgram& program)
{
	out.String(value.name);
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
			lowir_model::lowir_symbol_name(program, item.symbol_id) : item.symbol);
		out.I64(item.addr_addend);
		out.U64(item.zero_bytes);
	}
	WriteSymbolMetadata(out, value.metadata);
}

lowir_model::GlobalDefinition ReadGlobal(Reader& in,
	lowir_model::LowirProgram& program)
{
	lowir_model::GlobalDefinition value;
	value.name = in.String();
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
		item.symbol = in.String();
		item.addr_addend = in.I64();
		item.zero_bytes = in.Size();
	}
	value.metadata = ReadSymbolMetadata(in);
	return value;
}

void WriteFunctionDeclaration(Writer& out,
	const lowir_model::FunctionDeclaration& value,
	const lowir_model::LowirProgram& program)
{
	out.String(value.name);
	WriteParameters(out, value.params, program);
	WriteType(out, value.return_type);
	WriteBoundary(out, value.boundary);
	WriteSymbolMetadata(out, value.metadata);
}

lowir_model::FunctionDeclaration ReadFunctionDeclaration(Reader& in,
	lowir_model::LowirProgram& program)
{
	lowir_model::FunctionDeclaration value;
	value.name = in.String();
	value.params = ReadParameters(in, program.strings);
	value.return_type = ReadType(in);
	value.boundary = ReadBoundary(in);
	value.metadata = ReadSymbolMetadata(in);
	return value;
}

void WriteFunction(Writer& out, const lowir_model::Function& value,
	const lowir_model::LowirProgram& program)
{
	out.String(value.name);
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
	WriteSymbolMetadata(out, value.metadata);
}

lowir_model::Function ReadFunction(Reader& in,
	lowir_model::LowirProgram& program)
{
	lowir_model::Function value;
	value.name = in.String();
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
	value.metadata = ReadSymbolMetadata(in);
	return value;
}

void WriteExport(Writer& out, const ir_model::ExportedSymbol& value)
{
	out.String(value.internal_symbol);
	out.String(value.object_symbol);
	out.String(value.thread_local_wrapper_object_symbol);
	out.Bool(value.keep_internal_alias);
	out.Bool(value.prefer_local_object_binding);
	WriteEnum(out, value.linkage);
}

ir_model::ExportedSymbol ReadExport(Reader& in)
{
	ir_model::ExportedSymbol value;
	value.internal_symbol = in.String();
	value.object_symbol = in.String();
	value.thread_local_wrapper_object_symbol = in.String();
	value.keep_internal_alias = in.Bool();
	value.prefer_local_object_binding = in.Bool();
	value.linkage = ReadEnum<ir_model::SymbolLinkage>(in);
	return value;
}

void WriteProgram(Writer& out, const lowir_model::LowirProgram& value)
{
	out.U64(value.global_declarations.size());
	for (std::size_t i = 0; i < value.global_declarations.size(); ++i)
		WriteGlobalDeclaration(out, value.global_declarations[i]);
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
		out.String(value.object_aliases[i].object_symbol);
		out.String(value.object_aliases[i].target);
	}
	out.U64(value.exported_symbols.size());
	for (std::size_t i = 0; i < value.exported_symbols.size(); ++i)
		WriteExport(out, value.exported_symbols[i]);
	out.U64(value.source_bytes);
	out.U64(value.token_count);
}

lowir_model::LowirProgram ReadProgram(Reader& in)
{
	lowir_model::LowirProgram value;
	value.global_declarations.resize(in.Count(8));
	for (std::size_t i = 0; i < value.global_declarations.size(); ++i)
		value.global_declarations[i] = ReadGlobalDeclaration(in);
	value.globals.resize(in.Count(8));
	for (std::size_t i = 0; i < value.globals.size(); ++i)
		value.globals[i] = ReadGlobal(in, value);
	value.function_declarations.resize(in.Count(8));
	for (std::size_t i = 0; i < value.function_declarations.size(); ++i)
		value.function_declarations[i] = ReadFunctionDeclaration(in, value);
	value.functions.resize(in.Count(8));
	for (std::size_t i = 0; i < value.functions.size(); ++i)
		value.functions[i] = ReadFunction(in, value);
	value.object_aliases.resize(in.Count(16));
	for (std::size_t i = 0; i < value.object_aliases.size(); ++i)
	{
		value.object_aliases[i].object_symbol = in.String();
		value.object_aliases[i].target = in.String();
	}
	value.exported_symbols.resize(in.Count(8));
	for (std::size_t i = 0; i < value.exported_symbols.size(); ++i)
		value.exported_symbols[i] = ReadExport(in);
	value.source_bytes = in.Size();
	value.token_count = in.Size();
	return value;
}

typedef std::unordered_map<std::string, std::string> RenameMap;

void RenameString(std::string* value, const RenameMap& names,
	LinkStats* stats)
{
	if (value->empty()) return;
	if (stats) ++stats->rename_probes;
	const RenameMap::const_iterator found = names.find(*value);
	if (found != names.end()) *value = found->second;
}

void RenameOperand(lowir_model::LowirProgram* program,
	lowir_model::Operand* value, const RenameMap& names, LinkStats* stats)
{
	if (value->kind != lowir_model::Operand::OP_GLOBAL ||
		!value->has_spelling) return;
	const std::string& spelling = program->strings.get(value->literal);
	if (stats) ++stats->rename_probes;
	const RenameMap::const_iterator found = names.find(spelling);
	if (found != names.end())
		value->literal = program->strings.intern(found->second);
}

void RenameMetadata(lowir_model::SymbolMetadata* value,
	const RenameMap& names, LinkStats* stats)
{
	RenameString(&value->tls_for_symbol, names, stats);
}

void RenameProgram(lowir_model::LowirProgram* program,
	const RenameMap& names, LinkStats* stats)
{
	for (std::size_t i = 0; i < program->symbol_names.size(); ++i)
		RenameString(&program->symbol_names[i], names, stats);
	for (std::size_t i = 0; i < program->global_declarations.size(); ++i)
	{
		lowir_model::GlobalDeclaration& item = program->global_declarations[i];
		RenameString(&item.name, names, stats);
		RenameMetadata(&item.metadata, names, stats);
	}
	for (std::size_t i = 0; i < program->globals.size(); ++i)
	{
		lowir_model::GlobalDefinition& item = program->globals[i];
		RenameString(&item.name, names, stats);
		RenameOperand(program, &item.init_operand, names, stats);
		for (std::size_t j = 0; j < item.data_items.size(); ++j)
		{
			RenameOperand(program, &item.data_items[j].literal_operand,
				names, stats);
			RenameString(&item.data_items[j].symbol, names, stats);
		}
		RenameMetadata(&item.metadata, names, stats);
	}
	for (std::size_t i = 0; i < program->function_declarations.size(); ++i)
	{
		lowir_model::FunctionDeclaration& item =
			program->function_declarations[i];
		RenameString(&item.name, names, stats);
		RenameMetadata(&item.metadata, names, stats);
	}
	for (std::size_t i = 0; i < program->functions.size(); ++i)
	{
		lowir_model::Function& item = program->functions[i];
		RenameString(&item.name, names, stats);
		RenameMetadata(&item.metadata, names, stats);
		for (std::size_t j = 0; j < item.blocks.size(); ++j)
			for (std::size_t k = 0; k < item.blocks[j].instructions.size(); ++k)
			{
				lowir_model::Instruction& instruction =
					item.blocks[j].instructions[k];
				RenameOperand(program, &instruction.first, names, stats);
				RenameOperand(program, &instruction.second, names, stats);
				RenameOperand(program, &instruction.third, names, stats);
				for (std::size_t a = 0; a < instruction.args.size(); ++a)
					RenameOperand(program, &instruction.args[a], names, stats);
			}
	}
	for (std::size_t i = 0; i < program->object_aliases.size(); ++i)
		RenameString(&program->object_aliases[i].target, names, stats);
}

lowir_model::Function MakeLifecycleAggregate(lowir_model::LowirProgram& program,
	const std::string& name,
	lowir_model::SymbolRole role, const std::vector<std::string>& functions,
	bool reverse)
{
	lowir_model::Function result;
	result.name = name;
	result.return_type = lowir_model::builtin_lowir_type(lowir_model::LTK_VOID);
	result.metadata.role = role;
	result.metadata.binding = lowir_model::SBM_INTERNAL;
	lowir_model::Block block;
	block.id = lowir_model::allocate_lowir_block_id(
		result, program.strings.intern("^entry"));
	for (std::size_t i = 0; i < functions.size(); ++i)
	{
		const std::size_t index = reverse ? functions.size() - i - 1 : i;
		lowir_model::Instruction call;
		call.kind = lowir_model::Instruction::IK_CALL;
		call.call_returns_void = true;
		call.type = lowir_model::builtin_lowir_type(lowir_model::LTK_VOID);
		call.first.kind = lowir_model::Operand::OP_GLOBAL;
		call.first.literal = program.strings.intern(functions[index]);
		call.first.has_spelling = true;
		block.instructions.push_back(call);
	}
	lowir_model::Instruction ret;
	ret.kind = lowir_model::Instruction::IK_RETURN;
	ret.type = lowir_model::builtin_lowir_type(lowir_model::LTK_VOID);
	block.instructions.push_back(ret);
	result.blocks.push_back(block);
	return result;
}

bool IsWeak(lowir_model::SymbolBindingMode binding)
{
	return binding == lowir_model::SBM_WEAK;
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

LinkStats::LinkStats()
	: objects(0), symbols(0), symbol_probes(0), rename_probes(0),
	  definitions(0), coalesced_weak_definitions(0), link_nanoseconds(0) {}

bool UsesPrivateCompilerObjectFormat(const std::string& path)
{
	static const std::string suffix = ".obj";
	return path.size() >= suffix.size() &&
		path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void WriteCompilerObject(const std::string& path,
	const CompilerObject& object, ObjectSerializationStats* stats)
{
	const std::vector<unsigned char> bytes =
		SerializeCompilerObject(object, stats);
	std::ofstream output(path.c_str(),
		std::ios::out | std::ios::binary | std::ios::trunc);
	if (!output) throw std::runtime_error("unable to open object output: " + path);
	if (!bytes.empty()) output.write(reinterpret_cast<const char*>(&bytes[0]),
		static_cast<std::streamsize>(bytes.size()));
	if (!output) throw std::runtime_error("unable to write object output: " + path);
}

std::vector<unsigned char> SerializeCompilerObject(
	const CompilerObject& object, ObjectSerializationStats* stats)
{
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	if (stats) *stats = ObjectSerializationStats();
	std::vector<unsigned char> output;
	const std::size_t reserve = EstimateProgramPayloadSize(object.lowir);
	if (reserve) output.reserve(reserve);
	if (stats) stats->reserved_bytes = reserve;
	output.insert(output.end(), kMagic, kMagic + sizeof(kMagic) - 1);
	Writer payload(output, stats);
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

bool IsCompilerObject(const std::string& path)
{
	std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
	if (!input) return false;
	return FindCompilerPayload(input).found;
}

CompilerObject ReadCompilerObject(const std::string& path)
{
	std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
	if (!file) throw std::runtime_error("unable to open object file: " + path);
	const CompilerPayloadLocation location = FindCompilerPayload(file);
	if (!location.found)
		throw std::runtime_error("not a cppgm compiler object: " + path);
	file.clear();
	file.seekg(static_cast<std::streamoff>(location.offset), std::ios::beg);
	char magic[sizeof(kMagic) - 1];
	file.read(magic, sizeof(magic));
	if (file.gcount() != static_cast<std::streamsize>(sizeof(magic)) ||
		!std::equal(magic, magic + sizeof(magic), kMagic))
		throw std::runtime_error("not a cppgm compiler object: " + path);
	Reader input(file, location.size - (sizeof(kMagic) - 1));
	if (input.U32() != kVersion)
		throw std::runtime_error("unsupported cppgm object version");
	CompilerObject result;
	result.target = input.String();
	result.lowir = ReadProgram(input);
	input.RequireEnd();
	return result;
}

lowir_model::LowirProgram LinkCompilerObjects(
	std::vector<CompilerObject> objects, const std::string& target,
	LinkStats* stats)
{
	if (objects.empty()) throw std::runtime_error("no linker inputs");
	if (stats) *stats = LinkStats();
	std::chrono::steady_clock::time_point started;
	if (stats) started = std::chrono::steady_clock::now();
	std::unordered_map<std::string, std::string> external_names;
	lowir_model::LowirProgram result;
	for (std::size_t i = 0; i < objects.size(); ++i)
	{
		if (objects[i].target != target)
			throw std::runtime_error("link input target mismatch");
		RenameMap names;
		for (std::size_t j = 0; j < objects[i].lowir.exported_symbols.size(); ++j)
		{
			const ir_model::ExportedSymbol& symbol =
				objects[i].lowir.exported_symbols[j];
			if (stats) { ++stats->symbols; ++stats->symbol_probes; }
			if (symbol.linkage == ir_model::SL_INTERNAL ||
				symbol.prefer_local_object_binding)
				names[symbol.internal_symbol] = symbol.internal_symbol +
					".__u" + std::to_string(i);
			else
			{
				const std::string key = symbol.object_symbol.empty() ?
					symbol.internal_symbol : symbol.object_symbol;
				const std::pair<std::unordered_map<std::string, std::string>::iterator,
					bool> inserted = external_names.emplace(key,
						symbol.internal_symbol);
				names[symbol.internal_symbol] = inserted.first->second;
			}
		}
		RenameProgram(&objects[i].lowir, names, stats);
		lowir_model::materialize_lowir_program_symbol_spellings(
			objects[i].lowir);
		lowir_model::remap_lowir_program_strings(
			objects[i].lowir, result.strings);
		std::vector<ir_model::ExportedSymbol>().swap(
			objects[i].lowir.exported_symbols);
	}

	std::unordered_map<std::string, std::size_t> globals;
	std::unordered_map<std::string, std::size_t> functions;
	std::vector<std::string> initializers;
	std::vector<std::string> finalizers;
	for (std::size_t unit = 0; unit < objects.size(); ++unit)
	{
		lowir_model::LowirProgram& program = objects[unit].lowir;
		result.source_bytes += program.source_bytes;
		result.token_count += program.token_count;
		for (std::size_t i = 0; i < program.globals.size(); ++i)
		{
			lowir_model::GlobalDefinition& item = program.globals[i];
			if (stats) { ++stats->definitions; ++stats->symbol_probes; }
			const std::unordered_map<std::string, std::size_t>::iterator found =
				globals.find(item.name);
			if (found == globals.end())
			{
				globals[item.name] = result.globals.size();
				result.globals.push_back(std::move(item));
			}
			else if (IsWeak(item.metadata.binding))
			{
				if (stats) ++stats->coalesced_weak_definitions;
			}
			else if (IsWeak(result.globals[found->second].metadata.binding))
			{
				result.globals[found->second] = std::move(item);
				if (stats) ++stats->coalesced_weak_definitions;
			}
			else throw std::runtime_error("duplicate global definition: " + item.name);
		}
		for (std::size_t i = 0; i < program.functions.size(); ++i)
		{
			lowir_model::Function& item = program.functions[i];
			if (item.metadata.role == lowir_model::SR_INIT)
			{
				initializers.push_back(item.name);
				item.metadata.role = lowir_model::SR_NONE;
			}
			else if (item.metadata.role == lowir_model::SR_FINI)
			{
				finalizers.push_back(item.name);
				item.metadata.role = lowir_model::SR_NONE;
			}
			if (stats) { ++stats->definitions; ++stats->symbol_probes; }
			const std::unordered_map<std::string, std::size_t>::iterator found =
				functions.find(item.name);
			if (found == functions.end())
			{
				functions[item.name] = result.functions.size();
				result.functions.push_back(std::move(item));
			}
			else if (IsWeak(item.metadata.binding))
			{
				if (stats) ++stats->coalesced_weak_definitions;
			}
			else if (IsWeak(result.functions[found->second].metadata.binding))
			{
				result.functions[found->second] = std::move(item);
				if (stats) ++stats->coalesced_weak_definitions;
			}
			else throw std::runtime_error("duplicate function definition: " + item.name);
		}
		result.object_aliases.insert(result.object_aliases.end(),
			std::make_move_iterator(program.object_aliases.begin()),
			std::make_move_iterator(program.object_aliases.end()));
	}

	std::unordered_set<std::string> declared_globals;
	std::unordered_set<std::string> declared_functions;
	for (std::size_t unit = 0; unit < objects.size(); ++unit)
	{
		lowir_model::LowirProgram& program = objects[unit].lowir;
		for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
			if (!globals.count(program.global_declarations[i].name) &&
				declared_globals.insert(program.global_declarations[i].name).second)
				result.global_declarations.push_back(
					std::move(program.global_declarations[i]));
		for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
			if (!functions.count(program.function_declarations[i].name) &&
				declared_functions.insert(program.function_declarations[i].name).second)
				result.function_declarations.push_back(
					std::move(program.function_declarations[i]));
	}
	if (!initializers.empty())
		result.functions.push_back(MakeLifecycleAggregate(
			result, "@__cppgm_link_init", lowir_model::SR_INIT,
			initializers, false));
	if (!finalizers.empty())
		result.functions.push_back(MakeLifecycleAggregate(
			result, "@__cppgm_link_fini", lowir_model::SR_FINI,
			finalizers, true));

	std::unordered_map<std::string, std::string> aliases;
	std::vector<lowir_model::ObjectAlias> unique_aliases;
	for (std::size_t i = 0; i < result.object_aliases.size(); ++i)
	{
		const lowir_model::ObjectAlias& alias = result.object_aliases[i];
		const std::pair<std::unordered_map<std::string, std::string>::iterator,
			bool> inserted = aliases.emplace(alias.object_symbol, alias.target);
		if (inserted.second) unique_aliases.push_back(alias);
		else if (inserted.first->second != alias.target)
			throw std::runtime_error("conflicting object alias: " +
				alias.object_symbol);
	}
	result.object_aliases.swap(unique_aliases);
	lowir_model::resolve_lowir_program_symbols(result);
	lowir_model::finalize_lowir_object_model(result);
	if (stats)
	{
		stats->objects = objects.size();
		stats->link_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count());
	}
	return result;
}

}
}
