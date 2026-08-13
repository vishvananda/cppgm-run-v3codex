#pragma once

#include "lowir_model.h"
#include "lowir_native_mir.h"

namespace lowir_native {
namespace host_eh_detail {

bool requires_host_eh_storage(const lowir_model::LowirFunction & function);
void collect_host_eh_clauses(mir_model::MirFunction * function);

}  // namespace host_eh_detail
}  // namespace lowir_native
