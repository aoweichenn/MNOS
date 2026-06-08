#include <cstddef>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/shell/session.hpp>

namespace io = mnos::os::io;
namespace shell = mnos::os::shell;

namespace
{
using ::testing::Eq;
}

TEST(Stage14ShellSessionTest, StepStatusCatalogIsStable)
{
    EXPECT_THAT(shell::SHELL_SESSION_READ_BUFFER_SIZE, Eq(std::size_t{128}));
    EXPECT_THAT(shell::SHELL_SESSION_DEFAULT_PROMPT, Eq(std::string_view{"mnos> "}));

    EXPECT_TRUE(shell::is_shell_session_step_status_valid(shell::ShellSessionStepStatus::BLOCKED));
    EXPECT_TRUE(shell::is_shell_session_step_status_valid(shell::ShellSessionStepStatus::PENDING_INPUT));
    EXPECT_TRUE(shell::is_shell_session_step_status_valid(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_TRUE(shell::is_shell_session_step_status_valid(shell::ShellSessionStepStatus::EXITED));
    EXPECT_TRUE(shell::is_shell_session_step_status_valid(shell::ShellSessionStepStatus::IO_ERROR));
    EXPECT_FALSE(shell::is_shell_session_step_status_valid(shell::ShellSessionStepStatus::COUNT));

    EXPECT_THAT(
        shell::shell_session_step_status_to_index(shell::ShellSessionStepStatus::COMMAND),
        Eq(std::size_t{2}));
    EXPECT_THAT(
        shell::shell_session_step_status_to_name(shell::ShellSessionStepStatus::BLOCKED),
        Eq(std::string_view{"BLOCKED"}));
    EXPECT_THAT(
        shell::shell_session_step_status_to_name(shell::ShellSessionStepStatus::COUNT),
        Eq(std::string_view{"<invalid>"}));
}

TEST(Stage14ShellSessionTest, StepResultModelsBlockedPendingCommandExitAndIoError)
{
    const shell::ShellSessionStepResult blocked = shell::ShellSessionStepResult::blocked();
    EXPECT_THAT(blocked.status(), Eq(shell::ShellSessionStepStatus::BLOCKED));
    EXPECT_TRUE(blocked.is_blocked());
    EXPECT_FALSE(blocked.has_command_status());
    EXPECT_FALSE(blocked.has_io_status());

    const shell::ShellSessionStepResult pending = shell::ShellSessionStepResult::pending_input();
    EXPECT_THAT(pending.status(), Eq(shell::ShellSessionStepStatus::PENDING_INPUT));
    EXPECT_FALSE(pending.has_command_status());
    EXPECT_FALSE(pending.has_io_status());

    const shell::ShellSessionStepResult command =
        shell::ShellSessionStepResult::command(shell::ShellCommandResult::handled());
    EXPECT_THAT(command.status(), Eq(shell::ShellSessionStepStatus::COMMAND));
    EXPECT_TRUE(command.has_command_status());
    EXPECT_THAT(command.command_status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_FALSE(command.has_io_status());

    const shell::ShellSessionStepResult exited =
        shell::ShellSessionStepResult::exited(shell::ShellCommandResult::exit_requested());
    EXPECT_THAT(exited.status(), Eq(shell::ShellSessionStepStatus::EXITED));
    EXPECT_TRUE(exited.is_exited());
    EXPECT_TRUE(exited.has_command_status());
    EXPECT_THAT(exited.command_status(), Eq(shell::ShellCommandStatus::EXIT_REQUESTED));

    const shell::ShellSessionStepResult plain_exited = shell::ShellSessionStepResult::exited();
    EXPECT_THAT(plain_exited.status(), Eq(shell::ShellSessionStepStatus::EXITED));
    EXPECT_FALSE(plain_exited.has_command_status());

    const shell::ShellSessionStepResult io_error = shell::ShellSessionStepResult::io_error(io::IoStatus::BAD_DESCRIPTOR);
    EXPECT_THAT(io_error.status(), Eq(shell::ShellSessionStepStatus::IO_ERROR));
    EXPECT_TRUE(io_error.has_io_status());
    EXPECT_THAT(io_error.io_status(), Eq(io::IoStatus::BAD_DESCRIPTOR));
}
