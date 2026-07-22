#pragma once

#include "libraries/scpi/libscpi/inc/scpi/scpi.h"
#include <stdbool.h>

#ifdef	__cplusplus
extern "C" {
#endif

//TODO(Daqifi): Relocate for proper place
    
/**
 * SCPI Callback: Get the Enabled/Disabled status of LAN
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANEnabledGet(scpi_t * context);

/**
 * SCPI Callback: Set the Enabled/Disabled status of LAN
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANEnabledSet(scpi_t * context);

/**
 * SCPI Callback: Get the WINC deep-power-down state (#334).
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANPowerGet(scpi_t * context);

/**
 * SCPI Callback: Deep power-down / power-up the WINC chip (#334).
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANPowerSet(scpi_t * context);

/**
 * SCPI Callback: Get the type of LAN network
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANNetModeGet(scpi_t * context);


/**
 * SCPI Callback: Set the type of LAN network
 *   Infrastructure: 1
 *   AD-Hoc: 2 (Invalid for now. Validation is TBD.)
 *   P2P: 3 (Invalid for now. Validation is TBD.)
 *   Soft AP: 4 (default)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANNetModeSet(scpi_t * context);

/**
 * SCPI Callback: Get the AP-mode SSID hidden/cloaked flag (#45)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANHiddenGet(scpi_t * context);

/**
 * SCPI Callback: Set the AP-mode SSID hidden/cloaked flag (#45)
 *   1 = SSID hidden (not broadcast), 0 = SSID visible (default)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANHiddenSet(scpi_t * context);

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANAddrGet(scpi_t * context);

/**
 * SCPI Callback: Set the IP address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANAddrSet(scpi_t * context);

/**
 * SCPI Callback: Get the Ip mask of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMaskGet(scpi_t * context);

/**
 * SCPI Callback: Set the IP mask of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMaskSet(scpi_t * context);

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANGatewayGet(scpi_t * context);

/**
 * SCPI Callback: Set the IP address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANGatewaySet(scpi_t * context);


/**
 * SCPI Callback: Get the mac address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMacGet(scpi_t * context);

/**
 * SCPI Callback: Set the hostname address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANHostnameGet(scpi_t * context);

/**
 * SCPI Callback: Set the hostname address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
//scpi_result_t SCPI_LANHostnameSet(scpi_t * context);

/**
 * SCPI Callback: Set the ssid address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSsidGet(scpi_t * context);

/**
 * SCPI Callback: Set the ssid address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSsidSet(scpi_t * context);

scpi_result_t SCPI_LANSsidStrengthGet(scpi_t * context);

// CONFigured value queries (return user-set values, not DHCP-assigned)
scpi_result_t SCPI_LANConfAddrGet(scpi_t * context);
scpi_result_t SCPI_LANConfMaskGet(scpi_t * context);
scpi_result_t SCPI_LANConfGatewayGet(scpi_t * context);

/**
 * SCPI Callback: Set the security mode of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSecuritySet(scpi_t * context);
/**
 * SCPI Callback: Get the security mode of the device of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSecurityGet(scpi_t * context);

/**
 * SCPI Callback: Set the passkey of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANPasskeySet(scpi_t * context);

/**
 * SCPI Callback: Check the passkey of the device (for debugging)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANPasskeyGet(scpi_t * context);

/**
 * SCPI Callback: Returns the BSSID (MAC) of the associated AP as
 * "XX:XX:XX:XX:XX:XX". Errors if not connected as a STA.
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANBssidGet(scpi_t * context);

/**
 * SCPI Callback: Applies wifi settings of the device (optionally saving them)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSettingsApply(scpi_t * context);
/**
 * SCPI Callback: Hardware-reset the WINC chip (toggles GPIO reset).
 * Use to recover a wedged outbound TCP stack (#383). Returns
 * immediately; full recovery takes ~20 s -- poll ADDR? to confirm.
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANHardReset(scpi_t * context);
/**
 * @brief
 * @param context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANFwUpdate(scpi_t * context);
/**
 * @brief get the WI-FI Module information like CHIP-ID, Firmware version etc in Json format
 * @param context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANGetChipInfo(scpi_t * context);
/**
 * SCPI Callback: mDNS responder diagnostics (#58). Returns a JSON snapshot of
 * the responder's receive/answer counters + self-heal state so a wedge is
 * observable on-device (SYST:COMM:LAN:MDNS?).
 * @param context
 * @return SCPI_RES_OK on success, SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMdnsDiagGet(scpi_t * context);
/* ---------------------------------------------------------------------
 * User SPI1 master on the DIO terminal (#665, epic #664). SYST:COMM:SPI:*.
 * Hosted here as the SYST:COMM namespace home until the epic's I2C/UART
 * children justify a dedicated SCPIComm translation unit.
 * --------------------------------------------------------------------- */
/*! SCPI: SYST:COMM:SPI:CONFig <mosiDio|-1>,<misoDio|-1>,<csDio|-1>,<baud>,<mode 0-3>[,<order 0=MSB|1=LSB>] */
scpi_result_t SCPI_SpiConfigSet(scpi_t * context);
/*! SCPI: SYST:COMM:SPI:CONFig? -> JSON of the stored SPI config + state. */
scpi_result_t SCPI_SpiConfigGet(scpi_t * context);
/*! SCPI: SYST:COMM:SPI:ENAble <0|1> -> apply/tear down the SPI master. */
scpi_result_t SCPI_SpiEnableSet(scpi_t * context);
/*! SCPI: SYST:COMM:SPI:ENAble? -> 1 if the SPI master is active. */
scpi_result_t SCPI_SpiEnableGet(scpi_t * context);
/*! SCPI: SYST:COMM:SPI:TRANsfer? <hex> -> full-duplex transfer, MISO as hex. */
scpi_result_t SCPI_SpiTransfer(scpi_t * context);

/* User 1-Wire master on a DIO channel (#669, epic #664). SYST:COMM:OWIRe:*. */
/*! SCPI: SYST:COMM:OWIRe:ENAble <dio>,<0|1> -> claim/release the 1-Wire bus. */
scpi_result_t SCPI_OneWireEnableSet(scpi_t * context);
/*! SCPI: SYST:COMM:OWIRe:ENAble? -> claimed channel (0..15), or -1 if disabled. */
scpi_result_t SCPI_OneWireEnableGet(scpi_t * context);
/*! SCPI: SYST:COMM:OWIRe:RESet? -> reset pulse; 1 = presence detected. */
scpi_result_t SCPI_OneWireReset(scpi_t * context);
/*! SCPI: SYST:COMM:OWIRe:TRANsfer? <hexWrite>,<nRead> -> reset+write+read, hex. */
scpi_result_t SCPI_OneWireTransfer(scpi_t * context);
/*! SCPI: SYST:COMM:OWIRe:SEARch? -> comma list of 64-bit ROM ids (hex). */
scpi_result_t SCPI_OneWireSearch(scpi_t * context);

/* User UART on the DIO terminal (#16, epic #664). SYST:COMM:UART:*. */
/*! SCPI: SYST:COMM:UART:CONFig <rxDio|-1>,<txDio|-1>,<baud>[,<data 8>,<parity 0N|1E|2O>,<stop 1|2>] */
scpi_result_t SCPI_UartConfigSet(scpi_t * context);
/*! SCPI: SYST:COMM:UART:CONFig? -> JSON of the stored UART config + state. */
scpi_result_t SCPI_UartConfigGet(scpi_t * context);
/*! SCPI: SYST:COMM:UART:ENAble <0|1> -> apply/tear down the UART. */
scpi_result_t SCPI_UartEnableSet(scpi_t * context);
/*! SCPI: SYST:COMM:UART:ENAble? -> 1 if the UART is active. */
scpi_result_t SCPI_UartEnableGet(scpi_t * context);
/*! SCPI: SYST:COMM:UART:INVert <rx0|1>,<tx0|1> -> set RX/TX inversion. */
scpi_result_t SCPI_UartInvertSet(scpi_t * context);
/*! SCPI: SYST:COMM:UART:INVert? -> JSON {RxInv,TxInv}. */
scpi_result_t SCPI_UartInvertGet(scpi_t * context);
/*! SCPI: SYST:COMM:UART:WRITe <hex> -> transmit hex bytes. */
scpi_result_t SCPI_UartWrite(scpi_t * context);
/*! SCPI: SYST:COMM:UART:READ? <maxN> -> up to maxN RX bytes as hex. */
scpi_result_t SCPI_UartRead(scpi_t * context);
/*! SCPI: SYST:COMM:UART:COUNt? -> JSON {Pending,Overflow}. */
scpi_result_t SCPI_UartCount(scpi_t * context);

/* --- user I2C (#15, epic #664) -- SYST:COMM:I2C:* --- */
/*! SCPI: SYST:COMM:I2C:ENAble <0|1> -> master on/off (init I2C2, claim segments). */
scpi_result_t SCPI_I2cEnableSet(scpi_t * context);
/*! SCPI: SYST:COMM:I2C:ENAble? -> 1 if enabled else 0. */
scpi_result_t SCPI_I2cEnableGet(scpi_t * context);
/*! SCPI: SYST:COMM:I2C:SEGment <1|2>,<0|1> -> PCA9516A segment enable. */
scpi_result_t SCPI_I2cSegmentSet(scpi_t * context);
/*! SCPI: SYST:COMM:I2C:SEGment? <1|2> -> segment enabled state. */
scpi_result_t SCPI_I2cSegmentGet(scpi_t * context);
/*! SCPI: SYST:COMM:I2C:FREQuency <hz> -> bus frequency (>=400 kHz rejected). */
scpi_result_t SCPI_I2cFreqSet(scpi_t * context);
/*! SCPI: SYST:COMM:I2C:FREQuency? -> JSON {Freq,ActualFreq}. */
scpi_result_t SCPI_I2cFreqGet(scpi_t * context);
/*! SCPI: SYST:COMM:I2C:SCAN? -> comma list of ACKing 7-bit addresses. */
scpi_result_t SCPI_I2cScan(scpi_t * context);
/*! SCPI: SYST:COMM:I2C:TRANsfer? <addr>,<hexWrite>,<nRead> -> read bytes as hex. */
scpi_result_t SCPI_I2cTransfer(scpi_t * context);
/**
 * SCPI Callback: Enable/disable automatic WiFi power-save (#29).
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANPowerSaveSet(scpi_t * context);
/**
 * SCPI Callback: Query WiFi power-save state (#29).
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANPowerSaveGet(scpi_t * context);
/**
 * SCPI Callback: Set/query the TCP console idle timeout in seconds (#663).
 * 0 disables. @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANIdleTimeoutSet(scpi_t * context);
scpi_result_t SCPI_LANIdleTimeoutGet(scpi_t * context);
//scpi_result_t SCPI_LANAVSsidStrengthGet(scpi_t * context);
//
///**
// * SCPI Callback: Get the security mode of the device of the device
// * @return SCPI_RES_OK on success SCPI_RES_ERR on error
// */
//scpi_result_t SCPI_LANSecurityGet(scpi_t * context);
//
///**
// * SCPI Callback: Get the security mode of the available networks
// * @return SCPI_RES_OK on success SCPI_RES_ERR on error
// */
//scpi_result_t SCPI_LANAVSecurityGet(scpi_t * context);


/**
 * SCPI Callback: Saves the wifi settings of the device (but does not apply them)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSettingsSave(scpi_t * context);

/**
 * SCPI Callback: Loads the wifi settings of the device (but does not apply them)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSettingsLoad(scpi_t * context);

/**
 * SCPI Callback: Restores the wifi settings to the factory defaults (but does not apply them)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSettingsFactoryLoad(scpi_t * context);

//
///**
// * SCPI Callback: Returns SSIDs which were previously scanned with SCPI_LANAVSsidScan
// * @return 
// */
//scpi_result_t SCPI_LANAVSsidGet(scpi_t * context);
//
///**
// * SCPI Callback: Scans for SSIDs
// * @return 
// */
//scpi_result_t SCPI_LANAVSsidScan(scpi_t * context);

#ifdef	__cplusplus
}
#endif


