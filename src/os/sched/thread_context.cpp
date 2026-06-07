#include <limits>
#include <stdexcept>

#include <mnos/cpu/register/id.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace
{
constexpr const char* THREAD_CONTEXT_INVALID_ID_MESSAGE = "thread context requires a valid thread id";
constexpr const char* THREAD_CONTEXT_UNALIGNED_STACK_BOTTOM_MESSAGE = "thread kernel stack bottom must be page aligned";
constexpr const char* THREAD_CONTEXT_INVALID_STACK_SIZE_MESSAGE =
    "thread kernel stack size must be a non-zero page multiple";
constexpr const char* THREAD_CONTEXT_STACK_RANGE_OVERFLOW_MESSAGE = "thread kernel stack range overflows address value";
constexpr const char* THREAD_CONTEXT_INVALID_STATE_MESSAGE = "thread state is invalid";
constexpr const char* THREAD_CONTEXT_TRAP_FRAME_NOT_PRESENT_MESSAGE = "thread context has no last trap frame";

[[nodiscard]] bool is_valid_stack_size(const std::uint64_t stack_size_bytes) noexcept
{
    return stack_size_bytes != std::uint64_t{0} && mnos::os::mm::is_page_aligned(stack_size_bytes);
}

[[nodiscard]] mnos::os::mm::VirtualAddress checked_stack_top(
    const mnos::os::mm::VirtualAddress stack_bottom,
    const std::uint64_t stack_size_bytes)
{
    if (!mnos::os::mm::is_page_aligned(stack_bottom))
    {
        throw std::invalid_argument{THREAD_CONTEXT_UNALIGNED_STACK_BOTTOM_MESSAGE};
    }

    if (!is_valid_stack_size(stack_size_bytes))
    {
        throw std::invalid_argument{THREAD_CONTEXT_INVALID_STACK_SIZE_MESSAGE};
    }

    if (stack_bottom.value() > std::numeric_limits<mnos::os::mm::AddressValue>::max() - stack_size_bytes)
    {
        throw std::overflow_error{THREAD_CONTEXT_STACK_RANGE_OVERFLOW_MESSAGE};
    }

    return mnos::os::mm::VirtualAddress{stack_bottom.value() + stack_size_bytes};
}
}

namespace mnos::os::sched
{
ThreadContext::ThreadContext(const ThreadId id, const mm::VirtualAddress kernel_stack_bottom) :
    ThreadContext(id, kernel_stack_bottom, THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES)
{
}

ThreadContext::ThreadContext(
    const ThreadId id,
    const mm::VirtualAddress kernel_stack_bottom,
    const std::uint64_t kernel_stack_size_bytes) :
    id_(id),
    kernel_stack_bottom_(kernel_stack_bottom),
    kernel_stack_top_(checked_stack_top(kernel_stack_bottom, kernel_stack_size_bytes)),
    kernel_stack_size_bytes_(kernel_stack_size_bytes)
{
    if (!this->id_.is_valid())
    {
        throw std::invalid_argument{THREAD_CONTEXT_INVALID_ID_MESSAGE};
    }

    this->initialize_cpu_stack_pointer();
}

ThreadId ThreadContext::id() const noexcept
{
    return this->id_;
}

ThreadState ThreadContext::state() const noexcept
{
    return this->state_;
}

void ThreadContext::set_state(const ThreadState state)
{
    if (!is_thread_state_valid(state))
    {
        throw std::invalid_argument{THREAD_CONTEXT_INVALID_STATE_MESSAGE};
    }

    this->state_ = state;
}

bool ThreadContext::is_runnable() const noexcept
{
    return this->state_ == ThreadState::READY;
}

bool ThreadContext::is_alive() const noexcept
{
    return this->state_ != ThreadState::DEAD;
}

cpu::CpuState& ThreadContext::cpu_state() noexcept
{
    return this->cpu_state_;
}

const cpu::CpuState& ThreadContext::cpu_state() const noexcept
{
    return this->cpu_state_;
}

mm::VirtualAddress ThreadContext::kernel_stack_bottom() const noexcept
{
    return this->kernel_stack_bottom_;
}

mm::VirtualAddress ThreadContext::kernel_stack_top() const noexcept
{
    return this->kernel_stack_top_;
}

std::uint64_t ThreadContext::kernel_stack_size_bytes() const noexcept
{
    return this->kernel_stack_size_bytes_;
}

bool ThreadContext::contains_kernel_stack_address(const mm::VirtualAddress address) const noexcept
{
    return this->kernel_stack_bottom_ <= address && address < this->kernel_stack_top_;
}

bool ThreadContext::has_last_trap_frame() const noexcept
{
    return this->last_trap_frame_.has_value();
}

const cpu::system::TrapFrame& ThreadContext::last_trap_frame() const
{
    if (!this->last_trap_frame_.has_value())
    {
        throw std::logic_error{THREAD_CONTEXT_TRAP_FRAME_NOT_PRESENT_MESSAGE};
    }
    return this->last_trap_frame_.value();
}

bool ThreadContext::snapshot_pending_trap_frame()
{
    if (!this->cpu_state_.has_pending_trap())
    {
        return false;
    }

    this->last_trap_frame_ = this->cpu_state_.pending_trap();
    return true;
}

void ThreadContext::clear_last_trap_frame() noexcept
{
    this->last_trap_frame_.reset();
}

void ThreadContext::reset_cpu_state()
{
    this->cpu_state_.reset();
    this->last_trap_frame_.reset();
    this->initialize_cpu_stack_pointer();
}

void ThreadContext::initialize_cpu_stack_pointer()
{
    this->cpu_state_.registers().write(cpu::RegisterId::RSP, static_cast<cpu::Qword>(this->kernel_stack_top_.value()));
}
}
