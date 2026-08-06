#include "pa15_lowering.h"
#include "pa15_lowir_model.h"
#include "pa15_lowir_render.h"

#include "abi_mangle.h"
#include "pa11_model.h"
#include "pa12_semantic.h"
#include "pa12_semantic_model.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <ostream>
#include <streambuf>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace cppgm
{
namespace
{

using namespace pa11;
using namespace pa12_semantic_detail;

using namespace pa15_lowir_detail;

std::string StripOperationPrefix(const std::string& operation)
{
	const std::size_t colon = operation.rfind(':');
	return colon == std::string::npos ? operation : operation.substr(colon + 1);
}

std::string SanitizeSymbol(const std::string& name)
{
	std::string result;
	result.reserve(name.size() + 8);
	for (std::size_t i = 0; i < name.size(); ++i)
	{
		if (i + 1 < name.size() && name[i] == ':' && name[i + 1] == ':')
		{
			result += "__";
			++i;
		}
		else
		{
			const unsigned char c = static_cast<unsigned char>(name[i]);
			result += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '_' ? static_cast<char>(c) : '_';
		}
	}
	if (result.empty()) result = "anonymous";
	return result;
}

template <typename Value, std::size_t InlineCount>
class SmallSequence
{
public:
	SmallSequence() : count_(0) {}

	void Push(const Value& value)
	{
		if (count_ < InlineCount) inline_[count_] = value;
		else overflow_.push_back(value);
		++count_;
	}

	std::size_t size() const { return count_; }
	bool empty() const { return count_ == 0; }
	const Value& operator[](std::size_t index) const
	{
		return index < InlineCount ? inline_[index] : overflow_[index - InlineCount];
	}

private:
	Value inline_[InlineCount];
	std::vector<Value> overflow_;
	std::size_t count_;
};

typedef SmallSequence<std::uint32_t, 8> NodeChildren;
typedef SmallSequence<Operand, 8> CallArguments;
typedef SmallSequence<std::uint8_t, 8> CallArgumentFlags;
typedef SmallSequence<std::uint32_t, 8> SwitchCases;

class GraphLowerer
{
public:
	GraphLowerer(const SemanticGraphView& graph, TypedProgram& output,
		LowIRLoweringStats* stats, std::size_t source_ordinal)
		: graph_(graph), program_(graph.program), arena_(graph.arena),
		  output_(output), stats_(stats), function_(0), current_block_(0),
		  current_result_(LowVoid()), current_result_reference_(false),
		  temp_counter_(0), block_counter_(0), generated_slot_ordinal_(0),
		  source_ordinal_(source_ordinal)
	{
		function_symbols_.resize(program_.bindings.size(), kNoLowId);
		global_symbols_.resize(program_.bindings.size(), kNoLowId);
		literal_symbols_.resize(arena_.nodes.size(), kNoLowId);
		function_definition_.resize(program_.bindings.size(), kNoDumpEdge);
		function_declaration_.resize(program_.bindings.size(), kNoDumpEdge);
		global_node_.resize(program_.bindings.size(), kNoDumpEdge);
		binding_slots_.resize(program_.bindings.size(), kNoLowId);
		generated_slots_.resize(arena_.nodes.size(), kNoLowId);
		switch_case_blocks_.resize(arena_.nodes.size(), kNoLowId);
	}

	void Lower()
	{
		ScanTop(graph_.root);
		EmitTop(graph_.root);
		EmitDynamicInitializer();
	}

private:
	enum StatementTaskKind : std::uint8_t
	{
		STATEMENT_NODE,
		STATEMENT_SEQUENCE,
		STATEMENT_FOR_COMPONENTS,
		STATEMENT_IF_AFTER_THEN,
		STATEMENT_IF_AFTER_ELSE,
		STATEMENT_LOOP_AFTER_BODY,
		STATEMENT_DO_AFTER_BODY,
		STATEMENT_FOR_AFTER_INIT,
		STATEMENT_FOR_AFTER_BODY,
		STATEMENT_FOR_AFTER_ITERATION,
		STATEMENT_SWITCH_AFTER_BODY
	};

	struct StatementTask
	{
		std::uint32_t node;
		std::uint32_t auxiliary;
		std::uint32_t last;
		BlockId first;
		BlockId second;
		BlockId third;
		StatementTaskKind kind;
		bool flag;

		explicit StatementTask(StatementTaskKind kind_value)
			: node(kNoDumpEdge), auxiliary(kNoDumpEdge), last(kNoDumpEdge),
			  first(kNoLowId), second(kNoLowId),
			  third(kNoLowId), kind(kind_value), flag(false) {}
	};

	NodeChildren Children(std::uint32_t node) const
	{
		NodeChildren result;
		for (std::uint32_t edge = arena_.nodes[node].first_edge;
			edge != kNoDumpEdge; edge = arena_.edges[edge].next)
			result.Push(arena_.edges[edge].child);
		return result;
	}

	LowType LowerType(TypeId type) const
	{
		const TypeRecord* record = &program_.types.Get(type);
		while (record->kind == TYPE_QUALIFIED)
		{
			type = record->child;
			record = &program_.types.Get(type);
		}
		if (record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE || record->kind == TYPE_POINTER ||
			record->kind == TYPE_ARRAY || record->kind == TYPE_FUNCTION ||
			record->kind == TYPE_MEMBER_POINTER) return LowPtr();
		if (record->kind == TYPE_NAMED)
		{
			const EntityRecord& entity = program_.entities[record->entity];
			if ((entity.flavor == NAMED_ENUM || entity.flavor == NAMED_ENUM_CLASS) &&
				entity.underlying != kNoType) return LowerType(entity.underlying);
			throw std::runtime_error("PA15 scalar checkpoint cannot lower class type");
		}
		if (record->kind != TYPE_FUNDAMENTAL)
			throw std::runtime_error("invalid PA15 scalar type");
		switch (record->fundamental)
		{
		case FUND_BOOL: return LowU8();
		case FUND_CHAR: case FUND_SIGNED_CHAR: return LowI8();
		case FUND_UNSIGNED_CHAR: return LowU8();
		case FUND_SHORT_INT: return LowI16();
		case FUND_UNSIGNED_SHORT_INT: return LowU16();
		case FUND_INT: return LowI32();
		case FUND_UNSIGNED_INT: return LowU32();
		case FUND_LONG_INT: case FUND_LONG_LONG_INT:
		case FUND_WCHAR_T: case FUND_CHAR16_T: case FUND_CHAR32_T:
			return LowI64();
		case FUND_UNSIGNED_LONG_INT: case FUND_UNSIGNED_LONG_LONG_INT:
			return LowU64();
		case FUND_FLOAT: return LowF32();
		case FUND_DOUBLE: return LowF64();
		case FUND_LONG_DOUBLE: return LowF80();
		case FUND_VOID: return LowVoid();
		case FUND_NULLPTR_T: return LowPtr();
		}
		throw std::runtime_error("unsupported PA15 fundamental type");
	}

	bool IsReferenceType(TypeId type) const
	{
		const TypeRecord& record = program_.types.Get(type);
		return record.kind == TYPE_LVALUE_REFERENCE ||
			record.kind == TYPE_RVALUE_REFERENCE;
	}

	TypeId RemoveReference(TypeId type) const
	{
		const TypeRecord& record = program_.types.Get(type);
		return IsReferenceType(type) ? record.child : type;
	}

	TypeId RemoveTopQualifiers(TypeId type) const
	{
		while (program_.types.Get(type).kind == TYPE_QUALIFIED)
			type = program_.types.Get(type).child;
		return type;
	}

	TypeId ExpressionObjectType(TypeId type) const
	{
		return RemoveTopQualifiers(RemoveReference(type));
	}

	bool IsArrayType(TypeId type) const
	{
		return program_.types.Get(ExpressionObjectType(type)).kind == TYPE_ARRAY;
	}

	bool IsFunctionType(TypeId type) const
	{
		return program_.types.Get(ExpressionObjectType(type)).kind == TYPE_FUNCTION;
	}

	LowType LowerExpressionType(TypeId type) const
	{
		return LowerType(RemoveReference(type));
	}

	LowType LowerStorageType(TypeId type) const
	{
		const TypeId object = RemoveTopQualifiers(type);
		const TypeRecord& record = program_.types.Get(object);
		if (record.kind == TYPE_ARRAY)
			return record.bound == 0 ? LowPtr() :
				LowObject(program_.SizeOf(object), program_.AlignOf(object));
		return LowerType(type);
	}

	TypeId ArrayElementType(TypeId type) const
	{
		const TypeRecord& record = program_.types.Get(ExpressionObjectType(type));
		if (record.kind != TYPE_ARRAY)
			throw std::logic_error("PA15 expected array type");
		return record.child;
	}

	bool IsPointerLikeType(TypeId type) const
	{
		const TypeRecord& record = program_.types.Get(ExpressionObjectType(type));
		return record.kind == TYPE_POINTER || record.kind == TYPE_ARRAY;
	}

	TypeId PointeeType(TypeId type) const
	{
		const TypeRecord& record = program_.types.Get(ExpressionObjectType(type));
		if (record.kind != TYPE_POINTER && record.kind != TYPE_ARRAY)
			throw std::logic_error("PA15 expected pointer-like type");
		return record.child;
	}

	abi_mangle::AbiType MakeAbiType(TypeId type) const
	{
		using namespace abi_mangle;
		std::vector<AbiTypeModifier> modifiers;
		const TypeRecord* record = &program_.types.Get(type);
		while (record->kind == TYPE_QUALIFIED || record->kind == TYPE_POINTER ||
			record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE || record->kind == TYPE_ARRAY)
		{
			AbiTypeModifier modifier;
			if (record->kind == TYPE_QUALIFIED)
			{
				modifier.kind = ABI_TYPE_CV;
				modifier.is_const = (record->cv & CV_CONST) != 0;
				modifier.is_volatile = (record->cv & CV_VOLATILE) != 0;
			}
			else if (record->kind == TYPE_ARRAY)
			{
				modifier.kind = ABI_TYPE_ARRAY;
				modifier.array_bound.kind = ABI_ARRAY_BOUND_VALUE;
				modifier.array_bound.value = std::to_string(record->bound);
			}
			else modifier.kind = record->kind == TYPE_POINTER ? ABI_TYPE_POINTER :
				record->kind == TYPE_LVALUE_REFERENCE ? ABI_TYPE_LVALUE_REFERENCE :
				ABI_TYPE_RVALUE_REFERENCE;
			modifiers.push_back(modifier);
			type = record->child;
			record = &program_.types.Get(type);
		}
		abi_mangle::AbiType result;
		result.modifiers.swap(modifiers);
		if (record->kind == TYPE_FUNCTION)
		{
			result.kind = ABI_TYPE_FUNCTION;
			result.types.push_back(MakeAbiType(record->child));
			const TypeId* parameters = program_.types.Parameters(type);
			for (std::size_t i = 0; i < record->parameter_count; ++i)
				result.types.push_back(MakeAbiType(parameters[i]));
			result.variadic = record->variadic;
			return result;
		}
		if (record->kind == TYPE_NAMED)
		{
			result.kind = ABI_TYPE_NAMED;
			result.name = program_.names.Get(program_.entities[record->entity].name);
			return result;
		}
		if (record->kind != TYPE_FUNDAMENTAL)
			throw std::runtime_error("unsupported ABI type in PA15");
		result.kind = ABI_TYPE_BUILTIN;
		switch (record->fundamental)
		{
		case FUND_VOID: result.name = "void"; break;
		case FUND_BOOL: result.name = "bool"; break;
		case FUND_CHAR: result.name = "char"; break;
		case FUND_SIGNED_CHAR: result.name = "schar"; break;
		case FUND_UNSIGNED_CHAR: result.name = "uchar"; break;
		case FUND_SHORT_INT: result.name = "short"; break;
		case FUND_UNSIGNED_SHORT_INT: result.name = "ushort"; break;
		case FUND_INT: result.name = "int"; break;
		case FUND_UNSIGNED_INT: result.name = "uint"; break;
		case FUND_LONG_INT: result.name = "long"; break;
		case FUND_UNSIGNED_LONG_INT: result.name = "ulong"; break;
		case FUND_LONG_LONG_INT: result.name = "longlong"; break;
		case FUND_UNSIGNED_LONG_LONG_INT: result.name = "ulonglong"; break;
		case FUND_FLOAT: result.name = "float"; break;
		case FUND_DOUBLE: result.name = "double"; break;
		case FUND_LONG_DOUBLE: result.name = "longdouble"; break;
		case FUND_WCHAR_T: result.name = "wchar"; break;
		case FUND_CHAR16_T: result.name = "char16"; break;
		case FUND_CHAR32_T: result.name = "char32"; break;
		case FUND_NULLPTR_T: result.name = "ulong"; break;
		}
		return result;
	}

	std::string MangleFunction(const DumpNode& node) const
	{
		using namespace abi_mangle;
		const std::string qualified = program_.names.Get(node.text);
		if (qualified == "main") return std::string();
		const BindingRecord& binding = program_.bindings[node.binding];
		if (binding.language_linkage == LANGUAGE_LINKAGE_C &&
			binding.storage_class != STORAGE_CLASS_STATIC)
			return program_.names.Get(binding.name);
		AbiFactFile file;
		file.cases.push_back(AbiFactCase());
		AbiFactRecord target;
		target.set_kind(ABI_FACT_RECORD_TARGET);
		target.target.kind = ABI_TARGET_FACT_FUNCTION;
		target.target.internal_linkage = binding.storage_class == STORAGE_CLASS_STATIC;
		target.target.function.kind = ABI_FUNCTION_TARGET_PATH;
		target.target.function.qualified_name = qualified;
		file.cases[0].records.push_back(target);
		const TypeRecord& function_type = program_.types.Get(node.type);
		const TypeId* parameters = program_.types.Parameters(node.type);
		for (std::size_t i = 0; i < function_type.parameter_count; ++i)
		{
			AbiFactRecord parameter;
			parameter.set_kind(ABI_FACT_RECORD_FUNCTION);
			parameter.function.kind = ABI_FUNCTION_RECORD_PARAMETER;
			parameter.function.type = MakeAbiType(parameters[i]);
			file.cases[0].records.push_back(parameter);
		}
		if (function_type.variadic)
		{
			AbiFactRecord variadic;
			variadic.set_kind(ABI_FACT_RECORD_FUNCTION);
			variadic.function.kind = ABI_FUNCTION_RECORD_VARIADIC;
			file.cases[0].records.push_back(variadic);
		}
		std::string result = mangle_fact_file(file);
		if (!result.empty() && result[result.size() - 1] == '\n') result.resize(result.size() - 1);
		return result;
	}

	std::string MangleVariable(const DumpNode& node) const
	{
		using namespace abi_mangle;
		const BindingRecord& binding = program_.bindings[node.binding];
		if (binding.language_linkage == LANGUAGE_LINKAGE_C &&
			binding.storage_class != STORAGE_CLASS_STATIC)
			return program_.names.Get(binding.name);
		AbiFactFile file;
		file.cases.push_back(AbiFactCase());
		AbiFactRecord target;
		target.set_kind(ABI_FACT_RECORD_TARGET);
		target.target.kind = ABI_TARGET_FACT_VARIABLE;
		target.target.internal_linkage = binding.storage_class == STORAGE_CLASS_STATIC;
		target.target.qualified_name = program_.names.Get(
			binding.qualified_name != 0 ? binding.qualified_name : node.text);
		file.cases[0].records.push_back(target);
		std::string result = mangle_fact_file(file);
		if (!result.empty() && result[result.size() - 1] == '\n') result.resize(result.size() - 1);
		return result;
	}

	SymbolId InternSymbol(const DumpNode& node, Symbol::Kind kind,
		const std::string& proposed_name, const std::string& object_name)
	{
		const BindingRecord& binding = program_.bindings[node.binding];
		const bool internal = binding.storage_class == STORAGE_CLASS_STATIC;
		const bool c_linkage =
			binding.language_linkage == LANGUAGE_LINKAGE_C;
		SymbolIdentity identity;
		identity.kind = kind;
		identity.path = output_.identities.InternPath(program_,
			c_linkage && !internal ? program_.GlobalScope() : binding.owner,
			binding.name);
		identity.signature = kind == Symbol::FUNCTION_SYMBOL && !c_linkage ?
			output_.identities.InternFunctionSignature(program_, node.type,
				identity_type_cache_) : kNoLowId;
		identity.internal_owner = internal ? source_ordinal_ + 1 : 0;
		const IdentityTypeId source_type = output_.identities.InternType(
			program_, node.type, identity_type_cache_);
		SymbolId found = kNoLowId;
		if (output_.symbol_index.Find(identity, &found))
		{
			Symbol& symbol = output_.symbols[found];
			if (symbol.source_type != source_type)
				throw std::runtime_error("conflicting cross-source PA15 symbol type");
			if (!symbol.object_name.empty() && !object_name.empty() &&
				symbol.object_name != object_name)
				throw std::logic_error("conflicting PA15 ABI object identity");
			symbol.nonthrowing = symbol.nonthrowing || binding.nonthrowing;
			return found;
		}
		if (output_.symbols.size() >= kNoLowId)
			throw std::runtime_error("too many PA15 emission symbols");
		std::size_t& count = output_.symbol_name_counts[proposed_name];
		const std::string name = count++ == 0 ? proposed_name :
			proposed_name + "__sym" + std::to_string(count);
		const SymbolId symbol = static_cast<SymbolId>(output_.symbols.size());
		output_.symbols.push_back(Symbol(kind, name, object_name, c_linkage,
			internal, binding.nonthrowing));
		output_.symbols.back().source_type = source_type;
		output_.symbol_index.Insert(identity, symbol);
		return symbol;
	}

	void RegisterFunction(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.binding == kNoBinding) return;
		if (function_symbols_[record.binding] == kNoLowId)
		{
			const std::string base = SanitizeSymbol(program_.names.Get(record.text));
			std::size_t& count = overload_counts_[base];
			++count;
			const std::string name = count == 1 ? base :
				base + "__ov" + std::to_string(count);
			function_symbols_[record.binding] = InternSymbol(record,
				Symbol::FUNCTION_SYMBOL, name, MangleFunction(record));
		}
		if (record.kind == DUMP_FUNCTION_DEFINITION)
			function_definition_[record.binding] = node;
		else if (function_declaration_[record.binding] == kNoDumpEdge)
			function_declaration_[record.binding] = node;
	}

	void ScanTop(std::uint32_t node)
	{
		std::vector<std::uint32_t> pending(1, node);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const DumpNode& record = arena_.nodes[current];
			if (record.kind == DUMP_FUNCTION_DEFINITION ||
				record.kind == DUMP_FUNCTION_DECLARATION)
			{
				RegisterFunction(current);
				continue;
			}
			if (record.kind == DUMP_VARIABLE && record.binding != kNoBinding)
			{
				const BindingId canonical =
					program_.bindings[record.binding].canonical;
				if (global_symbols_[canonical] == kNoLowId)
				{
					const std::string name = SanitizeSymbol(program_.names.Get(
						program_.bindings[record.binding].qualified_name != 0 ?
						program_.bindings[record.binding].qualified_name : record.text));
					global_symbols_[canonical] = InternSymbol(record,
						Symbol::GLOBAL_SYMBOL, name, MangleVariable(record));
				}
				global_symbols_[record.binding] = global_symbols_[canonical];
				const bool declaration_only = Children(current).empty() &&
					program_.bindings[record.binding].storage_class ==
						STORAGE_CLASS_EXTERN;
				if (!declaration_only || global_node_[canonical] == kNoDumpEdge)
					global_node_[canonical] = current;
				continue;
			}
			if (record.kind != DUMP_TRANSLATION_UNIT &&
				record.kind != DUMP_NAMESPACE)
				continue;
			const NodeChildren children = Children(current);
			for (std::size_t i = children.size(); i != 0; --i)
				pending.push_back(children[i - 1]);
		}
	}

	void EmitTop(std::uint32_t node)
	{
		std::vector<std::uint32_t> pending(1, node);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const DumpNode& record = arena_.nodes[current];
			if (record.kind == DUMP_FUNCTION_DECLARATION)
			{
				if (record.binding != kNoBinding &&
					function_definition_[record.binding] == kNoDumpEdge &&
					function_declaration_[record.binding] == current)
				{
						const SymbolId symbol = function_symbols_[record.binding];
						if (!output_.symbols[symbol].declaration_emitted)
						{
							output_.declarations.push_back(LowerDeclaration(current));
							output_.symbols[symbol].declaration_emitted = true;
						}
				}
				continue;
			}
			if (record.kind == DUMP_FUNCTION_DEFINITION)
			{
				if (record.binding != kNoBinding &&
					function_definition_[record.binding] == current)
				{
						const SymbolId symbol = function_symbols_[record.binding];
						if (output_.symbols[symbol].definition_emitted)
							throw std::runtime_error(
								"duplicate cross-source function definition");
						output_.functions.push_back(LowerFunction(current));
						output_.symbols[symbol].definition_emitted = true;
				}
				continue;
			}
			if (record.kind == DUMP_VARIABLE)
			{
				if (record.binding != kNoBinding)
				{
					const BindingId canonical =
						program_.bindings[record.binding].canonical;
					if (global_node_[canonical] == current)
					{
						const bool declaration_only = Children(current).empty() &&
							program_.bindings[record.binding].storage_class ==
								STORAGE_CLASS_EXTERN;
						if (declaration_only)
						{
								const SymbolId symbol = global_symbols_[canonical];
								if (!output_.symbols[symbol].declaration_emitted)
								{
									output_.global_declarations.push_back(
										LowerGlobalDeclaration(current));
									output_.symbols[symbol].declaration_emitted = true;
								}
						}
						else
						{
								const SymbolId symbol = global_symbols_[canonical];
								if (output_.symbols[symbol].definition_emitted)
									throw std::runtime_error(
										"duplicate cross-source global definition");
								output_.globals.push_back(LowerGlobal(current));
								output_.symbols[symbol].definition_emitted = true;
						}
					}
				}
				continue;
			}
			if (record.kind != DUMP_TRANSLATION_UNIT &&
				record.kind != DUMP_NAMESPACE)
				continue;
			const NodeChildren children = Children(current);
			for (std::size_t i = children.size(); i != 0; --i)
				pending.push_back(children[i - 1]);
		}
	}

	void FillBoundary(std::uint32_t node, std::vector<Parameter>* parameters,
		LowType* result, bool* variadic) const
	{
		const DumpNode& record = arena_.nodes[node];
		const TypeRecord& function_type = program_.types.Get(record.type);
		*result = LowerType(function_type.child);
		*variadic = function_type.variadic;
		const NodeChildren children = Children(node);
		std::size_t parameter_index = 0;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& child = arena_.nodes[children[i]];
			if (child.kind != DUMP_PARAMETER) continue;
			Parameter parameter;
			parameter.name = child.text == 0 ? std::string() : program_.names.Get(child.text);
			if (parameter.name.empty()) parameter.name =
				(record.kind == DUMP_FUNCTION_DECLARATION ? "arg" : "__param") +
				std::to_string(parameter_index);
			parameter.type = LowerType(child.type);
			const TypeId* source_parameters = program_.types.Parameters(record.type);
			parameter.reference = parameter_index < function_type.parameter_count &&
				IsReferenceType(source_parameters[parameter_index]);
			parameters->push_back(parameter);
			++parameter_index;
		}
		const TypeId* source_parameters = program_.types.Parameters(record.type);
		while (parameter_index < function_type.parameter_count)
		{
			Parameter parameter;
			parameter.name =
				(record.kind == DUMP_FUNCTION_DECLARATION ? "arg" : "__param") +
				std::to_string(parameter_index);
			parameter.type = LowerType(source_parameters[parameter_index]);
			parameter.reference = IsReferenceType(source_parameters[parameter_index]);
			parameters->push_back(parameter);
			++parameter_index;
		}
	}

	FunctionDeclaration LowerDeclaration(std::uint32_t node) const
	{
		const DumpNode& record = arena_.nodes[node];
		FunctionDeclaration declaration;
		declaration.symbol = function_symbols_[record.binding];
		FillBoundary(node, &declaration.parameters, &declaration.result,
			&declaration.variadic);
		return declaration;
	}

	GlobalDeclaration LowerGlobalDeclaration(std::uint32_t node) const
	{
		const DumpNode& record = arena_.nodes[node];
		GlobalDeclaration declaration;
		declaration.symbol = global_symbols_[record.binding];
		const TypeRecord& type = program_.types.Get(
			RemoveTopQualifiers(record.type));
		declaration.typed = type.kind != TYPE_ARRAY || type.bound != 0;
		if (declaration.typed) declaration.type = LowerStorageType(record.type);
		return declaration;
	}

	Global LowerGlobal(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		Global global;
		global.symbol = global_symbols_[record.binding];
		global.type = LowerStorageType(record.type);
		const NodeChildren children = Children(node);
		const TypeRecord& source_type = program_.types.Get(
			ExpressionObjectType(record.type));
		if (source_type.kind == TYPE_ARRAY)
		{
			LowerStructuredGlobal(record, children, source_type, &global);
		}
		else if (!children.empty())
		{
			const DumpNode& initializer = arena_.nodes[children[0]];
			if (IsFloating(global.type) && initializer.kind == DUMP_LITERAL)
			{
				global.initializer_kind = Global::FLOATING_VALUE;
				global.floating_initializer = output_.literals.Intern(
					program_.names.Get(initializer.text));
			}
			else if (initializer.constant)
			{
				global.initializer_kind = Global::INTEGER_VALUE;
				global.initializer = initializer.constant_value;
			}
			else
			{
				SymbolId symbol = kNoLowId;
				std::int64_t offset = 0;
				if (!ResolveConstantAddress(children[0], &symbol, &offset))
					throw std::runtime_error(
						"global initializer is missing its PA12 constant fact");
				if (!RequiresDynamicAddress(children[0]))
				{
					global.initializer_kind = Global::ADDRESS_VALUE;
					global.address_symbol = symbol;
					global.address_offset = offset;
				}
				else
				{
					global.initializer_kind = Global::ZERO;
					dynamic_initializers_.push_back(
						DynamicInitializer(global.symbol, children[0]));
				}
			}
		}
		if (stats_) ++stats_->globals;
		return global;
	}

	void EmitDynamicInitializer()
	{
		if (dynamic_initializers_.empty()) return;
		const std::string proposed = "__cppgm_init";
		std::size_t& count = output_.symbol_name_counts[proposed];
		const std::string name = count++ == 0 ? proposed :
			proposed + "__sym" + std::to_string(count);
		const SymbolId symbol = static_cast<SymbolId>(output_.symbols.size());
		output_.symbols.push_back(Symbol(Symbol::FUNCTION_SYMBOL, name,
			std::string(), false, true, false));
		output_.symbols.back().definition_emitted = true;

		Function result;
		result.symbol = symbol;
		result.result = LowVoid();
		result.initializer = true;
		function_ = &result;
		current_result_ = LowVoid();
		current_result_reference_ = false;
		temp_counter_ = 0;
		block_counter_ = 0;
		break_targets_.clear();
		continue_targets_.clear();
		label_blocks_.clear();
		SelectBlock(AddBlock("entry"));
		for (std::size_t i = 0; i < dynamic_initializers_.size(); ++i)
		{
			Instruction store(Instruction::STORE);
			store.type = LowPtr();
			store.first = LowerValue(dynamic_initializers_[i].expression, LowPtr());
			store.second = Operand(Operand::GLOBAL,
				dynamic_initializers_[i].destination, LowPtr());
			Emit(store);
		}
		Emit(Instruction(Instruction::RETURN_VOID));
		if (stats_)
		{
			++stats_->functions;
			stats_->blocks += result.block_order.size();
		}
		function_ = 0;
		output_.functions.push_back(result);
	}

	bool SymbolForBinding(BindingId binding, SymbolId* symbol) const
	{
		if (binding < function_symbols_.size() &&
			function_symbols_[binding] != kNoLowId)
		{
			*symbol = function_symbols_[binding];
			return true;
		}
		if (binding >= program_.bindings.size()) return false;
		const BindingId canonical = program_.bindings[binding].canonical;
		if (canonical < global_symbols_.size() &&
			global_symbols_[canonical] != kNoLowId)
		{
			*symbol = global_symbols_[canonical];
			return true;
		}
		return false;
	}

	Operand FloatingOperand(const std::string& spelling, const LowType& type)
	{
		return Operand::Floating(output_.literals.Intern(spelling), type);
	}

	static int HexDigit(char value)
	{
		return value >= '0' && value <= '9' ? value - '0' :
			value >= 'a' && value <= 'f' ? value - 'a' + 10 :
			value >= 'A' && value <= 'F' ? value - 'A' + 10 : -1;
	}

	std::vector<unsigned char> DecodeStringLiteral(const std::string& spelling) const
	{
		std::vector<unsigned char> bytes;
		const std::size_t first = spelling.find('"');
		const std::size_t last = spelling.rfind('"');
		if (first == std::string::npos || last <= first)
			throw std::runtime_error("invalid PA15 string literal spelling");
		for (std::size_t i = first + 1; i < last; ++i)
		{
			unsigned value = static_cast<unsigned char>(spelling[i]);
			if (spelling[i] == '\\' && ++i < last)
			{
				const char escape = spelling[i];
				if (escape == 'x')
				{
					value = 0;
					int digit = -1;
					while (i + 1 < last && (digit = HexDigit(spelling[i + 1])) >= 0)
					{
						value = value * 16 + static_cast<unsigned>(digit);
						++i;
					}
				}
				else if (escape >= '0' && escape <= '7')
				{
					value = static_cast<unsigned>(escape - '0');
					for (int count = 1; count < 3 && i + 1 < last &&
						spelling[i + 1] >= '0' && spelling[i + 1] <= '7'; ++count)
						value = value * 8 +
							static_cast<unsigned>(spelling[++i] - '0');
				}
				else value = escape == 'n' ? '\n' : escape == 'r' ? '\r' :
					escape == 't' ? '\t' : escape == 'v' ? '\v' :
					escape == 'b' ? '\b' : escape == 'f' ? '\f' :
					escape == 'a' ? '\a' : static_cast<unsigned char>(escape);
			}
			bytes.push_back(static_cast<unsigned char>(value));
		}
		bytes.push_back(0);
		return bytes;
	}

	SymbolId EnsureStringLiteral(std::uint32_t node)
	{
		if (node >= literal_symbols_.size())
			throw std::logic_error("invalid PA15 literal node");
		if (literal_symbols_[node] != kNoLowId) return literal_symbols_[node];
		const std::string name = "__strlit__" +
			std::to_string(++output_.string_literal_count);
		const SymbolId symbol = static_cast<SymbolId>(output_.symbols.size());
		output_.symbols.push_back(Symbol(Symbol::GLOBAL_SYMBOL, name,
			std::string(), false, true, false));
		output_.symbols.back().definition_emitted = true;
		output_.symbols.back().referenced = true;
		literal_symbols_[node] = symbol;
		const std::vector<unsigned char> bytes = DecodeStringLiteral(
			program_.names.Get(arena_.nodes[node].text));
		Global global;
		global.symbol = symbol;
		global.type = LowObject(bytes.size(), 1);
		global.initializer_kind = Global::STRUCTURED_VALUE;
		for (std::size_t i = 0; i < bytes.size(); ++i)
		{
			Global::DataItem item;
			item.kind = Global::DataItem::INTEGER_ITEM;
			item.type = LowI8();
			item.integer_value = bytes[i];
			global.items.push_back(item);
		}
		output_.globals.push_back(global);
		if (stats_) ++stats_->globals;
		return symbol;
	}

	bool ResolveConstantAddress(std::uint32_t node, SymbolId* symbol,
		std::int64_t* offset)
	{
		const DumpNode& record = arena_.nodes[node];
		const NodeChildren children = Children(node);
		if (record.kind == DUMP_LITERAL && IsArrayType(record.type))
		{
			*symbol = EnsureStringLiteral(node);
			*offset = 0;
			return true;
		}
		if (record.kind == DUMP_ID_EXPRESSION && record.binding != kNoBinding)
		{
			*offset = 0;
			return SymbolForBinding(record.binding, symbol);
		}
		if ((record.kind == DUMP_CAST_EXPRESSION ||
			record.kind == DUMP_UNARY_EXPRESSION) && children.size() == 1)
			return ResolveConstantAddress(children[0], symbol, offset);
		if (record.kind == DUMP_SUBSCRIPT_EXPRESSION && children.size() == 2 &&
			arena_.nodes[children[1]].constant &&
			ResolveConstantAddress(children[0], symbol, offset))
		{
			*offset += arena_.nodes[children[1]].constant_value *
				static_cast<std::int64_t>(program_.SizeOf(record.type));
			return true;
		}
		if (record.kind == DUMP_BINARY_EXPRESSION && children.size() == 2)
		{
			const std::string operation =
				StripOperationPrefix(program_.names.Get(record.text));
			if ((operation == "+" || operation == "-") &&
				arena_.nodes[children[1]].constant &&
				ResolveConstantAddress(children[0], symbol, offset))
			{
				const std::int64_t scale = static_cast<std::int64_t>(
					program_.SizeOf(PointeeType(arena_.nodes[children[0]].type)));
				const std::int64_t delta =
					arena_.nodes[children[1]].constant_value * scale;
				*offset += operation == "+" ? delta : -delta;
				return true;
			}
		}
		if (record.kind == DUMP_CONDITIONAL_EXPRESSION && children.size() == 3 &&
			arena_.nodes[children[0]].constant)
			return ResolveConstantAddress(children[
				arena_.nodes[children[0]].constant_value ? 1 : 2], symbol, offset);
		return false;
	}

	bool RequiresDynamicAddress(std::uint32_t node) const
	{
		const DumpNode& record = arena_.nodes[node];
		const NodeChildren children = Children(node);
		if (record.kind == DUMP_CAST_EXPRESSION && children.size() == 1)
			return RequiresDynamicAddress(children[0]);
		return record.kind == DUMP_UNARY_EXPRESSION && children.size() == 1 &&
			StripOperationPrefix(program_.names.Get(record.text)) == "&" &&
			arena_.nodes[children[0]].kind == DUMP_SUBSCRIPT_EXPRESSION;
	}

	void LowerStructuredGlobal(const DumpNode& record,
		const NodeChildren& variable_children, const TypeRecord& array,
		Global* global)
	{
		global->initializer_kind = Global::STRUCTURED_VALUE;
		const NodeChildren values = variable_children.empty() ? NodeChildren() :
			Children(variable_children[0]);
		if (array.bound == 0 || values.size() > array.bound)
			throw std::runtime_error("invalid PA15 global array bound");
		const LowType element = LowerExpressionType(array.child);
		for (std::size_t i = 0; i < values.size(); ++i)
		{
			const DumpNode& value = arena_.nodes[values[i]];
			Global::DataItem item;
			item.type = element;
			if (IsFloating(element) && value.kind == DUMP_LITERAL)
			{
				item.kind = Global::DataItem::FLOATING_ITEM;
				item.floating_spelling = output_.literals.Intern(
					program_.names.Get(value.text));
			}
			else if (value.constant)
			{
				if (element.kind == LOW_PTR && value.constant_value == 0)
				{
					item.kind = Global::DataItem::ZERO_ITEM;
					item.zero_bytes = program_.SizeOf(array.child);
				}
				else
				{
					item.kind = Global::DataItem::INTEGER_ITEM;
					item.integer_value = value.constant_value;
				}
			}
			else if (ResolveConstantAddress(values[i], &item.symbol, &item.offset))
				item.kind = Global::DataItem::ADDRESS_ITEM;
			else throw std::runtime_error(
				"unsupported PA15 structured global initializer");
			global->items.push_back(item);
		}
		if (values.size() < array.bound)
		{
			Global::DataItem zero;
			zero.kind = Global::DataItem::ZERO_ITEM;
			zero.zero_bytes = static_cast<std::size_t>(array.bound - values.size()) *
				program_.SizeOf(array.child);
			global->items.push_back(zero);
		}
		(void)record;
	}

	std::string UniqueSlotName(const std::string& requested)
	{
		std::string base = requested.empty() ? "__slot" : requested;
		std::size_t& count = slot_name_counts_[base];
		++count;
		std::string candidate = count == 1 ? base :
			base + "__shadow" + std::to_string(count);
		while (assigned_names_[candidate])
		{
			++count;
			candidate = base + "__shadow" + std::to_string(count);
		}
		assigned_names_[candidate] = true;
		used_names_[candidate] = true;
		return candidate;
	}

	std::string GeneratedSlotName(const std::string& prefix)
	{
		while (true)
		{
			const std::string candidate = prefix + "__" +
				std::to_string(++generated_slot_ordinal_);
			if (!used_names_[candidate])
			{
				used_names_[candidate] = true;
				return candidate;
			}
		}
	}

	SlotId EnsureGeneratedSlot(std::uint32_t node, const std::string& prefix,
		const LowType& type)
	{
		if (generated_slots_[node] != kNoLowId)
			return generated_slots_[node];
		generated_slots_[node] = static_cast<SlotId>(function_->slots.size());
		Slot slot;
		slot.name = GeneratedSlotName(prefix);
		slot.type = type;
		function_->slots.push_back(slot);
		return generated_slots_[node];
	}

	void CollectSourceNames(std::uint32_t node)
	{
		std::vector<std::uint32_t> pending(1, node);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const DumpNode& record = arena_.nodes[current];
			if ((record.kind == DUMP_PARAMETER || record.kind == DUMP_VARIABLE) &&
				record.text != 0)
				used_names_[program_.names.Get(record.text)] = true;
			const NodeChildren children = Children(current);
			for (std::size_t i = children.size(); i != 0; --i)
				pending.push_back(children[i - 1]);
		}
	}

	void CollectSlots(std::uint32_t node)
	{
		std::vector<std::uint32_t> pending(1, node);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const DumpNode& record = arena_.nodes[current];
			if ((record.kind == DUMP_PARAMETER || record.kind == DUMP_VARIABLE) &&
				record.binding != kNoBinding)
			{
				if (binding_slots_[record.binding] == kNoLowId)
				{
					std::string requested = record.text == 0 ? std::string() :
						program_.names.Get(record.text);
					if (record.kind == DUMP_PARAMETER && requested.empty())
						requested = parameter_slot_index_ < function_->parameters.size() ?
							function_->parameters[parameter_slot_index_].name : "__param";
					const std::string name = UniqueSlotName(requested);
					binding_slots_[record.binding] =
						static_cast<SlotId>(function_->slots.size());
					Slot slot;
					slot.name = name;
					slot.type = LowerStorageType(record.type);
					function_->slots.push_back(slot);
				}
				if (record.kind == DUMP_PARAMETER) ++parameter_slot_index_;
			}
			const NodeChildren children = Children(current);
			for (std::size_t i = children.size(); i != 0; --i)
				pending.push_back(children[i - 1]);
		}
	}

	Function LowerFunction(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		Function result;
		result.symbol = function_symbols_[record.binding];
		result.entry = program_.names.Get(record.text) == "main";
		FillBoundary(node, &result.parameters, &result.result, &result.variadic);
		function_ = &result;
		current_result_ = result.result;
		const TypeRecord& source_function = program_.types.Get(record.type);
		current_result_reference_ = IsReferenceType(source_function.child);
		temp_counter_ = 0;
		block_counter_ = 0;
		break_targets_.clear();
		continue_targets_.clear();
		label_blocks_.clear();
		used_names_.Clear();
		assigned_names_.Clear();
		slot_name_counts_.Clear();
		generated_slot_ordinal_ = 0;
		parameter_slot_index_ = 0;
		CollectSourceNames(node);
		CollectSlots(node);
		SelectBlock(AddBlock("entry"));

		const NodeChildren children = Children(node);
		std::size_t parameter_index = 0;
		std::uint32_t body = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpNode& child = arena_.nodes[children[i]];
			if (child.kind == DUMP_PARAMETER)
			{
				Instruction store(Instruction::STORE);
				store.type = result.parameters[parameter_index].type;
				store.first = Operand(static_cast<ParameterId>(parameter_index),
					store.type);
				store.second = Operand(binding_slots_[child.binding], store.type);
				Emit(store);
				++parameter_index;
			}
			else if (child.kind == DUMP_COMPOUND_STATEMENT) body = children[i];
		}
		if (body != kNoDumpEdge) LowerStatement(body);
		if (!CurrentBlock().terminated)
		{
			if (result.entry)
			{
				Instruction instruction(Instruction::RETURN_VALUE);
				instruction.type = result.result;
				instruction.first = Operand(0, result.result);
				Emit(instruction);
			}
			else if (result.result.kind == LOW_VOID)
				Emit(Instruction(Instruction::RETURN_VOID));
			else throw std::runtime_error("non-void function has no return");
		}
		if (stats_)
		{
			++stats_->functions;
			stats_->blocks += result.block_order.size();
		}
		function_ = 0;
		current_result_reference_ = false;
		return result;
	}

	Block& CurrentBlock() { return function_->blocks[current_block_]; }

	BlockId AddBlock(const std::string& label)
	{
		if (function_->blocks.size() >= kNoLowId)
			throw std::runtime_error("too many PA15 LowIR blocks");
		const BlockId block = static_cast<BlockId>(function_->blocks.size());
		function_->blocks.push_back(Block(label));
		return block;
	}

	void SelectBlock(BlockId block)
	{
		current_block_ = block;
		if (!function_->blocks[block].selected)
		{
			function_->blocks[block].selected = true;
			function_->block_order.push_back(block);
		}
	}

	std::string NewLabel(const std::string& prefix)
	{
		return prefix + "_" + std::to_string(++block_counter_);
	}

	TempId NewTemp()
	{
		while (true)
		{
			if (temp_counter_ + 1 >= kNoLowId)
				throw std::runtime_error("too many PA15 LowIR temporaries");
			const TempId candidate = static_cast<TempId>(++temp_counter_);
			if (!used_names_["t" + std::to_string(candidate)]) return candidate;
		}
	}

	Operand Temp(const LowType& type)
	{
		return Operand(NewTemp(), type);
	}

	void Emit(const Instruction& instruction)
	{
		if (CurrentBlock().terminated)
			throw std::runtime_error("PA15 attempted to emit after a terminator");
		CurrentBlock().instructions.push_back(instruction);
		if (IsTerminator(instruction)) CurrentBlock().terminated = true;
		if (stats_) ++stats_->instructions;
	}

	Operand StorageFor(BindingId binding, const LowType& type)
	{
		if (stats_) ++stats_->binding_index_probes;
		if (binding < binding_slots_.size() && binding_slots_[binding] != kNoLowId)
			return Operand(binding_slots_[binding], type);
		if (binding < program_.bindings.size())
			binding = program_.bindings[binding].canonical;
		if (binding < global_symbols_.size() && global_symbols_[binding] != kNoLowId)
		{
			output_.symbols[global_symbols_[binding]].referenced = true;
			return Operand(Operand::GLOBAL, global_symbols_[binding], type);
		}
		throw std::runtime_error("PA15 binding has no lowered storage");
	}

	bool BindingIsReference(BindingId binding) const
	{
		return binding < program_.bindings.size() &&
			IsReferenceType(program_.bindings[binding].type);
	}

	Operand LoadStorage(const Operand& storage, const LowType& type)
	{
		const Operand result = Temp(type);
		Instruction load(Instruction::LOAD);
		load.dest = result.id;
		load.type = type;
		load.first = storage;
		Emit(load);
		return result;
	}

	Operand AddressOfStorage(const Operand& storage)
	{
		if (storage.kind == Operand::TEMP)
		{
			if (storage.type.kind != LOW_PTR)
				throw std::logic_error("PA15 indirect storage is not a pointer");
			return storage;
		}
		if (storage.kind == Operand::GLOBAL || storage.kind == Operand::FUNCTION)
			output_.symbols[storage.id].referenced = true;
		const Operand result = Temp(LowPtr());
		Instruction address(Instruction::ADDR);
		address.dest = result.id;
		address.first = storage;
		Emit(address);
		return result;
	}

	Operand DecayAddress(const Operand& address)
	{
		const Operand result = Temp(LowPtr());
		Instruction decay(Instruction::UNARY);
		decay.dest = result.id;
		decay.op = LOW_OP_DECAY;
		decay.type = LowPtr();
		decay.first = address;
		Emit(decay);
		return result;
	}

	Operand IndexAddress(const LowType& element, const Operand& base,
		const Operand& offset, bool array_projection)
	{
		const Operand result = Temp(LowPtr());
		Instruction index(Instruction::INDEX);
		index.dest = result.id;
		index.type = element;
		index.first = base;
		index.second = offset;
		index.indirect = array_projection;
		Emit(index);
		return result;
	}

	Operand LowerArrayPointer(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (IsArrayType(record.type))
		{
			if (record.kind == DUMP_LITERAL)
				return AddressOfStorage(LowerStorage(node));
			return record.kind == DUMP_CONDITIONAL_EXPRESSION ?
				LowerStorage(node) :
				DecayAddress(AddressOfStorage(LowerStorage(node)));
		}
		return LowerValue(node, LowPtr());
	}

	Operand LowerStorage(std::uint32_t node)
	{
		const DumpNode& record = arena_.nodes[node];
		if (record.kind == DUMP_ID_EXPRESSION && record.binding != kNoBinding)
		{
			if (record.binding < function_symbols_.size() &&
				function_symbols_[record.binding] != kNoLowId)
				return Operand(Operand::FUNCTION,
					function_symbols_[record.binding], LowPtr());
			const Operand storage = StorageFor(record.binding,
				LowerStorageType(program_.bindings[record.binding].type));
			return BindingIsReference(record.binding) ?
				LoadStorage(storage, LowPtr()) : storage;
		}
		if (record.kind == DUMP_LITERAL && IsArrayType(record.type))
			return Operand(Operand::GLOBAL, EnsureStringLiteral(node), LowPtr());
		const NodeChildren children = Children(node);
		if (record.kind == DUMP_UNARY_EXPRESSION && children.size() == 1 &&
			StripOperationPrefix(program_.names.Get(record.text)) == "*")
			return LowerValue(children[0], LowPtr());
		if (record.kind == DUMP_UNARY_EXPRESSION && children.size() == 1 &&
			(StripOperationPrefix(program_.names.Get(record.text)) == "++" ||
			 StripOperationPrefix(program_.names.Get(record.text)) == "--"))
			return LowerIncrement(record, children[0], true);
		if (record.kind == DUMP_SUBSCRIPT_EXPRESSION && children.size() == 2)
		{
			const Operand base = LowerArrayPointer(children[0]);
			const Operand offset = LowerValue(children[1]);
			return IndexAddress(LowerExpressionType(record.type), base, offset, true);
		}
		if (record.kind == DUMP_BINARY_EXPRESSION && children.size() == 2 &&
			StripOperationPrefix(program_.names.Get(record.text)) == ",")
		{
			(void)LowerValue(children[0]);
			return LowerStorage(children[1]);
		}
		if (record.kind == DUMP_CONDITIONAL_EXPRESSION &&
			(record.category == VALUE_LVALUE || record.category == VALUE_XVALUE))
			return LowerConditionalAddress(node, children);
		if (record.kind == DUMP_ASSIGNMENT_EXPRESSION)
			return LowerAssignmentCore(record, children, true);
		if (record.kind == DUMP_CALL_EXPRESSION && IsReferenceType(record.type))
			return LowerCall(record, children);
		if (record.kind == DUMP_CAST_EXPRESSION && children.size() == 1 &&
			(record.category == VALUE_LVALUE || record.category == VALUE_XVALUE))
			return LowerStorage(children[0]);
		throw std::runtime_error("expression does not designate scalar storage");
	}

	Operand Convert(Operand value, const LowType& target,
		bool canonicalize_immediate = true)
	{
		if (SameType(value.type, target))
		{
			value.type = target;
			return value;
		}
		if (IsInteger(value.type) && IsInteger(target) &&
			value.type.width == target.width)
		{
			if (value.kind == Operand::INTEGER)
			{
				value.type = target;
				return value;
			}
			const Operand result = Temp(target);
			Instruction copy(Instruction::COPY);
			copy.dest = result.id;
			copy.type = target;
			copy.first = value;
			Emit(copy);
			return result;
		}
		if (canonicalize_immediate && value.kind == Operand::INTEGER &&
			IsInteger(value.type) && IsInteger(target))
		{
			value.type = target;
			return value;
		}
		if ((IsInteger(value.type) && target.kind == LOW_PTR) ||
			(value.type.kind == LOW_PTR && IsInteger(target)))
		{
			const Operand result = Temp(target);
			Instruction copy(Instruction::COPY);
			copy.dest = result.id;
			copy.type = target;
			copy.first = value;
			Emit(copy);
			return result;
		}
		Instruction instruction(Instruction::CONVERT);
		instruction.type = target;
		instruction.source_type = value.type;
		if (IsInteger(value.type) && IsInteger(target))
			instruction.op = target.width < value.type.width ? LOW_OP_TRUNC :
				value.type.is_signed ? LOW_OP_SEXT : LOW_OP_ZEXT;
		else if (IsInteger(value.type) && IsFloating(target))
			instruction.op = value.type.is_signed ? LOW_OP_SITOFP : LOW_OP_UITOFP;
		else if (IsFloating(value.type) && IsInteger(target))
			instruction.op = target.is_signed ? LOW_OP_FPTOSI : LOW_OP_FPTOUI;
		else if (IsFloating(value.type) && IsFloating(target))
			instruction.op = target.width < value.type.width ?
				LOW_OP_FPTRUNC : LOW_OP_FPEXT;
		else throw std::runtime_error("unsupported PA15 scalar conversion");
		const Operand result = Temp(target);
		instruction.dest = result.id;
		instruction.first = value;
		Emit(instruction);
		return result;
	}

	bool IsBooleanType(TypeId type) const
	{
		const TypeRecord* record = &program_.types.Get(type);
		while (record->kind == TYPE_QUALIFIED ||
			record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE)
		{
			type = record->child;
			record = &program_.types.Get(type);
		}
		return record->kind == TYPE_FUNDAMENTAL &&
			record->fundamental == FUND_BOOL;
	}

	Operand LowerCondition(std::uint32_t node)
	{
		Operand value = LowerValue(node);
		if (IsBooleanType(arena_.nodes[node].type) || !IsFloating(value.type))
			return value;
		const Operand result = Temp(LowU8());
		Instruction compare(Instruction::CMP);
		compare.dest = result.id;
		compare.op = LOW_OP_NE;
		compare.type = value.type;
		compare.first = value;
		compare.second = IsFloating(value.type) ? FloatingOperand("0.0", value.type) :
			Operand(0, value.type);
		Emit(compare);
		return result;
	}

	Operand LowerValue(std::uint32_t node, const LowType& expected = LowType())
	{
		if (stats_) ++stats_->lowered_nodes;
		const DumpNode& record = arena_.nodes[node];
		const NodeChildren children = Children(node);
		Operand result;
		if ((record.category == VALUE_LVALUE || record.category == VALUE_XVALUE) &&
			IsArrayType(record.type))
			result = record.kind == DUMP_LITERAL ?
				AddressOfStorage(LowerStorage(node)) :
				record.kind == DUMP_CONDITIONAL_EXPRESSION ?
				LowerStorage(node) :
				DecayAddress(AddressOfStorage(LowerStorage(node)));
		else if (record.kind == DUMP_LITERAL)
		{
			const LowType type = LowerType(record.type);
			if (type.kind == LOW_PTR && record.text != 0 &&
				StripOperationPrefix(program_.names.Get(record.text)) == "nullptr")
			{
				result = Temp(type);
				Instruction copy(Instruction::COPY);
				copy.dest = result.id;
				copy.type = type;
				copy.first = Operand::NullPointer(type);
				Emit(copy);
			}
			else if (IsFloating(type))
				result = FloatingOperand(program_.names.Get(record.text), type);
			else
			{
				if (!record.constant)
					throw std::runtime_error("literal is missing its PA12 constant fact");
				result = Operand(record.constant_value, type);
			}
		}
		else if (record.kind == DUMP_ID_EXPRESSION)
		{
			if (record.binding != kNoBinding && record.binding < function_symbols_.size() &&
				function_symbols_[record.binding] != kNoLowId)
			{
				result = DecayAddress(AddressOfStorage(Operand(Operand::FUNCTION,
					function_symbols_[record.binding], LowPtr())));
			}
			else if (IsFunctionType(record.type))
			{
				result = DecayAddress(LowerStorage(node));
			}
			else
			{
				const LowType type = LowerExpressionType(record.type);
				const Operand storage = LowerStorage(node);
				result = LoadStorage(storage, type);
			}
		}
		else if (record.kind == DUMP_SUBSCRIPT_EXPRESSION)
			result = LoadStorage(LowerStorage(node),
				LowerExpressionType(record.type));
		else if (record.kind == DUMP_SIZEOF_EXPRESSION)
		{
			if (!record.constant)
				throw std::runtime_error("sizeof is missing its PA12 constant fact");
			const LowType type = LowerExpressionType(record.type);
			result = Temp(type);
			Instruction constant(Instruction::CONST);
			constant.dest = result.id;
			constant.type = type;
			constant.first = Operand(record.constant_value, type);
			Emit(constant);
		}
		else if (record.kind == DUMP_BINARY_EXPRESSION)
			result = LowerBinary(node, record, children);
		else if (record.kind == DUMP_ASSIGNMENT_EXPRESSION)
			result = LowerAssignment(record, children);
		else if (record.kind == DUMP_UNARY_EXPRESSION ||
			record.kind == DUMP_POSTFIX_EXPRESSION)
			result = LowerUnary(record, children);
		else if (record.kind == DUMP_CALL_EXPRESSION)
		{
			result = LowerCall(record, children);
			if (IsReferenceType(record.type) &&
				!IsFunctionType(RemoveReference(record.type)))
				result = LoadStorage(result,
					LowerExpressionType(RemoveReference(record.type)));
		}
		else if (record.kind == DUMP_CAST_EXPRESSION)
		{
			if (children.size() != 1) throw std::runtime_error("invalid semantic cast");
			if (LowerExpressionType(record.type).kind == LOW_VOID)
			{
				(void)LowerValue(children[0]);
				result = Operand(0, LowVoid());
			}
			else if (record.category == VALUE_LVALUE || record.category == VALUE_XVALUE)
				result = LoadStorage(LowerStorage(node),
					LowerExpressionType(record.type));
			else result = Convert(LowerValue(children[0]),
				LowerExpressionType(record.type), false);
		}
		else if (record.kind == DUMP_CONDITIONAL_EXPRESSION)
			result = LowerConditional(node, record, children);
		else if (record.kind == DUMP_BRACED_INIT_LIST)
		{
			if (children.empty()) result = Operand(0, LowerType(record.type));
			else if (children.size() == 1) result = LowerValue(children[0],
				LowerExpressionType(record.type));
			else throw std::runtime_error("scalar initializer has excess elements");
		}
		else throw std::runtime_error("semantic expression is outside the active PA15 checkpoint");
		return expected.kind == LOW_INVALID ? result : Convert(result, expected);
	}

	Operand LowerBinary(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children)
	{
		if (children.size() != 2) throw std::runtime_error("invalid semantic binary");
		const std::string op = StripOperationPrefix(program_.names.Get(record.text));
		if (op == "&&" || op == "||")
			return LowerLogical(node, children, op == "&&");
		if (op == ",")
		{
			(void)LowerValue(children[0]);
			return LowerValue(children[1]);
		}
		const bool comparison = op == "==" || op == "!=" || op == "<" ||
			op == "<=" || op == ">" || op == ">=";
		const bool left_pointer = IsPointerLikeType(arena_.nodes[children[0]].type);
		const bool right_pointer = IsPointerLikeType(arena_.nodes[children[1]].type);
		if ((op == "+" || op == "-") && left_pointer && !right_pointer)
			return LowerPointerOffset(children[0], children[1], op == "-");
		if (op == "+" && !left_pointer && right_pointer)
			return LowerPointerOffset(children[1], children[0], false);
		if (op == "-" && left_pointer && right_pointer)
			return LowerPointerDifference(children[0], children[1]);
		if (record.operand_type == kNoType &&
			!(comparison && (left_pointer || right_pointer)))
			throw std::runtime_error("binary expression is missing its PA12 operand type");
		const LowType operand_type = record.operand_type == kNoType ?
			LowPtr() : LowerExpressionType(record.operand_type);
		Operand left = LowerValue(children[0]);
		Operand right = LowerValue(children[1]);
		const bool canonical_pointer_difference_compare = comparison &&
			arena_.nodes[children[0]].kind == DUMP_BINARY_EXPRESSION &&
			arena_.nodes[children[0]].operand_type == kNoType &&
			LowerExpressionType(arena_.nodes[children[0]].type).kind == LOW_I64;
		const bool canonicalize_immediates =
			(!comparison && (op == "+" || op == "-")) ||
			canonical_pointer_difference_compare;
		if (comparison && operand_type.kind == LOW_PTR &&
			left.kind == Operand::INTEGER && left.integer_value == 0)
			left.type = operand_type;
		else left = Convert(left, operand_type, canonicalize_immediates);
		if (comparison && operand_type.kind == LOW_PTR &&
			right.kind == Operand::INTEGER && right.integer_value == 0)
			right.type = operand_type;
		else right = Convert(right, operand_type, canonicalize_immediates);
		const LowType result_type = LowerType(record.type);
		const Operand result = Temp(result_type);
		Instruction instruction(comparison ? Instruction::CMP : Instruction::BINARY);
		instruction.dest = result.id;
		instruction.type = operand_type;
		instruction.first = left;
		instruction.second = right;
		if (comparison)
		{
			instruction.op = op == "==" ? LOW_OP_EQ : op == "!=" ? LOW_OP_NE :
				op == "<" ? (operand_type.is_signed ? LOW_OP_LT : LOW_OP_ULT) :
				op == "<=" ? (operand_type.is_signed ? LOW_OP_LE : LOW_OP_ULE) :
				op == ">" ? (operand_type.is_signed ? LOW_OP_GT : LOW_OP_UGT) :
				(operand_type.is_signed ? LOW_OP_GE : LOW_OP_UGE);
		}
		else
		{
			instruction.op = op == "+" ? LOW_OP_ADD : op == "-" ? LOW_OP_SUB :
				op == "*" ? LOW_OP_MUL : op == "/" ?
					(operand_type.is_signed || IsFloating(operand_type) ?
						LOW_OP_DIV : LOW_OP_UDIV) :
				op == "%" ? (operand_type.is_signed ? LOW_OP_MOD : LOW_OP_UMOD) :
				op == "&" ? LOW_OP_AND : op == "|" ? LOW_OP_OR :
				op == "^" ? LOW_OP_XOR : op == "<<" ? LOW_OP_SHL : op == ">>" ?
					(operand_type.is_signed ? LOW_OP_SHR : LOW_OP_USHR) : LOW_OP_NONE;
			if (instruction.op == LOW_OP_NONE)
				throw std::runtime_error("unsupported binary operator");
		}
		Emit(instruction);
		return result;
	}

	Operand LowerLogical(std::uint32_t node, const NodeChildren& children,
		bool conjunction)
	{
		const DumpNode& left_record = arena_.nodes[children[0]];
		if (left_record.constant)
		{
			const bool left_truth = left_record.constant_value != 0;
			if ((conjunction && !left_truth) || (!conjunction && left_truth))
				return Operand(left_truth ? 1 : 0, LowU8());
			return LowerCondition(children[1]);
		}
		const Operand slot(EnsureGeneratedSlot(node,
			conjunction ? "land" : "lor", LowI64()), LowI64());
		const char* prefix = conjunction ? "land" : "lor";
		const BlockId rhs_block = AddBlock(NewLabel(std::string(prefix) + "_rhs"));
		const BlockId short_block = AddBlock(NewLabel(std::string(prefix) + "_short"));
		const BlockId end_block = AddBlock(NewLabel(std::string(prefix) + "_end"));
		const Operand left = LowerCondition(children[0]);
		Instruction branch(Instruction::BRANCH);
		branch.first = left;
		branch.target = conjunction ? rhs_block : short_block;
		branch.alternate = conjunction ? short_block : rhs_block;
		Emit(branch);
		SelectBlock(rhs_block);
		const Operand right = LowerCondition(children[1]);
		const Operand truth = Temp(LowI64());
		Instruction compare(Instruction::CMP);
		compare.dest = truth.id;
		compare.op = LOW_OP_NE;
		compare.type = LowI64();
		compare.first = right;
		compare.second = Operand(0, LowI64());
		Emit(compare);
		Instruction rhs_store(Instruction::STORE);
		rhs_store.type = LowI64();
		rhs_store.first = truth;
		rhs_store.second = slot;
		Emit(rhs_store);
		Instruction rhs_jump(Instruction::JUMP);
		rhs_jump.target = end_block;
		Emit(rhs_jump);
		SelectBlock(short_block);
		Instruction short_store(Instruction::STORE);
		short_store.type = LowI64();
		short_store.first = Operand(conjunction ? 0 : 1, LowI64());
		short_store.second = slot;
		Emit(short_store);
		Instruction short_jump(Instruction::JUMP);
		short_jump.target = end_block;
		Emit(short_jump);
		SelectBlock(end_block);
		const Operand result = Temp(LowU8());
		Instruction load(Instruction::LOAD);
		load.dest = result.id;
		load.type = LowI64();
		load.first = slot;
		Emit(load);
		return result;
	}

	Operand LowerPointerOffset(std::uint32_t base_node,
		std::uint32_t offset_node, bool subtract)
	{
		return ApplyPointerOffset(LowerArrayPointer(base_node),
			LowerValue(offset_node), PointeeType(arena_.nodes[base_node].type),
			subtract);
	}

	Operand LowerPointerDifference(std::uint32_t left_node,
		std::uint32_t right_node)
	{
		const Operand left = LowerArrayPointer(left_node);
		const Operand right = LowerArrayPointer(right_node);
		const Operand bytes = Temp(LowI64());
		Instruction subtract(Instruction::BINARY);
		subtract.dest = bytes.id;
		subtract.op = LOW_OP_SUB;
		subtract.type = LowPtr();
		subtract.first = left;
		subtract.second = right;
		Emit(subtract);
		const Operand result = Temp(LowI64());
		Instruction divide(Instruction::BINARY);
		divide.dest = result.id;
		divide.op = LOW_OP_DIV;
		divide.type = LowI64();
		divide.first = bytes;
		divide.second = Operand(static_cast<std::int64_t>(
			program_.SizeOf(PointeeType(arena_.nodes[left_node].type))), LowI64());
		Emit(divide);
		return result;
	}

	Operand ApplyPointerOffset(const Operand& base, const Operand& raw_offset,
		TypeId element_type, bool subtract)
	{
		const Operand offset = Convert(raw_offset, LowI64());
		const std::size_t element_size = program_.SizeOf(element_type);
		if (element_size == 1 && !subtract)
			return IndexAddress(LowI8(), base, offset, false);
		const Operand scaled = Temp(LowI64());
		Instruction multiply(Instruction::BINARY);
		multiply.dest = scaled.id;
		multiply.op = LOW_OP_MUL;
		multiply.type = LowI64();
		multiply.first = offset;
		multiply.second = Operand(static_cast<std::int64_t>(
			element_size), LowI64());
		Emit(multiply);
		Operand displacement = scaled;
		if (subtract)
		{
			displacement = Temp(LowI64());
			Instruction negate(Instruction::BINARY);
			negate.dest = displacement.id;
			negate.op = LOW_OP_SUB;
			negate.type = LowI64();
			negate.first = Operand(0, LowI64());
			negate.second = scaled;
			Emit(negate);
		}
		return IndexAddress(LowI8(), base, displacement, false);
	}

	Operand LowerAssignment(const DumpNode& record,
		const NodeChildren& children)
	{
		return LowerAssignmentCore(record, children, false);
	}

	Operand LowerAssignmentCore(const DumpNode& record,
		const NodeChildren& children, bool return_storage)
	{
		if (children.size() != 2) throw std::runtime_error("invalid semantic assignment");
		const std::string op = StripOperationPrefix(program_.names.Get(record.text));
		const LowType type = LowerExpressionType(record.type);
		Operand storage;
		Operand value;
		if (op == "=")
		{
			value = Convert(LowerValue(children[1]), type, false);
			storage = LowerStorage(children[0]);
		}
		else if ((op == "+=" || op == "-=") &&
			IsPointerLikeType(arena_.nodes[children[0]].type))
		{
			storage = LowerStorage(children[0]);
			const Operand left = LoadStorage(storage, LowPtr());
			value = ApplyPointerOffset(left, LowerValue(children[1]),
				PointeeType(arena_.nodes[children[0]].type), op == "-=");
		}
		else
		{
			storage = LowerStorage(children[0]);
			Operand left = Temp(type);
			Instruction load(Instruction::LOAD);
			load.dest = left.id;
			load.type = type;
			load.first = storage;
			Emit(load);
			if (record.operand_type == kNoType)
				throw std::runtime_error(
					"compound assignment is missing its PA12 operand type");
			const LowType operation_type = LowerType(record.operand_type);
			left = Convert(left, operation_type, false);
			const Operand right = Convert(LowerValue(children[1]), operation_type, false);
			value = Temp(operation_type);
			Instruction binary(Instruction::BINARY);
			binary.dest = value.id;
			binary.type = operation_type;
			binary.first = left;
			binary.second = right;
			binary.op = op == "+=" ? LOW_OP_ADD : op == "-=" ? LOW_OP_SUB :
				op == "*=" ? LOW_OP_MUL : op == "/=" ?
					(operation_type.is_signed || IsFloating(operation_type) ?
						LOW_OP_DIV : LOW_OP_UDIV) :
				op == "%=" ? (operation_type.is_signed ? LOW_OP_MOD : LOW_OP_UMOD) :
				op == "&=" ? LOW_OP_AND : op == "|=" ? LOW_OP_OR :
				op == "^=" ? LOW_OP_XOR : op == "<<=" ? LOW_OP_SHL : op == ">>=" ?
					(operation_type.is_signed ? LOW_OP_SHR : LOW_OP_USHR) : LOW_OP_NONE;
			if (binary.op == LOW_OP_NONE)
				throw std::runtime_error("unsupported PA15 compound assignment");
			Emit(binary);
			value = Convert(value, type, false);
		}
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = value;
		store.second = storage;
		Emit(store);
		return return_storage ? storage : value;
	}

	Operand LowerUnary(const DumpNode& record,
		const NodeChildren& children)
	{
		if (children.size() != 1) throw std::runtime_error("invalid semantic unary");
		const std::string op = StripOperationPrefix(program_.names.Get(record.text));
		if (op == "&") return AddressOfStorage(LowerStorage(children[0]));
		if (op == "*")
			return LoadStorage(LowerValue(children[0], LowPtr()),
				LowerExpressionType(record.type));
		if (op == "++" || op == "--")
			return LowerIncrement(record, children[0], false);
		if (op == "!")
		{
			const Operand value = LowerValue(children[0]);
			const Operand result = Temp(LowU8());
			Instruction compare(Instruction::CMP);
			compare.dest = result.id;
			compare.op = LOW_OP_EQ;
			compare.type = value.type;
			compare.first = value;
			compare.second = IsFloating(value.type) ? FloatingOperand("0.0", value.type) :
				Operand(0, value.type);
			Emit(compare);
			return result;
		}
		const LowType type = LowerExpressionType(record.type);
		Operand value = LowerValue(children[0], type);
		if (op == "+") return value;
		const Operand result = Temp(type);
		Instruction instruction(Instruction::UNARY);
		instruction.dest = result.id;
		instruction.op = op == "-" ? LOW_OP_NEG :
			op == "~" ? LOW_OP_BITNOT : LOW_OP_NONE;
		if (instruction.op == LOW_OP_NONE)
			throw std::runtime_error("increment/address unary lowering is outside the active checkpoint");
		instruction.type = type;
		instruction.first = value;
		Emit(instruction);
		return result;
	}

	Operand LowerIncrement(const DumpNode& record, std::uint32_t operand_node,
		bool return_storage)
	{
		const std::string op = StripOperationPrefix(program_.names.Get(record.text));
		const Operand storage = LowerStorage(operand_node);
		const LowType type = LowerExpressionType(arena_.nodes[operand_node].type);
		const Operand old_value = LoadStorage(storage, type);
		Operand new_value;
		if (IsPointerLikeType(arena_.nodes[operand_node].type))
			new_value = ApplyPointerOffset(old_value, Operand(1, LowI32()),
				PointeeType(arena_.nodes[operand_node].type), op == "--");
		else
		{
			new_value = Temp(type);
			Instruction binary(Instruction::BINARY);
			binary.dest = new_value.id;
			binary.op = op == "++" ? LOW_OP_ADD : LOW_OP_SUB;
			binary.type = type;
			binary.first = old_value;
			binary.second = Operand(1, type);
			Emit(binary);
		}
		Instruction store(Instruction::STORE);
		store.type = type;
		store.first = new_value;
		store.second = storage;
		Emit(store);
		if (return_storage) return storage;
		return record.kind == DUMP_POSTFIX_EXPRESSION ? old_value : new_value;
	}

	Operand LowerCall(const DumpNode& record,
		const NodeChildren& children)
	{
		if (children.empty()) throw std::runtime_error("semantic call has no callee");
		const DumpNode& callee = arena_.nodes[children[0]];
		if (stats_) ++stats_->binding_index_probes;
		const bool direct = callee.kind == DUMP_CALLEE &&
			callee.binding != kNoBinding &&
			callee.binding < function_symbols_.size() &&
			function_symbols_[callee.binding] != kNoLowId;
		TypeId function_type_id = callee.type;
		if (!direct)
		{
			function_type_id = ExpressionObjectType(function_type_id);
			const TypeRecord& callable = program_.types.Get(function_type_id);
			if (callable.kind == TYPE_POINTER)
				function_type_id = callable.child;
		}
		const TypeRecord& function_type = program_.types.Get(function_type_id);
		if (function_type.kind != TYPE_FUNCTION)
			throw std::runtime_error("invalid PA15 indirect callee type");
		const TypeId* parameters = program_.types.Parameters(function_type_id);
		Instruction call(Instruction::CALL);
		CallArguments arguments;
		CallArgumentFlags argument_references;
		call.type = LowerType(record.type);
		call.indirect = !direct;
		if (direct)
		{
			output_.symbols[function_symbols_[callee.binding]].referenced = true;
			call.first = Operand(Operand::FUNCTION,
				function_symbols_[callee.binding], LowPtr());
		}
		for (std::size_t i = 1; i < children.size(); ++i)
		{
			const bool reference = i - 1 < function_type.parameter_count &&
				IsReferenceType(parameters[i - 1]);
			argument_references.Push(reference ? 1 : 0);
			if (reference)
			{
				const DumpNode& argument = arena_.nodes[children[i]];
				if (argument.category == VALUE_LVALUE ||
					argument.category == VALUE_XVALUE)
					arguments.Push(AddressOfStorage(LowerStorage(children[i])));
				else
				{
					const LowType type = LowerExpressionType(parameters[i - 1]);
					const Operand slot(EnsureGeneratedSlot(children[i], "refarg", type),
						type);
					Instruction store(Instruction::STORE);
					store.type = type;
					store.first = Convert(LowerValue(children[i]), type);
					store.second = slot;
					Emit(store);
					arguments.Push(AddressOfStorage(slot));
				}
			}
			else
			{
				LowType expected = i - 1 < function_type.parameter_count ?
					LowerType(parameters[i - 1]) :
					LowerExpressionType(arena_.nodes[children[i]].type);
				if (i - 1 >= function_type.parameter_count)
				{
					if (expected.kind == LOW_F32) expected = LowF64();
					else if (IsInteger(expected) && expected.width < 32)
						expected = LowI32();
				}
				arguments.Push(Convert(LowerValue(children[i]), expected));
			}
		}
		if (!direct) call.first = LowerValue(children[0], LowPtr());
		AttachCallArguments(&call, arguments, argument_references);
		if (call.type.kind == LOW_VOID)
		{
			Emit(call);
			return Operand(0, LowVoid());
		}
		const Operand result = Temp(call.type);
		call.dest = result.id;
		Emit(call);
		return result;
	}

	void AttachCallArguments(Instruction* call, const CallArguments& arguments,
		const CallArgumentFlags& references)
	{
		if (arguments.size() != references.size())
			throw std::logic_error("PA15 call argument fact mismatch");
		if (arguments.empty()) return;
		if (arguments.size() >= kNoLowId ||
			output_.call_arguments.size() > kNoLowId - arguments.size() ||
			output_.call_arguments.size() !=
				output_.call_argument_references.size())
			throw std::runtime_error("too many PA15 call arguments");
		call->extra_first = static_cast<std::uint32_t>(
			output_.call_arguments.size());
		call->extra_count = static_cast<std::uint32_t>(arguments.size());
		for (std::size_t i = 0; i < arguments.size(); ++i)
		{
			output_.call_arguments.push_back(arguments[i]);
			output_.call_argument_references.push_back(references[i]);
		}
	}

	Operand LowerConditional(std::uint32_t node, const DumpNode& record,
		const NodeChildren& children)
	{
		if (children.size() != 3) throw std::runtime_error("invalid semantic conditional");
		const LowType type = LowerExpressionType(record.type);
		if (type.kind == LOW_VOID)
			return LowerDiscardedConditional(children);
		const Operand slot(EnsureGeneratedSlot(node, "cond", type), type);
		const BlockId then_block = AddBlock(NewLabel("cond_then"));
		const BlockId else_block = AddBlock(NewLabel("cond_else"));
		const BlockId end_block = AddBlock(NewLabel("cond_end"));
		const Operand condition = LowerCondition(children[0]);
		Instruction branch(Instruction::BRANCH);
		branch.first = condition;
		branch.target = then_block;
		branch.alternate = else_block;
		Emit(branch);
		SelectBlock(then_block);
		Instruction yes_store(Instruction::STORE);
		yes_store.type = type;
		yes_store.first = LowerValue(children[1], type);
		yes_store.second = slot;
		Emit(yes_store);
		Instruction yes_jump(Instruction::JUMP);
		yes_jump.target = end_block;
		Emit(yes_jump);
		SelectBlock(else_block);
		Instruction no_store(Instruction::STORE);
		no_store.type = type;
		no_store.first = LowerValue(children[2], type);
		no_store.second = slot;
		Emit(no_store);
		Instruction no_jump(Instruction::JUMP);
		no_jump.target = end_block;
		Emit(no_jump);
		SelectBlock(end_block);
		const Operand result = Temp(type);
		Instruction load(Instruction::LOAD);
		load.dest = result.id;
		load.type = type;
		load.first = slot;
		Emit(load);
		return result;
	}

	Operand LowerDiscardedConditional(const NodeChildren& children)
	{
		const BlockId then_block = AddBlock(NewLabel("discard_cond_then"));
		const BlockId else_block = AddBlock(NewLabel("discard_cond_else"));
		const BlockId end_block = AddBlock(NewLabel("discard_cond_end"));
		EmitBranch(LowerCondition(children[0]), then_block, else_block);
		SelectBlock(then_block);
		(void)LowerValue(children[1]);
		EmitJump(end_block);
		SelectBlock(else_block);
		(void)LowerValue(children[2]);
		EmitJump(end_block);
		SelectBlock(end_block);
		return Operand(0, LowVoid());
	}

	Operand LowerConditionalAddress(std::uint32_t node,
		const NodeChildren& children)
	{
		if (children.size() != 3)
			throw std::runtime_error("invalid semantic address conditional");
		const Operand slot(EnsureGeneratedSlot(node, "condaddr", LowPtr()),
			LowPtr());
		const BlockId then_block = AddBlock(NewLabel("condaddr_then"));
		const BlockId else_block = AddBlock(NewLabel("condaddr_else"));
		const BlockId end_block = AddBlock(NewLabel("condaddr_end"));
		const Operand condition = LowerCondition(children[0]);
		Instruction branch(Instruction::BRANCH);
		branch.first = condition;
		branch.target = then_block;
		branch.alternate = else_block;
		Emit(branch);
		SelectBlock(then_block);
		Instruction yes_store(Instruction::STORE);
		yes_store.type = LowPtr();
		yes_store.first = AddressOfStorage(LowerStorage(children[1]));
		yes_store.second = slot;
		Emit(yes_store);
		Instruction yes_jump(Instruction::JUMP);
		yes_jump.target = end_block;
		Emit(yes_jump);
		SelectBlock(else_block);
		Instruction no_store(Instruction::STORE);
		no_store.type = LowPtr();
		no_store.first = AddressOfStorage(LowerStorage(children[2]));
		no_store.second = slot;
		Emit(no_store);
		Instruction no_jump(Instruction::JUMP);
		no_jump.target = end_block;
		Emit(no_jump);
		SelectBlock(end_block);
		return LoadStorage(slot, LowPtr());
	}

	void PushStatementNode(std::uint32_t node)
	{
		StatementTask task(STATEMENT_NODE);
		task.node = node;
		statement_tasks_.push_back(task);
	}

	void PushStatementSequence(std::uint32_t edge,
		StatementTaskKind kind = STATEMENT_SEQUENCE)
	{
		if (edge == kNoDumpEdge) return;
		StatementTask task(kind);
		task.node = edge;
		statement_tasks_.push_back(task);
	}

	void LowerStatement(std::uint32_t node)
	{
		if (!statement_tasks_.empty())
			throw std::logic_error("nested PA15 statement scheduler");
		PushStatementNode(node);
		while (!statement_tasks_.empty())
		{
			const StatementTask task = statement_tasks_.back();
			statement_tasks_.pop_back();
			RunStatementTask(task);
		}
	}

	void RunStatementTask(const StatementTask& task)
	{
		if (task.kind == STATEMENT_NODE)
		{
			LowerStatementNode(task.node);
			return;
		}
		if (task.kind == STATEMENT_SEQUENCE)
		{
			const std::uint32_t child = arena_.edges[task.node].child;
			const DumpKind child_kind = arena_.nodes[child].kind;
			if (CurrentBlock().terminated && child_kind != DUMP_CASE_STATEMENT &&
				child_kind != DUMP_DEFAULT_STATEMENT &&
				child_kind != DUMP_LABELED_STATEMENT)
				return;
			PushStatementSequence(arena_.edges[task.node].next);
			PushStatementNode(child);
			return;
		}
		if (task.kind == STATEMENT_FOR_COMPONENTS)
		{
			const std::uint32_t child = arena_.edges[task.node].child;
			PushStatementSequence(arena_.edges[task.node].next,
				STATEMENT_FOR_COMPONENTS);
			const DumpKind child_kind = arena_.nodes[child].kind;
			if (child_kind == DUMP_SIMPLE_DECLARATION ||
				child_kind == DUMP_VARIABLE)
				PushStatementNode(child);
			else (void)LowerValue(child);
			return;
		}
		if (task.kind == STATEMENT_IF_AFTER_THEN)
		{
			const bool then_terminated = CurrentBlock().terminated;
			if (!then_terminated) EmitJump(task.second);
			SelectBlock(task.first);
			StatementTask after(STATEMENT_IF_AFTER_ELSE);
			after.first = task.second;
			after.flag = then_terminated;
			statement_tasks_.push_back(after);
			if (task.node != kNoDumpEdge) PushStatementNode(task.node);
			return;
		}
		if (task.kind == STATEMENT_IF_AFTER_ELSE)
		{
			const bool else_terminated = CurrentBlock().terminated;
			if (!else_terminated) EmitJump(task.first);
			if (!task.flag || !else_terminated) SelectBlock(task.first);
			return;
		}
		if (task.kind == STATEMENT_LOOP_AFTER_BODY)
		{
			PopLoopTargets();
			if (!CurrentBlock().terminated) EmitJump(task.first);
			SelectBlock(task.second);
			return;
		}
		if (task.kind == STATEMENT_DO_AFTER_BODY)
		{
			PopLoopTargets();
			if (!CurrentBlock().terminated) EmitJump(task.second);
			SelectBlock(task.second);
			EmitBranch(LowerControlCondition(task.node), task.first, task.third);
			SelectBlock(task.third);
			return;
		}
		if (task.kind == STATEMENT_FOR_AFTER_INIT)
		{
			StartForLoop(task.node, task.auxiliary, task.last);
			return;
		}
		if (task.kind == STATEMENT_FOR_AFTER_BODY)
		{
			PopLoopTargets();
			if (!CurrentBlock().terminated) EmitJump(task.first);
			SelectBlock(task.first);
			StatementTask after(STATEMENT_FOR_AFTER_ITERATION);
			after.first = task.second;
			after.second = task.third;
			statement_tasks_.push_back(after);
			if (task.node != kNoDumpEdge) PushStatementNode(task.node);
			return;
		}
		if (task.kind == STATEMENT_FOR_AFTER_ITERATION)
		{
			if (!CurrentBlock().terminated) EmitJump(task.first);
			SelectBlock(task.second);
			return;
		}
		if (task.kind == STATEMENT_SWITCH_AFTER_BODY)
		{
			if (break_targets_.empty())
				throw std::logic_error("missing PA15 switch target");
			break_targets_.pop_back();
			if (!CurrentBlock().terminated) EmitJump(task.first);
			SelectBlock(task.first);
			return;
		}
		throw std::logic_error("invalid PA15 statement task");
	}

	void PopLoopTargets()
	{
		if (break_targets_.empty() || continue_targets_.empty())
			throw std::logic_error("missing PA15 loop target");
		continue_targets_.pop_back();
		break_targets_.pop_back();
	}

	void LowerStatementNode(std::uint32_t node)
	{
		if (stats_) ++stats_->lowered_nodes;
		const DumpNode& record = arena_.nodes[node];
		const NodeChildren children = Children(node);
		if (record.kind == DUMP_TYPE_ALIAS) return;
		if (record.kind == DUMP_COMPOUND_STATEMENT ||
			record.kind == DUMP_SIMPLE_DECLARATION ||
			record.kind == DUMP_CONDITION_DECLARATION ||
			record.kind == DUMP_THEN || record.kind == DUMP_ELSE)
		{
			PushStatementSequence(record.first_edge);
			return;
		}
		if (record.kind == DUMP_VARIABLE)
		{
			if (!children.empty())
			{
				if (IsArrayType(record.type))
				{
					LowerArrayInitializer(record, children);
					return;
				}
				const LowType type = LowerStorageType(record.type);
				Instruction store(Instruction::STORE);
				store.type = type;
				store.first = IsReferenceType(record.type) ?
					AddressOfStorage(LowerStorage(children[0])) :
					Convert(LowerValue(children[0]), type, false);
				store.second = StorageFor(record.binding, type);
				Emit(store);
			}
			return;
		}
		if (record.kind == DUMP_RETURN_STATEMENT)
		{
			if (children.empty()) Emit(Instruction(Instruction::RETURN_VOID));
			else if (current_result_.kind == LOW_VOID)
			{
				(void)LowerValue(children[0]);
				Emit(Instruction(Instruction::RETURN_VOID));
			}
			else
			{
				Instruction instruction(Instruction::RETURN_VALUE);
				instruction.type = current_result_;
				if (current_result_reference_)
					instruction.first = AddressOfStorage(LowerStorage(children[0]));
				else
				{
					const Operand value = LowerValue(children[0]);
					const bool preserve_unsigned_conversion =
						value.kind == Operand::INTEGER && IsInteger(value.type) &&
						IsInteger(current_result_) && value.type.is_signed &&
						!current_result_.is_signed &&
						value.type.width < current_result_.width;
					instruction.first = Convert(value, current_result_,
						!preserve_unsigned_conversion);
				}
				Emit(instruction);
			}
			return;
		}
		if (record.kind == DUMP_EXPRESSION_STATEMENT)
		{
			if (!children.empty()) (void)LowerValue(children[0]);
			return;
		}
		if (record.kind == DUMP_IF_STATEMENT) { LowerIf(children); return; }
		if (record.kind == DUMP_WHILE_STATEMENT) { LowerWhile(children); return; }
		if (record.kind == DUMP_DO_STATEMENT) { LowerDo(children); return; }
		if (record.kind == DUMP_FOR_STATEMENT) { LowerFor(children); return; }
		if (record.kind == DUMP_SWITCH_STATEMENT) { LowerSwitch(children); return; }
		if (record.kind == DUMP_CASE_STATEMENT ||
			record.kind == DUMP_DEFAULT_STATEMENT)
		{
			if (node >= switch_case_blocks_.size() ||
				switch_case_blocks_[node] == kNoLowId)
				throw std::runtime_error("PA15 case has no switch target");
			const BlockId target = switch_case_blocks_[node];
			if (!CurrentBlock().terminated) EmitJump(target);
			SelectBlock(target);
			std::uint32_t edge = record.first_edge;
			if (record.kind == DUMP_CASE_STATEMENT && edge != kNoDumpEdge)
				edge = arena_.edges[edge].next;
			PushStatementSequence(edge);
			return;
		}
		if (record.kind == DUMP_LABELED_STATEMENT)
		{
			const BlockId target = LabelBlock(record.text);
			if (!CurrentBlock().terminated) EmitJump(target);
			SelectBlock(target);
			PushStatementSequence(record.first_edge);
			return;
		}
		if (record.kind == DUMP_GOTO_STATEMENT)
		{
			EmitJump(LabelBlock(record.text));
			return;
		}
		if (record.kind == DUMP_FOR_INIT_STATEMENT ||
			record.kind == DUMP_ITERATION)
		{
			PushStatementSequence(record.first_edge, STATEMENT_FOR_COMPONENTS);
			return;
		}
		if (record.kind == DUMP_BREAK_STATEMENT)
		{
			if (break_targets_.empty())
				throw std::runtime_error("PA15 break has no target");
			EmitJump(break_targets_.back());
			return;
		}
		if (record.kind == DUMP_CONTINUE_STATEMENT)
		{
			if (continue_targets_.empty())
				throw std::runtime_error("PA15 continue has no target");
			EmitJump(continue_targets_.back());
			return;
		}
		throw std::runtime_error("statement is outside the active PA15 checkpoint");
	}

	__attribute__((noinline)) void LowerArrayInitializer(const DumpNode& record,
		const NodeChildren& variable_children)
	{
		if (variable_children.size() != 1)
			throw std::runtime_error("invalid PA15 array initializer");
		const NodeChildren values = Children(variable_children[0]);
		const TypeRecord& array = program_.types.Get(ExpressionObjectType(record.type));
		if (array.kind != TYPE_ARRAY || array.bound == 0 ||
			values.size() > array.bound)
			throw std::runtime_error("invalid PA15 bounded array initializer");
		const LowType object_type = LowerStorageType(record.type);
		const Operand storage = StorageFor(record.binding, object_type);
		const Operand base = AddressOfStorage(storage);
		const LowType element = LowerExpressionType(array.child);
		const std::size_t element_size = program_.SizeOf(array.child);
		for (std::size_t i = 0; i < static_cast<std::size_t>(array.bound); ++i)
		{
			Operand destination = base;
			if (i != 0)
				destination = IndexAddress(LowI8(), base,
					Operand(static_cast<std::int64_t>(i * element_size), LowI64()),
					false);
			Instruction store(Instruction::STORE);
			store.type = element;
			store.first = i < values.size() ?
				Convert(LowerValue(values[i]), element) : Operand(0, element);
			store.second = destination;
			Emit(store);
		}
	}

	__attribute__((noinline)) Operand LowerControlCondition(
		std::uint32_t condition_node)
	{
		const NodeChildren condition_children = Children(condition_node);
		if (condition_children.size() != 1)
			throw std::runtime_error("invalid PA15 control condition");
		const std::uint32_t child = condition_children[0];
		if (arena_.nodes[child].kind != DUMP_CONDITION_DECLARATION)
			return LowerCondition(child);
		const NodeChildren declaration_children = Children(child);
		if (declaration_children.size() != 1 ||
			arena_.nodes[declaration_children[0]].kind != DUMP_VARIABLE)
			throw std::runtime_error("invalid PA15 condition declaration");
		const DumpNode& variable = arena_.nodes[declaration_children[0]];
		if (stats_) ++stats_->lowered_nodes;
		LowerStatementNode(declaration_children[0]);
		Operand value = LoadStorage(StorageFor(variable.binding,
			LowerStorageType(variable.type)), LowerExpressionType(variable.type));
		if (IsFloating(value.type))
		{
			const Operand truth = Temp(LowU8());
			Instruction compare(Instruction::CMP);
			compare.dest = truth.id;
			compare.op = LOW_OP_NE;
			compare.type = value.type;
			compare.first = value;
			compare.second = FloatingOperand("0.0", value.type);
			Emit(compare);
			return truth;
		}
		return value;
	}

	__attribute__((noinline)) Operand LowerSwitchCondition(
		std::uint32_t condition_node)
	{
		const NodeChildren condition_children = Children(condition_node);
		if (condition_children.size() != 1)
			throw std::runtime_error("invalid PA15 switch condition");
		const std::uint32_t child = condition_children[0];
		if (arena_.nodes[child].kind != DUMP_CONDITION_DECLARATION)
			return LowerValue(child);
		const NodeChildren declaration_children = Children(child);
		if (declaration_children.size() != 1 ||
			arena_.nodes[declaration_children[0]].kind != DUMP_VARIABLE)
			throw std::runtime_error("invalid PA15 switch declaration");
		const DumpNode& variable = arena_.nodes[declaration_children[0]];
		if (stats_) ++stats_->lowered_nodes;
		LowerStatementNode(declaration_children[0]);
		return LoadStorage(StorageFor(variable.binding,
			LowerStorageType(variable.type)), LowerExpressionType(variable.type));
	}

	std::uint32_t FindChildKind(const NodeChildren& children, DumpKind kind) const
	{
		for (std::size_t i = 0; i < children.size(); ++i)
			if (arena_.nodes[children[i]].kind == kind) return children[i];
		return kNoDumpEdge;
	}

	std::uint32_t FindLoopBody(const NodeChildren& children) const
	{
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const DumpKind kind = arena_.nodes[children[i]].kind;
			if (kind != DUMP_CONDITION && kind != DUMP_FOR_INIT_STATEMENT &&
				kind != DUMP_ITERATION)
				return children[i];
		}
		return kNoDumpEdge;
	}

	void EmitJump(BlockId target)
	{
		Instruction jump(Instruction::JUMP);
		jump.target = target;
		Emit(jump);
	}

	BlockId LabelBlock(NameId name)
	{
		const std::unordered_map<NameId, BlockId>::const_iterator found =
			label_blocks_.find(name);
		if (found != label_blocks_.end()) return found->second;
		const BlockId block = AddBlock(NewLabel("goto"));
		label_blocks_.insert(std::make_pair(name, block));
		return block;
	}

	void EmitBranch(const Operand& condition, BlockId yes, BlockId no)
	{
		Instruction branch(Instruction::BRANCH);
		branch.first = condition;
		branch.target = yes;
		branch.alternate = no;
		Emit(branch);
	}

	void CollectSwitchCases(std::uint32_t node, SwitchCases* cases) const
	{
		std::vector<std::uint32_t> pending(1, node);
		while (!pending.empty())
		{
			const std::uint32_t current = pending.back();
			pending.pop_back();
			const DumpNode& record = arena_.nodes[current];
			if (record.kind == DUMP_SWITCH_STATEMENT) continue;
			if (record.kind == DUMP_CASE_STATEMENT ||
				record.kind == DUMP_DEFAULT_STATEMENT)
				cases->Push(current);
			const NodeChildren children = Children(current);
			for (std::size_t i = children.size(); i != 0; --i)
			{
				if (record.kind == DUMP_CASE_STATEMENT && i == 1) continue;
				pending.push_back(children[i - 1]);
			}
		}
	}

	__attribute__((noinline)) void LowerSwitch(const NodeChildren& children)
	{
		const std::uint32_t condition = FindChildKind(children, DUMP_CONDITION);
		std::uint32_t body = kNoDumpEdge;
		for (std::size_t i = 0; i < children.size(); ++i)
			if (arena_.nodes[children[i]].kind != DUMP_CONDITION)
				body = children[i];
		if (condition == kNoDumpEdge || body == kNoDumpEdge)
			throw std::runtime_error("invalid PA15 switch statement");
		SwitchCases cases;
		CollectSwitchCases(body, &cases);
		const Operand value = LowerSwitchCondition(condition);
		const BlockId dispatch = AddBlock(NewLabel("switch_dispatch"));
		const BlockId end = AddBlock(NewLabel("switch_end"));
		BlockId default_target = end;
		for (std::size_t i = 0; i < cases.size(); ++i)
		{
			const bool is_default =
				arena_.nodes[cases[i]].kind == DUMP_DEFAULT_STATEMENT;
			const BlockId target = AddBlock(NewLabel(is_default ?
				"switch_default" : "switch_case"));
			switch_case_blocks_[cases[i]] = target;
			if (is_default) default_target = target;
		}
		EmitJump(dispatch);
		SelectBlock(dispatch);
		Instruction instruction(Instruction::SWITCH);
		instruction.first = value;
		instruction.target = default_target;
		SmallSequence<std::int64_t, 8> case_values;
		SmallSequence<BlockId, 8> case_targets;
		for (std::size_t i = 0; i < cases.size(); ++i)
		{
			if (arena_.nodes[cases[i]].kind != DUMP_CASE_STATEMENT) continue;
			const NodeChildren case_children = Children(cases[i]);
			if (case_children.empty() || !arena_.nodes[case_children[0]].constant)
				throw std::runtime_error("PA15 case lacks constant value");
			case_values.Push(arena_.nodes[case_children[0]].constant_value);
			case_targets.Push(switch_case_blocks_[cases[i]]);
		}
		AttachSwitchCases(&instruction, case_values, case_targets);
		Emit(instruction);
		break_targets_.push_back(end);
		StatementTask after(STATEMENT_SWITCH_AFTER_BODY);
		after.first = end;
		statement_tasks_.push_back(after);
		PushStatementNode(body);
	}

	void AttachSwitchCases(Instruction* instruction,
		const SmallSequence<std::int64_t, 8>& values,
		const SmallSequence<BlockId, 8>& targets)
	{
		if (values.size() != targets.size())
			throw std::logic_error("PA15 switch case fact mismatch");
		if (values.empty()) return;
		if (values.size() >= kNoLowId ||
			output_.switch_case_values.size() > kNoLowId - values.size() ||
			output_.switch_case_values.size() !=
				output_.switch_case_targets.size())
			throw std::runtime_error("too many PA15 switch cases");
		instruction->extra_first = static_cast<std::uint32_t>(
			output_.switch_case_values.size());
		instruction->extra_count = static_cast<std::uint32_t>(values.size());
		for (std::size_t i = 0; i < values.size(); ++i)
		{
			output_.switch_case_values.push_back(values[i]);
			output_.switch_case_targets.push_back(targets[i]);
		}
	}

	__attribute__((noinline)) void LowerWhile(const NodeChildren& children)
	{
		const std::uint32_t condition = FindChildKind(children, DUMP_CONDITION);
		const std::uint32_t body = FindLoopBody(children);
		if (condition == kNoDumpEdge || body == kNoDumpEdge)
			throw std::runtime_error("invalid PA15 while statement");
		const BlockId cond_block = AddBlock(NewLabel("while_cond"));
		const BlockId body_block = AddBlock(NewLabel("while_body"));
		const BlockId end_block = AddBlock(NewLabel("while_end"));
		EmitJump(cond_block);
		SelectBlock(cond_block);
		EmitBranch(LowerControlCondition(condition), body_block, end_block);
		SelectBlock(body_block);
		break_targets_.push_back(end_block);
		continue_targets_.push_back(cond_block);
		StatementTask after(STATEMENT_LOOP_AFTER_BODY);
		after.first = cond_block;
		after.second = end_block;
		statement_tasks_.push_back(after);
		PushStatementNode(body);
	}

	__attribute__((noinline)) void LowerDo(const NodeChildren& children)
	{
		const std::uint32_t condition = FindChildKind(children, DUMP_CONDITION);
		const std::uint32_t body = FindLoopBody(children);
		if (condition == kNoDumpEdge || body == kNoDumpEdge)
			throw std::runtime_error("invalid PA15 do statement");
		const BlockId body_block = AddBlock(NewLabel("do_body"));
		const BlockId cond_block = AddBlock(NewLabel("do_cond"));
		const BlockId end_block = AddBlock(NewLabel("do_end"));
		EmitJump(body_block);
		SelectBlock(body_block);
		break_targets_.push_back(end_block);
		continue_targets_.push_back(cond_block);
		StatementTask after(STATEMENT_DO_AFTER_BODY);
		after.node = condition;
		after.first = body_block;
		after.second = cond_block;
		after.third = end_block;
		statement_tasks_.push_back(after);
		PushStatementNode(body);
	}

	__attribute__((noinline)) void LowerFor(const NodeChildren& children)
	{
		const std::uint32_t init = FindChildKind(children, DUMP_FOR_INIT_STATEMENT);
		const std::uint32_t condition = FindChildKind(children, DUMP_CONDITION);
		const std::uint32_t iteration = FindChildKind(children, DUMP_ITERATION);
		const std::uint32_t body = FindLoopBody(children);
		if (body == kNoDumpEdge)
			throw std::runtime_error("invalid PA15 for statement");
		if (init != kNoDumpEdge)
		{
			StatementTask after(STATEMENT_FOR_AFTER_INIT);
			after.node = condition;
			after.auxiliary = iteration;
			after.last = body;
			statement_tasks_.push_back(after);
			PushStatementNode(init);
			return;
		}
		StartForLoop(condition, iteration, body);
	}

	void StartForLoop(std::uint32_t condition, std::uint32_t iteration,
		std::uint32_t body)
	{
		const BlockId cond_block = AddBlock(NewLabel("for_cond"));
		const BlockId body_block = AddBlock(NewLabel("for_body"));
		const BlockId iter_block = AddBlock(NewLabel("for_iter"));
		const BlockId end_block = AddBlock(NewLabel("for_end"));
		EmitJump(cond_block);
		SelectBlock(cond_block);
		if (condition == kNoDumpEdge) EmitJump(body_block);
		else EmitBranch(LowerControlCondition(condition), body_block, end_block);
		SelectBlock(body_block);
		break_targets_.push_back(end_block);
		continue_targets_.push_back(iter_block);
		StatementTask after(STATEMENT_FOR_AFTER_BODY);
		after.node = iteration;
		after.first = iter_block;
		after.second = cond_block;
		after.third = end_block;
		statement_tasks_.push_back(after);
		PushStatementNode(body);
	}

	__attribute__((noinline)) void LowerIf(const NodeChildren& children)
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
			throw std::runtime_error("invalid semantic if statement");
		const NodeChildren condition_children = Children(condition);
		if (condition_children.size() != 1)
			throw std::runtime_error("condition declarations are outside the active checkpoint");
		const BlockId then_block = AddBlock(NewLabel("if_then"));
		const BlockId else_block = AddBlock(NewLabel("if_else"));
		const BlockId end_block = AddBlock(NewLabel("if_end"));
		if (arena_.nodes[condition_children[0]].kind ==
			DUMP_CONDITION_DECLARATION)
			EmitBranch(LowerControlCondition(condition), then_block, else_block);
		else EmitConditionBranch(condition_children[0], then_block, else_block);
		SelectBlock(then_block);
		StatementTask after(STATEMENT_IF_AFTER_THEN);
		after.node = else_node;
		after.first = else_block;
		after.second = end_block;
		statement_tasks_.push_back(after);
		PushStatementNode(then_node);
	}

	void EmitConditionBranch(std::uint32_t node, BlockId true_block,
		BlockId false_block)
	{
		const DumpNode& record = arena_.nodes[node];
		const NodeChildren children = Children(node);
		if (record.kind == DUMP_BINARY_EXPRESSION && children.size() == 2)
		{
			const std::string operation =
				StripOperationPrefix(program_.names.Get(record.text));
			if (operation == "&&")
			{
				const BlockId rhs = AddBlock(NewLabel("land_rhs"));
				EmitConditionBranch(children[0], rhs, false_block);
				SelectBlock(rhs);
				EmitConditionBranch(children[1], true_block, false_block);
				return;
			}
			if (operation == "||")
			{
				const BlockId rhs = AddBlock(NewLabel("lor_rhs"));
				EmitConditionBranch(children[0], true_block, rhs);
				SelectBlock(rhs);
				EmitConditionBranch(children[1], true_block, false_block);
				return;
			}
		}
		Instruction branch(Instruction::BRANCH);
		branch.first = LowerCondition(node);
		branch.target = true_block;
		branch.alternate = false_block;
		Emit(branch);
	}

	const SemanticGraphView& graph_;
	const Program& program_;
	const DumpArena& arena_;
	TypedProgram& output_;
	LowIRLoweringStats* stats_;
	std::vector<SymbolId> function_symbols_;
	std::vector<SymbolId> global_symbols_;
	std::vector<SymbolId> literal_symbols_;
	std::vector<DynamicInitializer> dynamic_initializers_;
	std::vector<std::uint32_t> function_definition_;
	std::vector<std::uint32_t> function_declaration_;
	std::vector<std::uint32_t> global_node_;
	StringCounterTable overload_counts_;
	Function* function_;
	BlockId current_block_;
	LowType current_result_;
	bool current_result_reference_;
	std::size_t temp_counter_;
	std::size_t block_counter_;
	std::size_t generated_slot_ordinal_;
	std::vector<SlotId> binding_slots_;
	std::vector<SlotId> generated_slots_;
	std::vector<BlockId> switch_case_blocks_;
	std::vector<BlockId> break_targets_;
	std::vector<BlockId> continue_targets_;
	std::vector<StatementTask> statement_tasks_;
	std::unordered_map<NameId, BlockId> label_blocks_;
	StringCounterTable used_names_;
	StringCounterTable assigned_names_;
	StringCounterTable slot_name_counts_;
	std::size_t parameter_slot_index_;
	std::size_t source_ordinal_;
	std::vector<IdentityTypeId> identity_type_cache_;
};


class CountingStreamBuffer : public std::streambuf
{
public:
	explicit CountingStreamBuffer(std::streambuf* destination)
		: destination_(destination), bytes_(0) {}

	std::size_t Bytes() const { return bytes_; }

protected:
	int_type overflow(int_type character)
	{
		if (traits_type::eq_int_type(character, traits_type::eof()))
			return traits_type::not_eof(character);
		const int_type written = destination_->sputc(
			traits_type::to_char_type(character));
		if (!traits_type::eq_int_type(written, traits_type::eof())) ++bytes_;
		return written;
	}

	std::streamsize xsputn(const char* data, std::streamsize size)
	{
		const std::streamsize written = destination_->sputn(data, size);
		if (written > 0) bytes_ += static_cast<std::size_t>(written);
		return written;
	}

	int sync() { return destination_->pubsync(); }

private:
	std::streambuf* destination_;
	std::size_t bytes_;
};

class GraphConsumer : public SemanticGraphConsumer
{
public:
	GraphConsumer(TypedProgram& program, LowIRLoweringStats* stats,
		std::size_t source_ordinal)
		: program_(program), stats_(stats), source_ordinal_(source_ordinal) {}

	void Consume(const SemanticGraphView& graph)
	{
		const std::chrono::steady_clock::time_point started =
			std::chrono::steady_clock::now();
		GraphLowerer(graph, program_, stats_, source_ordinal_).Lower();
		if (stats_)
			stats_->lowering_nanoseconds += static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - started).count());
	}

private:
	TypedProgram& program_;
	LowIRLoweringStats* stats_;
	std::size_t source_ordinal_;
};

}

LowIRLoweringStats::LowIRLoweringStats()
	: source_bytes(0), tokens(0), semantic_nodes(0), semantic_edges(0),
	  lowered_nodes(0), functions(0), globals(0), blocks(0), instructions(0),
	  binding_index_probes(0), typed_storage_bytes(0), output_bytes(0),
	  semantic_nanoseconds(0), lowering_nanoseconds(0), render_nanoseconds(0)
{
}

void WriteLowIRProgram(const std::vector<LowIRSource>& sources,
	const PreprocessingOptions& options, std::ostream& output,
	LowIRLoweringStats* stats)
{
	if (sources.empty()) throw std::runtime_error("no PA15 source inputs");
	if (stats) *stats = LowIRLoweringStats();
	TypedProgram program;
	for (std::size_t i = 0; i < sources.size(); ++i)
	{
		GraphConsumer consumer(program, stats, i);
		SemanticAnalysisStats semantic_stats;
		ConsumeSemanticTranslationUnit(sources[i].path, sources[i].source,
			options, consumer, stats ? &semantic_stats : 0);
		if (stats)
		{
			stats->source_bytes += sources[i].source.size();
			stats->tokens += semantic_stats.tokens;
			stats->semantic_nodes += semantic_stats.semantic_nodes;
			stats->semantic_edges += semantic_stats.semantic_edges;
			stats->semantic_nanoseconds += semantic_stats.analysis_nanoseconds;
		}
	}
	const std::chrono::steady_clock::time_point render_started =
		std::chrono::steady_clock::now();
	CountingStreamBuffer buffer(output.rdbuf());
	std::ostream rendered(&buffer);
	RenderLowIRProgram(program, rendered);
	rendered.flush();
	if (!rendered || !output)
		throw std::runtime_error("unable to write LowIR output");
	if (stats)
	{
		stats->typed_storage_bytes = TypedStorageBytes(program);
		stats->output_bytes = buffer.Bytes();
		stats->render_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - render_started).count());
	}
}

}
