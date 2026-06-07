#pragma once

#include <cstddef>
#include <vector>

#include <mnos/os/mm/address.hpp>
#include <mnos/os/proc/process_id.hpp>
#include <mnos/os/sched/wait_queue.hpp>

namespace mnos::os::proc
{
inline constexpr mm::AddressValue FUTEX_WORD_ALIGNMENT_BYTES = mm::AddressValue{4};

class FutexKey final
{
public:
    FutexKey(ProcessId process_id, mm::VirtualAddress address);

    [[nodiscard]] ProcessId process_id() const noexcept;
    [[nodiscard]] mm::VirtualAddress address() const noexcept;
    [[nodiscard]] bool operator==(const FutexKey& other) const noexcept = default;

private:
    ProcessId process_id_;
    mm::VirtualAddress address_;
};

class FutexTable final
{
public:
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t futex_count() const noexcept;
    [[nodiscard]] std::size_t waiter_count(FutexKey key) const noexcept;
    [[nodiscard]] bool contains(FutexKey key, const sched::ThreadContext& thread) const noexcept;

    void wait(FutexKey key, sched::ThreadContext& thread);
    [[nodiscard]] sched::ThreadContext* wake_one(FutexKey key);
    [[nodiscard]] std::vector<sched::ThreadContext*> wake_all(FutexKey key);

private:
    class FutexBucket final
    {
    public:
        explicit FutexBucket(FutexKey key) noexcept;

        [[nodiscard]] FutexKey key() const noexcept;
        [[nodiscard]] sched::WaitQueue& wait_queue() noexcept;
        [[nodiscard]] const sched::WaitQueue& wait_queue() const noexcept;

    private:
        FutexKey key_;
        sched::WaitQueue wait_queue_;
    };

    [[nodiscard]] FutexBucket& bucket_for(FutexKey key);
    [[nodiscard]] const FutexBucket* find_bucket(FutexKey key) const noexcept;
    [[nodiscard]] FutexBucket* find_bucket(FutexKey key) noexcept;
    void erase_empty_bucket(FutexKey key) noexcept;

    std::vector<FutexBucket> buckets_;
};
}
