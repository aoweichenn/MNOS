#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include <mnos/cpu/memory/memory_model.hpp>

namespace
{
constexpr const char* MEMORY_MODEL_UNKNOWN_CORE_MESSAGE = "memory model core id is not in topology";
constexpr const char* MEMORY_MODEL_INVALID_DATA_SIZE_MESSAGE = "memory model data size is invalid";
constexpr mnos::cpu::Qword MEMORY_MODEL_BYTE_MASK = mnos::cpu::Qword{0xFF};

void require_valid_data_size(const mnos::cpu::DataSize size)
{
    if (!mnos::cpu::is_data_size_valid(size))
    {
        throw std::out_of_range{MEMORY_MODEL_INVALID_DATA_SIZE_MESSAGE};
    }
}

[[nodiscard]] mnos::cpu::Qword byte_mask_for_offset(const std::size_t byte_offset) noexcept
{
    return MEMORY_MODEL_BYTE_MASK << (byte_offset * mnos::cpu::DATA_SIZE_BYTE_BITS);
}
}

namespace mnos::cpu::memory
{
X86TsoMemoryModel::X86TsoMemoryModel(system::CoreTopology topology) :
    topology_(std::move(topology)), store_buffers_(this->topology_.core_count())
{
}

const system::CoreTopology& X86TsoMemoryModel::topology() const noexcept
{
    return this->topology_;
}

std::size_t X86TsoMemoryModel::pending_store_count() const noexcept
{
    std::size_t total_count = 0;
    for (const StoreQueue& store_queue : this->store_buffers_)
    {
        total_count += store_queue.size();
    }
    return total_count;
}

std::size_t X86TsoMemoryModel::pending_store_count(const system::CoreId core_id) const
{
    return this->store_buffers_.at(this->core_index(core_id)).size();
}

bool X86TsoMemoryModel::has_pending_stores(const system::CoreId core_id) const
{
    return !this->store_buffers_.at(this->core_index(core_id)).empty();
}

void X86TsoMemoryModel::store(
    const system::CoreId core_id,
    const Address64 address,
    const DataSize size,
    const Qword value)
{
    require_valid_data_size(size);
    StoreQueue& store_queue = this->store_buffers_.at(this->core_index(core_id));
    store_queue.push_back(StoreBufferEntry{core_id, address, size, value, this->next_sequence_});
    ++this->next_sequence_;
}

Qword X86TsoMemoryModel::load(
    const system::CoreId core_id,
    const MemoryBus& memory_bus,
    const Address64 address,
    const DataSize size) const
{
    require_valid_data_size(size);
    static_cast<void>(this->core_index(core_id));

    Qword value = memory_bus.read(address, size);
    const std::size_t byte_count = data_size_to_bytes(size);
    for (std::size_t byte_offset = 0; byte_offset < byte_count; ++byte_offset)
    {
        const Address64 byte_address = address + static_cast<Address64>(byte_offset);
        const std::optional<Byte> forwarded = this->forwarded_byte(core_id, byte_address);
        if (forwarded.has_value())
        {
            const Qword byte_mask = byte_mask_for_offset(byte_offset);
            value &= ~byte_mask;
            value |= static_cast<Qword>(forwarded.value()) << (byte_offset * DATA_SIZE_BYTE_BITS);
        }
    }

    return value;
}

bool X86TsoMemoryModel::drain_one(MemoryBus& memory_bus)
{
    std::optional<std::size_t> selected_core_index;
    Qword selected_sequence = std::numeric_limits<Qword>::max();
    for (std::size_t core_index_value = 0; core_index_value < this->store_buffers_.size(); ++core_index_value)
    {
        const StoreQueue& store_queue = this->store_buffers_[core_index_value];
        if (!store_queue.empty() && store_queue.front().sequence < selected_sequence)
        {
            selected_sequence = store_queue.front().sequence;
            selected_core_index = core_index_value;
        }
    }

    if (!selected_core_index.has_value())
    {
        return false;
    }

    StoreQueue& store_queue = this->store_buffers_[selected_core_index.value()];
    X86TsoMemoryModel::drain_entry(memory_bus, store_queue.front());
    store_queue.pop_front();
    return true;
}

bool X86TsoMemoryModel::drain_core(MemoryBus& memory_bus, const system::CoreId core_id)
{
    StoreQueue& store_queue = this->store_buffers_.at(this->core_index(core_id));
    if (store_queue.empty())
    {
        return false;
    }

    X86TsoMemoryModel::drain_entry(memory_bus, store_queue.front());
    store_queue.pop_front();
    return true;
}

void X86TsoMemoryModel::drain_all(MemoryBus& memory_bus)
{
    while (this->drain_one(memory_bus))
    {
    }
}

void X86TsoMemoryModel::fence(MemoryBus& memory_bus, const system::CoreId core_id)
{
    while (this->drain_core(memory_bus, core_id))
    {
    }
}

AtomicCompareExchangeResult X86TsoMemoryModel::compare_exchange(
    MemoryBus& memory_bus,
    const system::CoreId core_id,
    const Address64 address,
    const DataSize size,
    const Qword expected,
    const Qword desired)
{
    require_valid_data_size(size);
    this->fence(memory_bus, core_id);
    const Qword observed = memory_bus.read(address, size);
    const bool exchanged = observed == expected;
    if (exchanged)
    {
        memory_bus.write(address, size, desired);
    }
    return AtomicCompareExchangeResult{observed, exchanged};
}

AtomicFetchAddResult X86TsoMemoryModel::fetch_add(
    MemoryBus& memory_bus,
    const system::CoreId core_id,
    const Address64 address,
    const DataSize size,
    const Qword addend)
{
    require_valid_data_size(size);
    this->fence(memory_bus, core_id);
    const Qword old_value = memory_bus.read(address, size);
    const Qword new_value = old_value + addend;
    memory_bus.write(address, size, new_value);
    return AtomicFetchAddResult{old_value, new_value};
}

std::size_t X86TsoMemoryModel::core_index(const system::CoreId core_id) const
{
    if (!this->topology_.contains(core_id))
    {
        throw std::out_of_range{MEMORY_MODEL_UNKNOWN_CORE_MESSAGE};
    }
    return static_cast<std::size_t>(core_id.value());
}

std::optional<Byte> X86TsoMemoryModel::forwarded_byte(
    const system::CoreId core_id,
    const Address64 address) const
{
    const StoreQueue& store_queue = this->store_buffers_.at(this->core_index(core_id));
    const auto found_entry = std::find_if(
        store_queue.rbegin(),
        store_queue.rend(),
        [address](const StoreBufferEntry& entry) {
            return X86TsoMemoryModel::entry_contains_byte(entry, address);
        });
    if (found_entry == store_queue.rend())
    {
        return std::nullopt;
    }
    return X86TsoMemoryModel::entry_byte_at(*found_entry, address);
}

bool X86TsoMemoryModel::entry_contains_byte(const StoreBufferEntry& entry, const Address64 address)
{
    const Address64 byte_count = static_cast<Address64>(data_size_to_bytes(entry.size));
    return address >= entry.address && (address - entry.address) < byte_count;
}

Byte X86TsoMemoryModel::entry_byte_at(const StoreBufferEntry& entry, const Address64 address)
{
    const std::size_t byte_offset = static_cast<std::size_t>(address - entry.address);
    return static_cast<Byte>((entry.value >> (byte_offset * DATA_SIZE_BYTE_BITS)) & MEMORY_MODEL_BYTE_MASK);
}

void X86TsoMemoryModel::drain_entry(MemoryBus& memory_bus, const StoreBufferEntry& entry)
{
    memory_bus.write(entry.address, entry.size, entry.value);
}
}
