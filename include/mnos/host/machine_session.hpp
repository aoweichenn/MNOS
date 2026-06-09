#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include <mnos/host/terminal_backend.hpp>
#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/mm/page.hpp>

namespace mnos::os::dev
{
class TerminalDevice;
}

namespace mnos::os::kernel
{
class Kernel;
}

namespace mnos::os::platform
{
class Machine;
}

namespace mnos::os::proc
{
class Process;
}

namespace mnos::os::sched
{
class ThreadContext;
}

namespace mnos::host
{
inline constexpr mnos::os::mm::AddressValue HOST_MACHINE_SESSION_DEFAULT_MEMORY_PAGE_COUNT =
    mnos::os::mm::AddressValue{512};
inline constexpr std::size_t HOST_MACHINE_SESSION_DEFAULT_MEMORY_SIZE_BYTES =
    static_cast<std::size_t>(
        mnos::os::mm::MM_PAGE_SIZE_BYTES * HOST_MACHINE_SESSION_DEFAULT_MEMORY_PAGE_COUNT);
inline constexpr std::uint32_t HOST_MACHINE_SESSION_DEFAULT_PROCESSOR_COUNT = std::uint32_t{2};

enum class HostMachineSessionStatus : std::uint8_t
{
    CREATED,
    WAITING_FOR_INPUT,
    EXITED,
    SHELL_IO_ERROR,
    COUNT
};

inline constexpr std::size_t HOST_MACHINE_SESSION_STATUS_COUNT =
    static_cast<std::size_t>(HostMachineSessionStatus::COUNT);

[[nodiscard]] bool is_host_machine_session_status_valid(HostMachineSessionStatus status) noexcept;
[[nodiscard]] std::size_t host_machine_session_status_to_index(HostMachineSessionStatus status) noexcept;
[[nodiscard]] std::string_view host_machine_session_status_to_name(HostMachineSessionStatus status) noexcept;

struct HostMachineSessionConfig final
{
    std::size_t physical_memory_size_bytes = HOST_MACHINE_SESSION_DEFAULT_MEMORY_SIZE_BYTES;
    std::uint32_t processor_count = HOST_MACHINE_SESSION_DEFAULT_PROCESSOR_COUNT;
};

struct HostMachineSessionSnapshot final
{
    HostMachineSessionStatus status = HostMachineSessionStatus::CREATED;
    os::io::IoStatus shell_io_status = os::io::IoStatus::COUNT;
    bool has_shell_io_status = false;
    std::size_t command_count = std::size_t{0};
    std::size_t poll_count = std::size_t{0};
    std::size_t process_count = std::size_t{0};
    std::size_t terminal_output_stream_size = std::size_t{0};
    std::size_t physical_memory_size_bytes = std::size_t{0};
    os::mm::PageNumber physical_page_count = os::mm::PageNumber{0};
    os::mm::PageNumber free_page_count = os::mm::PageNumber{0};
    os::mm::PageNumber allocated_page_count = os::mm::PageNumber{0};
    std::uint32_t processor_count = std::uint32_t{0};
};

class HostMachineSession final
{
public:
    explicit HostMachineSession(HostMachineSessionConfig config = {}) noexcept;
    HostMachineSession(const HostMachineSession&) = delete;
    HostMachineSession& operator=(const HostMachineSession&) = delete;
    HostMachineSession(HostMachineSession&&) noexcept;
    HostMachineSession& operator=(HostMachineSession&&) noexcept;
    ~HostMachineSession();

    [[nodiscard]] const HostMachineSessionConfig& config() const noexcept;
    void boot();
    void reset();

    [[nodiscard]] bool booted() const noexcept;
    [[nodiscard]] HostMachineSessionStatus status() const noexcept;
    [[nodiscard]] bool waiting_for_input() const noexcept;
    [[nodiscard]] bool completed() const noexcept;
    [[nodiscard]] bool has_shell_io_status() const noexcept;
    [[nodiscard]] os::io::IoStatus shell_io_status() const noexcept;
    [[nodiscard]] std::size_t command_count() const noexcept;
    [[nodiscard]] std::size_t poll_count() const noexcept;
    [[nodiscard]] HostMachineSessionSnapshot snapshot() const;

    [[nodiscard]] os::platform::Machine& machine();
    [[nodiscard]] const os::platform::Machine& machine() const;
    [[nodiscard]] os::kernel::Kernel& kernel();
    [[nodiscard]] const os::kernel::Kernel& kernel() const;
    [[nodiscard]] os::dev::TerminalDevice& terminal_device();
    [[nodiscard]] const os::dev::TerminalDevice& terminal_device() const;
    [[nodiscard]] os::proc::Process& shell_process();
    [[nodiscard]] const os::proc::Process& shell_process() const;
    [[nodiscard]] os::sched::ThreadContext& shell_thread();
    [[nodiscard]] const os::sched::ThreadContext& shell_thread() const;

    [[nodiscard]] HostMachineSessionStatus pump_until_waiting();
    [[nodiscard]] HostMachineSessionStatus submit_input(std::string_view text);
    [[nodiscard]] HostMachineSessionStatus submit_input_event(const HostInputEvent& event);

private:
    struct SessionState;

    [[nodiscard]] SessionState& require_state();
    [[nodiscard]] const SessionState& require_state() const;
    void clear_runtime_status() noexcept;
    void schedule_shell_thread();
    void record_shell_io_error(os::io::IoStatus io_status) noexcept;

    HostMachineSessionConfig config_;
    std::unique_ptr<SessionState> state_;
    HostMachineSessionStatus status_ = HostMachineSessionStatus::CREATED;
    os::io::IoStatus shell_io_status_ = os::io::IoStatus::COUNT;
    bool has_shell_io_status_ = false;
    std::size_t command_count_ = std::size_t{0};
    std::size_t poll_count_ = std::size_t{0};
};
}
