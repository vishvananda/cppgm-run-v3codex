#pragma once

#include "lowir/model/program.h"
#include "lowir/io/prepare.h"
#include "lowering/ir/model.h"

namespace cppgm
{
namespace lowir_io
{

// Convert between the front-end's compact typed LowIR and the backend's typed
// LowIR model.  This is an in-memory phase adapter; it deliberately does not
// render or parse LowIR text.
lowir_model::LowirProgram AdaptTypedLowirForBackend(
	lowering::ir::Program&& source,
	lowir_model::LowirPreparationStats* preparation_stats = 0,
	lowir_model::PresentationPolicy presentation_policy =
		lowir_model::PRESENTATION_SERIALIZABLE);

}  // namespace lowir_io
}  // namespace cppgm
