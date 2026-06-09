#include <mnos/host/machine_session.hpp>

#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <mnos/core/enum_map.hpp>
#include <mnos/os/dev/terminal.hpp>
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
constexpr std::string_view HOST_MACHINE_SESSION_INVALID_ENUM_NAME = "<invalid>";
constexpr const char* HOST_MACHINE_SESSION_NOT_BOOTED_ERROR = "host machine session is not booted";

class HostMachineSessionStatusCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::host::HostMachineSessionStatus status) noexcept
    {
        return HOST_MACHINE_SESSION_STATUS_NAMES.contains(status);
    }

    [[nodiscard]] static std::size_t index(const mnos::host::HostMachineSessionStatus status) noexcept
    {
        return HOST_MACHINE_SESSION_STATUS_NAMES.index(status);
    }

    [[nodiscard]] static std::string_view name(const mnos::host::HostMachineSessionStatus status) noexcept
    {
        return HOST_MACHINE_SESSION_STATUS_NAMES.name(status);
    }

private:
    inline static constexpr auto HOST_MACHINE_SESSION_STATUS_NAMES =
        mnos::core::make_enum_name_table<mnos::host::HostMachineSessionStatus>(
            std::array<std::string_view, mnos::host::HOST_MACHINE_SESSION_STATUS_COUNT>{
                "CREATED",
                "WAITING_FOR_INPUT",
                "EXITED",
                "SHELL_IO_ERROR"},
            HOST_MACHINE_SESSION_INVALID_ENUM_NAME);
};

[[nodiscard]] bool shell_step_executed_command(const shell::ShellSessionStepResult& step) noexcept
{
    return step.status() == shell::ShellSessionStepStatus::COMMAND ||
        (step.status() == shell::ShellSessionStepStatus::EXITED && step.has_command_status());
}
}

namespace mnos::host
{
struct HostMachineSession::SessionState final
{
    explicit SessionState(const HostMachineSessionConfig& config) :
        machine(config.physical_memory_size_bytes, config.processor_count),
        boot_context(machine, config.processor_count),
        os_kernel(boot_context)
    {
    }

    platform::Machine machine;
    kernel::BootContext boot_context;
    kernel::Kernel os_kernel;
    proc::Process* shell_process = nullptr;
    sched::ThreadContext* shell_thread = nullptr;
    std::optional<shell::ShellSession> shell_session;
};

bool is_host_machine_session_status_valid(const HostMachineSessionStatus status) noexcept
{
    return HostMachineSessionStatusCatalog::contains(status);
}

std::size_t host_machine_session_status_to_index(const HostMachineSessionStatus status) noexcept
{
    return HostMachineSessionStatusCatalog::index(status);
}

std::string_view host_machine_session_status_to_name(const HostMachineSessionStatus status) noexcept
{
    return HostMachineSessionStatusCatalog::name(status);
}

HostMachineSession::HostMachineSession(HostMachineSessionConfig config) noexcept : config_(std::move(config))
{
}

HostMachineSession::HostMachineSession(HostMachineSession&&) noexcept = default;

HostMachineSession& HostMachineSession::operator=(HostMachineSession&&) noexcept = default;

HostMachineSession::~HostMachineSession() = default;

const HostMachineSessionConfig& HostMachineSession::config() const noexcept
{
    return this->config_;
}

void HostMachineSession::boot()
{
    std::unique_ptr<SessionState> next_state = std::make_unique<SessionState>(this->config_);
    next_state->os_kernel.boot();
    next_state->shell_process = &next_state->os_kernel.create_process();
    next_state->shell_thread = &next_state->os_kernel.create_thread(*next_state->shell_process);
    next_state->shell_session.emplace(
        next_state->os_kernel,
        *next_state->shell_process,
        *next_state->shell_thread);

    this->state_ = std::move(next_state);
    this->clear_runtime_status();
    this->schedule_shell_thread();
    static_cast<void>(this->pump_until_waiting());
}

void HostMachineSession::reset()
{
    this->boot();
}

bool HostMachineSession::booted() const noexcept
{
    return this->state_ != nullptr;
}

HostMachineSessionStatus HostMachineSession::status() const noexcept
{
    return this->status_;
}

bool HostMachineSession::waiting_for_input() const noexcept
{
    return this->status_ == HostMachineSessionStatus::WAITING_FOR_INPUT;
}

bool HostMachineSession::completed() const noexcept
{
    return this->status_ == HostMachineSessionStatus::EXITED;
}

bool HostMachineSession::has_shell_io_status() const noexcept
{
    return this->has_shell_io_status_;
}

os::io::IoStatus HostMachineSession::shell_io_status() const noexcept
{
    return this->shell_io_status_;
}

std::size_t HostMachineSession::command_count() const noexcept
{
    return this->command_count_;
}

std::size_t HostMachineSession::poll_count() const noexcept
{
    return this->poll_count_;
}

HostMachineSessionSnapshot HostMachineSession::snapshot() const
{
    HostMachineSessionSnapshot result{
        this->status_,
        this->shell_io_status_,
        this->has_shell_io_status_,
        this->command_count_,
        this->poll_count_};
    if (!this->booted())
    {
        result.physical_memory_size_bytes = this->config_.physical_memory_size_bytes;
        result.processor_count = this->config_.processor_count;
        return result;
    }

    const SessionState& state = this->require_state();
    result.process_count = state.os_kernel.process_count();
    result.terminal_output_stream_size = state.machine.terminal_device().output_stream_size();
    result.physical_memory_size_bytes = state.os_kernel.physical_memory_size_bytes();
    result.physical_page_count = state.os_kernel.physical_page_allocator().total_page_count();
    result.free_page_count = state.os_kernel.physical_page_allocator().free_page_count();
    result.allocated_page_count = state.os_kernel.physical_page_allocator().allocated_page_count();
    result.processor_count = state.os_kernel.bootstrap_processor_count();
    return result;
}

platform::Machine& HostMachineSession::machine()
{
    return this->require_state().machine;
}

const platform::Machine& HostMachineSession::machine() const
{
    return this->require_state().machine;
}

kernel::Kernel& HostMachineSession::kernel()
{
    return this->require_state().os_kernel;
}

const kernel::Kernel& HostMachineSession::kernel() const
{
    return this->require_state().os_kernel;
}

os::dev::TerminalDevice& HostMachineSession::terminal_device()
{
    return this->require_state().machine.terminal_device();
}

const os::dev::TerminalDevice& HostMachineSession::terminal_device() const
{
    return this->require_state().machine.terminal_device();
}

proc::Process& HostMachineSession::shell_process()
{
    SessionState& state = this->require_state();
    return *state.shell_process;
}

const proc::Process& HostMachineSession::shell_process() const
{
    const SessionState& state = this->require_state();
    return *state.shell_process;
}

sched::ThreadContext& HostMachineSession::shell_thread()
{
    SessionState& state = this->require_state();
    return *state.shell_thread;
}

const sched::ThreadContext& HostMachineSession::shell_thread() const
{
    const SessionState& state = this->require_state();
    return *state.shell_thread;
}

HostMachineSessionStatus HostMachineSession::pump_until_waiting()
{
    SessionState& state = this->require_state();
    if (this->status_ == HostMachineSessionStatus::EXITED ||
        this->status_ == HostMachineSessionStatus::SHELL_IO_ERROR)
    {
        return this->status_;
    }

    for (;;)
    {
        const shell::ShellSessionStepResult step = state.shell_session->poll();
        ++this->poll_count_;
        if (shell_step_executed_command(step))
        {
            ++this->command_count_;
        }

        switch (step.status())
        {
        case shell::ShellSessionStepStatus::BLOCKED:
            this->status_ = HostMachineSessionStatus::WAITING_FOR_INPUT;
            return this->status_;
        case shell::ShellSessionStepStatus::PENDING_INPUT:
        case shell::ShellSessionStepStatus::COMMAND:
            break;
        case shell::ShellSessionStepStatus::EXITED:
            this->status_ = HostMachineSessionStatus::EXITED;
            return this->status_;
        case shell::ShellSessionStepStatus::IO_ERROR:
            this->record_shell_io_error(step.io_status());
            return this->status_;
        case shell::ShellSessionStepStatus::COUNT:
            this->record_shell_io_error(os::io::IoStatus::COUNT);
            return this->status_;
        }
    }
}

HostMachineSessionStatus HostMachineSession::submit_input(const std::string_view text)
{
    if (text.empty() ||
        this->status_ == HostMachineSessionStatus::EXITED ||
        this->status_ == HostMachineSessionStatus::SHELL_IO_ERROR)
    {
        return this->status_;
    }

    SessionState& state = this->require_state();
    static_cast<void>(state.os_kernel.submit_terminal_input(text));
    this->schedule_shell_thread();
    return this->pump_until_waiting();
}

HostMachineSessionStatus HostMachineSession::submit_input_event(const HostInputEvent& event)
{
    const std::string terminal_input = host_input_event_to_terminal_input(event);
    if (terminal_input.empty())
    {
        return this->status_;
    }
    return this->submit_input(terminal_input);
}

HostMachineSession::SessionState& HostMachineSession::require_state()
{
    if (!this->state_)
    {
        throw std::logic_error{HOST_MACHINE_SESSION_NOT_BOOTED_ERROR};
    }
    return *this->state_;
}

const HostMachineSession::SessionState& HostMachineSession::require_state() const
{
    if (!this->state_)
    {
        throw std::logic_error{HOST_MACHINE_SESSION_NOT_BOOTED_ERROR};
    }
    return *this->state_;
}

void HostMachineSession::clear_runtime_status() noexcept
{
    this->status_ = HostMachineSessionStatus::WAITING_FOR_INPUT;
    this->shell_io_status_ = os::io::IoStatus::COUNT;
    this->has_shell_io_status_ = false;
    this->command_count_ = std::size_t{0};
    this->poll_count_ = std::size_t{0};
}

void HostMachineSession::schedule_shell_thread()
{
    SessionState& state = this->require_state();
    if (state.os_kernel.scheduler().has_current() &&
        &state.os_kernel.scheduler().current() == state.shell_thread)
    {
        return;
    }
    static_cast<void>(state.os_kernel.scheduler().schedule_next());
}

void HostMachineSession::record_shell_io_error(const os::io::IoStatus io_status) noexcept
{
    this->status_ = HostMachineSessionStatus::SHELL_IO_ERROR;
    this->shell_io_status_ = io_status;
    this->has_shell_io_status_ = true;
}
}
