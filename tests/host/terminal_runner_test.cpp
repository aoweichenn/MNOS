#include <cstddef>
#include <sstream>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/host/terminal_runner.hpp>
#include <mnos/os/io/file_descriptor.hpp>

namespace host = mnos::host;
namespace io = mnos::os::io;

namespace
{
using ::testing::Eq;
using ::testing::HasSubstr;

constexpr char TEST_ANSI_ESCAPE_CHARACTER = '\x1B';

[[nodiscard]] host::TerminalRunner make_plain_runner()
{
    host::TerminalRunnerConfig config;
    config.render_mode = host::TerminalRenderMode::PLAIN_SCREEN;
    return host::TerminalRunner{config};
}
}

TEST(HostTerminalRunnerTest, ExecutesHelpAndExitThroughSimulatedTerminal)
{
    const host::TerminalRunner runner = make_plain_runner();
    std::istringstream input{"help\nexit\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_TRUE(result.completed());
    EXPECT_FALSE(result.has_shell_io_status());
    EXPECT_THAT(result.command_count(), Eq(std::size_t{2}));
    EXPECT_GT(result.poll_count(), std::size_t{0});
    EXPECT_GT(result.render_count(), std::size_t{0});
    EXPECT_THAT(output.str(), HasSubstr("mnos> help"));
    EXPECT_THAT(output.str(), HasSubstr("builtins: help clear echo ps mem cpu ticks ls cat touch write stat exit"));
    EXPECT_THAT(output.str(), HasSubstr("mnos> exit"));
    EXPECT_THAT(output.str().find(TEST_ANSI_ESCAPE_CHARACTER), Eq(std::string::npos));
}

TEST(HostTerminalRunnerTest, FileCommandsPersistThroughShellSession)
{
    const host::TerminalRunner runner = make_plain_runner();
    std::istringstream input{
        "touch /note\n"
        "write /note hello terminal\n"
        "cat /note\n"
        "stat /note\n"
        "exit\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(result.command_count(), Eq(std::size_t{5}));
    EXPECT_THAT(output.str(), HasSubstr("mnos> touch /note"));
    EXPECT_THAT(output.str(), HasSubstr("mnos> write /note hello terminal"));
    EXPECT_THAT(output.str(), HasSubstr("hello terminal\n"));
    EXPECT_THAT(output.str(), HasSubstr("path=/note kind=file"));
}

TEST(HostTerminalRunnerTest, ReportsInputClosedWhenHostEndsWithoutExitCommand)
{
    const host::TerminalRunner runner = make_plain_runner();
    std::istringstream input{"echo before eof\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::INPUT_CLOSED));
    EXPECT_FALSE(result.completed());
    EXPECT_THAT(result.command_count(), Eq(std::size_t{1}));
    EXPECT_THAT(output.str(), HasSubstr("before eof"));
    EXPECT_THAT(output.str(), HasSubstr("mnos> "));
}

TEST(HostTerminalRunnerTest, AnsiModeEmitsScreenControlSequences)
{
    host::TerminalRunnerConfig config;
    config.render_mode = host::TerminalRenderMode::ANSI_SCREEN;
    const host::TerminalRunner runner{config};
    std::istringstream input{"exit\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(output.str(), HasSubstr("\x1B[2J\x1B[H"));
    EXPECT_THAT(output.str(), HasSubstr("mnos> exit"));
}

TEST(HostTerminalRunnerTest, ClearCommandRendersEmptyScreenBeforeNextPrompt)
{
    const host::TerminalRunner runner = make_plain_runner();
    std::istringstream input{"clear\nexit\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(result.command_count(), Eq(std::size_t{2}));
    EXPECT_THAT(output.str(), HasSubstr("mnos> \n\nmnos> "));
    EXPECT_THAT(output.str(), HasSubstr("mnos> exit"));
}

TEST(HostTerminalRunnerTest, CarriageReturnLineEndingsAreNormalized)
{
    const host::TerminalRunner runner = make_plain_runner();
    std::istringstream input{"echo carriage\r\nexit\r\n"};
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::EXITED));
    EXPECT_THAT(result.command_count(), Eq(std::size_t{2}));
    EXPECT_THAT(output.str(), HasSubstr("mnos> echo carriage"));
    EXPECT_THAT(output.str(), HasSubstr("carriage\n"));
}

TEST(HostTerminalRunnerTest, ReportsHostIoErrorWhenOutputStreamFails)
{
    const host::TerminalRunner runner = make_plain_runner();
    std::istringstream input{"exit\n"};
    std::ostringstream output;
    output.setstate(std::ios::badbit);

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::HOST_IO_ERROR));
    EXPECT_FALSE(result.completed());
    EXPECT_THAT(result.command_count(), Eq(std::size_t{0}));
}

TEST(HostTerminalRunnerTest, ReportsHostIoErrorWhenInputStreamFails)
{
    const host::TerminalRunner runner = make_plain_runner();
    std::istringstream input;
    input.setstate(std::ios::badbit);
    std::ostringstream output;

    const host::TerminalRunResult result = runner.run(input, output);

    EXPECT_THAT(result.status(), Eq(host::TerminalRunStatus::HOST_IO_ERROR));
    EXPECT_FALSE(result.completed());
    EXPECT_THAT(result.command_count(), Eq(std::size_t{0}));
    EXPECT_THAT(output.str(), HasSubstr("mnos> "));
}

TEST(HostTerminalRunnerTest, ResultFactoriesExposeErrorMetadataAndRunnerConfig)
{
    host::TerminalRunnerConfig config;
    config.physical_memory_size_bytes = host::HOST_TERMINAL_DEFAULT_MEMORY_SIZE_BYTES;
    config.processor_count = host::HOST_TERMINAL_DEFAULT_PROCESSOR_COUNT;
    config.render_mode = host::TerminalRenderMode::PLAIN_SCREEN;
    const host::TerminalRunner runner{config};

    EXPECT_THAT(runner.config().processor_count, Eq(host::HOST_TERMINAL_DEFAULT_PROCESSOR_COUNT));
    EXPECT_THAT(runner.config().render_mode, Eq(host::TerminalRenderMode::PLAIN_SCREEN));

    const host::TerminalRunResult shell_error = host::TerminalRunResult::shell_io_error(
        io::IoStatus::BAD_DESCRIPTOR,
        std::size_t{1},
        std::size_t{2},
        std::size_t{3});
    EXPECT_THAT(shell_error.status(), Eq(host::TerminalRunStatus::SHELL_IO_ERROR));
    EXPECT_TRUE(shell_error.has_shell_io_status());
    EXPECT_THAT(shell_error.shell_io_status(), Eq(io::IoStatus::BAD_DESCRIPTOR));
    EXPECT_THAT(shell_error.command_count(), Eq(std::size_t{1}));
    EXPECT_THAT(shell_error.poll_count(), Eq(std::size_t{2}));
    EXPECT_THAT(shell_error.render_count(), Eq(std::size_t{3}));

    const host::TerminalRunResult host_error =
        host::TerminalRunResult::host_io_error(std::size_t{4}, std::size_t{5}, std::size_t{6});
    EXPECT_THAT(host_error.status(), Eq(host::TerminalRunStatus::HOST_IO_ERROR));
    EXPECT_FALSE(host_error.has_shell_io_status());
    EXPECT_THAT(host_error.command_count(), Eq(std::size_t{4}));
    EXPECT_THAT(host_error.poll_count(), Eq(std::size_t{5}));
    EXPECT_THAT(host_error.render_count(), Eq(std::size_t{6}));
}
