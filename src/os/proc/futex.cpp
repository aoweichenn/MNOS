#include <stdexcept>

#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/proc/futex.hpp>

namespace
{
constexpr const char* FUTEX_INVALID_PROCESS_MESSAGE = "futex key requires a valid process id";
constexpr const char* FUTEX_USER_ADDRESS_MESSAGE = "futex key address must be a user address";
constexpr const char* FUTEX_ALIGNMENT_MESSAGE = "futex key address must be word aligned";
}

namespace mnos::os::proc
{
FutexKey::FutexKey(const ProcessId process_id, const mm::VirtualAddress address) :
    process_id_(process_id),
    address_(address)
{
    if (!this->process_id_.is_valid())
    {
        throw std::invalid_argument{FUTEX_INVALID_PROCESS_MESSAGE};
    }
    if (!mm::is_user_address(this->address_))
    {
        throw std::out_of_range{FUTEX_USER_ADDRESS_MESSAGE};
    }
    if ((this->address_.value() % FUTEX_WORD_ALIGNMENT_BYTES) != mm::AddressValue{0})
    {
        throw std::invalid_argument{FUTEX_ALIGNMENT_MESSAGE};
    }
}

ProcessId FutexKey::process_id() const noexcept
{
    return this->process_id_;
}

mm::VirtualAddress FutexKey::address() const noexcept
{
    return this->address_;
}

FutexTable::FutexBucket::FutexBucket(const FutexKey key) noexcept : key_(key)
{
}

FutexKey FutexTable::FutexBucket::key() const noexcept
{
    return this->key_;
}

sched::WaitQueue& FutexTable::FutexBucket::wait_queue() noexcept
{
    return this->wait_queue_;
}

const sched::WaitQueue& FutexTable::FutexBucket::wait_queue() const noexcept
{
    return this->wait_queue_;
}

bool FutexTable::empty() const noexcept
{
    return this->buckets_.empty();
}

std::size_t FutexTable::futex_count() const noexcept
{
    return this->buckets_.size();
}

std::size_t FutexTable::waiter_count(const FutexKey key) const noexcept
{
    const FutexBucket* const bucket = this->find_bucket(key);
    return bucket == nullptr ? std::size_t{0} : bucket->wait_queue().size();
}

bool FutexTable::contains(
    const FutexKey key,
    const sched::ThreadContext& thread) const noexcept
{
    const FutexBucket* const bucket = this->find_bucket(key);
    return bucket != nullptr && bucket->wait_queue().contains(thread);
}

void FutexTable::wait(const FutexKey key, sched::ThreadContext& thread)
{
    this->bucket_for(key).wait_queue().wait(thread);
}

sched::ThreadContext* FutexTable::wake_one(const FutexKey key)
{
    FutexBucket* const bucket = this->find_bucket(key);
    if (bucket == nullptr)
    {
        return nullptr;
    }

    sched::ThreadContext* const thread = bucket->wait_queue().wake_one();
    this->erase_empty_bucket(key);
    return thread;
}

std::vector<sched::ThreadContext*> FutexTable::wake_all(const FutexKey key)
{
    FutexBucket* const bucket = this->find_bucket(key);
    if (bucket == nullptr)
    {
        return {};
    }

    std::vector<sched::ThreadContext*> threads = bucket->wait_queue().wake_all();
    this->erase_empty_bucket(key);
    return threads;
}

FutexTable::FutexBucket& FutexTable::bucket_for(const FutexKey key)
{
    FutexBucket* const bucket = this->find_bucket(key);
    if (bucket != nullptr)
    {
        return *bucket;
    }

    this->buckets_.emplace_back(key);
    return this->buckets_.back();
}

const FutexTable::FutexBucket* FutexTable::find_bucket(const FutexKey key) const noexcept
{
    for (const FutexBucket& bucket : this->buckets_)
    {
        if (bucket.key() == key)
        {
            return &bucket;
        }
    }
    return nullptr;
}

FutexTable::FutexBucket* FutexTable::find_bucket(const FutexKey key) noexcept
{
    for (FutexBucket& bucket : this->buckets_)
    {
        if (bucket.key() == key)
        {
            return &bucket;
        }
    }
    return nullptr;
}

void FutexTable::erase_empty_bucket(const FutexKey key) noexcept
{
    for (auto bucket = this->buckets_.begin(); bucket != this->buckets_.end(); ++bucket)
    {
        if (bucket->key() == key && bucket->wait_queue().empty())
        {
            static_cast<void>(this->buckets_.erase(bucket));
            return;
        }
    }
}
}
