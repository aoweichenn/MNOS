#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cpu/support/cpu_test_helpers.hpp>
#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace cpu = mnos::cpu;
namespace cpu_support = mnos::test::cpu_support;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MACHINE_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{16});
constexpr std::uint32_t TEST_BOOTSTRAP_PROCESSOR_COUNT = std::uint32_t{2};
constexpr mm::AddressValue TEST_KERNEL_STACK_BOTTOM_VALUE = mm::AddressValue{0x20000};
constexpr cpu::Address64 TEST_MEMORY_BASE_ADDRESS = cpu::Address64{64};
constexpr cpu::SignedQword TEST_MEMORY_BASE_REGISTER_VALUE = static_cast<cpu::SignedQword>(TEST_MEMORY_BASE_ADDRESS);
constexpr cpu::SignedQword TEST_MEMORY_DISPLACEMENT = cpu::SignedQword{8};
constexpr cpu::Address64 TEST_STORED_ADDRESS =
    TEST_MEMORY_BASE_ADDRESS + static_cast<cpu::Address64>(TEST_MEMORY_DISPLACEMENT);
constexpr cpu::SignedQword TEST_OS_THREAD_VALUE = cpu::SignedQword{0x4D4E4F53};
constexpr std::size_t TEST_PROGRAM_STEP_COUNT = 5;
}

TEST(KernelBootIntegrationTest, BootedKernelRunsAThreadContextThroughCpuExecutor)
{
    platform::Machine machine(TEST_MACHINE_MEMORY_SIZE_BYTES);
    kernel::BootContext boot_context{machine, TEST_BOOTSTRAP_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};

    os_kernel.boot();

    sched::ThreadContext thread{sched::ThreadId::first_kernel_thread(), mm::VirtualAddress{TEST_KERNEL_STACK_BOTTOM_VALUE}};
    cpu::Program program{
        cpu_support::make_mov_imm(cpu::RegisterId::RBP, TEST_MEMORY_BASE_REGISTER_VALUE),
        cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_OS_THREAD_VALUE),
        cpu::Instruction::make_mov(
            cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD),
            cpu::Operand::reg(cpu::RegisterId::RAX)),
        cpu::Instruction::make_mov(
            cpu::Operand::reg(cpu::RegisterId::RBX),
            cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD)),
        cpu::Instruction::make_hlt(),
    };

    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(thread.cpu_state(), program, boot_context.memory_bus());

    EXPECT_TRUE(os_kernel.is_booted());
    EXPECT_THAT(os_kernel.bootstrap_processor_count(), Eq(TEST_BOOTSTRAP_PROCESSOR_COUNT));
    EXPECT_THAT(executed_steps, Eq(TEST_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(thread.cpu_state().is_halted());
    EXPECT_THAT(machine.physical_memory().read_qword(TEST_STORED_ADDRESS), Eq(static_cast<cpu::Qword>(TEST_OS_THREAD_VALUE)));
    EXPECT_THAT(thread.cpu_state().registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::Qword>(TEST_OS_THREAD_VALUE)));
}
