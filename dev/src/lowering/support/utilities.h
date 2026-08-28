#pragma once

#include "lowering/ir/model.h"

#include <cstddef>
#include <cstdint>
#include <streambuf>
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

class PresentationNameMap
{
	public:
	PresentationNameMap(const semantic::Program& program,
		semantic::Stats* stats);
	std::string Apply(const semantic::BindingRecord& binding) const;

private:
	const std::string& ClassTemplatePresentation(
		std::uint32_t presentation) const;
	const semantic::Program& program_;
	semantic::Stats* stats_;
	std::vector<std::uint32_t> replacement_presentations_;
	std::vector<semantic::EntityId> presentation_entities_;
	mutable std::vector<std::uint32_t> rendered_indices_;
	mutable std::vector<std::string> rendered_presentations_;
	mutable std::vector<semantic::NameId> path_;
};

class CountingStreamBuffer : public std::streambuf
{
public:
	explicit CountingStreamBuffer(std::streambuf* destination);
	std::size_t Bytes() const;

protected:
	int_type overflow(int_type character);
	std::streamsize xsputn(const char* data, std::streamsize size);
	int sync();

private:
	std::streambuf* destination_;
	std::size_t bytes_;
};

}  // namespace support
}  // namespace lowering
}  // namespace cppgm
