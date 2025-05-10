To build firmware only:
![image](https://github.com/user-attachments/assets/5c462d51-d03f-4e51-bc06-e278f5c1f860)

- Exclude all linker files from the build.
- Build and flash.

To build bootloader-compatible firmware:
![image](https://github.com/user-attachments/assets/56f12dc9-a299-4c99-b331-623045479049)

-Include old_hv2_bootld.ld.  
Then
-Flash the MCU with the bootloader firmware, then flash the firmware with the Windows app.
-Alternatively, include the DAQiFi firmware in the bootloader Project Properties -> Loding menu.  Then flash from MPLAB X.
