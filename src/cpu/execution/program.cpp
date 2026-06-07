#include <stdexcept>
#include <utility>

#include <mnos/cpu/execution/program.hpp>

namespace
{
constexpr const char* PROGRAM_INVALID_RIP_MESSAGE = "program rip is out of range";
}

namespace mnos::cpu
{
Program::Program(container_type instructions) noexcept : instructions_(std::move(instructions))
{
}

Program::Program(std::initializer_list<Instruction> instructions) : instructions_(instructions)
{
}

void Program::reserve(const std::size_t instruction_count)
{
    this->instructions_.reserve(instruction_count);
}

void Program::clear() noexcept
{
    this->instructions_.clear();
}

void Program::push_back(Instruction instruction)
{
    this->instructions_.push_back(std::move(instruction));
}

bool Program::empty() const noexcept
{
    return this->instructions_.empty();
}

std::size_t Program::size() const noexcept
{
    return this->instructions_.size();
}

std::span<const Instruction> Program::instructions() const noexcept
{
    return std::span<const Instruction>{this->instructions_};
}

bool Program::contains_rip(const RIP64 rip) const noexcept
{
    return rip < static_cast<RIP64>(this->instructions_.size());
}

const Instruction& Program::at(const std::size_t index) const
{
    return this->instructions_.at(index);
}

const Instruction& Program::instruction_at(const RIP64 rip) const
{
    if (!this->contains_rip(rip))
    {
        throw std::out_of_range{PROGRAM_INVALID_RIP_MESSAGE};
    }
    return this->instructions_[static_cast<std::size_t>(rip)];
}

Program::const_iterator Program::begin() const noexcept
{
    return this->instructions_.begin();
}

Program::const_iterator Program::end() const noexcept
{
    return this->instructions_.end();
}
}
