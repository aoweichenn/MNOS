#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace mnos::host
{
inline constexpr std::size_t HOST_TEXT_LAYOUT_DEFAULT_MAX_COLUMN_COUNT = std::size_t{72};
inline constexpr std::size_t HOST_TEXT_LAYOUT_DEFAULT_CONTINUATION_INDENT = std::size_t{4};

struct HostTextLayoutConfig final
{
    std::size_t max_column_count = HOST_TEXT_LAYOUT_DEFAULT_MAX_COLUMN_COUNT;
    std::size_t continuation_indent = HOST_TEXT_LAYOUT_DEFAULT_CONTINUATION_INDENT;
};

[[nodiscard]] std::string wrap_debugger_text(std::string_view text, HostTextLayoutConfig config = {});
} // namespace mnos::host
