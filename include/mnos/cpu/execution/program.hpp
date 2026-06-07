#pragma once

#include <cstddef>
#include <initializer_list>
#include <span>
#include <vector>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/instruction/instruction.hpp>

namespace mnos::cpu
{
class Program
{
public:
    using container_type = std::vector<Instruction>;
    using const_iterator = container_type::const_iterator;

    Program() = default;
    explicit Program(container_type instructions) noexcept;
    Program(std::initializer_list<Instruction> instructions);

    void reserve(std::size_t instruction_count);
    void clear() noexcept;
    void push_back(Instruction instruction);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::span<const Instruction> instructions() const noexcept;
    [[nodiscard]] bool contains_rip(InstructionPointer rip) const noexcept;

    [[nodiscard]] const Instruction& at(std::size_t index) const;
    [[nodiscard]] const Instruction& instruction_at(InstructionPointer rip) const;

    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;

private:
    std::vector<Instruction> instructions_;
};
}
