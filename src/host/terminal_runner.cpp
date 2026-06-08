#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <utility>

#include <mnos/host/terminal_backend.hpp>
#include <mnos/host/terminal_runner.hpp>
#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <mnos/os/shell/session.hpp>

namespace kernel = mnos::os::kernel;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;
namespace shell = mnos::os::shell;

namespace
{
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
    const mnos::host::HostTerminalBackend& backend) noexcept
{
    return ShellDriveResult{
        status,
        shell_io_status,
        command_count,
        poll_count,
        backend.render_count()};
}

[[nodiscard]] ShellDriveResult drive_shell_until_waiting(
    shell::ShellSession& session,
    platform::Machine& machine,
    mnos::host::HostTerminalBackend& backend)
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

        if (!backend.render_terminal(machine.terminal_device()))
        {
            return make_shell_drive_result(
                ShellDriveStatus::HOST_IO_ERROR,
                mnos::os::io::IoStatus::COUNT,
                command_count,
                poll_count,
                backend);
        }

        switch (step.status())
        {
        case shell::ShellSessionStepStatus::BLOCKED:
            return make_shell_drive_result(
                ShellDriveStatus::WAITING_FOR_INPUT,
                mnos::os::io::IoStatus::COUNT,
                command_count,
                poll_count,
                backend);
        case shell::ShellSessionStepStatus::PENDING_INPUT:
        case shell::ShellSessionStepStatus::COMMAND:
            break;
        case shell::ShellSessionStepStatus::EXITED:
            return make_shell_drive_result(
                ShellDriveStatus::EXITED,
                mnos::os::io::IoStatus::COUNT,
                command_count,
                poll_count,
                backend);
        case shell::ShellSessionStepStatus::IO_ERROR:
            return make_shell_drive_result(
                ShellDriveStatus::SHELL_IO_ERROR,
                step.io_status(),
                command_count,
                poll_count,
                backend);
        case shell::ShellSessionStepStatus::COUNT:
            return make_shell_drive_result(
                ShellDriveStatus::SHELL_IO_ERROR,
                mnos::os::io::IoStatus::COUNT,
                command_count,
                poll_count,
                backend);
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

[[nodiscard]] mnos::host::TerminalRunResult result_from_finished_drive(
    const ShellDriveResult& drive_result,
    const std::size_t total_command_count,
    const std::size_t total_poll_count)
{
    using Result = mnos::host::TerminalRunResult;

    if (drive_result.status == ShellDriveStatus::EXITED)
    {
        return Result::exited(total_command_count, total_poll_count, drive_result.render_count);
    }

    if (drive_result.status == ShellDriveStatus::SHELL_IO_ERROR)
    {
        return Result::shell_io_error(
            drive_result.shell_io_status,
            total_command_count,
            total_poll_count,
            drive_result.render_count);
    }

    return Result::host_io_error(total_command_count, total_poll_count, drive_result.render_count);
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
    StreamTerminalBackend backend{input, output, this->config_.render_mode};
    return this->run(backend);
}

TerminalRunResult TerminalRunner::run(HostTerminalBackend& backend) const
{
    platform::Machine machine{this->config_.physical_memory_size_bytes, this->config_.processor_count};
    kernel::BootContext boot_context{machine, this->config_.processor_count};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    proc::Process& shell_process = os_kernel.create_process();
    sched::ThreadContext& shell_thread = os_kernel.create_thread(shell_process);
    shell::ShellSession session{os_kernel, shell_process, shell_thread};
    schedule_shell_thread(os_kernel, shell_thread);

    std::size_t total_command_count = std::size_t{0};
    std::size_t total_poll_count = std::size_t{0};

    ShellDriveResult drive_result = drive_shell_until_waiting(session, machine, backend);
    total_command_count += drive_result.command_count;
    total_poll_count += drive_result.poll_count;
    if (drive_result.status != ShellDriveStatus::WAITING_FOR_INPUT)
    {
        return result_from_finished_drive(drive_result, total_command_count, total_poll_count);
    }

    for (;;)
    {
        const HostInputEvent event = backend.read_input_event();
        if (event.kind() == HostInputEventKind::HOST_IO_ERROR)
        {
            return TerminalRunResult::host_io_error(
                total_command_count,
                total_poll_count,
                backend.render_count());
        }
        if (event.kind() == HostInputEventKind::INPUT_CLOSED)
        {
            return TerminalRunResult::input_closed(
                total_command_count,
                total_poll_count,
                backend.render_count());
        }

        const std::string terminal_input = host_input_event_to_terminal_input(event);
        if (terminal_input.empty())
        {
            continue;
        }

        static_cast<void>(os_kernel.submit_terminal_input(terminal_input));
        schedule_shell_thread(os_kernel, shell_thread);

        drive_result = drive_shell_until_waiting(session, machine, backend);
        total_command_count += drive_result.command_count;
        total_poll_count += drive_result.poll_count;
        if (drive_result.status != ShellDriveStatus::WAITING_FOR_INPUT)
        {
            return result_from_finished_drive(drive_result, total_command_count, total_poll_count);
        }
    }
}
}
