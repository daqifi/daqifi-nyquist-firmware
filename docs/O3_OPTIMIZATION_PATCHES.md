# -O3 Optimization Patches

The firmware builds with GCC `-O3` optimization (XC32 v4.60 for PIC32MZ2048EFM144). This enables aggressive inlining and loop unrolling that improves streaming throughput but triggers false positives in third-party code and one edge case in our own code.

Four patches are required for a clean `-O3 -Werror` build. **Three of these are workarounds for compiler/library issues and should be reevaluated after upgrades.**

## Patches

### 1. libscpi: ELF Visibility Attribute Guard

**File:** `firmware/src/libraries/scpi/libscpi/src/utils_private.h` line 49

**Change:**
```c
// Before:
#if defined(__GNUC__) && (__GNUC__ >= 4)

// After:
#if defined(__GNUC__) && (__GNUC__ >= 4) && (defined(__unix__) || defined(__linux__) || defined(__APPLE__) || defined(__CYGWIN__))
```

**Why:** `__attribute__((visibility("hidden")))` is an ELF shared-library feature that is meaningless on bare-metal PIC32 (no ELF loader, no shared libraries). At `-O1` the compiler silently ignores it, but at `-O3 -Werror` it becomes an error because the attribute affects inlining decisions and the compiler can't resolve it.

**Permanent?** No — third-party library. Reevaluate after upgrading libscpi. Upstream may add their own platform guard.

---

### 2. Logger: strncpy → memcpy

**File:** `firmware/src/Util/Logger.c` line 483

**Change:**
```c
// Before:
strncpy(logBuffer.entries[logBuffer.head].message, message, message_len);

// After:
memcpy(logBuffer.entries[logBuffer.head].message, message, message_len);
```

**Why:** `strncpy(dst, src, strlen(src))` is a well-known anti-pattern — the count equals the source length, so strncpy never null-terminates the destination. The next line (`message[message_len] = '\0'`) does manual null-termination, which is exactly what `memcpy` + explicit null-term means. GCC `-O3` with `-Werror` flags this as `-Wstringop-truncation`.

**Permanent?** **Yes** — this is a genuine bug fix, not a workaround. The original code was incorrect regardless of optimization level.

---

### 3. WiFi Serial Bridge: noinline Attribute

**File:** `firmware/src/services/wifi_services/wifi_serial_bridge_interface.c` lines 64–68, 80

**Change:**
```c
// Forward declaration with noinline:
size_t __attribute__((noinline)) wifi_serial_bridge_interface_UARTReadGetBuffer(void *pBuf, size_t numBytes);

// Function definition with noinline:
size_t __attribute__((noinline)) wifi_serial_bridge_interface_UARTReadGetBuffer(void *pBuf, size_t numBytes) {
```

**Why:** At `-O3`, GCC inlines `UARTReadGetBuffer` (which operates on a 512-byte ring buffer) into `GetByte` (which passes a pointer to a 1-byte stack variable). After inlining, GCC sees a potential `memcpy` from a 512-byte source into a 1-byte destination and flags `-Warray-bounds`. This is a false positive — the runtime code path correctly limits `numBytes` to 1, but GCC's static analysis loses track of the bound after inlining.

`noinline` is preferable to `#pragma GCC diagnostic` because:
- The root cause is the inlining, not the diagnostic
- The function takes a mutex, so inlining it into a trivial wrapper is counterproductive for code size anyway
- The `noinline` is self-documenting with the comment block

**Permanent?** No — GCC may improve its static analysis in future versions. Reevaluate after upgrading XC32 (which bundles a specific GCC version).

---

### 4. wolfSSL tfm.c: Per-File Warning Suppression

**File:** MPLAB X project properties → per-file override for `firmware/src/third_party/wolfssl/wolfssl/wolfcrypt/src/tfm.c`

**Change:** Added `-Wno-error=array-bounds` as an additional compiler option for this single file.

**How to apply in MPLAB X:**
1. In the Projects panel, expand `Source Files > third_party > wolfssl > wolfcrypt > src`
2. Right-click `tfm.c` → Properties
3. Under `xc32-gcc` → `Additional options`, add: `-Wno-error=array-bounds`

**Why:** wolfSSL's big-number math (`fp_mul_comba`, `fp_sqr_comba`) uses deeply nested loops with computed indices. At `-O3`, GCC aggressively unrolls and inlines these loops, then loses track of the loop variable's range and flags false `-Warray-bounds` errors. This is a known issue with wolfSSL + GCC optimization — [similar reports exist](https://github.com/wolfSSL/wolfssl/issues) for other embedded platforms.

**Permanent?** No — reevaluate after:
- Upgrading wolfSSL (currently pinned to v5.4.0 due to [Microchip build breakage in v5.7.0](https://forum.microchip.com/s/topic/a5CV4000000249BMAQ/t397847))
- Upgrading XC32/GCC (improved static analysis may eliminate the false positive)

## Verification

After making any changes, verify a clean build:
```bash
cd firmware/daqifi.X
"/mnt/c/Program Files/Microchip/MPLABX/v6.30/gnuBins/GnuWin32/bin/make.exe" \
  -f nbproject/Makefile-default.mk CONF=default build -j$(nproc)
```

The build must complete with **zero warnings** (`-Werror` promotes all warnings to errors).

## After Upgrading Compilers or Libraries

1. Remove patches #1, #3, and #4
2. Attempt a clean `-O3 -Werror` build
3. If it passes, delete this document's entries for the resolved patches
4. If new warnings appear, investigate whether they are true bugs or new false positives, and apply targeted fixes following the same pattern (minimal, documented, with "reevaluate after upgrade" notes)

Patch #2 (strncpy → memcpy) is a permanent fix and should not be reverted.
