/* ==========================================================================
 * test_1202_cap_json_streaming_input.c -- issue #1202, part 1
 *
 * WHAT IS UNDER TEST
 *
 * firmware/src/services/Capabilities.c's Capabilities_HasStreamingInput() and
 * Capabilities_GetStreamingSummary() -- THE REAL ONES, extracted verbatim by
 * the Makefile recipe into gen_1202_cap_streaming.h (Capabilities.c pulls in
 * BoardConfig.h and streaming.h, which are not host-buildable). The real
 * Capabilities.h IS host-includable and is used as-is, so the summary struct
 * and the predicate's prototype are the shipped ones.
 *
 * THE DEFECT
 *
 * CONF:CAP:JSON? published current_max_rate_hz only when an analog channel
 * was enabled. START also accepts DIO alone as an input
 * (BOARDRUNTIMECONFIG_DIO_GLOBAL_ENABLE) and applies the cap computation's
 * zero-analog branch to it, so a client that pre-validated against the
 * capability document refused a configuration the device runs. Against the
 * pre-#1202 source, dio_only_publishes_the_computed_cap fails (rate 0).
 *
 * The SAME gate is repeated in SCPIInterface.c's per-interface recompute;
 * the Makefile pins that it goes through the same predicate, and the
 * python-test-suite companion checks the full CONF:CAP:JSON? path on
 * hardware.
 * ========================================================================== */

#include "test_framework.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "Capabilities.h"   /* real header, via -I firmware/src/services */

/* ── Mocked dependencies of the two extracted functions ──────────────── */
typedef struct { uint8_t BoardVariant; } tBoardConfig;
enum { BOARDCONFIG_ALL_CONFIG };
enum { BOARDRUNTIMECONFIG_DIO_GLOBAL_ENABLE };

#define STREAMING_ISR_MAX_HZ        22000
#define STREAMING_ISR_MAX_HZ_LEGACY 16000
#define STREAMING_TYPE1_AGG_MAX_HZ  55000
#define STREAMING_TICK_BUDGET       110000
#define STREAMING_TICK_OVERHEAD     6

typedef struct {
    uint32_t adcAdditiveHz;
    uint32_t transportHz;
    uint32_t scanBoundHz;
    uint32_t sdAdditiveHz;
    uint16_t totalChannels;
} StreamingCapTerms;

static tBoardConfig g_cfg = { 1u };
static bool  g_dio_enable;
static bool *g_dio_ptr = &g_dio_enable;   /* NULL models an unavailable slot */

/* What the mocked cap computation reports. Distinct non-zero terms so a test
 * can tell "published the computation" from "published something else". */
static uint16_t g_analog;
static uint32_t g_composite = 4321u;

static const void *BoardConfig_Get(int which, int idx) {
    (void)which; (void)idx;
    return &g_cfg;
}

static void *BoardRunTimeConfig_Get(int which) {
    (void)which;
    return g_dio_ptr;
}

static uint32_t Streaming_ComputeMaxFreqTermsForConfig(StreamingCapTerms *t) {
    t->adcAdditiveHz = 11111u;
    t->transportHz   = 4321u;
    t->scanBoundHz   = 33333u;
    t->sdAdditiveHz  = 0u;
    t->totalChannels = g_analog;
    return g_composite;
}

/* THE REAL FUNCTIONS, extracted verbatim by the Makefile recipe. */
#include "gen_1202_cap_streaming.h"

static void set(uint16_t analog, bool dio) {
    g_analog = analog;
    g_dio_enable = dio;
    g_dio_ptr = &g_dio_enable;
}

TEST(no_input_publishes_no_rate) {
    CapabilitiesStreamingSummary s;
    set(0, false);
    Capabilities_GetStreamingSummary(&s);
    ASSERT_EQ(s.maxFreqHz, 0);
    ASSERT_EQ(s.capAdcAdditiveHz, 0);
    ASSERT_EQ(s.capTransportHz, 0);
    ASSERT_EQ(s.capScanBoundHz, 0);
    ASSERT_EQ(s.capSdAdditiveHz, 0);
}

/* The #1202 case: START accepts this configuration, so a rate must exist. */
TEST(dio_only_publishes_the_computed_cap) {
    CapabilitiesStreamingSummary s;
    set(0, true);
    Capabilities_GetStreamingSummary(&s);
    ASSERT_EQ(s.maxFreqHz, g_composite);
    ASSERT_EQ(s.capAdcAdditiveHz, 11111u);
    ASSERT_EQ(s.capTransportHz, 4321u);
    ASSERT_EQ(s.capScanBoundHz, 33333u);
}

TEST(analog_only_publishes_the_computed_cap) {
    CapabilitiesStreamingSummary s;
    set(3, false);
    Capabilities_GetStreamingSummary(&s);
    ASSERT_EQ(s.maxFreqHz, g_composite);
    ASSERT_EQ(s.capTransportHz, 4321u);
}

TEST(analog_and_dio_publishes_the_computed_cap) {
    CapabilitiesStreamingSummary s;
    set(2, true);
    Capabilities_GetStreamingSummary(&s);
    ASSERT_EQ(s.maxFreqHz, g_composite);
}

TEST(predicate_truth_table) {
    set(0, false); ASSERT_FALSE(Capabilities_HasStreamingInput(0));
    set(0, true);  ASSERT_TRUE(Capabilities_HasStreamingInput(0));
    set(0, false); ASSERT_TRUE(Capabilities_HasStreamingInput(1));
    set(0, true);  ASSERT_TRUE(Capabilities_HasStreamingInput(24));
}

/* An unavailable DIO slot is not an input: the permissive answer would
 * publish a rate for a device START would refuse. */
TEST(missing_dio_slot_is_not_an_input) {
    set(0, true);
    g_dio_ptr = NULL;
    ASSERT_FALSE(Capabilities_HasStreamingInput(0));
    CapabilitiesStreamingSummary s;
    Capabilities_GetStreamingSummary(&s);
    ASSERT_EQ(s.maxFreqHz, 0);
}

int main(void) {
    printf("#1202 CONF:CAP:JSON? streaming-input gate\n");
    RUN(no_input_publishes_no_rate);
    RUN(dio_only_publishes_the_computed_cap);
    RUN(analog_only_publishes_the_computed_cap);
    RUN(analog_and_dio_publishes_the_computed_cap);
    RUN(predicate_truth_table);
    RUN(missing_dio_slot_is_not_an_input);
    return test_summary();
}
