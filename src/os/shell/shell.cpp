#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/sched/thread_state.hpp>
#include <mnos/os/shell/shell.hpp>

namespace
{
constexpr std::size_t SHELL_BUILTIN_COUNT = std::size_t{8};
constexpr std::string_view SHELL_UNKNOWN_COMMAND_PREFIX = "unknown command: ";
constexpr std::string_view SHELL_PARSE_ERROR_UNTERMINATED_QUOTE = "parse error: unterminated quote\n";
constexpr std::string_view SHELL_ARGUMENT_SEPARATOR = " ";
constexpr std::string_view SHELL_LINE_ENDING = "\n";
constexpr const char* SHELL_COMMAND_MISSING_MESSAGE = "shell parse result does not contain a command";
constexpr const char* SHELL_ARGUMENT_INDEX_OUT_OF_RANGE_MESSAGE = "shell command argument index is out of range";
constexpr const char* SHELL_BUILTIN_INDEX_OUT_OF_RANGE_MESSAGE = "shell builtin index is out of range";

using BuiltinHandler = mnos::os::shell::ShellCommandResult (*)(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext&);

struct ShellBuiltinSpec final
{
    std::string_view name;
    BuiltinHandler handler;
};

[[nodiscard]] bool shell_character_is_whitespace(const char character) noexcept
{
    return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

[[nodiscard]] bool shell_character_is_quote(const char character) noexcept
{
    return character == '\'' || character == '"';
}

void shell_write(mnos::os::shell::ShellContext& context, const std::string_view text)
{
    context.os_kernel().console_write(text);
}

[[nodiscard]] std::string shell_join_arguments(const mnos::os::shell::ShellCommand& command)
{
    std::string output;
    for (std::size_t argument_index = std::size_t{0}; argument_index < command.argument_count(); ++argument_index)
    {
        if (argument_index != std::size_t{0})
        {
            output.append(SHELL_ARGUMENT_SEPARATOR);
        }
        output.append(command.argument_at(argument_index));
    }
    output.append(SHELL_LINE_ENDING);
    return output;
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_help(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_clear(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_echo(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_ps(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_mem(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_cpu(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_ticks(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);
[[nodiscard]] mnos::os::shell::ShellCommandResult handle_exit(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context);

[[nodiscard]] const std::array<ShellBuiltinSpec, SHELL_BUILTIN_COUNT>& shell_builtin_catalog() noexcept
{
    static constexpr std::array<ShellBuiltinSpec, SHELL_BUILTIN_COUNT> CATALOG{
        ShellBuiltinSpec{"help", handle_help},
        ShellBuiltinSpec{"clear", handle_clear},
        ShellBuiltinSpec{"echo", handle_echo},
        ShellBuiltinSpec{"ps", handle_ps},
        ShellBuiltinSpec{"mem", handle_mem},
        ShellBuiltinSpec{"cpu", handle_cpu},
        ShellBuiltinSpec{"ticks", handle_ticks},
        ShellBuiltinSpec{"exit", handle_exit}};
    return CATALOG;
}

[[nodiscard]] std::string shell_help_text()
{
    std::string output{"builtins:"};
    for (const ShellBuiltinSpec& builtin : shell_builtin_catalog())
    {
        output.append(SHELL_ARGUMENT_SEPARATOR);
        output.append(builtin.name);
    }
    output.append(SHELL_LINE_ENDING);
    return output;
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_help(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext& context)
{
    shell_write(context, shell_help_text());
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_clear(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext& context)
{
    context.os_kernel().console().clear();
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_echo(
    const mnos::os::shell::ShellCommand& command,
    mnos::os::shell::ShellContext& context)
{
    shell_write(context, shell_join_arguments(command));
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_ps(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext& context)
{
    mnos::os::kernel::Kernel& os_kernel = context.os_kernel();
    std::string output{"pid threads states\n"};
    for (std::size_t process_index = std::size_t{0}; process_index < os_kernel.process_count(); ++process_index)
    {
        const mnos::os::proc::Process& process = os_kernel.process_at(process_index);
        output.append(std::to_string(process.id().value()));
        output.append(SHELL_ARGUMENT_SEPARATOR);
        output.append(std::to_string(process.thread_count()));
        output.append(SHELL_ARGUMENT_SEPARATOR);
        for (std::size_t thread_index = std::size_t{0}; thread_index < process.thread_count(); ++thread_index)
        {
            if (thread_index != std::size_t{0})
            {
                output.push_back(',');
            }
            output.append(mnos::os::sched::thread_state_to_name(process.thread_at(thread_index).state()));
        }
        output.append(SHELL_LINE_ENDING);
    }
    shell_write(context, output);
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_mem(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext& context)
{
    const mnos::os::mm::PhysicalPageAllocator& allocator = context.os_kernel().physical_page_allocator();
    std::string output{"memory_pages total="};
    output.append(std::to_string(allocator.total_page_count()));
    output.append(" free=");
    output.append(std::to_string(allocator.free_page_count()));
    output.append(" allocated=");
    output.append(std::to_string(allocator.allocated_page_count()));
    output.append(SHELL_LINE_ENDING);
    shell_write(context, output);
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_cpu(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext& context)
{
    const mnos::os::kernel::Kernel& os_kernel = context.os_kernel();
    std::string output{"cores="};
    output.append(std::to_string(os_kernel.bootstrap_processor_count()));
    output.append(" ready_threads=");
    output.append(std::to_string(os_kernel.scheduler().ready_count()));
    output.append(" running=");
    output.append(os_kernel.scheduler().has_current() ? "1" : "0");
    output.append(SHELL_LINE_ENDING);
    shell_write(context, output);
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_ticks(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext& context)
{
    std::string output{"ticks="};
    output.append(std::to_string(context.os_kernel().scheduler_tick_count()));
    output.append(SHELL_LINE_ENDING);
    shell_write(context, output);
    return mnos::os::shell::ShellCommandResult::handled();
}

[[nodiscard]] mnos::os::shell::ShellCommandResult handle_exit(
    const mnos::os::shell::ShellCommand&,
    mnos::os::shell::ShellContext&)
{
    return mnos::os::shell::ShellCommandResult::exit_requested();
}
}

namespace mnos::os::shell
{
ShellCommand::ShellCommand(std::string name, std::vector<std::string> arguments) :
    name_(std::move(name)),
    arguments_(std::move(arguments))
{
}

std::string_view ShellCommand::name() const noexcept
{
    return this->name_;
}

const std::vector<std::string>& ShellCommand::arguments() const noexcept
{
    return this->arguments_;
}

std::size_t ShellCommand::argument_count() const noexcept
{
    return this->arguments_.size();
}

std::string_view ShellCommand::argument_at(const std::size_t index) const
{
    if (index >= this->arguments_.size())
    {
        throw std::out_of_range{SHELL_ARGUMENT_INDEX_OUT_OF_RANGE_MESSAGE};
    }
    return this->arguments_[index];
}

ShellParseResult ShellParseResult::command(ShellCommand command)
{
    return ShellParseResult{ShellParseStatus::COMMAND, std::move(command)};
}

ShellParseResult ShellParseResult::empty()
{
    return ShellParseResult{ShellParseStatus::EMPTY, std::nullopt};
}

ShellParseResult ShellParseResult::unterminated_quote()
{
    return ShellParseResult{ShellParseStatus::UNTERMINATED_QUOTE, std::nullopt};
}

ShellParseResult::ShellParseResult(
    const ShellParseStatus status,
    std::optional<ShellCommand> command) :
    status_(status),
    command_(std::move(command))
{
}

ShellParseStatus ShellParseResult::status() const noexcept
{
    return this->status_;
}

bool ShellParseResult::has_command() const noexcept
{
    return this->command_.has_value();
}

const ShellCommand& ShellParseResult::command() const
{
    if (!this->command_.has_value())
    {
        throw std::logic_error{SHELL_COMMAND_MISSING_MESSAGE};
    }
    return this->command_.value();
}

ShellParseResult ShellParser::parse(const std::string_view line) const
{
    std::vector<std::string> tokens;
    std::string token;
    bool token_started = false;
    bool escaped = false;
    bool quoted = false;
    char quote_character = '\0';

    for (const char character : line)
    {
        if (escaped)
        {
            token.push_back(character);
            token_started = true;
            escaped = false;
            continue;
        }

        if (character == '\\')
        {
            escaped = true;
            token_started = true;
            continue;
        }

        if (quoted)
        {
            if (character == quote_character)
            {
                quoted = false;
                continue;
            }
            token.push_back(character);
            token_started = true;
            continue;
        }

        if (shell_character_is_quote(character))
        {
            quoted = true;
            quote_character = character;
            token_started = true;
            continue;
        }

        if (shell_character_is_whitespace(character))
        {
            if (token_started)
            {
                tokens.push_back(std::move(token));
                token.clear();
                token_started = false;
            }
            continue;
        }

        token.push_back(character);
        token_started = true;
    }

    if (quoted)
    {
        return ShellParseResult::unterminated_quote();
    }
    if (escaped)
    {
        token.push_back('\\');
    }
    if (token_started)
    {
        tokens.push_back(std::move(token));
    }
    if (tokens.empty())
    {
        return ShellParseResult::empty();
    }

    std::string command_name = std::move(tokens.front());
    std::vector<std::string> arguments;
    arguments.reserve(tokens.size() - std::size_t{1});
    for (std::size_t token_index = std::size_t{1}; token_index < tokens.size(); ++token_index)
    {
        arguments.push_back(std::move(tokens[token_index]));
    }
    return ShellParseResult::command(ShellCommand{std::move(command_name), std::move(arguments)});
}

ShellCommandResult ShellCommandResult::handled() noexcept
{
    return ShellCommandResult{ShellCommandStatus::HANDLED};
}

ShellCommandResult ShellCommandResult::empty() noexcept
{
    return ShellCommandResult{ShellCommandStatus::EMPTY};
}

ShellCommandResult ShellCommandResult::unknown_command() noexcept
{
    return ShellCommandResult{ShellCommandStatus::UNKNOWN_COMMAND};
}

ShellCommandResult ShellCommandResult::parse_error() noexcept
{
    return ShellCommandResult{ShellCommandStatus::PARSE_ERROR};
}

ShellCommandResult ShellCommandResult::exit_requested() noexcept
{
    return ShellCommandResult{ShellCommandStatus::EXIT_REQUESTED};
}

ShellCommandResult::ShellCommandResult(const ShellCommandStatus status) noexcept : status_(status)
{
}

ShellCommandStatus ShellCommandResult::status() const noexcept
{
    return this->status_;
}

bool ShellCommandResult::is_handled() const noexcept
{
    return this->status_ == ShellCommandStatus::HANDLED;
}

ShellContext::ShellContext(kernel::Kernel& os_kernel) noexcept : os_kernel_(&os_kernel)
{
}

kernel::Kernel& ShellContext::os_kernel() noexcept
{
    return *this->os_kernel_;
}

const kernel::Kernel& ShellContext::os_kernel() const noexcept
{
    return *this->os_kernel_;
}

bool ShellBuiltinRegistry::contains(const std::string_view name) const noexcept
{
    return std::ranges::any_of(
        shell_builtin_catalog(),
        [name](const ShellBuiltinSpec& builtin) noexcept
        {
            return builtin.name == name;
        });
}

std::size_t ShellBuiltinRegistry::size() const noexcept
{
    return shell_builtin_catalog().size();
}

std::string_view ShellBuiltinRegistry::name_at(const std::size_t index) const
{
    const std::array<ShellBuiltinSpec, SHELL_BUILTIN_COUNT>& catalog = shell_builtin_catalog();
    if (index >= catalog.size())
    {
        throw std::out_of_range{SHELL_BUILTIN_INDEX_OUT_OF_RANGE_MESSAGE};
    }
    return catalog[index].name;
}

ShellCommandResult ShellBuiltinRegistry::execute(const ShellCommand& command, ShellContext& context) const
{
    const std::array<ShellBuiltinSpec, SHELL_BUILTIN_COUNT>& catalog = shell_builtin_catalog();
    const auto builtin = std::ranges::find_if(
        catalog,
        [&command](const ShellBuiltinSpec& spec) noexcept
        {
            return spec.name == command.name();
        });
    if (builtin == catalog.end())
    {
        std::string output{SHELL_UNKNOWN_COMMAND_PREFIX};
        output.append(command.name());
        output.append(SHELL_LINE_ENDING);
        shell_write(context, output);
        return ShellCommandResult::unknown_command();
    }
    return builtin->handler(command, context);
}

Shell::Shell(kernel::Kernel& os_kernel) noexcept : context_(os_kernel)
{
}

bool Shell::running() const noexcept
{
    return this->running_;
}

ShellCommandResult Shell::execute_line(const std::string_view line)
{
    const ShellParseResult parse_result = this->parser_.parse(line);
    switch (parse_result.status())
    {
    case ShellParseStatus::EMPTY:
        return ShellCommandResult::empty();
    case ShellParseStatus::UNTERMINATED_QUOTE:
        shell_write(this->context_, SHELL_PARSE_ERROR_UNTERMINATED_QUOTE);
        return ShellCommandResult::parse_error();
    case ShellParseStatus::COMMAND:
    {
        const ShellCommandResult result = this->builtins_.execute(parse_result.command(), this->context_);
        if (result.status() == ShellCommandStatus::EXIT_REQUESTED)
        {
            this->running_ = false;
        }
        return result;
    }
    case ShellParseStatus::COUNT:
    default:
        shell_write(this->context_, SHELL_PARSE_ERROR_UNTERMINATED_QUOTE);
        return ShellCommandResult::parse_error();
    }
}
}
