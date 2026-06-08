#include <array>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <mnos/os/shell/session.hpp>

namespace
{
constexpr std::string_view SHELL_SESSION_STATUS_NAME_INVALID_TEXT = "<invalid>";
constexpr char SHELL_SESSION_LINE_FEED_CHARACTER = '\n';
constexpr char SHELL_SESSION_CARRIAGE_RETURN_CHARACTER = '\r';

class ShellSessionStepStatusCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::os::shell::ShellSessionStepStatus status) noexcept
    {
        return SHELL_SESSION_STEP_STATUS_NAMES.contains(status);
    }

    [[nodiscard]] static std::size_t index(const mnos::os::shell::ShellSessionStepStatus status) noexcept
    {
        return SHELL_SESSION_STEP_STATUS_NAMES.index(status);
    }

    [[nodiscard]] static std::string_view name(const mnos::os::shell::ShellSessionStepStatus status) noexcept
    {
        return SHELL_SESSION_STEP_STATUS_NAMES.name(status);
    }

private:
    inline static constexpr auto SHELL_SESSION_STEP_STATUS_NAMES =
        mnos::core::make_enum_name_table<mnos::os::shell::ShellSessionStepStatus>(
            std::array<std::string_view, mnos::os::shell::SHELL_SESSION_STEP_STATUS_COUNT>{
                "BLOCKED",
                "PENDING_INPUT",
                "COMMAND",
                "EXITED",
                "IO_ERROR"},
            SHELL_SESSION_STATUS_NAME_INVALID_TEXT);
};
}

namespace mnos::os::shell
{
bool is_shell_session_step_status_valid(const ShellSessionStepStatus status) noexcept
{
    return ShellSessionStepStatusCatalog::contains(status);
}

std::size_t shell_session_step_status_to_index(const ShellSessionStepStatus status) noexcept
{
    return ShellSessionStepStatusCatalog::index(status);
}

std::string_view shell_session_step_status_to_name(const ShellSessionStepStatus status) noexcept
{
    return ShellSessionStepStatusCatalog::name(status);
}

ShellSessionStepResult ShellSessionStepResult::blocked() noexcept
{
    return ShellSessionStepResult{
        ShellSessionStepStatus::BLOCKED,
        ShellCommandStatus::COUNT,
        io::IoStatus::COUNT};
}

ShellSessionStepResult ShellSessionStepResult::pending_input() noexcept
{
    return ShellSessionStepResult{
        ShellSessionStepStatus::PENDING_INPUT,
        ShellCommandStatus::COUNT,
        io::IoStatus::COUNT};
}

ShellSessionStepResult ShellSessionStepResult::command(const ShellCommandResult command_result) noexcept
{
    return ShellSessionStepResult{
        ShellSessionStepStatus::COMMAND,
        command_result.status(),
        io::IoStatus::COUNT};
}

ShellSessionStepResult ShellSessionStepResult::exited() noexcept
{
    return ShellSessionStepResult{
        ShellSessionStepStatus::EXITED,
        ShellCommandStatus::COUNT,
        io::IoStatus::COUNT};
}

ShellSessionStepResult ShellSessionStepResult::exited(const ShellCommandResult command_result) noexcept
{
    return ShellSessionStepResult{
        ShellSessionStepStatus::EXITED,
        command_result.status(),
        io::IoStatus::COUNT};
}

ShellSessionStepResult ShellSessionStepResult::io_error(const io::IoStatus io_status) noexcept
{
    return ShellSessionStepResult{
        ShellSessionStepStatus::IO_ERROR,
        ShellCommandStatus::COUNT,
        io_status};
}

ShellSessionStepResult::ShellSessionStepResult(
    const ShellSessionStepStatus status,
    const ShellCommandStatus command_status,
    const io::IoStatus io_status) noexcept :
    status_(status),
    command_status_(command_status),
    io_status_(io_status)
{
}

ShellSessionStepStatus ShellSessionStepResult::status() const noexcept
{
    return this->status_;
}

ShellCommandStatus ShellSessionStepResult::command_status() const noexcept
{
    return this->command_status_;
}

io::IoStatus ShellSessionStepResult::io_status() const noexcept
{
    return this->io_status_;
}

bool ShellSessionStepResult::has_command_status() const noexcept
{
    return this->command_status_ != ShellCommandStatus::COUNT;
}

bool ShellSessionStepResult::has_io_status() const noexcept
{
    return this->io_status_ != io::IoStatus::COUNT;
}

bool ShellSessionStepResult::is_blocked() const noexcept
{
    return this->status_ == ShellSessionStepStatus::BLOCKED;
}

bool ShellSessionStepResult::is_exited() const noexcept
{
    return this->status_ == ShellSessionStepStatus::EXITED;
}

ShellSession::ShellSession(
    kernel::Kernel& os_kernel,
    proc::Process& process,
    sched::ThreadContext& thread) noexcept :
    os_kernel_(&os_kernel),
    process_(&process),
    thread_(&thread),
    shell_(os_kernel)
{
}

bool ShellSession::running() const noexcept
{
    return this->shell_.running();
}

bool ShellSession::prompt_pending() const noexcept
{
    return this->prompt_pending_;
}

bool ShellSession::has_pending_input() const noexcept
{
    return !this->pending_line_.empty();
}

std::size_t ShellSession::pending_input_size() const noexcept
{
    return this->pending_line_.size();
}

ShellSessionStepResult ShellSession::poll()
{
    if (!this->shell_.running())
    {
        return ShellSessionStepResult::exited();
    }

    const std::optional<io::IoStatus> prompt_error = this->write_prompt_if_needed();
    if (prompt_error.has_value())
    {
        return ShellSessionStepResult::io_error(prompt_error.value());
    }

    if (this->has_complete_line())
    {
        return this->execute_next_ready_line();
    }

    std::array<char, SHELL_SESSION_READ_BUFFER_SIZE> buffer{};
    const io::IoResult read_result =
        this->os_kernel_->read_fd(*this->process_, *this->thread_, io::FileDescriptor::stdin(), buffer);
    if (read_result.is_blocked())
    {
        return ShellSessionStepResult::blocked();
    }
    if (!read_result.is_ready())
    {
        return ShellSessionStepResult::io_error(read_result.status());
    }

    this->pending_line_.append(buffer.data(), read_result.byte_count());
    if (this->has_complete_line())
    {
        return this->execute_next_ready_line();
    }
    return ShellSessionStepResult::pending_input();
}

std::optional<io::IoStatus> ShellSession::write_prompt_if_needed()
{
    if (!this->prompt_pending_)
    {
        return std::nullopt;
    }

    const io::IoResult write_result =
        this->os_kernel_->write_fd(*this->process_, io::FileDescriptor::stdout(), SHELL_SESSION_DEFAULT_PROMPT);
    if (!write_result.is_ready())
    {
        return write_result.status();
    }

    this->prompt_pending_ = false;
    return std::nullopt;
}

bool ShellSession::has_complete_line() const
{
    return this->next_line_break_index().has_value();
}

std::optional<std::size_t> ShellSession::next_line_break_index() const
{
    const std::size_t line_break_index = this->pending_line_.find(SHELL_SESSION_LINE_FEED_CHARACTER);
    if (line_break_index == std::string::npos)
    {
        return std::nullopt;
    }
    return line_break_index;
}

ShellSessionStepResult ShellSession::execute_next_ready_line()
{
    const std::optional<std::size_t> line_break_index = this->next_line_break_index();
    if (!line_break_index.has_value())
    {
        return ShellSessionStepResult::pending_input();
    }

    std::size_t line_length = line_break_index.value();
    if (line_length != std::size_t{0} &&
        this->pending_line_[line_length - std::size_t{1}] == SHELL_SESSION_CARRIAGE_RETURN_CHARACTER)
    {
        --line_length;
    }

    const std::string_view line{this->pending_line_.data(), line_length};
    const ShellCommandResult command_result = this->shell_.execute_line(line);
    this->pending_line_.erase(std::size_t{0}, line_break_index.value() + std::size_t{1});

    if (!this->shell_.running())
    {
        this->prompt_pending_ = false;
        return ShellSessionStepResult::exited(command_result);
    }

    this->prompt_pending_ = true;
    return ShellSessionStepResult::command(command_result);
}
}
