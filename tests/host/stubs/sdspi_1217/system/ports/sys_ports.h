#ifndef SDSPI_1217_SYS_PORTS_H
#define SDSPI_1217_SYS_PORTS_H
#include <stdbool.h>
typedef int SYS_PORT_PIN;
#define SYS_PORT_PIN_NONE (-1)
void SYS_PORT_PinSet(SYS_PORT_PIN pin);
void SYS_PORT_PinClear(SYS_PORT_PIN pin);
bool SYS_PORT_PinRead(SYS_PORT_PIN pin);
#endif
