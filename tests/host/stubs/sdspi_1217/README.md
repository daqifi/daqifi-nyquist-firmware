Stubs for `test_1217_abandoned_write.c`, which compiles the REAL
`drv_sdspi.c` and `drv_sdspi_driver_interface.c` on the host. They are listed
ahead of `firmware/src/config/default` on the include path, so these win and the
driver's own headers are used for everything else. The behaviour behind them
(DRV_SPI's FIFO and CS handling, SYS_TIME) is in the test.
