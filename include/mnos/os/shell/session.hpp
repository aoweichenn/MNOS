#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/shell/shell.hpp>

namespace mnos::os::kernel
{
class Kernel;
}

namespace mnos::os::proc
{
class Process;
}

namespace mnos::os::sched
{
class ThreadContext;
}

namespace mnos::os::shell
{
inline constexpr std::size_t SHELL_SESSION_READ_BUFFER_SIZE = std::size_t{128};
inline constexpr std::string_view SHELL_SESSION_DEFAULT_PROMPT = "mnos> ";

enum class ShellSessionStepStatus : std::uint8_t
{
    BLOCKED,
    PENDING_INPUT,
    COMMAND,
    EXITED,
    IO_ERROR,
    COUNT
};

inline constexpr std::size_t SHELL_SESSION_STEP_STATUS_COUNT =
    static_cast<std::size_t>(ShellSessionStepStatus::COUNT);

[[nodiscard]] bool is_shell_session_step_status_valid(ShellSessionStepStatus status) noexcept;
[[nodiscard]] std::size_t shell_session_step_status_to_index(ShellSessionStepStatus status) noexcept;
[[nodiscard]] std::string_view shell_session_step_status_to_name(ShellSessionStepStatus status) noexcept;

class ShellSessionStepResult final
{
public:
    [[nodiscard]] static ShellSessionStepResult blocked() noexcept;
    [[nodiscard]] static ShellSessionStepResult pending_input() noexcept;
    [[nodiscard]] static ShellSessionStepResult command(ShellCommandResult command_result) noexcept;
    [[nodiscard]] static ShellSessionStepResult exited() noexcept;
    [[nodiscard]] static ShellSessionStepResult exited(ShellCommandResult command_result) noexcept;
    [[nodiscard]] static ShellSessionStepResult io_error(io::IoStatus io_status) noexcept;

    [[nodiscard]] ShellSessionStepStatus status() const noexcept;
    [[nodiscard]] ShellCommandStatus command_status() const noexcept;
    [[nodiscard]] io::IoStatus io_status() const noexcept;
    [[nodiscard]] bool has_command_status() const noexcept;
    [[nodiscard]] bool has_io_status() const noexcept;
    [[nodiscard]] bool is_blocked() const noexcept;
    [[nodiscard]] bool is_exited() const noexcept;

private:
    ShellSessionStepResult(
        ShellSessionStepStatus status,
        ShellCommandStatus command_status,
        io::IoStatus io_status) noexcept;

    ShellSessionStepStatus status_;
    ShellCommandStatus command_status_;
    io::IoStatus io_status_;
};

class ShellSession final
{
public:
    ShellSession(kernel::Kernel& os_kernel, proc::Process& process, sched::ThreadContext& thread) noexcept;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool prompt_pending() const noexcept;
    [[nodiscard]] bool has_pending_input() const noexcept;
    [[nodiscard]] std::size_t pending_input_size() const noexcept;
    [[nodiscard]] ShellSessionStepResult poll();

private:
    [[nodiscard]] std::optional<io::IoStatus> write_prompt_if_needed();
    [[nodiscard]] bool has_complete_line() const;
    [[nodiscard]] std::optional<std::size_t> next_line_break_index() const;
    [[nodiscard]] ShellSessionStepResult execute_next_ready_line();

    kernel::Kernel* os_kernel_;
    proc::Process* process_;
    sched::ThreadContext* thread_;
    Shell shell_;
    std::string pending_line_;
    bool prompt_pending_ = true;
};
}
