/* #1217: the REAL SD driver, as one host translation unit, for
 * test_1217_abandoned_write.c. Built with warnings off: this is vendored code
 * compiled for a platform it was not written for, and warnings in it are not
 * this test's subject. The driver has one-shot Initialize state and no
 * teardown, so this also gives the test a way to start each case clean. */
#include "drv_sdspi.c"
#include "drv_sdspi_driver_interface.c"

void test1217_DriverReset(void)
{
    (void) memset(gDrvSDSPIObj, 0, sizeof(gDrvSDSPIObj));
    for (unsigned i = 0U; i < DRV_SDSPI_INSTANCES_NUMBER; i++)
    {
        /* These are file-scope, so they outlive the memset. Same value as
         * Initialize leaves them, but a previous case may have sent a token
         * through the buffers. */
        (void) memset(gDrvSDSPICmdResponseBuffer[i], 0, sizeof(gDrvSDSPICmdResponseBuffer[i]));
    }
}
