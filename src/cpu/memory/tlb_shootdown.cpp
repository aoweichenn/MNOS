#include <stdexcept>

#include <mnos/cpu/memory/tlb_shootdown.hpp>

namespace
{
constexpr const char* TLB_SHOOTDOWN_PAGE_ADDRESS_NOT_PRESENT_MESSAGE =
    "tlb shootdown request does not carry a page address";
constexpr const char* TLB_SHOOTDOWN_PCID_NOT_PRESENT_MESSAGE =
    "tlb shootdown request does not carry a process context id";
}

namespace mnos::cpu::memory
{
TlbShootdownRequest TlbShootdownRequest::page(
    const Qword sequence,
    const system::CoreId source_core,
    const system::CoreId target_core,
    const Address64 linear_address,
    std::optional<ProcessContextId> context_id) noexcept
{
    return TlbShootdownRequest{
        sequence,
        source_core,
        target_core,
        TlbShootdownScope::PAGE,
        linear_address,
        context_id};
}

TlbShootdownRequest TlbShootdownRequest::all(
    const Qword sequence,
    const system::CoreId source_core,
    const system::CoreId target_core,
    std::optional<ProcessContextId> context_id) noexcept
{
    return TlbShootdownRequest{
        sequence,
        source_core,
        target_core,
        TlbShootdownScope::ALL,
        std::nullopt,
        context_id};
}

TlbShootdownRequest::TlbShootdownRequest(
    const Qword sequence,
    const system::CoreId source_core,
    const system::CoreId target_core,
    const TlbShootdownScope scope,
    std::optional<Address64> linear_address,
    std::optional<ProcessContextId> context_id) noexcept :
    sequence_(sequence),
    source_core_(source_core), target_core_(target_core), scope_(scope), linear_address_(linear_address),
    context_id_(context_id)
{
}

Qword TlbShootdownRequest::sequence() const noexcept
{
    return this->sequence_;
}

system::CoreId TlbShootdownRequest::source_core() const noexcept
{
    return this->source_core_;
}

system::CoreId TlbShootdownRequest::target_core() const noexcept
{
    return this->target_core_;
}

TlbShootdownScope TlbShootdownRequest::scope() const noexcept
{
    return this->scope_;
}

Address64 TlbShootdownRequest::linear_address() const
{
    if (!this->linear_address_.has_value())
    {
        throw std::logic_error{TLB_SHOOTDOWN_PAGE_ADDRESS_NOT_PRESENT_MESSAGE};
    }
    return this->linear_address_.value();
}

bool TlbShootdownRequest::has_process_context_id() const noexcept
{
    return this->context_id_.has_value();
}

ProcessContextId TlbShootdownRequest::process_context_id() const
{
    if (!this->context_id_.has_value())
    {
        throw std::logic_error{TLB_SHOOTDOWN_PCID_NOT_PRESENT_MESSAGE};
    }
    return this->context_id_.value();
}

std::size_t TlbShootdownController::pending_count() const noexcept
{
    return this->pending_requests_.size();
}

std::size_t TlbShootdownController::acknowledged_count() const noexcept
{
    return this->acknowledged_sequences_.size();
}

const TlbShootdownRequest& TlbShootdownController::request_page(
    const system::CoreId source_core,
    const system::CoreId target_core,
    const Address64 linear_address,
    std::optional<ProcessContextId> context_id)
{
    this->pending_requests_.push_back(
        TlbShootdownRequest::page(this->next_sequence(), source_core, target_core, linear_address, context_id));
    return this->pending_requests_.back();
}

const TlbShootdownRequest& TlbShootdownController::request_all(
    const system::CoreId source_core,
    const system::CoreId target_core,
    std::optional<ProcessContextId> context_id)
{
    this->pending_requests_.push_back(
        TlbShootdownRequest::all(this->next_sequence(), source_core, target_core, context_id));
    return this->pending_requests_.back();
}

bool TlbShootdownController::has_pending_for(const system::CoreId target_core) const noexcept
{
    for (const TlbShootdownRequest& request : this->pending_requests_)
    {
        if (request.target_core() == target_core)
        {
            return true;
        }
    }
    return false;
}

std::optional<TlbShootdownRequest> TlbShootdownController::take_next_for(const system::CoreId target_core)
{
    for (auto request_iterator = this->pending_requests_.begin();
         request_iterator != this->pending_requests_.end();
         ++request_iterator)
    {
        if (request_iterator->target_core() == target_core)
        {
            const TlbShootdownRequest request = *request_iterator;
            this->pending_requests_.erase(request_iterator);
            return request;
        }
    }
    return std::nullopt;
}

void TlbShootdownController::acknowledge(const Qword sequence)
{
    const auto [unused_iterator, inserted] = this->acknowledged_sequence_set_.insert(sequence);
    static_cast<void>(unused_iterator);
    if (!inserted)
    {
        return;
    }
    this->acknowledged_sequences_.push_back(sequence);
}

bool TlbShootdownController::has_acknowledged(const Qword sequence) const noexcept
{
    return this->acknowledged_sequence_set_.contains(sequence);
}

void TlbShootdownController::apply(MemoryManagementUnit& mmu, const TlbShootdownRequest& request)
{
    if (request.scope() == TlbShootdownScope::PAGE)
    {
        if (request.has_process_context_id())
        {
            mmu.invalidate_page(request.linear_address(), request.process_context_id());
        }
        else
        {
            mmu.invalidate_page(request.linear_address());
        }
    }
    else if (request.has_process_context_id())
    {
        mmu.flush_tlb_context(request.process_context_id());
    }
    else
    {
        mmu.flush_tlb();
    }
    this->acknowledge(request.sequence());
}

Qword TlbShootdownController::next_sequence() noexcept
{
    const Qword sequence = this->next_sequence_;
    ++this->next_sequence_;
    return sequence;
}
}
