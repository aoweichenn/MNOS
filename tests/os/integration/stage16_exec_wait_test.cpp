#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/decode/executable_image.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/proc/user_loader.hpp>
#include <mnos/os/sched/thread_context.hpp>
#include <mnos/os/sched/thread_state.hpp>

namespace cpu = mnos::cpu;
namespace io = mnos::os::io;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES =
    static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{512});
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{2};
constexpr mm::VirtualAddress TEST_TEXT_BASE = mm::ADDRESS_LAYOUT_USER_TEXT_BASE;
constexpr mm::AddressValue TEST_STACK_SIZE_BYTES = mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2};
constexpr std::size_t TEST_EXEC_MAX_STEPS = std::size_t{16};
constexpr std::size_t TEST_LOOP_MAX_STEPS = std::size_t{4};
constexpr std::size_t TEST_BLOCKING_READ_STEP_COUNT = std::size_t{5};
constexpr std::int64_t TEST_EXIT_CODE = std::int64_t{42};
constexpr std::int64_t TEST_SECOND_EXIT_CODE = std::int64_t{7};
constexpr mm::VirtualAddress TEST_SECOND_PARENT_WAIT_STACK_BOTTOM{mm::AddressValue{0x7100'0000}};
constexpr mm::VirtualAddress TEST_READ_BUFFER_ADDRESS{
    mm::ADDRESS_LAYOUT_USER_STACK_TOP.value() - mm::AddressValue{16}};
constexpr std::uint64_t TEST_READ_BYTE_COUNT = std::uint64_t{1};
constexpr cpu::Byte TEST_REX_W = cpu::Byte{0x48};
constexpr cpu::Byte TEST_OPCODE_MOV_RAX_IMM64 = cpu::Byte{0xB8};
constexpr cpu::Byte TEST_OPCODE_MOV_RDX_IMM64 = cpu::Byte{0xBA};
constexpr cpu::Byte TEST_OPCODE_MOV_RBP_IMM64 = cpu::Byte{0xBD};
constexpr cpu::Byte TEST_OPCODE_MOV_RSI_IMM64 = cpu::Byte{0xBE};
constexpr cpu::Byte TEST_OPCODE_MOV_RDI_IMM64 = cpu::Byte{0xBF};
constexpr cpu::Byte TEST_OPCODE_MOV_R64_RM64 = cpu::Byte{0x8B};
constexpr cpu::Byte TEST_MODRM_RAX_RBP_DISP8 = cpu::Byte{0x45};
constexpr cpu::Byte TEST_ZERO_DISPLACEMENT = cpu::Byte{0x00};
constexpr cpu::Byte TEST_OPCODE_JMP_REL8 = cpu::Byte{0xEB};
constexpr cpu::Byte TEST_JMP_REL8_SELF_OFFSET = cpu::Byte{0xFE};
constexpr cpu::Byte TEST_OPCODE_SYSCALL_ESCAPE = cpu::Byte{0x0F};
constexpr cpu::Byte TEST_OPCODE_SYSCALL = cpu::Byte{0x05};
constexpr cpu::Byte TEST_OPCODE_HLT = cpu::Byte{0xF4};

void append_u64_le(std::vector<cpu::Byte>& bytes, const std::uint64_t value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint64_t); ++byte_index)
    {
        bytes.push_back(static_cast<cpu::Byte>(
            (value >> static_cast<unsigned>(byte_index * cpu::DATA_SIZE_BYTE_BITS)) & std::uint64_t{0xFF}));
    }
}

void append_mov_imm64(std::vector<cpu::Byte>& bytes, const cpu::Byte opcode, const std::uint64_t value)
{
    bytes.push_back(TEST_REX_W);
    bytes.push_back(opcode);
    append_u64_le(bytes, value);
}

[[nodiscard]] std::vector<cpu::Byte> make_exit_program_bytes(const std::int64_t exit_code)
{
    std::vector<cpu::Byte> bytes;
    bytes.reserve(std::size_t{24});
    append_mov_imm64(bytes, TEST_OPCODE_MOV_RAX_IMM64, static_cast<std::uint64_t>(kernel::SyscallNumber::EXIT));
    append_mov_imm64(bytes, TEST_OPCODE_MOV_RDI_IMM64, static_cast<std::uint64_t>(exit_code));
    bytes.push_back(TEST_OPCODE_SYSCALL_ESCAPE);
    bytes.push_back(TEST_OPCODE_SYSCALL);
    bytes.push_back(TEST_OPCODE_HLT);
    return bytes;
}

[[nodiscard]] std::vector<cpu::Byte> make_blocking_read_program_bytes()
{
    std::vector<cpu::Byte> bytes;
    bytes.reserve(std::size_t{43});
    append_mov_imm64(bytes, TEST_OPCODE_MOV_RAX_IMM64, static_cast<std::uint64_t>(kernel::SyscallNumber::READ));
    append_mov_imm64(bytes, TEST_OPCODE_MOV_RDI_IMM64, static_cast<std::uint64_t>(io::FileDescriptor::stdin().value()));
    append_mov_imm64(bytes, TEST_OPCODE_MOV_RSI_IMM64, TEST_READ_BUFFER_ADDRESS.value());
    append_mov_imm64(bytes, TEST_OPCODE_MOV_RDX_IMM64, TEST_READ_BYTE_COUNT);
    bytes.push_back(TEST_OPCODE_SYSCALL_ESCAPE);
    bytes.push_back(TEST_OPCODE_SYSCALL);
    bytes.push_back(TEST_OPCODE_HLT);
    return bytes;
}

[[nodiscard]] std::vector<cpu::Byte> make_kernel_address_fault_program_bytes()
{
    std::vector<cpu::Byte> bytes;
    bytes.reserve(std::size_t{15});
    append_mov_imm64(bytes, TEST_OPCODE_MOV_RBP_IMM64, mm::ADDRESS_LAYOUT_KERNEL_HIGH_BASE.value());
    bytes.push_back(TEST_REX_W);
    bytes.push_back(TEST_OPCODE_MOV_R64_RM64);
    bytes.push_back(TEST_MODRM_RAX_RBP_DISP8);
    bytes.push_back(TEST_ZERO_DISPLACEMENT);
    bytes.push_back(TEST_OPCODE_HLT);
    return bytes;
}

[[nodiscard]] std::vector<cpu::Byte> make_loop_program_bytes()
{
    return std::vector<cpu::Byte>{TEST_OPCODE_JMP_REL8, TEST_JMP_REL8_SELF_OFFSET};
}

[[nodiscard]] proc::UserProgram make_program_with_text(std::vector<cpu::Byte> text)
{
    proc::UserProgram program{TEST_TEXT_BASE};
    program.set_initial_stack_size_bytes(TEST_STACK_SIZE_BYTES);
    program.add_segment(proc::UserSegment::text(TEST_TEXT_BASE, std::move(text)));
    return program;
}

[[nodiscard]] proc::UserProgram make_halted_program()
{
    return make_program_with_text(std::vector<cpu::Byte>{TEST_OPCODE_HLT});
}

[[nodiscard]] cpu::ExecutableImage make_executable_image(const std::vector<cpu::Byte>& bytes)
{
    return cpu::ExecutableImage{
        cpu::ExecutableImage::container_type{bytes.begin(), bytes.end()},
        static_cast<cpu::InstructionPointer>(TEST_TEXT_BASE.value())};
}

void write_syscall_number(sched::ThreadContext& thread, const kernel::SyscallNumber number)
{
    thread.cpu_state().registers().write(cpu::RegisterId::RAX, static_cast<cpu::Qword>(number));
}

void write_syscall_arg0(sched::ThreadContext& thread, const cpu::Qword value)
{
    thread.cpu_state().registers().write(cpu::RegisterId::RDI, value);
}
}

TEST(Stage16ExecWaitIntegrationTest, ExecUserProgramRunsX8664SyscallExitAndRecordsTrace)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& parent = os_kernel.create_process();

    const std::vector<cpu::Byte> bytes = make_exit_program_bytes(TEST_EXIT_CODE);
    proc::UserProgram program = make_program_with_text(bytes);
    const cpu::ExecutableImage image = make_executable_image(bytes);

    const kernel::UserProcessRunResult result =
        os_kernel.exec_user_program(parent.id(), program, image, TEST_EXEC_MAX_STEPS);

    EXPECT_THAT(result.status(), Eq(kernel::UserProcessRunStatus::EXITED));
    EXPECT_TRUE(result.has_exit_code());
    EXPECT_THAT(result.exit_code(), Eq(TEST_EXIT_CODE));
    EXPECT_THAT(result.executed_step_count(), Eq(std::size_t{3}));
    EXPECT_FALSE(result.trace().empty());
    ASSERT_THAT(os_kernel.process_count(), Eq(std::size_t{2}));

    const proc::Process& child = os_kernel.process_at(std::size_t{1});
    EXPECT_THAT(child.parent_id(), Eq(parent.id()));
    EXPECT_TRUE(child.is_exited());
    EXPECT_THAT(child.exit_code(), Eq(TEST_EXIT_CODE));
    ASSERT_TRUE(os_kernel.has_last_user_execution_trace());

    const cpu::ExecutionTrace trace = os_kernel.take_last_user_execution_trace();
    ASSERT_THAT(trace.size(), Eq(result.trace().size()));
    EXPECT_THAT(trace.at(std::size_t{0}).opcode, Eq(cpu::Opcode::MOV));
    EXPECT_THAT(trace.at(std::size_t{1}).opcode, Eq(cpu::Opcode::MOV));
    EXPECT_THAT(trace.at(std::size_t{2}).opcode, Eq(cpu::Opcode::SYSCALL));
    EXPECT_FALSE(os_kernel.has_last_user_execution_trace());
}

TEST(Stage16ExecWaitIntegrationTest, WaitReapsExitedChildAndRejectsSecondWait)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& parent = os_kernel.create_process();
    sched::ThreadContext& parent_thread = os_kernel.create_thread(parent);

    const std::vector<cpu::Byte> bytes = make_exit_program_bytes(TEST_SECOND_EXIT_CODE);
    proc::UserProgram program = make_program_with_text(bytes);
    const cpu::ExecutableImage image = make_executable_image(bytes);
    const kernel::UserProcessRunResult run_result =
        os_kernel.exec_user_program(parent.id(), program, image, TEST_EXEC_MAX_STEPS);

    const kernel::ProcessWaitResult wait_result =
        os_kernel.wait_process(parent, parent_thread, run_result.process_id());

    EXPECT_THAT(wait_result.status(), Eq(kernel::ProcessWaitStatus::EXITED));
    EXPECT_TRUE(wait_result.has_exit_code());
    EXPECT_THAT(wait_result.exit_code(), Eq(TEST_SECOND_EXIT_CODE));
    EXPECT_TRUE(os_kernel.process_at(std::size_t{1}).is_reaped());

    const kernel::ProcessWaitResult repeated_wait =
        os_kernel.wait_process(parent, parent_thread, run_result.process_id());
    EXPECT_THAT(repeated_wait.status(), Eq(kernel::ProcessWaitStatus::NO_CHILD));
}

TEST(Stage16ExecWaitIntegrationTest, WaitBlocksUntilRunningChildExits)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& parent = os_kernel.create_process();
    sched::ThreadContext& parent_thread = os_kernel.create_thread(parent);
    sched::ThreadContext& second_parent_thread =
        os_kernel.create_thread(parent, TEST_SECOND_PARENT_WAIT_STACK_BOTTOM);
    proc::Process& child = os_kernel.create_user_process(make_halted_program(), parent.id());

    ASSERT_NE(os_kernel.scheduler().schedule_next(), nullptr);
    ASSERT_EQ(&os_kernel.scheduler().current(), &parent_thread);
    const kernel::ProcessWaitResult blocked_wait = os_kernel.wait_process(parent, parent_thread, child.id());

    EXPECT_THAT(blocked_wait.status(), Eq(kernel::ProcessWaitStatus::BLOCKED));
    EXPECT_THAT(parent_thread.state(), Eq(sched::ThreadState::BLOCKED));
    ASSERT_TRUE(os_kernel.scheduler().has_current());
    EXPECT_EQ(&os_kernel.scheduler().current(), &second_parent_thread);

    const kernel::ProcessWaitResult second_blocked_wait =
        os_kernel.wait_process(parent, second_parent_thread, child.id());
    EXPECT_THAT(second_blocked_wait.status(), Eq(kernel::ProcessWaitStatus::BLOCKED));
    EXPECT_THAT(second_parent_thread.state(), Eq(sched::ThreadState::BLOCKED));
    ASSERT_TRUE(os_kernel.scheduler().has_current());
    EXPECT_EQ(&os_kernel.scheduler().current(), &child.thread_at(std::size_t{0}));

    os_kernel.exit_process(child, TEST_EXIT_CODE);

    EXPECT_TRUE(child.is_exited());
    EXPECT_THAT(parent_thread.state(), Eq(sched::ThreadState::READY));
    EXPECT_THAT(second_parent_thread.state(), Eq(sched::ThreadState::READY));
    os_kernel.exit_process(child, TEST_SECOND_EXIT_CODE);
    EXPECT_THAT(child.exit_code(), Eq(TEST_EXIT_CODE));

    const kernel::ProcessWaitResult exited_wait = os_kernel.wait_process(parent, parent_thread, child.id());
    EXPECT_THAT(exited_wait.status(), Eq(kernel::ProcessWaitStatus::EXITED));
    EXPECT_THAT(exited_wait.exit_code(), Eq(TEST_EXIT_CODE));
    EXPECT_TRUE(child.is_reaped());
}

TEST(Stage16ExecWaitIntegrationTest, WaitSyscallReturnsExitCodeOrErrno)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& parent = os_kernel.create_process();
    sched::ThreadContext& parent_thread = os_kernel.create_thread(parent);
    proc::Process& child = os_kernel.create_user_process(make_halted_program(), parent.id());
    os_kernel.exit_process(child, TEST_EXIT_CODE);

    write_syscall_number(parent_thread, kernel::SyscallNumber::WAIT);
    write_syscall_arg0(parent_thread, static_cast<cpu::Qword>(child.id().value()));
    EXPECT_THAT(os_kernel.dispatch_syscall(parent, parent_thread), Eq(kernel::SyscallResult::HANDLED));
    EXPECT_THAT(parent_thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(TEST_EXIT_CODE)));

    write_syscall_number(parent_thread, kernel::SyscallNumber::WAIT);
    write_syscall_arg0(parent_thread, cpu::Qword{0});
    EXPECT_THAT(os_kernel.dispatch_syscall(parent, parent_thread), Eq(kernel::SyscallResult::INVALID_ARGUMENT));
    EXPECT_THAT(
        parent_thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::INVALID_ARGUMENT)));

    write_syscall_number(parent_thread, kernel::SyscallNumber::WAIT);
    write_syscall_arg0(parent_thread, static_cast<cpu::Qword>(child.id().value()));
    EXPECT_THAT(os_kernel.dispatch_syscall(parent, parent_thread), Eq(kernel::SyscallResult::NOT_FOUND));
    EXPECT_THAT(
        parent_thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::NO_ENTRY)));

    write_syscall_number(parent_thread, kernel::SyscallNumber::WAIT);
    write_syscall_arg0(parent_thread, static_cast<cpu::Qword>(child.id().value()));
    EXPECT_THAT(os_kernel.dispatch_syscall(parent_thread), Eq(kernel::SyscallResult::INVALID_CONTEXT));
    EXPECT_THAT(
        parent_thread.cpu_state().registers().read(cpu::RegisterId::RAX),
        Eq(kernel::syscall_error_result(kernel::SyscallError::OPERATION_NOT_SUPPORTED)));
}

TEST(Stage16ExecWaitIntegrationTest, WaitSyscallBlocksForRunningChild)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& parent = os_kernel.create_process();
    sched::ThreadContext& parent_thread = os_kernel.create_thread(parent);
    proc::Process& child = os_kernel.create_user_process(make_halted_program(), parent.id());

    write_syscall_number(parent_thread, kernel::SyscallNumber::WAIT);
    write_syscall_arg0(parent_thread, static_cast<cpu::Qword>(child.id().value()));

    EXPECT_THAT(os_kernel.dispatch_syscall(parent, parent_thread), Eq(kernel::SyscallResult::BLOCKED));
    EXPECT_THAT(parent_thread.cpu_state().registers().read(cpu::RegisterId::RAX), Eq(cpu::Qword{0}));
    EXPECT_THAT(parent_thread.state(), Eq(sched::ThreadState::BLOCKED));
}

TEST(Stage16ExecWaitIntegrationTest, ExecUserProgramReportsBlockedReadWithoutInput)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& parent = os_kernel.create_process();

    const std::vector<cpu::Byte> bytes = make_blocking_read_program_bytes();
    proc::UserProgram program = make_program_with_text(bytes);
    const cpu::ExecutableImage image = make_executable_image(bytes);

    const kernel::UserProcessRunResult result =
        os_kernel.exec_user_program(parent.id(), program, image, TEST_EXEC_MAX_STEPS);

    EXPECT_THAT(result.status(), Eq(kernel::UserProcessRunStatus::BLOCKED));
    EXPECT_FALSE(result.has_exit_code());
    EXPECT_THAT(result.executed_step_count(), Eq(TEST_BLOCKING_READ_STEP_COUNT));
    ASSERT_FALSE(result.trace().empty());
    EXPECT_THAT(result.trace().at(result.trace().size() - std::size_t{1}).opcode, Eq(cpu::Opcode::SYSCALL));
    ASSERT_THAT(os_kernel.process_count(), Eq(std::size_t{2}));
    EXPECT_THAT(os_kernel.process_at(std::size_t{1}).thread_at(std::size_t{0}).state(), Eq(sched::ThreadState::BLOCKED));
    EXPECT_TRUE(os_kernel.has_last_user_execution_trace());
}

TEST(Stage16ExecWaitIntegrationTest, ExecUserProgramKillsInvalidKernelAddressAccess)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& parent = os_kernel.create_process();

    const std::vector<cpu::Byte> bytes = make_kernel_address_fault_program_bytes();
    proc::UserProgram program = make_program_with_text(bytes);
    const cpu::ExecutableImage image = make_executable_image(bytes);

    const kernel::UserProcessRunResult result =
        os_kernel.exec_user_program(parent.id(), program, image, TEST_EXEC_MAX_STEPS);

    EXPECT_THAT(result.status(), Eq(kernel::UserProcessRunStatus::KILLED));
    EXPECT_TRUE(result.has_exit_code());
    EXPECT_THAT(result.exit_code(), Eq(kernel::KERNEL_USER_TRAP_KILLED_EXIT_CODE));
    EXPECT_THAT(result.executed_step_count(), Eq(std::size_t{2}));
    ASSERT_THAT(os_kernel.process_count(), Eq(std::size_t{2}));
    const proc::Process& child = os_kernel.process_at(std::size_t{1});
    EXPECT_TRUE(child.is_exited());
    EXPECT_THAT(child.exit_code(), Eq(kernel::KERNEL_USER_TRAP_KILLED_EXIT_CODE));
    EXPECT_TRUE(os_kernel.has_last_user_execution_trace());
}

TEST(Stage16ExecWaitIntegrationTest, ExecUserProgramReportsMaxStepsForRunningLoop)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& parent = os_kernel.create_process();

    const std::vector<cpu::Byte> bytes = make_loop_program_bytes();
    proc::UserProgram program = make_program_with_text(bytes);
    const cpu::ExecutableImage image = make_executable_image(bytes);

    const kernel::UserProcessRunResult result =
        os_kernel.exec_user_program(parent.id(), program, image, TEST_LOOP_MAX_STEPS);

    EXPECT_THAT(result.status(), Eq(kernel::UserProcessRunStatus::MAX_STEPS));
    EXPECT_FALSE(result.has_exit_code());
    EXPECT_THAT(result.executed_step_count(), Eq(TEST_LOOP_MAX_STEPS));
    ASSERT_THAT(result.trace().size(), Eq(TEST_LOOP_MAX_STEPS));
    EXPECT_THAT(result.trace().at(std::size_t{0}).opcode, Eq(cpu::Opcode::JMP));
    ASSERT_THAT(os_kernel.process_count(), Eq(std::size_t{2}));
    EXPECT_TRUE(os_kernel.process_at(std::size_t{1}).is_running());
    EXPECT_TRUE(os_kernel.has_last_user_execution_trace());
}
