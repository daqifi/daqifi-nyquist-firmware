To build firmware only:
- Exclude all linker files from the build.
- Build and flash.

To build bootloader-compatible firmware:
-Include old_hv2_bootld.ld.  
Then
-Flash the MCU with the bootloader firmware, then flash the firmware with the Windows app.
-Alternatively, include the daqifi firmware in the bootloader Project Properties -> Loding menu.  Then flash from MPLAB X.
