#include "lowering/abi/symbol_metadata.h"

#include "lowering/ir/model.h"

#include <string>

namespace cppgm
{
namespace lowering
{
namespace abi
{

void ApplyBuiltinSymbolMetadata(lowering::ir::Symbol* symbol,
	semantic::BuiltinFunctionKind kind,
	hosted_builtin::MemoryIntrinsicKind memory_kind)
{
	using namespace semantic;
	using lowering::ir::Symbol;
	if (kind == BUILTIN_FUNCTION_HOSTED_MEMORY_INTRINSIC)
	{
		switch (hosted_builtin::GetMemoryIntrinsic(memory_kind).effect)
		{
		case hosted_builtin::MEMORY_EFFECT_READNONE:
			symbol->effects = Symbol::EFFECTS_READNONE; break;
		case hosted_builtin::MEMORY_EFFECT_READONLY:
			symbol->effects = Symbol::EFFECTS_READONLY; break;
		case hosted_builtin::MEMORY_EFFECT_READWRITE:
			symbol->effects = Symbol::EFFECTS_READWRITE; break;
		}
		return;
	}
	switch (kind)
	{
	case BUILTIN_FUNCTION_STRLEN: symbol->effects = Symbol::EFFECTS_READONLY; break;
	case BUILTIN_FUNCTION_UNREACHABLE: break;
	case BUILTIN_FUNCTION_MEMCPY:
	case BUILTIN_FUNCTION_MEMMOVE: symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_NANL:
	case BUILTIN_FUNCTION_ISNAN:
	case BUILTIN_FUNCTION_HOSTED_INTEGER_INTRINSIC:
	case BUILTIN_FUNCTION_HOSTED_FLOATING_INTRINSIC:
		symbol->effects = Symbol::EFFECTS_READNONE; break;
	case BUILTIN_FUNCTION_HOSTED_MEMORY_INTRINSIC: break;
	case BUILTIN_FUNCTION_IA32_EMMS:
		symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_ABORT:
		symbol->noreturn = true; break;
	case BUILTIN_FUNCTION_ALLOCA:
	case BUILTIN_FUNCTION_VSNPRINTF:
	case BUILTIN_FUNCTION_VA_START:
	case BUILTIN_FUNCTION_VA_END:
	case BUILTIN_FUNCTION_VA_ARG:
		symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_OPERATOR_NEW:
	case BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY:
		symbol->runtime_role = Symbol::RUNTIME_ROLE_ALLOCATE_MEMORY;
		symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_OPERATOR_DELETE:
	case BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY:
		symbol->runtime_role = Symbol::RUNTIME_ROLE_FREE_MEMORY;
		symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_NONE: break;
	}
}

void ApplyNativeRuntimeSymbolMetadata(
	const lowering::ir::Program& program,
	lowering::ir::Symbol* symbol)
{
	using lowering::ir::Symbol;
	if (!symbol->c_linkage || !symbol->object_name.valid()) return;
	const std::string& object_name = program.strings.get(symbol->object_name);
	if (object_name == "malloc")
		symbol->runtime_role = Symbol::RUNTIME_ROLE_ALLOCATE_MEMORY;
	else if (object_name == "free")
		symbol->runtime_role = Symbol::RUNTIME_ROLE_FREE_MEMORY;
}

void ApplyBuiltinParameterAliasMetadata(lowering::ir::Parameter* parameter,
	semantic::BuiltinFunctionKind kind,
	hosted_builtin::MemoryIntrinsicKind memory_kind, std::size_t index)
{
	using namespace semantic;
	using lowering::ir::Parameter;
	if (kind == BUILTIN_FUNCTION_HOSTED_MEMORY_INTRINSIC)
	{
		if (memory_kind == hosted_builtin::MEMORY_INTRINSIC_MEMCPY && index < 2)
			parameter->alias = Parameter::ALIAS_NOALIAS;
		return;
	}
	if (kind == BUILTIN_FUNCTION_MEMCPY && index < 2)
	{
		parameter->alias = Parameter::ALIAS_NOALIAS;
	}
}

}  // namespace abi
}  // namespace lowering
}  // namespace cppgm

