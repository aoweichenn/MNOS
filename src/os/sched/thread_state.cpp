#include <array>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/os/sched/thread_state.hpp>

namespace
{
constexpr std::string_view THREAD_STATE_NAME_INVALID_TEXT = "<invalid>";

class ThreadStateCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::os::sched::ThreadState state) noexcept
    {
        return THREAD_STATE_NAMES.contains(state);
    }

    [[nodiscard]] static std::size_t index(const mnos::os::sched::ThreadState state) noexcept
    {
        return THREAD_STATE_NAMES.index(state);
    }

    [[nodiscard]] static std::string_view name(const mnos::os::sched::ThreadState state) noexcept
    {
        return THREAD_STATE_NAMES.name(state);
    }

private:
    inline static constexpr auto THREAD_STATE_NAMES = mnos::core::make_enum_name_table<mnos::os::sched::ThreadState>(
        std::array<std::string_view, mnos::os::sched::THREAD_STATE_COUNT>{
            "READY",
            "RUNNING",
            "BLOCKED",
            "DEAD"},
        THREAD_STATE_NAME_INVALID_TEXT);
};
}

namespace mnos::os::sched
{
bool is_thread_state_valid(const ThreadState state) noexcept
{
    return ThreadStateCatalog::contains(state);
}

std::size_t thread_state_to_index(const ThreadState state) noexcept
{
    return ThreadStateCatalog::index(state);
}

std::string_view thread_state_to_name(const ThreadState state) noexcept
{
    return ThreadStateCatalog::name(state);
}
}
