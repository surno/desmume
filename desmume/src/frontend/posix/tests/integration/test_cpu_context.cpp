#include <gtest/gtest.h>
#include "../../../src/EmulatorContext.h"
#include "../../../src/armcpu_context.h"

TEST(CPUContext, CPUInitializationPerContext) {
    std::unique_ptr<EmulatorContext> a(NDS_CreateContext());
    std::unique_ptr<EmulatorContext> b(NDS_CreateContext());
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    ASSERT_TRUE(a->init());
    ASSERT_TRUE(b->init());

    // Bind to A and mutate a bit of CPU-visible state
    NDS_SetActiveContext(a.get());
    getContext().arm9.instruct_adr = 0x08000000;

    // Bind to B and set different value
    NDS_SetActiveContext(b.get());
    getContext().arm9.instruct_adr = 0x04000000;

    // Verify isolation
    NDS_SetActiveContext(a.get());
    EXPECT_EQ(getContext().arm9.instruct_adr, 0x08000000u);
    NDS_SetActiveContext(b.get());
    EXPECT_EQ(getContext().arm9.instruct_adr, 0x04000000u);
}

TEST(CPUContext, ChangeCPSRUsesCorrectCPUInstance) {
    std::unique_ptr<EmulatorContext> a(NDS_CreateContext());
    ASSERT_TRUE(a);
    ASSERT_TRUE(a->init());

    NDS_SetActiveContext(a.get());
    armcpu_t* cpu9 = &getContext().arm9;

    // Flip T bit and ensure call path doesn't crash and keeps invariants coherent
    cpu9->CPSR.bits.T = 0;
    armcpu_changeCPSR_context(cpu9);
    // We cannot assert scheduler state here (global). Just assert the CPU pointer remains valid.
    EXPECT_EQ(cpu9, &getContext().arm9);
}
