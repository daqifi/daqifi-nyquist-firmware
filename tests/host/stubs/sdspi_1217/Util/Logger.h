#ifndef SDSPI_1217_LOGGER_H
#define SDSPI_1217_LOGGER_H
/* Counts instead of printing: a test can then assert that a fault was reported.
 * LOG_E also keeps the formatted text of recent lines (test1217_LogE), so a test
 * can assert what a line says, e.g. which cause abandoned a write. */
extern unsigned gTest1217LogE;
void test1217_LogE(const char* fmt, ...);
#define LOG_MODULE_SD 0
#define LOG_LEVEL_SD 0
#define LOG_E(...) test1217_LogE(__VA_ARGS__)
#define LOG_D(...) ((void)0)
#define LOG_I(...) ((void)0)
#define LOG_E_ONCE(bit, ...) (gTest1217LogE++)
#define LOG_D_ONCE(bit, ...) ((void)0)
#endif
