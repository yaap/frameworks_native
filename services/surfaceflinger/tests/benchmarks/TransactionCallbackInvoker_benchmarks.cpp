#include <benchmark/benchmark.h>

#include "TransactionCallbackInvoker.h"

namespace android::surfaceflinger {

static void TransactionCallbackInvoker_sendCallbacks(benchmark::State& state) {
    TransactionCallbackInvoker invoker;

    size_t numCallbacks = static_cast<size_t>(state.range(0));

    std::vector<sp<BBinder>> listeners;
    std::vector<sp<BBinder>> surfaceControls;
    std::vector<std::vector<CallbackId>> callbackIds;

    listeners.reserve(numCallbacks);
    surfaceControls.reserve(numCallbacks);
    callbackIds.reserve(numCallbacks);

    for (size_t i = 0; i < numCallbacks; i++) {
        listeners.push_back(sp<BBinder>::make());
        surfaceControls.push_back(sp<BBinder>::make());
        callbackIds.push_back({{static_cast<int64_t>(i), CallbackId::Type::ON_COMMIT}});
    }

    for (auto _ : state) {
        for (size_t i = 0; i < numCallbacks; i++) {
            invoker.addCallbackHandle(
                    CallbackHandle{listeners[i], callbackIds[i], surfaceControls[i]});
        }
        invoker.sendCallbacks(false);
        invoker.clearCompletedTransactions();
    }
}

BENCHMARK(TransactionCallbackInvoker_sendCallbacks)->Arg(1)->Arg(2)->Arg(5);

} // namespace android::surfaceflinger
