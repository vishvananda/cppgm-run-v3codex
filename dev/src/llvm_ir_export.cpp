#include "llvm_ir_export.h"

#include "llvm_ir_model.h"
#include "pa11_model.h"
#include "pa12_semantic_model.h"
#include "pa15_lowering_abi.h"
#include "post_tokenizer.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppgm
{

using namespace pa11;
using namespace pa12_semantic_detail;

namespace
{

const char kX86_64DataLayout[] =
	"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-"
	"f80:128-n8:16:32:64-S128";
const char kX86_64Triple[] = "x86_64-pc-linux-gnu";

const char* DumpKindText(DumpKind kind)
{
	static const char* const names[] = {
		"translation-unit", "namespace", "type-alias", "variable",
		"function-declaration", "function-definition", "parameter",
		"compound-statement", "simple-declaration", "return-statement",
		"expression-statement", "statement-expression",
		"statement-expression-result", "if-statement", "switch-statement",
		"while-statement", "do-statement", "for-statement", "break-statement",
		"continue-statement", "condition", "condition-declaration",
		"for-init-statement", "iteration", "then", "else", "case-statement",
		"default-statement", "labeled-statement", "goto-statement",
		"gnu-asm-statement", "call-expression", "callee", "id-expression",
		"literal", "unary-expression", "postfix-expression",
		"binary-expression", "subscript-expression", "conditional-expression",
		"conditional-arm", "sizeof-expression", "assignment-expression",
		"cast-expression", "typeid-expression", "dynamic-cast-expression",
		"throw-expression", "try-statement", "handler", "initializer-list",
		"initializer-list-begin", "initializer-list-size", "braced-init-list",
		"aggregate-construction-action", "class-value-transfer",
		"special-member-construction-action",
		"special-member-assignment-action", "special-member-subobject-action",
		"initializer-action", "base-initializer-action",
		"vptr-initialization-action", "delegating-initializer-action",
		"member-expression", "new-expression", "delete-expression",
		"temporary-object", "constructor-action", "constructor-array-action",
		"destructor-action", "complex-construction", "complex-component"
	};
	const std::size_t index = static_cast<std::size_t>(kind);
	return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}

std::string StripFloatingSuffix(std::string spelling)
{
	spelling.erase(std::remove(spelling.begin(), spelling.end(), '\''),
		spelling.end());
	static const char* const suffixes[] = {
		"F128", "f128", "F32x", "f32x", "F64x", "f64x",
		"F16", "f16", "F32", "f32", "F64", "f64",
		"Q", "q", "L", "l", "F", "f"
	};
	for (std::size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
	{
		const std::size_t count = std::strlen(suffixes[i]);
		if (spelling.size() >= count && spelling.compare(
			spelling.size() - count, count, suffixes[i]) == 0)
		{
			spelling.erase(spelling.size() - count);
			break;
		}
	}
	return spelling;
}

std::string Hex64(std::uint64_t value)
{
	std::ostringstream output;
	output << std::uppercase << std::hex << std::setfill('0')
		<< std::setw(16) << value;
	return output.str();
}

std::string RenderFloatingConstant(const std::string& source,
	const llvm_ir::Type& type)
{
	const std::string spelling = StripFloatingSuffix(source);
	char* end = 0;
	const long double parsed = std::strtold(spelling.c_str(), &end);
	if (!end || end != spelling.c_str() + spelling.size())
		throw std::runtime_error("unsupported LLVM floating literal spelling");
	if (type.kind == llvm_ir::Type::FLOAT)
	{
		const double exact = static_cast<double>(static_cast<float>(parsed));
		std::uint64_t bits = 0;
		std::memcpy(&bits, &exact, sizeof(bits));
		return "0x" + Hex64(bits);
	}
	if (type.kind == llvm_ir::Type::DOUBLE)
	{
		const double exact = static_cast<double>(parsed);
		std::uint64_t bits = 0;
		std::memcpy(&bits, &exact, sizeof(bits));
		return "0x" + Hex64(bits);
	}
	if (type.kind == llvm_ir::Type::X86_FP80)
	{
		if (LDBL_MANT_DIG != 64 || LDBL_MAX_EXP != 16384 ||
			sizeof(long double) < 10)
			throw std::runtime_error(
				"host cannot encode x86_fp80 LLVM constants");
		const long double exact = parsed;
		unsigned char bytes[sizeof(long double)];
		std::memcpy(bytes, &exact, sizeof(bytes));
		const std::uint16_t exponent = static_cast<std::uint16_t>(
			bytes[8] | static_cast<std::uint16_t>(bytes[9]) << 8);
		std::uint64_t significand = 0;
		for (int i = 7; i >= 0; --i)
			significand = (significand << 8) | bytes[i];
		std::ostringstream output;
		output << "0xK" << std::uppercase << std::hex << std::setfill('0')
			<< std::setw(4) << exponent << std::setw(16) << significand;
		return output.str();
	}
	throw std::runtime_error(
		"LLVM half/fp128 constant export is not implemented yet");
}

struct ExpressionValue
{
	llvm_ir::Operand operand;
	TypeId source_type;

	ExpressionValue(const llvm_ir::Operand& operand_value = llvm_ir::Operand(),
		TypeId source_type_value = kNoType)
		: operand(operand_value), source_type(source_type_value) {}
};

struct Location
{
	bool present;
	bool reference;
	std::string pointer_name;
	llvm_ir::Type storage_type;
	TypeId source_type;
	std::size_t alignment;

	Location()
		: present(false), reference(false), source_type(kNoType), alignment(1) {}
};

struct FunctionFact
{
	std::uint32_t node;
	BindingId canonical;
	std::string name;
	bool definition;

	FunctionFact(std::uint32_t node_value, BindingId canonical_value,
		const std::string& name_value, bool definition_value)
		: node(node_value), canonical(canonical_value), name(name_value),
		  definition(definition_value) {}
};

struct GlobalFact
{
	std::uint32_t node;
	BindingId canonical;
	std::string name;
	bool definition;

	GlobalFact(std::uint32_t node_value, BindingId canonical_value,
		const std::string& name_value, bool definition_value)
		: node(node_value), canonical(canonical_value), name(name_value),
		  definition(definition_value) {}
};

class SemanticLlvmLowerer
{
public:
	SemanticLlvmLowerer(const SemanticGraphView& graph, llvm_ir::Module* output,
		LlvmIrExportStats* stats)
		: graph_(graph), program_(graph.program), arena_(graph.arena),
		  output_(*output), stats_(stats), function_(0), current_block_(0),
		  value_ordinal_(0), block_ordinal_(0), current_result_type_(kNoType),
		  current_function_binding_(kNoBinding)
	{
		function_names_.resize(program_.bindings.size());
		global_names_.resize(program_.bindings.size());
	}

	void Lower()
	{
		DiscoverTopLevel(graph_.root);
		EmitGlobals();
		EmitFunctions();
	}

private:
	typedef std::vector<std::uint32_t> ChildrenList;

	ChildrenList Children(std::uint32_t node) const
	{
		ChildrenList result;
		std::uint32_t edge = arena_.nodes[node].first_edge;
		while (edge != kNoDumpEdge)
		{
			if (edge >= arena_.edges.size())
				throw std::logic_error("semantic LLVM edge is out of range");
			result.push_back(arena_.edges[edge].child);
			edge = arena_.edges[edge].next;
		}
		return result;
	}

	BindingId Canonical(BindingId binding) const
	{
		if (binding == kNoBinding || binding >= program_.bindings.size())
			return binding;
		const BindingId canonical = program_.bindings[binding].canonical;
		return canonical == kNoBinding ? binding : canonical;
	}

	TypeId RemoveReference(TypeId type) const
	{
		const TypeRecord& record = program_.types.Get(type);
		return record.kind == TYPE_LVALUE_REFERENCE ||
			record.kind == TYPE_RVALUE_REFERENCE ? record.child : type;
	}

	TypeId RemoveTopCv(TypeId type) const
	{
		while (program_.types.Get(type).kind == TYPE_QUALIFIED)
			type = program_.types.Get(type).child;
		return type;
	}

	TypeId ObjectType(TypeId type) const
	{
		return RemoveTopCv(RemoveReference(type));
	}

	bool IsReference(TypeId type) const
	{
		const TypeRecord& record = program_.types.Get(type);
		return record.kind == TYPE_LVALUE_REFERENCE ||
			record.kind == TYPE_RVALUE_REFERENCE;
	}

	bool IsBoolean(TypeId type) const
	{
		type = ObjectType(type);
		const TypeRecord& record = program_.types.Get(type);
		return record.kind == TYPE_FUNDAMENTAL &&
			record.fundamental == FUND_BOOL;
	}

	bool IsNullptr(TypeId type) const
	{
		type = ObjectType(type);
		const TypeRecord& record = program_.types.Get(type);
		return record.kind == TYPE_FUNDAMENTAL &&
			record.fundamental == FUND_NULLPTR_T;
	}

	bool IsConstObject(TypeId type) const
	{
		const TypeRecord& record = program_.types.Get(type);
		if ((record.cv & CV_CONST) != 0) return true;
		if (record.kind == TYPE_QUALIFIED) return IsConstObject(record.child);
		if (record.kind == TYPE_ARRAY) return IsConstObject(record.child);
		return false;
	}

	bool IsFloating(TypeId type) const
	{
		type = ObjectType(type);
		const TypeRecord& record = program_.types.Get(type);
		if (record.kind != TYPE_FUNDAMENTAL) return false;
		switch (record.fundamental)
		{
		case FUND_FLOAT: case FUND_FLOAT16: case FUND_FLOAT32:
		case FUND_FLOAT32X: case FUND_DOUBLE: case FUND_FLOAT64:
		case FUND_FLOAT64X: case FUND_LONG_DOUBLE:
		case FUND_STDFLOAT128: case FUND_FLOAT128: return true;
		default: return false;
		}
	}

	bool IsUnsigned(TypeId type) const
	{
		type = ObjectType(type);
		const TypeRecord& record = program_.types.Get(type);
		if (record.kind == TYPE_NAMED)
		{
			const EntityRecord& entity = program_.entities[record.entity];
			if ((entity.flavor == NAMED_ENUM ||
				 entity.flavor == NAMED_ENUM_CLASS) && entity.underlying != kNoType)
				return IsUnsigned(entity.underlying);
			return false;
		}
		if (record.kind == TYPE_BITINT) return record.bitint_unsigned;
		if (record.kind == TYPE_POINTER || record.kind == TYPE_BLOCK_POINTER ||
			record.kind == TYPE_MEMBER_POINTER) return true;
		if (record.kind != TYPE_FUNDAMENTAL) return false;
		switch (record.fundamental)
		{
		case FUND_BOOL: case FUND_UNSIGNED_CHAR:
		case FUND_UNSIGNED_SHORT_INT: case FUND_UNSIGNED_INT:
		case FUND_UNSIGNED_LONG_INT: case FUND_UNSIGNED_LONG_LONG_INT:
		case FUND_CHAR16_T: case FUND_CHAR32_T: case FUND_UINT128:
			return true;
		default: return false;
		}
	}

	llvm_ir::Type ValueType(TypeId type) const
	{
		const TypeRecord& original = program_.types.Get(type);
		if (original.kind == TYPE_LVALUE_REFERENCE ||
			original.kind == TYPE_RVALUE_REFERENCE)
			return llvm_ir::Type(llvm_ir::Type::POINTER);
		type = RemoveTopCv(type);
		const TypeRecord& record = program_.types.Get(type);
		switch (record.kind)
		{
		case TYPE_POINTER: case TYPE_BLOCK_POINTER: case TYPE_ARRAY:
		case TYPE_FUNCTION: return llvm_ir::Type(llvm_ir::Type::POINTER);
		case TYPE_MEMBER_POINTER:
			return llvm_ir::Type(program_.types.IsFunction(record.child) ?
				llvm_ir::Type::I128 : llvm_ir::Type::I64);
		case TYPE_NAMED:
		{
			const EntityRecord& entity = program_.entities[record.entity];
			if ((entity.flavor == NAMED_ENUM ||
				 entity.flavor == NAMED_ENUM_CLASS) && entity.underlying != kNoType)
				return ValueType(entity.underlying);
			return llvm_ir::Type::Array(program_.SizeOf(type),
				llvm_ir::Type(llvm_ir::Type::I8));
		}
		case TYPE_VECTOR: case TYPE_COMPLEX:
			return llvm_ir::Type::Array(program_.SizeOf(type),
				llvm_ir::Type(llvm_ir::Type::I8));
		case TYPE_BITINT:
			if (record.bound <= 1) return llvm_ir::Type(llvm_ir::Type::I1);
			if (record.bound <= 8) return llvm_ir::Type(llvm_ir::Type::I8);
			if (record.bound <= 16) return llvm_ir::Type(llvm_ir::Type::I16);
			if (record.bound <= 32) return llvm_ir::Type(llvm_ir::Type::I32);
			if (record.bound <= 64) return llvm_ir::Type(llvm_ir::Type::I64);
			if (record.bound <= 128) return llvm_ir::Type(llvm_ir::Type::I128);
			throw std::runtime_error("LLVM exporter does not support wide _BitInt");
		case TYPE_FUNDAMENTAL: break;
		case TYPE_INVALID: case TYPE_QUALIFIED:
		case TYPE_LVALUE_REFERENCE: case TYPE_RVALUE_REFERENCE:
			throw std::logic_error("invalid canonical source type in LLVM exporter");
		}
		switch (record.fundamental)
		{
		case FUND_VOID: return llvm_ir::Type(llvm_ir::Type::VOID);
		case FUND_BOOL: return llvm_ir::Type(llvm_ir::Type::I1);
		case FUND_CHAR: case FUND_SIGNED_CHAR: case FUND_UNSIGNED_CHAR:
			return llvm_ir::Type(llvm_ir::Type::I8);
		case FUND_SHORT_INT: case FUND_UNSIGNED_SHORT_INT: case FUND_CHAR16_T:
			return llvm_ir::Type(llvm_ir::Type::I16);
		case FUND_INT: case FUND_UNSIGNED_INT: case FUND_WCHAR_T:
		case FUND_CHAR32_T: return llvm_ir::Type(llvm_ir::Type::I32);
		case FUND_LONG_INT: case FUND_UNSIGNED_LONG_INT:
		case FUND_LONG_LONG_INT: case FUND_UNSIGNED_LONG_LONG_INT:
			return llvm_ir::Type(llvm_ir::Type::I64);
		case FUND_NULLPTR_T: return llvm_ir::Type(llvm_ir::Type::POINTER);
		case FUND_INT128: case FUND_UINT128:
			return llvm_ir::Type(llvm_ir::Type::I128);
		case FUND_FLOAT: case FUND_FLOAT32:
			return llvm_ir::Type(llvm_ir::Type::FLOAT);
		case FUND_FLOAT16: return llvm_ir::Type(llvm_ir::Type::HALF);
		case FUND_DOUBLE: case FUND_FLOAT32X: case FUND_FLOAT64:
			return llvm_ir::Type(llvm_ir::Type::DOUBLE);
		case FUND_LONG_DOUBLE: case FUND_FLOAT64X:
			return llvm_ir::Type(llvm_ir::Type::X86_FP80);
		case FUND_STDFLOAT128: case FUND_FLOAT128:
			return llvm_ir::Type(llvm_ir::Type::FP128);
		}
		throw std::logic_error("unsupported fundamental LLVM type");
	}

	llvm_ir::Type StorageType(TypeId type) const
	{
		const TypeRecord& original = program_.types.Get(type);
		if (original.kind == TYPE_LVALUE_REFERENCE ||
			original.kind == TYPE_RVALUE_REFERENCE)
			return llvm_ir::Type(llvm_ir::Type::POINTER);
		type = RemoveTopCv(type);
		const TypeRecord& record = program_.types.Get(type);
		if (record.kind == TYPE_ARRAY)
		{
			return llvm_ir::Type::Array(
				record.IsIncompleteArray() ? 0 : record.bound,
				StorageType(record.child));
		}
		if (IsBoolean(type)) return llvm_ir::Type(llvm_ir::Type::I8);
		return ValueType(type);
	}

	std::size_t Alignment(TypeId type) const
	{
		if (IsReference(type)) return 8;
		const std::size_t result = program_.AlignOf(RemoveTopCv(type));
		return result == 0 ? 1 : result;
	}

	std::size_t IntegerWidth(const llvm_ir::Type& type) const
	{
		switch (type.kind)
		{
		case llvm_ir::Type::I1: return 1;
		case llvm_ir::Type::I8: return 8;
		case llvm_ir::Type::I16: return 16;
		case llvm_ir::Type::I32: return 32;
		case llvm_ir::Type::I64: return 64;
		case llvm_ir::Type::I128: return 128;
		default: return 0;
		}
	}

	bool IsLlvmFloating(const llvm_ir::Type& type) const
	{
		return type.kind == llvm_ir::Type::FLOAT ||
			type.kind == llvm_ir::Type::HALF ||
			type.kind == llvm_ir::Type::DOUBLE ||
			type.kind == llvm_ir::Type::X86_FP80 ||
			type.kind == llvm_ir::Type::FP128;
	}

	std::size_t FloatingRank(const llvm_ir::Type& type) const
	{
		return type.kind == llvm_ir::Type::HALF ? 1 :
			type.kind == llvm_ir::Type::FLOAT ? 2 :
			type.kind == llvm_ir::Type::DOUBLE ? 3 :
			type.kind == llvm_ir::Type::X86_FP80 ? 4 :
			type.kind == llvm_ir::Type::FP128 ? 5 : 0;
	}

	std::string FunctionName(std::uint32_t node) const
	{
		const DumpNode& record = arena_.nodes[node];
		const BindingRecord& binding = program_.bindings[record.binding];
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_NEW)
			return "_Znwm";
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY)
			return "_Znam";
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_DELETE ||
			binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY)
		{
			const TypeRecord& function = program_.types.Get(record.type);
			const bool sized = function.kind == TYPE_FUNCTION &&
				function.parameter_count > 1;
			return binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_DELETE ?
				(sized ? "_ZdlPvm" : "_ZdlPv") :
				(sized ? "_ZdaPvm" : "_ZdaPv");
		}
		const std::string mangled =
			pa15_lowering_abi::MangleFunction(program_, record);
		// The native LowIR boundary represents the process entry point with an
		// empty assembly-name override. LLVM textual IR still requires a name.
		if (!mangled.empty() && (binding.storage_class != STORAGE_CLASS_STATIC ||
			binding.language_linkage != LANGUAGE_LINKAGE_C)) return mangled;
		if (!mangled.empty()) return "__cppgm_internal." +
			std::to_string(record.binding) + "." + mangled;
		return program_.names.Get(program_.bindings[record.binding].name);
	}

	std::string GlobalName(std::uint32_t node) const
	{
		const DumpNode& record = arena_.nodes[node];
		const std::string mangled =
			pa15_lowering_abi::MangleVariable(program_, record);
		const BindingRecord& binding = program_.bindings[record.binding];
		if (binding.storage_class == STORAGE_CLASS_STATIC &&
			binding.language_linkage == LANGUAGE_LINKAGE_C)
			return "__cppgm_internal." + std::to_string(record.binding) +
				"." + mangled;
		return mangled;
	}

	void DiscoverTopLevel(std::uint32_t root)
	{
		std::vector<std::uint32_t> pending(1, root);
		std::vector<std::int32_t> function_by_binding(
			program_.bindings.size(), -1);
		std::vector<std::int32_t> global_by_binding(
			program_.bindings.size(), -1);
		while (!pending.empty())
		{
			const std::uint32_t node = pending.back();
			pending.pop_back();
			const DumpNode& record = arena_.nodes[node];
			if ((record.kind == DUMP_FUNCTION_DECLARATION ||
				record.kind == DUMP_FUNCTION_DEFINITION) &&
				record.binding != kNoBinding)
			{
				const BindingId canonical = Canonical(record.binding);
				const bool definition = record.kind == DUMP_FUNCTION_DEFINITION;
				const std::string name = FunctionName(node);
				if (canonical >= function_by_binding.size())
					throw std::logic_error("LLVM function binding is out of range");
				const std::int32_t found = function_by_binding[canonical];
				if (found < 0)
				{
					function_by_binding[canonical] =
						static_cast<std::int32_t>(functions_.size());
					functions_.push_back(FunctionFact(node, canonical, name, definition));
				}
				else if (definition && !functions_[found].definition)
					functions_[found] = FunctionFact(node, canonical, name, true);
				function_names_[canonical] = name;
				function_names_[record.binding] = name;
				continue;
			}
			if (record.kind == DUMP_VARIABLE && record.binding != kNoBinding)
			{
				const BindingId canonical = Canonical(record.binding);
				const bool definition =
					!pa15_lowering_abi::IsVariableDeclarationOnly(
						program_, record, !Children(node).empty());
				const std::string name = GlobalName(node);
				if (canonical >= global_by_binding.size())
					throw std::logic_error("LLVM global binding is out of range");
				const std::int32_t found = global_by_binding[canonical];
				if (found < 0)
				{
					global_by_binding[canonical] =
						static_cast<std::int32_t>(globals_.size());
					globals_.push_back(GlobalFact(node, canonical, name, definition));
				}
				else if (definition && !globals_[found].definition)
					globals_[found] = GlobalFact(node, canonical, name, true);
				global_names_[canonical] = name;
				global_names_[record.binding] = name;
				continue;
			}
			if (record.kind != DUMP_TRANSLATION_UNIT &&
				record.kind != DUMP_NAMESPACE)
				continue;
			const ChildrenList children = Children(node);
			for (std::size_t i = children.size(); i != 0; --i)
				pending.push_back(children[i - 1]);
		}
		// Hosted semantic declarations and some class-member declarations are
		// intentionally absent from the presentation tree. A callee node still
		// carries the canonical binding and complete function type needed for an
		// LLVM declaration, so register those direct-call boundaries here.
		for (std::uint32_t node = 0; node < arena_.nodes.size(); ++node)
		{
			const DumpNode& record = arena_.nodes[node];
			if (record.kind != DUMP_CALLEE || record.binding == kNoBinding)
				continue;
			const BindingId canonical = Canonical(record.binding);
			if (canonical >= function_by_binding.size())
				throw std::logic_error("LLVM callee binding is out of range");
			if (function_by_binding[canonical] < 0)
			{
				const std::string name = FunctionName(node);
				function_by_binding[canonical] =
					static_cast<std::int32_t>(functions_.size());
				functions_.push_back(FunctionFact(node, canonical, name, false));
				function_names_[canonical] = name;
			}
			function_names_[record.binding] = function_names_[canonical];
		}
	}

	void EmitGlobals()
	{
		for (std::size_t i = 0; i < globals_.size(); ++i)
		{
			const GlobalFact& fact = globals_[i];
			const DumpNode& node = arena_.nodes[fact.node];
			llvm_ir::Global global;
			global.name = fact.name;
			global.type = StorageType(node.type);
			global.declaration = !fact.definition;
			global.constant = IsConstObject(node.type);
			global.alignment = Alignment(node.type);
			const BindingRecord& binding = program_.bindings[fact.canonical];
			if (fact.definition && (binding.storage_class == STORAGE_CLASS_STATIC ||
				binding.unnamed_namespace_linkage))
				global.linkage = llvm_ir::Linkage::INTERNAL;
			else if (fact.definition && binding.weak_odr)
				global.linkage = llvm_ir::Linkage::WEAK_ODR;
			else if (fact.definition && binding.weak_symbol)
				global.linkage = llvm_ir::Linkage::WEAK;
			if (fact.definition)
				global.initializer = ConstantGlobalInitializer(fact, global.type);
			output_.globals.push_back(global);
			if (stats_) ++stats_->globals;
		}
	}

	llvm_ir::Operand ZeroConstant(const llvm_ir::Type& type) const
	{
		if (type.kind == llvm_ir::Type::POINTER)
			return llvm_ir::Operand::Null(type);
		return llvm_ir::Operand(llvm_ir::Operand::ZERO_INITIALIZER, type);
	}

	ExpressionValue ZeroValue(TypeId source_type) const
	{
		const llvm_ir::Type type = ValueType(source_type);
		if (type.kind == llvm_ir::Type::POINTER)
			return ExpressionValue(llvm_ir::Operand::Null(type), source_type);
		if (IsLlvmFloating(type))
			return ExpressionValue(llvm_ir::Operand::Floating(type,
				RenderFloatingConstant("0.0", type)), source_type);
		if (IntegerWidth(type) != 0)
			return ExpressionValue(llvm_ir::Operand::Integer(type, "0"), source_type);
		return ExpressionValue(ZeroConstant(type), source_type);
	}

	llvm_ir::Operand EnsureStringLiteral(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		const std::string spelling = program_.names.Get(record.text);
		const std::unordered_map<std::string, std::string>::const_iterator found =
			string_names_.find(spelling);
		if (found != string_names_.end())
			return llvm_ir::Operand::Global(
				llvm_ir::Type(llvm_ir::Type::POINTER), found->second);
		FundamentalType decoded_type = FT_CHAR;
		std::vector<std::uint32_t> units;
		if (!DecodeStringLiteralCodeUnits(spelling, &decoded_type, &units) ||
			units.empty())
			throw std::runtime_error("invalid LLVM string literal spelling");
		llvm_ir::Type element;
		std::size_t alignment = 1;
		if (decoded_type == FT_CHAR) element = llvm_ir::Type(llvm_ir::Type::I8);
		else if (decoded_type == FT_WCHAR_T || decoded_type == FT_CHAR32_T)
		{
			element = llvm_ir::Type(llvm_ir::Type::I32);
			alignment = 4;
		}
		else if (decoded_type == FT_CHAR16_T)
		{
			element = llvm_ir::Type(llvm_ir::Type::I16);
			alignment = 2;
		}
		else throw std::runtime_error("unsupported LLVM string element type");
		const llvm_ir::Type type = llvm_ir::Type::Array(units.size(), element);
		std::vector<llvm_ir::Operand> elements;
		for (std::size_t i = 0; i < units.size(); ++i)
			elements.push_back(llvm_ir::Operand::Integer(
				element, std::to_string(units[i])));
		llvm_ir::Global global;
		global.name = ".cppgm.str." +
			std::to_string(string_names_.size() + 1);
		global.type = type;
		global.initializer = llvm_ir::Operand::Aggregate(type, elements);
		global.linkage = llvm_ir::Linkage::PRIVATE;
		global.constant = true;
		global.unnamed_address = true;
		global.alignment = alignment;
		string_names_[spelling] = global.name;
		output_.globals.push_back(global);
		if (stats_) ++stats_->globals;
		return llvm_ir::Operand::Global(
			llvm_ir::Type(llvm_ir::Type::POINTER), global.name);
	}

	llvm_ir::Operand ConstantAddress(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		const ChildrenList children = Children(node);
		if (record.kind == DUMP_LITERAL &&
			program_.types.Get(ObjectType(record.type)).kind == TYPE_ARRAY)
			return EnsureStringLiteral(node);
		if ((record.kind == DUMP_UNARY_EXPRESSION &&
			record.OperationIs(OP_AMP)) || record.kind == DUMP_CAST_EXPRESSION ||
			record.kind == DUMP_CONDITIONAL_ARM)
		{
			if (children.size() != 1)
				throw std::runtime_error("constant address wrapper has invalid arity");
			return ConstantAddress(children[0]);
		}
		if (record.kind == DUMP_ID_EXPRESSION && record.binding != kNoBinding)
		{
			const BindingId canonical = Canonical(record.binding);
			if (canonical < global_names_.size() &&
				!global_names_[canonical].empty())
				return llvm_ir::Operand::Global(
					llvm_ir::Type(llvm_ir::Type::POINTER), global_names_[canonical]);
			if (canonical < function_names_.size() &&
				!function_names_[canonical].empty())
				return llvm_ir::Operand::Global(
					llvm_ir::Type(llvm_ir::Type::POINTER), function_names_[canonical]);
		}
		if (record.kind == DUMP_SUBSCRIPT_EXPRESSION && children.size() == 2)
		{
			const TypeRecord& base = program_.types.Get(
				ObjectType(arena_.nodes[children[0]].type));
			if (base.kind != TYPE_POINTER && base.kind != TYPE_ARRAY)
				throw std::runtime_error("constant subscript base is not an array");
			const DumpNode& index = arena_.nodes[children[1]];
			if (!index.constant)
				throw std::runtime_error("constant subscript has runtime index");
			std::vector<llvm_ir::Operand> indices(1,
				llvm_ir::Operand::Integer(llvm_ir::Type(llvm_ir::Type::I64),
					std::to_string(index.constant_value)));
			return llvm_ir::Operand::GetElementPtr(StorageType(base.child),
				ConstantAddress(children[0]), indices);
		}
		if (record.kind == DUMP_BINARY_EXPRESSION && children.size() == 2 &&
			(record.OperationIs(OP_PLUS) || record.OperationIs(OP_MINUS)))
		{
			const TypeRecord& left = program_.types.Get(
				ObjectType(arena_.nodes[children[0]].type));
			const bool left_pointer = left.kind == TYPE_POINTER ||
				left.kind == TYPE_ARRAY;
			const std::uint32_t base_node = left_pointer ? children[0] : children[1];
			const std::uint32_t index_node = left_pointer ? children[1] : children[0];
			const TypeRecord& base = program_.types.Get(
				ObjectType(arena_.nodes[base_node].type));
			const DumpNode& index = arena_.nodes[index_node];
			if ((base.kind != TYPE_POINTER && base.kind != TYPE_ARRAY) ||
				!index.constant)
				throw std::runtime_error("unsupported constant pointer arithmetic");
			std::int64_t offset = index.constant_value;
			if (record.OperationIs(OP_MINUS)) offset = -offset;
			std::vector<llvm_ir::Operand> indices(1,
				llvm_ir::Operand::Integer(llvm_ir::Type(llvm_ir::Type::I64),
					std::to_string(offset)));
			return llvm_ir::Operand::GetElementPtr(StorageType(base.child),
				ConstantAddress(base_node), indices);
		}
		if (record.kind == DUMP_CONDITIONAL_EXPRESSION && children.size() == 3 &&
			arena_.nodes[children[0]].constant)
			return ConstantAddress(children[
				arena_.nodes[children[0]].constant_value ? 1 : 2]);
		if (record.kind == DUMP_LITERAL &&
			((record.constant && record.constant_value == 0) ||
			 IsNullptr(record.type)))
			return llvm_ir::Operand::Null();
		throw std::runtime_error(
			"LLVM exporter does not support this constant address yet");
	}

	llvm_ir::Operand LowerConstantInitializer(std::uint32_t node,
		const llvm_ir::Type& type)
	{
		const DumpNode& record = arena_.nodes[node];
		const ChildrenList children = Children(node);
		if (record.kind == DUMP_BRACED_INIT_LIST ||
			record.kind == DUMP_INITIALIZER_LIST)
		{
			if (type.kind == llvm_ir::Type::ARRAY)
			{
				if (type.elements.size() != 1 || children.size() > type.count)
					throw std::runtime_error("invalid constant array initializer");
				std::vector<llvm_ir::Operand> elements;
				for (std::size_t i = 0; i < children.size(); ++i)
					elements.push_back(LowerConstantInitializer(
						children[i], type.elements[0]));
				while (elements.size() < type.count)
					elements.push_back(ZeroConstant(type.elements[0]));
				return llvm_ir::Operand::Aggregate(type, elements);
			}
			if (children.empty()) return ZeroConstant(type);
			if (children.size() == 1)
				return LowerConstantInitializer(children[0], type);
			throw std::runtime_error("excess scalar constant initializers");
		}
		if (type.kind == llvm_ir::Type::POINTER)
		{
			if (record.kind == DUMP_LITERAL &&
				((record.constant && record.constant_value == 0) ||
				 IsNullptr(record.type)))
				return llvm_ir::Operand::Null(type);
			return ConstantAddress(node);
		}
		if (IsLlvmFloating(type))
		{
			if (record.kind == DUMP_CAST_EXPRESSION && children.size() == 1)
				return LowerConstantInitializer(children[0], type);
			if (record.kind != DUMP_LITERAL)
				throw std::runtime_error("unsupported floating global initializer");
			const std::string spelling = record.value_initialization ? "0.0" :
				program_.names.Get(record.text);
			return llvm_ir::Operand::Floating(type,
				RenderFloatingConstant(spelling, type));
		}
		if (IntegerWidth(type) != 0 && record.constant)
			return llvm_ir::Operand::Integer(type,
				std::to_string(record.constant_value));
		if ((record.kind == DUMP_CAST_EXPRESSION ||
			record.kind == DUMP_CONDITIONAL_ARM) && children.size() == 1)
			return LowerConstantInitializer(children[0], type);
		if (record.kind == DUMP_CONDITIONAL_EXPRESSION && children.size() == 3 &&
			arena_.nodes[children[0]].constant)
			return LowerConstantInitializer(children[
				arena_.nodes[children[0]].constant_value ? 1 : 2], type);
		throw std::runtime_error(
			"LLVM exporter does not support this global initializer yet");
	}

	llvm_ir::Operand ConstantGlobalInitializer(const GlobalFact& fact,
		const llvm_ir::Type& type)
	{
		std::uint32_t initializer = kNoDumpEdge;
		for (std::size_t i = 0; i < graph_.namespace_objects.size(); ++i)
		{
			const NamespaceObjectAction& action = graph_.namespace_objects[i];
			if (Canonical(action.object) != fact.canonical ||
				action.initializer == kNoDumpEdge)
				continue;
			initializer = action.initializer;
			break;
		}
		if (initializer == kNoDumpEdge) return ZeroConstant(type);
		return LowerConstantInitializer(initializer, type);
	}

	void FillBoundary(const DumpNode& node, const ChildrenList& children,
		llvm_ir::Function* function) const
	{
		const TypeRecord& source = program_.types.Get(node.type);
		if (source.kind != TYPE_FUNCTION)
			throw std::logic_error("LLVM function node has a non-function type");
		function->result = ValueType(source.child);
		function->variadic = source.variadic;
		const TypeId* source_parameters = program_.types.Parameters(node.type);
		std::vector<const DumpNode*> parameter_nodes;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& child = arena_.nodes[children[i]];
			if (child.kind == DUMP_PARAMETER) parameter_nodes.push_back(&child);
		}
		for (std::size_t ordinal = 0; ordinal < source.parameter_count; ++ordinal)
		{
			const TypeId parameter_type = source_parameters[ordinal];
			llvm_ir::Parameter parameter;
			parameter.type = ValueType(parameter_type);
			if (ordinal < parameter_nodes.size())
				parameter.name = "arg." + std::to_string(ordinal);
			if (parameter.type.kind == llvm_ir::Type::I1)
				parameter.attributes.push_back("zeroext");
			function->parameters.push_back(parameter);
		}
		if (function->result.kind == llvm_ir::Type::I1)
			function->return_attributes.push_back("zeroext");
	}

	void EmitFunctions()
	{
		for (std::size_t i = 0; i < functions_.size(); ++i)
		{
			const FunctionFact& fact = functions_[i];
			llvm_ir::Function function;
			function.name = fact.name;
			function.declaration = !fact.definition;
			const BindingRecord& binding = program_.bindings[fact.canonical];
			if (fact.definition && (binding.storage_class == STORAGE_CLASS_STATIC ||
				binding.unnamed_namespace_linkage))
				function.linkage = llvm_ir::Linkage::INTERNAL;
			else if (fact.definition && binding.weak_odr)
				function.linkage = llvm_ir::Linkage::WEAK_ODR;
			else if (fact.definition && binding.weak_symbol)
				function.linkage = llvm_ir::Linkage::WEAK;
			const ChildrenList children = Children(fact.node);
			FillBoundary(arena_.nodes[fact.node], children, &function);
			if (fact.definition) LowerFunction(fact, children, &function);
			output_.functions.push_back(function);
			if (stats_) ++stats_->functions;
		}
	}

	void LowerFunction(const FunctionFact& fact, const ChildrenList& children,
		llvm_ir::Function* function)
	{
		function_ = function;
		current_function_binding_ = fact.canonical;
		const TypeRecord& source_function =
			program_.types.Get(arena_.nodes[fact.node].type);
		current_result_type_ = source_function.child;
		value_ordinal_ = 0;
		block_ordinal_ = 0;
		locations_.assign(program_.bindings.size(), Location());
		block_incoming_.clear();
		break_targets_.clear();
		continue_targets_.clear();
		label_blocks_.clear();
		switch_labels_.clear();
		AddBlock("entry", true);
		current_block_ = 0;
		CollectLocations(fact.node);
		EmitAllocasAndParameters(children);
		CollectFunctionLabels(fact.node);
		std::uint32_t body = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpKind kind = arena_.nodes[children[i]].kind;
			if (kind == DUMP_COMPOUND_STATEMENT || kind == DUMP_TRY_STATEMENT)
				body = children[i];
		}
		if (body != kNoDumpEdge) LowerStatement(body);
		if (!CurrentTerminated())
		{
			if (!block_incoming_[current_block_]) EmitUnreachable();
			else if (IsMain(fact.canonical))
				EmitReturn(ExpressionValue(llvm_ir::Operand::Integer(
					function_->result, "0"), current_result_type_));
			else if (function_->result.kind == llvm_ir::Type::VOID)
				EmitReturn(ExpressionValue(llvm_ir::Operand(
					llvm_ir::Operand::UNDEF, llvm_ir::Type(llvm_ir::Type::VOID)),
					current_result_type_));
			else throw std::runtime_error(
				"non-void function has no return in LLVM exporter");
		}
		if (stats_)
		{
			stats_->blocks += function_->blocks.size();
			for (std::size_t i = 0; i < function_->blocks.size(); ++i)
				stats_->instructions += function_->blocks[i].instructions.size();
		}
		function_ = 0;
		current_result_type_ = kNoType;
		current_function_binding_ = kNoBinding;
	}

	bool IsMain(BindingId binding) const
	{
		const BindingRecord& record = program_.bindings[binding];
		return record.owner == program_.GlobalScope() &&
			program_.names.Get(record.name) == "main";
	}

	void CollectLocations(std::uint32_t function_node)
	{
		std::vector<std::uint32_t> pending;
		const ChildrenList top = Children(function_node);
		for (std::size_t i = top.size(); i != 0; --i)
			pending.push_back(top[i - 1]);
		while (!pending.empty())
		{
			const std::uint32_t node = pending.back();
			pending.pop_back();
			const DumpNode& record = arena_.nodes[node];
			if ((record.kind == DUMP_PARAMETER || record.kind == DUMP_VARIABLE) &&
				record.binding != kNoBinding &&
				record.binding < locations_.size() &&
				!locations_[record.binding].present)
			{
				Location& location = locations_[record.binding];
				location.present = true;
				location.reference = IsReference(record.type);
				location.pointer_name = "slot." +
					std::to_string(record.binding);
				location.storage_type = StorageType(record.type);
				location.source_type = record.type;
				location.alignment = Alignment(record.type);
			}
			const ChildrenList children = Children(node);
			for (std::size_t i = children.size(); i != 0; --i)
				pending.push_back(children[i - 1]);
		}
	}

	void CollectFunctionLabels(std::uint32_t function_node)
	{
		std::vector<std::uint32_t> pending = Children(function_node);
		while (!pending.empty())
		{
			const std::uint32_t node = pending.back();
			pending.pop_back();
			const DumpNode& record = arena_.nodes[node];
			if (record.kind == DUMP_LABELED_STATEMENT)
			{
				if (record.text == 0 || label_blocks_.count(record.text) != 0)
					throw std::logic_error("invalid or duplicate semantic label");
				label_blocks_[record.text] = AddBlock(FreshBlock("label"));
			}
			const ChildrenList children = Children(node);
			for (std::size_t i = 0; i < children.size(); ++i)
				pending.push_back(children[i]);
		}
	}

	void EmitAllocasAndParameters(const ChildrenList& children)
	{
		for (std::size_t i = 0; i < locations_.size(); ++i)
		{
			if (!locations_[i].present) continue;
			llvm_ir::Instruction allocation(llvm_ir::Instruction::ALLOCA);
			allocation.result = locations_[i].pointer_name;
			allocation.type = locations_[i].storage_type;
			allocation.alignment = locations_[i].alignment;
			Emit(allocation);
		}
		std::size_t parameter = 0;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& child = arena_.nodes[children[i]];
			if (child.kind != DUMP_PARAMETER) continue;
			if (child.binding == kNoBinding || child.binding >= locations_.size() ||
				!locations_[child.binding].present ||
				parameter >= function_->parameters.size())
				throw std::logic_error("LLVM parameter has no storage fact");
			const llvm_ir::Parameter& source = function_->parameters[parameter++];
			StoreToLocation(locations_[child.binding],
				ExpressionValue(llvm_ir::Operand::Local(source.type, source.name),
					child.type));
		}
	}

	std::string FreshValue()
	{
		if (value_ordinal_ == std::numeric_limits<std::uint32_t>::max())
			throw std::runtime_error("too many LLVM SSA values");
		return "v." + std::to_string(++value_ordinal_);
	}

	std::string FreshBlock(const std::string& prefix)
	{
		if (block_ordinal_ == std::numeric_limits<std::uint32_t>::max())
			throw std::runtime_error("too many LLVM basic blocks");
		return prefix + "." + std::to_string(++block_ordinal_);
	}

	std::size_t AddBlock(const std::string& name, bool incoming = false)
	{
		llvm_ir::Block block;
		block.name = name;
		function_->blocks.push_back(block);
		block_incoming_.push_back(incoming);
		return function_->blocks.size() - 1;
	}

	llvm_ir::Block& CurrentBlock()
	{
		return function_->blocks[current_block_];
	}

	bool CurrentTerminated() const
	{
		const llvm_ir::Block& block = function_->blocks[current_block_];
		return !block.instructions.empty() &&
			llvm_ir::IsTerminator(block.instructions.back());
	}

	void Emit(const llvm_ir::Instruction& instruction)
	{
		if (CurrentTerminated())
			throw std::logic_error("instruction emitted after LLVM terminator");
		CurrentBlock().instructions.push_back(instruction);
	}

	llvm_ir::Operand EmitLoad(const llvm_ir::Type& type,
		const llvm_ir::Operand& address, std::size_t alignment)
	{
		llvm_ir::Instruction load(llvm_ir::Instruction::LOAD);
		load.result = FreshValue();
		load.type = type;
		load.first = address;
		load.alignment = alignment;
		Emit(load);
		return llvm_ir::Operand::Local(type, load.result);
	}

	void EmitStore(const llvm_ir::Operand& value,
		const llvm_ir::Operand& address, std::size_t alignment)
	{
		llvm_ir::Instruction store(llvm_ir::Instruction::STORE);
		store.first = value;
		store.second = address;
		store.alignment = alignment;
		Emit(store);
	}

	llvm_ir::Operand LocationAddress(const Location& location) const
	{
		return llvm_ir::Operand::Local(
			llvm_ir::Type(llvm_ir::Type::POINTER), location.pointer_name);
	}

	ExpressionValue LoadLocation(const Location& location)
	{
		if (location.reference)
		{
			const llvm_ir::Operand address = EmitLoad(
				llvm_ir::Type(llvm_ir::Type::POINTER), LocationAddress(location),
				location.alignment);
			return ExpressionValue(address, location.source_type);
		}
		llvm_ir::Operand value = EmitLoad(location.storage_type,
			LocationAddress(location), location.alignment);
		if (IsBoolean(location.source_type))
		{
			llvm_ir::Instruction cast(llvm_ir::Instruction::CAST);
			cast.result = FreshValue();
			cast.type = llvm_ir::Type(llvm_ir::Type::I1);
			cast.first = value;
			cast.cast_operation = llvm_ir::Instruction::TRUNC;
			Emit(cast);
			value = llvm_ir::Operand::Local(cast.type, cast.result);
		}
		return ExpressionValue(value, location.source_type);
	}

	void StoreToLocation(const Location& location, ExpressionValue value)
	{
		if (location.reference)
		{
			if (value.operand.type.kind != llvm_ir::Type::POINTER)
				throw std::runtime_error("reference initializer is not an address");
			EmitStore(value.operand, LocationAddress(location), location.alignment);
			return;
		}
		value = Convert(value, ValueType(location.source_type),
			location.source_type);
		if (IsBoolean(location.source_type) &&
			value.operand.type.kind == llvm_ir::Type::I1)
		{
			llvm_ir::Instruction cast(llvm_ir::Instruction::CAST);
			cast.result = FreshValue();
			cast.type = llvm_ir::Type(llvm_ir::Type::I8);
			cast.first = value.operand;
			cast.cast_operation = llvm_ir::Instruction::ZEXT;
			Emit(cast);
			value.operand = llvm_ir::Operand::Local(cast.type, cast.result);
		}
		if (value.operand.type != location.storage_type)
			throw std::runtime_error("LLVM storage/value type mismatch");
		EmitStore(value.operand, LocationAddress(location), location.alignment);
	}

	ExpressionValue Convert(ExpressionValue value,
		const llvm_ir::Type& target, TypeId target_source)
	{
		if (value.operand.type == target)
		{
			value.source_type = target_source;
			return value;
		}
		if (target.kind == llvm_ir::Type::I1 &&
			value.operand.type.kind != llvm_ir::Type::I1)
			return ConvertToCondition(value, target_source);
		if (target.kind == llvm_ir::Type::POINTER &&
			IntegerWidth(value.operand.type) != 0 &&
			value.operand.kind == llvm_ir::Operand::INTEGER &&
			value.operand.text == "0")
			return ExpressionValue(llvm_ir::Operand::Null(target), target_source);
		llvm_ir::Instruction cast(llvm_ir::Instruction::CAST);
		cast.result = FreshValue();
		cast.type = target;
		cast.first = value.operand;
		const std::size_t source_width = IntegerWidth(value.operand.type);
		const std::size_t target_width = IntegerWidth(target);
		if (source_width != 0 && target_width != 0)
			cast.cast_operation = source_width > target_width ?
				llvm_ir::Instruction::TRUNC :
				IsUnsigned(value.source_type) ? llvm_ir::Instruction::ZEXT :
				llvm_ir::Instruction::SEXT;
		else if (source_width != 0 && IsLlvmFloating(target))
			cast.cast_operation = IsUnsigned(value.source_type) ?
				llvm_ir::Instruction::UITOFP : llvm_ir::Instruction::SITOFP;
		else if (IsLlvmFloating(value.operand.type) && target_width != 0)
			cast.cast_operation = IsUnsigned(target_source) ?
				llvm_ir::Instruction::FPTOUI : llvm_ir::Instruction::FPTOSI;
		else if (IsLlvmFloating(value.operand.type) && IsLlvmFloating(target))
			cast.cast_operation = FloatingRank(value.operand.type) >
				FloatingRank(target) ? llvm_ir::Instruction::FPTRUNC :
				llvm_ir::Instruction::FPEXT;
		else if (value.operand.type.kind == llvm_ir::Type::POINTER &&
			target_width != 0)
			cast.cast_operation = llvm_ir::Instruction::PTRTOINT;
		else if (source_width != 0 && target.kind == llvm_ir::Type::POINTER)
			cast.cast_operation = llvm_ir::Instruction::INTTOPTR;
		else throw std::runtime_error("unsupported semantic LLVM conversion");
		Emit(cast);
		return ExpressionValue(llvm_ir::Operand::Local(target, cast.result),
			target_source);
	}

	ExpressionValue ConvertToCondition(ExpressionValue value,
		TypeId condition_type)
	{
		if (value.operand.type.kind == llvm_ir::Type::I1)
			return ExpressionValue(value.operand, condition_type);
		llvm_ir::Instruction compare(
			IsLlvmFloating(value.operand.type) ? llvm_ir::Instruction::FCMP :
			llvm_ir::Instruction::ICMP);
		compare.result = FreshValue();
		compare.type = value.operand.type;
		compare.first = value.operand;
		if (value.operand.type.kind == llvm_ir::Type::POINTER)
		{
			compare.second = llvm_ir::Operand::Null(value.operand.type);
			compare.predicate = llvm_ir::Instruction::NE;
		}
		else if (IsLlvmFloating(value.operand.type))
		{
			compare.second = llvm_ir::Operand::Floating(value.operand.type, "0.0");
			compare.predicate = llvm_ir::Instruction::FUNE;
		}
		else
		{
			compare.second = llvm_ir::Operand::Integer(value.operand.type, "0");
			compare.predicate = llvm_ir::Instruction::NE;
		}
		Emit(compare);
		return ExpressionValue(llvm_ir::Operand::Local(
			llvm_ir::Type(llvm_ir::Type::I1), compare.result), condition_type);
	}

	ExpressionValue LowerValue(std::uint32_t node)
	{
		if (stats_) ++stats_->semantic_nodes_lowered;
		const DumpNode& record = arena_.nodes[node];
		const ChildrenList children = Children(node);
		if (record.kind == DUMP_LITERAL &&
			program_.types.Get(ObjectType(record.type)).kind == TYPE_ARRAY)
			return ExpressionValue(EnsureStringLiteral(node), record.type);
		if (record.kind == DUMP_BRACED_INIT_LIST ||
			record.kind == DUMP_INITIALIZER_LIST)
		{
			if (children.empty()) return ZeroValue(record.type);
			if (children.size() == 1)
				return Convert(LowerValue(children[0]), ValueType(record.type),
					record.type);
			throw std::runtime_error(
				"aggregate braced value is not a scalar LLVM expression");
		}
		if (record.kind == DUMP_LITERAL || record.kind == DUMP_SIZEOF_EXPRESSION)
		{
			const llvm_ir::Type type = ValueType(record.type);
			if (type.kind == llvm_ir::Type::POINTER && record.constant &&
				record.constant_value == 0)
				return ExpressionValue(llvm_ir::Operand::Null(type), record.type);
			if (IsLlvmFloating(type))
			{
				if (record.text == 0 && !record.value_initialization)
					throw std::runtime_error("floating literal has no semantic spelling");
				const std::string spelling = record.value_initialization ? "0.0" :
					program_.names.Get(record.text);
				return ExpressionValue(llvm_ir::Operand::Floating(type,
					RenderFloatingConstant(spelling, type)),
					record.type);
			}
			if (!record.constant)
				throw std::runtime_error("literal has no semantic constant fact");
			return ExpressionValue(llvm_ir::Operand::Integer(type,
				std::to_string(record.constant_value)), record.type);
		}
		if (record.kind == DUMP_ID_EXPRESSION)
		{
			if (record.constant && record.binding != kNoBinding &&
				program_.bindings[record.binding].kind != BIND_VARIABLE)
				return ExpressionValue(llvm_ir::Operand::Integer(
					ValueType(record.type), std::to_string(record.constant_value)),
					record.type);
			if (record.binding != kNoBinding && record.binding < locations_.size() &&
				locations_[record.binding].present)
			{
				const Location& location = locations_[record.binding];
				if (program_.types.Get(ObjectType(record.type)).kind == TYPE_ARRAY)
					return ExpressionValue(LocationAddress(location), record.type);
				if (location.reference)
				{
					const ExpressionValue address = LoadLocation(location);
					const TypeKind referred_kind =
						program_.types.Get(ObjectType(record.type)).kind;
					if (referred_kind == TYPE_FUNCTION || referred_kind == TYPE_ARRAY)
						return ExpressionValue(address.operand, record.type);
					const TypeId value_type = RemoveReference(record.type);
					return ExpressionValue(EmitLoad(ValueType(value_type),
						address.operand, Alignment(value_type)), value_type);
				}
				return LoadLocation(location);
			}
			const BindingId canonical = Canonical(record.binding);
			if (canonical != kNoBinding && canonical < global_names_.size() &&
				!global_names_[canonical].empty())
			{
				const llvm_ir::Operand address = llvm_ir::Operand::Global(
					llvm_ir::Type(llvm_ir::Type::POINTER), global_names_[canonical]);
				if (program_.types.Get(ObjectType(record.type)).kind == TYPE_ARRAY)
					return ExpressionValue(address, record.type);
				return ExpressionValue(EmitLoad(ValueType(record.type), address,
					Alignment(record.type)), record.type);
			}
			if (canonical != kNoBinding && canonical < function_names_.size() &&
				!function_names_[canonical].empty())
				return ExpressionValue(llvm_ir::Operand::Global(
					llvm_ir::Type(llvm_ir::Type::POINTER),
					function_names_[canonical]), record.type);
			throw std::runtime_error("unresolved id-expression in LLVM exporter");
		}
		if (record.kind == DUMP_BINARY_EXPRESSION)
			return LowerBinary(record, children);
		if (record.kind == DUMP_CONDITIONAL_EXPRESSION)
		{
			if (record.category == VALUE_LVALUE || record.category == VALUE_XVALUE)
			{
				const llvm_ir::Operand address =
					LowerConditionalAddress(record, children);
				const TypeKind kind =
					program_.types.Get(ObjectType(record.type)).kind;
				if (kind == TYPE_ARRAY || kind == TYPE_FUNCTION)
					return ExpressionValue(address, record.type);
				return LoadObject(address, record.type);
			}
			return LowerConditional(record, children);
		}
		if (record.kind == DUMP_SUBSCRIPT_EXPRESSION)
			return ExpressionValue(EmitLoad(ValueType(record.type),
				LowerAddress(node), Alignment(record.type)), record.type);
		if (record.kind == DUMP_ASSIGNMENT_EXPRESSION)
			return LowerAssignment(record, children);
		if (record.kind == DUMP_UNARY_EXPRESSION ||
			record.kind == DUMP_POSTFIX_EXPRESSION)
			return LowerUnary(record, children);
		if (record.kind == DUMP_CAST_EXPRESSION)
		{
			if (children.size() != 1)
				throw std::runtime_error("semantic cast has invalid arity");
			if (ValueType(record.type).kind == llvm_ir::Type::VOID)
			{
				(void)LowerValue(children[0]);
				return ExpressionValue(llvm_ir::Operand(
					llvm_ir::Operand::UNDEF,
					llvm_ir::Type(llvm_ir::Type::VOID)), record.type);
			}
			return Convert(LowerValue(children[0]), ValueType(record.type),
				record.type);
		}
		if (record.kind == DUMP_CALL_EXPRESSION)
			return LowerCall(record, children);
		if (record.kind == DUMP_NEW_EXPRESSION)
			return LowerNew(record, children);
		if (record.kind == DUMP_CONDITIONAL_ARM && children.size() == 1)
			return LowerValue(children[0]);
		throw std::runtime_error(std::string("LLVM exporter does not implement ") +
			DumpKindText(record.kind) + " expression");
	}

	ExpressionValue LowerBinary(const DumpNode& record,
		const ChildrenList& children)
	{
		if (children.size() != 2)
			throw std::runtime_error("binary expression has invalid arity");
		if (record.logical_operation != LOGICAL_OPERATION_NONE)
			return LowerLogical(record, children);
		if (record.OperationIs(OP_COMMA))
		{
			(void)LowerValue(children[0]);
			if (ValueType(record.type).kind == llvm_ir::Type::VOID)
			{
				(void)LowerValue(children[1]);
				return ExpressionValue(llvm_ir::Operand(
					llvm_ir::Operand::UNDEF,
					llvm_ir::Type(llvm_ir::Type::VOID)), record.type);
			}
			return Convert(LowerValue(children[1]), ValueType(record.type),
				record.type);
		}
		const TypeId left_source = arena_.nodes[children[0]].type;
		const TypeId right_source = arena_.nodes[children[1]].type;
		const TypeRecord& left_object =
			program_.types.Get(ObjectType(left_source));
		const TypeRecord& right_object =
			program_.types.Get(ObjectType(right_source));
		if ((record.OperationIs(OP_PLUS) || record.OperationIs(OP_MINUS)) &&
			(left_object.kind == TYPE_POINTER || left_object.kind == TYPE_ARRAY ||
			 right_object.kind == TYPE_POINTER || right_object.kind == TYPE_ARRAY))
			return LowerPointerBinary(record, children);
		const TypeId operand_source = record.operand_type == kNoType ?
			arena_.nodes[children[0]].type : record.operand_type;
		const llvm_ir::Type operand_type = ValueType(operand_source);
		ExpressionValue left = Convert(LowerValue(children[0]), operand_type,
			operand_source);
		ExpressionValue right = Convert(LowerValue(children[1]), operand_type,
			operand_source);
		const bool comparison = record.OperationIs(OP_EQ) ||
			record.OperationIs(OP_NE) || record.OperationIs(OP_LT) ||
			record.OperationIs(OP_LE) || record.OperationIs(OP_GT) ||
			record.OperationIs(OP_GE);
		llvm_ir::Instruction instruction(comparison ?
			(IsLlvmFloating(operand_type) ? llvm_ir::Instruction::FCMP :
			 llvm_ir::Instruction::ICMP) : llvm_ir::Instruction::BINARY);
		instruction.result = FreshValue();
		instruction.type = operand_type;
		instruction.first = left.operand;
		instruction.second = right.operand;
		if (comparison)
		{
			if (IsLlvmFloating(operand_type))
				instruction.predicate = record.OperationIs(OP_EQ) ?
					llvm_ir::Instruction::FOEQ : record.OperationIs(OP_NE) ?
					llvm_ir::Instruction::FUNE : record.OperationIs(OP_LT) ?
					llvm_ir::Instruction::FOLT : record.OperationIs(OP_LE) ?
					llvm_ir::Instruction::FOLE : record.OperationIs(OP_GT) ?
					llvm_ir::Instruction::FOGT : llvm_ir::Instruction::FOGE;
			else if (record.OperationIs(OP_EQ))
				instruction.predicate = llvm_ir::Instruction::EQ;
			else if (record.OperationIs(OP_NE))
				instruction.predicate = llvm_ir::Instruction::NE;
			else if (IsUnsigned(operand_source) ||
				operand_type.kind == llvm_ir::Type::POINTER)
				instruction.predicate = record.OperationIs(OP_LT) ?
					llvm_ir::Instruction::ULT : record.OperationIs(OP_LE) ?
					llvm_ir::Instruction::ULE : record.OperationIs(OP_GT) ?
					llvm_ir::Instruction::UGT : llvm_ir::Instruction::UGE;
			else instruction.predicate = record.OperationIs(OP_LT) ?
				llvm_ir::Instruction::SLT : record.OperationIs(OP_LE) ?
				llvm_ir::Instruction::SLE : record.OperationIs(OP_GT) ?
				llvm_ir::Instruction::SGT : llvm_ir::Instruction::SGE;
		}
		else if (IsLlvmFloating(operand_type))
			instruction.binary_operation = record.OperationIs(OP_PLUS) ?
				llvm_ir::Instruction::FADD : record.OperationIs(OP_MINUS) ?
				llvm_ir::Instruction::FSUB : record.OperationIs(OP_STAR) ?
				llvm_ir::Instruction::FMUL : record.OperationIs(OP_DIV) ?
				llvm_ir::Instruction::FDIV : record.OperationIs(OP_MOD) ?
				llvm_ir::Instruction::FREM : llvm_ir::Instruction::BINARY_NONE;
		else instruction.binary_operation = record.OperationIs(OP_PLUS) ?
			llvm_ir::Instruction::ADD : record.OperationIs(OP_MINUS) ?
			llvm_ir::Instruction::SUB : record.OperationIs(OP_STAR) ?
			llvm_ir::Instruction::MUL : record.OperationIs(OP_DIV) ?
			(IsUnsigned(operand_source) ? llvm_ir::Instruction::UDIV :
			 llvm_ir::Instruction::SDIV) : record.OperationIs(OP_MOD) ?
			(IsUnsigned(operand_source) ? llvm_ir::Instruction::UREM :
			 llvm_ir::Instruction::SREM) : record.OperationIs(OP_AMP) ?
			llvm_ir::Instruction::AND : record.OperationIs(OP_BOR) ?
			llvm_ir::Instruction::OR : record.OperationIs(OP_XOR) ?
			llvm_ir::Instruction::XOR : record.OperationIs(OP_LSHIFT) ?
			llvm_ir::Instruction::SHL : record.OperationIs(OP_RSHIFT) ?
			(IsUnsigned(operand_source) ? llvm_ir::Instruction::LSHR :
			 llvm_ir::Instruction::ASHR) : llvm_ir::Instruction::BINARY_NONE;
		if (!comparison && instruction.binary_operation ==
			llvm_ir::Instruction::BINARY_NONE)
			throw std::runtime_error("unsupported binary operator in LLVM exporter");
		Emit(instruction);
		const llvm_ir::Type result = comparison ?
			llvm_ir::Type(llvm_ir::Type::I1) : ValueType(record.type);
		return ExpressionValue(llvm_ir::Operand::Local(result,
			instruction.result), record.type);
	}

	ExpressionValue LowerLogical(const DumpNode& record,
		const ChildrenList& children)
	{
		const bool conjunction =
			record.logical_operation == LOGICAL_OPERATION_AND;
		ExpressionValue left = ConvertToCondition(LowerValue(children[0]),
			arena_.nodes[children[0]].type);
		const std::string left_predecessor = CurrentBlock().name;
		const std::size_t rhs_block = AddBlock(FreshBlock("logic.rhs"));
		const std::size_t end_block = AddBlock(FreshBlock("logic.end"));
		if (conjunction)
			EmitConditionalBranch(left.operand, rhs_block, end_block);
		else EmitConditionalBranch(left.operand, end_block, rhs_block);
		current_block_ = rhs_block;
		ExpressionValue right = ConvertToCondition(LowerValue(children[1]),
			arena_.nodes[children[1]].type);
		const std::string right_predecessor = CurrentBlock().name;
		EmitBranch(end_block);
		current_block_ = end_block;
		llvm_ir::Instruction phi(llvm_ir::Instruction::PHI);
		phi.result = FreshValue();
		phi.type = llvm_ir::Type(llvm_ir::Type::I1);
		phi.operands.push_back(llvm_ir::Operand::Integer(phi.type,
			conjunction ? "0" : "1"));
		phi.labels.push_back(left_predecessor);
		phi.operands.push_back(right.operand);
		phi.labels.push_back(right_predecessor);
		Emit(phi);
		return ExpressionValue(llvm_ir::Operand::Local(phi.type, phi.result),
			record.type);
	}

	ExpressionValue LowerPointerBinary(const DumpNode& record,
		const ChildrenList& children)
	{
		const TypeId left_source = arena_.nodes[children[0]].type;
		const TypeId right_source = arena_.nodes[children[1]].type;
		const TypeRecord& left = program_.types.Get(ObjectType(left_source));
		const TypeRecord& right = program_.types.Get(ObjectType(right_source));
		const bool left_pointer = left.kind == TYPE_POINTER ||
			left.kind == TYPE_ARRAY;
		const bool right_pointer = right.kind == TYPE_POINTER ||
			right.kind == TYPE_ARRAY;
		if (left_pointer && right_pointer)
		{
			if (!record.OperationIs(OP_MINUS))
				throw std::runtime_error("unsupported operation on two pointers");
			ExpressionValue left_value = LowerValue(children[0]);
			ExpressionValue right_value = LowerValue(children[1]);
			llvm_ir::Instruction left_integer(llvm_ir::Instruction::CAST);
			left_integer.result = FreshValue();
			left_integer.type = llvm_ir::Type(llvm_ir::Type::I64);
			left_integer.first = left_value.operand;
			left_integer.cast_operation = llvm_ir::Instruction::PTRTOINT;
			Emit(left_integer);
			llvm_ir::Instruction right_integer(llvm_ir::Instruction::CAST);
			right_integer.result = FreshValue();
			right_integer.type = llvm_ir::Type(llvm_ir::Type::I64);
			right_integer.first = right_value.operand;
			right_integer.cast_operation = llvm_ir::Instruction::PTRTOINT;
			Emit(right_integer);
			llvm_ir::Instruction difference(llvm_ir::Instruction::BINARY);
			difference.result = FreshValue();
			difference.type = llvm_ir::Type(llvm_ir::Type::I64);
			difference.first = llvm_ir::Operand::Local(
				difference.type, left_integer.result);
			difference.second = llvm_ir::Operand::Local(
				difference.type, right_integer.result);
			difference.binary_operation = llvm_ir::Instruction::SUB;
			Emit(difference);
			ExpressionValue result(llvm_ir::Operand::Local(
				difference.type, difference.result), record.type);
			const std::size_t element_size = program_.SizeOf(left.child);
			if (element_size > 1)
			{
				llvm_ir::Instruction scale(llvm_ir::Instruction::BINARY);
				scale.result = FreshValue();
				scale.type = difference.type;
				scale.first = result.operand;
				scale.second = llvm_ir::Operand::Integer(scale.type,
					std::to_string(element_size));
				scale.binary_operation = llvm_ir::Instruction::SDIV;
				Emit(scale);
				result.operand = llvm_ir::Operand::Local(scale.type, scale.result);
			}
			return Convert(result, ValueType(record.type), record.type);
		}
		if (record.OperationIs(OP_MINUS) && !left_pointer)
			throw std::runtime_error("integer minus pointer is invalid");
		const std::uint32_t pointer_node = left_pointer ? children[0] : children[1];
		const std::uint32_t index_node = left_pointer ? children[1] : children[0];
		const TypeRecord& pointer = program_.types.Get(ObjectType(
			arena_.nodes[pointer_node].type));
		const TypeId element = pointer.child;
		ExpressionValue base = LowerValue(pointer_node);
		ExpressionValue index = Convert(LowerValue(index_node),
			llvm_ir::Type(llvm_ir::Type::I64),
			arena_.nodes[index_node].type);
		if (record.OperationIs(OP_MINUS))
		{
			llvm_ir::Instruction negate(llvm_ir::Instruction::BINARY);
			negate.result = FreshValue();
			negate.type = index.operand.type;
			negate.first = llvm_ir::Operand::Integer(negate.type, "0");
			negate.second = index.operand;
			negate.binary_operation = llvm_ir::Instruction::SUB;
			Emit(negate);
			index.operand = llvm_ir::Operand::Local(negate.type, negate.result);
		}
		llvm_ir::Instruction gep(llvm_ir::Instruction::GETELEMENTPTR);
		gep.result = FreshValue();
		gep.source_type = StorageType(element);
		gep.first = base.operand;
		gep.operands.push_back(index.operand);
		Emit(gep);
		return ExpressionValue(llvm_ir::Operand::Local(
			llvm_ir::Type(llvm_ir::Type::POINTER), gep.result), record.type);
	}

	ExpressionValue LowerConditional(const DumpNode& record,
		const ChildrenList& children)
	{
		if (children.size() != 3 || record.category != VALUE_PRVALUE)
			throw std::runtime_error(
				"only prvalue conditional expressions are implemented");
		const ExpressionValue condition = ConvertToCondition(
			LowerValue(children[0]), arena_.nodes[children[0]].type);
		const std::size_t yes_block = AddBlock(FreshBlock("cond.true"));
		const std::size_t no_block = AddBlock(FreshBlock("cond.false"));
		const std::size_t end_block = AddBlock(FreshBlock("cond.end"));
		EmitConditionalBranch(condition.operand, yes_block, no_block);
		const llvm_ir::Type result_type = ValueType(record.type);
		current_block_ = yes_block;
		ExpressionValue yes = Convert(LowerValue(children[1]), result_type,
			record.type);
		const std::string yes_predecessor = CurrentBlock().name;
		EmitBranch(end_block);
		current_block_ = no_block;
		ExpressionValue no = Convert(LowerValue(children[2]), result_type,
			record.type);
		const std::string no_predecessor = CurrentBlock().name;
		EmitBranch(end_block);
		current_block_ = end_block;
		if (result_type.kind == llvm_ir::Type::VOID)
			return ExpressionValue(llvm_ir::Operand(
				llvm_ir::Operand::UNDEF, result_type), record.type);
		llvm_ir::Instruction phi(llvm_ir::Instruction::PHI);
		phi.result = FreshValue();
		phi.type = result_type;
		phi.operands.push_back(yes.operand);
		phi.labels.push_back(yes_predecessor);
		phi.operands.push_back(no.operand);
		phi.labels.push_back(no_predecessor);
		Emit(phi);
		return ExpressionValue(llvm_ir::Operand::Local(phi.type, phi.result),
			record.type);
	}

	llvm_ir::Operand LowerAddress(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		const ChildrenList children = Children(node);
		if (record.kind == DUMP_LITERAL &&
			program_.types.Get(ObjectType(record.type)).kind == TYPE_ARRAY)
			return EnsureStringLiteral(node);
		if (record.kind == DUMP_ID_EXPRESSION && record.binding != kNoBinding)
		{
			if (record.binding < locations_.size() &&
				locations_[record.binding].present)
			{
				const Location& location = locations_[record.binding];
				if (!location.reference) return LocationAddress(location);
				return EmitLoad(llvm_ir::Type(llvm_ir::Type::POINTER),
					LocationAddress(location), location.alignment);
			}
			const BindingId canonical = Canonical(record.binding);
			if (canonical < global_names_.size() && !global_names_[canonical].empty())
				return llvm_ir::Operand::Global(
					llvm_ir::Type(llvm_ir::Type::POINTER), global_names_[canonical]);
		}
		if (record.kind == DUMP_UNARY_EXPRESSION && record.OperationIs(OP_STAR) &&
			children.size() == 1)
			return LowerValue(children[0]).operand;
		if (record.kind == DUMP_UNARY_EXPRESSION &&
			(record.OperationIs(OP_INC) || record.OperationIs(OP_DEC)) &&
			children.size() == 1)
		{
			(void)LowerIncrement(record, children);
			return LowerAddress(children[0]);
		}
		if (record.kind == DUMP_ASSIGNMENT_EXPRESSION && children.size() == 2)
		{
			(void)LowerAssignment(record, children);
			return LowerAddress(children[0]);
		}
		if (record.kind == DUMP_BINARY_EXPRESSION &&
			record.OperationIs(OP_COMMA) && children.size() == 2)
		{
			(void)LowerValue(children[0]);
			return LowerAddress(children[1]);
		}
		if (record.kind == DUMP_CONDITIONAL_EXPRESSION)
			return LowerConditionalAddress(record, children);
		if (record.kind == DUMP_CALL_EXPRESSION && IsReference(record.type))
			return LowerCall(record, children).operand;
		if (record.kind == DUMP_CAST_EXPRESSION && children.size() == 1 &&
			(record.category == VALUE_LVALUE || record.category == VALUE_XVALUE))
		{
			const DumpNode& child = arena_.nodes[children[0]];
			return child.category == VALUE_LVALUE || child.category == VALUE_XVALUE ?
				LowerAddress(children[0]) : LowerValue(children[0]).operand;
		}
		if (record.kind == DUMP_ID_EXPRESSION && record.binding != kNoBinding)
		{
			const BindingId canonical = Canonical(record.binding);
			if (canonical < function_names_.size() &&
				!function_names_[canonical].empty())
				return llvm_ir::Operand::Global(
					llvm_ir::Type(llvm_ir::Type::POINTER),
					function_names_[canonical]);
		}
		if (record.kind == DUMP_SUBSCRIPT_EXPRESSION && children.size() == 2)
		{
			const TypeId base_type = arena_.nodes[children[0]].type;
			const TypeRecord& base = program_.types.Get(ObjectType(base_type));
			if (base.kind != TYPE_POINTER && base.kind != TYPE_ARRAY)
				throw std::runtime_error("LLVM subscript base is not a pointer");
			ExpressionValue pointer = LowerValue(children[0]);
			ExpressionValue index = Convert(LowerValue(children[1]),
				llvm_ir::Type(llvm_ir::Type::I64),
				arena_.nodes[children[1]].type);
			llvm_ir::Instruction gep(llvm_ir::Instruction::GETELEMENTPTR);
			gep.result = FreshValue();
			gep.source_type = StorageType(base.child);
			gep.first = pointer.operand;
			gep.operands.push_back(index.operand);
			Emit(gep);
			return llvm_ir::Operand::Local(
				llvm_ir::Type(llvm_ir::Type::POINTER), gep.result);
		}
		throw std::runtime_error("unsupported lvalue in LLVM exporter");
	}

	llvm_ir::Operand LowerConditionalAddress(const DumpNode& record,
		const ChildrenList& children)
	{
		if (children.size() != 3 ||
			(record.category != VALUE_LVALUE && record.category != VALUE_XVALUE))
			throw std::runtime_error("conditional expression is not an lvalue");
		const ExpressionValue condition = ConvertToCondition(
			LowerValue(children[0]), arena_.nodes[children[0]].type);
		const std::size_t yes_block = AddBlock(FreshBlock("lcond.true"));
		const std::size_t no_block = AddBlock(FreshBlock("lcond.false"));
		const std::size_t end_block = AddBlock(FreshBlock("lcond.end"));
		EmitConditionalBranch(condition.operand, yes_block, no_block);
		current_block_ = yes_block;
		const llvm_ir::Operand yes = LowerAddress(children[1]);
		const std::string yes_predecessor = CurrentBlock().name;
		EmitBranch(end_block);
		current_block_ = no_block;
		const llvm_ir::Operand no = LowerAddress(children[2]);
		const std::string no_predecessor = CurrentBlock().name;
		EmitBranch(end_block);
		current_block_ = end_block;
		llvm_ir::Instruction phi(llvm_ir::Instruction::PHI);
		phi.result = FreshValue();
		phi.type = llvm_ir::Type(llvm_ir::Type::POINTER);
		phi.operands.push_back(yes);
		phi.labels.push_back(yes_predecessor);
		phi.operands.push_back(no);
		phi.labels.push_back(no_predecessor);
		Emit(phi);
		return llvm_ir::Operand::Local(phi.type, phi.result);
	}

	ExpressionValue LoadObject(const llvm_ir::Operand& address, TypeId type)
	{
		const llvm_ir::Type storage_type = StorageType(type);
		llvm_ir::Operand value = EmitLoad(storage_type, address, Alignment(type));
		if (IsBoolean(type))
		{
			llvm_ir::Instruction cast(llvm_ir::Instruction::CAST);
			cast.result = FreshValue();
			cast.type = llvm_ir::Type(llvm_ir::Type::I1);
			cast.first = value;
			cast.cast_operation = llvm_ir::Instruction::TRUNC;
			Emit(cast);
			value = llvm_ir::Operand::Local(cast.type, cast.result);
		}
		return ExpressionValue(value, type);
	}

	ExpressionValue StoreObject(const llvm_ir::Operand& address, TypeId type,
		ExpressionValue value)
	{
		value = Convert(value, ValueType(type), type);
		llvm_ir::Operand stored = value.operand;
		if (IsBoolean(type) && stored.type.kind == llvm_ir::Type::I1)
		{
			llvm_ir::Instruction cast(llvm_ir::Instruction::CAST);
			cast.result = FreshValue();
			cast.type = llvm_ir::Type(llvm_ir::Type::I8);
			cast.first = stored;
			cast.cast_operation = llvm_ir::Instruction::ZEXT;
			Emit(cast);
			stored = llvm_ir::Operand::Local(cast.type, cast.result);
		}
		if (stored.type != StorageType(type))
			throw std::runtime_error("LLVM object store type mismatch");
		EmitStore(stored, address, Alignment(type));
		return value;
	}

	llvm_ir::Instruction::BinaryOperation CompoundOperation(
		const DumpNode& record, TypeId operation_type) const
	{
		const bool floating = IsFloating(operation_type);
		if (record.OperationIs(OP_PLUSASS))
			return floating ? llvm_ir::Instruction::FADD : llvm_ir::Instruction::ADD;
		if (record.OperationIs(OP_MINUSASS))
			return floating ? llvm_ir::Instruction::FSUB : llvm_ir::Instruction::SUB;
		if (record.OperationIs(OP_STARASS))
			return floating ? llvm_ir::Instruction::FMUL : llvm_ir::Instruction::MUL;
		if (record.OperationIs(OP_DIVASS))
			return floating ? llvm_ir::Instruction::FDIV : IsUnsigned(operation_type) ?
				llvm_ir::Instruction::UDIV : llvm_ir::Instruction::SDIV;
		if (record.OperationIs(OP_MODASS))
			return floating ? llvm_ir::Instruction::FREM : IsUnsigned(operation_type) ?
				llvm_ir::Instruction::UREM : llvm_ir::Instruction::SREM;
		if (record.OperationIs(OP_BANDASS)) return llvm_ir::Instruction::AND;
		if (record.OperationIs(OP_BORASS)) return llvm_ir::Instruction::OR;
		if (record.OperationIs(OP_XORASS)) return llvm_ir::Instruction::XOR;
		if (record.OperationIs(OP_LSHIFTASS)) return llvm_ir::Instruction::SHL;
		if (record.OperationIs(OP_RSHIFTASS))
			return IsUnsigned(operation_type) ? llvm_ir::Instruction::LSHR :
				llvm_ir::Instruction::ASHR;
		return llvm_ir::Instruction::BINARY_NONE;
	}

	ExpressionValue LowerAssignment(const DumpNode& record,
		const ChildrenList& children)
	{
		if (children.size() != 2)
			throw std::runtime_error("assignment expression has invalid arity");
		const llvm_ir::Operand address = LowerAddress(children[0]);
		const TypeId object_type = arena_.nodes[children[0]].type;
		if (record.OperationIs(OP_ASS))
			return StoreObject(address, object_type, LowerValue(children[1]));
		const TypeId operation_source = record.operand_type == kNoType ?
			object_type : record.operand_type;
		const llvm_ir::Type operation_type = ValueType(operation_source);
		if (operation_type.kind == llvm_ir::Type::POINTER)
		{
			if (!record.OperationIs(OP_PLUSASS) &&
				!record.OperationIs(OP_MINUSASS))
				throw std::runtime_error("unsupported pointer compound assignment");
			const TypeRecord& pointer = program_.types.Get(ObjectType(object_type));
			if (pointer.kind != TYPE_POINTER)
				throw std::runtime_error("pointer compound target has wrong type");
			ExpressionValue base = LoadObject(address, object_type);
			ExpressionValue index = Convert(LowerValue(children[1]),
				llvm_ir::Type(llvm_ir::Type::I64),
				arena_.nodes[children[1]].type);
			if (record.OperationIs(OP_MINUSASS))
			{
				llvm_ir::Instruction negate(llvm_ir::Instruction::BINARY);
				negate.result = FreshValue();
				negate.type = index.operand.type;
				negate.first = llvm_ir::Operand::Integer(negate.type, "0");
				negate.second = index.operand;
				negate.binary_operation = llvm_ir::Instruction::SUB;
				Emit(negate);
				index.operand = llvm_ir::Operand::Local(negate.type, negate.result);
			}
			llvm_ir::Instruction gep(llvm_ir::Instruction::GETELEMENTPTR);
			gep.result = FreshValue();
			gep.source_type = StorageType(pointer.child);
			gep.first = base.operand;
			gep.operands.push_back(index.operand);
			Emit(gep);
			return StoreObject(address, object_type, ExpressionValue(
				llvm_ir::Operand::Local(llvm_ir::Type(llvm_ir::Type::POINTER),
					gep.result), object_type));
		}
		ExpressionValue left = Convert(LoadObject(address, object_type),
			operation_type, operation_source);
		ExpressionValue right = Convert(LowerValue(children[1]),
			operation_type, operation_source);
		llvm_ir::Instruction operation(llvm_ir::Instruction::BINARY);
		operation.result = FreshValue();
		operation.type = operation_type;
		operation.first = left.operand;
		operation.second = right.operand;
		operation.binary_operation = CompoundOperation(record, operation_source);
		if (operation.binary_operation == llvm_ir::Instruction::BINARY_NONE)
			throw std::runtime_error(
				"unsupported compound assignment in LLVM exporter");
		Emit(operation);
		return StoreObject(address, object_type, ExpressionValue(
			llvm_ir::Operand::Local(operation.type, operation.result),
			operation_source));
	}

	ExpressionValue LowerIncrement(const DumpNode& record,
		const ChildrenList& children)
	{
		const std::uint32_t operand_node = children[0];
		const TypeId object_type = arena_.nodes[operand_node].type;
		const llvm_ir::Operand address = LowerAddress(operand_node);
		const ExpressionValue previous = LoadObject(address, object_type);
		ExpressionValue next;
		if (previous.operand.type.kind == llvm_ir::Type::POINTER)
		{
			const TypeRecord& pointer = program_.types.Get(ObjectType(object_type));
			if (pointer.kind != TYPE_POINTER)
				throw std::runtime_error("unsupported pointer increment type");
			llvm_ir::Instruction gep(llvm_ir::Instruction::GETELEMENTPTR);
			gep.result = FreshValue();
			gep.source_type = StorageType(pointer.child);
			gep.first = previous.operand;
			gep.operands.push_back(llvm_ir::Operand::Integer(
				llvm_ir::Type(llvm_ir::Type::I64),
				record.OperationIs(OP_DEC) ? "-1" : "1"));
			Emit(gep);
			next = ExpressionValue(llvm_ir::Operand::Local(
				llvm_ir::Type(llvm_ir::Type::POINTER), gep.result), object_type);
		}
		else
		{
			llvm_ir::Instruction operation(llvm_ir::Instruction::BINARY);
			operation.result = FreshValue();
			operation.type = previous.operand.type;
			operation.first = previous.operand;
			operation.second = IsLlvmFloating(operation.type) ?
				llvm_ir::Operand::Floating(operation.type,
					RenderFloatingConstant("1.0", operation.type)) :
				llvm_ir::Operand::Integer(operation.type, "1");
			operation.binary_operation = record.OperationIs(OP_DEC) ?
				(IsLlvmFloating(operation.type) ? llvm_ir::Instruction::FSUB :
				 llvm_ir::Instruction::SUB) :
				(IsLlvmFloating(operation.type) ? llvm_ir::Instruction::FADD :
				 llvm_ir::Instruction::ADD);
			Emit(operation);
			next = ExpressionValue(llvm_ir::Operand::Local(operation.type,
				operation.result), object_type);
		}
		next = StoreObject(address, object_type, next);
		return record.kind == DUMP_POSTFIX_EXPRESSION ? previous : next;
	}

	ExpressionValue LowerUnary(const DumpNode& record,
		const ChildrenList& children)
	{
		if (children.size() != 1)
			throw std::runtime_error("unary expression has invalid arity");
		if (record.OperationIs(OP_INC) || record.OperationIs(OP_DEC))
			return LowerIncrement(record, children);
		if (record.OperationIs(OP_AMP))
			return ExpressionValue(LowerAddress(children[0]), record.type);
		if (record.OperationIs(OP_STAR))
		{
			const TypeId result_type = RemoveReference(record.type);
			return ExpressionValue(EmitLoad(ValueType(result_type),
				LowerValue(children[0]).operand, Alignment(result_type)), result_type);
		}
		ExpressionValue value = LowerValue(children[0]);
		if (record.OperationIs(OP_PLUS)) return value;
		if (record.OperationIs(OP_LNOT))
		{
			value = ConvertToCondition(value, record.type);
			llvm_ir::Instruction invert(llvm_ir::Instruction::BINARY);
			invert.result = FreshValue();
			invert.type = llvm_ir::Type(llvm_ir::Type::I1);
			invert.first = value.operand;
			invert.second = llvm_ir::Operand::Integer(invert.type, "true");
			invert.binary_operation = llvm_ir::Instruction::XOR;
			Emit(invert);
			return ExpressionValue(llvm_ir::Operand::Local(invert.type,
				invert.result), record.type);
		}
		llvm_ir::Instruction operation(llvm_ir::Instruction::BINARY);
		operation.result = FreshValue();
		operation.type = value.operand.type;
		operation.first = record.OperationIs(OP_MINUS) ?
			(IsLlvmFloating(value.operand.type) ?
			 llvm_ir::Operand::Floating(value.operand.type, "0.0") :
			 llvm_ir::Operand::Integer(value.operand.type, "0")) : value.operand;
		operation.second = record.OperationIs(OP_MINUS) ? value.operand :
			llvm_ir::Operand::Integer(value.operand.type, "-1");
		operation.binary_operation = record.OperationIs(OP_MINUS) ?
			(IsLlvmFloating(value.operand.type) ? llvm_ir::Instruction::FSUB :
			 llvm_ir::Instruction::SUB) : record.OperationIs(OP_COMPL) ?
			llvm_ir::Instruction::XOR : llvm_ir::Instruction::BINARY_NONE;
		if (operation.binary_operation == llvm_ir::Instruction::BINARY_NONE)
			throw std::runtime_error("unsupported unary operator in LLVM exporter");
		Emit(operation);
		return ExpressionValue(llvm_ir::Operand::Local(operation.type,
			operation.result), record.type);
	}

	ExpressionValue LowerCall(const DumpNode& record,
		const ChildrenList& children)
	{
		if (children.empty())
			throw std::runtime_error("semantic call has no callee");
		const DumpNode& callee = arena_.nodes[children[0]];
		TypeId function_type_id = RemoveTopCv(RemoveReference(callee.type));
		const TypeRecord* function_type = &program_.types.Get(function_type_id);
		if (function_type->kind == TYPE_POINTER ||
			function_type->kind == TYPE_BLOCK_POINTER)
		{
			function_type_id = function_type->child;
			function_type = &program_.types.Get(function_type_id);
		}
		if (function_type->kind != TYPE_FUNCTION)
			throw std::logic_error("direct LLVM call has no function type");
		const TypeId* parameters = program_.types.Parameters(function_type_id);
		llvm_ir::Instruction call(llvm_ir::Instruction::CALL);
		call.type = ValueType(function_type->child);
		if (call.type.kind == llvm_ir::Type::I1)
			call.return_attributes.push_back("zeroext");
		if (callee.kind == DUMP_CALLEE && callee.binding != kNoBinding)
		{
			const BindingId canonical = Canonical(callee.binding);
			if (canonical >= function_names_.size() ||
				function_names_[canonical].empty())
				throw std::runtime_error("direct LLVM call has no function symbol");
			call.callee = function_names_[canonical];
		}
		else
		{
			call.indirect_call = true;
			call.first = Convert(LowerValue(children[0]),
				llvm_ir::Type(llvm_ir::Type::POINTER), callee.type).operand;
		}
		for (std::size_t i = 1; i < children.size(); ++i)
		{
			TypeId target = i - 1 < function_type->parameter_count ?
				parameters[i - 1] : arena_.nodes[children[i]].type;
			ExpressionValue argument;
			if (IsReference(target))
			{
				const DumpNode& source = arena_.nodes[children[i]];
				const llvm_ir::Operand address =
					source.category == VALUE_LVALUE || source.category == VALUE_XVALUE ?
					LowerAddress(children[i]) :
					MaterializeTemporary(children[i], RemoveReference(target));
				argument = ExpressionValue(address, target);
			}
			else
			{
				llvm_ir::Type target_type = ValueType(target);
				if (i - 1 >= function_type->parameter_count)
				{
					if (target_type.kind == llvm_ir::Type::FLOAT)
						target_type = llvm_ir::Type(llvm_ir::Type::DOUBLE);
					else if (IntegerWidth(target_type) != 0 &&
						IntegerWidth(target_type) < 32)
						target_type = llvm_ir::Type(llvm_ir::Type::I32);
				}
				argument = Convert(LowerValue(children[i]), target_type, target);
			}
			call.operands.push_back(argument.operand);
			call.argument_attributes.push_back(std::vector<std::string>());
			if (i - 1 < function_type->parameter_count &&
				argument.operand.type.kind == llvm_ir::Type::I1)
				call.argument_attributes.back().push_back("zeroext");
		}
		if (call.type.kind != llvm_ir::Type::VOID) call.result = FreshValue();
		Emit(call);
		if (call.type.kind == llvm_ir::Type::VOID)
			return ExpressionValue(llvm_ir::Operand(
				llvm_ir::Operand::UNDEF, call.type), record.type);
		return ExpressionValue(llvm_ir::Operand::Local(call.type, call.result),
			record.type);
	}

	ExpressionValue LowerNew(const DumpNode& record,
		const ChildrenList& children)
	{
		if (children.empty() ||
			arena_.nodes[children[0]].kind != DUMP_CALL_EXPRESSION)
			throw std::runtime_error("LLVM new-expression has no allocation call");
		ExpressionValue storage = Convert(LowerCall(arena_.nodes[children[0]],
			Children(children[0])), llvm_ir::Type(llvm_ir::Type::POINTER),
			record.type);
		const TypeRecord& pointer = program_.types.Get(ObjectType(record.type));
		if (pointer.kind != TYPE_POINTER)
			throw std::logic_error("new-expression result is not a pointer");
		if (children.size() > 2)
			throw std::runtime_error(
				"complex new initialization is not implemented yet");
		if (children.size() == 2)
			(void)StoreObject(storage.operand, pointer.child,
				LowerValue(children[1]));
		return storage;
	}

	void LowerStatement(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (CurrentTerminated() && record.kind != DUMP_CASE_STATEMENT &&
			record.kind != DUMP_DEFAULT_STATEMENT &&
			record.kind != DUMP_LABELED_STATEMENT) return;
		if (stats_) ++stats_->semantic_nodes_lowered;
		const ChildrenList children = Children(node);
		switch (record.kind)
		{
		case DUMP_COMPOUND_STATEMENT: case DUMP_SIMPLE_DECLARATION:
		case DUMP_THEN: case DUMP_ELSE: case DUMP_CONDITION_DECLARATION:
			for (std::size_t i = 0; i < children.size(); ++i)
				LowerStatement(children[i]);
			return;
		case DUMP_FOR_INIT_STATEMENT:
			for (std::size_t i = 0; i < children.size(); ++i)
			{
				const DumpKind kind = arena_.nodes[children[i]].kind;
				if (kind == DUMP_SIMPLE_DECLARATION)
					LowerStatement(children[i]);
				else (void)LowerValue(children[i]);
			}
			return;
		case DUMP_ITERATION:
			for (std::size_t i = 0; i < children.size(); ++i)
				(void)LowerValue(children[i]);
			return;
		case DUMP_VARIABLE:
			LowerVariable(record, children);
			return;
		case DUMP_RETURN_STATEMENT:
			LowerReturn(children);
			return;
		case DUMP_EXPRESSION_STATEMENT:
			for (std::size_t i = 0; i < children.size(); ++i)
				(void)LowerValue(children[i]);
			return;
		case DUMP_IF_STATEMENT:
			LowerIf(children);
			return;
		case DUMP_WHILE_STATEMENT:
			LowerWhile(children);
			return;
		case DUMP_DO_STATEMENT:
			LowerDo(children);
			return;
		case DUMP_FOR_STATEMENT:
			LowerFor(children);
			return;
		case DUMP_SWITCH_STATEMENT:
			LowerSwitch(children);
			return;
		case DUMP_CASE_STATEMENT: case DUMP_DEFAULT_STATEMENT:
			LowerSwitchLabel(node, record, children);
			return;
		case DUMP_LABELED_STATEMENT:
			LowerLabel(record, children);
			return;
		case DUMP_GOTO_STATEMENT:
			LowerGoto(record);
			return;
		case DUMP_BREAK_STATEMENT:
			if (break_targets_.empty())
				throw std::logic_error("LLVM break has no target");
			EmitBranch(break_targets_.back());
			return;
		case DUMP_CONTINUE_STATEMENT:
			if (continue_targets_.empty())
				throw std::logic_error("LLVM continue has no target");
			EmitBranch(continue_targets_.back());
			return;
		case DUMP_TYPE_ALIAS: case DUMP_PARAMETER:
			return;
		default:
			throw std::runtime_error(std::string("LLVM exporter does not implement ") +
				DumpKindText(record.kind) + " statement");
		}
	}

	void LowerVariable(const DumpNode& record, const ChildrenList& children)
	{
		if (record.binding == kNoBinding || record.binding >= locations_.size() ||
			!locations_[record.binding].present)
			throw std::logic_error("local LLVM variable has no location");
		const Location& location = locations_[record.binding];
		if (children.empty())
			return; // default initialization leaves scalar/aggregate storage indeterminate
		if (location.storage_type.kind == llvm_ir::Type::ARRAY)
		{
			InitializeObject(LocationAddress(location), record.type, children[0]);
			return;
		}
		if (location.reference)
		{
			const DumpNode& initializer = arena_.nodes[children[0]];
			const llvm_ir::Operand address = initializer.category == VALUE_LVALUE ||
				initializer.category == VALUE_XVALUE ? LowerAddress(children[0]) :
				MaterializeTemporary(children[0], RemoveReference(record.type));
			StoreToLocation(location, ExpressionValue(address, record.type));
		}
		else StoreToLocation(location, LowerValue(children[0]));
	}

	llvm_ir::Operand MaterializeTemporary(std::uint32_t initializer, TypeId type)
	{
		llvm_ir::Instruction allocation(llvm_ir::Instruction::ALLOCA);
		allocation.result = FreshValue();
		allocation.type = StorageType(type);
		allocation.alignment = Alignment(type);
		Emit(allocation);
		const llvm_ir::Operand address = llvm_ir::Operand::Local(
			llvm_ir::Type(llvm_ir::Type::POINTER), allocation.result);
		(void)StoreObject(address, type, LowerValue(initializer));
		return address;
	}

	void InitializeObject(const llvm_ir::Operand& address, TypeId type,
		std::uint32_t initializer)
	{
		const TypeId object_type = RemoveTopCv(type);
		const TypeRecord& object = program_.types.Get(object_type);
		if (object.kind == TYPE_ARRAY)
		{
			const DumpNode& init = arena_.nodes[initializer];
			const ChildrenList values = Children(initializer);
			if (init.kind != DUMP_BRACED_INIT_LIST &&
				init.kind != DUMP_INITIALIZER_LIST)
				throw std::runtime_error(
					"non-list local array initialization is not implemented");
			if (values.size() > object.bound)
				throw std::logic_error("semantic array has excess initializers");
			for (std::uint64_t i = 0; i < object.bound; ++i)
			{
				llvm_ir::Instruction gep(llvm_ir::Instruction::GETELEMENTPTR);
				gep.result = FreshValue();
				gep.source_type = StorageType(object.child);
				gep.first = address;
				gep.operands.push_back(llvm_ir::Operand::Integer(
					llvm_ir::Type(llvm_ir::Type::I64), std::to_string(i)));
				Emit(gep);
				const llvm_ir::Operand element_address = llvm_ir::Operand::Local(
					llvm_ir::Type(llvm_ir::Type::POINTER), gep.result);
				if (i < values.size())
					InitializeObject(element_address, object.child,
						values[static_cast<std::size_t>(i)]);
				else (void)StoreObject(element_address, object.child,
					ZeroValue(object.child));
			}
			return;
		}
		const DumpNode& init = arena_.nodes[initializer];
		const ChildrenList values = Children(initializer);
		if (init.kind == DUMP_BRACED_INIT_LIST ||
			init.kind == DUMP_INITIALIZER_LIST)
		{
			if (values.empty())
			{
				(void)StoreObject(address, type, ZeroValue(type));
				return;
			}
			if (values.size() != 1)
				throw std::runtime_error("excess scalar local initializers");
			initializer = values[0];
		}
		(void)StoreObject(address, type, LowerValue(initializer));
	}

	void LowerReturn(const ChildrenList& children)
	{
		if (children.empty())
		{
			EmitReturn(ExpressionValue(llvm_ir::Operand(
				llvm_ir::Operand::UNDEF, llvm_ir::Type(llvm_ir::Type::VOID)),
				current_result_type_));
			return;
		}
		ExpressionValue value;
		if (IsReference(current_result_type_))
			value = ExpressionValue(LowerAddress(children[0]), current_result_type_);
		else value = Convert(LowerValue(children[0]), function_->result,
			current_result_type_);
		EmitReturn(value);
	}

	void EmitReturn(const ExpressionValue& value)
	{
		llvm_ir::Instruction result(llvm_ir::Instruction::RETURN);
		result.type = function_->result;
		if (result.type.kind != llvm_ir::Type::VOID) result.first = value.operand;
		Emit(result);
	}

	void EmitUnreachable()
	{
		Emit(llvm_ir::Instruction(llvm_ir::Instruction::UNREACHABLE));
	}

	void EmitBranch(std::size_t target)
	{
		llvm_ir::Instruction branch(llvm_ir::Instruction::BRANCH);
		branch.target = function_->blocks[target].name;
		Emit(branch);
		block_incoming_[target] = true;
	}

	void EmitConditionalBranch(const llvm_ir::Operand& condition,
		std::size_t yes, std::size_t no)
	{
		llvm_ir::Instruction branch(
			llvm_ir::Instruction::CONDITIONAL_BRANCH);
		branch.first = condition;
		branch.target = function_->blocks[yes].name;
		branch.alternate = function_->blocks[no].name;
		Emit(branch);
		block_incoming_[yes] = true;
		block_incoming_[no] = true;
	}

	ExpressionValue LowerConditionValue(std::uint32_t condition)
	{
		const DumpNode& condition_record = arena_.nodes[condition];
		if (condition_record.kind != DUMP_CONDITION)
			throw std::logic_error("LLVM control-flow condition has wrong kind");
		const ChildrenList children = Children(condition);
		if (children.size() != 1)
			throw std::runtime_error("semantic condition has invalid arity");
		const DumpNode& child = arena_.nodes[children[0]];
		ExpressionValue value;
		if (child.kind == DUMP_CONDITION_DECLARATION)
		{
			LowerStatement(children[0]);
			const ChildrenList declarations = Children(children[0]);
			if (declarations.size() != 1 ||
				arena_.nodes[declarations[0]].kind != DUMP_VARIABLE)
				throw std::runtime_error(
					"unsupported condition declaration shape");
			const BindingId binding = arena_.nodes[declarations[0]].binding;
			if (binding == kNoBinding || binding >= locations_.size() ||
				!locations_[binding].present)
				throw std::logic_error(
					"condition declaration has no LLVM location");
			value = LoadLocation(locations_[binding]);
		}
		else value = LowerValue(children[0]);
		return value;
	}

	ExpressionValue LowerCondition(std::uint32_t condition)
	{
		ExpressionValue value = LowerConditionValue(condition);
		return ConvertToCondition(value, value.source_type);
	}

	void LowerIf(const ChildrenList& children)
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
			throw std::runtime_error("semantic if statement is incomplete");
		ExpressionValue predicate = LowerCondition(condition);
		const std::size_t then_block = AddBlock(FreshBlock("if.then"));
		const std::size_t else_block = AddBlock(FreshBlock("if.else"));
		const std::size_t end_block = AddBlock(FreshBlock("if.end"));
		EmitConditionalBranch(predicate.operand, then_block, else_block);
		current_block_ = then_block;
		LowerStatement(then_node);
		if (!CurrentTerminated()) EmitBranch(end_block);
		current_block_ = else_block;
		if (else_node != kNoDumpEdge) LowerStatement(else_node);
		if (!CurrentTerminated()) EmitBranch(end_block);
		current_block_ = end_block;
	}

	void LowerWhile(const ChildrenList& children)
	{
		std::uint32_t condition = kNoDumpEdge;
		std::uint32_t body = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			if (arena_.nodes[children[i]].kind == DUMP_CONDITION)
				condition = children[i];
			else body = children[i];
		}
		if (condition == kNoDumpEdge || body == kNoDumpEdge)
			throw std::runtime_error("semantic while statement is incomplete");
		const std::size_t header = AddBlock(FreshBlock("while.cond"));
		const std::size_t body_block = AddBlock(FreshBlock("while.body"));
		const std::size_t exit_block = AddBlock(FreshBlock("while.end"));
		EmitBranch(header);
		current_block_ = header;
		const ExpressionValue predicate = LowerCondition(condition);
		EmitConditionalBranch(predicate.operand, body_block, exit_block);
		break_targets_.push_back(exit_block);
		continue_targets_.push_back(header);
		current_block_ = body_block;
		LowerStatement(body);
		if (!CurrentTerminated()) EmitBranch(header);
		continue_targets_.pop_back();
		break_targets_.pop_back();
		current_block_ = exit_block;
	}

	void LowerDo(const ChildrenList& children)
	{
		std::uint32_t condition = kNoDumpEdge;
		std::uint32_t body = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			if (arena_.nodes[children[i]].kind == DUMP_CONDITION)
				condition = children[i];
			else body = children[i];
		}
		if (condition == kNoDumpEdge || body == kNoDumpEdge)
			throw std::runtime_error("semantic do statement is incomplete");
		const std::size_t body_block = AddBlock(FreshBlock("do.body"));
		const std::size_t condition_block = AddBlock(FreshBlock("do.cond"));
		const std::size_t exit_block = AddBlock(FreshBlock("do.end"));
		EmitBranch(body_block);
		break_targets_.push_back(exit_block);
		continue_targets_.push_back(condition_block);
		current_block_ = body_block;
		LowerStatement(body);
		if (!CurrentTerminated()) EmitBranch(condition_block);
		current_block_ = condition_block;
		const ExpressionValue predicate = LowerCondition(condition);
		EmitConditionalBranch(predicate.operand, body_block, exit_block);
		continue_targets_.pop_back();
		break_targets_.pop_back();
		current_block_ = exit_block;
	}

	void LowerFor(const ChildrenList& children)
	{
		std::uint32_t init = kNoDumpEdge;
		std::uint32_t condition = kNoDumpEdge;
		std::uint32_t iteration = kNoDumpEdge;
		std::uint32_t body = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			switch (arena_.nodes[children[i]].kind)
			{
			case DUMP_FOR_INIT_STATEMENT: init = children[i]; break;
			case DUMP_CONDITION: condition = children[i]; break;
			case DUMP_ITERATION: iteration = children[i]; break;
			default: body = children[i]; break;
			}
		}
		if (body == kNoDumpEdge)
			throw std::runtime_error("semantic for statement has no body");
		if (init != kNoDumpEdge) LowerStatement(init);
		const std::size_t header = AddBlock(FreshBlock("for.cond"));
		const std::size_t body_block = AddBlock(FreshBlock("for.body"));
		const std::size_t iteration_block = AddBlock(FreshBlock("for.inc"));
		const std::size_t exit_block = AddBlock(FreshBlock("for.end"));
		EmitBranch(header);
		current_block_ = header;
		if (condition == kNoDumpEdge) EmitBranch(body_block);
		else
		{
			const ExpressionValue predicate = LowerCondition(condition);
			EmitConditionalBranch(predicate.operand, body_block, exit_block);
		}
		break_targets_.push_back(exit_block);
		continue_targets_.push_back(iteration_block);
		current_block_ = body_block;
		LowerStatement(body);
		if (!CurrentTerminated()) EmitBranch(iteration_block);
		current_block_ = iteration_block;
		if (iteration != kNoDumpEdge) LowerStatement(iteration);
		if (!CurrentTerminated()) EmitBranch(header);
		continue_targets_.pop_back();
		break_targets_.pop_back();
		current_block_ = exit_block;
	}

	typedef std::unordered_map<std::uint32_t, std::size_t> SwitchLabelMap;

	void CollectSwitchLabels(std::uint32_t node, SwitchLabelMap* labels,
		std::vector<std::uint32_t>* order)
	{
		const ChildrenList children = Children(node);
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const std::uint32_t child = children[i];
			const DumpNode& record = arena_.nodes[child];
			if (record.kind == DUMP_SWITCH_STATEMENT) continue;
			if (record.kind == DUMP_CASE_STATEMENT ||
				record.kind == DUMP_DEFAULT_STATEMENT)
			{
				(*labels)[child] = AddBlock(FreshBlock(
					record.kind == DUMP_CASE_STATEMENT ? "switch.case" :
					"switch.default"));
				order->push_back(child);
			}
			CollectSwitchLabels(child, labels, order);
		}
	}

	void LowerSwitch(const ChildrenList& children)
	{
		std::uint32_t condition = kNoDumpEdge;
		std::uint32_t body = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			if (arena_.nodes[children[i]].kind == DUMP_CONDITION)
				condition = children[i];
			else body = children[i];
		}
		if (condition == kNoDumpEdge || body == kNoDumpEdge)
			throw std::runtime_error("semantic switch statement is incomplete");
		ExpressionValue value = LowerConditionValue(condition);
		const llvm_ir::Type switch_type = ValueType(value.source_type);
		value = Convert(value, switch_type, value.source_type);
		const std::size_t exit_block = AddBlock(FreshBlock("switch.end"));
		SwitchLabelMap labels;
		std::vector<std::uint32_t> order;
		CollectSwitchLabels(body, &labels, &order);
		const std::size_t body_entry = AddBlock(FreshBlock("switch.body"));
		llvm_ir::Instruction dispatch(llvm_ir::Instruction::SWITCH);
		dispatch.first = value.operand;
		dispatch.target = function_->blocks[exit_block].name;
		std::size_t default_block = exit_block;
		for (std::size_t i = 0; i < order.size(); ++i)
		{
			const DumpNode& label = arena_.nodes[order[i]];
			const std::size_t block = labels[order[i]];
			if (label.kind == DUMP_DEFAULT_STATEMENT)
			{
				default_block = block;
				continue;
			}
			const ChildrenList label_children = Children(order[i]);
			if (label_children.empty() ||
				!arena_.nodes[label_children[0]].constant)
				throw std::runtime_error("switch case has no constant fact");
			dispatch.operands.push_back(llvm_ir::Operand::Integer(switch_type,
				std::to_string(arena_.nodes[label_children[0]].constant_value)));
			dispatch.labels.push_back(function_->blocks[block].name);
			block_incoming_[block] = true;
		}
		dispatch.target = function_->blocks[default_block].name;
		block_incoming_[default_block] = true;
		Emit(dispatch);
		break_targets_.push_back(exit_block);
		switch_labels_.push_back(labels);
		current_block_ = body_entry;
		LowerStatement(body);
		if (!CurrentTerminated()) EmitBranch(exit_block);
		switch_labels_.pop_back();
		break_targets_.pop_back();
		current_block_ = exit_block;
	}

	void LowerSwitchLabel(std::uint32_t node, const DumpNode& record,
		const ChildrenList& children)
	{
		if (switch_labels_.empty() ||
			switch_labels_.back().count(node) == 0)
			throw std::logic_error("switch label has no active switch");
		const std::size_t target = switch_labels_.back().find(node)->second;
		if (!CurrentTerminated()) EmitBranch(target);
		current_block_ = target;
		const std::size_t begin =
			record.kind == DUMP_CASE_STATEMENT ? 1 : 0;
		for (std::size_t i = begin; i < children.size(); ++i)
			LowerStatement(children[i]);
	}

	void LowerLabel(const DumpNode& record, const ChildrenList& children)
	{
		const std::unordered_map<NameId, std::size_t>::const_iterator found =
			label_blocks_.find(record.text);
		if (found == label_blocks_.end())
			throw std::logic_error("semantic label has no LLVM block");
		if (!CurrentTerminated()) EmitBranch(found->second);
		current_block_ = found->second;
		for (std::size_t i = 0; i < children.size(); ++i)
			LowerStatement(children[i]);
	}

	void LowerGoto(const DumpNode& record)
	{
		const std::unordered_map<NameId, std::size_t>::const_iterator found =
			label_blocks_.find(record.text);
		if (found == label_blocks_.end())
			throw std::runtime_error("goto target has no semantic label");
		EmitBranch(found->second);
	}

	const SemanticGraphView& graph_;
	const Program& program_;
	const DumpArena& arena_;
	llvm_ir::Module& output_;
	LlvmIrExportStats* stats_;
	std::vector<FunctionFact> functions_;
	std::vector<GlobalFact> globals_;
	std::unordered_map<std::string, std::string> string_names_;
	std::vector<std::string> function_names_;
	std::vector<std::string> global_names_;
	std::vector<Location> locations_;
	llvm_ir::Function* function_;
	std::size_t current_block_;
	std::uint32_t value_ordinal_;
	std::uint32_t block_ordinal_;
	std::vector<bool> block_incoming_;
	std::vector<std::size_t> break_targets_;
	std::vector<std::size_t> continue_targets_;
	std::unordered_map<NameId, std::size_t> label_blocks_;
	std::vector<SwitchLabelMap> switch_labels_;
	TypeId current_result_type_;
	BindingId current_function_binding_;
};

class LlvmGraphConsumer : public SemanticGraphConsumer
{
public:
	LlvmGraphConsumer(llvm_ir::Module* module, LlvmIrExportStats* stats)
		: module_(module), stats_(stats) {}

	void Consume(const SemanticGraphView& graph)
	{
		const std::chrono::steady_clock::time_point started =
			std::chrono::steady_clock::now();
		SemanticLlvmLowerer(graph, module_, stats_).Lower();
		if (stats_)
			stats_->lowering_nanoseconds = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started).count());
	}

private:
	llvm_ir::Module* module_;
	LlvmIrExportStats* stats_;
};

}

LlvmIrExportStats::LlvmIrExportStats()
	: semantic_nodes_lowered(0), functions(0), globals(0), blocks(0),
	  instructions(0), lowering_nanoseconds(0), serialization_nanoseconds(0)
{
}

void WriteLlvmIrTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options,
	std::ostream& output, LlvmIrExportStats* stats)
{
	if (stats) *stats = LlvmIrExportStats();
	llvm_ir::Module module;
	module.source_filename = path;
	module.target_data_layout = kX86_64DataLayout;
	module.target_triple = kX86_64Triple;
	LlvmGraphConsumer consumer(&module, stats);
	ConsumeSemanticTranslationUnit(path, source, options, consumer,
		stats ? &stats->semantic : 0, true, true, false);
	const std::chrono::steady_clock::time_point started =
		std::chrono::steady_clock::now();
	output << llvm_ir::SerializeModule(module);
	if (!output) throw std::runtime_error("unable to write LLVM IR output");
	if (stats)
		stats->serialization_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - started).count());
}

}
