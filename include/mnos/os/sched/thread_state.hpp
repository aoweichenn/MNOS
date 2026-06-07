#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mnos::os::sched
{
enum class ThreadState : std::uint8_t
{
    READY,
    RUNNING,
    BLOCKED,
    DEAD,
    COUNT
};

inline constexpr std::size_t THREAD_STATE_KIND_COUNT = static_cast<std::size_t>(ThreadState::COUNT);

[[nodiscard]] bool is_thread_state_valid(ThreadState state) noexcept;
[[nodiscard]] std::size_t thread_state_to_index(ThreadState state) noexcept;
[[nodiscard]] std::string_view thread_state_to_name(ThreadState state) noexcept;
}
