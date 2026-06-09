#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <mnos/host/machine_session.hpp>
#include <mnos/host/terminal_backend.hpp>

namespace mnos::host
{
inline constexpr std::string_view HOST_DEBUGGER_DEFAULT_TITLE = "MNOS x86-64 Machine";

struct HostDebuggerSessionConfig final
{
    HostMachineSessionConfig machine;
    std::string title{HOST_DEBUGGER_DEFAULT_TITLE};
};

struct HostDebuggerFrame final
{
    HostMachineSessionSnapshot snapshot;
    std::string title;
    std::string status_text;
    std::string counters_text;
    std::string memory_text;
    std::string processor_text;
    std::string cursor_text;
    std::string summary_text;
    std::string display_text;
    bool booted = false;
    bool accepts_input = false;
    std::size_t display_column_count = std::size_t{0};
    std::size_t display_row_count = std::size_t{0};
    std::size_t cursor_column = std::size_t{0};
    std::size_t cursor_row = std::size_t{0};
    std::uint64_t scroll_count = std::uint64_t{0};
};

class HostDebuggerSession final
{
public:
    explicit HostDebuggerSession(HostDebuggerSessionConfig config = {}) noexcept;
    HostDebuggerSession(const HostDebuggerSession&) = delete;
    HostDebuggerSession& operator=(const HostDebuggerSession&) = delete;
    HostDebuggerSession(HostDebuggerSession&&) noexcept;
    HostDebuggerSession& operator=(HostDebuggerSession&&) noexcept;
    ~HostDebuggerSession();

    [[nodiscard]] const HostDebuggerSessionConfig& config() const noexcept;
    [[nodiscard]] const HostMachineSession& machine_session() const noexcept;
    [[nodiscard]] HostMachineSession& machine_session() noexcept;

    void boot();
    void reset();

    [[nodiscard]] HostMachineSessionStatus pump_until_waiting();
    [[nodiscard]] HostMachineSessionStatus submit_text(std::string_view text);
    [[nodiscard]] HostMachineSessionStatus submit_command_line(std::string_view command_line);
    [[nodiscard]] HostMachineSessionStatus submit_special_key(HostSpecialKey key);
    [[nodiscard]] HostDebuggerFrame frame() const;

private:
    HostDebuggerSessionConfig config_;
    HostMachineSession machine_session_;
};
}
