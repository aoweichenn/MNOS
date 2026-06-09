#include <mnos/host/console_options.hpp>

#include <string_view>

namespace
{
constexpr std::string_view HOST_CONSOLE_HELP_FLAG = "--help";
constexpr std::string_view HOST_CONSOLE_SHORT_HELP_FLAG = "-h";
constexpr std::string_view HOST_CONSOLE_PLAIN_FLAG = "--plain";
constexpr std::string_view HOST_CONSOLE_ANSI_FLAG = "--ansi";
constexpr std::string_view HOST_CONSOLE_ANSI_SCREEN_FLAG = "--ansi-screen";
constexpr std::string_view HOST_CONSOLE_PLAIN_SCREEN_FLAG = "--plain-screen";
constexpr std::string_view HOST_CONSOLE_RAW_FLAG = "--raw";
constexpr std::string_view HOST_CONSOLE_LINE_FLAG = "--line";
}

namespace mnos::host
{
ConsoleOptionParseResult ConsoleOptionParseResult::run(const ConsoleOptions options) noexcept
{
    return ConsoleOptionParseResult{ConsoleOptionParseStatus::RUN, options, {}};
}

ConsoleOptionParseResult ConsoleOptionParseResult::help(const ConsoleOptions options) noexcept
{
    return ConsoleOptionParseResult{ConsoleOptionParseStatus::HELP_REQUESTED, options, {}};
}

ConsoleOptionParseResult ConsoleOptionParseResult::invalid_argument(
    const ConsoleOptions options,
    const std::string_view argument) noexcept
{
    return ConsoleOptionParseResult{ConsoleOptionParseStatus::INVALID_ARGUMENT, options, argument};
}

ConsoleOptionParseResult::ConsoleOptionParseResult(
    const ConsoleOptionParseStatus status,
    const ConsoleOptions options,
    const std::string_view invalid_argument) noexcept :
    status_(status),
    options_(options),
    invalid_argument_(invalid_argument)
{
}

ConsoleOptionParseStatus ConsoleOptionParseResult::status() const noexcept
{
    return this->status_;
}

const ConsoleOptions& ConsoleOptionParseResult::options() const noexcept
{
    return this->options_;
}

std::string_view ConsoleOptionParseResult::invalid_argument() const noexcept
{
    return this->invalid_argument_;
}

bool ConsoleOptionParseResult::should_run() const noexcept
{
    return this->status_ == ConsoleOptionParseStatus::RUN;
}

ConsoleOptionParseResult parse_console_options(const std::span<const std::string_view> arguments) noexcept
{
    ConsoleOptions options;
    for (const std::string_view argument : arguments)
    {
        if (argument == HOST_CONSOLE_HELP_FLAG || argument == HOST_CONSOLE_SHORT_HELP_FLAG)
        {
            return ConsoleOptionParseResult::help(options);
        }
        if (argument == HOST_CONSOLE_PLAIN_FLAG)
        {
            options.render_mode = TerminalRenderMode::PLAIN_STREAM;
            continue;
        }
        if (argument == HOST_CONSOLE_ANSI_FLAG)
        {
            options.render_mode = TerminalRenderMode::ANSI_STREAM;
            continue;
        }
        if (argument == HOST_CONSOLE_ANSI_SCREEN_FLAG)
        {
            options.render_mode = TerminalRenderMode::ANSI_SCREEN;
            continue;
        }
        if (argument == HOST_CONSOLE_PLAIN_SCREEN_FLAG)
        {
            options.render_mode = TerminalRenderMode::PLAIN_SCREEN;
            continue;
        }
        if (argument == HOST_CONSOLE_RAW_FLAG)
        {
            options.input_mode = TerminalInputMode::RAW;
            continue;
        }
        if (argument == HOST_CONSOLE_LINE_FLAG)
        {
            options.input_mode = TerminalInputMode::LINE;
            continue;
        }

        return ConsoleOptionParseResult::invalid_argument(options, argument);
    }

    return ConsoleOptionParseResult::run(options);
}

TerminalInputMode resolve_console_input_mode(
    const TerminalInputMode configured_mode,
    const bool stdin_is_terminal) noexcept
{
    if (configured_mode == TerminalInputMode::RAW || configured_mode == TerminalInputMode::LINE)
    {
        return configured_mode;
    }
    if (stdin_is_terminal)
    {
        return TerminalInputMode::RAW;
    }
    return TerminalInputMode::LINE;
}
}
