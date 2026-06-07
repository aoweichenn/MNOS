#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/instruction/opcode.hpp>

namespace mnos::cpu
{
struct ExecutionTraceEntry
{
    CycleCount cycle_count;
    InstructionPointer rip_before;
    InstructionPointer rip_after;
    Opcode opcode;
    bool halted_after;
    bool trap_pending_after = false;
};

class ExecutionTrace
{
public:
    using container_type = std::vector<ExecutionTraceEntry>;
    using const_iterator = container_type::const_iterator;

    void reserve(std::size_t entry_count);
    void clear() noexcept;
    void push_back(ExecutionTraceEntry entry);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::span<const ExecutionTraceEntry> entries() const noexcept;
    [[nodiscard]] const ExecutionTraceEntry& at(std::size_t index) const;

    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;

private:
    std::vector<ExecutionTraceEntry> entries_;
};
}
