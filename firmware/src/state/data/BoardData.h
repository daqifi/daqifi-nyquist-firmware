/*! @file BoardData.h
 * @brief Interface of the board data module
 * 
 * j.longares@abluethinginthecloud.com
 * 
 * A Blue Thing In The Cloud S.L.U.
 *   === When Technology becomes art ===
 * www.abluethinginthecloud.com
 *     
 */

#ifndef __BOARDDATA_H__
#define __BOARDDATA_H__

#include "AInSample.h"
#include "DIOSample.h"
#include "Util/HeapList.h"
#include "HAL/Power/PowerApi.h"
#include "HAL/UI/UI.h"
#include "services/daqifi_settings.h"


#ifdef __cplusplus
extern "C" {
#endif  

    //! Enumeration with the fields that could be set and get, relative to the
    // board data

    enum eBoardData {
        //! Return all board data 
        BOARDDATA_ALL_DATA,
        //! Used to know if the application is inside an ISR
        BOARDDATA_IN_ISR,
        //! DIO latest field
        BOARDDATA_DIO_LATEST,
        //! DIO Samples list
        BOARDDATA_DIO_SAMPLES,
        //! State of the AIN module
        BOARDATA_AIN_MODULE,
        //! Latest AIN SAMPLES
        BOARDDATA_AIN_LATEST,
        //! Latest AIN samples size
        BOARDDATA_AIN_LATEST_SIZE,
        //! Timestamp corresponding to the a sample
        BOARDDATA_AIN_LATEST_TIMESTAMP,
        //! Collected analog input samples
        BOARDDATA_AIN_SAMPLES,
        //! Global power satructure
        BOARDATA_POWER_DATA,
        //! UI Global structure
        BOARDDATA_UI_VARIABLES,
        //! Wifi settings
        BOARDDATA_WIFI_SETTINGS,
        //! Streaming trigger timestamp
        BOARDDATA_STREAMING_TIMESTAMP,
        //! Number of accessible fields
        BOARDDATA_NUM_OF_FIELDS
    };

    /*! @strcut sBoardData
     * @brief Data structure for defining the data space for the board's acquisition
     * functions to use
     * @typedef tBoardData
     * @brief Data type associated to the structure sBoardData
     */
    typedef struct sBoardData {
        //! Indicates whether the application is currently inside of an isr
        int InISR;
        //! The latest DIO sample
        DIOSample DIOLatest;
        //! Collected DIO samples
        DIOSampleList DIOSamples;
        //! The current state of the module
        AInModDataArray AInState;
        //! The latest AIn samples
        AInSampleArray AInLatest;
        //! Collected Analog-input samples
        AInSampleList AInSamples;
        //! Global Power structure
        tPowerData PowerData;
        //! Global UI Structure
        tUIReadVars UIReadVars;
        //! The active wifi settings
        wifi_manager_settings_t wifiSettings;
        //! The streaming trigger timestamp
        uint32_t StreamTrigStamp;
    } tBoardData;

    /*!
     * Initializes the board data 
     * @param[in] boardData Pointer to data space 
     */
    void InitializeBoardData(tBoardData* boardData);

    /*! This function is used for getting a board data parameter
     * @param[in] parameter Parameter to be get
     * @param[in] index In case that the parameter is an array, an index can be 
     * specified here for getting a specific member of the array
     * @return Parameter which is part of the global Board Configuration 
     *         structure
     */
    void *BoardData_Get(                                                  
                            enum eBoardData parameter,                      
                            uint8_t index);

    /*! This function is used for setting a board data parameter
     * @param[in] parameter Parameter to be set
     * @param[in] index In case that the parameter is an array, an index can be 
     * specified here for setting a specific member of the array
     * @param[in] pSetValue Pointer to the configuration value to be set
     */
    void BoardData_Set(                                                         
                            enum eBoardData parameter,                      
                            uint8_t index,                                  
                            const void *pSetValue);

#ifdef __cplusplus
}
#endif

#endif /* __BOARDDATA_H__ */