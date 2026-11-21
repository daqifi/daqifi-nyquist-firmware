#ifndef HIF881_LOGGER_H
#define HIF881_LOGGER_H
/* #881 host-test stub: counts LOG_E so a test can assert a drop was reported. */
extern unsigned gTest881LogE;
#define LOG_MODULE_WIFI 0
#define LOG_E(...) (gTest881LogE++)
#endif
