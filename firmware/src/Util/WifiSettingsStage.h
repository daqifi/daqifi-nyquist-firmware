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
 * ON THE MAC: the runtime copy's macAddr is NVM's value, or zero on a
 * factory-initialised board; the WINC's MAC goes to the LIVE copy only.
 * Nothing user-visible reads the runtime MAC (LAN:MAC? reads the live copy),
 * and APPLY keeps the live MAC. Keeping runtime's own MAC therefore only avoids
 * changing the bytes a later SAVE stores.
 *
 * No #includes on purpose: the includer must already have <stdbool.h>, <string.h>, the
 * taskENTER_CRITICAL/taskEXIT_CRITICAL pair and wifi_manager_settings_t. The
 * firmware gets them from wifi_manager.h; the host test defines stand-ins.
 *
 * ========================================================================== */
#ifndef WIFI_SETTINGS_STAGE_H
#define WIFI_SETTINGS_STAGE_H

/* WHAT THE CRITICAL SECTIONS DO AND DO NOT COVER. Every whole-struct access to
 * the runtime copy in this file is one critical section holding the memcpy and
 * nothing else (the pointers are looked up and the struct is built BEFORE it):
 * the SAVE and APPLY 1 snapshots, and the LOAD / FACRESET copies, with or
 * without the argument. Those are whole against each other. They are NOT whole
 * against the single setters (SSID, PASs, NETType, ...), which write the runtime
 * copy unprotected, so a snapshot can still catch a setter halfway, as on main.
 * APPLY's own copy of runtime into the LIVE copy (inside
 * wifi_manager_UpdateNetworkSettings) is unprotected too, as on main.
 * The includer provides taskENTER_CRITICAL / taskEXIT_CRITICAL. */

/* Copy `loaded` over `runtime`, keeping runtime's macAddr, in one critical
 * section. */
static inline void WifiSettingsStage_IntoRuntime(
        wifi_manager_settings_t *runtime,
        const wifi_manager_settings_t *loaded) {
    wifi_manager_settings_t local = *loaded;
    taskENTER_CRITICAL();
    local.macAddr = runtime->macAddr;
    memcpy(runtime, &local, sizeof(*runtime));
    taskEXIT_CRITICAL();
}

/* Whole-struct copy of the runtime copy into `dst` (SAVE, APPLY 1): the memcpy
 * and nothing else inside the critical section. */
static inline void WifiSettingsStage_Snapshot(
        wifi_manager_settings_t *dst,
        const wifi_manager_settings_t *runtime) {
    taskENTER_CRITICAL();
    memcpy(dst, runtime, sizeof(*dst));
    taskEXIT_CRITICAL();
}

/* The no-argument LOAD / FACRESET copy: the whole loaded struct over the runtime
 * copy, MAC included (unchanged behaviour), memcpy only inside the section. */
static inline void WifiSettingsStage_Overwrite(
        wifi_manager_settings_t *runtime,
        const wifi_manager_settings_t *loaded) {
    taskENTER_CRITICAL();
    memcpy(runtime, loaded, sizeof(*runtime));
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
