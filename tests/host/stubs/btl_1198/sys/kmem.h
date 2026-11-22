/* Host stand-in for XC32's <sys/kmem.h>, for test_1198 (see ../README.md).
 * KSEG0 -> KSEG1 is bit 29 on PIC32. Only datastream.c's READ_CRC uses it, and
 * the value only has to be an address the test's APP_CalculateCrc stub
 * recognises as NOT one of its own buffers (so it is never dereferenced). */
#ifndef BTL1198_STUB_SYS_KMEM_H
#define BTL1198_STUB_SYS_KMEM_H
#include <stdint.h>
#define KVA0_TO_KVA1(v) ((uintptr_t)(v) | 0x20000000u)
#endif
