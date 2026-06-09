#include <array>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <mnos/core/enum_map.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/proc/process_context.hpp>

namespace
{
constexpr std::string_view PROCESS_STATE_INVALID_NAME = "<invalid>";
constexpr const char* PROCESS_INVALID_ID_MESSAGE = "process requires a valid process id";
constexpr const char* PROCESS_THREAD_INDEX_OUT_OF_RANGE_MESSAGE = "process thread index is out of range";
constexpr const char* PROCESS_DUPLICATE_THREAD_ID_MESSAGE = "process cannot create duplicate thread ids";
constexpr const char* PROCESS_CREATE_THREAD_NOT_RUNNING_MESSAGE =
    "process cannot create a thread unless it is running";
constexpr const char* PROCESS_EXIT_NOT_RUNNING_MESSAGE = "process cannot exit unless it is running";
constexpr const char* PROCESS_REAP_RUNNING_MESSAGE = "process cannot reap a running process";
constexpr const char* PROCESS_EXIT_REAPED_MESSAGE = "process cannot exit after it was reaped";
constexpr const char* PROCESS_CONTEXT_PCID_MESSAGE = "process id cannot be represented as an x86-64 pcid";

class ProcessStateCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::os::proc::ProcessState state) noexcept
    {
        return PROCESS_STATE_NAMES.contains(state);
    }

    [[nodiscard]] static std::size_t index(const mnos::os::proc::ProcessState state) noexcept
    {
        return PROCESS_STATE_NAMES.index(state);
    }

    [[nodiscard]] static std::string_view name(const mnos::os::proc::ProcessState state) noexcept
    {
        return PROCESS_STATE_NAMES.name(state);
    }

private:
    inline static constexpr auto PROCESS_STATE_NAMES =
        mnos::core::make_enum_name_table<mnos::os::proc::ProcessState>(
            std::array<std::string_view, mnos::os::proc::PROCESS_STATE_COUNT>{
                "RUNNING",
                "EXITED",
                "REAPED"},
            PROCESS_STATE_INVALID_NAME);
};
}

namespace mnos::os::proc
{
bool is_process_state_valid(const ProcessState state) noexcept
{
    return ProcessStateCatalog::contains(state);
}

std::size_t process_state_to_index(const ProcessState state) noexcept
{
    return ProcessStateCatalog::index(state);
}

std::string_view process_state_to_name(const ProcessState state) noexcept
{
    return ProcessStateCatalog::name(state);
}

cpu::memory::ProcessContextId process_context_id_for(const ProcessId process_id)
{
    if (process_id.value() > cpu::memory::PROCESS_CONTEXT_ID_MAX_VALUE)
    {
        throw std::out_of_range{PROCESS_CONTEXT_PCID_MESSAGE};
    }
    return cpu::memory::ProcessContextId{static_cast<cpu::memory::ProcessContextId::value_type>(process_id.value())};
}

Process::Process(const ProcessId id, mm::AddressSpace address_space) :
    Process(id, std::move(address_space), ProcessId::invalid())
{
}

Process::Process(const ProcessId id, mm::AddressSpace address_space, const ProcessId parent_id) :
    id_(id),
    parent_id_(parent_id),
    address_space_(std::move(address_space))
{
    if (!this->id_.is_valid())
    {
        throw std::invalid_argument{PROCESS_INVALID_ID_MESSAGE};
    }
}

ProcessId Process::id() const noexcept
{
    return this->id_;
}

ProcessId Process::parent_id() const noexcept
{
    return this->parent_id_;
}

bool Process::has_parent() const noexcept
{
    return this->parent_id_.is_valid();
}

ProcessState Process::state() const noexcept
{
    return this->state_;
}

bool Process::is_running() const noexcept
{
    return this->state_ == ProcessState::RUNNING;
}

bool Process::is_exited() const noexcept
{
    return this->state_ == ProcessState::EXITED;
}

bool Process::is_reaped() const noexcept
{
    return this->state_ == ProcessState::REAPED;
}

std::int64_t Process::exit_code() const noexcept
{
    return this->exit_code_;
}

mm::AddressSpace& Process::address_space() noexcept
{
    return this->address_space_;
}

const mm::AddressSpace& Process::address_space() const noexcept
{
    return this->address_space_;
}

io::FileDescriptorTable& Process::file_descriptors() noexcept
{
    return this->file_descriptors_;
}

const io::FileDescriptorTable& Process::file_descriptors() const noexcept
{
    return this->file_descriptors_;
}

sched::ThreadContext& Process::create_thread(
    const sched::ThreadId thread_id,
    const mm::VirtualAddress kernel_stack_bottom,
    const std::uint64_t kernel_stack_size_bytes)
{
    if (!this->is_running())
    {
        throw std::logic_error{PROCESS_CREATE_THREAD_NOT_RUNNING_MESSAGE};
    }
    if (this->contains_thread_id(thread_id))
    {
        throw std::logic_error{PROCESS_DUPLICATE_THREAD_ID_MESSAGE};
    }

    this->threads_.emplace_back(thread_id, kernel_stack_bottom, kernel_stack_size_bytes);
    return this->threads_.back();
}

std::size_t Process::thread_count() const noexcept
{
    return this->threads_.size();
}

bool Process::empty() const noexcept
{
    return this->threads_.empty();
}

sched::ThreadContext& Process::thread_at(const std::size_t index)
{
    if (index >= this->threads_.size())
    {
        throw std::out_of_range{PROCESS_THREAD_INDEX_OUT_OF_RANGE_MESSAGE};
    }
    return this->threads_[index];
}

const sched::ThreadContext& Process::thread_at(const std::size_t index) const
{
    if (index >= this->threads_.size())
    {
        throw std::out_of_range{PROCESS_THREAD_INDEX_OUT_OF_RANGE_MESSAGE};
    }
    return this->threads_[index];
}

void Process::mark_exited(const std::int64_t exit_code)
{
    if (this->is_reaped())
    {
        throw std::logic_error{PROCESS_EXIT_REAPED_MESSAGE};
    }
    if (!this->is_running())
    {
        throw std::logic_error{PROCESS_EXIT_NOT_RUNNING_MESSAGE};
    }

    this->state_ = ProcessState::EXITED;
    this->exit_code_ = exit_code;
}

void Process::mark_reaped()
{
    if (!this->is_exited())
    {
        throw std::logic_error{PROCESS_REAP_RUNNING_MESSAGE};
    }

    this->state_ = ProcessState::REAPED;
}

bool Process::contains_thread_id(const sched::ThreadId thread_id) const noexcept
{
    for (const sched::ThreadContext& thread : this->threads_)
    {
        if (thread.id() == thread_id)
        {
            return true;
        }
    }
    return false;
}
}
