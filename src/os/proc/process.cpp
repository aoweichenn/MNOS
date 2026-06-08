#include <stdexcept>
#include <utility>

#include <mnos/os/proc/process.hpp>
#include <mnos/os/proc/process_context.hpp>

namespace
{
constexpr const char* PROCESS_INVALID_ID_MESSAGE = "process requires a valid process id";
constexpr const char* PROCESS_THREAD_INDEX_OUT_OF_RANGE_MESSAGE = "process thread index is out of range";
constexpr const char* PROCESS_DUPLICATE_THREAD_ID_MESSAGE = "process cannot create duplicate thread ids";
constexpr const char* PROCESS_CONTEXT_PCID_MESSAGE = "process id cannot be represented as an x86-64 pcid";
}

namespace mnos::os::proc
{
cpu::memory::ProcessContextId process_context_id_for(const ProcessId process_id)
{
    if (process_id.value() > cpu::memory::PROCESS_CONTEXT_ID_MAX_VALUE)
    {
        throw std::out_of_range{PROCESS_CONTEXT_PCID_MESSAGE};
    }
    return cpu::memory::ProcessContextId{static_cast<cpu::memory::ProcessContextId::value_type>(process_id.value())};
}

Process::Process(const ProcessId id, mm::AddressSpace address_space) :
    id_(id),
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
