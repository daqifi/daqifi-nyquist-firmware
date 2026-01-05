/*! @file version.h
 * @brief Firmware and hardware version information
 *
 * Single source of truth for version strings across all board variants.
 * Update these values when releasing new versions.
 */

#ifndef __VERSION_H__
#define __VERSION_H__

#ifdef __cplusplus
extern "C" {
#endif

//! Hardware revision string
#define HARDWARE_REVISION "2.0.0"

//! Firmware revision string
#define FIRMWARE_REVISION "3.4.1n_12_12_25"

#ifdef __cplusplus
}
#endif

#endif /* __VERSION_H__ */
