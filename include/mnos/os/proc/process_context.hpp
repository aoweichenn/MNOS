#pragma once

#include <mnos/cpu/memory/paging.hpp>
#include <mnos/os/proc/process_id.hpp>

namespace mnos::os::proc
{
[[nodiscard]] cpu::memory::ProcessContextId process_context_id_for(ProcessId process_id);
}
