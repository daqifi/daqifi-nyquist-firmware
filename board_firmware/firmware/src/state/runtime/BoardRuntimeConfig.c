#include "BoardRuntimeConfig.h"

BoardRuntimeConfig __attribute__((coherent)) g_BoardRuntimeConfig;

void InitBoardRuntimeConfig(int boardVariant)
{
    // Initialize variable to known state
    memset(&g_BoardRuntimeConfig, 0, sizeof(g_BoardRuntimeConfig));
    
    switch (boardVariant)
    {
    case 3:
        memcpy(&g_BoardRuntimeConfig, &g_NQ3BoardRuntimeConfig, sizeof(BoardRuntimeConfig));
        break;
    case 2:
        memcpy(&g_BoardRuntimeConfig, &g_NQ2BoardRuntimeConfig, sizeof(BoardRuntimeConfig));
        break;
    case 1: // Everything else is an NQ1
    default:
        memcpy(&g_BoardRuntimeConfig, &g_NQ1BoardRuntimeConfig, sizeof(BoardRuntimeConfig));
        break;
    }
}
