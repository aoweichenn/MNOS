#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <mnos/host/terminal_backend.hpp>

namespace mnos::host
{
inline constexpr std::string_view HOST_CONSOLE_USAGE_TEXT =
    "usage: mnos_console [--ansi|--plain|--ansi-screen|--plain-screen] [--raw|--line]\n";

enum class ConsoleOptionParseStatus : std::uint8_t
{
    RUN,
    HELP_REQUESTED,
    INVALID_ARGUMENT,
    COUNT
};

struct ConsoleOptions final
{
    TerminalRenderMode render_mode = TerminalRenderMode::ANSI_STREAM;
    TerminalInputMode input_mode = TerminalInputMode::AUTO;
};

class ConsoleOptionParseResult final
{
public:
    [[nodiscard]] static ConsoleOptionParseResult run(ConsoleOptions options) noexcept;
    [[nodiscard]] static ConsoleOptionParseResult help(ConsoleOptions options) noexcept;
    [[nodiscard]] static ConsoleOptionParseResult invalid_argument(
        ConsoleOptions options,
        std::string_view argument) noexcept;

    [[nodiscard]] ConsoleOptionParseStatus status() const noexcept;
    [[nodiscard]] const ConsoleOptions& options() const noexcept;
    [[nodiscard]] std::string_view invalid_argument() const noexcept;
    [[nodiscard]] bool should_run() const noexcept;

private:
    ConsoleOptionParseResult(
        ConsoleOptionParseStatus status,
        ConsoleOptions options,
        std::string_view invalid_argument) noexcept;

    ConsoleOptionParseStatus status_;
    ConsoleOptions options_;
    std::string_view invalid_argument_;
};

[[nodiscard]] ConsoleOptionParseResult parse_console_options(std::span<const std::string_view> arguments) noexcept;
[[nodiscard]] TerminalInputMode resolve_console_input_mode(TerminalInputMode configured_mode, bool stdin_is_terminal)
    noexcept;
}
