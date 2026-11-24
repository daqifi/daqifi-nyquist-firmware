/* ==========================================================================
 * WifiSettingsStage.h -- the copy rule behind LAN:LOAD 1 / LAN:FACRESET 1
 * (#1347).
 *
 * There are two copies of the WiFi settings: the RUNTIME copy (what the LAN:
 * setters write, the getters read, LAN:APPLY passes on and LAN:SAVE writes to
 * NVM) and the LIVE copy (g_BoardData.wifiSettings, what the WiFi state
 * machine uses). `LOAD 1` / `FACRESET 1` hand a freshly loaded struct to
 * wifi_manager_UpdateNetworkSettings(), which writes the LIVE copy and only
 * propagates powerSaveDisabled into the runtime copy -- so the getters, a
 * later APPLY and a later SAVE kept the pre-reset values.
 *
 * The rule, kept here so a host test can compile the SAME text the firmware
 * does (the handlers themselves are not host-includable):
 *
 *   1. Stage into the runtime copy ONLY after the update returned true. A
 *      rejected call (APPLY gate engaged) or a rolled-back one (REINIT could
 *      not be queued) leaves the runtime copy exactly as it was.
 *   2. The source is the CALLER's own struct, never the live copy: the WiFi
 *      task can rewrite the live ssid/hostName (ApplyDefaultSuffixAndDeviceName)
 *      the moment REINIT is queued, so a read of live could be torn or
 *      half-suffixed. The caller's stack struct has no other writer.
 *   3. The runtime copy's own macAddr is kept. The loaded struct carries NVM's
 *      MAC or the factory default's zeroed one; neither is the board's MAC.
 *
 * No #includes on purpose: the includer must already have <stdbool.h>,
 * the taskENTER_CRITICAL pair and wifi_manager_settings_t (the firmware gets it from wifi_manager.h; the host
 * test defines a minimal stand-in with the same macAddr member).
 * ========================================================================== */
#ifndef WIFI_SETTINGS_STAGE_H
#define WIFI_SETTINGS_STAGE_H

/* Copy `loaded` over `runtime`, keeping runtime's macAddr. The copy is one
 * critical section (taskENTER_CRITICAL/taskEXIT_CRITICAL, which the includer
 * must provide) so the other SCPI task (USB, pri 7, can preempt a TCP-SCPI
 * handler) never sees a half-written runtime copy mid-memcpy. */
static inline void WifiSettingsStage_IntoRuntime(
        wifi_manager_settings_t *runtime,
        const wifi_manager_settings_t *loaded) {
    wifi_manager_settings_t local = *loaded;
    taskENTER_CRITICAL();
    local.macAddr = runtime->macAddr;
    *runtime = local;
    taskEXIT_CRITICAL();
}

/* Apply `loaded` through `update` and stage it into the runtime copy only if
 * update accepted it. Returns update's result. */
static inline bool WifiSettingsStage_ApplyThenStage(
        wifi_manager_settings_t *runtime,
        const wifi_manager_settings_t *loaded,
        bool (*update)(wifi_manager_settings_t *)) {
    /* update() takes a non-const pointer; it only reads from it. */
    if (!update((wifi_manager_settings_t *) loaded)) {
        return false;
    }
    WifiSettingsStage_IntoRuntime(runtime, loaded);
    return true;
}

#endif /* WIFI_SETTINGS_STAGE_H */
