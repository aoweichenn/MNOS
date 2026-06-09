#include <mnos/host/debugger_text_layout.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace
{
constexpr char HOST_TEXT_LAYOUT_SPACE = ' ';
constexpr char HOST_TEXT_LAYOUT_LINE_FEED = '\n';

[[nodiscard]] std::size_t leading_space_count(const std::string_view line) noexcept
{
    std::size_t count = std::size_t{0};
    while (count < line.size() && line[count] == HOST_TEXT_LAYOUT_SPACE)
    {
        ++count;
    }
    return count;
}

[[nodiscard]] std::size_t next_token_end(const std::string_view line, const std::size_t token_start) noexcept
{
    std::size_t token_end = token_start;
    while (token_end < line.size() && line[token_end] != HOST_TEXT_LAYOUT_SPACE)
    {
        ++token_end;
    }
    return token_end;
}

[[nodiscard]] std::size_t next_token_start(const std::string_view line,
                                           const std::size_t search_start) noexcept
{
    std::size_t token_start = search_start;
    while (token_start < line.size() && line[token_start] == HOST_TEXT_LAYOUT_SPACE)
    {
        ++token_start;
    }
    return token_start;
}

void append_indent(std::string& output, const std::size_t indent_count)
{
    output.append(indent_count, HOST_TEXT_LAYOUT_SPACE);
}

void append_wrapped_line(std::string& output, const std::string_view line,
                         const mnos::host::HostTextLayoutConfig& config)
{
    const std::size_t leading_count = leading_space_count(line);
    const std::size_t continuation_count = leading_count + config.continuation_indent;
    std::size_t current_column_count = std::size_t{0};
    std::size_t token_start = next_token_start(line, leading_count);

    append_indent(output, leading_count);
    current_column_count = leading_count;

    while (token_start < line.size())
    {
        const std::size_t token_end = next_token_end(line, token_start);
        const std::string_view token = line.substr(token_start, token_end - token_start);
        const bool has_text_on_line = current_column_count > leading_count;
        const std::size_t separator_width = has_text_on_line ? std::size_t{1} : std::size_t{0};
        const std::size_t next_column_count = current_column_count + separator_width + token.size();

        if (has_text_on_line && next_column_count > config.max_column_count)
        {
            output.push_back(HOST_TEXT_LAYOUT_LINE_FEED);
            append_indent(output, continuation_count);
            current_column_count = continuation_count;
        }
        else if (has_text_on_line)
        {
            output.push_back(HOST_TEXT_LAYOUT_SPACE);
            current_column_count += separator_width;
        }

        output.append(token);
        current_column_count += token.size();
        token_start = next_token_start(line, token_end);
    }
}
} // namespace

namespace mnos::host
{
std::string wrap_debugger_text(const std::string_view text, const HostTextLayoutConfig config)
{
    if (text.empty() || config.max_column_count == std::size_t{0})
    {
        return std::string{text};
    }

    std::string output;
    output.reserve(text.size() + (text.size() / std::max(config.max_column_count, std::size_t{1})));

    std::size_t line_start = std::size_t{0};
    bool has_previous_line = false;
    while (line_start < text.size())
    {
        const std::size_t line_end = text.find(HOST_TEXT_LAYOUT_LINE_FEED, line_start);
        const bool is_last_line = line_end == std::string_view::npos;
        const std::string_view line =
            is_last_line ? text.substr(line_start) : text.substr(line_start, line_end - line_start);

        if (has_previous_line)
        {
            output.push_back(HOST_TEXT_LAYOUT_LINE_FEED);
        }
        append_wrapped_line(output, line, config);
        has_previous_line = true;

        if (is_last_line)
        {
            break;
        }
        line_start = line_end + std::size_t{1};
    }

    if (!text.empty() && text.back() == HOST_TEXT_LAYOUT_LINE_FEED)
    {
        output.push_back(HOST_TEXT_LAYOUT_LINE_FEED);
    }
    return output;
}
} // namespace mnos::host
