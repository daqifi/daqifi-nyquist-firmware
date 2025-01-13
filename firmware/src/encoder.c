/*! @file encoder.c
 * @brief This file includes the buffer for JSON or NanoPB encoding, as well as
 * its accessor. Only the encoding modules should access to this module
 * 
 * @author Javier Longares Abaiz
 */


#include "encoder.h"

//! Buffer declaration
static uint8_t buffer[ ENCODER_BUFFER_SIZE ];

/*! This function is used for getting the enconding
 * @return Pointer to the encoding buffer
 */
uint8_t *Encoder_Get_Buffer( void ){
    return buffer;
}