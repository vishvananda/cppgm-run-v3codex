#include "pa30_object.h"

#include <algorithm>
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
const std::uint32_t kVersion = 1;
const std::uint64_t kMaxObjectElements = UINT64_C(1) << 28;

class Writer
{
public:
	void Byte(std::uint8_t value) { bytes_.push_back(value); }

	void U32(std::uint32_t value)
	{
		for (unsigned i = 0; i < 4; ++i) Byte(value >> (i * 8));
	}

	void U64(std::uint64_t value)
	{
		for (unsigned i = 0; i < 8; ++i) Byte(value >> (i * 8));
	}

	void I64(std::int64_t value) { U64(static_cast<std::uint64_t>(value)); }
	void Bool(bool value) { Byte(value ? 1 : 0); }

	void String(const std::string& value)
	{
		U64(value.size());
		bytes_.insert(bytes_.end(), value.begin(), value.end());
	}

	const std::vector<unsigned char>& Bytes() const { return bytes_; }

private:
	std::vector<unsigned char> bytes_;
};

class Reader
{
public:
	explicit Reader(const std::vector<unsigned char>& bytes)
		: bytes_(bytes), at_(0) {}

	std::uint8_t Byte()
	{
		Require(1);
		return bytes_[at_++];
	}

	std::uint32_t U32()
	{
		std::uint32_t value = 0;
		for (unsigned i = 0; i < 4; ++i)
			value |= static_cast<std::uint32_t>(Byte()) << (i * 8);
		return value;
	}

	std::uint64_t U64()
	{
		std::uint64_t value = 0;
		for (unsigned i = 0; i < 8; ++i)
			value |= static_cast<std::uint64_t>(Byte()) << (i * 8);
		return value;
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
		const std::uint64_t value = U64();
		if (value > kMaxObjectElements ||
			value > std::numeric_limits<std::size_t>::max())
			throw std::runtime_error("compiler object collection is too large");
		return static_cast<std::size_t>(value);
	}

	std::string String()
	{
		const std::size_t size = Size();
		Require(size);
		const std::string result(bytes_.begin() + at_, bytes_.begin() + at_ + size);
		at_ += size;
		return result;
	}

	void RequireEnd() const
	{
		if (at_ != bytes_.size())
			throw std::runtime_error("trailing bytes in compiler object");
	}

private:
	void Require(std::size_t count) const
	{
		if (count > bytes_.size() - at_)
			throw std::runtime_error("truncated compiler object");
	}

	const std::vector<unsigned char>& bytes_;
	std::size_t at_;
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
	out.String(value.text);
	WriteEnum(out, value.kind);
	out.U64(value.bit_width);
	out.U64(value.storage_size);
	out.U64(value.alignment);
}

lowir_model::LowType ReadType(Reader& in)
{
	lowir_model::LowType value;
	value.text = in.String();
	value.kind = ReadEnum<lowir_model::LowTypeKind>(in);
	value.bit_width = in.Size();
	value.storage_size = in.Size();
	value.alignment = in.Size();
	return value;
}

void WriteOperand(Writer& out, const lowir_model::Operand& value)
{
	WriteEnum(out, value.kind);
	out.String(value.text);
	out.I64(value.int_value);
	WriteType(out, value.literal_type);
}

lowir_model::Operand ReadOperand(Reader& in)
{
	lowir_model::Operand value;
	value.kind = ReadEnum<lowir_model::Operand::Kind>(in);
	value.text = in.String();
	value.int_value = in.I64();
	value.literal_type = ReadType(in);
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

void WriteParameter(Writer& out, const lowir_model::Parameter& value)
{
	out.String(value.name);
	WriteType(out, value.type);
	WriteEnum(out, value.metadata.passing);
	WriteEnum(out, value.metadata.capture);
	WriteEnum(out, value.metadata.access);
	WriteEnum(out, value.metadata.alias);
}

lowir_model::Parameter ReadParameter(Reader& in)
{
	lowir_model::Parameter value;
	value.name = in.String();
	value.type = ReadType(in);
	value.metadata.passing = ReadEnum<lowir_model::ParamPassingMode>(in);
	value.metadata.capture = ReadEnum<lowir_model::ParamCaptureMode>(in);
	value.metadata.access = ReadEnum<lowir_model::ParamAccessMode>(in);
	value.metadata.alias = ReadEnum<lowir_model::ParamAliasMode>(in);
	return value;
}

void WriteParameters(Writer& out,
	const std::vector<lowir_model::Parameter>& values)
{
	out.U64(values.size());
	for (std::size_t i = 0; i < values.size(); ++i)
		WriteParameter(out, values[i]);
}

std::vector<lowir_model::Parameter> ReadParameters(Reader& in)
{
	std::vector<lowir_model::Parameter> values(in.Size());
	for (std::size_t i = 0; i < values.size(); ++i)
		values[i] = ReadParameter(in);
	return values;
}

void WriteDebug(Writer& out,
	const lowir_model::InstructionDebugLocation& value)
{
	out.String(value.file);
	out.U64(value.line);
	out.U64(value.column);
}

lowir_model::InstructionDebugLocation ReadDebug(Reader& in)
{
	lowir_model::InstructionDebugLocation value;
	value.file = in.String();
	value.line = in.Size();
	value.column = in.Size();
	return value;
}

void WriteInstruction(Writer& out, const lowir_model::Instruction& value)
{
	WriteEnum(out, value.kind);
	out.String(value.dest);
	WriteType(out, value.type);
	WriteType(out, value.source_type);
	out.String(value.op);
	out.U64(value.byte_count);
	out.U64(value.byte_alignment);
	out.Bool(value.has_eh_selector);
	out.I64(value.eh_selector);
	WriteEnum(out, value.index_projection);
	WriteOperand(out, value.first);
	WriteOperand(out, value.second);
	WriteOperand(out, value.third);
	out.U64(value.args.size());
	for (std::size_t i = 0; i < value.args.size(); ++i)
		WriteOperand(out, value.args[i]);
	out.Bool(value.call_returns_void);
	out.Bool(value.has_call_signature);
	WriteParameters(out, value.call_params);
	WriteType(out, value.call_return_type);
	WriteBoundary(out, value.call_boundary);
	WriteDebug(out, value.debug_location);
}

lowir_model::Instruction ReadInstruction(Reader& in)
{
	lowir_model::Instruction value;
	value.kind = ReadEnum<lowir_model::Instruction::Kind>(in);
	value.dest = in.String();
	value.type = ReadType(in);
	value.source_type = ReadType(in);
	value.op = in.String();
	value.byte_count = in.Size();
	value.byte_alignment = in.Size();
	value.has_eh_selector = in.Bool();
	value.eh_selector = in.I64();
	value.index_projection = ReadEnum<lowir_model::IndexProjectionKind>(in);
	value.first = ReadOperand(in);
	value.second = ReadOperand(in);
	value.third = ReadOperand(in);
	value.args.resize(in.Size());
	for (std::size_t i = 0; i < value.args.size(); ++i)
		value.args[i] = ReadOperand(in);
	value.call_returns_void = in.Bool();
	value.has_call_signature = in.Bool();
	value.call_params = ReadParameters(in);
	value.call_return_type = ReadType(in);
	value.call_boundary = ReadBoundary(in);
	value.debug_location = ReadDebug(in);
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

void WriteGlobal(Writer& out, const lowir_model::GlobalDefinition& value)
{
	out.String(value.name);
	out.Bool(value.structured);
	WriteEnum(out, value.storage);
	WriteType(out, value.type);
	WriteEnum(out, value.init_kind);
	WriteOperand(out, value.init_operand);
	out.I64(value.addr_addend);
	out.U64(value.data_items.size());
	for (std::size_t i = 0; i < value.data_items.size(); ++i)
	{
		const lowir_model::GlobalDefinition::DataItem& item = value.data_items[i];
		WriteEnum(out, item.kind);
		WriteType(out, item.type);
		WriteOperand(out, item.literal_operand);
		out.String(item.symbol);
		out.I64(item.addr_addend);
		out.U64(item.zero_bytes);
	}
	WriteSymbolMetadata(out, value.metadata);
}

lowir_model::GlobalDefinition ReadGlobal(Reader& in)
{
	lowir_model::GlobalDefinition value;
	value.name = in.String();
	value.structured = in.Bool();
	value.storage = ReadEnum<lowir_model::GlobalStorageMode>(in);
	value.type = ReadType(in);
	value.init_kind = ReadEnum<lowir_model::GlobalDefinition::InitKind>(in);
	value.init_operand = ReadOperand(in);
	value.addr_addend = in.I64();
	value.data_items.resize(in.Size());
	for (std::size_t i = 0; i < value.data_items.size(); ++i)
	{
		lowir_model::GlobalDefinition::DataItem& item = value.data_items[i];
		item.kind = ReadEnum<lowir_model::GlobalDefinition::DataItem::Kind>(in);
		item.type = ReadType(in);
		item.literal_operand = ReadOperand(in);
		item.symbol = in.String();
		item.addr_addend = in.I64();
		item.zero_bytes = in.Size();
	}
	value.metadata = ReadSymbolMetadata(in);
	return value;
}

void WriteFunctionDeclaration(Writer& out,
	const lowir_model::FunctionDeclaration& value)
{
	out.String(value.name);
	WriteParameters(out, value.params);
	WriteType(out, value.return_type);
	WriteBoundary(out, value.boundary);
	WriteSymbolMetadata(out, value.metadata);
}

lowir_model::FunctionDeclaration ReadFunctionDeclaration(Reader& in)
{
	lowir_model::FunctionDeclaration value;
	value.name = in.String();
	value.params = ReadParameters(in);
	value.return_type = ReadType(in);
	value.boundary = ReadBoundary(in);
	value.metadata = ReadSymbolMetadata(in);
	return value;
}

void WriteFunction(Writer& out, const lowir_model::Function& value)
{
	out.String(value.name);
	WriteParameters(out, value.params);
	WriteType(out, value.return_type);
	out.U64(value.slots.size());
	for (std::size_t i = 0; i < value.slots.size(); ++i)
	{
		out.String(value.slots[i].first);
		WriteType(out, value.slots[i].second);
	}
	out.U64(value.blocks.size());
	for (std::size_t i = 0; i < value.blocks.size(); ++i)
	{
		out.String(value.blocks[i].label);
		out.U64(value.blocks[i].instructions.size());
		for (std::size_t j = 0; j < value.blocks[i].instructions.size(); ++j)
			WriteInstruction(out, value.blocks[i].instructions[j]);
	}
	WriteDebug(out, value.debug_location);
	WriteBoundary(out, value.boundary);
	WriteSymbolMetadata(out, value.metadata);
}

lowir_model::Function ReadFunction(Reader& in)
{
	lowir_model::Function value;
	value.name = in.String();
	value.params = ReadParameters(in);
	value.return_type = ReadType(in);
	value.slots.resize(in.Size());
	for (std::size_t i = 0; i < value.slots.size(); ++i)
	{
		value.slots[i].first = in.String();
		value.slots[i].second = ReadType(in);
	}
	value.blocks.resize(in.Size());
	for (std::size_t i = 0; i < value.blocks.size(); ++i)
	{
		value.blocks[i].label = in.String();
		value.blocks[i].instructions.resize(in.Size());
		for (std::size_t j = 0; j < value.blocks[i].instructions.size(); ++j)
			value.blocks[i].instructions[j] = ReadInstruction(in);
	}
	value.debug_location = ReadDebug(in);
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
		WriteGlobal(out, value.globals[i]);
	out.U64(value.function_declarations.size());
	for (std::size_t i = 0; i < value.function_declarations.size(); ++i)
		WriteFunctionDeclaration(out, value.function_declarations[i]);
	out.U64(value.functions.size());
	for (std::size_t i = 0; i < value.functions.size(); ++i)
		WriteFunction(out, value.functions[i]);
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
	value.global_declarations.resize(in.Size());
	for (std::size_t i = 0; i < value.global_declarations.size(); ++i)
		value.global_declarations[i] = ReadGlobalDeclaration(in);
	value.globals.resize(in.Size());
	for (std::size_t i = 0; i < value.globals.size(); ++i)
		value.globals[i] = ReadGlobal(in);
	value.function_declarations.resize(in.Size());
	for (std::size_t i = 0; i < value.function_declarations.size(); ++i)
		value.function_declarations[i] = ReadFunctionDeclaration(in);
	value.functions.resize(in.Size());
	for (std::size_t i = 0; i < value.functions.size(); ++i)
		value.functions[i] = ReadFunction(in);
	value.object_aliases.resize(in.Size());
	for (std::size_t i = 0; i < value.object_aliases.size(); ++i)
	{
		value.object_aliases[i].object_symbol = in.String();
		value.object_aliases[i].target = in.String();
	}
	value.exported_symbols.resize(in.Size());
	for (std::size_t i = 0; i < value.exported_symbols.size(); ++i)
		value.exported_symbols[i] = ReadExport(in);
	value.source_bytes = in.Size();
	value.token_count = in.Size();
	return value;
}

std::vector<unsigned char> ReadFileBytes(const std::string& path)
{
	std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
	if (!input) throw std::runtime_error("unable to open object file: " + path);
	return std::vector<unsigned char>(std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

typedef std::unordered_map<std::string, std::string> RenameMap;

void RenameOperand(lowir_model::Operand* value, const RenameMap& names)
{
	if (value->kind != lowir_model::Operand::OP_GLOBAL) return;
	const RenameMap::const_iterator found = names.find(value->text);
	if (found != names.end()) value->text = found->second;
}

void RenameMetadata(lowir_model::SymbolMetadata* value,
	const RenameMap& names)
{
	const RenameMap::const_iterator tls = names.find(value->tls_for_symbol);
	if (tls != names.end()) value->tls_for_symbol = tls->second;
}

void RenameProgram(lowir_model::LowirProgram* program,
	const RenameMap& names)
{
	for (std::size_t i = 0; i < program->global_declarations.size(); ++i)
	{
		lowir_model::GlobalDeclaration& item = program->global_declarations[i];
		if (names.count(item.name)) item.name = names.find(item.name)->second;
		RenameMetadata(&item.metadata, names);
	}
	for (std::size_t i = 0; i < program->globals.size(); ++i)
	{
		lowir_model::GlobalDefinition& item = program->globals[i];
		if (names.count(item.name)) item.name = names.find(item.name)->second;
		RenameOperand(&item.init_operand, names);
		for (std::size_t j = 0; j < item.data_items.size(); ++j)
		{
			RenameOperand(&item.data_items[j].literal_operand, names);
			if (names.count(item.data_items[j].symbol))
				item.data_items[j].symbol = names.find(item.data_items[j].symbol)->second;
		}
		RenameMetadata(&item.metadata, names);
	}
	for (std::size_t i = 0; i < program->function_declarations.size(); ++i)
	{
		lowir_model::FunctionDeclaration& item =
			program->function_declarations[i];
		if (names.count(item.name)) item.name = names.find(item.name)->second;
		RenameMetadata(&item.metadata, names);
	}
	for (std::size_t i = 0; i < program->functions.size(); ++i)
	{
		lowir_model::Function& item = program->functions[i];
		if (names.count(item.name)) item.name = names.find(item.name)->second;
		RenameMetadata(&item.metadata, names);
		for (std::size_t j = 0; j < item.blocks.size(); ++j)
			for (std::size_t k = 0; k < item.blocks[j].instructions.size(); ++k)
			{
				lowir_model::Instruction& instruction =
					item.blocks[j].instructions[k];
				RenameOperand(&instruction.first, names);
				RenameOperand(&instruction.second, names);
				RenameOperand(&instruction.third, names);
				for (std::size_t a = 0; a < instruction.args.size(); ++a)
					RenameOperand(&instruction.args[a], names);
			}
	}
	for (std::size_t i = 0; i < program->object_aliases.size(); ++i)
		if (names.count(program->object_aliases[i].target))
			program->object_aliases[i].target =
				names.find(program->object_aliases[i].target)->second;
	for (std::size_t i = 0; i < program->exported_symbols.size(); ++i)
		if (names.count(program->exported_symbols[i].internal_symbol))
			program->exported_symbols[i].internal_symbol =
				names.find(program->exported_symbols[i].internal_symbol)->second;
}

lowir_model::Function MakeLifecycleAggregate(const std::string& name,
	lowir_model::SymbolRole role, const std::vector<std::string>& functions,
	bool reverse)
{
	lowir_model::Function result;
	result.name = name;
	result.return_type = lowir_model::builtin_lowir_type(lowir_model::LTK_VOID);
	result.metadata.role = role;
	result.metadata.binding = lowir_model::SBM_INTERNAL;
	lowir_model::Block block;
	block.label = "^entry";
	for (std::size_t i = 0; i < functions.size(); ++i)
	{
		const std::size_t index = reverse ? functions.size() - i - 1 : i;
		lowir_model::Instruction call;
		call.kind = lowir_model::Instruction::IK_CALL;
		call.call_returns_void = true;
		call.type = lowir_model::builtin_lowir_type(lowir_model::LTK_VOID);
		call.first.kind = lowir_model::Operand::OP_GLOBAL;
		call.first.text = functions[index];
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

}

LinkStats::LinkStats()
	: objects(0), symbols(0), symbol_probes(0), definitions(0),
	  coalesced_weak_definitions(0) {}

void WriteCompilerObject(const std::string& path,
	const CompilerObject& object)
{
	Writer payload;
	payload.String(object.target);
	WriteProgram(payload, object.lowir);
	std::ofstream output(path.c_str(),
		std::ios::out | std::ios::binary | std::ios::trunc);
	if (!output) throw std::runtime_error("unable to open object output: " + path);
	output.write(kMagic, sizeof(kMagic) - 1);
	const std::uint32_t version = kVersion;
	for (unsigned i = 0; i < 4; ++i) output.put(version >> (i * 8));
	const std::vector<unsigned char>& bytes = payload.Bytes();
	if (!bytes.empty()) output.write(reinterpret_cast<const char*>(&bytes[0]),
		static_cast<std::streamsize>(bytes.size()));
	if (!output) throw std::runtime_error("unable to write object output: " + path);
}

bool IsCompilerObject(const std::string& path)
{
	std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
	if (!input) return false;
	char magic[sizeof(kMagic) - 1];
	input.read(magic, sizeof(magic));
	return input.gcount() == static_cast<std::streamsize>(sizeof(magic)) &&
		std::equal(magic, magic + sizeof(magic), kMagic);
}

CompilerObject ReadCompilerObject(const std::string& path)
{
	const std::vector<unsigned char> file = ReadFileBytes(path);
	const std::size_t header = sizeof(kMagic) - 1 + 4;
	if (file.size() < header ||
		!std::equal(kMagic, kMagic + sizeof(kMagic) - 1, file.begin()))
		throw std::runtime_error("not a cppgm compiler object: " + path);
	std::uint32_t version = 0;
	for (unsigned i = 0; i < 4; ++i)
		version |= static_cast<std::uint32_t>(file[sizeof(kMagic) - 1 + i]) <<
			(i * 8);
	if (version != kVersion)
		throw std::runtime_error("unsupported cppgm object version");
	const std::vector<unsigned char> payload(file.begin() + header, file.end());
	Reader input(payload);
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
	std::unordered_map<std::string, std::string> external_names;
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
			if (symbol.linkage == ir_model::SL_INTERNAL)
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
		RenameProgram(&objects[i].lowir, names);
	}

	lowir_model::LowirProgram result;
	std::unordered_map<std::string, std::size_t> globals;
	std::unordered_map<std::string, std::size_t> functions;
	std::vector<bool> keep_globals;
	std::vector<bool> keep_functions;
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
				result.globals.push_back(item);
				keep_globals.push_back(true);
			}
			else if (IsWeak(item.metadata.binding))
			{
				if (stats) ++stats->coalesced_weak_definitions;
			}
			else if (IsWeak(result.globals[found->second].metadata.binding))
			{
				result.globals[found->second] = item;
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
				result.functions.push_back(item);
				keep_functions.push_back(true);
			}
			else if (IsWeak(item.metadata.binding))
			{
				if (stats) ++stats->coalesced_weak_definitions;
			}
			else if (IsWeak(result.functions[found->second].metadata.binding))
			{
				result.functions[found->second] = item;
				if (stats) ++stats->coalesced_weak_definitions;
			}
			else throw std::runtime_error("duplicate function definition: " + item.name);
		}
		result.object_aliases.insert(result.object_aliases.end(),
			program.object_aliases.begin(), program.object_aliases.end());
		result.exported_symbols.insert(result.exported_symbols.end(),
			program.exported_symbols.begin(), program.exported_symbols.end());
	}

	std::unordered_set<std::string> declared_globals;
	std::unordered_set<std::string> declared_functions;
	for (std::size_t unit = 0; unit < objects.size(); ++unit)
	{
		const lowir_model::LowirProgram& program = objects[unit].lowir;
		for (std::size_t i = 0; i < program.global_declarations.size(); ++i)
			if (!globals.count(program.global_declarations[i].name) &&
				declared_globals.insert(program.global_declarations[i].name).second)
				result.global_declarations.push_back(
					program.global_declarations[i]);
		for (std::size_t i = 0; i < program.function_declarations.size(); ++i)
			if (!functions.count(program.function_declarations[i].name) &&
				declared_functions.insert(program.function_declarations[i].name).second)
				result.function_declarations.push_back(
					program.function_declarations[i]);
	}
	if (!initializers.empty())
		result.functions.push_back(MakeLifecycleAggregate(
			"@__cppgm_link_init", lowir_model::SR_INIT, initializers, false));
	if (!finalizers.empty())
		result.functions.push_back(MakeLifecycleAggregate(
			"@__cppgm_link_fini", lowir_model::SR_FINI, finalizers, true));

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
	if (stats) stats->objects = objects.size();
	return result;
}

}
}
