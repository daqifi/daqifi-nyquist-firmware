#ifndef SDSPI_1217_CONFIGURATION_H
#define SDSPI_1217_CONFIGURATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define DRV_SDSPI_INSTANCES_NUMBER 1
#define SYS_TIME_CPU_CLOCK_FREQUENCY 200000000U   /* #1255: the CP0 count is half this */
#define CACHE_ALIGN
#define SYS_ASSERT(test, message) ((void)(test))
#endif
