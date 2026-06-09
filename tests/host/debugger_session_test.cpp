#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/execution/trace.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/host/debugger_session.hpp>
#include <mnos/os/dev/terminal.hpp>
#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>

namespace
{
namespace cpu = mnos::cpu;
namespace host = mnos::host;
namespace io = mnos::os::io;
namespace mm = mnos::os::mm;

using ::testing::Eq;
using ::testing::HasSubstr;

constexpr std::string_view TEST_DEBUGGER_TITLE = "MNOS Test Debugger";
constexpr std::string_view TEST_NOT_BOOTED_TEXT = "not booted";
constexpr std::string_view TEST_PROMPT = "mnos> ";
constexpr cpu::SignedQword TEST_TRACE_PROGRAM_VALUE = cpu::SignedQword{7};
constexpr std::size_t TEST_UNBOOTABLE_MEMORY_SIZE_BYTES =
    static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES / mm::AddressValue{2});
constexpr std::size_t TEST_TRACE_PROGRAM_MAX_STEPS = std::size_t{4};

[[nodiscard]] std::size_t count_substring(const std::string_view text, const std::string_view needle) noexcept
{
    std::size_t count = std::size_t{0};
    std::size_t offset = std::size_t{0};
    while (offset < text.size())
    {
        const std::size_t match_offset = text.find(needle, offset);
        if (match_offset == std::string_view::npos)
        {
            break;
        }
        ++count;
        offset = match_offset + needle.size();
    }
    return count;
}

[[nodiscard]] host::HostDebuggerSession make_titled_debugger_session()
{
    host::HostDebuggerSessionConfig config;
    config.title = std::string{TEST_DEBUGGER_TITLE};
    return host::HostDebuggerSession{config};
}

[[nodiscard]] cpu::ExecutionTrace make_real_cpu_execution_trace()
{
    cpu::Program program{
        cpu::Instruction::make_mov(
            cpu::Operand::reg(cpu::RegisterId::RAX),
            cpu::Operand::imm(TEST_TRACE_PROGRAM_VALUE)),
        cpu::Instruction::make_hlt(),
    };

    cpu::CpuState state;
    cpu::Executor executor;
    cpu::ExecutionTrace trace;
    static_cast<void>(executor.run(state, program, TEST_TRACE_PROGRAM_MAX_STEPS, &trace));
    return trace;
}
}

TEST(HostDebuggerSessionTest, FrameBeforeBootDescribesCreatedMachine)
{
    host::HostDebuggerSession session = make_titled_debugger_session();

    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_FALSE(frame.booted);
    EXPECT_FALSE(frame.accepts_input);
    EXPECT_THAT(frame.run_state, Eq(host::HostDebuggerRunState::PAUSED));
    EXPECT_THAT(frame.trace_entry_count, Eq(std::size_t{0}));
    EXPECT_THAT(frame.title, Eq(TEST_DEBUGGER_TITLE));
    EXPECT_THAT(frame.snapshot.status, Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(frame.display_text, HasSubstr(TEST_NOT_BOOTED_TEXT));
    EXPECT_THAT(frame.run_control_text, HasSubstr("debugger_run_state=PAUSED"));
    EXPECT_THAT(frame.run_control_text, HasSubstr("instruction_trace_entries=0"));
    EXPECT_THAT(frame.status_text, HasSubstr("state=CREATED"));
    EXPECT_THAT(frame.status_text, HasSubstr("booted=no"));
    EXPECT_THAT(frame.cpu_text, HasSubstr("cpu unavailable"));
    EXPECT_THAT(frame.registers_text, HasSubstr("registers unavailable"));
    EXPECT_THAT(frame.paging_text, HasSubstr("paging unavailable"));
    EXPECT_THAT(frame.trace_text, HasSubstr("trace empty"));
    EXPECT_THAT(frame.instruction_trace_text, HasSubstr("instruction trace empty"));
    EXPECT_THAT(frame.summary_text, HasSubstr(TEST_DEBUGGER_TITLE));
    EXPECT_THAT(frame.processor_text, HasSubstr("processors=2"));
}

TEST(HostDebuggerSessionTest, RunStateCatalogIsStable)
{
    EXPECT_TRUE(host::is_host_debugger_run_state_valid(host::HostDebuggerRunState::PAUSED));
    EXPECT_TRUE(host::is_host_debugger_run_state_valid(host::HostDebuggerRunState::RUNNING));
    EXPECT_FALSE(host::is_host_debugger_run_state_valid(host::HostDebuggerRunState::COUNT));
    EXPECT_THAT(host::host_debugger_run_state_to_index(host::HostDebuggerRunState::RUNNING), Eq(std::size_t{1}));
    EXPECT_THAT(host::host_debugger_run_state_to_name(host::HostDebuggerRunState::PAUSED), Eq("PAUSED"));
    EXPECT_THAT(host::host_debugger_run_state_to_name(host::HostDebuggerRunState::COUNT), Eq("<invalid>"));
}

TEST(HostDebuggerSessionTest, PreBootInputOperationsAreSafeNoops)
{
    host::HostDebuggerSession session = make_titled_debugger_session();
    const host::HostDebuggerSession& const_session = session;

    EXPECT_THAT(const_session.config().title, Eq(TEST_DEBUGGER_TITLE));
    EXPECT_FALSE(const_session.machine_session().booted());
    EXPECT_FALSE(session.machine_session().booted());
    EXPECT_THAT(session.pump_until_waiting(), Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(session.submit_text("echo ignored"), Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(session.submit_command_line("echo ignored"), Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(
        session.submit_special_key(host::HostSpecialKey::ENTER),
        Eq(host::HostMachineSessionStatus::CREATED));

    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_FALSE(frame.booted);
    EXPECT_FALSE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{0}));
    EXPECT_THAT(frame.trace_entry_count, Eq(std::size_t{4}));
    EXPECT_THAT(frame.status_text, HasSubstr("state=CREATED"));
    EXPECT_THAT(frame.trace_text, HasSubstr("action=pump"));
    EXPECT_THAT(frame.trace_text, HasSubstr("skipped"));
}

TEST(HostDebuggerSessionTest, BootProducesPromptAndMachineSummary)
{
    host::HostDebuggerSession session = make_titled_debugger_session();

    session.boot();
    session.boot();
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.booted);
    EXPECT_TRUE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.status, Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(frame.display_column_count, Eq(mnos::os::dev::TERMINAL_DEFAULT_COLUMN_COUNT));
    EXPECT_THAT(frame.display_row_count, Eq(mnos::os::dev::TERMINAL_DEFAULT_ROW_COUNT));
    EXPECT_THAT(frame.display_text, HasSubstr(TEST_PROMPT));
    EXPECT_THAT(frame.run_control_text, HasSubstr("trace_entries=2"));
    EXPECT_THAT(frame.run_control_text, HasSubstr("instruction_trace_entries=0"));
    EXPECT_THAT(frame.status_text, HasSubstr("accepts_input=yes"));
    EXPECT_THAT(frame.counters_text, HasSubstr("commands=0"));
    EXPECT_THAT(frame.memory_text, HasSubstr("memory_pages total=512"));
    EXPECT_THAT(frame.cpu_text, HasSubstr("thread id=1"));
    EXPECT_THAT(frame.cpu_text, HasSubstr("rip=0x"));
    EXPECT_THAT(frame.cpu_text, HasSubstr("paging enabled="));
    EXPECT_THAT(frame.registers_text, HasSubstr("register drill-down"));
    EXPECT_THAT(frame.registers_text, HasSubstr("RAX=0x"));
    EXPECT_THAT(frame.paging_text, HasSubstr("address_space root=0x"));
    EXPECT_THAT(frame.paging_text, HasSubstr("rip linear=0x"));
    EXPECT_THAT(frame.paging_text, HasSubstr("PML4 table=0x"));
    EXPECT_THAT(frame.instruction_trace_text, HasSubstr("instruction trace empty"));
    EXPECT_THAT(frame.trace_text, HasSubstr("action=boot"));
    EXPECT_THAT(frame.trace_text, HasSubstr("skipped"));
    EXPECT_THAT(frame.summary_text, HasSubstr(frame.cursor_text));
    EXPECT_THAT(frame.summary_text, HasSubstr(frame.cpu_text));
    EXPECT_THAT(frame.summary_text, HasSubstr(frame.registers_text));
    EXPECT_THAT(frame.summary_text, HasSubstr(frame.paging_text));
}

TEST(HostDebuggerSessionTest, MoveOperationsPreserveDebuggerState)
{
    host::HostDebuggerSession source = make_titled_debugger_session();
    source.boot();
    static_cast<void>(source.submit_command_line("echo movable"));

    host::HostDebuggerSession moved{std::move(source)};
    const host::HostDebuggerFrame moved_frame = moved.frame();

    EXPECT_TRUE(moved_frame.booted);
    EXPECT_THAT(moved_frame.title, Eq(TEST_DEBUGGER_TITLE));
    EXPECT_THAT(moved_frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(moved_frame.display_text, HasSubstr("movable"));

    host::HostDebuggerSession assigned;
    assigned = std::move(moved);
    const host::HostDebuggerFrame assigned_frame = assigned.frame();

    EXPECT_TRUE(assigned_frame.booted);
    EXPECT_THAT(assigned_frame.title, Eq(TEST_DEBUGGER_TITLE));
    EXPECT_THAT(assigned_frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(assigned_frame.display_text, HasSubstr("movable"));
    EXPECT_THAT(assigned_frame.trace_text, HasSubstr("action=input_command"));
}

TEST(HostDebuggerSessionTest, SubmitCommandLineNormalizesNewlineAndUpdatesFrame)
{
    host::HostDebuggerSession session;

    session.boot();
    EXPECT_THAT(
        session.submit_command_line("echo debugger ready"),
        Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(frame.display_text, HasSubstr("mnos> echo debugger ready"));
    EXPECT_THAT(frame.display_text, HasSubstr("debugger ready"));
    EXPECT_THAT(frame.counters_text, HasSubstr("commands=1"));
    EXPECT_THAT(frame.trace_text, HasSubstr("action=input_command"));
    EXPECT_THAT(frame.trace_text, HasSubstr("preview=\"echo debugger ready\""));
}

TEST(HostDebuggerSessionTest, SubmitCommandLineAcceptsExistingCarriageReturn)
{
    host::HostDebuggerSession session;

    session.boot();
    EXPECT_THAT(
        session.submit_command_line("echo carriage\r"),
        Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(frame.display_text, HasSubstr("carriage"));
}

TEST(HostDebuggerSessionTest, SubmitInputEventSupportsRawGuiTextAndSpecialKeys)
{
    host::HostDebuggerSession session;

    session.boot();
    EXPECT_THAT(
        session.submit_input_event(host::HostInputEvent::text("echo raw gui")),
        Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(session.frame().snapshot.command_count, Eq(std::size_t{0}));
    EXPECT_THAT(
        session.submit_input_event(host::HostInputEvent::special_key(host::HostSpecialKey::ENTER)),
        Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(frame.display_text, HasSubstr("raw gui"));
    EXPECT_THAT(frame.trace_text, HasSubstr("kind=TEXT"));
    EXPECT_THAT(frame.trace_text, HasSubstr("kind=SPECIAL_KEY key=ENTER"));
}

TEST(HostDebuggerSessionTest, EmptyCommandLineStillSubmitsTerminalEnter)
{
    host::HostDebuggerSession session;

    session.boot();
    EXPECT_THAT(session.submit_command_line(""), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(count_substring(frame.display_text, TEST_PROMPT), Eq(std::size_t{2}));
}

TEST(HostDebuggerSessionTest, SpecialKeyInputSeparatesVisibleAndControlKeys)
{
    host::HostDebuggerSession session;

    session.boot();
    EXPECT_THAT(session.submit_text("echo key input"), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(session.submit_special_key(host::HostSpecialKey::ARROW_UP), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(session.frame().snapshot.command_count, Eq(std::size_t{0}));
    EXPECT_THAT(session.submit_special_key(host::HostSpecialKey::ENTER), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{1}));
    EXPECT_THAT(frame.display_text, HasSubstr("key input"));
}

TEST(HostDebuggerSessionTest, ResetRebootsFreshMachineFrame)
{
    host::HostDebuggerSession session;

    session.boot();
    static_cast<void>(session.submit_command_line("echo before reset"));
    session.record_instruction_trace(make_real_cpu_execution_trace());
    EXPECT_THAT(session.instruction_trace_entry_count(), Eq(std::size_t{2}));
    session.reset();
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.booted);
    EXPECT_TRUE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.command_count, Eq(std::size_t{0}));
    EXPECT_THAT(frame.instruction_trace_entry_count, Eq(std::size_t{0}));
    EXPECT_THAT(frame.display_text, HasSubstr(TEST_PROMPT));
    EXPECT_THAT(frame.display_text.find("before reset"), Eq(std::string::npos));
    EXPECT_THAT(frame.instruction_trace_text, HasSubstr("instruction trace empty"));
    EXPECT_THAT(frame.trace_text, HasSubstr("action=reset"));
}

TEST(HostDebuggerSessionTest, RunControlActionsRecordDeterministicTrace)
{
    host::HostDebuggerSession session;

    EXPECT_THAT(session.run_state(), Eq(host::HostDebuggerRunState::PAUSED));
    EXPECT_THAT(session.run_until_waiting(), Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(session.frame().trace_text, HasSubstr("action=run"));
    EXPECT_THAT(session.frame().trace_text, HasSubstr("skipped"));

    session.boot();
    session.clear_trace();
    EXPECT_THAT(session.trace_entry_count(), Eq(std::size_t{0}));
    session.pause();
    EXPECT_THAT(session.step_until_waiting(), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    EXPECT_THAT(session.run_until_waiting(), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_THAT(frame.run_state, Eq(host::HostDebuggerRunState::PAUSED));
    EXPECT_THAT(frame.trace_entry_count, Eq(std::size_t{3}));
    EXPECT_THAT(frame.trace_text, HasSubstr("action=pause"));
    EXPECT_THAT(frame.trace_text, HasSubstr("action=step"));
    EXPECT_THAT(frame.trace_text, HasSubstr("action=run"));
}

TEST(HostDebuggerSessionTest, TraceCapacityKeepsNewestEntries)
{
    host::HostDebuggerSessionConfig config;
    config.trace_capacity = std::size_t{3};
    host::HostDebuggerSession session{config};

    session.boot();
    static_cast<void>(session.submit_text("echo bounded"));
    static_cast<void>(session.submit_special_key(host::HostSpecialKey::ENTER));
    static_cast<void>(session.submit_command_line("mem"));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_THAT(frame.trace_entry_count, Eq(std::size_t{3}));
    EXPECT_THAT(frame.trace_text.find("#1 "), Eq(std::string::npos));
    EXPECT_THAT(frame.trace_text, HasSubstr("action=input_text"));
    EXPECT_THAT(frame.trace_text, HasSubstr("action=input_special"));
    EXPECT_THAT(frame.trace_text, HasSubstr("action=input_command"));
}

TEST(HostDebuggerSessionTest, ZeroTraceCapacityDisablesTraceStorage)
{
    host::HostDebuggerSessionConfig config;
    config.trace_capacity = std::size_t{0};
    host::HostDebuggerSession session{config};

    session.boot();
    static_cast<void>(session.submit_command_line("mem"));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_THAT(session.trace_entry_count(), Eq(std::size_t{0}));
    EXPECT_THAT(frame.trace_entry_count, Eq(std::size_t{0}));
    EXPECT_THAT(frame.trace_text, HasSubstr("trace empty"));
}

TEST(HostDebuggerSessionTest, RecordsRealCpuExecutionTraceForInstructionPanel)
{
    host::HostDebuggerSession session;
    const cpu::ExecutionTrace trace = make_real_cpu_execution_trace();

    session.record_instruction_trace(trace);
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_THAT(session.instruction_trace_entry_count(), Eq(trace.size()));
    EXPECT_THAT(frame.instruction_trace_entry_count, Eq(trace.size()));
    EXPECT_THAT(frame.instruction_trace_text, HasSubstr("cycle=1"));
    EXPECT_THAT(frame.instruction_trace_text, HasSubstr("opcode=MOV"));
    EXPECT_THAT(frame.instruction_trace_text, HasSubstr("opcode=HLT"));
    EXPECT_THAT(frame.instruction_trace_text, HasSubstr("halted=yes"));
    EXPECT_THAT(frame.summary_text, HasSubstr(frame.instruction_trace_text));
}

TEST(HostDebuggerSessionTest, RunsSampleUserProgramIntoInstructionTrace)
{
    host::HostDebuggerSession session;

    EXPECT_THAT(session.run_sample_user_program(), Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(session.frame().trace_text, HasSubstr("exec_user_sample"));
    EXPECT_THAT(session.frame().trace_text, HasSubstr("skipped"));

    session.boot();
    session.clear_trace();
    EXPECT_THAT(session.run_sample_user_program(), Eq(host::HostMachineSessionStatus::WAITING_FOR_INPUT));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_THAT(frame.run_state, Eq(host::HostDebuggerRunState::PAUSED));
    EXPECT_THAT(frame.instruction_trace_entry_count, Eq(std::size_t{3}));
    EXPECT_THAT(frame.instruction_trace_text, HasSubstr("opcode=MOV"));
    EXPECT_THAT(frame.instruction_trace_text, HasSubstr("opcode=SYSCALL"));
    EXPECT_THAT(frame.trace_text, HasSubstr("action=exec_user_sample"));
    EXPECT_THAT(frame.trace_text, HasSubstr("user_status=EXITED"));
    EXPECT_THAT(frame.trace_text, HasSubstr("wait_status=EXITED"));
    EXPECT_THAT(frame.trace_text, HasSubstr("exit=42"));
    EXPECT_THAT(frame.summary_text, HasSubstr(frame.instruction_trace_text));
}

TEST(HostDebuggerSessionTest, InstructionTraceCapacityKeepsNewestEntriesAndCanClear)
{
    host::HostDebuggerSessionConfig config;
    config.instruction_trace_capacity = std::size_t{1};
    host::HostDebuggerSession session{config};
    const cpu::ExecutionTrace trace = make_real_cpu_execution_trace();

    session.record_instruction_trace(trace.entries());
    const host::HostDebuggerFrame bounded_frame = session.frame();

    EXPECT_THAT(bounded_frame.instruction_trace_entry_count, Eq(std::size_t{1}));
    EXPECT_THAT(bounded_frame.instruction_trace_text.find("opcode=MOV"), Eq(std::string::npos));
    EXPECT_THAT(bounded_frame.instruction_trace_text, HasSubstr("opcode=HLT"));

    session.clear_instruction_trace();
    const host::HostDebuggerFrame cleared_frame = session.frame();

    EXPECT_THAT(cleared_frame.instruction_trace_entry_count, Eq(std::size_t{0}));
    EXPECT_THAT(cleared_frame.instruction_trace_text, HasSubstr("instruction trace empty"));
}

TEST(HostDebuggerSessionTest, ZeroInstructionTraceCapacityDisablesInstructionTraceStorage)
{
    host::HostDebuggerSessionConfig config;
    config.instruction_trace_capacity = std::size_t{0};
    host::HostDebuggerSession session{config};

    session.record_instruction_trace(make_real_cpu_execution_trace());
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_THAT(session.instruction_trace_entry_count(), Eq(std::size_t{0}));
    EXPECT_THAT(frame.instruction_trace_entry_count, Eq(std::size_t{0}));
    EXPECT_THAT(frame.instruction_trace_text, HasSubstr("instruction trace empty"));
}

TEST(HostDebuggerSessionTest, ExitCommandMakesFrameReadOnlyUntilReset)
{
    host::HostDebuggerSession session;

    session.boot();
    EXPECT_THAT(session.submit_command_line("exit"), Eq(host::HostMachineSessionStatus::EXITED));
    EXPECT_THAT(session.pump_until_waiting(), Eq(host::HostMachineSessionStatus::EXITED));
    EXPECT_THAT(
        session.submit_command_line("echo ignored"),
        Eq(host::HostMachineSessionStatus::EXITED));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.booted);
    EXPECT_FALSE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.status, Eq(host::HostMachineSessionStatus::EXITED));
    EXPECT_THAT(frame.status_text, HasSubstr("state=EXITED"));
    EXPECT_THAT(frame.trace_text, HasSubstr("action=input_command"));
}

TEST(HostDebuggerSessionTest, ShellIoErrorStatusAppearsInFrameSummary)
{
    host::HostDebuggerSession session;

    session.boot();
    ASSERT_TRUE(session.machine_session().kernel().close_fd(
        session.machine_session().shell_process(),
        io::FileDescriptor::stdout()));
    EXPECT_THAT(
        session.submit_command_line("echo closed"),
        Eq(host::HostMachineSessionStatus::SHELL_IO_ERROR));
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_TRUE(frame.booted);
    EXPECT_FALSE(frame.accepts_input);
    EXPECT_TRUE(frame.snapshot.has_shell_io_status);
    EXPECT_THAT(frame.snapshot.shell_io_status, Eq(io::IoStatus::BAD_DESCRIPTOR));
    EXPECT_THAT(frame.status_text, HasSubstr("state=SHELL_IO_ERROR"));
    EXPECT_THAT(frame.status_text, HasSubstr("shell_io_status="));
    EXPECT_THAT(frame.summary_text, HasSubstr(frame.status_text));
}

TEST(HostDebuggerSessionTest, FailedBootLeavesCreatedFrame)
{
    host::HostDebuggerSessionConfig config;
    config.machine.physical_memory_size_bytes = TEST_UNBOOTABLE_MEMORY_SIZE_BYTES;
    host::HostDebuggerSession session{config};

    EXPECT_THROW(session.boot(), std::runtime_error);
    EXPECT_FALSE(session.machine_session().booted());
    const host::HostDebuggerFrame frame = session.frame();

    EXPECT_FALSE(frame.booted);
    EXPECT_FALSE(frame.accepts_input);
    EXPECT_THAT(frame.snapshot.status, Eq(host::HostMachineSessionStatus::CREATED));
    EXPECT_THAT(frame.snapshot.physical_memory_size_bytes, Eq(TEST_UNBOOTABLE_MEMORY_SIZE_BYTES));
}
