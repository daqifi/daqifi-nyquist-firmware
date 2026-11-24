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

static int g_crit;
#define taskENTER_CRITICAL() (g_crit++)
#define taskEXIT_CRITICAL()  (g_crit--)
#include "WifiSettingsStage.h"   /* real header (via -I firmware/src/Util) */

static bool g_accept;
static int  g_update_calls;
static bool fake_update(wifi_manager_settings_t *p) {
    (void) p;
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

int main(void)
{
    printf("WifiSettingsStage.h -- #1347 LOAD 1 / FACRESET 1 runtime staging\n");
    printf("----------------------------------------------------------------\n");
    RUN(stages_after_accept_every_field_reaches_runtime);
    RUN(rejected_update_leaves_runtime_unchanged);
    RUN(mac_is_kept_not_nvms_and_not_the_zeroed_factory_one);
    RUN(the_loaded_struct_is_not_modified);
    RUN(no_argument_style_stage_is_idempotent);
    return TEST_SUMMARY();
}
