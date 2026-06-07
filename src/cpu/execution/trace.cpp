#include <utility>

#include <mnos/cpu/execution/trace.hpp>

namespace mnos::cpu
{
void ExecutionTrace::reserve(const std::size_t entry_count)
{
    this->entries_.reserve(entry_count);
}

void ExecutionTrace::clear() noexcept
{
    this->entries_.clear();
}

void ExecutionTrace::push_back(ExecutionTraceEntry entry)
{
    this->entries_.push_back(std::move(entry));
}

bool ExecutionTrace::empty() const noexcept
{
    return this->entries_.empty();
}

std::size_t ExecutionTrace::size() const noexcept
{
    return this->entries_.size();
}

std::span<const ExecutionTraceEntry> ExecutionTrace::entries() const noexcept
{
    return std::span<const ExecutionTraceEntry>{this->entries_};
}

const ExecutionTraceEntry& ExecutionTrace::at(const std::size_t index) const
{
    return this->entries_.at(index);
}

ExecutionTrace::const_iterator ExecutionTrace::begin() const noexcept
{
    return this->entries_.begin();
}

ExecutionTrace::const_iterator ExecutionTrace::end() const noexcept
{
    return this->entries_.end();
}
}
