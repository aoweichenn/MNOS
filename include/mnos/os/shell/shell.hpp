#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mnos::os::kernel
{
class Kernel;
}

namespace mnos::os::shell
{
struct ShellBuiltinInfo final
{
    std::string_view name;
    std::string_view syntax;
    std::string_view description;
};

enum class ShellParseStatus : std::uint8_t
{
    COMMAND,
    EMPTY,
    UNTERMINATED_QUOTE,
    COUNT
};

enum class ShellCommandStatus : std::uint8_t
{
    HANDLED,
    EMPTY,
    UNKNOWN_COMMAND,
    PARSE_ERROR,
    EXIT_REQUESTED,
    COUNT
};

class ShellCommand final
{
public:
    ShellCommand(std::string name, std::vector<std::string> arguments);

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] const std::vector<std::string>& arguments() const noexcept;
    [[nodiscard]] std::size_t argument_count() const noexcept;
    [[nodiscard]] std::string_view argument_at(std::size_t index) const;

private:
    std::string name_;
    std::vector<std::string> arguments_;
};

class ShellParseResult final
{
public:
    [[nodiscard]] static ShellParseResult command(ShellCommand command);
    [[nodiscard]] static ShellParseResult empty();
    [[nodiscard]] static ShellParseResult unterminated_quote();

    [[nodiscard]] ShellParseStatus status() const noexcept;
    [[nodiscard]] bool has_command() const noexcept;
    [[nodiscard]] const ShellCommand& command() const;

private:
    ShellParseResult(ShellParseStatus status, std::optional<ShellCommand> command);

    ShellParseStatus status_;
    std::optional<ShellCommand> command_;
};

class ShellParser final
{
public:
    [[nodiscard]] ShellParseResult parse(std::string_view line) const;
};

class ShellCommandResult final
{
public:
    [[nodiscard]] static ShellCommandResult handled() noexcept;
    [[nodiscard]] static ShellCommandResult empty() noexcept;
    [[nodiscard]] static ShellCommandResult unknown_command() noexcept;
    [[nodiscard]] static ShellCommandResult parse_error() noexcept;
    [[nodiscard]] static ShellCommandResult exit_requested() noexcept;

    [[nodiscard]] ShellCommandStatus status() const noexcept;
    [[nodiscard]] bool is_handled() const noexcept;

private:
    explicit ShellCommandResult(ShellCommandStatus status) noexcept;

    ShellCommandStatus status_;
};

class ShellContext final
{
public:
    explicit ShellContext(kernel::Kernel& os_kernel) noexcept;

    [[nodiscard]] kernel::Kernel& os_kernel() noexcept;
    [[nodiscard]] const kernel::Kernel& os_kernel() const noexcept;

private:
    kernel::Kernel* os_kernel_;
};

class ShellBuiltinRegistry final
{
public:
    [[nodiscard]] bool contains(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<ShellBuiltinInfo> find(std::string_view name) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] ShellBuiltinInfo info_at(std::size_t index) const;
    [[nodiscard]] std::string_view name_at(std::size_t index) const;
    [[nodiscard]] std::string_view syntax_at(std::size_t index) const;
    [[nodiscard]] std::string_view description_at(std::size_t index) const;
    [[nodiscard]] ShellCommandResult execute(const ShellCommand& command, ShellContext& context) const;
};

class Shell final
{
public:
    explicit Shell(kernel::Kernel& os_kernel) noexcept;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] ShellCommandResult execute_line(std::string_view line);

private:
    ShellContext context_;
    ShellParser parser_;
    ShellBuiltinRegistry builtins_;
    bool running_ = true;
};
}
