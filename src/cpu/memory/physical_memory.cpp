#include <algorithm>
#include <stdexcept>
#include <utility>

#include <mnos/cpu/memory/physical_memory.hpp>

namespace
{
constexpr const char* PHYSICAL_MEMORY_ADDRESS_OUT_OF_RANGE_MESSAGE = "physical memory address range is out of bounds";
constexpr mnos::cpu::UQWORD64 PHYSICAL_MEMORY_BYTE_MASK = mnos::cpu::UQWORD64{0xFF};
}

namespace mnos::cpu
{
PhysicalMemory::PhysicalMemory(const std::size_t size_bytes) : bytes_(size_bytes, UBYTE8{0})
{
}

PhysicalMemory::PhysicalMemory(container_type bytes) noexcept : bytes_(std::move(bytes))
{
}

PhysicalMemory::PhysicalMemory(std::initializer_list<UBYTE8> bytes) : bytes_(bytes)
{
}

void PhysicalMemory::resize(const std::size_t size_bytes)
{
    this->bytes_.resize(size_bytes, UBYTE8{0});
}

void PhysicalMemory::clear() noexcept
{
    this->bytes_.clear();
}

void PhysicalMemory::fill(const UBYTE8 value) noexcept
{
    std::fill(this->bytes_.begin(), this->bytes_.end(), value);
}

bool PhysicalMemory::empty() const noexcept
{
    return this->bytes_.empty();
}

std::size_t PhysicalMemory::size() const noexcept
{
    return this->bytes_.size();
}

std::span<UBYTE8> PhysicalMemory::bytes() noexcept
{
    return std::span<UBYTE8>{this->bytes_};
}

std::span<const UBYTE8> PhysicalMemory::bytes() const noexcept
{
    return std::span<const UBYTE8>{this->bytes_};
}

bool PhysicalMemory::contains_range(const ADDRESS64 address, const std::size_t byte_count) const noexcept
{
    const std::size_t start_index = static_cast<std::size_t>(address);
    if (start_index > this->bytes_.size())
    {
        return false;
    }

    return byte_count <= this->bytes_.size() - start_index;
}

UBYTE8 PhysicalMemory::read_byte(const ADDRESS64 address) const
{
    return static_cast<UBYTE8>(this->read(address, DataSize::BYTE));
}

UWORD16 PhysicalMemory::read_word(const ADDRESS64 address) const
{
    return static_cast<UWORD16>(this->read(address, DataSize::WORD));
}

UDWORD32 PhysicalMemory::read_dword(const ADDRESS64 address) const
{
    return static_cast<UDWORD32>(this->read(address, DataSize::DWORD));
}

UQWORD64 PhysicalMemory::read_qword(const ADDRESS64 address) const
{
    return this->read(address, DataSize::QWORD);
}

UQWORD64 PhysicalMemory::read(const ADDRESS64 address, const DataSize size) const
{
    return this->read_little_endian(address, data_size_to_bytes(size));
}

void PhysicalMemory::write_byte(const ADDRESS64 address, const UBYTE8 value)
{
    this->write(address, DataSize::BYTE, value);
}

void PhysicalMemory::write_word(const ADDRESS64 address, const UWORD16 value)
{
    this->write(address, DataSize::WORD, value);
}

void PhysicalMemory::write_dword(const ADDRESS64 address, const UDWORD32 value)
{
    this->write(address, DataSize::DWORD, value);
}

void PhysicalMemory::write_qword(const ADDRESS64 address, const UQWORD64 value)
{
    this->write(address, DataSize::QWORD, value);
}

void PhysicalMemory::write(const ADDRESS64 address, const DataSize size, const UQWORD64 value)
{
    this->write_little_endian(address, data_size_to_bytes(size), value);
}

UQWORD64 PhysicalMemory::read_little_endian(const ADDRESS64 address, const std::size_t byte_count) const
{
    const std::size_t start_index = this->checked_index(address, byte_count);
    UQWORD64 value = UQWORD64{0};

    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
    {
        const std::size_t bit_shift = byte_index * DATA_SIZE_BYTE_BITS;
        value |= static_cast<UQWORD64>(this->bytes_[start_index + byte_index]) << bit_shift;
    }

    return value;
}

void PhysicalMemory::write_little_endian(
    const ADDRESS64 address,
    const std::size_t byte_count,
    const UQWORD64 value)
{
    const std::size_t start_index = this->checked_index(address, byte_count);

    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
    {
        const std::size_t bit_shift = byte_index * DATA_SIZE_BYTE_BITS;
        this->bytes_[start_index + byte_index] = static_cast<UBYTE8>((value >> bit_shift) & PHYSICAL_MEMORY_BYTE_MASK);
    }
}

std::size_t PhysicalMemory::checked_index(const ADDRESS64 address, const std::size_t byte_count) const
{
    if (!this->contains_range(address, byte_count))
    {
        throw std::out_of_range{PHYSICAL_MEMORY_ADDRESS_OUT_OF_RANGE_MESSAGE};
    }

    return static_cast<std::size_t>(address);
}
}
