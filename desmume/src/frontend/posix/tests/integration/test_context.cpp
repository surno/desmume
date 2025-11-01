#include <gtest/gtest.h>
#include "../../../src/EmulatorContext.h"

// Basic context lifecycle and thread binding tests

class ContextFixture : public ::testing::Test {
protected:
    EmulatorContext* ctx = nullptr;

    void SetUp() override {
        ctx = NDS_CreateContext();
        ASSERT_NE(ctx, nullptr);
        ASSERT_TRUE(ctx->init());
    }

    void TearDown() override {
        if (ctx) {
            NDS_DestroyContext(ctx);
            ctx = nullptr;
        }
    }
};

TEST_F(ContextFixture, ContextCreationInitialized) {
    EXPECT_TRUE(ctx->isInitialized());
}

TEST_F(ContextFixture, ThreadLocalBindingWorks) {
    NDS_SetActiveContext(ctx);
    EmulatorContext* active = NDS_GetActiveContext();
    EXPECT_EQ(active, ctx);
}

TEST_F(ContextFixture, ContextGuardBindsTemporarily) {
    EmulatorContext* prev = NDS_GetActiveContext();
    {
        ContextGuard guard(ctx);
        EmulatorContext* active = NDS_GetActiveContext();
        EXPECT_EQ(active, ctx);
    }
    // After guard, active should be restored to previous (may be nullptr)
    EXPECT_EQ(NDS_GetActiveContext(), prev);
}

TEST(ContextIsolation, MultipleIndependentContexts) {
    std::unique_ptr<EmulatorContext> a(NDS_CreateContext());
    std::unique_ptr<EmulatorContext> b(NDS_CreateContext());
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    ASSERT_TRUE(a->init());
    ASSERT_TRUE(b->init());

    NDS_SetActiveContext(a.get());
    getContext().nds.cycles = 1234;
    getContext().execute = true;

    NDS_SetActiveContext(b.get());
    getContext().nds.cycles = 5678;
    getContext().execute = false;

    NDS_SetActiveContext(a.get());
    EXPECT_EQ(getContext().nds.cycles, 1234);
    EXPECT_TRUE(getContext().execute);

    NDS_SetActiveContext(b.get());
    EXPECT_EQ(getContext().nds.cycles, 5678);
    EXPECT_FALSE(getContext().execute);
}
