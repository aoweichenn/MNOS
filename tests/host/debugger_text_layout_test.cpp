#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/host/debugger_text_layout.hpp>

namespace
{
namespace host = mnos::host;

using ::testing::Eq;
using ::testing::StartsWith;

constexpr std::size_t TEST_LAYOUT_MAX_COLUMNS = std::size_t{32};
constexpr std::size_t TEST_LAYOUT_CONTINUATION_INDENT = std::size_t{4};
constexpr std::string_view TEST_LAYOUT_LONG_REGISTER_LINE =
    "RAX=0x0000000000000000 RBX=0x0000000000000000 RCX=0x0000000000000000";
constexpr std::string_view TEST_LAYOUT_LONG_PAGE_LINE =
    "  PML4 table=0x0000000000011000 index=0 entry=0x0000000000011000 "
    "present=yes writable=yes";

[[nodiscard]] host::HostTextLayoutConfig make_test_config() noexcept
{
    host::HostTextLayoutConfig config;
    config.max_column_count = TEST_LAYOUT_MAX_COLUMNS;
    config.continuation_indent = TEST_LAYOUT_CONTINUATION_INDENT;
    return config;
}

[[nodiscard]] std::size_t longest_line_length(const std::string_view text) noexcept
{
    std::size_t longest = std::size_t{0};
    std::size_t current = std::size_t{0};
    for (const char character : text)
    {
        if (character == '\n')
        {
            longest = std::max(longest, current);
            current = std::size_t{0};
        }
        else
        {
            ++current;
        }
    }
    return std::max(longest, current);
}
} // namespace

TEST(HostDebuggerTextLayoutTest, WrapsSpaceSeparatedDebuggerFieldsAtColumnLimit)
{
    const std::string wrapped = host::wrap_debugger_text(TEST_LAYOUT_LONG_REGISTER_LINE, make_test_config());

    EXPECT_THAT(wrapped, Eq("RAX=0x0000000000000000\n"
                            "    RBX=0x0000000000000000\n"
                            "    RCX=0x0000000000000000"));
    EXPECT_LE(longest_line_length(wrapped), TEST_LAYOUT_MAX_COLUMNS);
}

TEST(HostDebuggerTextLayoutTest, PreservesBaseIndentForPageWalkLines)
{
    const std::string wrapped = host::wrap_debugger_text(TEST_LAYOUT_LONG_PAGE_LINE, make_test_config());

    EXPECT_THAT(wrapped, StartsWith("  PML4 table=0x0000000000011000\n      index=0"));
    EXPECT_LE(longest_line_length(wrapped), TEST_LAYOUT_MAX_COLUMNS);
}

TEST(HostDebuggerTextLayoutTest, PreservesBlankLinesAndTrailingNewline)
{
    const std::string wrapped = host::wrap_debugger_text("alpha beta\n\n", make_test_config());

    EXPECT_THAT(wrapped, Eq("alpha beta\n\n"));
}

TEST(HostDebuggerTextLayoutTest, ZeroColumnLimitKeepsOriginalText)
{
    host::HostTextLayoutConfig config = make_test_config();
    config.max_column_count = std::size_t{0};

    const std::string wrapped = host::wrap_debugger_text(TEST_LAYOUT_LONG_REGISTER_LINE, config);

    EXPECT_THAT(wrapped, Eq(TEST_LAYOUT_LONG_REGISTER_LINE));
}
