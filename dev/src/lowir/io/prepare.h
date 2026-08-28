#pragma once

#include "lowir/model/program.h"

#include <cstddef>
#include <cstdint>

namespace lowir_model
{

struct LowirPreparationStats
{
	// Presentation telemetry is collected only when this optional stats object
	// is supplied.  It measures the representation boundary without adding
	// counters to ordinary compilation.
	std::size_t typed_name_entries = 0;
	std::size_t typed_name_bytes = 0;
	std::size_t adapter_prefix_renders = 0;
	std::size_t adapter_prefix_bytes = 0;
	std::size_t adapter_integer_renders = 0;
	std::size_t adapter_integer_bytes = 0;
	std::size_t adapter_literal_materializations = 0;
	StringPoolStats adapter_string_pool;
	std::size_t lowir_string_entries = 0;
	std::size_t lowir_spelling_bytes = 0;
	std::size_t lowir_string_storage_bytes = 0;
	std::size_t lowir_model_storage_bytes = 0;
	std::size_t typed_lowir_peak_live_bytes = 0;
	std::size_t reference_operand_visits = 0;
	std::size_t referenced_symbols = 0;
	std::size_t declaration_visits = 0;
	std::size_t retained_declarations = 0;
	std::size_t function_order_visits = 0;
	std::size_t function_moves = 0;
	std::size_t function_copies = 0;
	std::size_t alias_order_visits = 0;
	std::size_t alias_moves = 0;
	std::size_t serialized_operand_visits = 0;
	std::size_t derived_operand_visits = 0;
	std::size_t boundary_call_visits = 0;
	std::size_t exports = 0;
	std::uint64_t frontend_canonical_nanoseconds = 0;
	std::uint64_t serialized_canonical_nanoseconds = 0;
	std::uint64_t derived_facts_nanoseconds = 0;
};

// Apply source-adapter presentation rules that are not meaningful for an
// arbitrary external LowIR file. The result has deterministic declaration,
// generated-function, alias, linkage, and object-spelling form.
void canonicalize_frontend_lowir(
	LowirProgram& program, LowirPreparationStats* stats = 0);

// Remove producer-private fields and express all persistent facts in the form
// represented by LowIR syntax and the canonical compiler-object payload.
void canonicalize_serialized_lowir_facts(
	LowirProgram& program, LowirPreparationStats* stats = 0);

// Rebuild facts that are deterministic functions of persistent LowIR but are
// consumed directly by optimization, object linking, or native lowering.
void derive_lowir_object_facts(
	LowirProgram& program, LowirPreparationStats* stats = 0);

// Publish the object-boundary tables for a typed producer that has already
// supplied canonical operand bindings and direct-call boundary facts.
void publish_prederived_lowir_object_facts(
	LowirProgram& program, LowirPreparationStats* stats = 0);

// Complete the persistent and derived object boundary in one call.
void finalize_lowir_object_model(
	LowirProgram& program, LowirPreparationStats* stats = 0);

// Direct-call boundaries are also needed before validation of external text.
void propagate_direct_call_boundaries(
	LowirProgram& program, LowirPreparationStats* stats = 0);

}  // namespace lowir_model
