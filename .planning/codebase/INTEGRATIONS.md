# External Integrations

**Analysis Date:** 2026-01-10

## Hardware Interfaces

**Analog-to-Digital Converters (ADCs):**
- AD7173 - 24-bit precision ADC
  - HAL Driver: `firmware/src/HAL/ADC/AD7173.h`, `firmware/src/HAL/ADC/AD7173.c`
  - Interface: SPI
  - Channels: 4 differential

- AD7609 - 18-bit simultaneous sampling ADC
  - HAL Driver: `firmware/src/HAL/ADC/AD7609.h`, `firmware/src/HAL/ADC/AD7609.c`
  - Interface: SPI with parallel data
  - Channels: 8 simultaneous

- MC12bADC - 12-bit onboard ADC module
  - HAL Driver: `firmware/src/HAL/ADC/MC12bADC.h`, `firmware/src/HAL/ADC/MC12bADC.c`
  - Interface: Internal peripheral
  - Purpose: Monitoring channels

**Digital-to-Analog Converter (DAC):**
- DAC7718 - 8-channel 12-bit DAC (NQ3 variant only)
  - HAL Driver: `firmware/src/HAL/DAC7718/DAC7718.c`, `firmware/src/HAL/DAC7718/DAC7718.h`
  - Interface: SPI2 at 10 MHz
  - Control Pins: CS (RK0), CLR/RST (RJ13), LDAC (tied to 3.3V)
  - Requires: 10V rail (POWERED_UP state)

**Power Management:**
- BQ24297 - Battery charging and power management IC
  - HAL Driver: `firmware/src/HAL/BQ24297/BQ24297.h`
  - Features: Battery management, OTG mode, power state control

## Communication Interfaces

**USB 2.0 High-Speed:**
- USB CDC (Communications Device Class) - Virtual COM port
  - Driver: `firmware/src/config/default/driver/usb/usbhs/`
  - Service: `firmware/src/services/UsbCdc/UsbCdc.c`
  - Circular buffer: 16KB (USBCDC_CIRCULAR_BUFF_SIZE)
  - Baud rate: 115200 (virtual)

**WiFi - WINC1500:**
- Microchip WINC1500 WiFi module
  - Driver: `firmware/src/config/default/driver/winc/`
  - Manager: `firmware/src/services/wifi_services/wifi_manager.c`
  - TCP Server: `firmware/src/services/wifi_services/wifi_tcp_server.c`
  - Interface: SPI (20 MHz)
  - Circular buffer: 5.6KB (WIFI_CIRCULAR_BUFF_SIZE)
  - Modes: Access point, station, network scanning
  - Default SSID: "DAQiFi" (open network)
  - Default Port: 9760

**SD Card:**
- SD SPI Mode storage
  - Driver: `firmware/src/config/default/driver/sdspi/`
  - Manager: `firmware/src/services/sd_card_services/sd_card_manager.c`
  - Static buffer: 64KB (DMA-safe, cache-aligned)
  - File system: FAT32 with auto file splitting at 3.9GB

## Communication Protocols

**SCPI (Standard Commands for Programmable Instruments):**
- IEEE 488.2 compliant command interface
  - Entry point: `firmware/src/services/SCPI/SCPIInterface.c`
  - ADC commands: `firmware/src/services/SCPI/SCPIADC.c`
  - DAC commands: `firmware/src/services/SCPI/SCPIDAC.c`
  - DIO commands: `firmware/src/services/SCPI/SCPIDIO.c`
  - Network commands: `firmware/src/services/SCPI/SCPILAN.c`
  - SD card commands: `firmware/src/services/SCPI/SCPIStorageSD.c`

**Data Encoding Formats:**
- CSV - `firmware/src/services/csv_encoder.c`
- JSON - `firmware/src/services/JSON_Encoder.c`
- Protocol Buffers - `firmware/src/services/DaqifiPB/NanoPB_Encoder.c`
  - Schema: `firmware/src/services/DaqifiPB/DaqifiOutMessage.proto`

## Peripheral Interfaces

**SPI:**
- SPI0: Shared between WiFi and SD card
  - Coordination: `firmware/src/services/spi0_protected/`
  - Note: WiFi+SD simultaneous access prohibited due to bus conflict

- SPI2: DAC7718 (NQ3 only)

**I2C:**
- Driver: `firmware/src/config/default/driver/i2c/`
- Usage: Device communication

**Timers:**
- General-purpose timers: `firmware/src/HAL/TimerApi/TimerApi.h`
- Streaming timer: Configurable sample rate trigger

**DMA:**
- System DMA: `firmware/src/config/default/system/dma/sys_dma.h`
- Used by: ADC sample collection, SD card operations

## Storage

**Non-Volatile Memory (NVM):**
- HAL: `firmware/src/HAL/NVM/nvm.h`
- Purpose: Settings persistence (WiFi, calibration, etc.)
- Settings manager: `firmware/src/services/daqifi_settings.c`

**SD Card File System:**
- FAT32 with FatFS
- Auto file splitting at 3.9GB (FAT32 limit workaround)
- CSV header: First file only

## Environment Configuration

**Development:**
- USB CDC for SCPI commands (picocom, serial terminals)
- PICkit 4 for programming/debugging
- WSL USB passthrough via usbipd

**Runtime Configuration:**
- Default network: Open WiFi AP "DAQiFi" at 192.168.1.1:9760
- Power states: STANDBY (0), POWERED_UP (1), POWERED_UP_EXT_DOWN (2)
- Settings stored in NVM with MD5 integrity check

---

*Integration audit: 2026-01-10*
*Update when adding/removing external services*
