/* #1206 host stub: the CP0 count is the test's simulated clock. Each read
 * advances it, so a wait loop that polls the count makes simulated progress. */
#ifndef NVM1206_STUB_XC_H
#define NVM1206_STUB_XC_H
#include <stdint.h>
uint32_t nvm1206_Cp0Count(void);
#define _CP0_GET_COUNT() nvm1206_Cp0Count()
#endif
