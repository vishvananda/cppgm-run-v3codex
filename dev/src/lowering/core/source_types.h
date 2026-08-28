#pragma once

#include "semantic/model/graph.h"
#include "lowering/ir/types.h"

#include <cstdint>
#include <string>

namespace cppgm
{
namespace lowering
{

std::int64_t CanonicalIntegerImmediate(std::int64_t value,
	std::uint8_t width, bool is_signed);
bool IsNullPointerLiteralCast(const semantic::Program& program,
	const semantic::DumpNode& source, semantic::TypeId target);
bool IsIntNullPointerLiteralCast(const semantic::Program& program,
	const semantic::DumpNode& source, semantic::TypeId target);
std::string NormalizeFloatingLiteral(const std::string& spelling,
	const lowering::ir::LowType& type);
bool DecodeFloatingLiteral(const std::string& spelling,
	const lowering::ir::LowType& type, std::uint64_t* low,
	std::uint64_t* high);

class SourceTypeLowering
{
public:
	explicit SourceTypeLowering(const semantic::Program& program);

	lowering::ir::LowType Lower(semantic::TypeId type) const;
	lowering::ir::LowType LowerExpression(semantic::TypeId type) const;
	lowering::ir::LowType LowerStorage(semantic::TypeId type) const;
	bool IsReference(semantic::TypeId type) const;
	bool IsArray(semantic::TypeId type) const;
	bool IsFunction(semantic::TypeId type) const;
	bool IsClassObject(semantic::TypeId type) const;
	bool IsComplexObject(semantic::TypeId type) const;
	bool IsPointerLike(semantic::TypeId type) const;
	bool IsNullptr(semantic::TypeId type) const;
	semantic::TypeId RemoveReference(semantic::TypeId type) const;
	semantic::TypeId RemoveTopQualifiers(semantic::TypeId type) const;
	semantic::TypeId ExpressionObject(semantic::TypeId type) const;
	semantic::TypeId ArrayElement(semantic::TypeId type) const;
	semantic::TypeId Pointee(semantic::TypeId type) const;

private:
	const semantic::Program& program_;
};

}
}
