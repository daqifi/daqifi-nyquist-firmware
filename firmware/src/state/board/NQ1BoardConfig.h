/*! @file NQ1BoardConfig.h
 * @brief Interface of the Board Configuration module for Nyquist 1
 * 
 * j.longares@abluethinginthecloud.com
 * 
 * A Blue Thing In The Cloud S.L.U.
 *   === When Technology becomes art ===
 * www.abluethinginthecloud.com
 *     
 */

#ifndef __NQ1BOARDCONFIG_H__
#define __NQ1BOARDCONFIG_H__


#ifdef __cplusplus
extern "C" {
#endif

/*! This function is used for getting a board version 1 configuration parameter
 * @return Pointer to Board Configuration structure
 */
const void *NQ1BoardConfig_Get( void );

#ifdef __cplusplus
}
#endif

#endif /* __NQ1BOARDCONFIG_H__ */