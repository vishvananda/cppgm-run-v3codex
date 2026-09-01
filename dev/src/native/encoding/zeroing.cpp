#include "native/encoding/zeroing.h"

#include "native/errors.h"
#include "native/encoding/instructions.h"

#include <climits>
#include <cstdint>

namespace lowir_native {
namespace {

unsigned next_store_width(std::size_t remaining)
{
	if (remaining >= 8) return 64;
	if (remaining >= 4) return 32;
	if (remaining >= 2) return 16;
	return 8;
}

std::size_t direct_store_cost(X64Register destination,
	std::size_t byte_count)
{
	// Two immediate stores already approach the fixed REP setup cost.  This
	// bound keeps the decision independent of the span size.
	if (byte_count > 16) return static_cast<std::size_t>(-1);
	std::size_t cost = 0;
	std::size_t offset = 0;
	while (offset < byte_count)
	{
		const unsigned width = next_store_width(byte_count - offset);
		cost += immediate_store_encoding_size(destination,
			static_cast<long long>(offset), width);
		offset += width / CHAR_BIT;
	}
	return cost;
}

std::size_t count_materialization_cost(std::size_t byte_count)
{
	const std::uint64_t value = static_cast<std::uint64_t>(byte_count);
	if (value <= UINT64_C(0xffffffff)) return 5;
	if (value >= UINT64_C(0xffffffff80000000)) return 7;
	return 10;
}

std::size_t rep_store_cost(X64Register destination, std::size_t byte_count)
{
	const std::size_t address_move = destination == XR_RDI ? 0 : 3;
	return address_move + count_materialization_cost(byte_count) + 4;
}

void emit_direct_stores(elf_detail::CodeBuffer & out,
	X64Register destination, std::size_t byte_count)
{
	std::size_t offset = 0;
	std::size_t stores = 0;
	while (offset < byte_count)
	{
		const unsigned width = next_store_width(byte_count - offset);
		emit_immediate_store(out, destination, static_cast<long long>(offset),
			0, width);
		offset += width / CHAR_BIT;
		++stores;
	}
	out.note_direct_zero_encoding(byte_count, stores);
}

void emit_vector_zero_16(elf_detail::CodeBuffer & out,
	X64Register destination)
{
	// The allocator reserves xmm6/xmm7 for encoder scratch use.  A pxor and
	// one unaligned store are smaller than REP setup for every address
	// register, preserve integer flags, and require only baseline x86-64 SSE2.
	out.byte(0x66);
	out.byte(0x0f);
	out.byte(0xef);
	out.byte(0xff); // pxor xmm7, xmm7
	out.byte(0xf3);
	emit_rex(out, false, static_cast<X64Register>(XMM_7), destination);
	out.byte(0x0f);
	out.byte(0x7f);
	emit_memory_modrm(out, static_cast<unsigned>(XMM_7), destination, 0);
	out.note_direct_zero_encoding(16, 1);
}

}

void emit_zero_bytes(elf_detail::CodeBuffer & out, X64Register destination,
	std::size_t byte_count)
{
	if (byte_count == 0)
		native_errors::ThrowInternal("native zero-byte span is empty");
	if (byte_count == 16)
	{
		emit_vector_zero_16(out, destination);
		return;
	}
	if (direct_store_cost(destination, byte_count) <
		rep_store_cost(destination, byte_count))
	{
		emit_direct_stores(out, destination, byte_count);
		return;
	}
	if (destination != XR_RDI) emit_register_move(out, XR_RDI, destination);
	emit_immediate_move(out, XR_RCX, byte_count);
	out.byte(0x31);
	out.byte(0xc0);
	out.byte(0xf3);
	out.byte(0xaa);
}

}
