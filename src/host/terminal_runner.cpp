#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <utility>

#include <mnos/host/machine_session.hpp>
#include <mnos/host/terminal_backend.hpp>
#include <mnos/host/terminal_runner.hpp>
#include <mnos/os/io/file_descriptor.hpp>

namespace
{
[[nodiscard]] mnos::host::TerminalRunResult result_from_finished_drive(
    const mnos::host::HostMachineSession& session,
    const std::size_t render_count)
{
    using Result = mnos::host::TerminalRunResult;

    if (session.status() == mnos::host::HostMachineSessionStatus::EXITED)
    {
        return Result::exited(session.command_count(), session.poll_count(), render_count);
    }

    if (session.status() == mnos::host::HostMachineSessionStatus::SHELL_IO_ERROR)
    {
        return Result::shell_io_error(
            session.shell_io_status(),
            session.command_count(),
            session.poll_count(),
            render_count);
    }

    return Result::host_io_error(session.command_count(), session.poll_count(), render_count);
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
    StreamTerminalBackend backend{input, output, this->config_.render_mode, this->config_.input_mode};
    return this->run(backend);
}

TerminalRunResult TerminalRunner::run(HostTerminalBackend& backend) const
{
    HostMachineSessionConfig session_config;
    session_config.physical_memory_size_bytes = this->config_.physical_memory_size_bytes;
    session_config.processor_count = this->config_.processor_count;
    HostMachineSession session{session_config};
    session.boot();
    if (!backend.render_terminal(session.terminal_device()))
    {
        return TerminalRunResult::host_io_error(
            session.command_count(),
            session.poll_count(),
            backend.render_count());
    }
    if (!session.waiting_for_input())
    {
        return result_from_finished_drive(session, backend.render_count());
    }

    for (;;)
    {
        const HostInputEvent event = backend.read_input_event();
        if (event.kind() == HostInputEventKind::HOST_IO_ERROR)
        {
            return TerminalRunResult::host_io_error(
                session.command_count(),
                session.poll_count(),
                backend.render_count());
        }
        if (event.kind() == HostInputEventKind::INPUT_CLOSED)
        {
            return TerminalRunResult::input_closed(
                session.command_count(),
                session.poll_count(),
                backend.render_count());
        }

        const std::string terminal_input = host_input_event_to_terminal_input(event);
        if (terminal_input.empty())
        {
            continue;
        }

        static_cast<void>(session.submit_input(terminal_input));
        if (!backend.render_terminal(session.terminal_device()))
        {
            return TerminalRunResult::host_io_error(
                session.command_count(),
                session.poll_count(),
                backend.render_count());
        }
        if (!session.waiting_for_input())
        {
            return result_from_finished_drive(session, backend.render_count());
        }
    }
}
}
