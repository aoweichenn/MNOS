#pragma once

#include <cstddef>
#include <initializer_list>
#include <span>
#include <vector>

#include <mnos/cpu/common/types.hpp>

namespace mnos::cpu
{
class ExecutableImage
{
public:
    using container_type = std::vector<Byte>;

    ExecutableImage() = default;
    explicit ExecutableImage(container_type bytes) noexcept;
    ExecutableImage(container_type bytes, InstructionPointer base_rip) noexcept;
    ExecutableImage(std::initializer_list<Byte> bytes);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] InstructionPointer base_rip() const noexcept;
    [[nodiscard]] InstructionPointer end_rip() const noexcept;
    [[nodiscard]] bool contains_rip(InstructionPointer rip) const noexcept;
    [[nodiscard]] std::size_t offset_of(InstructionPointer rip) const;
    [[nodiscard]] Byte byte_at(InstructionPointer rip) const;
    [[nodiscard]] std::span<const Byte> bytes() const noexcept;

private:
    container_type bytes_;
    InstructionPointer base_rip_ = InstructionPointer{0};
};
}
