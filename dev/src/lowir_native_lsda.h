#pragma once

#include <cstddef>

namespace lowir_native {

struct Stats;

namespace object_elf_detail {
struct HostFunctionLayout;
}

namespace lsda_detail {

void record_unprotected_unwind_range(
    object_elf_detail::HostFunctionLayout & function,
    std::size_t start, std::size_t length);
void coalesce_call_sites(object_elf_detail::HostFunctionLayout & function,
                         Stats * stats);

}  // namespace lsda_detail
}  // namespace lowir_native
