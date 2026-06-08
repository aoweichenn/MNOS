#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mnos::os::dev
{
inline constexpr std::size_t TERMINAL_DEFAULT_COLUMN_COUNT = std::size_t{80};
inline constexpr std::size_t TERMINAL_DEFAULT_ROW_COUNT = std::size_t{25};
inline constexpr char TERMINAL_BLANK_CHARACTER = ' ';
inline constexpr std::uint8_t TERMINAL_DEFAULT_ATTRIBUTE = std::uint8_t{0x07};
inline constexpr char TERMINAL_BACKSPACE_CHARACTER = '\b';
inline constexpr char TERMINAL_DELETE_CHARACTER = '\x7F';
inline constexpr char TERMINAL_NEWLINE_CHARACTER = '\n';
inline constexpr char TERMINAL_CARRIAGE_RETURN_CHARACTER = '\r';

class TextCell final
{
public:
    constexpr TextCell() noexcept = default;
    TextCell(char character, std::uint8_t attribute) noexcept;

    [[nodiscard]] char character() const noexcept;
    [[nodiscard]] std::uint8_t attribute() const noexcept;
    void set(char character, std::uint8_t attribute) noexcept;

private:
    char character_ = TERMINAL_BLANK_CHARACTER;
    std::uint8_t attribute_ = TERMINAL_DEFAULT_ATTRIBUTE;
};

class TextDisplayBuffer final
{
public:
    TextDisplayBuffer();
    TextDisplayBuffer(std::size_t column_count, std::size_t row_count);

    [[nodiscard]] std::size_t column_count() const noexcept;
    [[nodiscard]] std::size_t row_count() const noexcept;
    [[nodiscard]] std::size_t cursor_column() const noexcept;
    [[nodiscard]] std::size_t cursor_row() const noexcept;
    [[nodiscard]] std::uint64_t scroll_count() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const TextCell& cell_at(std::size_t column, std::size_t row) const;
    [[nodiscard]] std::string line(std::size_t row) const;
    [[nodiscard]] std::vector<std::string> lines() const;
    [[nodiscard]] std::string render_text() const;

    void clear();
    void write_char(char character);
    void write_string(std::string_view text);
    void erase_previous_cell();
    void set_cursor(std::size_t column, std::size_t row);

private:
    [[nodiscard]] std::size_t cell_index(std::size_t column, std::size_t row) const;
    void put_printable(char character);
    void newline();
    void carriage_return() noexcept;
    void advance_cursor_after_put();
    void scroll_up();
    void clear_row(std::size_t row);

    std::size_t column_count_;
    std::size_t row_count_;
    std::size_t cursor_column_ = std::size_t{0};
    std::size_t cursor_row_ = std::size_t{0};
    std::uint64_t scroll_count_ = std::uint64_t{0};
    std::vector<TextCell> cells_;
};

class KeyEvent final
{
public:
    explicit KeyEvent(char character) noexcept;

    [[nodiscard]] char character() const noexcept;
    [[nodiscard]] bool is_enter() const noexcept;
    [[nodiscard]] bool is_backspace() const noexcept;
    [[nodiscard]] bool is_printable_ascii() const noexcept;

private:
    char character_;
};

class KeyboardInputQueue final
{
public:
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool has_capacity() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    void set_capacity(std::size_t capacity);

    [[nodiscard]] bool push(KeyEvent event);
    [[nodiscard]] std::optional<KeyEvent> pop() noexcept;
    [[nodiscard]] std::vector<KeyEvent> drain();
    void clear() noexcept;

private:
    std::size_t capacity_ = std::size_t{0};
    std::deque<KeyEvent> events_;
};

class TerminalDevice final
{
public:
    TerminalDevice();
    TerminalDevice(std::size_t column_count, std::size_t row_count);

    [[nodiscard]] TextDisplayBuffer& display() noexcept;
    [[nodiscard]] const TextDisplayBuffer& display() const noexcept;
    [[nodiscard]] KeyboardInputQueue& keyboard() noexcept;
    [[nodiscard]] const KeyboardInputQueue& keyboard() const noexcept;

    void write_output(std::string_view text);
    [[nodiscard]] std::size_t submit_input(std::string_view text);
    [[nodiscard]] std::vector<KeyEvent> drain_input();

private:
    TextDisplayBuffer display_;
    KeyboardInputQueue keyboard_;
};
}
