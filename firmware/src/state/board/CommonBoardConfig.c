#include "CommonBoardConfig.h"

/**
 * @file CommonBoardConfig.c
 * @brief Common board configuration const data shared across all Nyquist variants
 *
 * Contains actual const arrays and structures for configuration elements
 * that are identical across NQ1, NQ2, NQ3 board variants.
 */

// =============================================================================
// CSV Column Headers - Common across all variants
// =============================================================================

/**
 * CSV column headers for first channel in a row (no leading comma).
 * All variants use 16 analog input channels with "ain" prefix.
 */
const char* COMMON_CSV_CHANNEL_HEADERS_FIRST[16] = {
    "ain0_ts,ain0_val",   "ain1_ts,ain1_val",   "ain2_ts,ain2_val",   "ain3_ts,ain3_val",
    "ain4_ts,ain4_val",   "ain5_ts,ain5_val",   "ain6_ts,ain6_val",   "ain7_ts,ain7_val",
    "ain8_ts,ain8_val",   "ain9_ts,ain9_val",   "ain10_ts,ain10_val", "ain11_ts,ain11_val",
    "ain12_ts,ain12_val", "ain13_ts,ain13_val", "ain14_ts,ain14_val", "ain15_ts,ain15_val"
};

/**
 * CSV column headers for subsequent channels in a row (with leading comma).
 * All variants use 16 analog input channels with "ain" prefix.
 */
const char* COMMON_CSV_CHANNEL_HEADERS_SUBSEQUENT[16] = {
    ",ain0_ts,ain0_val",   ",ain1_ts,ain1_val",   ",ain2_ts,ain2_val",   ",ain3_ts,ain3_val",
    ",ain4_ts,ain4_val",   ",ain5_ts,ain5_val",   ",ain6_ts,ain6_val",   ",ain7_ts,ain7_val",
    ",ain8_ts,ain8_val",   ",ain9_ts,ain9_val",   ",ain10_ts,ain10_val", ",ain11_ts,ain11_val",
    ",ain12_ts,ain12_val", ",ain13_ts,ain13_val", ",ain14_ts,ain14_val", ",ain15_ts,ain15_val"
};
