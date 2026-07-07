# microrl Alternatives Evaluation (2026-07)

**Ticket:** [#43 — Evaluate alternatives to microrl for command line processing](https://github.com/daqifi/daqifi-nyquist-firmware/issues/43)
**Scope:** Desk evaluation only — no firmware change, no build, no bench. Survey what microrl gives us today, its limits, and 2–4 candidate replacements, then make a justified recommendation.
**Verdict (TL;DR):** **Keep microrl.** No candidate delivers a feature or resource win large enough to justify the integration risk of replacing a line editor that is wired into three transports (USB CDC, TCP, and the SCPI echo/transparent-mode paths) and already carries load-bearing DAQiFi modifications. Details and the exit criteria that *would* change this recommendation are below.

---

## 1. What microrl does for us today

microrl ("micro read line", v1.5.1, Apache-2.0, Eugene Samoylov) is a small VT100-style line editor. In this firmware it is **not** used as a shell or a command parser — libscpi does all command tokenization and dispatch. microrl's job is strictly the *terminal line-editing front end*: it accumulates keystrokes into a line buffer, handles editing keys, maintains history, echoes, and on Enter hands the **complete line** to a callback that forwards it to libscpi.

Evidence for the "line editor only, not parser" split (config and integration, verified in-tree):

- `libraries/microrl/src/config.h:101` — `USE_SPLIT` is **commented out**. The in-tree comment at that line records the reason: *"Added the ability to disable the command line splitting … This allows us to use microrl with libscpi, which contains its own parsing."* microrl therefore does **not** tokenize into `argc/argv`; it passes the raw line through.
- `libraries/microrl/src/config.h:42` — `_USE_COMPLETE` is **commented out**. Tab-completion is not compiled in and no `microrl_set_complete_callback()` call exists anywhere in `services/` (grep-confirmed). Completion is a non-feature for us today.
- `_USE_HISTORY` **is** defined, `_RING_HISTORY_LEN 64` — a 64-byte history ring per console.
- `_USE_ESC_SEQ` **is** defined — arrow keys, HOME, END, plus the Ctrl-A/B/E/F/H/K/U/P/N hotkeys.
- `_COMMAND_LINE_LEN (1+512)` — a 513-byte line buffer per console (the dominant RAM cost, see §3).

### Integration surface (what a replacement would have to re-satisfy)

| Consumer | File | What it wires |
|---|---|---|
| USB CDC console | `services/UsbCdc/UsbCdc.c` | `microrl_init` + `microrl_set_echo(true)` + `microrl_set_execute_callback(microrl_commandComplete)`; feeds bytes via `microrl_insert_char` in the RX loop (UsbCdc.c:817, 866–869); `microrl_echo` print callback writes back to the CDC TX buffer. The `microrl_t console` is embedded in the coherent USB settings struct (`UsbCdc.h:98`). |
| WiFi TCP console | `services/wifi_services/wifi_tcp_server.c` | Same three-call init at wifi_tcp_server.c:342–344 but with `set_echo(false)` (TCP client does its own echo); feeds bytes at wifi_tcp_server.c:581. `microrl_t console` embedded in `wifi_tcp_server_clientContext_t` (`wifi_tcp_server.h:58`). `WIFI_MAX_CLIENT` is 1 → one TCP console instance. |
| SCPI echo control | `services/SCPI/SCPIInterface.c` | `SCPI_GetMicroRLClient()` (SCPIInterface.c:284) resolves the per-transport console so echo-control commands can call `microrl_set_echo()` (SCPIInterface.c:3789) on the correct client. This is the coupling that makes echo a *per-connection* property. |
| USB transparent / raw mode | `services/UsbCdc/UsbCdc.c:721, 861` | Transparent SCPI mode deliberately **bypasses** microrl; normal mode routes through it. The RX loop synthesizes canonical `\n\r` terminators into microrl (UsbCdc.c:868–869) so `\r`, `\n`, `\r\n`, `\n\r` all collapse to one line without double-submits. |

### DAQiFi-local modifications already carried on top of upstream

microrl in-tree is **not pristine upstream** — it has been patched, and those patches are load-bearing:

- **Insert-mode / mid-line cursor editing** (ticket #48). Upstream 1.5.1 is essentially append-at-end. The in-tree `microrl.c` adds `dataCursor` (index within the line, distinct from `cmdlen`) and `terminalPos`, with `memmove`-based mid-line insert/delete (microrl.c:296–369) so the user can arrow back into a line and edit in place. This is a genuine behavioral upgrade over stock microrl.
- **Per-client echo tri-state** (`echoOn > -1` guard, microrl.c:290–301, marked `// DAQiFi modification`) so a console can suppress echo entirely (password-style / TCP-managed echo) without losing the redraw logic.
- **Uniform line-ending handling** (`lastChar` tracking, documented in `microrl.h`) to fold `\r\n`/`\n\r` into a single submit.

These are the hidden cost of any migration: a replacement doesn't just have to match stock microrl, it has to match *our modified* microrl, and re-earn the #48 insert-mode behavior and the echo tri-state.

---

## 2. Concerns raised in the ticket, assessed against the actual usage

The ticket lists Age, Security, Features, and Size. Re-scoped to how microrl is *actually* used here:

- **Security / "input validation".** microrl is a line editor, not a parser — it does not interpret command semantics, so "input filtering" mostly lives (correctly) at the libscpi and SCPI-callback layer, not here. The one real memory-safety surface microrl owns is its fixed 513-byte line buffer, and it already bounds writes to it (`_COMMAND_LINE_LEN`; overflow chars are dropped, per config.h). There is no dynamic allocation in the microrl path. A replacement would need to preserve that fixed-buffer, no-malloc property — several candidates below **regress** it.
- **Age / maintenance.** True that upstream microrl 1.5.1 is old and lightly maintained. But we have already forked it de-facto (§1 modifications), so "upstream activity" buys us little — we own this code regardless. A dead-but-tiny-and-working dependency we've already patched is lower risk than a live dependency we'd have to newly integrate.
- **Features.** The commonly-cited microrl gaps (no tab-completion, no built-in command table, no per-command help) are gaps we have **deliberately not wired up** — libscpi owns command structure. Adding a shell that provides these would duplicate/fight libscpi, not complement it.
- **Size.** Real, but small in absolute terms (§3), and the dominant term is the line-buffer length we chose (513 B), which is a config knob orthogonal to library choice.

---

## 3. Resource footprint (the RAM-tight constraint)

Firmware RAM headroom is ~500 B above the 8192 min-stack floor; a +876 B BSS change already broke the link once (ticket #391). So the RAM question dominates.

**microrl RAM, as configured:** the cost is `sizeof(microrl_t)` × (number of live consoles). Per instance (from `microrl.h`):

| Field group | Bytes (approx) |
|---|---|
| `cmdline[513]` (line buffer) | 513 |
| `ring_hist` = `ring_buf[64]` + 3× int | ~76 |
| escape/tmp/lastChar chars | ~4 |
| `prompt_str` ptr + `prompt_size` + `terminalPos` + `cmdlen` + `dataCursor` + `echoOn` | ~24 |
| `execute` / `get_completion` / `print` callback ptrs | 12 |
| **Per-instance total** | **~630 B** |

Live instances: **2** — one USB console (in the coherent USB settings struct) + one TCP console (`WIFI_MAX_CLIENT == 1`). Total ≈ **1.25 KB of static RAM**, ~82 % of which is the two 513-byte line buffers. Flash for `microrl.c` is ~21 KB of source → a few KB of `.text` (history + esc-seq + insert-mode paths).

Key consequence: **the RAM is dominated by the line-buffer size we chose, not by the library.** Any replacement carrying the same 512-byte line capacity for two consoles lands at roughly the same static footprint. The only way to meaningfully *cut* RAM is to shrink the line buffer or share one buffer across consoles — both are microrl config/refactor changes that need **no** library swap.

---

## 4. Candidate alternatives

Four candidates, chosen to span the design space (drop-in editor → full shell → in-house). Footprint figures for third-party libs are order-of-magnitude from upstream docs/source (**X** = external, un-reverified on PIC32); portability/integration are inferences (**I**) from their design.

### 4a. linenoise (antirez / Redis)
- **What it is:** A compact readline replacement (~1 file, BSD-2). Line editing, history, optional completion/hints.
- **Footprint (X):** Small `.text`, but the canonical build path is **blocking + malloc-based** (`linenoise()` returns a `malloc`'d string; history is a `malloc`'d `char**`). BSD-2 licensed — fine.
- **Bare-metal fit (I):** Poor without surgery. It assumes a blocking `read()`/`write()` fd model and a terminal in raw mode it controls. Our path is the opposite: **byte-at-a-time, callback-fed, non-blocking**, driven from RX interrupts/tasks across two transports. There is a "no-tty / multiplexed" API in newer forks, but adopting it means we'd re-add exactly the byte-fed state machine microrl already gives us. The malloc dependency also fights our zero-malloc console path (§2) and our tight-RAM/no-heap-growth posture (heap can grow at most ~+1.9 KB per project memory).
- **Verdict:** Net negative. Trades a working non-blocking, no-malloc editor for a blocking, malloc-based one we'd have to un-blocking-ify.

### 4b. embedded-cli (funbiscuit)
- **What it is:** Purpose-built embedded CLI (MIT). Static-buffer option (no malloc), byte-fed API (`embeddedCliReceiveChar`), built-in command table, tokenization, history, auto-complete.
- **Footprint (X):** Configurable; a static build with a modest buffer + small history is in the low-single-KB RAM range — **comparable to our two microrl consoles**, not dramatically smaller. MIT is fine.
- **Bare-metal fit (I):** Good — this is the closest architectural match (byte-fed, static-buffer, embedded-first). It's the only candidate that would be a genuine *drop-in-shaped* replacement.
- **The catch (I):** Its value-add is the parts we don't use — built-in **command table + tokenization + completion**. With libscpi owning parsing, we'd disable that half and use it purely as a line editor, i.e. re-implement the exact microrl role. And we'd **lose the #48 insert-mode + echo-tri-state modifications** and have to re-earn them on embedded-cli's editing model. Same footprint, real migration risk, no new capability we'd actually turn on.
- **Verdict:** The strongest candidate on paper, but a lateral move. Only worth it if we decide we want its command-table/completion features *instead of* libscpi's — which is a much larger architectural decision than "replace the line editor."

### 4c. letter-shell / microshell (full embedded shells)
- **What they are:** Complete interactive shells — command registration macros, per-command help, permission levels, completion, some with thread-safety hooks (letter-shell: MIT; microshell: MIT/BSD-ish).
- **Footprint (X):** Largest of the set — the command table, help strings, and shell state push both flash and RAM up. RAM per instance exceeds microrl once you add the command registry.
- **Bare-metal fit (I):** They run on FreeRTOS/PIC32-class parts, but they are **shells**, i.e. they want to *own* the command surface. That directly duplicates and conflicts with libscpi (SCPI is our command surface, and clients — daqifi-core, python-core, java-api — speak SCPI, not a shell grammar). Adopting a shell here means either running it hollow (all the cost, none of the shell features) or a disruptive re-plumb of command dispatch.
- **Verdict:** Wrong layer. These solve "I need a command shell," which libscpi already solves. Rejected.

### 4d. Hand-rolled minimal line editor
- **What it is:** A purpose-built byte-fed editor tailored to SCPI: fixed line buffer, Enter→libscpi, backspace, optional history, optional arrow/insert.
- **Footprint (I):** Potentially the smallest — we could drop history and esc-seq handling if the field truly doesn't need them, cutting the 64-byte ring and the esc-seq `.text`. But if we keep insert-mode + history (which #48 shows we wanted), we converge back onto ~what microrl already is.
- **Bare-metal fit:** By construction perfect — it's ours.
- **The catch (E/I):** We would be re-writing microrl-with-#48-mods from scratch, including the escape-sequence state machine, the `\r\n`/`\n\r` folding, per-client echo, and mid-line `memmove` editing — every one a place we'd re-introduce the bugs microrl has already had shaken out. This is maintenance cost with no capability upside unless the goal is specifically **RAM reduction**, in which case shrinking microrl's line buffer / sharing buffers (§5) gets most of the win at a fraction of the risk.
- **Verdict:** Only justified if a hard RAM shortfall forces it *and* the config-level RAM levers in §5 prove insufficient.

---

## 5. Comparison table

| Criterion | **microrl (current)** | linenoise | embedded-cli | letter/microshell | Hand-rolled |
|---|---|---|---|---|---|
| Static RAM / console | ~630 B (513 B is the line buf) | small struct **+ malloc** per line/history | ~comparable (static-buf build) | **larger** (+ command registry) | ≤ microrl (tunable) |
| malloc-free | **Yes** | No (canonical build) | Yes (static build) | Usually yes | Yes |
| Flash (.text) | few KB | small | low-single KB | largest | smallest |
| Byte-fed / non-blocking API | **Yes** (fits our RX loop) | No (blocking fd model) | Yes | Yes | Yes |
| Line editing (arrows/HOME/END) | Yes | Yes | Yes | Yes | If we build it |
| **Insert / mid-line edit (#48)** | **Yes (in-tree mod)** | Yes | Yes (own model) | Yes | Re-implement |
| History | Yes (64 B ring) | Yes (malloc) | Yes | Yes | Optional |
| Tab-completion | Off (unused) | Optional | Built-in | Built-in | N/A |
| Command parsing | **libscpi (kept)** | none | built-in (would fight libscpi) | built-in (fights libscpi) | libscpi |
| Per-client echo tri-state | **Yes (in-tree mod)** | manual | manual | manual | build it |
| PIC32/FreeRTOS proven | **Yes — shipping** | needs de-block | plausible (I) | plausible (I) | Yes |
| License | Apache-2.0 | BSD-2 | MIT | MIT/BSD | n/a |
| Integration effort vs today | **0 (incumbent)** | High (re-arch to non-blocking + no-malloc) | Medium (re-earn #48 + echo mods) | High (re-plumb dispatch) | High (rebuild + re-debug) |
| Net new capability we'd use | — | none | none (we'd disable its shell half) | none (dup of libscpi) | none |

**RAM levers that need NO library swap** (the real answer to "microrl is too big"):
1. Reduce `_COMMAND_LINE_LEN` from 513 to the true max SCPI line length (SCPI commands are short; 128–256 B is plausible) → saves ~256–385 B **per console × 2**.
2. Reduce/disable `_RING_HISTORY_LEN` (64 B) if field history is not needed on the constrained transport.
3. Share one line buffer across the two consoles (they are never edited concurrently at the byte level in the same task).

Any of these recovers meaningful RAM at essentially zero risk, which no library migration can claim.

---

## 6. Recommendation

**Keep microrl.** Justification, ranked:

1. **No candidate delivers new capability we would actually enable.** libscpi owns command parsing; the shell-class candidates (embedded-cli's command table, letter/microshell) would duplicate or fight it. The editor-class candidate that fits (embedded-cli in static mode) is a *lateral* move — same footprint, and we'd have to re-earn the #48 insert-mode and echo-tri-state modifications on a new editing model.
2. **The stated concern (size) is a config problem, not a library problem.** ~82 % of microrl's RAM is the 513-byte line buffer we chose. §5's levers cut that with zero migration risk. Swapping libraries to save RAM while carrying the same line capacity saves nothing.
3. **We already own the code.** The in-tree microrl is forked (insert-mode, echo tri-state, line-ending folding). "Upstream is stale" is largely moot — we maintain it either way, and it is small, no-malloc, and shipping across three transports.
4. **Migration risk is concentrated exactly where bugs hurt.** The console feeds the SCPI control plane on USB *and* TCP; a regression in line assembly, echo, or terminator handling is a field-visible break for every client SDK. The reward for taking that risk here is ~nil.

**This recommendation flips only if one of these concrete needs appears:**
- **A hard RAM shortfall** the §5 config levers cannot close → then a **hand-rolled minimal editor** sized exactly to need (§4d), *not* a third-party lib, is the path (smallest footprint, no malloc, no upstream coupling).
- **A decision to replace libscpi's command surface with a shell grammar** (a much larger architectural change, and one clients would have to follow) → then **embedded-cli** becomes the natural fit, because at that point its command-table/completion half is a feature, not dead weight.
- **A security requirement for input sanitization at the line layer** that can't live in the SCPI callbacks → still cheaper to add a filter hook to our forked microrl than to migrate.

Absent one of those, replacing microrl is churn without payoff.

---

## 7. Suggested follow-up (optional, low-risk)

If the underlying motive behind #43 is RAM (most likely, given the tight-headroom history), the highest-value action is **not** a library swap but a **line-buffer/history right-sizing pass on microrl** (§5 lever 1–2), measured against the map. That is a small, reversible, bench-testable change that directly attacks the only real cost — and it keeps the shipping, already-modified editor in place. Track separately from #43 if pursued.

---

*Epistemic tags: **V** = verified in-tree (file:line cited); **X** = external/upstream, not re-verified on PIC32; **I** = inference from design; **E** = empirical. In-tree integration facts (§1) are **V**; third-party footprint/portability claims (§4) are **X/I** — a proof-of-concept build would be required to upgrade any of them to **E** before acting on a migration.*
