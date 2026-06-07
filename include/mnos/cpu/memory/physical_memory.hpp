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
    using container_type = std::vector<UBYTE8>;

    PhysicalMemory() = default;
    explicit PhysicalMemory(std::size_t size_bytes);
    explicit PhysicalMemory(container_type bytes) noexcept;
    PhysicalMemory(std::initializer_list<UBYTE8> bytes);

    void resize(std::size_t size_bytes);
    void clear() noexcept;
    void fill(UBYTE8 value) noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::span<UBYTE8> bytes() noexcept;
    [[nodiscard]] std::span<const UBYTE8> bytes() const noexcept;
    [[nodiscard]] bool contains_range(ADDRESS64 address, std::size_t byte_count) const noexcept;

    [[nodiscard]] UBYTE8 read_byte(ADDRESS64 address) const;
    [[nodiscard]] UWORD16 read_word(ADDRESS64 address) const;
    [[nodiscard]] UDWORD32 read_dword(ADDRESS64 address) const;
    [[nodiscard]] UQWORD64 read_qword(ADDRESS64 address) const;
    [[nodiscard]] UQWORD64 read(ADDRESS64 address, DataSize size) const;

    void write_byte(ADDRESS64 address, UBYTE8 value);
    void write_word(ADDRESS64 address, UWORD16 value);
    void write_dword(ADDRESS64 address, UDWORD32 value);
    void write_qword(ADDRESS64 address, UQWORD64 value);
    void write(ADDRESS64 address, DataSize size, UQWORD64 value);

private:
    [[nodiscard]] UQWORD64 read_little_endian(ADDRESS64 address, std::size_t byte_count) const;
    void write_little_endian(ADDRESS64 address, std::size_t byte_count, UQWORD64 value);
    [[nodiscard]] std::size_t checked_index(ADDRESS64 address, std::size_t byte_count) const;

    std::vector<UBYTE8> bytes_;
};
}
