#pragma once

#include "lowir_model.h"
#include "lowir_prepare.h"
#include "lowering/ir/model.h"

namespace cppgm
{

// Convert between the front-end's compact typed LowIR and the backend's typed
// LowIR model.  This is an in-memory phase adapter; it deliberately does not
// render or parse LowIR text.
lowir_model::LowirProgram AdaptTypedLowIRForNative(
	lowering::ir::Program&& source,
	lowir_model::LowirPreparationStats* preparation_stats = 0,
	lowir_model::PresentationPolicy presentation_policy =
		lowir_model::PRESENTATION_SERIALIZABLE);

}
