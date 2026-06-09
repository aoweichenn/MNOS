#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include <mnos/host/console_options.hpp>
#include <mnos/host/terminal_runner.hpp>

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#include <termios.h>
#include <unistd.h>
#endif

namespace
{
constexpr const char* HOST_CONSOLE_RAW_MODE_ERROR = "mnos_console: failed to enable raw terminal mode\n";
constexpr int HOST_CONSOLE_SUCCESS_EXIT_CODE = 0;
constexpr int HOST_CONSOLE_USAGE_ERROR_EXIT_CODE = 2;
constexpr int HOST_CONSOLE_RUNTIME_ERROR_EXIT_CODE = 1;

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
constexpr int HOST_CONSOLE_STDIN_FD = STDIN_FILENO;
constexpr int HOST_CONSOLE_INVALID_FD = -1;
constexpr tcflag_t HOST_CONSOLE_RAW_DISABLED_LOCAL_FLAGS =
    static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
constexpr tcflag_t HOST_CONSOLE_RAW_DISABLED_INPUT_FLAGS = static_cast<tcflag_t>(ICRNL | IXON);
constexpr cc_t HOST_CONSOLE_RAW_MINIMUM_READ_BYTES = cc_t{1};
constexpr cc_t HOST_CONSOLE_RAW_READ_TIMEOUT_DS = cc_t{0};

class HostConsoleRawModeGuard final
{
public:
    HostConsoleRawModeGuard() noexcept = default;
    HostConsoleRawModeGuard(const HostConsoleRawModeGuard&) = delete;
    HostConsoleRawModeGuard& operator=(const HostConsoleRawModeGuard&) = delete;

    HostConsoleRawModeGuard(HostConsoleRawModeGuard&& other) noexcept :
        fd_(other.fd_),
        original_(other.original_),
        enabled_(other.enabled_)
    {
        other.fd_ = HOST_CONSOLE_INVALID_FD;
        other.enabled_ = false;
    }

    HostConsoleRawModeGuard& operator=(HostConsoleRawModeGuard&& other) noexcept
    {
        if (this != &other)
        {
            this->restore();
            this->fd_ = other.fd_;
            this->original_ = other.original_;
            this->enabled_ = other.enabled_;
            other.fd_ = HOST_CONSOLE_INVALID_FD;
            other.enabled_ = false;
        }
        return *this;
    }

    ~HostConsoleRawModeGuard()
    {
        this->restore();
    }

    [[nodiscard]] static std::optional<HostConsoleRawModeGuard> enable_for_fd(const int fd) noexcept
    {
        termios original{};
        if (tcgetattr(fd, &original) != 0)
        {
            return std::nullopt;
        }

        termios raw = original;
        raw.c_lflag &= static_cast<tcflag_t>(~HOST_CONSOLE_RAW_DISABLED_LOCAL_FLAGS);
        raw.c_iflag &= static_cast<tcflag_t>(~HOST_CONSOLE_RAW_DISABLED_INPUT_FLAGS);
        raw.c_cc[VMIN] = HOST_CONSOLE_RAW_MINIMUM_READ_BYTES;
        raw.c_cc[VTIME] = HOST_CONSOLE_RAW_READ_TIMEOUT_DS;
        if (tcsetattr(fd, TCSAFLUSH, &raw) != 0)
        {
            return std::nullopt;
        }

        return HostConsoleRawModeGuard{fd, original};
    }

private:
    HostConsoleRawModeGuard(const int fd, const termios& original) noexcept :
        fd_(fd),
        original_(original),
        enabled_(true)
    {
    }

    void restore() noexcept
    {
        if (this->enabled_)
        {
            static_cast<void>(tcsetattr(this->fd_, TCSAFLUSH, &this->original_));
            this->enabled_ = false;
        }
    }

    int fd_ = HOST_CONSOLE_INVALID_FD;
    termios original_{};
    bool enabled_ = false;
};

[[nodiscard]] bool host_console_stdin_is_terminal() noexcept
{
    return isatty(HOST_CONSOLE_STDIN_FD) != 0;
}
#else
constexpr int HOST_CONSOLE_STDIN_FD = 0;

class HostConsoleRawModeGuard final
{
public:
    [[nodiscard]] static std::optional<HostConsoleRawModeGuard> enable_for_fd(int) noexcept
    {
        return std::nullopt;
    }
};

[[nodiscard]] bool host_console_stdin_is_terminal() noexcept
{
    return false;
}
#endif

void print_usage(std::ostream& output)
{
    output << mnos::host::HOST_CONSOLE_USAGE_TEXT;
}

[[nodiscard]] std::optional<HostConsoleRawModeGuard> enable_raw_mode_if_needed(
    const mnos::host::TerminalInputMode input_mode)
{
    if (input_mode != mnos::host::TerminalInputMode::RAW || !host_console_stdin_is_terminal())
    {
        return HostConsoleRawModeGuard{};
    }
    return HostConsoleRawModeGuard::enable_for_fd(HOST_CONSOLE_STDIN_FD);
}
}

int main(const int argc, char** argv)
{
    const int user_argument_count = argc > 0 ? argc - 1 : 0;
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(user_argument_count));
    for (int argument_index = 1; argument_index < argc; ++argument_index)
    {
        arguments.emplace_back(argv[argument_index]);
    }

    const mnos::host::ConsoleOptionParseResult options_result = mnos::host::parse_console_options(arguments);
    if (options_result.status() == mnos::host::ConsoleOptionParseStatus::HELP_REQUESTED)
    {
        print_usage(std::cout);
        return HOST_CONSOLE_SUCCESS_EXIT_CODE;
    }
    if (options_result.status() == mnos::host::ConsoleOptionParseStatus::INVALID_ARGUMENT)
    {
        print_usage(std::cerr);
        return HOST_CONSOLE_USAGE_ERROR_EXIT_CODE;
    }

    mnos::host::TerminalRunnerConfig config;
    config.render_mode = options_result.options().render_mode;
    config.input_mode = options_result.options().input_mode;

    const mnos::host::TerminalInputMode input_mode =
        mnos::host::resolve_console_input_mode(config.input_mode, host_console_stdin_is_terminal());
    std::optional<HostConsoleRawModeGuard> raw_mode_guard = enable_raw_mode_if_needed(input_mode);
    if (!raw_mode_guard.has_value())
    {
        std::cerr << HOST_CONSOLE_RAW_MODE_ERROR;
        return HOST_CONSOLE_RUNTIME_ERROR_EXIT_CODE;
    }

    const mnos::host::TerminalRunner runner{config};
    mnos::host::StreamTerminalBackend backend{std::cin, std::cout, config.render_mode, input_mode};
    const mnos::host::TerminalRunResult result = runner.run(backend);
    if (result.status() == mnos::host::TerminalRunStatus::SHELL_IO_ERROR ||
        result.status() == mnos::host::TerminalRunStatus::HOST_IO_ERROR)
    {
        return HOST_CONSOLE_RUNTIME_ERROR_EXIT_CODE;
    }
    return HOST_CONSOLE_SUCCESS_EXIT_CODE;
}
