#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>

#include <mnos/cpu/execution/trace.hpp>
#include <mnos/host/machine_session.hpp>
#include <mnos/host/terminal_backend.hpp>

namespace mnos::host
{
inline constexpr std::string_view HOST_DEBUGGER_DEFAULT_TITLE = "MNOS x86-64 Machine";
inline constexpr std::size_t HOST_DEBUGGER_DEFAULT_TRACE_CAPACITY = std::size_t{64};
inline constexpr std::size_t HOST_DEBUGGER_DEFAULT_INSTRUCTION_TRACE_CAPACITY = std::size_t{64};

enum class HostDebuggerRunState : std::uint8_t
{
    PAUSED,
    RUNNING,
    COUNT
};

inline constexpr std::size_t HOST_DEBUGGER_RUN_STATE_COUNT =
    static_cast<std::size_t>(HostDebuggerRunState::COUNT);

[[nodiscard]] bool is_host_debugger_run_state_valid(HostDebuggerRunState state) noexcept;
[[nodiscard]] std::size_t host_debugger_run_state_to_index(HostDebuggerRunState state) noexcept;
[[nodiscard]] std::string_view host_debugger_run_state_to_name(HostDebuggerRunState state) noexcept;

struct HostDebuggerSessionConfig final
{
    HostMachineSessionConfig machine;
    std::string title{HOST_DEBUGGER_DEFAULT_TITLE};
    std::size_t trace_capacity = HOST_DEBUGGER_DEFAULT_TRACE_CAPACITY;
    std::size_t instruction_trace_capacity = HOST_DEBUGGER_DEFAULT_INSTRUCTION_TRACE_CAPACITY;
};

struct HostDebuggerControlState final
{
    bool can_reset = true;
    bool can_step = false;
    bool can_run = false;
    bool can_pause = false;
    bool can_execute_user_program = false;
    bool can_submit_input = false;
    bool can_send_exit = false;
};

struct HostDebuggerFrame final
{
    HostMachineSessionSnapshot snapshot;
    HostDebuggerControlState controls;
    std::string title;
    std::string run_control_text;
    std::string status_text;
    std::string counters_text;
    std::string memory_text;
    std::string processor_text;
    std::string cpu_text;
    std::string registers_text;
    std::string paging_text;
    std::string cursor_text;
    std::string summary_text;
    std::string display_text;
    std::string trace_text;
    std::string instruction_trace_text;
    HostDebuggerRunState run_state = HostDebuggerRunState::PAUSED;
    bool booted = false;
    bool accepts_input = false;
    std::size_t display_column_count = std::size_t{0};
    std::size_t display_row_count = std::size_t{0};
    std::size_t cursor_column = std::size_t{0};
    std::size_t cursor_row = std::size_t{0};
    std::size_t trace_entry_count = std::size_t{0};
    std::size_t instruction_trace_entry_count = std::size_t{0};
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
    [[nodiscard]] HostDebuggerRunState run_state() const noexcept;
    [[nodiscard]] std::size_t trace_entry_count() const noexcept;
    [[nodiscard]] std::size_t instruction_trace_entry_count() const noexcept;

    void boot();
    void reset();
    void pause();
    void clear_trace() noexcept;
    void clear_instruction_trace() noexcept;
    void record_instruction_trace(std::span<const cpu::ExecutionTraceEntry> entries);
    void record_instruction_trace(const cpu::ExecutionTrace& trace);

    [[nodiscard]] HostMachineSessionStatus pump_until_waiting();
    [[nodiscard]] HostMachineSessionStatus step_until_waiting();
    [[nodiscard]] HostMachineSessionStatus run_until_waiting();
    [[nodiscard]] HostMachineSessionStatus run_sample_user_program();
    [[nodiscard]] HostMachineSessionStatus submit_text(std::string_view text);
    [[nodiscard]] HostMachineSessionStatus submit_command_line(std::string_view command_line);
    [[nodiscard]] HostMachineSessionStatus submit_special_key(HostSpecialKey key);
    [[nodiscard]] HostMachineSessionStatus submit_input_event(const HostInputEvent& event);
    [[nodiscard]] HostDebuggerFrame frame() const;

private:
    void append_trace(std::string_view action, HostMachineSessionStatus status, std::string_view detail = {});
    void append_instruction_trace(const cpu::ExecutionTraceEntry& entry);

    HostDebuggerSessionConfig config_;
    HostMachineSession machine_session_;
    std::deque<std::string> trace_entries_;
    std::deque<std::string> instruction_trace_entries_;
    HostDebuggerRunState run_state_ = HostDebuggerRunState::PAUSED;
    std::uint64_t next_trace_sequence_ = std::uint64_t{1};
    std::uint64_t next_instruction_trace_sequence_ = std::uint64_t{1};
};
}
