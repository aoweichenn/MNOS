#pragma once

#include <cstddef>
#include <initializer_list>
#include <span>
#include <vector>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/common/types.hpp>

namespace mnos::cpu
{
class PhysicalMemory
{
public:
    using container_type = std::vector<Byte>;

    PhysicalMemory() = default;
    explicit PhysicalMemory(std::size_t size_bytes);
    explicit PhysicalMemory(container_type bytes) noexcept;
    PhysicalMemory(std::initializer_list<Byte> bytes);

    void resize(std::size_t size_bytes);
    void clear() noexcept;
    void fill(Byte value) noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::span<Byte> bytes() noexcept;
    [[nodiscard]] std::span<const Byte> bytes() const noexcept;
    [[nodiscard]] bool contains_range(Address64 address, std::size_t byte_count) const noexcept;

    [[nodiscard]] Byte read_byte(Address64 address) const;
    [[nodiscard]] Word read_word(Address64 address) const;
    [[nodiscard]] Dword read_dword(Address64 address) const;
    [[nodiscard]] Qword read_qword(Address64 address) const;
    [[nodiscard]] Qword read(Address64 address, DataSize size) const;

    void write_byte(Address64 address, Byte value);
    void write_word(Address64 address, Word value);
    void write_dword(Address64 address, Dword value);
    void write_qword(Address64 address, Qword value);
    void write(Address64 address, DataSize size, Qword value);

private:
    [[nodiscard]] Qword read_little_endian_value(Address64 address, std::size_t byte_count) const;
    void write_little_endian_value(Address64 address, std::size_t byte_count, Qword value);
    [[nodiscard]] std::size_t checked_start_index(Address64 address, std::size_t byte_count) const;

    std::vector<Byte> bytes_;
};
}
