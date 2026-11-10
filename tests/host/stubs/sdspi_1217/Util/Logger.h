#ifndef SDSPI_1217_LOGGER_H
#define SDSPI_1217_LOGGER_H
/* Counts instead of printing: a test can then assert that a fault was reported. */
extern unsigned gTest1217LogE;
#define LOG_MODULE_SD 0
#define LOG_LEVEL_SD 0
#define LOG_E(...) (gTest1217LogE++)
#define LOG_D(...) ((void)0)
#define LOG_I(...) ((void)0)
#define LOG_E_ONCE(bit, ...) (gTest1217LogE++)
#define LOG_D_ONCE(bit, ...) ((void)0)
#endif
