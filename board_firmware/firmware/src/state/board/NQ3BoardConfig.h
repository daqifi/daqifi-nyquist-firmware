/*! @file NQ3BoardConfig.h
 * @brief Interface of the Board Configuration module for Nyquist 3
 *     
 */

#ifndef __NQ3BOARDCONFIG_H__
#define __NQ3BOARDCONFIG_H__


#ifdef __cplusplus
extern "C" {
#endif

/*! This function is used for getting a board version 3 configuration parameter
 * @return Pointer to Board Configuration structure
 */
const void *NQ3BoardConfig_Get( void );

#ifdef __cplusplus
}
#endif

#endif /* __NQ3BOARDCONFIG_H__ */