# PIC32MZ reference library

Microchip documentation for the PIC32MZ2048EFM144 (our MCU) and
related Harmony framework. Catalog organized by topic with notes
on which files are most relevant to current open issues.

**Most relevant to the live #406 / #421 ADC investigation:**
1. `Section22._12-bit_HS_SAR_ADC_FRM_DS60001344E.pdf` — TRGSRC
   encoding table on page 22-38; CSS scan rules on page 22-32.
   See [TRGSRC findings](#critical-finding-trgsrc-encoding-page-22-38)
   below.
2. `61105F.pdf` (Section 14 Timers) — TMR4/5 paired mode is the
   streaming trigger source.
3. `PIC32MZ-Embedded-Connectivity-with-Floating-Point-Unit-Family-Data-Sheet-DS60001320H.pdf` — device-specific TRGSRC values
   (per-chip mapping for values 4-31; the FRM is generic across
   the SAR ADC family).

## Critical finding: TRGSRC encoding (page 22-38)

The per-channel `ADCTRGx.TRGSRCn<4:0>` field has these
documented values (verbatim from FRM Section 22, Register 22-19):

| Value | Name | Description |
|------:|------|-------------|
| `00000` | No Trigger | Channel never converts |
| `00001` | GSWTRG | Global Software Edge — fires on `ADCCON3.GSWTRG` |
| `00010` | GLSWTRG | Global Level Software Trigger |
| **`00011`** | **STRIG** | **Scan Trigger — REQUIRED for shared MODULE7 scan** |
| `00100`-`11110` | (per device datasheet) | TMR1/3/5/OC1 etc. |
| `11111` | (per device datasheet) | reserved/external |

Page 22-32 (ADCCSS1 Note 2) is the rule for shared MODULE7
inclusion of low-numbered (Class 1/2) channels:

> "If a Class 1 or Class 2 input is included in the scan by
> setting the CSSx bit to '1' and by setting the TRGSRCx<4:0>
> bits to STRIG mode ('0b011'), the user application must
> ensure that no other triggers are generated for that input
> using the RQCNVRT bit in the ADCCON3 register or the hardware
> input or any digital filter. Otherwise, the scan behavior is
> unpredictable."

So:
- Hardware ADCHS channels CH0-CH11 have a per-channel TRGSRC
  field (Class 1 or Class 2 inputs).
- Hardware ADCHS channels CH12-CH31 share TRGSRC entries with
  channels 0-11 (only 32 trigger slots total).
- Hardware ADCHS channels CH32-CH63 are always Class 3 — no
  TRGSRC field, always follow the module's STRGSRC.

This means the bug fix for #406/#421 is **set TRGSRC=3 (STRIG)
on every shared-module channel that has a per-channel TRGSRC
slot** — currently NQ1 board enables ADCHS_CH5/6/7/8/11 in CSS1
for MODULE7 scan, and they need TRGSRC=3, not the PLIB default
of 1 (GSWTRG) and not the audit-branch's 4 (TMR1).

## Index

### Family Reference Manual sections (for PIC32MZ)

The numbered "DS6000xxxx" files are Section X of the PIC32
Family Reference Manual. The FRM is generic across the PIC32
family — for chip-specific values (clock dividers, IRQ
numbers, peripheral count) cross-check the device datasheet
DS60001320H.

| File | Section | Topic | Notes |
|---|---|---|---|
| `61127D.pdf` | 1 | Introduction | FRM front matter, conventions |
| `61130F.pdf` | 10 | Power-Saving Modes | Sleep / Idle entry |
| `60001120F.pdf` | 12 | I/O Ports | TRIS/LAT/PORT/CNCON |
| `60001128H.pdf` | 13 | Parallel Master Port (PMP) | Not used in DAQiFi |
| `61105F.pdf` | **14** | **Timers** | **TMR1/3/5; TMR4/5 paired drives streaming trigger** |
| `60001122G.pdf` | 15 | Input Capture | Not used in DAQiFi |
| `61111E.pdf` | 16 | Output Compare | Not used in DAQiFi |
| `60001110f.pdf` | 19 | Comparator | Not used in DAQiFi |
| `61109G.pdf` | 20 | Comparator Voltage Reference | Vref source for ADC |
| `60001107H.pdf` | 21 | UART | Debug UART4, ICSP UART |
| `Section22._12-bit_HS_SAR_ADC_FRM_DS60001344E.pdf` | **22** | **12-bit HS SAR ADC** | **THE source for #406 / #421. TRGSRC enum p.22-38, CSS scan rules p.22-32, EOS interrupt p.22-?, MODULE7 mux operation throughout** |
| `61106G.pdf` | 23 | SPI | SPI2 (DAC), SPI4 (WINC), SPI6 (AD7609 NQ3) |
| `60001116G.pdf` | 24 | I²C | I2C5 (BQ24297) |
| `61125F.pdf` | 29 | RTCC | Not actively used |
| `60001117H.pdf` | 31 | DMA Controller | SPI/USB/SD DMA |
| `61129F.pdf` | 33 | Programming and Diagnostics | EJTAG/ICSP, debug HW |
| `61154C.pdf` | 34 | CAN | Not used in DAQiFi |
| `60001155D.pdf` | 35 | Ethernet Controller | Not used (we use WINC1500 SPI) |
| `60001183B.pdf` | 41 | Prefetch Module for L1 CPU Cache | Flash wait-state hiding (relevant to errata #38) |
| `60001250B.pdf` | 42 | Oscillators with Enhanced PLL | SYSCLK, REFCLK1 (for ADC and SPI4) |
| `60001244C.pdf` | 46 | Serial Quad Interface (SQI) | Not used in DAQiFi |
| `60001245A.pdf` | 47 | External Bus Interface (EBI) | Not used in DAQiFi |
| `60001214A.pdf` | 48 | Memory Organization and Permissions | KSEG0/1, MMU/TLB |
| `60001246B.pdf` | 49 | Crypto Engine and RNG | Hardware crypto (we use wolfSSL software path instead) |
| `61232A.pdf` | 51 | Hi-Speed USB with OTG | USB CDC streaming path |
| `60001193B.pdf` | 52 | Flash Memory with Live Update | Bootloader, NVM partitioning |

### Device-specific datasheet, errata, programming spec

| File | Document | Notes |
|---|---|---|
| `PIC32MZ-Embedded-Connectivity-with-Floating-Point-Unit-Family-Data-Sheet-DS60001320H.pdf` | **DS60001320H** | **PIC32MZ-EF family device datasheet.** Chip-specific TRGSRC values for fields 4-31 (TMR1/3/5/OC mappings), package pinouts, electrical specs, peripheral counts (8 ADC modules, 6 SPI, 9 timers, etc.) |
| `PIC32MZ-Embedded-Connectivity-with-Floating-Point-Unit-Family-Silicon-Errata-DS80000663.pdf` | **DS80000663** | Silicon errata. Already digested in CLAUDE.md "Known Silicon Errata" table. Re-read for any ADC-related entries when chasing #406. |
| `PIC32-Flash-Programming-Specification-DS60001145.pdf` | DS60001145 | Programming protocol — used by IPE / PICkit. Mostly opaque to firmware engineers. |

### Application notes

| File | App note | Topic |
|---|---|---|
| `00001600C.pdf` | **AN1600** | **Using L1 Cache on PIC32MZ** — KSEG0 cached vs KSEG1 uncached, prefetch interaction. Relevant to DMA buffer placement (`__attribute__((coherent))`). |
| `Worlds-Fastest-Embedded-Interleaved-12-bit-ADC-Using-PIC32MZ-and-PIC32MK-Families-DS00002785A.pdf` | **AN2785** | **Interleaved 12-bit ADC** — use multiple dedicated modules (MODULE0-6) sampling the same input on staggered triggers to multiply effective sample rate. Up to 30 Msps with 6× interleave. Relevant if we want to push past current per-channel ceiling. |
| `Basic-32-Bit-MCU-Design-and-Troubleshooting-Checklist-DS70005439.pdf` | DS70005439 | Hardware checklist (decoupling, clock layout, etc.) — board-design reference, less relevant to firmware. |
| `AN3703-Live-Update-Application-on-PIC32MZ-MCU-Using-MPLAB-Harmony-v3-DS00003703.pdf` | AN3703 | Live firmware update via dual-bank flash. Not in our bootloader scope today. |

### Harmony framework tech briefs ("How-to" series)

Generic Harmony documentation, not chip-specific. Mostly
useful when adding/removing PLIBs via MHC/MCC.

| File | Document | Topic |
|---|---|---|
| `How-to-Setup-MPLAB-Harmony-v3-Software-Development-Framework-DS90003232.pdf` | DS90003232 | Initial Harmony v3 setup (MHC/MCC tooling) |
| `How-to-Build-an-Application-by-Adding-a-New-PLIB-Driver-or-Middleware-to-an-Existing-MPLAB-Harmony-v3-Project-DS90003253.pdf` | DS90003253 | Adding new PLIBs/drivers via MHC |
| `How-to-Use-the-MPLAB-Harmony-v3-Debug-System-Service-DS90003246.pdf` | DS90003246 | Harmony's debug/log service (we use our own `Util/Logger.c` instead) |
| `How-to-Use-the-DMA-CRC-Generator-on-PIC32MXPIC32MZPIC32MM-Devices-DS90003196.pdf` | DS90003196 | DMA-driven CRC computation |
| `Difference_Between_MPLAB_Harmony_PLIBs_and_Drivers_and_When_to_Use_Them_Tech_Brief_DS90003291A.pdf` | DS90003291A | PLIB (low-level register API) vs Driver (state-machine). We use PLIB for ADC, GPIO, Timers; Drivers for SPI, USB, SD, WINC. |
| `The-Difference-Between-MPLAB-Harmonyv3-Synchronous-and-Asynchronous-Drivers-and-When-to-Use-DS90003269A.pdf` | DS90003269A | Sync vs async driver patterns |
| `MPLAB_Harmony_v3_Synchronous_Drivers_and_Their_Usage_in_FreeRTOS-Based_Applications_DS90003290A.pdf` | DS90003290A | Sync drivers in FreeRTOS — relevant to our task model |
| `Multiplexed-and-Remappable-Functionalities-on-IOpins-on-PIC32M-Devices-Using-MPLAB-HarmonyV3-DS90003296.pdf` | DS90003296 | PPS (peripheral pin select) configuration |
| `Handling-Cache-Coherency-Issues-at-Runtime-Using-Cache-Maintenance-Operations-on-PIC32MZ-MCUs-DS90003308.pdf` | **DS90003308** | **Cache coherency at runtime** — alternative to `__attribute__((coherent))` blanket policy. Lets you keep DMA buffers cached and use `_data_cache_invalidate`/`_data_cache_flush` around transfers. Worth reading if we ever want to move WINC SPI staging buffer back to KSEG0 cached. |
| `HarmonyHelpAppsRTOSDemonstrations.pdf` | (no DS#) | Harmony help excerpt for RTOS demos |

## Quick lookup index

When debugging X, read Y first:

| Problem area | Primary reference | Secondary |
|---|---|---|
| ADC channels missing / TRGSRC config | Section 22 ADC FRM (DS60001344E) p.22-32 (CSS), p.22-38 (TRGSRC), p.22-32 Note 2 | Datasheet DS60001320H ADC chapter |
| Streaming timer / ADC trigger sync | Section 14 Timers FRM (61105F) | Section 22 ADC FRM p.22-37 (STRGSRC) |
| DMA buffer corruption | AN1600 + Section 41 Prefetch + DS90003308 | Section 31 DMA |
| WINC1500 SPI bus weirdness | Section 23 SPI FRM (61106G) | Errata DS80000663 #27 (SRMT bit) |
| BQ24297 I²C wedges | Section 24 I²C FRM (60001116G) | Errata DS80000663 #6, #37 |
| USB CDC bursty delivery | Section 51 USB OTG FRM (61232A) | (Harmony driver not in scope) |
| Boot reset issues | Section 52 Flash Live Update + Section 9 WDT/PWT | Errata DS80000663 #38 |
| Memory layout / linker fights | Section 48 Memory Org + Section 41 Prefetch | (linker scripts in `firmware/src/config/default/p32MZ2048EFM144.ld`) |

## When fetching new references

Microchip CDN blocks direct WebFetch (403) but Microchip
documents are easy to find via:

- Family Reference Manual sections: search `ds6000xxxx` on
  microchip.com and download from the device's "Documentation"
  tab.
- Errata: linked from each device's product page.
- App notes: search `AN<number>` or `DS00<number>`.

If you find a relevant new doc, drop the PDF in this folder
and add a row to the appropriate table above.
