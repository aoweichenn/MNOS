#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_set>
#include <vector>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/system/core_id.hpp>

namespace mnos::cpu::memory
{
inline constexpr Qword TLB_SHOOTDOWN_FIRST_SEQUENCE = Qword{1};

enum class TlbShootdownScope : std::uint8_t
{
    PAGE,
    ALL,
};

class TlbShootdownRequest final
{
public:
    [[nodiscard]] static TlbShootdownRequest page(
        Qword sequence,
        system::CoreId source_core,
        system::CoreId target_core,
        Address64 linear_address,
        std::optional<ProcessContextId> context_id = std::nullopt) noexcept;
    [[nodiscard]] static TlbShootdownRequest all(
        Qword sequence,
        system::CoreId source_core,
        system::CoreId target_core,
        std::optional<ProcessContextId> context_id = std::nullopt) noexcept;

    [[nodiscard]] Qword sequence() const noexcept;
    [[nodiscard]] system::CoreId source_core() const noexcept;
    [[nodiscard]] system::CoreId target_core() const noexcept;
    [[nodiscard]] TlbShootdownScope scope() const noexcept;
    [[nodiscard]] Address64 linear_address() const;
    [[nodiscard]] bool has_process_context_id() const noexcept;
    [[nodiscard]] ProcessContextId process_context_id() const;

private:
    TlbShootdownRequest(
        Qword sequence,
        system::CoreId source_core,
        system::CoreId target_core,
        TlbShootdownScope scope,
        std::optional<Address64> linear_address,
        std::optional<ProcessContextId> context_id) noexcept;

    Qword sequence_;
    system::CoreId source_core_;
    system::CoreId target_core_;
    TlbShootdownScope scope_;
    std::optional<Address64> linear_address_;
    std::optional<ProcessContextId> context_id_;
};

class TlbShootdownController final
{
public:
    [[nodiscard]] std::size_t pending_count() const noexcept;
    [[nodiscard]] std::size_t acknowledged_count() const noexcept;
    [[nodiscard]] const TlbShootdownRequest& request_page(
        system::CoreId source_core,
        system::CoreId target_core,
        Address64 linear_address,
        std::optional<ProcessContextId> context_id = std::nullopt);
    [[nodiscard]] const TlbShootdownRequest& request_all(
        system::CoreId source_core,
        system::CoreId target_core,
        std::optional<ProcessContextId> context_id = std::nullopt);
    [[nodiscard]] bool has_pending_for(system::CoreId target_core) const noexcept;
    [[nodiscard]] std::optional<TlbShootdownRequest> take_next_for(system::CoreId target_core);
    void acknowledge(Qword sequence);
    [[nodiscard]] bool has_acknowledged(Qword sequence) const noexcept;
    void apply(MemoryManagementUnit& mmu, const TlbShootdownRequest& request);

private:
    [[nodiscard]] Qword next_sequence() noexcept;

    Qword next_sequence_ = TLB_SHOOTDOWN_FIRST_SEQUENCE;
    std::deque<TlbShootdownRequest> pending_requests_;
    std::vector<Qword> acknowledged_sequences_;
    std::unordered_set<Qword> acknowledged_sequence_set_;
};
}
