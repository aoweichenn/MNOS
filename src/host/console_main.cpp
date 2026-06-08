#include <iostream>
#include <string_view>

#include <mnos/host/terminal_runner.hpp>

namespace
{
constexpr std::string_view HOST_CONSOLE_HELP_FLAG = "--help";
constexpr std::string_view HOST_CONSOLE_SHORT_HELP_FLAG = "-h";
constexpr std::string_view HOST_CONSOLE_PLAIN_FLAG = "--plain";
constexpr std::string_view HOST_CONSOLE_ANSI_FLAG = "--ansi";
constexpr int HOST_CONSOLE_SUCCESS_EXIT_CODE = 0;
constexpr int HOST_CONSOLE_USAGE_ERROR_EXIT_CODE = 2;
constexpr int HOST_CONSOLE_RUNTIME_ERROR_EXIT_CODE = 1;

void print_usage(std::ostream& output)
{
    output << "usage: mnos_console [--ansi|--plain]\n";
}
}

int main(const int argc, char** argv)
{
    mnos::host::TerminalRunnerConfig config;
    for (int argument_index = 1; argument_index < argc; ++argument_index)
    {
        const std::string_view argument{argv[argument_index]};
        if (argument == HOST_CONSOLE_HELP_FLAG || argument == HOST_CONSOLE_SHORT_HELP_FLAG)
        {
            print_usage(std::cout);
            return HOST_CONSOLE_SUCCESS_EXIT_CODE;
        }
        if (argument == HOST_CONSOLE_PLAIN_FLAG)
        {
            config.render_mode = mnos::host::TerminalRenderMode::PLAIN_SCREEN;
            continue;
        }
        if (argument == HOST_CONSOLE_ANSI_FLAG)
        {
            config.render_mode = mnos::host::TerminalRenderMode::ANSI_SCREEN;
            continue;
        }

        print_usage(std::cerr);
        return HOST_CONSOLE_USAGE_ERROR_EXIT_CODE;
    }

    const mnos::host::TerminalRunner runner{config};
    const mnos::host::TerminalRunResult result = runner.run(std::cin, std::cout);
    if (result.status() == mnos::host::TerminalRunStatus::SHELL_IO_ERROR ||
        result.status() == mnos::host::TerminalRunStatus::HOST_IO_ERROR)
    {
        return HOST_CONSOLE_RUNTIME_ERROR_EXIT_CODE;
    }
    return HOST_CONSOLE_SUCCESS_EXIT_CODE;
}
