#pragma once

#include "lowering/ir/model.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cppgm
{
namespace semantic
{
struct Stats;
struct DumpNode;
}
namespace lowering
{
namespace support
{


std::string SanitizeSymbol(const std::string& name);
std::vector<unsigned char> DecodeStringLiteral(const std::string& spelling);
std::int64_t CanonicalIntegerImmediate(std::int64_t value,
	std::uint8_t width, bool is_signed);
bool IsNullPointerLiteralCast(const semantic::Program& program,
	const semantic::DumpNode& source, semantic::TypeId target);
bool IsIntNullPointerLiteralCast(const semantic::Program& program,
	const semantic::DumpNode& source, semantic::TypeId target);
bool NeedsAggregateStorageAddress(bool namespace_object, bool has_leaf,
	const semantic::BindingRecord& binding);
semantic::EntityId LambdaClosureEntity(
	const semantic::Program& program, semantic::TypeId type);
bool IsLambdaCaptureMember(
	const semantic::Program& program, semantic::BindingId binding);
std::string MissingStorageBindingDetail(
	const semantic::Program& program, semantic::BindingId binding);
std::string NormalizeFloatingLiteral(const std::string& spelling,
	const lowering::ir::LowType& type);
bool DecodeFloatingLiteral(const std::string& spelling,
	const lowering::ir::LowType& type, std::uint64_t* low,
	std::uint64_t* high);

}  // namespace support
}  // namespace lowering
}  // namespace cppgm
