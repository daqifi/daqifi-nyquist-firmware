Stubs for `test_1206_nvm_bounded_waits.c`, which compiles the REAL
`firmware/src/HAL/NVM/nvm.c` on the host. They stand in for Harmony's
`configuration.h` / `definitions.h` (DMACON, the NVM PLIB, `PA_TO_KVA1`),
`<xc.h>` (`_CP0_GET_COUNT`) and the two FreeRTOS headers nvm.c includes. Every
register and PLIB accessor is a function in the test, so each read advances a
simulated clock and the test decides when DMA quiesces, when the controller goes
idle and whether the completion interrupt is delivered. `clock_config.h` is
forwarded to the real one, so the budget under test is the firmware's own.
