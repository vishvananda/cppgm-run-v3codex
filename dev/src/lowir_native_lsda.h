#pragma once

namespace lowir_native {

struct Stats;

namespace object_elf_detail {
struct HostFunctionLayout;
}

namespace lsda_detail {

void coalesce_call_sites(object_elf_detail::HostFunctionLayout & function,
                         Stats * stats);

}  // namespace lsda_detail
}  // namespace lowir_native
