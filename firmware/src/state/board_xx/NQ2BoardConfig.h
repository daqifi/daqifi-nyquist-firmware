/*! @file NQ2BoardConfig.h
 * @brief Interface of the Board Configuration module for Nyquist 2
 *     
 */

#ifndef __NQ2BOARDCONFIG_H__
#define __NQ2BOARDCONFIG_H__


#ifdef __cplusplus
extern "C" {
#endif

/*! This function is used for getting a board version 2 configuration parameter
 * @return Pointer to Board Configuration structure
 */
const void *NQ2BoardConfig_Get( void );

#ifdef __cplusplus
}
#endif

#endif /* __NQ2BOARDCONFIG_H__ */