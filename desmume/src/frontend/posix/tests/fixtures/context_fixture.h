#pragma once
#include <gtest/gtest.h>
#include "../../../src/EmulatorContext.h"

/// Shared fixture providing a ready-to-use EmulatorContext bound to the test thread.
class BoundContextFixture : public ::testing::Test {
protected:
    EmulatorContext* ctx = nullptr;

    void SetUp() override {
        ctx = NDS_CreateContext();
        ASSERT_NE(ctx, nullptr);
        ASSERT_TRUE(ctx->init());
        NDS_SetActiveContext(ctx);
    }

    void TearDown() override {
        // Unbind explicitly, then destroy
        if (NDS_GetActiveContext() == ctx) {
            NDS_SetActiveContext(nullptr);
        }
        if (ctx) {
            NDS_DestroyContext(ctx);
            ctx = nullptr;
        }
    }
};
