/* #1279 host stub: every LOG_E is reported to the test, which fails it when
 * the scheduler is suspended (a log call may block on a mutex). */
#ifndef WINCSPI1279_LOGGER_H
#define WINCSPI1279_LOGGER_H
extern void test1279_log_e(void);
#define LOG_MODULE_WIFI 0
#define LOG_E(...) test1279_log_e()
#define LOG_I(...) test1279_log_e()
#endif
