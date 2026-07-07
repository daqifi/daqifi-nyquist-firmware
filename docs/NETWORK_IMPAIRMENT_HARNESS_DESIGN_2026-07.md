# Network-Impairment Harness — Design Doc (2026-07)

**Ticket:** [#456 — test(wifi): network-impairment harness — packet loss, latency, AP kick, reboot simulation](https://github.com/daqifi/daqifi-nyquist-firmware/issues/456)
**Status:** Design / plan only. **No firmware change. No bench work in this ticket.** The harness itself is the follow-up implementation.
**Home of the eventual code:** `daqifi-python-test-suite` (this doc lives in the firmware repo `docs/` for now, per the ticket, and references the suite).

---

## 1. Purpose

We have verified the firmware's #397 self-heal + auto-stop path under the *clean* condition "transport disabled at boot / no consumer connected." The realistic field failure modes are messier and currently **uncharacterized**:

| Real-world condition | What it looks like on the wire |
|---|---|
| Brief WiFi blip (AP channel rebalance) | 1–3 s total loss of link, then recovery |
| Sustained outage (AP reboot, router FW update) | 30–90 s total loss, DHCP re-acquire on return |
| Lossy link | 5–30 % packet loss, +100–300 ms latency, jitter |
| AP-initiated kick | disassoc / deauth frame → association drops |
| DHCP lease loss | device keeps association but loses its IP |

The harness must **inject these reproducibly**, run a streaming workload across the impairment, and capture both **firmware-side** health (`SYST:STR:STATS?`, `STAT:QUES:COND?`, `SYST:LOG?`) and **host-side** truth (PC socket byte count / pcap) so we can state, per condition, whether the WINC1500 + our state machine survived without a wedge.

**Non-goal:** measuring peak throughput. That is the job of `test_overnight_wifi.py`. This harness measures **survival and recovery** under degraded links, at a deliberately sub-cap rate.

---

## 2. What already exists (survey)

### 2.1 Reusable test-suite primitives (`test_harness.py`)

The harness is already the right substrate — build the impairment runner from these, do **not** re-implement them (project rule: new utilities go *in* the harness).

| Primitive | Role in the impairment harness |
|---|---|
| `ReliableSCPI` (`.command`, `.query`, `.query_kv`, `.query_json`) | All control-plane SCPI over **USB** (out-of-band from the impaired WiFi path — critical, see §4). `query_json` parses `SYST:STR:STATS?` / `CONF:CAP:JSON?`. |
| `StreamingMeasurement` (`.start(freq, duration, is_csv, wait_for_serial=False)`) | Runs the streaming window; for WiFi pass `wait_for_serial=False` and take SPS from `TotalSamplesStreamed` over the window. |
| `FastReader` | Background drain for the USB control path so it never back-pressures. |
| `TcpDrainClient` (in `test_wifi_stress_benchmark.py`) | The PC-side WiFi data consumer — `bytes_received` is the **only** trustworthy WiFi throughput signal (firmware WiFi counters have lied historically, #371). Promote it into `test_harness.py` as part of this work. |
| `device_reset` / `device_recovery.py` (`reboot_device`, `read_idn`, `wifi_status`, `get_ip`) | Post-test recovery + the fresh-handle `SYST:REBoot` escape used when a host serial write wedges. `get_ip` returns the DHCP address to point `TcpDrainClient` at. |
| `build_result_row` / `write_results` / `collect_run_metadata` | Canonical CSV shape + the `.meta.json` version triplet (firmware `*IDN?` + fw version + suite SHA). Every impairment run must emit these so results are comparable night-to-night. |
| `stream_capture_rows`, `csv_values_are_integer` | Spot integrity checks on recovered CSV rows. |

### 2.2 Existing WiFi scripts to mirror (not fork)

- `test_overnight_wifi.py` — the canonical WiFi runner: TCP data via `TcpDrainClient` at `device:9760`, SCPI control over USB, `--host <device-ip>` (STA) or `--ap-mode` (Windows `IP_UNICAST_IF` pin via `tcp-drain-persistent.ps1`). The impairment runner should reuse its connection/settling/measurement scaffolding wholesale.
- `test_wifi_stress_benchmark.py` — source of `TcpDrainClient`.
- `test_overnight_characterization.py` — the USB/SD sibling that shares the channel-config + rate-sweep matrix (imported, so drift propagates).

### 2.3 iperf usage

iperf is **not** currently wired into the WiFi runners (only `test_capability_envelope.py` references it). Throughput truth today is `TcpDrainClient.bytes_received`, not iperf. For the impairment harness, `iperf3` is **optional** and used only as an *independent cross-check* of the injected condition (per-second loss/jitter under netem) — it does **not** replace the firmware streaming workload, which is what actually exercises the WINC + state machine.

### 2.4 Firmware behavior the harness asserts against (verified this session)

All verified against `firmware/src/services/streaming.c` and `SCPIInterface.c` in-tree:

- **Auto-stop grace** — `streaming.c`: `TRANSPORT_GRACE_DEFAULT_SEC 60`, min 5, max 300, in `gTransportGraceSec`. Once *all configured transports* are unhealthy continuously for `grace` seconds, streaming auto-stops and sets `QUES_BIT_TRANSPORT_DOWN (1<<12)`.
- **Grace is tunable** — `SYSTem:STReam:CONSumer:GRACe <5..300>` / `...:GRACe?` (`SCPIInterface.c:5219–5220`). The harness sets a **short grace (e.g. 10 s)** so auto-stop tests don't each take 60 s.
- **Startup-drop grace (#450)** — `SYSTem:STReam:LOSS:GRACe` (`:5221`) — pre-window drops fold into a separate counter; the harness re-clears stats after a 3 s transient before opening its verdict window (same discipline as `test_overnight_wifi.py`).
- **QUES condition bits** (CLAUDE.md table, set live during streaming, cleared at stop): bit 4 Data Loss (windowed ≥ threshold), bit 8 USB overflow, bit 9 WiFi overflow (512), bit 10 SD overflow, bit 11 Encoder fail, bit 12 Transport Down (4096, auto-stop fired).
- **Stats fields** the harness diffs pre/post: `WifiDroppedBytes`, `QueueDroppedSamples`, `TotalSamplesStreamed`, `TotalBytesStreamed`, `SampleLossPercent`, `ByteLossPercent`, `WindowLossPercent`.
- **Auto-stop LOG line**: `Streaming: all configured transports down >Ns — auto-stop` (retrieve via `SYST:LOG?`).
- **WiFi config surface** (verified `SCPIInterface.c:5098–5137`): `SYST:COMM:LAN:ENAbled`, `NETType`, `SECurity`, `SSID`, `PASs`, `APPLY`, `ADDRess?`, `BSSID?`, `SSIDStr?`. Recovery detection uses `ADDRess?` (non-zero IP) and `BSSID?`.

> **SCPI discipline:** every command above was grep-verified in `SCPIInterface.c` this session. The eventual runner must keep doing this — no guessed syntax.

---

## 3. Bench topology

```
   ┌─────────────┐   USB CDC (control plane, SCPI)   ┌──────────────┐
   │             │◄──────────────────────────────────►│              │
   │  DAQiFi     │                                     │   Test PC    │
   │  Nyquist    │   2.4 GHz WiFi (data plane, TCP)    │  (WSL + PS)  │
   │  (WINC1500) │◄─────────┐              ┌──────────►│  TcpDrain    │
   └─────────────┘          │              │           └──────┬───────┘
                            ▼              ▼                  │ SSH
                   ┌──────────────────────────────┐          │
                   │  OpenWrt impairment AP        │◄─────────┘
                   │  (GL.iNet GL-AR300M or sim.)  │
                   │  hostapd + tc/netem + iw      │
                   │  wired uplink to Test PC/LAN  │
                   └──────────────────────────────┘
                            ▲
                   ┌────────┴────────┐
                   │  Smart plug     │  (Kasa/Tapo, python-kasa/tinytuya)
                   │  AP mains power │  → genuine power-cycle outages
                   └─────────────────┘
```

**Design invariant — the control plane must not ride the impaired path.** SCPI runs over **USB CDC**, which is unaffected by WiFi netem/deauth/power-cycle. This is what lets the harness keep querying `SYST:STR:STATS?` / `STAT:QUES:COND?` *while WiFi is dead* to observe the firmware's reaction. (It also honors CLAUDE.md's quiescence rule differently: USB SCPI at priority 7 does preempt the data path, so the harness issues **one** stats query at defined checkpoints, not a poll loop — see §6.)

**AP choice.** The ticket's GL.iNet GL-AR300M (OpenWrt, ~$50) is the recommended cheapest path: `hostapd`, `hostapd_cli`, `tc`, `iw` all native, SSH-accessible from WSL. Any hostapd-based OpenWrt AP works. The impairment is applied on the AP's **radio interface** (`wlan0`) or its **bridge** so it hits the DAQiFi's traffic specifically.

---

## 4. Impairment catalogue

Each impairment defines: **injector** (reproducible command), **expected firmware behavior**, and **pass criteria**. All injections are issued over SSH to the AP (except device reboot, which is USB SCPI, and the power-cycle, which is the smart plug).

### 4.1 Packet loss (continuous)

- **Inject:** `ssh root@$AP "tc qdisc replace dev wlan0 root netem loss ${PCT}%"`
  Remove: `ssh root@$AP "tc qdisc del dev wlan0 root"`
- **Profiles:** 5 %, 15 %, 30 %.
- **Expected firmware behavior:** streaming continues; TCP retransmits absorb moderate loss. `WifiDroppedBytes` may rise if the WiFi circular buffer (14 KB) backs up under retransmit stalls; QUES bit 9 (WiFi overflow) may latch under 30 %. **No wedge; no auto-stop** as long as *some* forward progress keeps the transport "healthy" within grace.
- **Pass criteria:**
  - Device stays responsive on USB SCPI throughout (`*IDN?` returns) — **no HIF/WINC wedge**.
  - At 5 %: `TcpDrainClient.bytes_received > 0` and monotonically increasing; run completes; QUES Transport-Down (bit 12) **not** set.
  - At 30 %: either (a) survives with elevated `WifiDroppedBytes`/bit 9, **or** (b) cleanly auto-stops with bit 12 + the LOG line if forward progress fully stalls past grace — **both are PASS**; a silent wedge (USB dies / stats frozen / device needs reflash) is **FAIL**.
  - After impairment removal: a fresh streaming session runs clean (self-heal confirmed).

### 4.2 Added latency + jitter

- **Inject:** `tc qdisc replace dev wlan0 root netem delay 200ms 50ms distribution normal`
  Combined with loss: `... netem delay 150ms 40ms loss 5%`.
- **Expected firmware behavior:** higher round-trip inflates the TCP window drain time; the WiFi ring absorbs bursts. Throughput drops; no error state unless the ring overflows (bit 9).
- **Pass criteria:** device responsive; run completes; loss stays within the windowed threshold (bit 4 may set transiently but must clear at stop); no wedge.

### 4.3 Brief blip — 100 % loss for 5 s (self-heal expected)

- **Inject:** `tc qdisc replace dev wlan0 root netem loss 100%`; sleep 5 s; `tc qdisc del dev wlan0 root`.
- **Expected firmware behavior:** transport goes unhealthy for 5 s — **less than grace** — so streaming must **not** auto-stop. On recovery the pipeline resumes. Some `WifiDroppedBytes` during the blackout is acceptable (ring overflow).
- **Pass criteria:**
  - Streaming still `Measuring` (OPER bit 4) after the blip; QUES bit 12 **not** set.
  - `TotalSamplesStreamed` continues advancing after recovery.
  - `TcpDrainClient` resumes receiving bytes after the blip without a reconnect from the PC side (TCP session survived) **or** with an automatic reconnect if the socket was reset — document which.

### 4.4 Sustained outage — 100 % loss for (grace + margin) (auto-stop expected)

- **Inject:** set short grace first: `SYST:STR:CONS:GRAC 10` (USB); then `netem loss 100%` for ~15 s; remove.
- **Expected firmware behavior:** all configured transports unhealthy > grace → **auto-stop**, `QUES_BIT_TRANSPORT_DOWN (bit 12)` set, LOG line `Streaming: all configured transports down >Ns — auto-stop`.
- **Pass criteria:**
  - Poll `STAT:QUES:COND?` at grace+margin → bit 12 (4096) present.
  - `SYST:LOG?` contains the auto-stop line.
  - OPER bit 4 (Measuring) cleared (streaming actually stopped).
  - A **fresh** `SYST:STR:START` after impairment removal succeeds and streams clean — bit 12 cleared on next start (self-heal).
  - No wedge: USB SCPI responsive the whole time.

### 4.5 AP-initiated kick (disassoc / deauth)

- **Inject:** `ssh root@$AP "hostapd_cli disassociate $DEV_MAC"` (soft) and `hostapd_cli deauthenticate $DEV_MAC` (hard). `$DEV_MAC` from `SYST:COMM:LAN:BSSID?`/the AP's station dump (`hostapd_cli all_sta`).
- **Expected firmware behavior:** WINC loses association → our WiFi state machine should re-associate + re-DHCP without host intervention. The TCP data socket dies; the PC `TcpDrainClient` must reconnect after the device re-acquires an IP. This is the path most likely to expose a WINC HIF re-entrancy / listener wedge (memory: #560/#475 listener wedge, #517 stale-state).
- **Pass criteria:**
  - Within a bounded window (e.g. 30 s) `SYST:COMM:LAN:ADDR?` returns a non-zero IP again (re-associated + re-DHCP).
  - `SYST:COMM:LAN:BSSID?` reports the AP BSSID (association real, not stale-cached — cross-check against #517).
  - A new `TcpDrainClient.connect()` to the (possibly new) IP succeeds and receives streaming bytes.
  - No wedge; if streaming was still within grace it either survived (socket-level reconnect) or auto-stopped cleanly — document observed behavior and file a ticket if reconnect never completes.

### 4.6 DHCP lease loss

- **Inject:** shorten the AP's DHCP lease (`option lease` in dnsmasq/udhcpd) and drop the lease server briefly, or `hostapd_cli` disassoc timed with a lease expiry, forcing a renew that fails then succeeds.
- **Expected firmware behavior:** device keeps association but IP goes 0.0.0.0 until renew; on renew, `ADDR?` returns a (possibly new) address.
- **Pass criteria:** `ADDR?` recovers to non-zero within a bounded window; streaming resumes to the new IP; no wedge. (Lower priority — implement in Phase 3.)

### 4.7 Full AP power-cycle (real outage)

- **Inject:** `python -m kasa --host $PLUG_IP off` (or `tinytuya`), sleep 60 s, `... on`; wait for AP to boot + DAQiFi to re-associate.
- **Expected firmware behavior:** the hardest case — link vanishes, AP MAC/BSSID may change on reboot, DHCP re-acquired from a fresh server. Combines 4.4 (auto-stop past grace) + 4.5 (re-associate) + 4.6 (re-DHCP). This is the acceptance test that #455 (store-and-forward durable tier) will eventually validate "zero data loss across a 60 s AP power-cycle" against.
- **Pass criteria:**
  - During outage: streaming auto-stops (bit 12) past grace; USB SCPI stays alive.
  - After AP returns: device re-associates (`BSSID?` real, `ADDR?` non-zero) within ~60 s of AP boot; a fresh streaming session runs clean.
  - Device never needs a reflash or physical power-cycle to recover — **that would be FAIL** and an immediate ticket.

---

## 5. Wedge / FAIL taxonomy (what "survived" means)

A **PASS** requires the device to reach a clean, controllable state by USB SCPI without human intervention. Distinguish these failure signatures explicitly in results (mirror the debugging-discipline V/E tagging):

| Signature | Detection | Severity |
|---|---|---|
| Silent WINC HIF wedge | USB `*IDN?` OK but `ADDR?`/`BSSID?` frozen/stale forever; no re-associate | FAIL — file ticket (class #560/#517) |
| CDC-dead wedge | USB SCPI stops responding entirely; needs `SYST:REBoot`/reflash | FAIL — file ticket (class #525/#552) |
| Stats-frozen | `TotalSamplesStreamed` stuck while OPER says Measuring | FAIL |
| Graceful auto-stop | bit 12 + LOG line + OPER Measuring cleared + fresh start works | PASS |
| Self-heal | blip < grace, streaming continues, samples advance | PASS |

The harness's recovery step is `device_recovery.recover` (reboot → idn → wifi). If recovery is *needed* to continue the matrix, the just-finished cell is FAIL regardless of what the counters said.

---

## 6. Runner design

`test_456_impairment.py` (follow-up; not written in this ticket) — a recipe runner in the `test_overnight_wifi.py` style:

```
CLI: --host <device-ip> | --ap-mode   (data path, reuse test_overnight_wifi scaffolding)
     --ap-ssh root@<ap-ip>            (impairment injector target)
     --plug <kasa-ip>                 (optional, power-cycle tests)
     --profiles loss5,loss30,blip5s,outage,deauth,powercycle
     --port <usb-serial>              (control plane; do NOT hardcode COM)
     --grace 10                       (short grace for auto-stop cells)
     --rate <sub-cap Hz>              (deliberately below the WiFi cap)

Per profile:
  1. device_recovery.recover(scpi)                 # known-good start
  2. configure WiFi (ENA/NETType/SEC/SSID/PASs/APPLY), wait settle, ADDR?
  3. TcpDrainClient.connect(ip); start_drain()
  4. SYST:STR:CONS:GRAC <grace>; SYST:STR:STATS:CLE
  5. StreamingMeasurement.start(rate, dur, wait_for_serial=False)
  6. t0+3s: re-clear stats (transient grace, #450)
  7. inject impairment (SSH tc/hostapd_cli / plug)
  8. at defined checkpoints ONLY: single-shot SYST:STR:STATS? + STAT:QUES:COND?
     (NOT a poll loop — quiescence rule; one query per checkpoint)
  9. remove impairment
  10. capture: STATS?, QUES:COND?, LOG?, TcpDrain.bytes_received, iperf3 (optional)
  11. verify recovery: fresh stream runs clean
  12. build_result_row(...) → CSV + .meta.json (collect_run_metadata)
```

**Injection helpers** live in a new `impairment.py` module in the suite (SSH via `subprocess`/`paramiko`; smart-plug via `python-kasa`), imported by the runner — same "utilities go in the harness" rule.

**Metrics captured per cell** (into the canonical CSV shape, extra columns): `profile`, `injected_pct/delay/duration`, `wedge_signature`, `ques_bits_hex`, `wifi_dropped_bytes`, `queue_dropped`, `pc_bytes_received`, `reassoc_seconds`, `autostop_fired`, `recovered_without_reboot`.

---

## 7. Phased implementation plan

**Phase 0 — bench provisioning (hardware, one-time).**
Acquire/repurpose an OpenWrt AP (GL-AR300M or similar). Bring up `hostapd` 2.4 GHz SSID the DAQiFi associates to; confirm `tc`, `hostapd_cli`, `iw` present; enable SSH key auth from WSL. Optional: Kasa/Tapo smart plug on the AP mains. **Deliverable:** documented AP setup (SSID/creds in `~/.daqifi.env`, never committed) + a `impairment.py` connectivity smoke test (`tc qdisc show`, `hostapd_cli status` over SSH).

**Phase 1 — netem loss/latency (software-only impairments).**
Implement `impairment.py` (SSH tc netem apply/remove) + the runner profiles 4.1–4.4 (loss 5/15/30 %, latency, 5 s blip, sustained-outage auto-stop with short grace). These need no AP-side association control and are the fastest to prove out. **Deliverable:** `test_456_impairment.py` runs the 4 profiles, emits CSV + meta; results table added here and to CLAUDE.md (like the throughput tables).

**Phase 2 — association control (AP kick).**
Add `hostapd_cli disassociate/deauthenticate` injectors + profile 4.5. Requires resolving `$DEV_MAC` (from `all_sta` / `BSSID?`). This is the phase most likely to surface a WINC wedge — budget for filing follow-up tickets. **Deliverable:** deauth profile characterized; re-associate timing recorded.

**Phase 3 — full-outage + DHCP (hardware impairments).**
Add smart-plug power-cycle (4.7) + DHCP-lease-loss (4.6). These are the store-and-forward (#455) acceptance gate. **Deliverable:** power-cycle profile characterized; documented as the #455 validation recipe.

**Phase 4 — CI/overnight integration.**
Fold the impairment matrix into `nightly_regression.py` as an optional leg (gated on AP presence), so degraded-link survival is checked continuously alongside the freeze-aware at-cap soaks. **Deliverable:** nightly leg + trend CSV in `benchmarks/456_impairment/`.

---

## 8. Acceptance criteria mapping (from #456)

| Ticket criterion | Covered by |
|---|---|
| OpenWrt AP with `tc netem` + `hostapd_cli` over SSH | Phase 0 |
| Python harness: apply named profile → run stream → capture fw+host metrics → remove | §6 runner + Phases 1–3 |
| Test matrix run on main; results in CLAUDE.md | Phase 1/2/3 deliverables |
| ≥3 profiles characterized (5 % loss, 5 s blip, AP power-cycle 60 s) | 4.1, 4.3, 4.7 |
| Failure modes → follow-up tickets | §5 taxonomy; Phase 2/3 note |

---

## 9. Risks & open questions

- **WSL routing / AP-mode socket pinning** — the same `IP_UNICAST_IF` issue `test_overnight_wifi.py` solved with `tcp-drain-persistent.ps1` applies here in AP-mode. STA-mode against the OpenWrt AP avoids it (recommended for the impairment bench).
- **`tc` on the radio vs the bridge** — impairing `wlan0` hits all associated clients; if other devices share the AP, use a per-station `tc filter` or a dedicated impairment SSID. Document the exact interface in Phase 0.
- **Grace vs test duration** — auto-stop cells use a short grace (10 s) via `CONS:GRAC`; restore the 60 s default in the recovery step so subsequent cells aren't contaminated.
- **Bench contention** — this harness needs a *dedicated* impairment AP; it must not run against the shared production AP (would disrupt overnight jobs). Flag in the runner (refuse to inject against a non-impairment SSID).
- **WINC re-associate reliability** — memory flags #560/#475 listener wedge and #517 stale-state as prior WiFi-recovery hazards; profile 4.5 is the direct probe. Expect to file at least one firmware ticket out of Phase 2.

---

## 10. References

- Firmware: `firmware/src/services/streaming.c` (grace/auto-stop, QUES bits), `firmware/src/services/SCPI/SCPIInterface.c` (LAN + stream SCPI, verified line refs in §2.4).
- Suite: `test_harness.py` (primitives), `test_overnight_wifi.py` (WiFi runner scaffolding), `test_wifi_stress_benchmark.py` (`TcpDrainClient`), `device_recovery.py` (recovery), `nightly_regression.py` (CI integration target).
- Related tickets: #397 (self-heal/auto-stop — the mechanism this validates), #455 (store-and-forward — future acceptance consumer), #398 (GTK rekey starvation — reproducible via netem bw-cap + rekey timing), #560/#475/#517 (prior WiFi-recovery wedges this harness is designed to catch).
- CLAUDE.md sections: "SCPI Status Registers (OPER & QUES)", "Streaming Frequency Capping", "Talking SCPI to the device", "How we test (policy)".
