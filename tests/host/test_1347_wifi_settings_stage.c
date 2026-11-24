/* ==========================================================================
 * test_1347_wifi_settings_stage.c -- host test for Util/WifiSettingsStage.h
 * (#1347): LAN:LOAD 1 / LAN:FACRESET 1 must stage the loaded settings into
 * the RUNTIME copy, but only after the update was accepted, and without
 * replacing the runtime copy's MAC.
 *
 * The header is compiled from the SAME text the firmware uses. Only
 * wifi_manager_settings_t is a stand-in (the real one needs Harmony headers);
 * it keeps the members the rule touches. The handlers' use of the helper is
 * pinned by check_1347_stage_wiring.py.
 *
 * Mutants (each must turn this suite red; see the PR body):
 *   M1  drop the staging copy                        -> stages_after_accept...
 *   M2  copy BEFORE the update's return is checked   -> rejected_update...
 *   M3  drop the MAC restore                         -> mac_is_kept...
 *
 * Run: make -C tests/host run
 * ========================================================================== */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "test_framework.h"

typedef struct { uint8_t addr[6]; } WDRV_WINC_MAC_ADDR;
typedef struct {
    char ssid[33];
    char hostName[33];
    char passKey[65];
    WDRV_WINC_MAC_ADDR macAddr;
    int networkMode;
    int securityMode;
    bool isEnabled;
    uint16_t tcpPort;
    bool ssidHidden;
    bool powerSaveDisabled;
} wifi_manager_settings_t;

/* The critical-section stub COUNTS depth, and memcpy is wrapped so each copy
 * records the depth it ran at: a copy outside the section (or a second copy
 * inside it) is visible, which a bare no-op stub never was. */
static int g_crit;
static int g_copies;          /* memcpy calls since the last reset */
static int g_depth_at_copy;   /* depth seen by the last memcpy */
static int g_crit_in_update;  /* depth seen inside the update callback */
static void *crit_memcpy(void *d, const void *src, size_t n)
{
    g_copies++;
    g_depth_at_copy = g_crit;
    return (memcpy)(d, src, n);
}
#define memcpy(d, src, n) crit_memcpy((d), (src), (n))
#define taskENTER_CRITICAL() (g_crit++)
#define taskEXIT_CRITICAL()  (g_crit--)
#include "WifiSettingsStage.h"   /* real header (via -I firmware/src/Util) */

static bool g_accept;
static int  g_update_calls;
static bool fake_update(wifi_manager_settings_t *p) {
    (void) p;
    g_crit_in_update = g_crit;
    g_update_calls++;
    return g_accept;
}

static wifi_manager_settings_t mk(const char *ssid, int mode, const uint8_t mac[6]) {
    wifi_manager_settings_t s;
    memset(&s, 0, sizeof s);
    strcpy(s.ssid, ssid);
    strcpy(s.hostName, ssid);
    strcpy(s.passKey, "old-pass");
    memcpy(s.macAddr.addr, mac, 6);
    s.networkMode = mode;
    s.securityMode = 3;
    s.isEnabled = true;
    s.tcpPort = 9760;
    s.ssidHidden = true;
    return s;
}

static const uint8_t MAC_RT[6]  = {0x00, 0x1E, 0xC0, 0xAA, 0x95, 0xA7};
static const uint8_t MAC_NVM[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
static const uint8_t MAC_ZERO[6] = {0, 0, 0, 0, 0, 0};

TEST(stages_after_accept_every_field_reaches_runtime)
{
    wifi_manager_settings_t rt = mk("OldNet", 1, MAC_RT);
    wifi_manager_settings_t ld = mk("DAQiFi", 4, MAC_NVM);
    strcpy(ld.passKey, "");
    ld.securityMode = 0; ld.isEnabled = false; ld.tcpPort = 9760; ld.ssidHidden = false;
    ld.powerSaveDisabled = true;
    g_accept = true; g_update_calls = 0;
    ASSERT_TRUE(WifiSettingsStage_ApplyThenStage(&rt, &ld, fake_update));
    ASSERT_EQ(g_update_calls, 1);
    ASSERT_TRUE(strcmp(rt.ssid, "DAQiFi") == 0);
    ASSERT_TRUE(strcmp(rt.hostName, "DAQiFi") == 0);
    ASSERT_TRUE(rt.passKey[0] == '\0');          /* old passphrase is gone */
    ASSERT_EQ(rt.networkMode, 4);
    ASSERT_EQ(rt.securityMode, 0);
    ASSERT_FALSE(rt.isEnabled);
    ASSERT_FALSE(rt.ssidHidden);
    ASSERT_TRUE(rt.powerSaveDisabled);
}

TEST(rejected_update_leaves_runtime_unchanged)
{
    wifi_manager_settings_t rt = mk("OldNet", 1, MAC_RT);
    wifi_manager_settings_t before = rt;
    wifi_manager_settings_t ld = mk("DAQiFi", 4, MAC_NVM);
    g_accept = false; g_update_calls = 0;
    ASSERT_FALSE(WifiSettingsStage_ApplyThenStage(&rt, &ld, fake_update));
    ASSERT_EQ(g_update_calls, 1);
    ASSERT_BYTES(&rt, &before, sizeof rt);
}

TEST(mac_is_kept_not_nvms_and_not_the_zeroed_factory_one)
{
    wifi_manager_settings_t rt = mk("OldNet", 1, MAC_RT);
    wifi_manager_settings_t ld = mk("DAQiFi", 4, MAC_NVM);
    g_accept = true;
    ASSERT_TRUE(WifiSettingsStage_ApplyThenStage(&rt, &ld, fake_update));
    ASSERT_BYTES(rt.macAddr.addr, MAC_RT, 6);
    /* factory default: zeroed MAC */
    wifi_manager_settings_t fd = mk("DAQiFi", 4, MAC_ZERO);
    ASSERT_TRUE(WifiSettingsStage_ApplyThenStage(&rt, &fd, fake_update));
    ASSERT_BYTES(rt.macAddr.addr, MAC_RT, 6);
}

TEST(the_loaded_struct_is_not_modified)
{
    wifi_manager_settings_t rt = mk("OldNet", 1, MAC_RT);
    wifi_manager_settings_t ld = mk("DAQiFi", 4, MAC_NVM);
    wifi_manager_settings_t ld0 = ld;
    g_accept = true;
    (void) WifiSettingsStage_ApplyThenStage(&rt, &ld, fake_update);
    ASSERT_BYTES(&ld, &ld0, sizeof ld);
}

TEST(no_argument_style_stage_is_idempotent)
{
    wifi_manager_settings_t rt = mk("OldNet", 1, MAC_RT);
    wifi_manager_settings_t ld = mk("DAQiFi", 4, MAC_NVM);
    WifiSettingsStage_IntoRuntime(&rt, &ld);
    wifi_manager_settings_t once = rt;
    WifiSettingsStage_IntoRuntime(&rt, &ld);
    ASSERT_BYTES(&rt, &once, sizeof rt);
}

static void reset_counters(void)
{
    g_copies = 0;
    g_depth_at_copy = -1;
    g_crit_in_update = -1;
}

TEST(snapshot_copies_every_field_in_exactly_one_depth_1_section)
{
    wifi_manager_settings_t rt = mk("OldNet", 1, MAC_RT);
    wifi_manager_settings_t dst;
    memset(&dst, 0xAB, sizeof dst);
    reset_counters();
    WifiSettingsStage_Snapshot(&dst, &rt);
    ASSERT_EQ(g_copies, 1);
    ASSERT_EQ(g_depth_at_copy, 1);
    ASSERT_EQ(g_crit, 0);
    ASSERT_BYTES(&dst, &rt, sizeof rt);
}

TEST(overwrite_copies_every_field_incl_mac_in_one_depth_1_section)
{
    wifi_manager_settings_t rt = mk("OldNet", 1, MAC_RT);
    wifi_manager_settings_t ld = mk("DAQiFi", 4, MAC_NVM);
    reset_counters();
    WifiSettingsStage_Overwrite(&rt, &ld);
    ASSERT_EQ(g_copies, 1);
    ASSERT_EQ(g_depth_at_copy, 1);
    ASSERT_EQ(g_crit, 0);
    ASSERT_BYTES(&rt, &ld, sizeof rt);               /* the whole struct, MAC too */
    ASSERT_BYTES(rt.macAddr.addr, MAC_NVM, 6);       /* unchanged no-argument behaviour */
}

TEST(into_runtime_is_one_depth_1_section_and_the_update_runs_outside_it)
{
    wifi_manager_settings_t rt = mk("OldNet", 1, MAC_RT);
    wifi_manager_settings_t ld = mk("DAQiFi", 4, MAC_NVM);
    g_accept = true;
    reset_counters();
    ASSERT_TRUE(WifiSettingsStage_ApplyThenStage(&rt, &ld, fake_update));
    ASSERT_EQ(g_crit_in_update, 0);                  /* update is NOT inside a section */
    ASSERT_EQ(g_copies, 1);
    ASSERT_EQ(g_depth_at_copy, 1);
    ASSERT_EQ(g_crit, 0);
}

TEST(a_rejected_update_takes_no_section_and_no_copy)
{
    wifi_manager_settings_t rt = mk("OldNet", 1, MAC_RT);
    wifi_manager_settings_t ld = mk("DAQiFi", 4, MAC_NVM);
    g_accept = false;
    reset_counters();
    ASSERT_FALSE(WifiSettingsStage_ApplyThenStage(&rt, &ld, fake_update));
    ASSERT_EQ(g_copies, 0);
    ASSERT_EQ(g_crit, 0);
}

int main(void)
{
    printf("WifiSettingsStage.h -- #1347 LOAD 1 / FACRESET 1 runtime staging\n");
    printf("----------------------------------------------------------------\n");
    RUN(stages_after_accept_every_field_reaches_runtime);
    RUN(rejected_update_leaves_runtime_unchanged);
    RUN(mac_is_kept_not_nvms_and_not_the_zeroed_factory_one);
    RUN(the_loaded_struct_is_not_modified);
    RUN(no_argument_style_stage_is_idempotent);
    RUN(snapshot_copies_every_field_in_exactly_one_depth_1_section);
    RUN(overwrite_copies_every_field_incl_mac_in_one_depth_1_section);
    RUN(into_runtime_is_one_depth_1_section_and_the_update_runs_outside_it);
    RUN(a_rejected_update_takes_no_section_and_no_copy);
    return TEST_SUMMARY();
}
