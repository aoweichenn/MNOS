#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/shell/shell.hpp>

namespace io = mnos::os::io;
namespace shell = mnos::os::shell;

namespace
{
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;

constexpr std::uint64_t TEST_RAW_DESCRIPTOR_OVERFLOW =
    static_cast<std::uint64_t>(std::numeric_limits<io::FileDescriptor::value_type>::max()) + std::uint64_t{1};
}

TEST(Stage13IoShellTest, FileDescriptorValueObjectAndStandardEntriesAreExplicit)
{
    EXPECT_FALSE(io::FileDescriptor{}.is_valid());
    EXPECT_TRUE(io::FileDescriptor::stdin().is_valid());
    EXPECT_TRUE(io::FileDescriptor::stdin().is_standard_stream());
    EXPECT_TRUE(io::FileDescriptor::stdout().is_standard_stream());
    EXPECT_TRUE(io::FileDescriptor::stderr().is_standard_stream());
    EXPECT_FALSE(io::FileDescriptor{3}.is_standard_stream());
    EXPECT_LT(io::FileDescriptor::stdin(), io::FileDescriptor::stdout());

    const io::FileDescriptorEntry stdin_entry{
        io::FileDescriptor::stdin(),
        io::FileDeviceKind::TTY,
        io::FileAccessMode::READ_ONLY};
    EXPECT_THAT(stdin_entry.descriptor(), Eq(io::FileDescriptor::stdin()));
    EXPECT_THAT(stdin_entry.device_kind(), Eq(io::FileDeviceKind::TTY));
    EXPECT_TRUE(stdin_entry.readable());
    EXPECT_FALSE(stdin_entry.writable());

    const io::FileDescriptorEntry stdout_entry{
        io::FileDescriptor::stdout(),
        io::FileDeviceKind::TTY,
        io::FileAccessMode::WRITE_ONLY};
    EXPECT_FALSE(stdout_entry.readable());
    EXPECT_TRUE(stdout_entry.writable());

    const io::FileDescriptorEntry rw_entry{
        io::FileDescriptor{3},
        io::FileDeviceKind::TTY,
        io::FileAccessMode::READ_WRITE};
    EXPECT_TRUE(rw_entry.readable());
    EXPECT_TRUE(rw_entry.writable());
    EXPECT_THROW(
        static_cast<void>(io::FileDescriptorEntry{
            io::FileDescriptor{4},
            io::FileDeviceKind::VFS_FILE,
            io::FileAccessMode::READ_WRITE}),
        std::invalid_argument);
}

TEST(Stage13IoShellTest, FileDescriptorTableDefaultsToTtyStdio)
{
    io::FileDescriptorTable table;

    EXPECT_THAT(table.size(), Eq(io::FILE_DESCRIPTOR_STANDARD_STREAM_COUNT));
    ASSERT_NE(table.find(io::FileDescriptor::stdin()), nullptr);
    ASSERT_NE(table.find(io::FileDescriptor::stdout()), nullptr);
    ASSERT_NE(table.find(io::FileDescriptor::stderr()), nullptr);
    ASSERT_NE(table.find_mutable(io::FileDescriptor::stdin()), nullptr);
    EXPECT_TRUE(table.contains(io::FileDescriptor::stdin()));
    EXPECT_FALSE(table.contains(io::FileDescriptor{99}));
    EXPECT_TRUE(table.readable(io::FileDescriptor::stdin()));
    EXPECT_FALSE(table.writable(io::FileDescriptor::stdin()));
    EXPECT_FALSE(table.readable(io::FileDescriptor::stdout()));
    EXPECT_TRUE(table.writable(io::FileDescriptor::stdout()));
    EXPECT_FALSE(table.readable(io::FileDescriptor::stderr()));
    EXPECT_TRUE(table.writable(io::FileDescriptor::stderr()));
    EXPECT_EQ(table.find(io::FileDescriptor::invalid()), nullptr);
    EXPECT_EQ(table.find_mutable(io::FileDescriptor{99}), nullptr);
    EXPECT_FALSE(table.close(io::FileDescriptor{99}));
}

TEST(Stage13IoShellTest, OpenFileDescriptionAndEmptyEntriesRejectMissingPayloads)
{
    io::OpenFileDescription tty = io::OpenFileDescription::tty(io::FileAccessMode::READ_WRITE);
    EXPECT_THAT(tty.device_kind(), Eq(io::FileDeviceKind::TTY));
    EXPECT_THAT(tty.access_mode(), Eq(io::FileAccessMode::READ_WRITE));
    EXPECT_TRUE(tty.readable());
    EXPECT_TRUE(tty.writable());
    EXPECT_FALSE(tty.has_vfs_file());
    EXPECT_THROW(static_cast<void>(tty.vfs_file()), std::logic_error);
    const io::OpenFileDescription& const_tty = tty;
    EXPECT_THROW(static_cast<void>(const_tty.vfs_file()), std::logic_error);

    io::FileDescriptorEntry empty_entry;
    EXPECT_THAT(empty_entry.descriptor(), Eq(io::FileDescriptor::invalid()));
    EXPECT_THAT(empty_entry.device_kind(), Eq(io::FileDeviceKind::COUNT));
    EXPECT_THAT(empty_entry.access_mode(), Eq(io::FileAccessMode::COUNT));
    EXPECT_FALSE(empty_entry.readable());
    EXPECT_FALSE(empty_entry.writable());
    EXPECT_FALSE(empty_entry.has_description());
    EXPECT_THROW(static_cast<void>(empty_entry.description()), std::logic_error);
    const io::FileDescriptorEntry& const_entry = empty_entry;
    EXPECT_THROW(static_cast<void>(const_entry.description()), std::logic_error);
}

TEST(Stage13IoShellTest, FileDescriptorRawConversionRejectsOverflow)
{
    EXPECT_THAT(io::file_descriptor_from_raw(std::uint64_t{0}), Eq(io::FileDescriptor::stdin()));
    EXPECT_THAT(io::file_descriptor_from_raw(std::uint64_t{1}), Eq(io::FileDescriptor::stdout()));
    EXPECT_THAT(io::file_descriptor_from_raw(std::uint64_t{2}), Eq(io::FileDescriptor::stderr()));
    EXPECT_THAT(io::file_descriptor_from_raw(TEST_RAW_DESCRIPTOR_OVERFLOW), Eq(io::FileDescriptor::invalid()));
}

TEST(Stage13IoShellTest, IoResultCarriesStatusAndByteCount)
{
    const io::IoResult ready = io::IoResult::ready(std::size_t{7});
    EXPECT_THAT(ready.status(), Eq(io::IoStatus::READY));
    EXPECT_THAT(ready.byte_count(), Eq(std::size_t{7}));
    EXPECT_TRUE(ready.is_ready());
    EXPECT_FALSE(ready.is_blocked());

    const io::IoResult blocked = io::IoResult::blocked();
    EXPECT_THAT(blocked.status(), Eq(io::IoStatus::BLOCKED));
    EXPECT_THAT(blocked.byte_count(), Eq(std::size_t{0}));
    EXPECT_FALSE(blocked.is_ready());
    EXPECT_TRUE(blocked.is_blocked());

    EXPECT_THAT(io::IoResult::bad_descriptor().status(), Eq(io::IoStatus::BAD_DESCRIPTOR));
    EXPECT_THAT(io::IoResult::bad_address().status(), Eq(io::IoStatus::BAD_ADDRESS));
    EXPECT_THAT(io::IoResult::invalid_argument().status(), Eq(io::IoStatus::INVALID_ARGUMENT));
    EXPECT_THAT(io::IoResult::no_space().status(), Eq(io::IoStatus::NO_SPACE));
}

TEST(Stage13IoShellTest, ShellParserHandlesWhitespaceQuotesAndEscapes)
{
    const shell::ShellParser parser;

    const shell::ShellParseResult result =
        parser.parse("  echo alpha \"two words\" 'three words' escaped\\ space  ");
    ASSERT_THAT(result.status(), Eq(shell::ShellParseStatus::COMMAND));
    ASSERT_TRUE(result.has_command());
    EXPECT_THAT(result.command().name(), Eq(std::string_view{"echo"}));
    EXPECT_THAT(
        result.command().arguments(),
        ElementsAre("alpha", "two words", "three words", "escaped space"));
    EXPECT_THAT(result.command().argument_count(), Eq(std::size_t{4}));
    EXPECT_THAT(result.command().argument_at(std::size_t{1}), Eq(std::string_view{"two words"}));
}

TEST(Stage13IoShellTest, ShellParserCoversEmptyQuotedAndErrorInputs)
{
    const shell::ShellParser parser;

    const shell::ShellParseResult empty = parser.parse(" \t\r\n");
    EXPECT_THAT(empty.status(), Eq(shell::ShellParseStatus::EMPTY));
    EXPECT_FALSE(empty.has_command());
    EXPECT_THROW(static_cast<void>(empty.command()), std::logic_error);

    const shell::ShellParseResult empty_argument = parser.parse("echo \"\"");
    ASSERT_THAT(empty_argument.status(), Eq(shell::ShellParseStatus::COMMAND));
    EXPECT_THAT(empty_argument.command().arguments(), ElementsAre(""));

    const shell::ShellParseResult trailing_escape = parser.parse("echo path\\");
    ASSERT_THAT(trailing_escape.status(), Eq(shell::ShellParseStatus::COMMAND));
    EXPECT_THAT(trailing_escape.command().arguments(), ElementsAre("path\\"));

    const shell::ShellParseResult unterminated = parser.parse("echo \"missing");
    EXPECT_THAT(unterminated.status(), Eq(shell::ShellParseStatus::UNTERMINATED_QUOTE));
    EXPECT_FALSE(unterminated.has_command());
}

TEST(Stage13IoShellTest, ShellCommandAndResultObjectsExposeStableStatuses)
{
    const shell::ShellCommand command{"echo", {"hello"}};
    EXPECT_THAT(command.name(), Eq(std::string_view{"echo"}));
    EXPECT_THAT(command.arguments(), ElementsAre("hello"));
    EXPECT_THAT(command.argument_at(std::size_t{0}), Eq(std::string_view{"hello"}));
    EXPECT_THROW(static_cast<void>(command.argument_at(std::size_t{1})), std::out_of_range);

    EXPECT_THAT(shell::ShellCommandResult::handled().status(), Eq(shell::ShellCommandStatus::HANDLED));
    EXPECT_TRUE(shell::ShellCommandResult::handled().is_handled());
    EXPECT_THAT(shell::ShellCommandResult::empty().status(), Eq(shell::ShellCommandStatus::EMPTY));
    EXPECT_THAT(
        shell::ShellCommandResult::unknown_command().status(),
        Eq(shell::ShellCommandStatus::UNKNOWN_COMMAND));
    EXPECT_THAT(shell::ShellCommandResult::parse_error().status(), Eq(shell::ShellCommandStatus::PARSE_ERROR));
    EXPECT_THAT(
        shell::ShellCommandResult::exit_requested().status(),
        Eq(shell::ShellCommandStatus::EXIT_REQUESTED));
}

TEST(Stage13IoShellTest, ShellBuiltinRegistryKeepsBuiltinCatalogDiscoverable)
{
    const shell::ShellBuiltinRegistry registry;

    EXPECT_THAT(registry.size(), Eq(std::size_t{14}));
    EXPECT_TRUE(registry.contains("help"));
    EXPECT_TRUE(registry.contains("clear"));
    EXPECT_TRUE(registry.contains("echo"));
    EXPECT_TRUE(registry.contains("ps"));
    EXPECT_TRUE(registry.contains("mem"));
    EXPECT_TRUE(registry.contains("cpu"));
    EXPECT_TRUE(registry.contains("ticks"));
    EXPECT_TRUE(registry.contains("ls"));
    EXPECT_TRUE(registry.contains("cat"));
    EXPECT_TRUE(registry.contains("touch"));
    EXPECT_TRUE(registry.contains("write"));
    EXPECT_TRUE(registry.contains("stat"));
    EXPECT_TRUE(registry.contains("run"));
    EXPECT_TRUE(registry.contains("exit"));
    EXPECT_FALSE(registry.contains("missing"));
    const std::optional<shell::ShellBuiltinInfo> ls_info = registry.find("ls");
    ASSERT_TRUE(ls_info.has_value());
    EXPECT_THAT(ls_info->name, Eq(std::string_view{"ls"}));
    EXPECT_THAT(ls_info->syntax, Eq(std::string_view{"ls [path]"}));
    EXPECT_THAT(ls_info->description, Eq(std::string_view{"list directory entries"}));
    const std::optional<shell::ShellBuiltinInfo> run_info = registry.find("run");
    ASSERT_TRUE(run_info.has_value());
    EXPECT_THAT(run_info->syntax, Eq(std::string_view{"run path [max_steps]"}));
    EXPECT_THAT(run_info->description, Eq(std::string_view{"load and execute an ELF64 user program"}));
    EXPECT_FALSE(registry.find("missing").has_value());

    const shell::ShellBuiltinInfo help_info = registry.info_at(std::size_t{0});
    EXPECT_THAT(help_info.name, Eq(std::string_view{"help"}));
    EXPECT_THAT(help_info.syntax, Eq(std::string_view{"help [command]"}));
    EXPECT_THAT(registry.name_at(std::size_t{0}), Eq(std::string_view{"help"}));
    EXPECT_THAT(registry.syntax_at(std::size_t{0}), Eq(std::string_view{"help [command]"}));
    EXPECT_THAT(
        registry.description_at(std::size_t{0}),
        Eq(std::string_view{"show all commands or details for one command"}));
    EXPECT_THROW(static_cast<void>(registry.info_at(registry.size())), std::out_of_range);
    EXPECT_THROW(static_cast<void>(registry.name_at(registry.size())), std::out_of_range);
    EXPECT_THROW(static_cast<void>(registry.syntax_at(registry.size())), std::out_of_range);
    EXPECT_THROW(static_cast<void>(registry.description_at(registry.size())), std::out_of_range);
}
