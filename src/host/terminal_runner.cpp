#include <algorithm>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mnos/host/terminal_runner.hpp>
#include <mnos/os/dev/terminal.hpp>
#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <mnos/os/shell/session.hpp>

namespace dev = mnos::os::dev;
namespace kernel = mnos::os::kernel;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;
namespace shell = mnos::os::shell;

namespace
{
constexpr std::string_view HOST_TERMINAL_ANSI_CLEAR_SCREEN = "\x1B[2J";
constexpr std::string_view HOST_TERMINAL_ANSI_CURSOR_HOME = "\x1B[H";
constexpr std::string_view HOST_TERMINAL_BACKSPACE_ERASE = "\b \b";
constexpr char HOST_TERMINAL_NEWLINE_CHARACTER = '\n';
constexpr char HOST_TERMINAL_CARRIAGE_RETURN_CHARACTER = '\r';
constexpr char HOST_TERMINAL_BLANK_CHARACTER = ' ';

enum class ShellDriveStatus : std::uint8_t
{
    WAITING_FOR_INPUT,
    EXITED,
    SHELL_IO_ERROR,
    HOST_IO_ERROR
};

struct ShellDriveResult final
{
    ShellDriveStatus status;
    mnos::os::io::IoStatus shell_io_status;
    std::size_t command_count;
    std::size_t poll_count;
    std::size_t render_count;
};

[[nodiscard]] bool terminal_render_mode_is_screen(const mnos::host::TerminalRenderMode mode) noexcept
{
    return mode == mnos::host::TerminalRenderMode::ANSI_SCREEN ||
        mode == mnos::host::TerminalRenderMode::PLAIN_SCREEN;
}

[[nodiscard]] bool terminal_render_mode_uses_ansi(const mnos::host::TerminalRenderMode mode) noexcept
{
    return mode == mnos::host::TerminalRenderMode::ANSI_STREAM ||
        mode == mnos::host::TerminalRenderMode::ANSI_SCREEN;
}

[[nodiscard]] std::size_t trimmed_line_size(
    const std::string_view line,
    const std::size_t cursor_column,
    const bool is_cursor_row) noexcept
{
    std::size_t line_size = line.size();
    while (line_size > std::size_t{0} && line[line_size - std::size_t{1}] == HOST_TERMINAL_BLANK_CHARACTER)
    {
        --line_size;
    }

    if (is_cursor_row)
    {
        line_size = std::max(line_size, std::min(cursor_column, line.size()));
    }
    return line_size;
}

[[nodiscard]] std::string render_display_for_host(const dev::TextDisplayBuffer& display)
{
    const std::vector<std::string> raw_lines = display.lines();
    std::vector<std::string> trimmed_lines;
    trimmed_lines.reserve(raw_lines.size());

    std::size_t last_visible_row = std::size_t{0};
    bool has_visible_row = false;
    for (std::size_t row = std::size_t{0}; row < raw_lines.size(); ++row)
    {
        const bool is_cursor_row = row == display.cursor_row();
        const std::size_t line_size = trimmed_line_size(raw_lines[row], display.cursor_column(), is_cursor_row);
        trimmed_lines.emplace_back(raw_lines[row].substr(std::size_t{0}, line_size));
        if (line_size != std::size_t{0})
        {
            last_visible_row = row;
            has_visible_row = true;
        }
    }

    if (!has_visible_row)
    {
        return {};
    }

    std::string rendered;
    for (std::size_t row = std::size_t{0}; row <= last_visible_row; ++row)
    {
        if (row != std::size_t{0})
        {
            rendered.push_back(HOST_TERMINAL_NEWLINE_CHARACTER);
        }
        rendered.append(trimmed_lines[row]);
    }
    return rendered;
}

[[nodiscard]] std::string render_output_stream_for_host(
    const std::string_view output_stream,
    const mnos::host::TerminalRenderMode mode)
{
    std::string rendered;
    rendered.reserve(output_stream.size());
    for (const char character : output_stream)
    {
        if (character == dev::TERMINAL_CLEAR_SCREEN_CHARACTER)
        {
            if (terminal_render_mode_uses_ansi(mode))
            {
                rendered.append(HOST_TERMINAL_ANSI_CLEAR_SCREEN);
                rendered.append(HOST_TERMINAL_ANSI_CURSOR_HOME);
            }
            continue;
        }
        if (character == dev::TERMINAL_BACKSPACE_CHARACTER || character == dev::TERMINAL_DELETE_CHARACTER)
        {
            rendered.append(HOST_TERMINAL_BACKSPACE_ERASE);
            continue;
        }
        rendered.push_back(character);
    }
    return rendered;
}

class TerminalStreamRenderer final
{
public:
    explicit TerminalStreamRenderer(const mnos::host::TerminalRenderMode mode) noexcept : mode_(mode)
    {
    }

    [[nodiscard]] bool render_if_changed(dev::TerminalDevice& terminal, std::ostream& output)
    {
        const std::string_view pending_output = terminal.output_stream_since(std::size_t{0});
        if (pending_output.empty())
        {
            return true;
        }

        const std::string rendered = render_output_stream_for_host(pending_output, this->mode_);
        if (!rendered.empty())
        {
            output << rendered;
            output.flush();
            if (!output.good())
            {
                return false;
            }
            ++this->render_count_;
        }
        terminal.discard_output_stream_before(pending_output.size());
        return true;
    }

    [[nodiscard]] std::size_t render_count() const noexcept
    {
        return this->render_count_;
    }

private:
    mnos::host::TerminalRenderMode mode_;
    std::size_t render_count_ = std::size_t{0};
};

class TerminalScreenRenderer final
{
public:
    explicit TerminalScreenRenderer(const mnos::host::TerminalRenderMode mode) noexcept : mode_(mode)
    {
    }

    [[nodiscard]] bool render_if_changed(const dev::TextDisplayBuffer& display, std::ostream& output)
    {
        const std::string rendered = render_display_for_host(display);
        if (rendered == this->last_frame_)
        {
            return true;
        }

        this->last_frame_ = rendered;
        if (terminal_render_mode_uses_ansi(this->mode_))
        {
            output << HOST_TERMINAL_ANSI_CLEAR_SCREEN << HOST_TERMINAL_ANSI_CURSOR_HOME << rendered;
            output.flush();
        }
        else
        {
            output << rendered << HOST_TERMINAL_NEWLINE_CHARACTER;
        }

        if (!output.good())
        {
            return false;
        }

        ++this->render_count_;
        return true;
    }

    [[nodiscard]] std::size_t render_count() const noexcept
    {
        return this->render_count_;
    }

private:
    mnos::host::TerminalRenderMode mode_;
    std::string last_frame_;
    std::size_t render_count_ = std::size_t{0};
};

class TerminalHostRenderer final
{
public:
    explicit TerminalHostRenderer(const mnos::host::TerminalRenderMode mode) noexcept :
        mode_(mode),
        stream_renderer_(mode),
        screen_renderer_(mode)
    {
    }

    [[nodiscard]] bool render_if_changed(dev::TerminalDevice& terminal, std::ostream& output)
    {
        if (terminal_render_mode_is_screen(this->mode_))
        {
            return this->screen_renderer_.render_if_changed(terminal.display(), output);
        }
        return this->stream_renderer_.render_if_changed(terminal, output);
    }

    [[nodiscard]] std::size_t render_count() const noexcept
    {
        if (terminal_render_mode_is_screen(this->mode_))
        {
            return this->screen_renderer_.render_count();
        }
        return this->stream_renderer_.render_count();
    }

private:
    mnos::host::TerminalRenderMode mode_;
    TerminalStreamRenderer stream_renderer_;
    TerminalScreenRenderer screen_renderer_;
};

[[nodiscard]] bool shell_step_executed_command(const shell::ShellSessionStepResult& step) noexcept
{
    return step.status() == shell::ShellSessionStepStatus::COMMAND ||
        (step.status() == shell::ShellSessionStepStatus::EXITED && step.has_command_status());
}

[[nodiscard]] ShellDriveResult make_shell_drive_result(
    const ShellDriveStatus status,
    const mnos::os::io::IoStatus shell_io_status,
    const std::size_t command_count,
    const std::size_t poll_count,
    const TerminalHostRenderer& renderer) noexcept
{
    return ShellDriveResult{
        status,
        shell_io_status,
        command_count,
        poll_count,
        renderer.render_count()};
}

[[nodiscard]] ShellDriveResult drive_shell_until_waiting(
    shell::ShellSession& session,
    platform::Machine& machine,
    TerminalHostRenderer& renderer,
    std::ostream& output)
{
    std::size_t command_count = std::size_t{0};
    std::size_t poll_count = std::size_t{0};

    for (;;)
    {
        const shell::ShellSessionStepResult step = session.poll();
        ++poll_count;
        if (shell_step_executed_command(step))
        {
            ++command_count;
        }

        if (!renderer.render_if_changed(machine.terminal_device(), output))
        {
            return make_shell_drive_result(
                ShellDriveStatus::HOST_IO_ERROR,
                mnos::os::io::IoStatus::COUNT,
                command_count,
                poll_count,
                renderer);
        }

        switch (step.status())
        {
        case shell::ShellSessionStepStatus::BLOCKED:
            return make_shell_drive_result(
                ShellDriveStatus::WAITING_FOR_INPUT,
                mnos::os::io::IoStatus::COUNT,
                command_count,
                poll_count,
                renderer);
        case shell::ShellSessionStepStatus::PENDING_INPUT:
        case shell::ShellSessionStepStatus::COMMAND:
            break;
        case shell::ShellSessionStepStatus::EXITED:
            return make_shell_drive_result(
                ShellDriveStatus::EXITED,
                mnos::os::io::IoStatus::COUNT,
                command_count,
                poll_count,
                renderer);
        case shell::ShellSessionStepStatus::IO_ERROR:
            return make_shell_drive_result(
                ShellDriveStatus::SHELL_IO_ERROR,
                step.io_status(),
                command_count,
                poll_count,
                renderer);
        case shell::ShellSessionStepStatus::COUNT:
            return make_shell_drive_result(
                ShellDriveStatus::SHELL_IO_ERROR,
                mnos::os::io::IoStatus::COUNT,
                command_count,
                poll_count,
                renderer);
        }
    }
}

void schedule_shell_thread(kernel::Kernel& os_kernel, sched::ThreadContext& shell_thread)
{
    if (os_kernel.scheduler().has_current() && &os_kernel.scheduler().current() == &shell_thread)
    {
        return;
    }
    static_cast<void>(os_kernel.scheduler().schedule_next());
}

void append_host_line_ending(std::string& line)
{
    if (!line.empty() && line.back() == HOST_TERMINAL_CARRIAGE_RETURN_CHARACTER)
    {
        line.pop_back();
    }
    line.push_back(HOST_TERMINAL_NEWLINE_CHARACTER);
}

[[nodiscard]] mnos::host::TerminalRunResult result_from_drive(
    const ShellDriveResult& drive_result,
    const std::size_t total_command_count,
    const std::size_t total_poll_count)
{
    switch (drive_result.status)
    {
    case ShellDriveStatus::EXITED:
        return mnos::host::TerminalRunResult::exited(
            total_command_count,
            total_poll_count,
            drive_result.render_count);
    case ShellDriveStatus::SHELL_IO_ERROR:
        return mnos::host::TerminalRunResult::shell_io_error(
            drive_result.shell_io_status,
            total_command_count,
            total_poll_count,
            drive_result.render_count);
    case ShellDriveStatus::HOST_IO_ERROR:
        return mnos::host::TerminalRunResult::host_io_error(
            total_command_count,
            total_poll_count,
            drive_result.render_count);
    case ShellDriveStatus::WAITING_FOR_INPUT:
        break;
    }

    return mnos::host::TerminalRunResult::input_closed(
        total_command_count,
        total_poll_count,
        drive_result.render_count);
}
}

namespace mnos::host
{
TerminalRunResult TerminalRunResult::exited(
    const std::size_t command_count,
    const std::size_t poll_count,
    const std::size_t render_count) noexcept
{
    return TerminalRunResult{
        TerminalRunStatus::EXITED,
        os::io::IoStatus::COUNT,
        false,
        command_count,
        poll_count,
        render_count};
}

TerminalRunResult TerminalRunResult::input_closed(
    const std::size_t command_count,
    const std::size_t poll_count,
    const std::size_t render_count) noexcept
{
    return TerminalRunResult{
        TerminalRunStatus::INPUT_CLOSED,
        os::io::IoStatus::COUNT,
        false,
        command_count,
        poll_count,
        render_count};
}

TerminalRunResult TerminalRunResult::shell_io_error(
    const os::io::IoStatus io_status,
    const std::size_t command_count,
    const std::size_t poll_count,
    const std::size_t render_count) noexcept
{
    return TerminalRunResult{
        TerminalRunStatus::SHELL_IO_ERROR,
        io_status,
        true,
        command_count,
        poll_count,
        render_count};
}

TerminalRunResult TerminalRunResult::host_io_error(
    const std::size_t command_count,
    const std::size_t poll_count,
    const std::size_t render_count) noexcept
{
    return TerminalRunResult{
        TerminalRunStatus::HOST_IO_ERROR,
        os::io::IoStatus::COUNT,
        false,
        command_count,
        poll_count,
        render_count};
}

TerminalRunResult::TerminalRunResult(
    const TerminalRunStatus status,
    const os::io::IoStatus shell_io_status,
    const bool has_shell_io_status,
    const std::size_t command_count,
    const std::size_t poll_count,
    const std::size_t render_count) noexcept :
    status_(status),
    shell_io_status_(shell_io_status),
    has_shell_io_status_(has_shell_io_status),
    command_count_(command_count),
    poll_count_(poll_count),
    render_count_(render_count)
{
}

TerminalRunStatus TerminalRunResult::status() const noexcept
{
    return this->status_;
}

bool TerminalRunResult::completed() const noexcept
{
    return this->status_ == TerminalRunStatus::EXITED;
}

bool TerminalRunResult::has_shell_io_status() const noexcept
{
    return this->has_shell_io_status_;
}

os::io::IoStatus TerminalRunResult::shell_io_status() const noexcept
{
    return this->shell_io_status_;
}

std::size_t TerminalRunResult::command_count() const noexcept
{
    return this->command_count_;
}

std::size_t TerminalRunResult::poll_count() const noexcept
{
    return this->poll_count_;
}

std::size_t TerminalRunResult::render_count() const noexcept
{
    return this->render_count_;
}

TerminalRunner::TerminalRunner(TerminalRunnerConfig config) noexcept : config_(std::move(config))
{
}

const TerminalRunnerConfig& TerminalRunner::config() const noexcept
{
    return this->config_;
}

TerminalRunResult TerminalRunner::run(std::istream& input, std::ostream& output) const
{
    platform::Machine machine{this->config_.physical_memory_size_bytes, this->config_.processor_count};
    kernel::BootContext boot_context{machine, this->config_.processor_count};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    proc::Process& shell_process = os_kernel.create_process();
    sched::ThreadContext& shell_thread = os_kernel.create_thread(shell_process);
    shell::ShellSession session{os_kernel, shell_process, shell_thread};
    schedule_shell_thread(os_kernel, shell_thread);

    TerminalHostRenderer renderer{this->config_.render_mode};
    std::size_t total_command_count = std::size_t{0};
    std::size_t total_poll_count = std::size_t{0};

    ShellDriveResult drive_result = drive_shell_until_waiting(session, machine, renderer, output);
    total_command_count += drive_result.command_count;
    total_poll_count += drive_result.poll_count;
    if (drive_result.status != ShellDriveStatus::WAITING_FOR_INPUT)
    {
        return result_from_drive(drive_result, total_command_count, total_poll_count);
    }

    std::string line;
    while (std::getline(input, line))
    {
        append_host_line_ending(line);
        static_cast<void>(os_kernel.submit_terminal_input(line));
        schedule_shell_thread(os_kernel, shell_thread);

        drive_result = drive_shell_until_waiting(session, machine, renderer, output);
        total_command_count += drive_result.command_count;
        total_poll_count += drive_result.poll_count;
        if (drive_result.status != ShellDriveStatus::WAITING_FOR_INPUT)
        {
            return result_from_drive(drive_result, total_command_count, total_poll_count);
        }
    }

    if (input.bad())
    {
        return TerminalRunResult::host_io_error(
            total_command_count,
            total_poll_count,
            renderer.render_count());
    }

    return TerminalRunResult::input_closed(
        total_command_count,
        total_poll_count,
        renderer.render_count());
}
}
