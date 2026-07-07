# exFAT Filesystem Support — Feasibility Assessment (2026-07)

**Ticket:** [#213 — feat(sd): add exFAT filesystem support for large SD cards](https://github.com/daqifi/daqifi-nyquist-firmware/issues/213)
**Status:** Feasibility only — **no code change in this document**. Enabling `FF_FS_EXFAT` is a follow-up, gated on the licensing call and a measured RAM/flash delta (see Recommendation).
**Scope:** Assess flipping `FF_FS_EXFAT` from `0` to `1` in the ChaN FatFs config the firmware already bundles.

Evidence is tagged per the repo's debugging-discipline convention: **V** = verified from primary source in-tree (file:line), **X** = external authority, **I** = inference.

---

## 1. What it enables

exFAT lifts the two FAT32 limits that shape our SD logging today:

| | FAT32 (current) | exFAT |
|---|---|---|
| Max single-file size | 4 GB − 1 byte | 2^64 − 1 (16 EB) |
| Practical partition size | ~32 GB (Windows won't format FAT32 larger without third-party tools) | 128 PB |
| Free-cluster allocation | FAT link-chain, 2 entry writes/cluster | allocation bitmap, 1 bit/cluster |

- The firmware bundles FatFs **R0.15 w/patch2** (V: `firmware/src/config/default/system/fs/fat_fs/file_system/ff.c:2` header). exFAT support is compiled in conditionally and is currently **off**: `#define FF_FS_EXFAT 0` (V: `ffconf.h:233`).
- The LFN prerequisite is already satisfied: `FF_USE_LFN 1` (V: `ffconf.h:115`). exFAT requires `FF_USE_LFN >= 1` (V: `ffconf.h:235`) and C99 (V: `ff.h:73` — `#error exFAT feature wants C99 or later`; we build XC32/GCC in gnu C mode, so this is satisfied — **I**).
- 64-bit LBA stays off: `FF_LBA64 0` (V: `ffconf.h:205`). At the fixed 512-byte sector size (V: `FF_MIN_SS == FF_MAX_SS == 512`, `ffconf.h`), 32-bit LBA addresses up to 2 TB — sufficient for the 64/128/256 GB cards the ticket targets (**I**). exFAT does **not** require `FF_LBA64` for these capacities.

---

## 2. Flash cost

- Ticket estimate: **+4–6 KB** code (~764 lines of `#if FF_FS_EXFAT` conditional in `ff.c`). `ff.c` is 7,082 lines total (V: `wc -l ff.c`).
- **Not measured here** — this is a doc-only assessment and the build touches no hardware. The follow-up MUST rebuild and diff the `.map` `kseg0_program_mem` usage before/after the flip to get the real number.
- Headroom is ample: flash is 2 MB, current image ~432 KB (~80 % free, per ticket). Flash is **not** the constraint. **RAM is** (§3).

---

## 3. RAM cost — the real constraint (verified, and it is in the #391 danger zone)

The firmware has **~500 B of static headroom** above the 8192-byte minimum stack; a **+876 B BSS** change already broke the link in ticket #391 (per repo CLAUDE.md). exFAT's static footprint must be measured against that ~500 B budget, not against the "2 MB flash / 512 KB RAM" headline.

**Verified static-BSS cost of the flip (FF_USE_LFN == 1, current config):**

```
firmware/src/config/default/system/fs/fat_fs/file_system/ff.c:520-524
  #if FF_USE_LFN == 1
  #if FF_FS_EXFAT
  static BYTE  DirBuf[MAXDIRB(FF_MAX_LFN)];   // exFAT dir-entry scratchpad
  #endif
  static WCHAR LfnBuf[FF_MAX_LFN + 1];        // LFN buffer (already present)
```

- `MAXDIRB(nc) = (nc + 44) / 15 * SZDIRE`, `SZDIRE = 32` (V: `ff.c:518`). For `FF_MAX_LFN = 255`: `(255+44)/15*32 = 19*32 = **608 bytes**`.
- **Critically, `DirBuf[608]` is guarded by `#if FF_FS_EXFAT`** (V: `ff.c:521-523`) — it does **not** exist today. Flipping the switch adds it to `.bss`. `LfnBuf` is already allocated regardless (no change).
- `FATFS` struct grows ~20 B (V: `ff.h:151-193` — `dirbuf` ptr, `cdc_scl/cdc_size/cdc_ofs`, `bitbase`). There is **one** static `FATFS` instance (V: `ffconf.h` `FF_VOLUMES 1`), so ~20 B of BSS.

**Verified static-BSS delta ≈ 608 + 20 ≈ ~628 bytes.**

That is **greater than the ~500 B headroom** and comparable to the +876 B that broke #391 (**I** — same class of failure). **Flipping `FF_FS_EXFAT 1` on the current config will very likely fail the link**, the same way #391 did. Per the task's standing rule, that is a **RAM-blocked** change, not something to flip and hope.

Additional (non-BSS) growth, for completeness:
- `FFOBJID` grows ~20 B (V: `ff.h:186-192` — `n_cont/n_frag/c_scl/c_size/c_ofs`). `FFOBJID` is embedded in every `FIL`/`DIR`, but those are caller/stack-allocated in this codebase, so this lands on stack, not persistent BSS (**I**). Each open file object is ~20 B larger — negligible against the SD task's 1024-word (4 KB) stack.
- Ticket's "+2–3 KB RAM" estimate is the right order of magnitude but conflates BSS and per-object growth; the **load-bearing number is the ~628 B of new static BSS**.

**Mitigations that move the exFAT working buffer off BSS** (the follow-up should evaluate one of these before enabling):
- `FF_USE_LFN = 3` (heap): `INIT_NAMBUF` does `ff_memalloc((FF_MAX_LFN+1)*2 + MAXDIRB(FF_MAX_LFN))` = 512 + 608 = 1120 B per FS operation, freed after (V: `ff.c:544-546`). Keeps BSS flat; costs ~1.1 KB transient heap per op (heap is ~13 KB free after boot per CLAUDE.md) and requires `ff_memalloc`/`ff_memfree` in `ffsystem.c`. Adds a `FR_NOT_ENOUGH_CORE` failure mode. **Preferred if the ~628 B BSS won't fit.**
- `FF_USE_LFN = 2` (stack): puts 1120 B on the caller's stack (V: `ff.c:531-533`) — risky on the 4 KB SD task; not recommended.
- Reduce `FF_MAX_LFN` (e.g. 128 → `MAXDIRB = (172)/15*32 = 11*32 = 352 B`) to shrink the exFAT scratchpad, at the cost of shorter max filenames. Marginal; doesn't fully close the gap.

---

## 4. Interaction with file-splitting

The automatic file-splitting feature exists **specifically to dodge the FAT32 4 GB file limit** (V: `sd_card_manager.h:21-26` — `SD_CARD_MANAGER_FAT32_MAX_FILE_SIZE 4294967295ULL`, safe max = 4 GB − 100 MB; split naming `experiment-0001.csv…`, up to 9999 files, V: `sd_card_manager.c:16`, `:1289`).

- On an **exFAT** volume the 4 GB ceiling is gone (16 EB), so exFAT **removes the *need* for splitting to avoid the FAT32 cap**. A single >4 GB logging file becomes possible.
- **Splitting should be retained, not removed.** It still serves file *manageability* (a 200 GB single CSV is hostile to download/analysis tooling) and the existing `download_sd_files.py` / `analyze_split_files.py` flow depends on it. Recommendation: keep splitting available; on exFAT it simply becomes optional (user picks a large or unlimited `MAXSize`).
- **Hard code coupling to fix in the follow-up:** `SCPI_StorageSDMaxSizeSet` **rejects any `MAXSize > 4294967295`** unconditionally (V: `SCPIStorageSD.c:731-736`, `FAT32_MAX_FILE_SIZE`). On an exFAT card a user still could not set a >4 GB split threshold — the validator's 4 GB clamp must become filesystem-aware (query mounted `fs_type`, allow the exFAT ceiling when `FS_EXFAT`). Without this, enabling exFAT delivers the big-partition benefit but **not** the big-single-file benefit.

---

## 5. Other code touch-points the follow-up must handle

1. **Mount validation** accepts only FAT16/FAT32 and rejects everything else as "Unsupported filesystem — reformat as FAT32" (V: `sd_card_manager.c:660` — `fs->fs_type == FS_FAT16 || fs->fs_type == FS_FAT32`). Must add `|| fs->fs_type == FS_EXFAT` (V: `ff.h:420` `FS_EXFAT 4`). This is the behavior #178 introduced (see below) and #213 explicitly replaces.
2. **Format** currently uses `SYS_FS_FORMAT_ANY` → FAT16/FAT32 auto-select (V: `sd_card_manager.c:1568-1572`). To format exFAT, pass `SYS_FS_FORMAT_EXFAT` (V: `sys_fs.h:248` `0x04`; `ff.h:412` `FM_EXFAT`). Ticket proposes a `SYST:STOR:SD:FORmat exFAT` parameter — per the repo's "keep the SCPI surface lean" rule, prefer **extending the existing `SYST:STOR:SD:FORmat`** with an optional filesystem-type argument over adding a new command (V: current `FORmat` takes no arg, `SCPIStorageSD.c:659`).
3. The format sector-count *estimator* (`cst32` FAT32 heuristic, V: `sd_card_manager.c:1544-1556`) is FAT32-specific and would report a bogus progress denominator for exFAT — cosmetic, but worth a guard.

---

## 6. Licensing caveat — **X-class, and it is the gating item**

> **The in-tree `ffconf.h` comment does NOT contain a Microsoft patent note.** (V: `ffconf.h:234-236` — the only caveat it states is *"Note that enabling exFAT discards ANSI C (C89) compatibility."*) The ChaN FatFs source license header is a 1-clause BSD-style permissive notice with **no patent grant and no exFAT mention** (V: `ff.c:5-17`). The patent question is therefore **not** answered anywhere in the tree — it must be assessed externally.

- **X:** exFAT is a **Microsoft-patented filesystem**. Microsoft holds multiple exFAT patents (e.g. US 8,321,439 and related). In **August 2019** Microsoft published the exFAT specification and added exFAT to the **Open Invention Network (OIN)** patent-nonaggression pool — but **OIN coverage is scoped to the "Linux System" definition for OIN licensees**, i.e. the in-kernel Linux exFAT driver. It does **not** grant a blanket royalty-free right to ship exFAT in arbitrary commercial embedded firmware.
- **X:** ChaN's FatFs documentation states that exFAT is Microsoft's patented format and that using it in a product is the integrator's responsibility. Commercial products shipping exFAT have historically obtained a license through Microsoft's IP licensing program (long administered via Tuxera).
- **I:** DAQiFi Nyquist firmware is a **commercial shipping product**, not the Linux kernel, so the OIN pledge likely does **not** cover it. Enabling exFAT probably requires a Microsoft exFAT patent license (or a legal determination that our use is covered / the risk is acceptable). **This is a business/legal decision, not an engineering one**, and it gates the whole feature.

---

## 7. Recommendation

**Do not flip `FF_FS_EXFAT` to `1` yet.** Two independent gates block a speculative enable:

1. **Licensing (blocking, business/legal — §6):** exFAT is Microsoft-patented; the OIN pledge covers the Linux kernel, not our commercial firmware. Get a legal read on whether we need a Microsoft exFAT license *before* any engineering work. If the answer is "no license / not acceptable risk," the feature stops here regardless of the code being easy.
2. **RAM (blocking as-configured, engineering — §3):** the flip adds **~628 B of static BSS** (verified: `DirBuf[608]` gated by `#if FF_FS_EXFAT`, plus ~20 B `FATFS` growth). That exceeds the ~500 B headroom and is the same failure class as #391's +876 B link break. Enabling on the current `FF_USE_LFN 1` config will very likely fail the link. The follow-up must either (a) move the exFAT working buffer to heap via `FF_USE_LFN 3`, or (b) build-and-`.map`-verify that the ~628 B actually fits after other RAM changes land.

**If both gates clear,** the implementation is small and low-risk (mature FatFs code path): flip `FF_FS_EXFAT 1` (+ chosen LFN buffer strategy), widen the mount whitelist to `FS_EXFAT` (§5.1), make the `MAXSize` validator filesystem-aware so >4 GB single files are actually reachable on exFAT (§4), extend `SYST:STOR:SD:FORmat` with a filesystem-type argument (§5.2), and ship a `daqifi-python-test-suite` regression that formats/mounts/streams-to/reads-back an exFAT card >32 GB and confirms a single >4 GB file. Retain file-splitting as an optional manageability feature.

**Sequencing:** licensing first (cheap, and it can kill the feature), RAM `.map` measurement second, code last.

---

*Assessment date: 2026-07. Doc-only; no firmware built or flashed. All file:line references are to the worktree at assessment time.*
