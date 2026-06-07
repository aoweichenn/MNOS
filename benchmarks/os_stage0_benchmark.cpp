#include <cstddef>
#include <cstdint>

#include <benchmark/benchmark.h>

#include <mnos/cpu/register/id.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace cpu = mnos::cpu;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace sched = mnos::os::sched;

namespace
{
constexpr std::size_t BENCHMARK_MACHINE_MEMORY_SIZE_BYTES =
    static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{64});
constexpr std::uint32_t BENCHMARK_BOOTSTRAP_PROCESSOR_COUNT = std::uint32_t{4};
constexpr mm::AddressValue BENCHMARK_STACK_BOTTOM_VALUE = mm::AddressValue{0x40000};
constexpr cpu::UQWORD64 BENCHMARK_RAX_VALUE = cpu::UQWORD64{0x1234ABCDULL};
}

static void BM_OSKernelBoot(benchmark::State& state)
{
    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        platform::Machine machine(BENCHMARK_MACHINE_MEMORY_SIZE_BYTES);
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

BENCHMARK(BM_OSKernelBoot);
BENCHMARK(BM_ThreadContextReset);
