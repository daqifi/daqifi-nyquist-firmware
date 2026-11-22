Stubs for `test_1198_bootloader_drop_rejected_frame.c`, which compiles the
REAL bootloader `datastream.c` on the host. They are listed ahead of the
bootloader's own `framework/` directory on the include path, so these two win
and the vendored Harmony headers are used for everything else.

- `sys/kmem.h` stands in for XC32's; only `KVA0_TO_KVA1` (READ_CRC) is used.
- `system/reset/sys_reset.h` replaces the vendored one, whose PLIB chain needs
  the device's processor header. Only `SYS_RESET_SoftwareReset` (JMP_TO_APP)
  is used; the test defines it and counts its calls.
