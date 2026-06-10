#pragma once

#include <string_view>
#include <vector>

#include <mnos/cpu/common/types.hpp>

namespace mnos::os::kernel
{
class Kernel;
}

namespace mnos::host
{
inline constexpr std::string_view HOST_DEMO_BIN_DIRECTORY = "/bin";
inline constexpr std::string_view HOST_DEMO_EXIT42_PATH = "/bin/exit42";
inline constexpr std::int64_t HOST_DEMO_EXIT42_CODE = std::int64_t{42};

[[nodiscard]] std::vector<cpu::Byte> make_host_demo_exit42_elf64();
void install_host_demo_user_programs(os::kernel::Kernel& os_kernel);
}
