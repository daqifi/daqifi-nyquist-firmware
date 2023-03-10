#include "Delay.h"

#include "state/data/BoardData.h"

void Delay(const uint32_t ticks)
{
    if (g_BoardData.InISR > 0)
    {
        uint32_t i;
        for (i=0; i < ticks; ++i)
        {
            // If we're in an ISR, busy wait
            asm("nop");
        }
    }
    else
    {
        // If we're running normally, defer to the OS
        vTaskDelay(ticks);
    }
}
