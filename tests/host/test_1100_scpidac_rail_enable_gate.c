/* ==========================================================================
 * test_1100_scpidac_rail_enable_gate.c -- issue #1100
 *
 * WHAT IS UNDER TEST
 *
 * DAC_EnsureHardwareInitialized() (SCPIDAC.c) gated DAC writes on
 * `powerState == POWERED_UP` alone. `SYSTem:FORce5V5POWer:STATe 0`
 * (SCPI_Force5v5PowerStateSet, SCPIInterface.c) clears
 * PowerWriteVars.EN_5_10V_Val -- the 5/10V rail's own commanded enable --
 * and calls Power_Write() to drop the rail, WITHOUT changing powerState. So
 * a caller could pass the powerState check while the rail the DAC actually
 * needs was commanded off, and the write would proceed against a chip whose
 * supply is disabled.
 *
 * #1099 fixed the identical blind spot one call site over, at the HAL level
 * in DAC7718_ReadWriteReg() (DAC7718.c) -- gating on both `powerState` and
 * `EN_5_10V_Val`. #1100 is the caller-level twin: DAC_EnsureHardwareInitialized()
 * must agree with the HAL check about what "powered" means, checked at BOTH
 * of its existing power-check points:
 *
 *   1. The top-of-function gate (ahead of the "already initialized"
 *      short-circuit) -- so a rail already disabled refuses immediately and
 *      clears a stale READY flag, exactly like the existing powerState arm.
 *   2. The fresh re-validate immediately before publishing
 *      `dacHardwareInitialized = true` -- DAC7718_Init() can take tens of ms
 *      (several SPI polling loops), so a rail dropped DURING that call must
 *      not be published as ready, exactly like the existing #980-pass-3
 *      powerStillUpAtPublish re-check this mirrors.
 *
 * HOW IT IS TESTED
 *
 * SCPIDAC.c is not a host-test candidate (libscpi + the whole board-config/
 * BoardData graph), so -- same technique as test_980_dac7718_error_paths.c's
 * Part B, which this file is a sibling of rather than an edit to (that file
 * is held by PR #1099's own in-flight EN_5_10V_Val work at the HAL level;
 * splitting into a separate file avoids two PRs racing to edit the same
 * lines) -- this re-implements DAC_EnsureHardwareInitialized()'s control-flow
 * shape against mocked primitives, mirroring the real function's variable
 * names, branch order and early-return points line-for-line, with an
 * OLD_BUGGY (pre-#1100) shape proven against the SAME mocks so every
 * headline assertion has a "this fails without the fix" companion.
 *
 * FIDELITY -- what this does NOT cover
 *
 * Collapses `pPowerWriteVars == NULL` and `!pPowerWriteVars->EN_5_10V_Val`
 * into one boolean input (`railEnabled`), the same simplification the
 * sibling Part B model already makes for `pPowerState == NULL` vs.
 * `powerState != POWERED_UP` under its `powered` input -- both real NULL
 * checks are refusals with identical caller-visible behaviour, so nothing is
 * lost by not distinguishing them here. No real FreeRTOS semaphore, no real
 * critical section -- same caveat as Part B; see its header comment.
 * ========================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "test_framework.h"

typedef struct {
    bool hardwareInitialized;  /* dacHardwareInitialized */
    uint8_t instanceId;        /* dacInstanceId, 0xFF == none */
    bool initInProgress;       /* dacInitInProgress */
    bool slotConsumed;         /* mirrors MAX_DAC7718_CONFIG==1's allocator */
    int newConfigCalls;
    int initCalls;
} DacState;

static void dac_state_init(DacState *s)
{
    s->hardwareInitialized = false;
    s->instanceId = 0xFF;
    s->initInProgress = false;
    s->slotConsumed = false;
    s->newConfigCalls = 0;
    s->initCalls = 0;
}

/* Same one-slot-allocator fidelity as test_980's mock_new_config(). */
static uint8_t mock_new_config(DacState *s)
{
    s->newConfigCalls++;
    if (s->slotConsumed) {
        return 0xFF;
    }
    s->slotConsumed = true;
    return 0;
}

static bool mock_init(DacState *s, bool succeeds)
{
    s->initCalls++;
    return succeeds;
}

/* #1100 shape -- mirrors DAC_EnsureHardwareInitialized() from SCPIDAC.c as it
 * stands after #1100: the powerState gate, then the NEW EN_5_10V_Val gate
 * (both ahead of the "already initialized" short-circuit and both clearing a
 * stale READY flag on refusal), then the existing claim/allocate/init
 * region, then BOTH fresh reads -- powerState and EN_5_10V_Val -- re-checked
 * immediately before publishing. */
static bool ensure_hardware_initialized_railgate(DacState *s, bool powered,
                                                  bool railEnabled,
                                                  bool boardOk, bool variantOk,
                                                  bool initSucceeds,
                                                  bool powerStillUpAtPublish,
                                                  bool railStillEnabledAtPublish)
{
    if (!powered) {
        s->hardwareInitialized = false;
        return false;
    }

    /* #1100: the new gate, in the same position (ahead of the short-circuit)
     * and with the same clear-on-refusal behaviour as the powerState arm
     * just above. */
    if (!railEnabled) {
        s->hardwareInitialized = false;
        return false;
    }

    if (s->hardwareInitialized) {
        return true;
    }

    if (!boardOk) {
        return false;
    }
    if (!variantOk) {
        return false;
    }

    bool claimed;
    if (s->initInProgress) {
        claimed = false;
    } else {
        s->initInProgress = true;
        claimed = true;
    }

    if (!claimed) {
        return false;
    }

    if (s->hardwareInitialized) {
        s->initInProgress = false;
        return true;
    }

    if (s->instanceId == 0xFF) {
        uint8_t newId = mock_new_config(s);
        if (newId == 0xFF) {
            s->initInProgress = false;
            return false;
        }
        s->instanceId = newId;
    }

    if (!mock_init(s, initSucceeds)) {
        s->initInProgress = false;
        return false;
    }

    /* #1100: the fresh re-validate now covers BOTH fields, not just power. */
    if (!powerStillUpAtPublish || !railStillEnabledAtPublish) {
        s->hardwareInitialized = false;
        s->initInProgress = false;
        return false;
    }

    s->hardwareInitialized = true;
    s->initInProgress = false;
    return true;
}

/* Convenience wrapper for the common case where nothing drops mid-call. */
static bool ensure_hardware_initialized(DacState *s, bool powered,
                                        bool railEnabled, bool boardOk,
                                        bool variantOk, bool initSucceeds)
{
    return ensure_hardware_initialized_railgate(s, powered, railEnabled,
                                                boardOk, variantOk,
                                                initSucceeds, true, true);
}

/* Pre-#1100 shape: EN_5_10V_Val is never consulted at either checkpoint --
 * `railEnabled` is accepted only so both shapes share a call signature; the
 * old shape ignores it completely, which IS the bug #1100 fixes. Otherwise
 * identical to the post-#1100 shape (this file does not model #980's
 * init-failure/slot-retention behaviour, already covered by test_980 --
 * this old shape exists only to demonstrate the rail-enable omission). */
static bool old_buggy_ensure_hardware_initialized(DacState *s, bool powered,
                                                  bool railEnabled,
                                                  bool boardOk, bool variantOk,
                                                  bool initSucceeds)
{
    (void)railEnabled;   /* accepted, never consulted -- the #1100 bug */

    if (!powered) {
        s->hardwareInitialized = false;
        return false;
    }

    if (s->hardwareInitialized) {
        return true;
    }

    if (!boardOk || !variantOk) {
        return false;
    }

    bool claimed;
    if (s->initInProgress) {
        claimed = false;
    } else {
        s->initInProgress = true;
        claimed = true;
    }
    if (!claimed) {
        return false;
    }

    if (s->instanceId == 0xFF) {
        uint8_t newId = mock_new_config(s);
        if (newId == 0xFF) {
            s->initInProgress = false;
            return false;
        }
        s->instanceId = newId;
    }

    if (!mock_init(s, initSucceeds)) {
        s->initInProgress = false;
        return false;
    }

    /* Old shape re-validates power only -- rail never re-checked either. */
    s->hardwareInitialized = true;
    s->initInProgress = false;
    return true;
}

/* Headline: a caller passing powerState==POWERED_UP with the rail commanded
 * OFF must be refused, and refused BEFORE ever touching the allocator --
 * this is a config precondition, not a transient hardware failure. */
TEST(rail_disabled_refuses_even_when_powered_and_board_ok)
{
    DacState s;
    dac_state_init(&s);

    bool result = ensure_hardware_initialized(&s, true, false, true, true, true);

    ASSERT_FALSE(result);
    ASSERT_FALSE(s.hardwareInitialized);
    ASSERT_EQ(0, s.newConfigCalls);
    ASSERT_EQ(0, s.initCalls);
}

/* The regression this ticket exists to prevent: the SAME inputs that #1100
 * correctly refuses above are WRONGLY accepted by the pre-fix shape. Without
 * this companion, `rail_disabled_refuses_even_when_powered_and_board_ok`
 * alone cannot show the check is meaningful rather than vacuously true. */
TEST(old_shape_incorrectly_succeeds_with_rail_disabled)
{
    DacState s;
    dac_state_init(&s);

    bool result = old_buggy_ensure_hardware_initialized(&s, true, false,
                                                         true, true, true);

    ASSERT_TRUE(result);
    ASSERT_TRUE(s.hardwareInitialized);
}

/* A caller that raced ahead and already saw hardwareInitialized==true must
 * not get a stale "true" back once the rail is commanded off -- same
 * discipline #980 item 3 already established for powerState, now extended
 * to EN_5_10V_Val. The flag is cleared, not just the return value refused,
 * so DAC7718_Init()'s register write re-runs on the rail's return rather
 * than trusting stale hardware state. */
TEST(rail_disabled_clears_a_previously_ready_flag)
{
    DacState s;
    dac_state_init(&s);
    /* Get to a genuinely-ready state first. */
    ASSERT_TRUE(ensure_hardware_initialized(&s, true, true, true, true, true));
    ASSERT_TRUE(s.hardwareInitialized);

    bool result = ensure_hardware_initialized(&s, true, false, true, true, true);

    ASSERT_FALSE(result);
    ASSERT_FALSE(s.hardwareInitialized);
}

/* The race #1100 closes at the SECOND checkpoint: the rail was enabled at
 * the top of the call, but is commanded off during DAC7718_Init()'s
 * tens-of-ms run. Re-reading powerState alone (the pre-#1100 shape) cannot
 * catch this -- only the rail re-read does. */
TEST(rail_dropping_during_init_is_not_published_as_ready)
{
    DacState s;
    dac_state_init(&s);

    bool result = ensure_hardware_initialized_railgate(&s, true, true, true,
                                                        true, true,
                                                        true /* power still up */,
                                                        false /* rail dropped mid-init */);

    ASSERT_FALSE(result);
    ASSERT_FALSE(s.hardwareInitialized);
    ASSERT_EQ(1, s.initCalls);   /* Init DID run -- the drop is caught after */
}

/* Baseline: the new checks must not disturb the good path. Exactly one
 * allocation, exactly one init call, published ready. */
TEST(rail_enabled_and_powered_succeeds)
{
    DacState s;
    dac_state_init(&s);

    bool result = ensure_hardware_initialized(&s, true, true, true, true, true);

    ASSERT_TRUE(result);
    ASSERT_TRUE(s.hardwareInitialized);
    ASSERT_EQ(1, s.newConfigCalls);
    ASSERT_EQ(1, s.initCalls);
}

/* A rail-disabled refusal must not burn the one-slot allocator: once the
 * rail returns, the SAME slot is reused, not re-allocated (mirrors #980's
 * power_loss_clears_ready_but_retains_the_slot for the powerState arm). The
 * rail-enable gate sits ahead of any allocator touch, so this follows from
 * the same code path as the headline test above, but is asserted directly
 * across a disable/re-enable cycle to prove it end to end. */
TEST(rail_recovery_reuses_existing_slot_no_reallocation)
{
    DacState s;
    dac_state_init(&s);
    ASSERT_TRUE(ensure_hardware_initialized(&s, true, true, true, true, true));
    ASSERT_EQ(1, s.newConfigCalls);
    uint8_t idBefore = s.instanceId;

    /* Rail drops: refused, flag cleared, slot untouched. */
    ASSERT_FALSE(ensure_hardware_initialized(&s, true, false, true, true, true));
    ASSERT_EQ(idBefore, s.instanceId);
    ASSERT_EQ(1, s.newConfigCalls);   /* no second allocation attempt */

    /* Rail returns: re-initializes on the SAME slot. */
    ASSERT_TRUE(ensure_hardware_initialized(&s, true, true, true, true, true));
    ASSERT_EQ(idBefore, s.instanceId);
    ASSERT_EQ(1, s.newConfigCalls);   /* still just the one allocation, ever */
    ASSERT_EQ(2, s.initCalls);        /* but Init() re-ran */
}

int main(void)
{
    printf("#1100 -- SCPIDAC caller-level power check misses the rail enable\n");
    printf("---------------------------------------------\n");

    RUN(rail_disabled_refuses_even_when_powered_and_board_ok);
    RUN(old_shape_incorrectly_succeeds_with_rail_disabled);
    RUN(rail_disabled_clears_a_previously_ready_flag);
    RUN(rail_dropping_during_init_is_not_published_as_ready);
    RUN(rail_enabled_and_powered_succeeds);
    RUN(rail_recovery_reuses_existing_slot_no_reallocation);

    return TEST_SUMMARY();
}
