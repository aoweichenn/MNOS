#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/system/core_id.hpp>
#include <mnos/cpu/system/core_topology.hpp>

namespace mnos::cpu::memory
{
struct StoreBufferEntry
{
    system::CoreId core_id;
    Address64 address = Address64{0};
    DataSize size = DataSize::QWORD;
    Qword value = Qword{0};
    Qword sequence = Qword{0};
};

struct AtomicCompareExchangeResult
{
    Qword observed = Qword{0};
    bool exchanged = false;
};

struct AtomicFetchAddResult
{
    Qword old_value = Qword{0};
    Qword new_value = Qword{0};
};

class X86TsoMemoryModel final
{
public:
    explicit X86TsoMemoryModel(system::CoreTopology topology = system::CoreTopology::single_core());

    [[nodiscard]] const system::CoreTopology& topology() const noexcept;
    [[nodiscard]] std::size_t pending_store_count() const noexcept;
    [[nodiscard]] std::size_t pending_store_count(system::CoreId core_id) const;
    [[nodiscard]] bool has_pending_stores(system::CoreId core_id) const;

    void store(system::CoreId core_id, Address64 address, DataSize size, Qword value);
    [[nodiscard]] Qword load(system::CoreId core_id, const MemoryBus& memory_bus, Address64 address, DataSize size)
        const;

    [[nodiscard]] bool drain_one(MemoryBus& memory_bus);
    [[nodiscard]] bool drain_core(MemoryBus& memory_bus, system::CoreId core_id);
    void drain_all(MemoryBus& memory_bus);
    void fence(MemoryBus& memory_bus, system::CoreId core_id);

    [[nodiscard]] AtomicCompareExchangeResult compare_exchange(
        MemoryBus& memory_bus,
        system::CoreId core_id,
        Address64 address,
        DataSize size,
        Qword expected,
        Qword desired);
    [[nodiscard]] AtomicFetchAddResult fetch_add(
        MemoryBus& memory_bus,
        system::CoreId core_id,
        Address64 address,
        DataSize size,
        Qword addend);

private:
    using StoreQueue = std::deque<StoreBufferEntry>;

    [[nodiscard]] std::size_t core_index(system::CoreId core_id) const;
    [[nodiscard]] std::optional<Byte> forwarded_byte(system::CoreId core_id, Address64 address) const;
    [[nodiscard]] static bool entry_contains_byte(const StoreBufferEntry& entry, Address64 address);
    [[nodiscard]] static Byte entry_byte_at(const StoreBufferEntry& entry, Address64 address);
    static void drain_entry(MemoryBus& memory_bus, const StoreBufferEntry& entry);

    system::CoreTopology topology_;
    std::vector<StoreQueue> store_buffers_;
    Qword next_sequence_ = Qword{0};
};
}
