/* Host stand-in for Harmony's system/reset/sys_reset.h, for test_1198 (see
 * ../../README.md). The vendored header's PLIB chain needs the device's
 * processor header; datastream.c only calls SYS_RESET_SoftwareReset
 * (JMP_TO_APP), which the test defines and counts. */
#ifndef BTL1198_STUB_SYS_RESET_H
#define BTL1198_STUB_SYS_RESET_H
void SYS_RESET_SoftwareReset(void);
#endif
