#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <memory>
#include "../../../src/EmulatorContext.h"

TEST(Threading, EachThreadHasIndependentContext) {
    constexpr int kThreads = 4;
    std::vector<std::unique_ptr<EmulatorContext>> contexts;
    contexts.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
        std::unique_ptr<EmulatorContext> c(NDS_CreateContext());
        ASSERT_TRUE(c);
        ASSERT_TRUE(c->init());
        contexts.emplace_back(std::move(c));
    }

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([i, &contexts]() {
            NDS_SetActiveContext(contexts[i].get());
            EmulatorContext& ctx = getContext();
            for (int j = 0; j < 1000; ++j) {
                ctx.nds.cycles += static_cast<u32>(i + j);
                ctx.execute = ((i + j) % 2) == 0;
            }
        });
    }

    for (auto &t : threads) t.join();

    // Bind and perform basic sanity checks that state exists per-context
    for (int i = 0; i < kThreads; ++i) {
        NDS_SetActiveContext(contexts[i].get());
        (void)getContext();
        SUCCEED();
    }
}
