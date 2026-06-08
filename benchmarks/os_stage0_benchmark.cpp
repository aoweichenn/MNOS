#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <benchmark/benchmark.h>

#include <mnos/cpu/memory/tlb_shootdown.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/apic.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/os/block/block_device.hpp>
#include <mnos/os/block/buffer_cache.hpp>
#include <mnos/os/fs/simple_fs.hpp>
#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/mm/physical_page_allocator.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/sched/round_robin_scheduler.hpp>
#include <mnos/os/sched/smp_scheduler.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;
namespace block = mnos::os::block;
namespace fs = mnos::os::fs;
namespace io = mnos::os::io;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;

namespace
{
constexpr std::size_t BENCHMARK_MACHINE_MEMORY_SIZE_BYTES =
    static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{64});
constexpr std::uint32_t BENCHMARK_BOOTSTRAP_PROCESSOR_COUNT = std::uint32_t{4};
constexpr mm::AddressValue BENCHMARK_STACK_BOTTOM_VALUE = mm::AddressValue{0x40000};
constexpr cpu::Qword BENCHMARK_RAX_VALUE = cpu::Qword{0x1234ABCDULL};
constexpr mm::PageNumber BENCHMARK_ALLOCATOR_PAGE_COUNT = mm::PageNumber{1024};
constexpr mm::PageNumber BENCHMARK_ALLOCATOR_FIRST_PAGE = mm::PageNumber{8};
constexpr std::size_t BENCHMARK_SCHEDULER_THREAD_COUNT = 16;
constexpr std::uint32_t BENCHMARK_SMP_SCHEDULER_CORE_COUNT = std::uint32_t{4};
constexpr mm::AddressValue BENCHMARK_SCHEDULER_STACK_BASE = mm::AddressValue{0x1000000};
constexpr mm::AddressValue BENCHMARK_SCHEDULER_STACK_STRIDE = sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES;
constexpr cpu_system::CoreId BENCHMARK_BOOT_CORE{0};
constexpr cpu_system::CoreId BENCHMARK_SECOND_CORE{1};
constexpr cpu::Address64 BENCHMARK_TLB_LINEAR_PAGE = cpu::Address64{0x4000};
constexpr cpu::Address64 BENCHMARK_TLB_PHYSICAL_FRAME = cpu::Address64{0x14000};
constexpr cpu::Address64 BENCHMARK_TLB_LEAF_ENTRY = cpu::Address64{0x24000};
constexpr cpu::Qword BENCHMARK_TLB_GENERATION = cpu::Qword{3};
constexpr cpu_memory::ProcessContextId BENCHMARK_TLB_PCID{5};
constexpr std::uint64_t BENCHMARK_BLOCK_DEVICE_BLOCK_COUNT = std::uint64_t{4096};
constexpr std::size_t BENCHMARK_BUFFER_CACHE_CAPACITY_BLOCKS = 64;
constexpr block::BlockAddress BENCHMARK_BLOCK_DEVICE_IO_ADDRESS{std::uint64_t{32}};
constexpr block::BlockAddress BENCHMARK_BUFFER_CACHE_HIT_ADDRESS{std::uint64_t{64}};
constexpr cpu::Byte BENCHMARK_BLOCK_DATA_SEED = cpu::Byte{0x5A};
constexpr std::uint32_t BENCHMARK_SIMPLE_FS_INODE_COUNT = std::uint32_t{64};
constexpr std::string_view BENCHMARK_KERNEL_VFS_FILE_PATH = "/bench";

[[nodiscard]] std::vector<cpu::Byte> make_benchmark_block()
{
    std::vector<cpu::Byte> bytes(block::BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES);
    for (std::size_t byte_index = std::size_t{0}; byte_index < bytes.size(); ++byte_index)
    {
        bytes[byte_index] = static_cast<cpu::Byte>(BENCHMARK_BLOCK_DATA_SEED + static_cast<cpu::Byte>(byte_index));
    }
    return bytes;
}

[[nodiscard]] cpu_memory::PageTranslation make_benchmark_translation()
{
    return cpu_memory::PageTranslation{
        BENCHMARK_TLB_LINEAR_PAGE,
        BENCHMARK_TLB_PHYSICAL_FRAME,
        cpu_memory::PAGE_SIZE_4K_BYTES,
        cpu_memory::PagePermissions::kernel_read_write_execute(),
        BENCHMARK_TLB_LEAF_ENTRY,
        false};
}
}

static void BM_OSKernelBoot(benchmark::State& state)
{
    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        platform::Machine machine(BENCHMARK_MACHINE_MEMORY_SIZE_BYTES, BENCHMARK_BOOTSTRAP_PROCESSOR_COUNT);
        kernel::BootContext boot_context{machine, BENCHMARK_BOOTSTRAP_PROCESSOR_COUNT};
        kernel::Kernel os_kernel{boot_context};
        os_kernel.boot();
        benchmark::DoNotOptimize(os_kernel.is_booted());
    }

    state.SetItemsProcessed(state.iterations());
}

static void BM_ThreadContextReset(benchmark::State& state)
{
    sched::ThreadContext thread{sched::ThreadId::first_kernel_thread(), mm::VirtualAddress{BENCHMARK_STACK_BOTTOM_VALUE}};

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        thread.cpu_state().registers().write(cpu::RegisterId::RAX, BENCHMARK_RAX_VALUE);
        thread.cpu_state().halt();
        thread.reset_cpu_state();
        benchmark::DoNotOptimize(thread.cpu_state().registers().read(cpu::RegisterId::RSP));
    }

    state.SetItemsProcessed(state.iterations());
}

static void BM_PhysicalPageAllocatorAllocateFree(benchmark::State& state)
{
    mm::PhysicalPageAllocator allocator{BENCHMARK_ALLOCATOR_PAGE_COUNT, BENCHMARK_ALLOCATOR_FIRST_PAGE};

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        const mm::PhysicalAddress page = allocator.allocate_page();
        benchmark::DoNotOptimize(page.value());
        allocator.free_page(page);
    }

    state.SetItemsProcessed(state.iterations());
}

static void BM_RoundRobinSchedulerCycle(benchmark::State& state)
{
    std::vector<sched::ThreadContext> threads;
    threads.reserve(BENCHMARK_SCHEDULER_THREAD_COUNT);
    sched::RoundRobinScheduler scheduler;
    for (std::size_t thread_index = 0; thread_index < BENCHMARK_SCHEDULER_THREAD_COUNT; ++thread_index)
    {
        threads.emplace_back(
            sched::ThreadId{sched::THREAD_ID_FIRST_KERNEL_VALUE + static_cast<sched::ThreadId::value_type>(thread_index)},
            mm::VirtualAddress{
                BENCHMARK_SCHEDULER_STACK_BASE +
                static_cast<mm::AddressValue>(thread_index * BENCHMARK_SCHEDULER_STACK_STRIDE)});
        scheduler.enqueue(threads.back());
    }
    static_cast<void>(scheduler.schedule_next());

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        benchmark::DoNotOptimize(scheduler.yield_current());
    }

    state.SetItemsProcessed(state.iterations());
}

static void BM_SmpSchedulerPerCoreCycle(benchmark::State& state)
{
    std::vector<sched::ThreadContext> threads;
    threads.reserve(BENCHMARK_SCHEDULER_THREAD_COUNT);
    sched::SmpScheduler scheduler{cpu_system::CoreTopology{BENCHMARK_SMP_SCHEDULER_CORE_COUNT}};
    for (std::size_t thread_index = 0; thread_index < BENCHMARK_SCHEDULER_THREAD_COUNT; ++thread_index)
    {
        threads.emplace_back(
            sched::ThreadId{sched::THREAD_ID_FIRST_KERNEL_VALUE + static_cast<sched::ThreadId::value_type>(thread_index)},
            mm::VirtualAddress{
                BENCHMARK_SCHEDULER_STACK_BASE +
                static_cast<mm::AddressValue>(thread_index * BENCHMARK_SCHEDULER_STACK_STRIDE)});
        scheduler.enqueue(
            threads.back(),
            cpu_system::CoreId{
                static_cast<cpu_system::CoreId::value_type>(thread_index % BENCHMARK_SMP_SCHEDULER_CORE_COUNT)});
    }
    for (std::uint32_t core_index = std::uint32_t{0}; core_index < BENCHMARK_SMP_SCHEDULER_CORE_COUNT; ++core_index)
    {
        static_cast<void>(scheduler.schedule_next(cpu_system::CoreId{core_index}));
    }

    std::uint32_t next_core_index = std::uint32_t{0};
    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        benchmark::DoNotOptimize(scheduler.yield_current(cpu_system::CoreId{next_core_index}));
        next_core_index = (next_core_index + std::uint32_t{1}) % BENCHMARK_SMP_SCHEDULER_CORE_COUNT;
    }

    state.SetItemsProcessed(state.iterations());
}

static void BM_KernelCreateProcessThread(benchmark::State& state)
{
    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        platform::Machine machine(BENCHMARK_MACHINE_MEMORY_SIZE_BYTES, BENCHMARK_BOOTSTRAP_PROCESSOR_COUNT);
        kernel::BootContext boot_context{machine, BENCHMARK_BOOTSTRAP_PROCESSOR_COUNT};
        kernel::Kernel os_kernel{boot_context};
        os_kernel.boot();
        proc::Process& process = os_kernel.create_process();
        benchmark::DoNotOptimize(&os_kernel.create_thread(process));
    }

    state.SetItemsProcessed(state.iterations());
}

static void BM_KernelSmpTimerPreempt(benchmark::State& state)
{
    platform::Machine machine(BENCHMARK_MACHINE_MEMORY_SIZE_BYTES, BENCHMARK_BOOTSTRAP_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, BENCHMARK_BOOTSTRAP_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& process = os_kernel.create_process();
    static_cast<void>(os_kernel.create_thread_on_core(process, BENCHMARK_BOOT_CORE));
    static_cast<void>(os_kernel.create_thread_on_core(process, BENCHMARK_BOOT_CORE));
    static_cast<void>(os_kernel.smp_scheduler().schedule_next(BENCHMARK_BOOT_CORE));

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        benchmark::DoNotOptimize(os_kernel.handle_smp_timer_interrupt(BENCHMARK_BOOT_CORE));
    }

    state.SetItemsProcessed(state.iterations());
}

static void BM_ApicTimerTick(benchmark::State& state)
{
    cpu_system::LocalApic local_apic{BENCHMARK_BOOT_CORE};
    local_apic.enable();
    local_apic.configure_periodic_timer(
        cpu_system::InterruptVector::timer(),
        kernel::KERNEL_STAGE7_DEFAULT_TIMER_INTERVAL_TICKS);

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        const std::optional<cpu_system::ApicInterrupt> interrupt = local_apic.tick();
        benchmark::DoNotOptimize(interrupt.has_value());
        static_cast<void>(local_apic.take_pending_interrupt());
    }

    state.SetItemsProcessed(state.iterations());
}

static void BM_KernelTimerPreempt(benchmark::State& state)
{
    platform::Machine machine(BENCHMARK_MACHINE_MEMORY_SIZE_BYTES, BENCHMARK_BOOTSTRAP_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, BENCHMARK_BOOTSTRAP_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& process = os_kernel.create_process();
    static_cast<void>(os_kernel.create_thread(process));
    static_cast<void>(os_kernel.create_thread(process));
    static_cast<void>(os_kernel.scheduler().schedule_next());

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        benchmark::DoNotOptimize(os_kernel.handle_timer_interrupt(BENCHMARK_BOOT_CORE));
    }

    state.SetItemsProcessed(state.iterations());
}

static void BM_TlbShootdownApply(benchmark::State& state)
{
    cpu_memory::MemoryManagementUnit target_mmu;
    cpu_memory::TlbShootdownController controller;

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        target_mmu.tlb().insert(make_benchmark_translation(), BENCHMARK_TLB_GENERATION, BENCHMARK_TLB_PCID);
        static_cast<void>(controller.request_page(
            BENCHMARK_BOOT_CORE,
            BENCHMARK_SECOND_CORE,
            BENCHMARK_TLB_LINEAR_PAGE,
            BENCHMARK_TLB_PCID));
        const std::optional<cpu_memory::TlbShootdownRequest> request =
            controller.take_next_for(BENCHMARK_SECOND_CORE);
        if (request.has_value())
        {
            controller.apply(target_mmu, request.value());
        }
        benchmark::DoNotOptimize(controller.acknowledged_count());
    }

    state.SetItemsProcessed(state.iterations());
}

static void BM_MemoryBlockDeviceReadWrite(benchmark::State& state)
{
    block::MemoryBlockDevice device{
        block::BlockDeviceGeometry{block::BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES, BENCHMARK_BLOCK_DEVICE_BLOCK_COUNT}};
    const std::vector<cpu::Byte> source = make_benchmark_block();
    std::vector<cpu::Byte> destination(block::BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES);

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        device.write_block(BENCHMARK_BLOCK_DEVICE_IO_ADDRESS, source);
        device.read_block(BENCHMARK_BLOCK_DEVICE_IO_ADDRESS, destination);
        benchmark::DoNotOptimize(destination.data());
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(
        static_cast<std::int64_t>(state.iterations()) *
        static_cast<std::int64_t>(block::BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES * std::size_t{2}));
}

static void BM_BufferCacheReadHit(benchmark::State& state)
{
    block::MemoryBlockDevice device{
        block::BlockDeviceGeometry{block::BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES, BENCHMARK_BLOCK_DEVICE_BLOCK_COUNT}};
    const std::vector<cpu::Byte> source = make_benchmark_block();
    std::vector<cpu::Byte> destination(block::BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES);
    device.write_block(BENCHMARK_BUFFER_CACHE_HIT_ADDRESS, source);
    block::BufferCache cache{device, BENCHMARK_BUFFER_CACHE_CAPACITY_BLOCKS};
    cache.read_block(BENCHMARK_BUFFER_CACHE_HIT_ADDRESS, destination);

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        cache.read_block(BENCHMARK_BUFFER_CACHE_HIT_ADDRESS, destination);
        benchmark::DoNotOptimize(destination.data());
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(
        static_cast<std::int64_t>(state.iterations()) *
        static_cast<std::int64_t>(block::BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES));
}

static void BM_SimpleFsFileReadWrite(benchmark::State& state)
{
    block::MemoryBlockDevice device{
        block::BlockDeviceGeometry{block::BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES, BENCHMARK_BLOCK_DEVICE_BLOCK_COUNT}};
    block::BufferCache cache{device, BENCHMARK_BUFFER_CACHE_CAPACITY_BLOCKS};
    fs::SimpleFileSystem::format(cache, fs::SimpleFsFormatOptions{BENCHMARK_SIMPLE_FS_INODE_COUNT});
    fs::SimpleFileSystem file_system{cache};
    const fs::InodeNumber file = file_system.create_file(file_system.root_inode(), "bench");
    const std::vector<cpu::Byte> source = make_benchmark_block();
    std::vector<cpu::Byte> destination(source.size());

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        benchmark::DoNotOptimize(file_system.write_file(file, std::uint64_t{0}, source));
        benchmark::DoNotOptimize(file_system.read_file(file, std::uint64_t{0}, destination));
        benchmark::DoNotOptimize(destination.data());
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(
        static_cast<std::int64_t>(state.iterations()) *
        static_cast<std::int64_t>(block::BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES * std::size_t{2}));
}

static void BM_KernelVfsOpenClose(benchmark::State& state)
{
    platform::Machine machine(BENCHMARK_MACHINE_MEMORY_SIZE_BYTES, BENCHMARK_BOOTSTRAP_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, BENCHMARK_BOOTSTRAP_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& process = os_kernel.create_process();
    static_cast<void>(os_kernel.vfs().create_file(BENCHMARK_KERNEL_VFS_FILE_PATH));

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        const io::FileDescriptor descriptor =
            os_kernel.open_file(process, BENCHMARK_KERNEL_VFS_FILE_PATH, io::FileAccessMode::READ_ONLY, false);
        benchmark::DoNotOptimize(descriptor.value());
        benchmark::DoNotOptimize(os_kernel.close_fd(process, descriptor));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_OSKernelBoot);
BENCHMARK(BM_ThreadContextReset);
BENCHMARK(BM_PhysicalPageAllocatorAllocateFree);
BENCHMARK(BM_RoundRobinSchedulerCycle);
BENCHMARK(BM_SmpSchedulerPerCoreCycle);
BENCHMARK(BM_KernelCreateProcessThread);
BENCHMARK(BM_KernelSmpTimerPreempt);
BENCHMARK(BM_ApicTimerTick);
BENCHMARK(BM_KernelTimerPreempt);
BENCHMARK(BM_TlbShootdownApply);
BENCHMARK(BM_MemoryBlockDeviceReadWrite);
BENCHMARK(BM_BufferCacheReadHit);
BENCHMARK(BM_SimpleFsFileReadWrite);
BENCHMARK(BM_KernelVfsOpenClose);
