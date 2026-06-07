#include <cstddef>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/memory/page_table_builder.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/cpu/perf/performance_model.hpp>
#include <mnos/cpu/system/privilege.hpp>

namespace cpu = mnos::cpu;
namespace cpu_execution = mnos::cpu::execution;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_perf = mnos::cpu::perf;
namespace cpu_system = mnos::cpu::system;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_CACHE_LINE_BYTES = 16;
constexpr std::size_t TEST_CACHE_SET_COUNT = 1;
constexpr std::size_t TEST_CACHE_ASSOCIATIVITY = 1;
constexpr cpu::Address64 TEST_CACHE_BASE_ADDRESS = cpu::Address64{0x00};
constexpr cpu::Address64 TEST_CACHE_SECOND_ADDRESS = cpu::Address64{0x20};
constexpr std::size_t TEST_ACCESS_BYTES = 8;
constexpr cpu::CycleCount TEST_CACHE_HIT_CYCLES = cpu::CycleCount{1};
constexpr cpu::CycleCount TEST_CACHE_MISS_PENALTY = cpu::CycleCount{10};
constexpr cpu::CycleCount TEST_DIRTY_EVICTION_PENALTY = cpu::CycleCount{7};
constexpr cpu::CycleCount TEST_TLB_MISS_PENALTY = cpu::CycleCount{4};
constexpr cpu::CycleCount TEST_RETIRE_CYCLES = cpu::CycleCount{2};
constexpr cpu::CycleCount TEST_BRANCH_REDIRECT_CYCLES = cpu::CycleCount{6};
constexpr cpu::CycleCount TEST_EXCEPTION_FLUSH_CYCLES = cpu::CycleCount{8};
constexpr std::size_t TEST_PAGING_MEMORY_SIZE_BYTES = 128 * 1024;
constexpr cpu::Address64 TEST_PAGING_ROOT_TABLE = cpu::Address64{0x1000};
constexpr cpu::Address64 TEST_PAGING_NEXT_TABLE = cpu::Address64{0x2000};
constexpr cpu::Address64 TEST_LINEAR_PAGE = cpu::Address64{0x4000};
constexpr cpu::Address64 TEST_PHYSICAL_PAGE = cpu::Address64{0x9000};
constexpr cpu::Address64 TEST_LINEAR_ADDRESS = TEST_LINEAR_PAGE + cpu::Address64{0x18};
constexpr cpu::Address64 TEST_PHYSICAL_ADDRESS = TEST_PHYSICAL_PAGE + cpu::Address64{0x18};
constexpr cpu::Qword TEST_MEMORY_VALUE = cpu::Qword{0x0102030405060708ULL};

[[nodiscard]] cpu_perf::Stage8PerformanceConfig make_test_config()
{
    return cpu_perf::Stage8PerformanceConfig{
        cpu_memory::CacheGeometry{TEST_CACHE_LINE_BYTES, TEST_CACHE_SET_COUNT, TEST_CACHE_ASSOCIATIVITY},
        cpu_memory::CacheGeometry{TEST_CACHE_LINE_BYTES, TEST_CACHE_SET_COUNT, TEST_CACHE_ASSOCIATIVITY},
        cpu_memory::CacheWritePolicy::WRITE_BACK,
        cpu_execution::PipelineConfig{
            TEST_RETIRE_CYCLES,
            TEST_BRANCH_REDIRECT_CYCLES,
            TEST_EXCEPTION_FLUSH_CYCLES},
        TEST_CACHE_HIT_CYCLES,
        TEST_CACHE_MISS_PENALTY,
        TEST_DIRTY_EVICTION_PENALTY,
        TEST_TLB_MISS_PENALTY};
}
}

TEST(Stage8PerformanceModelTest, AggregatesCacheTlbAndPipelineCounters)
{
    cpu_perf::Stage8PerformanceModel model{make_test_config()};

    model.record_instruction_fetch(TEST_CACHE_BASE_ADDRESS, TEST_ACCESS_BYTES);
    model.record_instruction_fetch(TEST_CACHE_BASE_ADDRESS + cpu::Address64{4}, TEST_ACCESS_BYTES);
    model.record_data_write(TEST_CACHE_BASE_ADDRESS, TEST_ACCESS_BYTES);
    model.record_data_read(TEST_CACHE_SECOND_ADDRESS, TEST_ACCESS_BYTES);
    model.record_tlb_miss();
    model.record_tlb_hit();
    model.record_retired_instruction(false, false);
    model.record_retired_instruction(true, true);
    model.record_exception_flush();

    const cpu_perf::PerformanceCounters& counters = model.counters();
    EXPECT_THAT(counters.instruction_fetches(), Eq(cpu::CycleCount{2}));
    EXPECT_THAT(counters.l1i_misses(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(counters.l1i_hits(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(counters.data_writes(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(counters.data_reads(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(counters.l1d_misses(), Eq(cpu::CycleCount{2}));
    EXPECT_THAT(counters.dirty_evictions(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(counters.tlb_misses(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(counters.tlb_hits(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(counters.instructions_retired(), Eq(cpu::CycleCount{2}));
    EXPECT_THAT(counters.branch_instructions(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(counters.branch_redirects(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(counters.pipeline_flushes(), Eq(cpu::CycleCount{2}));
    EXPECT_GT(counters.cycles(), cpu::CycleCount{0});
    EXPECT_GT(counters.pipeline_stall_cycles(), cpu::CycleCount{0});

    model.reset();
    EXPECT_THAT(model.counters().cycles(), Eq(cpu::CycleCount{0}));
    EXPECT_TRUE(model.instruction_cache().empty());
    EXPECT_TRUE(model.data_cache().empty());
    EXPECT_THAT(model.pipeline().flush_count(), Eq(cpu::CycleCount{0}));
}

TEST(Stage8PerformanceModelTest, MemoryManagementUnitReportsDataCacheAndTlbEvents)
{
    cpu::PhysicalMemory memory(TEST_PAGING_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::PageTableBuilder page_table_builder{
        memory_bus,
        TEST_PAGING_ROOT_TABLE,
        TEST_PAGING_NEXT_TABLE};
    page_table_builder.clear_root_table();
    page_table_builder.map_4k(
        TEST_LINEAR_PAGE,
        TEST_PHYSICAL_PAGE,
        cpu_memory::PagePermissions::kernel_read_write_execute());
    memory.write_qword(TEST_PHYSICAL_ADDRESS, TEST_MEMORY_VALUE);

    cpu_memory::PagingState paging_state;
    paging_state.load_cr3(TEST_PAGING_ROOT_TABLE);
    paging_state.enable();

    cpu_memory::MemoryManagementUnit mmu;
    cpu_perf::Stage8PerformanceModel model{make_test_config()};
    EXPECT_FALSE(mmu.has_stage8_performance_model());
    mmu.attach_stage8_performance_model(model);
    EXPECT_TRUE(mmu.has_stage8_performance_model());

    EXPECT_THAT(
        mmu.read(memory_bus, paging_state, cpu_system::PrivilegeLevel::RING0, TEST_LINEAR_ADDRESS, cpu::DataSize::QWORD),
        Eq(TEST_MEMORY_VALUE));
    EXPECT_THAT(
        mmu.read(memory_bus, paging_state, cpu_system::PrivilegeLevel::RING0, TEST_LINEAR_ADDRESS, cpu::DataSize::QWORD),
        Eq(TEST_MEMORY_VALUE));

    EXPECT_THAT(model.counters().tlb_misses(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(model.counters().tlb_hits(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(model.counters().data_reads(), Eq(cpu::CycleCount{2}));
    EXPECT_THAT(model.counters().l1d_misses(), Eq(cpu::CycleCount{1}));
    EXPECT_THAT(model.counters().l1d_hits(), Eq(cpu::CycleCount{1}));

    const cpu::CycleCount cycles_before_detach = model.counters().cycles();
    mmu.detach_stage8_performance_model();
    EXPECT_FALSE(mmu.has_stage8_performance_model());
    EXPECT_THAT(
        mmu.read(memory_bus, paging_state, cpu_system::PrivilegeLevel::RING0, TEST_LINEAR_ADDRESS, cpu::DataSize::QWORD),
        Eq(TEST_MEMORY_VALUE));
    EXPECT_THAT(model.counters().cycles(), Eq(cycles_before_detach));
}
