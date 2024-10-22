/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.h

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#ifndef _OCMP_API_H    /* Guard against multiple inclusion */
#define _OCMP_API_H

#include "configuration.h"
#include "definitions.h"


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif

    typedef enum {
        OCMPAPI_ID_1 = 1,
        OCMPAPI_ID_3 = 3,
        OCMPAPI_ID_4 = 4,
        OCMPAPI_ID_6 = 6,
        OCMPAPI_ID_7 = 7,
        OCMPAPI_ID_8 = 8,
    } OcmpApi_id_t;

    void OcmpApi_Initialize(OcmpApi_id_t id);

    void OcmpApi_Enable(OcmpApi_id_t id, uint16_t portRemapPin);

    void OcmpApi_Disable(OcmpApi_id_t id, uint16_t portRemapPin);

    void OcmpApi_CompareValueSet(OcmpApi_id_t id, uint16_t value);

    uint16_t OcmpApi_CompareValueGet(OcmpApi_id_t id);


    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */

/* *****************************************************************************
 End of File
 */
