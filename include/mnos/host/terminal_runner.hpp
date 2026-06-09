#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>

#include <mnos/host/terminal_backend.hpp>
#include <mnos/os/mm/page.hpp>

namespace mnos::os::io
{
enum class IoStatus : std::uint8_t;
}

namespace mnos::host
{
inline constexpr mnos::os::mm::AddressValue HOST_TERMINAL_DEFAULT_MEMORY_PAGE_COUNT =
    mnos::os::mm::AddressValue{512};
inline constexpr std::size_t HOST_TERMINAL_DEFAULT_MEMORY_SIZE_BYTES =
    static_cast<std::size_t>(mnos::os::mm::MM_PAGE_SIZE_BYTES * HOST_TERMINAL_DEFAULT_MEMORY_PAGE_COUNT);
inline constexpr std::uint32_t HOST_TERMINAL_DEFAULT_PROCESSOR_COUNT = std::uint32_t{2};

enum class TerminalRunStatus : std::uint8_t
{
    EXITED,
    INPUT_CLOSED,
    SHELL_IO_ERROR,
    HOST_IO_ERROR,
    COUNT
};

struct TerminalRunnerConfig final
{
    std::size_t physical_memory_size_bytes = HOST_TERMINAL_DEFAULT_MEMORY_SIZE_BYTES;
    std::uint32_t processor_count = HOST_TERMINAL_DEFAULT_PROCESSOR_COUNT;
    TerminalRenderMode render_mode = TerminalRenderMode::ANSI_STREAM;
    TerminalInputMode input_mode = TerminalInputMode::AUTO;
};

class TerminalRunResult final
{
public:
    [[nodiscard]] static TerminalRunResult exited(
        std::size_t command_count,
        std::size_t poll_count,
        std::size_t render_count) noexcept;
    [[nodiscard]] static TerminalRunResult input_closed(
        std::size_t command_count,
        std::size_t poll_count,
        std::size_t render_count) noexcept;
    [[nodiscard]] static TerminalRunResult shell_io_error(
        os::io::IoStatus io_status,
        std::size_t command_count,
        std::size_t poll_count,
        std::size_t render_count) noexcept;
    [[nodiscard]] static TerminalRunResult host_io_error(
        std::size_t command_count,
        std::size_t poll_count,
        std::size_t render_count) noexcept;

    [[nodiscard]] TerminalRunStatus status() const noexcept;
    [[nodiscard]] bool completed() const noexcept;
    [[nodiscard]] bool has_shell_io_status() const noexcept;
    [[nodiscard]] os::io::IoStatus shell_io_status() const noexcept;
    [[nodiscard]] std::size_t command_count() const noexcept;
    [[nodiscard]] std::size_t poll_count() const noexcept;
    [[nodiscard]] std::size_t render_count() const noexcept;

private:
    TerminalRunResult(
        TerminalRunStatus status,
        os::io::IoStatus shell_io_status,
        bool has_shell_io_status,
        std::size_t command_count,
        std::size_t poll_count,
        std::size_t render_count) noexcept;

    TerminalRunStatus status_;
    os::io::IoStatus shell_io_status_;
    bool has_shell_io_status_;
    std::size_t command_count_;
    std::size_t poll_count_;
    std::size_t render_count_;
};

class TerminalRunner final
{
public:
    explicit TerminalRunner(TerminalRunnerConfig config = {}) noexcept;

    [[nodiscard]] const TerminalRunnerConfig& config() const noexcept;
    [[nodiscard]] TerminalRunResult run(std::istream& input, std::ostream& output) const;
    [[nodiscard]] TerminalRunResult run(HostTerminalBackend& backend) const;

private:
    TerminalRunnerConfig config_;
};
}
