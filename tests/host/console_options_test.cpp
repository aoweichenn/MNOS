#include <array>
#include <span>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/host/console_options.hpp>

namespace
{
namespace host = mnos::host;

using ::testing::Eq;

[[nodiscard]] host::ConsoleOptionParseResult parse_arguments(
    const std::span<const std::string_view> arguments) noexcept
{
    return host::parse_console_options(arguments);
}
}

TEST(HostConsoleOptionsTest, DefaultsToAnsiStreamAndAutoInput)
{
    const std::array<std::string_view, 0> arguments{};

    const host::ConsoleOptionParseResult result = parse_arguments(arguments);

    EXPECT_THAT(result.status(), Eq(host::ConsoleOptionParseStatus::RUN));
    EXPECT_TRUE(result.should_run());
    EXPECT_THAT(result.options().render_mode, Eq(host::TerminalRenderMode::ANSI_STREAM));
    EXPECT_THAT(result.options().input_mode, Eq(host::TerminalInputMode::AUTO));
    EXPECT_THAT(result.invalid_argument(), Eq(std::string_view{}));
}

TEST(HostConsoleOptionsTest, ParsesRenderAndInputModesWithLastFlagWinning)
{
    constexpr std::array arguments{
        std::string_view{"--plain"},
        std::string_view{"--ansi-screen"},
        std::string_view{"--plain-screen"},
        std::string_view{"--raw"},
        std::string_view{"--line"}};

    const host::ConsoleOptionParseResult result = parse_arguments(arguments);

    EXPECT_THAT(result.status(), Eq(host::ConsoleOptionParseStatus::RUN));
    EXPECT_THAT(result.options().render_mode, Eq(host::TerminalRenderMode::PLAIN_SCREEN));
    EXPECT_THAT(result.options().input_mode, Eq(host::TerminalInputMode::LINE));
}

TEST(HostConsoleOptionsTest, HelpStopsParsingAndKeepsEarlierOptions)
{
    constexpr std::array arguments{
        std::string_view{"--plain"},
        std::string_view{"--raw"},
        std::string_view{"--help"},
        std::string_view{"--ansi"}};

    const host::ConsoleOptionParseResult result = parse_arguments(arguments);

    EXPECT_THAT(result.status(), Eq(host::ConsoleOptionParseStatus::HELP_REQUESTED));
    EXPECT_FALSE(result.should_run());
    EXPECT_THAT(result.options().render_mode, Eq(host::TerminalRenderMode::PLAIN_STREAM));
    EXPECT_THAT(result.options().input_mode, Eq(host::TerminalInputMode::RAW));
}

TEST(HostConsoleOptionsTest, ShortHelpIsAccepted)
{
    constexpr std::array arguments{std::string_view{"-h"}};

    const host::ConsoleOptionParseResult result = parse_arguments(arguments);

    EXPECT_THAT(result.status(), Eq(host::ConsoleOptionParseStatus::HELP_REQUESTED));
}

TEST(HostConsoleOptionsTest, InvalidArgumentPreservesParsedOptionsAndArgument)
{
    constexpr std::array arguments{
        std::string_view{"--ansi-screen"},
        std::string_view{"--raw"},
        std::string_view{"--unknown"},
        std::string_view{"--plain"}};

    const host::ConsoleOptionParseResult result = parse_arguments(arguments);

    EXPECT_THAT(result.status(), Eq(host::ConsoleOptionParseStatus::INVALID_ARGUMENT));
    EXPECT_FALSE(result.should_run());
    EXPECT_THAT(result.options().render_mode, Eq(host::TerminalRenderMode::ANSI_SCREEN));
    EXPECT_THAT(result.options().input_mode, Eq(host::TerminalInputMode::RAW));
    EXPECT_THAT(result.invalid_argument(), Eq(std::string_view{"--unknown"}));
}

TEST(HostConsoleOptionsTest, AutoInputModeUsesRawOnlyForInteractiveTerminal)
{
    EXPECT_THAT(
        host::resolve_console_input_mode(host::TerminalInputMode::AUTO, true),
        Eq(host::TerminalInputMode::RAW));
    EXPECT_THAT(
        host::resolve_console_input_mode(host::TerminalInputMode::AUTO, false),
        Eq(host::TerminalInputMode::LINE));
    EXPECT_THAT(
        host::resolve_console_input_mode(host::TerminalInputMode::COUNT, true),
        Eq(host::TerminalInputMode::RAW));
}

TEST(HostConsoleOptionsTest, ExplicitInputModesOverrideTerminalKind)
{
    EXPECT_THAT(
        host::resolve_console_input_mode(host::TerminalInputMode::RAW, false),
        Eq(host::TerminalInputMode::RAW));
    EXPECT_THAT(
        host::resolve_console_input_mode(host::TerminalInputMode::LINE, true),
        Eq(host::TerminalInputMode::LINE));
}
