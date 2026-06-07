#include <algorithm>
#include <stdexcept>
#include <utility>

#include <mnos/cpu/memory/physical_memory.hpp>

namespace
{
constexpr const char* PHYSICAL_MEMORY_ADDRESS_OUT_OF_RANGE_MESSAGE = "physical memory address range is out of bounds";
constexpr mnos::cpu::Qword PHYSICAL_MEMORY_BYTE_MASK = mnos::cpu::Qword{0xFF};
}

namespace mnos::cpu
{
PhysicalMemory::PhysicalMemory(const std::size_t size_bytes) : bytes_(size_bytes, Byte{0})
{
}

PhysicalMemory::PhysicalMemory(container_type bytes) noexcept : bytes_(std::move(bytes))
{
}

PhysicalMemory::PhysicalMemory(std::initializer_list<Byte> bytes) : bytes_(bytes)
{
}

void PhysicalMemory::resize(const std::size_t size_bytes)
{
    this->bytes_.resize(size_bytes, Byte{0});
}

void PhysicalMemory::clear() noexcept
{
    this->bytes_.clear();
}

void PhysicalMemory::fill(const Byte value) noexcept
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

std::span<Byte> PhysicalMemory::bytes() noexcept
{
    return std::span<Byte>{this->bytes_};
}

std::span<const Byte> PhysicalMemory::bytes() const noexcept
{
    return std::span<const Byte>{this->bytes_};
}

bool PhysicalMemory::contains_range(const Address64 address, const std::size_t byte_count) const noexcept
{
    const std::size_t start_index = static_cast<std::size_t>(address);
    if (start_index > this->bytes_.size())
    {
        return false;
    }

    return byte_count <= this->bytes_.size() - start_index;
}

Byte PhysicalMemory::read_byte(const Address64 address) const
{
    return static_cast<Byte>(this->read(address, DataSize::BYTE));
}

Word PhysicalMemory::read_word(const Address64 address) const
{
    return static_cast<Word>(this->read(address, DataSize::WORD));
}

Dword PhysicalMemory::read_dword(const Address64 address) const
{
    return static_cast<Dword>(this->read(address, DataSize::DWORD));
}

Qword PhysicalMemory::read_qword(const Address64 address) const
{
    return this->read(address, DataSize::QWORD);
}

Qword PhysicalMemory::read(const Address64 address, const DataSize size) const
{
    return this->read_little_endian_value(address, data_size_to_bytes(size));
}

void PhysicalMemory::write_byte(const Address64 address, const Byte value)
{
    this->write(address, DataSize::BYTE, value);
}

void PhysicalMemory::write_word(const Address64 address, const Word value)
{
    this->write(address, DataSize::WORD, value);
}

void PhysicalMemory::write_dword(const Address64 address, const Dword value)
{
    this->write(address, DataSize::DWORD, value);
}

void PhysicalMemory::write_qword(const Address64 address, const Qword value)
{
    this->write(address, DataSize::QWORD, value);
}

void PhysicalMemory::write(const Address64 address, const DataSize size, const Qword value)
{
    this->write_little_endian_value(address, data_size_to_bytes(size), value);
}

Qword PhysicalMemory::read_little_endian_value(const Address64 address, const std::size_t byte_count) const
{
    const std::size_t start_index = this->checked_start_index(address, byte_count);
    Qword value = Qword{0};

    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
    {
        const std::size_t bit_shift = byte_index * DATA_SIZE_BYTE_BITS;
        value |= static_cast<Qword>(this->bytes_[start_index + byte_index]) << bit_shift;
    }

    return value;
}

void PhysicalMemory::write_little_endian_value(
    const Address64 address,
    const std::size_t byte_count,
    const Qword value)
{
    const std::size_t start_index = this->checked_start_index(address, byte_count);

    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
    {
        const std::size_t bit_shift = byte_index * DATA_SIZE_BYTE_BITS;
        this->bytes_[start_index + byte_index] = static_cast<Byte>((value >> bit_shift) & PHYSICAL_MEMORY_BYTE_MASK);
    }
}

std::size_t PhysicalMemory::checked_start_index(const Address64 address, const std::size_t byte_count) const
{
    if (!this->contains_range(address, byte_count))
    {
        throw std::out_of_range{PHYSICAL_MEMORY_ADDRESS_OUT_OF_RANGE_MESSAGE};
    }

    return static_cast<std::size_t>(address);
}
}
