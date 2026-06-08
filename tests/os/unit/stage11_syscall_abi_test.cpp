#include <stdexcept>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/register/id.hpp>
#include <mnos/os/proc/process_context.hpp>
#include <mnos/os/proc/process_id.hpp>
#include <mnos/os/kernel/syscall.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace kernel = mnos::os::kernel;
namespace proc = mnos::os::proc;

namespace
{
using ::testing::Eq;

constexpr cpu::Qword TEST_SYSCALL_ARG0 = cpu::Qword{0x1000};
constexpr cpu::Qword TEST_SYSCALL_ARG1 = cpu::Qword{0x2000};
constexpr cpu::Qword TEST_SYSCALL_ARG2 = cpu::Qword{0x3000};
constexpr cpu::Qword TEST_SYSCALL_ARG3 = cpu::Qword{0x4000};
constexpr cpu::Qword TEST_SYSCALL_ARG4 = cpu::Qword{0x5000};
constexpr cpu::Qword TEST_SYSCALL_ARG5 = cpu::Qword{0x6000};
constexpr cpu::Qword TEST_SYSCALL_RESULT = cpu::Qword{0xCAFE};
constexpr proc::ProcessId TEST_PROCESS_ID{7};
constexpr proc::ProcessId TEST_TOO_LARGE_PCID{
    static_cast<proc::ProcessId::value_type>(cpu_memory::PROCESS_CONTEXT_ID_MAX_VALUE) + proc::ProcessId::value_type{1}};
}

TEST(Stage11SyscallAbiTest, MapsExpandedSyscallCatalog)
{
    EXPECT_TRUE(kernel::is_syscall_number_valid(kernel::SyscallNumber::GETPID));
    EXPECT_TRUE(kernel::is_syscall_number_valid(kernel::SyscallNumber::MAP_ANON_PAGE));
    EXPECT_TRUE(kernel::is_syscall_number_valid(kernel::SyscallNumber::FORK_COW));
    EXPECT_TRUE(kernel::is_syscall_number_valid(kernel::SyscallNumber::FUTEX_WAIT));
    EXPECT_TRUE(kernel::is_syscall_number_valid(kernel::SyscallNumber::FUTEX_WAKE_ONE));
    EXPECT_TRUE(kernel::is_syscall_number_valid(kernel::SyscallNumber::FUTEX_WAKE_ALL));
    EXPECT_FALSE(kernel::is_syscall_number_valid(kernel::SyscallNumber::COUNT));

    EXPECT_THAT(kernel::syscall_number_to_index(kernel::SyscallNumber::MAP_ANON_PAGE), Eq(std::size_t{3}));
    EXPECT_THAT(kernel::syscall_number_to_name(kernel::SyscallNumber::FUTEX_WAKE_ALL), Eq(std::string_view{"FUTEX_WAKE_ALL"}));
    EXPECT_THAT(kernel::syscall_number_from_raw(cpu::Qword{7}), Eq(kernel::SyscallNumber::FUTEX_WAKE_ALL));
    EXPECT_THAT(kernel::syscall_number_from_raw(cpu::Qword{8}), Eq(kernel::SyscallNumber::COUNT));
    EXPECT_THAT(kernel::syscall_argument_to_index(kernel::SyscallArgument::ARG5), Eq(std::size_t{5}));
    EXPECT_THAT(kernel::SYSCALL_UNSUPPORTED_RESULT, Eq(kernel::syscall_error_result(kernel::SyscallError::NO_SYS)));
}

TEST(Stage11SyscallAbiTest, ReadsX8664ArgumentRegistersAndWritesRaxResult)
{
    cpu::CpuState cpu_state;
    cpu_state.registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(kernel::SyscallNumber::FORK_COW));
    cpu_state.registers().write(cpu::RegisterId::RDI, TEST_SYSCALL_ARG0);
    cpu_state.registers().write(cpu::RegisterId::RSI, TEST_SYSCALL_ARG1);
    cpu_state.registers().write(cpu::RegisterId::RDX, TEST_SYSCALL_ARG2);
    cpu_state.registers().write(cpu::RegisterId::R10, TEST_SYSCALL_ARG3);
    cpu_state.registers().write(cpu::RegisterId::R8, TEST_SYSCALL_ARG4);
    cpu_state.registers().write(cpu::RegisterId::R9, TEST_SYSCALL_ARG5);

    kernel::SyscallFrame frame{cpu_state};

    EXPECT_THAT(frame.number(), Eq(kernel::SyscallNumber::FORK_COW));
    EXPECT_THAT(kernel::syscall_argument_register(kernel::SyscallArgument::ARG0), Eq(cpu::RegisterId::RDI));
    EXPECT_THAT(kernel::syscall_argument_register(kernel::SyscallArgument::ARG3), Eq(cpu::RegisterId::R10));
    EXPECT_THAT(frame.argument(kernel::SyscallArgument::ARG0), Eq(TEST_SYSCALL_ARG0));
    EXPECT_THAT(frame.argument(kernel::SyscallArgument::ARG1), Eq(TEST_SYSCALL_ARG1));
    EXPECT_THAT(frame.argument(kernel::SyscallArgument::ARG2), Eq(TEST_SYSCALL_ARG2));
    EXPECT_THAT(frame.argument(kernel::SyscallArgument::ARG3), Eq(TEST_SYSCALL_ARG3));
    EXPECT_THAT(frame.argument(kernel::SyscallArgument::ARG4), Eq(TEST_SYSCALL_ARG4));
    EXPECT_THAT(frame.argument(kernel::SyscallArgument::ARG5), Eq(TEST_SYSCALL_ARG5));
    EXPECT_FALSE(kernel::is_syscall_argument_valid(kernel::SyscallArgument::COUNT));
    EXPECT_THROW(
        static_cast<void>(frame.argument(kernel::SyscallArgument::COUNT)),
        std::out_of_range);

    frame.set_result(TEST_SYSCALL_RESULT);
    EXPECT_THAT(cpu_state.registers().read(cpu::RegisterId::RAX), Eq(TEST_SYSCALL_RESULT));
    frame.set_error(kernel::SyscallError::BAD_ADDRESS);
    EXPECT_THAT(cpu_state.registers().read(cpu::RegisterId::RAX), Eq(kernel::syscall_error_result(kernel::SyscallError::BAD_ADDRESS)));
}

TEST(Stage11SyscallAbiTest, MapsProcessIdsToX8664Pcid)
{
    EXPECT_THAT(proc::process_context_id_for(TEST_PROCESS_ID).value(), Eq(TEST_PROCESS_ID.value()));
    EXPECT_THROW(static_cast<void>(proc::process_context_id_for(TEST_TOO_LARGE_PCID)), std::out_of_range);
}
