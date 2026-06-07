#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/decode/executable_image.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/execution/trace.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/page_table_builder.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/privilege.hpp>
#include <mnos/cpu/system/trap_controller.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = 128;
constexpr cpu::Address64 TEST_MEMORY_STORED_ADDRESS = cpu::Address64{72};
constexpr cpu::Address64 TEST_INDEXED_MEMORY_STORED_ADDRESS = cpu::Address64{80};
constexpr cpu::Address64 TEST_RIP_RELATIVE_LOAD_ADDRESS = cpu::Address64{16};
constexpr cpu::Qword TEST_EXPECTED_VALUE = cpu::Qword{42};
constexpr cpu::Qword TEST_SKIPPED_VALUE = cpu::Qword{13};
constexpr std::size_t TEST_MAIN_PROGRAM_STEP_COUNT = 8;
constexpr std::size_t TEST_RIP_RELATIVE_PROGRAM_STEP_COUNT = 2;
constexpr std::size_t TEST_INDEXED_PROGRAM_STEP_COUNT = 4;
constexpr std::size_t TEST_BRANCH_PROGRAM_STEP_COUNT = 5;
constexpr std::size_t TEST_SINGLE_HLT_STEP_COUNT = 1;
constexpr std::size_t TEST_LOOP_MAX_STEPS = 3;
constexpr cpu::Address64 TEST_STAGE2_STACK_TOP = cpu::Address64{120};
constexpr cpu::Address64 TEST_STAGE2_LOAD_ADDRESS = cpu::Address64{32};
constexpr std::size_t TEST_STAGE2_STACK_BYTE_PROGRAM_STEP_COUNT = 7;
constexpr std::size_t TEST_STAGE2_LOGIC_BYTE_PROGRAM_STEP_COUNT = 8;
constexpr cpu::Qword TEST_STAGE2_SIGN_EXTEND_EXPECTED = cpu::Qword{0xFFFF'FFFF'FFFF'FF80ULL};
constexpr cpu::Address64 TEST_STAGE3_SYSCALL_HANDLER_RIP = cpu::Address64{23};
constexpr cpu::Qword TEST_STAGE3_USER_STACK_TOP = cpu::Qword{120};
constexpr cpu::Qword TEST_STAGE3_KERNEL_STACK_TOP = cpu::Qword{96};
constexpr std::size_t TEST_STAGE3_SYSCALL_BYTE_PROGRAM_STEP_COUNT = 6;
constexpr std::size_t TEST_STAGE4_MEMORY_SIZE_BYTES = 128 * 1024;
constexpr cpu::Address64 TEST_STAGE4_ROOT_TABLE = cpu::Address64{0x1000};
constexpr cpu::Address64 TEST_STAGE4_NEXT_TABLE = cpu::Address64{0x2000};
constexpr cpu::Address64 TEST_STAGE4_PAGE_FAULT_HANDLER_RIP = cpu::Address64{1};
constexpr cpu::Qword TEST_STAGE4_KERNEL_STACK_TOP = cpu::Qword{0xE000};
constexpr cpu::Qword TEST_STAGE4_USER_STACK_TOP = cpu::Qword{0xF000};
constexpr cpu::Qword TEST_STAGE4_NX_FAULT_ERROR =
    cpu_memory::PAGE_FAULT_ERROR_PRESENT_BIT |
    cpu_memory::PAGE_FAULT_ERROR_USER_BIT |
    cpu_memory::PAGE_FAULT_ERROR_INSTRUCTION_FETCH_BIT;
constexpr cpu::Qword TEST_STAGE6_COMPARE_VALUE = cpu::Qword{10};
constexpr cpu::Qword TEST_STAGE6_EXCHANGE_VALUE = cpu::Qword{42};
constexpr cpu::Qword TEST_STAGE6_ADD_VALUE = cpu::Qword{5};
constexpr std::size_t TEST_STAGE6_ATOMIC_BYTE_PROGRAM_STEP_COUNT = 8;
constexpr std::size_t TEST_STAGE7_INVLPG_BYTE_PROGRAM_STEP_COUNT = 3;
constexpr cpu::Address64 TEST_STAGE7_INVLPG_LINEAR_PAGE = cpu::Address64{0x4000};
constexpr cpu::Address64 TEST_STAGE7_INVLPG_PHYSICAL_FRAME = cpu::Address64{0xA000};
constexpr cpu::Address64 TEST_STAGE7_INVLPG_LEAF_ENTRY = cpu::Address64{0xB000};
constexpr cpu_memory::ProcessContextId TEST_STAGE7_PCID{19};
}

TEST(ByteExecutorTest, RunsDecodedByteProgramThroughMemoryAndBranching)
{
    cpu::ExecutableImage image{
        0x48, 0xBD, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBP, 64
        0x48, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 1
        0x48, 0x81, 0xC0, 0x29, 0x00, 0x00, 0x00,                   // ADD RAX, 41
        0x48, 0x89, 0x45, 0x08,                                     // MOV [RBP + 8], RAX
        0x48, 0x8B, 0x5D, 0x08,                                     // MOV RBX, [RBP + 8]
        0x48, 0x81, 0xFB, 0x2A, 0x00, 0x00, 0x00,                   // CMP RBX, 42
        0x74, 0x0A,                                                 // JE +10
        0x48, 0xB9, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RCX, 13
        0xF4};                                                      // HLT
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu::CpuState state;
    cpu::Executor executor;
    cpu::ExecutionTrace trace;
    trace.reserve(TEST_MAIN_PROGRAM_STEP_COUNT);

    const std::size_t executed_steps = executor.run(state, image, memory_bus, image.size(), &trace);

    EXPECT_THAT(executed_steps, Eq(TEST_MAIN_PROGRAM_STEP_COUNT));
    EXPECT_THAT(trace.size(), Eq(TEST_MAIN_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(state.is_halted());
    EXPECT_TRUE(state.flags().read(cpu::FlagId::ZF));
    EXPECT_THAT(state.rip(), Eq(image.end_rip()));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(TEST_EXPECTED_VALUE));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(TEST_EXPECTED_VALUE));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RCX), Eq(cpu::Qword{0}));
    EXPECT_THAT(memory.read_qword(TEST_MEMORY_STORED_ADDRESS), Eq(TEST_EXPECTED_VALUE));
}

TEST(ByteExecutorTest, ExecutesRipRelativeLoad)
{
    cpu::ExecutableImage image{
        0x48, 0x8B, 0x05, 0x09, 0x00, 0x00, 0x00, // MOV RAX, [RIP + 9]
        0xF4};                                    // HLT
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    memory.write_qword(TEST_RIP_RELATIVE_LOAD_ADDRESS, TEST_EXPECTED_VALUE);
    cpu::MemoryBus memory_bus{memory};
    cpu::CpuState state;
    cpu::Executor executor;

    const std::size_t executed_steps = executor.run(state, image, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_RIP_RELATIVE_PROGRAM_STEP_COUNT));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(TEST_EXPECTED_VALUE));
}

TEST(ByteExecutorTest, ExecutesSibIndexedLoad)
{
    cpu::ExecutableImage image{
        0x48, 0xB8, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 2
        0x48, 0xBD, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBP, 64
        0x48, 0x8B, 0x5C, 0x85, 0x08,                               // MOV RBX, [RBP + RAX*4 + 8]
        0xF4};                                                       // HLT
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    memory.write_qword(TEST_INDEXED_MEMORY_STORED_ADDRESS, TEST_SKIPPED_VALUE);
    cpu::MemoryBus memory_bus{memory};
    cpu::CpuState state;
    cpu::Executor executor;

    const std::size_t executed_steps = executor.run(state, image, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_INDEXED_PROGRAM_STEP_COUNT));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(TEST_SKIPPED_VALUE));
}

TEST(ByteExecutorTest, RunsByteImageWithoutMemoryBus)
{
    cpu::ExecutableImage image{0xF4}; // HLT
    cpu::CpuState state;
    cpu::Executor executor;

    const std::size_t executed_steps = executor.run(state, image);

    EXPECT_THAT(executed_steps, Eq(TEST_SINGLE_HLT_STEP_COUNT));
    EXPECT_TRUE(state.is_halted());
    EXPECT_THAT(state.rip(), Eq(image.end_rip()));
}

TEST(ByteExecutorTest, SupportsImageStepWithMemoryBusAndHaltedState)
{
    cpu::ExecutableImage image{0xF4}; // HLT
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu::CpuState state;
    cpu::Executor executor;

    EXPECT_THAT(executor.step(state, image, memory_bus), Eq(cpu::StepResult::HALTED));
    EXPECT_TRUE(state.is_halted());
    EXPECT_THAT(executor.step(state, image, memory_bus), Eq(cpu::StepResult::HALTED));
}

TEST(ByteExecutorTest, ExecutesJeFallthroughAndJneTaken)
{
    cpu::ExecutableImage image{
        0x48, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 1
        0x48, 0x81, 0xF8, 0x02, 0x00, 0x00, 0x00,                   // CMP RAX, 2
        0x74, 0x0A,                                                 // JE +10
        0x75, 0x0A,                                                 // JNE +10
        0x48, 0xBB, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBX, 13
        0xF4};                                                      // HLT
    cpu::CpuState state;
    cpu::Executor executor;

    const std::size_t executed_steps = executor.run(state, image);

    EXPECT_THAT(executed_steps, Eq(TEST_BRANCH_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(state.is_halted());
    EXPECT_FALSE(state.flags().read(cpu::FlagId::ZF));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(cpu::Qword{0}));
}

TEST(ByteExecutorTest, EnforcesMaxStepLimitForByteImage)
{
    cpu::ExecutableImage image{0xEB, 0xFE}; // JMP -2
    cpu::CpuState state;
    cpu::Executor executor;

    EXPECT_THROW(static_cast<void>(executor.run(state, image, TEST_LOOP_MAX_STEPS)), std::runtime_error);
}

TEST(ByteExecutorTest, RejectsDecodedJumpOutsideImage)
{
    cpu::ExecutableImage image{0xEB, 0x7F};
    cpu::CpuState state;
    cpu::Executor executor;

    EXPECT_THROW(static_cast<void>(executor.step(state, image)), std::out_of_range);
}

TEST(ByteExecutorTest, ExecutesStage2StackCallAndReturnByteImage)
{
    cpu::ExecutableImage image{
        0x48, 0xBC, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RSP, 120
        0xE8, 0x0B, 0x00, 0x00, 0x00,                               // CALL +11
        0x48, 0xBB, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBX, 42
        0xF4,                                                       // HLT
        0x6A, 0x0D,                                                 // PUSH 13
        0x58,                                                       // POP RAX
        0xC3};                                                      // RET
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu::CpuState state;
    cpu::Executor executor;

    const std::size_t executed_steps = executor.run(state, image, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_STAGE2_STACK_BYTE_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(state.is_halted());
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(cpu::Qword{13}));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(TEST_EXPECTED_VALUE));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSP), Eq(TEST_STAGE2_STACK_TOP));
}

TEST(ByteExecutorTest, ExecutesStage2LogicalConditionAndExtensionByteImage)
{
    cpu::ExecutableImage image{
        0x48, 0xBD, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBP, 32
        0x48, 0xB8, 0xF0, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 0xF0F0
        0x48, 0x81, 0xE0, 0x0F, 0x0F, 0x00, 0x00,                   // AND RAX, 0x0F0F
        0x0F, 0x94, 0xC3,                                           // SETE BL
        0x48, 0x0F, 0x44, 0xCB,                                     // CMOVE RCX, RBX
        0x48, 0x0F, 0xB6, 0x55, 0x00,                               // MOVZX RDX, BYTE [RBP]
        0x48, 0x0F, 0xBE, 0x75, 0x01,                               // MOVSX RSI, BYTE [RBP + 1]
        0xF4};                                                      // HLT
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    memory.write_byte(TEST_STAGE2_LOAD_ADDRESS, cpu::Byte{0xF0});
    memory.write_byte(TEST_STAGE2_LOAD_ADDRESS + cpu::Address64{1}, cpu::Byte{0x80});
    cpu::MemoryBus memory_bus{memory};
    cpu::CpuState state;
    cpu::Executor executor;

    const std::size_t executed_steps = executor.run(state, image, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_STAGE2_LOGIC_BYTE_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::ZF));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(cpu::Qword{0}));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(cpu::Qword{1}));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RCX), Eq(cpu::Qword{1}));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RDX), Eq(cpu::Qword{0xF0}));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSI), Eq(TEST_STAGE2_SIGN_EXTEND_EXPECTED));
}

TEST(ByteExecutorTest, ExecutesStage3SyscallAndSysretByteImage)
{
    cpu::ExecutableImage image{
        0x48, 0xBC, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RSP, 120
        0x0F, 0x05,                                                 // SYSCALL
        0x48, 0xB8, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 42
        0xF4,                                                       // HLT
        0x48, 0xBB, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBX, 13
        0x48, 0x0F, 0x07};                                          // SYSRETQ

    cpu_system::TrapController trap_controller;
    trap_controller.configure_syscall(cpu_system::SyscallDescriptor::enabled(TEST_STAGE3_SYSCALL_HANDLER_RIP));
    trap_controller.tss().set_privilege_stack(cpu_system::PrivilegeLevel::RING0, TEST_STAGE3_KERNEL_STACK_TOP);

    cpu::CpuState state;
    state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    state.flags().write(cpu::FlagId::IF, true);
    cpu::Executor executor;
    executor.attach_trap_controller(trap_controller);

    const std::size_t executed_steps = executor.run(state, image);

    EXPECT_THAT(executed_steps, Eq(TEST_STAGE3_SYSCALL_BYTE_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(state.is_halted());
    EXPECT_FALSE(state.has_pending_trap());
    EXPECT_THAT(state.privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::IF));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSP), Eq(TEST_STAGE3_USER_STACK_TOP));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(TEST_EXPECTED_VALUE));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(TEST_SKIPPED_VALUE));
}

TEST(ByteExecutorTest, ExecutesStage6AtomicByteImage)
{
    cpu::ExecutableImage image{
        0x48, 0xBD, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBP, 64
        0x48, 0xB8, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 10
        0x48, 0xBB, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBX, 42
        0xF0, 0x48, 0x0F, 0xB1, 0x5D, 0x08,                         // LOCK CMPXCHG [RBP + 8], RBX
        0x48, 0xB9, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RCX, 5
        0xF0, 0x48, 0x0F, 0xC1, 0x4D, 0x08,                         // LOCK XADD [RBP + 8], RCX
        0x0F, 0xAE, 0xF0,                                           // MFENCE
        0xF4};                                                      // HLT
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    memory.write_qword(TEST_MEMORY_STORED_ADDRESS, TEST_STAGE6_COMPARE_VALUE);
    cpu::MemoryBus memory_bus{memory};
    cpu::CpuState state;
    cpu::Executor executor;

    const std::size_t executed_steps = executor.run(state, image, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_STAGE6_ATOMIC_BYTE_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(state.is_halted());
    EXPECT_THAT(memory.read_qword(TEST_MEMORY_STORED_ADDRESS), Eq(TEST_STAGE6_EXCHANGE_VALUE + TEST_STAGE6_ADD_VALUE));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RCX), Eq(TEST_STAGE6_EXCHANGE_VALUE));
    EXPECT_FALSE(state.flags().read(cpu::FlagId::ZF));
}

TEST(ByteExecutorTest, ExecutesStage7InvlpgByteImage)
{
    cpu::ExecutableImage image{
        0x48, 0xB8, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 0x4000
        0x0F, 0x01, 0x38,                                           // INVLPG [RAX]
        0xF4};                                                      // HLT
    cpu::CpuState state;
    state.paging().set_process_context_id_enabled(true);
    state.paging().load_cr3(
        TEST_STAGE4_ROOT_TABLE,
        TEST_STAGE7_PCID,
        cpu_memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
    cpu::Executor executor;
    executor.mmu().tlb().insert(
        cpu_memory::PageTranslation{
            TEST_STAGE7_INVLPG_LINEAR_PAGE,
            TEST_STAGE7_INVLPG_PHYSICAL_FRAME,
            cpu_memory::PAGE_SIZE_4K_BYTES,
            cpu_memory::PagePermissions::kernel_read_write_execute(),
            TEST_STAGE7_INVLPG_LEAF_ENTRY,
            false},
        state.paging().generation(),
        TEST_STAGE7_PCID);

    const std::size_t executed_steps = executor.run(state, image);

    EXPECT_THAT(executed_steps, Eq(TEST_STAGE7_INVLPG_BYTE_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(state.is_halted());
    EXPECT_EQ(
        executor.mmu().tlb().lookup(TEST_STAGE7_INVLPG_LINEAR_PAGE, state.paging().generation(), TEST_STAGE7_PCID),
        nullptr);
}

TEST(ByteExecutorTest, RaisesStage4ExecuteDisablePageFaultBeforeExecutingImageInstruction)
{
    cpu::ExecutableImage image{0xF4}; // HLT
    cpu::PhysicalMemory memory(TEST_STAGE4_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::PageTableBuilder page_table_builder{
        memory_bus,
        TEST_STAGE4_ROOT_TABLE,
        TEST_STAGE4_NEXT_TABLE};
    page_table_builder.clear_root_table();
    page_table_builder.map_4k(
        cpu::Address64{0},
        cpu::Address64{0},
        cpu_memory::PagePermissions::user_read_write_no_execute());

    cpu_system::TrapController trap_controller;
    trap_controller.idt().set_gate(
        cpu_system::InterruptVector::page_fault(),
        cpu_system::InterruptGate::interrupt_gate(
            TEST_STAGE4_PAGE_FAULT_HANDLER_RIP,
            cpu_system::PrivilegeLevel::RING0));
    trap_controller.tss().set_privilege_stack(cpu_system::PrivilegeLevel::RING0, TEST_STAGE4_KERNEL_STACK_TOP);

    cpu::CpuState state;
    state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    state.registers().write(cpu::RegisterId::RSP, TEST_STAGE4_USER_STACK_TOP);
    state.paging().load_cr3(TEST_STAGE4_ROOT_TABLE);
    state.paging().enable();
    cpu::Executor executor;
    executor.attach_trap_controller(trap_controller);

    EXPECT_THAT(executor.step(state, image, memory_bus), Eq(cpu::StepResult::EXECUTED));
    EXPECT_FALSE(state.is_halted());
    EXPECT_TRUE(state.has_pending_trap());
    EXPECT_THAT(state.pending_trap().vector(), Eq(cpu_system::InterruptVector::page_fault()));
    EXPECT_THAT(state.pending_trap().error_code(), Eq(TEST_STAGE4_NX_FAULT_ERROR));
    EXPECT_THAT(state.paging().page_fault_linear_address(), Eq(cpu::Address64{0}));
    EXPECT_THAT(state.rip(), Eq(TEST_STAGE4_PAGE_FAULT_HANDLER_RIP));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSP), Eq(TEST_STAGE4_KERNEL_STACK_TOP));
}
