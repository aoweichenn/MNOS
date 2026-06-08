#include <algorithm>
#include <limits>
#include <stdexcept>

#include <mnos/os/dev/terminal.hpp>

namespace
{
constexpr const char* TEXT_DISPLAY_INVALID_GEOMETRY_MESSAGE = "text display requires non-zero geometry";
constexpr const char* TEXT_DISPLAY_GEOMETRY_OVERFLOW_MESSAGE = "text display geometry overflows cell count";
constexpr const char* TEXT_DISPLAY_CELL_OUT_OF_RANGE_MESSAGE = "text display cell is out of range";
constexpr std::size_t TERMINAL_UNBOUNDED_KEYBOARD_CAPACITY = std::size_t{0};
constexpr unsigned char TERMINAL_PRINTABLE_ASCII_FIRST = static_cast<unsigned char>(' ');
constexpr unsigned char TERMINAL_PRINTABLE_ASCII_LAST = static_cast<unsigned char>('~');

[[nodiscard]] std::size_t checked_text_display_cell_count(
    const std::size_t column_count,
    const std::size_t row_count)
{
    if (column_count == std::size_t{0} || row_count == std::size_t{0})
    {
        throw std::invalid_argument{TEXT_DISPLAY_INVALID_GEOMETRY_MESSAGE};
    }
    if (column_count > (std::numeric_limits<std::size_t>::max() / row_count))
    {
        throw std::overflow_error{TEXT_DISPLAY_GEOMETRY_OVERFLOW_MESSAGE};
    }
    return column_count * row_count;
}
}

namespace mnos::os::dev
{
TextCell::TextCell(const char character, const std::uint8_t attribute) noexcept :
    character_(character),
    attribute_(attribute)
{
}

char TextCell::character() const noexcept
{
    return this->character_;
}

std::uint8_t TextCell::attribute() const noexcept
{
    return this->attribute_;
}

void TextCell::set(const char character, const std::uint8_t attribute) noexcept
{
    this->character_ = character;
    this->attribute_ = attribute;
}

TextDisplayBuffer::TextDisplayBuffer() :
    TextDisplayBuffer(TERMINAL_DEFAULT_COLUMN_COUNT, TERMINAL_DEFAULT_ROW_COUNT)
{
}

TextDisplayBuffer::TextDisplayBuffer(const std::size_t column_count, const std::size_t row_count) :
    column_count_(column_count),
    row_count_(row_count),
    cells_(checked_text_display_cell_count(column_count, row_count))
{
}

std::size_t TextDisplayBuffer::column_count() const noexcept
{
    return this->column_count_;
}

std::size_t TextDisplayBuffer::row_count() const noexcept
{
    return this->row_count_;
}

std::size_t TextDisplayBuffer::cursor_column() const noexcept
{
    return this->cursor_column_;
}

std::size_t TextDisplayBuffer::cursor_row() const noexcept
{
    return this->cursor_row_;
}

std::uint64_t TextDisplayBuffer::scroll_count() const noexcept
{
    return this->scroll_count_;
}

bool TextDisplayBuffer::empty() const noexcept
{
    return std::ranges::all_of(
        this->cells_,
        [](const TextCell& cell) noexcept {
            return cell.character() == TERMINAL_BLANK_CHARACTER;
        });
}

const TextCell& TextDisplayBuffer::cell_at(const std::size_t column, const std::size_t row) const
{
    return this->cells_[this->cell_index(column, row)];
}

std::string TextDisplayBuffer::line(const std::size_t row) const
{
    if (row >= this->row_count_)
    {
        throw std::out_of_range{TEXT_DISPLAY_CELL_OUT_OF_RANGE_MESSAGE};
    }

    std::string text;
    text.reserve(this->column_count_);
    for (std::size_t column = std::size_t{0}; column < this->column_count_; ++column)
    {
        text.push_back(this->cell_at(column, row).character());
    }
    return text;
}

std::vector<std::string> TextDisplayBuffer::lines() const
{
    std::vector<std::string> rendered_lines;
    rendered_lines.reserve(this->row_count_);
    for (std::size_t row = std::size_t{0}; row < this->row_count_; ++row)
    {
        rendered_lines.push_back(this->line(row));
    }
    return rendered_lines;
}

std::string TextDisplayBuffer::render_text() const
{
    std::string text;
    const std::size_t newline_count = this->row_count_ - std::size_t{1};
    text.reserve((this->column_count_ * this->row_count_) + newline_count);
    for (std::size_t row = std::size_t{0}; row < this->row_count_; ++row)
    {
        if (row > std::size_t{0})
        {
            text.push_back(TERMINAL_NEWLINE_CHARACTER);
        }
        text.append(this->line(row));
    }
    return text;
}

void TextDisplayBuffer::clear()
{
    for (TextCell& cell : this->cells_)
    {
        cell.set(TERMINAL_BLANK_CHARACTER, TERMINAL_DEFAULT_ATTRIBUTE);
    }
    this->cursor_column_ = std::size_t{0};
    this->cursor_row_ = std::size_t{0};
}

void TextDisplayBuffer::write_char(const char character)
{
    if (character == TERMINAL_NEWLINE_CHARACTER)
    {
        this->newline();
        return;
    }
    if (character == TERMINAL_CARRIAGE_RETURN_CHARACTER)
    {
        this->carriage_return();
        return;
    }
    if (character == TERMINAL_BACKSPACE_CHARACTER || character == TERMINAL_DELETE_CHARACTER)
    {
        this->erase_previous_cell();
        return;
    }
    this->put_printable(character);
}

void TextDisplayBuffer::write_string(const std::string_view text)
{
    for (const char character : text)
    {
        this->write_char(character);
    }
}

void TextDisplayBuffer::erase_previous_cell()
{
    if (this->cursor_column_ == std::size_t{0})
    {
        if (this->cursor_row_ == std::size_t{0})
        {
            return;
        }
        --this->cursor_row_;
        this->cursor_column_ = this->column_count_ - std::size_t{1};
    }
    else
    {
        --this->cursor_column_;
    }
    this->cells_[this->cell_index(this->cursor_column_, this->cursor_row_)]
        .set(TERMINAL_BLANK_CHARACTER, TERMINAL_DEFAULT_ATTRIBUTE);
}

void TextDisplayBuffer::set_cursor(const std::size_t column, const std::size_t row)
{
    if (column >= this->column_count_ || row >= this->row_count_)
    {
        throw std::out_of_range{TEXT_DISPLAY_CELL_OUT_OF_RANGE_MESSAGE};
    }
    this->cursor_column_ = column;
    this->cursor_row_ = row;
}

std::size_t TextDisplayBuffer::cell_index(const std::size_t column, const std::size_t row) const
{
    if (column >= this->column_count_ || row >= this->row_count_)
    {
        throw std::out_of_range{TEXT_DISPLAY_CELL_OUT_OF_RANGE_MESSAGE};
    }
    return (row * this->column_count_) + column;
}

void TextDisplayBuffer::put_printable(const char character)
{
    this->cells_[this->cell_index(this->cursor_column_, this->cursor_row_)]
        .set(character, TERMINAL_DEFAULT_ATTRIBUTE);
    this->advance_cursor_after_put();
}

void TextDisplayBuffer::newline()
{
    this->cursor_column_ = std::size_t{0};
    ++this->cursor_row_;
    if (this->cursor_row_ >= this->row_count_)
    {
        this->scroll_up();
    }
}

void TextDisplayBuffer::carriage_return() noexcept
{
    this->cursor_column_ = std::size_t{0};
}

void TextDisplayBuffer::advance_cursor_after_put()
{
    ++this->cursor_column_;
    if (this->cursor_column_ >= this->column_count_)
    {
        this->newline();
    }
}

void TextDisplayBuffer::scroll_up()
{
    if (this->row_count_ <= std::size_t{1})
    {
        this->clear_row(std::size_t{0});
        this->cursor_row_ = std::size_t{0};
        this->cursor_column_ = std::size_t{0};
        ++this->scroll_count_;
        return;
    }

    for (std::size_t row = std::size_t{1}; row < this->row_count_; ++row)
    {
        for (std::size_t column = std::size_t{0}; column < this->column_count_; ++column)
        {
            this->cells_[this->cell_index(column, row - std::size_t{1})] =
                this->cells_[this->cell_index(column, row)];
        }
    }
    this->clear_row(this->row_count_ - std::size_t{1});
    this->cursor_row_ = this->row_count_ - std::size_t{1};
    this->cursor_column_ = std::size_t{0};
    ++this->scroll_count_;
}

void TextDisplayBuffer::clear_row(const std::size_t row)
{
    for (std::size_t column = std::size_t{0}; column < this->column_count_; ++column)
    {
        this->cells_[this->cell_index(column, row)].set(TERMINAL_BLANK_CHARACTER, TERMINAL_DEFAULT_ATTRIBUTE);
    }
}

KeyEvent::KeyEvent(const char character) noexcept : character_(character)
{
}

char KeyEvent::character() const noexcept
{
    return this->character_;
}

bool KeyEvent::is_enter() const noexcept
{
    return this->character_ == TERMINAL_NEWLINE_CHARACTER ||
        this->character_ == TERMINAL_CARRIAGE_RETURN_CHARACTER;
}

bool KeyEvent::is_backspace() const noexcept
{
    return this->character_ == TERMINAL_BACKSPACE_CHARACTER ||
        this->character_ == TERMINAL_DELETE_CHARACTER;
}

bool KeyEvent::is_printable_ascii() const noexcept
{
    const auto character = static_cast<unsigned char>(this->character_);
    return character >= TERMINAL_PRINTABLE_ASCII_FIRST && character <= TERMINAL_PRINTABLE_ASCII_LAST;
}

bool KeyboardInputQueue::empty() const noexcept
{
    return this->events_.empty();
}

std::size_t KeyboardInputQueue::size() const noexcept
{
    return this->events_.size();
}

bool KeyboardInputQueue::has_capacity() const noexcept
{
    return this->capacity_ == TERMINAL_UNBOUNDED_KEYBOARD_CAPACITY || this->events_.size() < this->capacity_;
}

std::size_t KeyboardInputQueue::capacity() const noexcept
{
    return this->capacity_;
}

void KeyboardInputQueue::set_capacity(const std::size_t capacity)
{
    this->capacity_ = capacity;
    while (this->capacity_ != TERMINAL_UNBOUNDED_KEYBOARD_CAPACITY && this->events_.size() > this->capacity_)
    {
        this->events_.pop_front();
    }
}

bool KeyboardInputQueue::push(const KeyEvent event)
{
    if (!this->has_capacity())
    {
        return false;
    }
    this->events_.push_back(event);
    return true;
}

std::optional<KeyEvent> KeyboardInputQueue::pop() noexcept
{
    if (this->events_.empty())
    {
        return std::nullopt;
    }
    KeyEvent event = this->events_.front();
    this->events_.pop_front();
    return event;
}

std::vector<KeyEvent> KeyboardInputQueue::drain()
{
    std::vector<KeyEvent> drained_events;
    drained_events.reserve(this->events_.size());
    while (std::optional<KeyEvent> event = this->pop())
    {
        drained_events.push_back(event.value());
    }
    return drained_events;
}

void KeyboardInputQueue::clear() noexcept
{
    this->events_.clear();
}

TerminalDevice::TerminalDevice() :
    TerminalDevice(TERMINAL_DEFAULT_COLUMN_COUNT, TERMINAL_DEFAULT_ROW_COUNT)
{
}

TerminalDevice::TerminalDevice(const std::size_t column_count, const std::size_t row_count) :
    display_(column_count, row_count)
{
}

TextDisplayBuffer& TerminalDevice::display() noexcept
{
    return this->display_;
}

const TextDisplayBuffer& TerminalDevice::display() const noexcept
{
    return this->display_;
}

KeyboardInputQueue& TerminalDevice::keyboard() noexcept
{
    return this->keyboard_;
}

const KeyboardInputQueue& TerminalDevice::keyboard() const noexcept
{
    return this->keyboard_;
}

void TerminalDevice::write_output(const std::string_view text)
{
    this->display_.write_string(text);
}

std::size_t TerminalDevice::submit_input(const std::string_view text)
{
    std::size_t accepted_count = std::size_t{0};
    for (const char character : text)
    {
        if (!this->keyboard_.push(KeyEvent{character}))
        {
            break;
        }
        ++accepted_count;
    }
    return accepted_count;
}

std::vector<KeyEvent> TerminalDevice::drain_input()
{
    return this->keyboard_.drain();
}
}
