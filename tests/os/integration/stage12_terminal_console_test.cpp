#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace sched = mnos::os::sched;
namespace tty = mnos::os::tty;

namespace
{
using ::testing::Eq;
using ::testing::SizeIs;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{96});
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{2};
constexpr std::string_view TEST_PROMPT = "MNOS> ";
constexpr std::string_view TEST_COMMAND = "mem\n";

[[nodiscard]] std::string_view read_prefix(const std::array<char, 16>& buffer, const std::size_t size) noexcept
{
    return std::string_view{buffer.data(), size};
}
}

TEST(Stage12TerminalConsoleIntegrationTest, MachineBootContextAndKernelExposeSharedTerminal)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    ASSERT_TRUE(os_kernel.has_stage12_services());
    EXPECT_EQ(&boot_context.terminal_device(), &machine.terminal_device());
    EXPECT_EQ(&os_kernel.console().terminal(), &machine.terminal_device());

    os_kernel.console_write(TEST_PROMPT);
    const std::string prompt_line = machine.terminal_device().display().line(std::size_t{0});
    EXPECT_THAT(prompt_line.substr(std::size_t{0}, TEST_PROMPT.size()), Eq(TEST_PROMPT));
}

TEST(Stage12TerminalConsoleIntegrationTest, ConsoleReadBlocksCurrentThreadAndInputWakesIt)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    mnos::os::proc::Process& process = os_kernel.create_process();
    sched::ThreadContext& reader = os_kernel.create_thread(process);
    sched::ThreadContext& worker = os_kernel.create_thread(process);
    ASSERT_EQ(os_kernel.scheduler().schedule_next(), &reader);

    std::array<char, 16> buffer{};
    const tty::ConsoleReadResult blocked = os_kernel.console_read(reader, buffer);
    EXPECT_TRUE(blocked.is_blocked());
    EXPECT_THAT(reader.state(), Eq(sched::ThreadState::BLOCKED));
    ASSERT_TRUE(os_kernel.scheduler().has_current());
    EXPECT_EQ(&os_kernel.scheduler().current(), &worker);
    EXPECT_THAT(os_kernel.console().waiting_reader_count(), Eq(std::size_t{1}));

    os_kernel.console_write(TEST_PROMPT);
    const std::vector<sched::ThreadContext*> readers = os_kernel.submit_terminal_input(TEST_COMMAND);
    ASSERT_THAT(readers, SizeIs(1));
    EXPECT_EQ(readers.at(std::size_t{0}), &reader);
    EXPECT_THAT(reader.state(), Eq(sched::ThreadState::READY));
    EXPECT_THAT(os_kernel.console().waiting_reader_count(), Eq(std::size_t{0}));
    const std::string command_line = machine.terminal_device().display().line(std::size_t{0});
    const std::string expected_echo = std::string{TEST_PROMPT.data(), TEST_PROMPT.size()} + "mem";
    EXPECT_THAT(command_line.substr(std::size_t{0}, expected_echo.size()), Eq(expected_echo));

    ASSERT_EQ(os_kernel.scheduler().schedule_next(), &reader);
    const tty::ConsoleReadResult ready = os_kernel.console_read(reader, buffer);
    EXPECT_TRUE(ready.is_ready());
    EXPECT_THAT(ready.byte_count(), Eq(TEST_COMMAND.size()));
    EXPECT_THAT(read_prefix(buffer, ready.byte_count()), Eq(TEST_COMMAND));
}
