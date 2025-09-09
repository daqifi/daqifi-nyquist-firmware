#
# Generated Makefile - do not edit!
#
# Edit the Makefile in the project folder instead (../Makefile). Each target
# has a -pre and a -post target defined where you can add customized code.
#
# This makefile implements configuration specific macros and targets.


# Include project Makefile
ifeq "${IGNORE_LOCAL}" "TRUE"
# do not include local makefile. User is passing all local related variables already
else
include Makefile
# Include makefile containing local settings
ifeq "$(wildcard nbproject/Makefile-local-usbdevice_pic32mz_ef_sk.mk)" "nbproject/Makefile-local-usbdevice_pic32mz_ef_sk.mk"
include nbproject/Makefile-local-usbdevice_pic32mz_ef_sk.mk
endif
endif

# Environment
MKDIR=gnumkdir -p
RM=rm -f 
MV=mv 
CP=cp 

# Macros
CND_CONF=usbdevice_pic32mz_ef_sk
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
IMAGE_TYPE=debug
OUTPUT_SUFFIX=elf
DEBUGGABLE_SUFFIX=elf
FINAL_IMAGE=${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX}
else
IMAGE_TYPE=production
OUTPUT_SUFFIX=hex
DEBUGGABLE_SUFFIX=elf
FINAL_IMAGE=${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX}
endif

ifeq ($(COMPARE_BUILD), true)
COMPARISON_BUILD=-mafrlcsj
else
COMPARISON_BUILD=
endif

# Object Directory
OBJECTDIR=build/${CND_CONF}/${IMAGE_TYPE}

# Distribution Directory
DISTDIR=dist/${CND_CONF}/${IMAGE_TYPE}

# Source Files Quoted if spaced
SOURCEFILES_QUOTED_IF_SPACED=../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_cache_pic32mz.S ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c ../src/main.c ../src/app.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c

# Object Files Quoted if spaced
OBJECTFILES_QUOTED_IF_SPACED=${OBJECTDIR}/_ext/658126509/sys_devcon.o ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o ${OBJECTDIR}/_ext/278102762/sys_ports_static.o ${OBJECTDIR}/_ext/612711379/sys_reset.o ${OBJECTDIR}/_ext/1340256719/system_init.o ${OBJECTDIR}/_ext/1340256719/system_interrupt.o ${OBJECTDIR}/_ext/1340256719/system_exceptions.o ${OBJECTDIR}/_ext/1340256719/system_tasks.o ${OBJECTDIR}/_ext/1360937237/main.o ${OBJECTDIR}/_ext/1360937237/app.o ${OBJECTDIR}/_ext/2102953168/datastream.o ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o ${OBJECTDIR}/_ext/100716713/bootloader.o ${OBJECTDIR}/_ext/100716713/nvm.o ${OBJECTDIR}/_ext/1562194298/drv_tmr.o ${OBJECTDIR}/_ext/273993777/drv_usbhs.o ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o ${OBJECTDIR}/_ext/734223846/sys_clk.o ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o ${OBJECTDIR}/_ext/1322988963/sys_tmr.o ${OBJECTDIR}/_ext/768830106/usb_device.o ${OBJECTDIR}/_ext/768830106/usb_device_hid.o
POSSIBLE_DEPFILES=${OBJECTDIR}/_ext/658126509/sys_devcon.o.d ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d ${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d ${OBJECTDIR}/_ext/612711379/sys_reset.o.d ${OBJECTDIR}/_ext/1340256719/system_init.o.d ${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d ${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d ${OBJECTDIR}/_ext/1340256719/system_tasks.o.d ${OBJECTDIR}/_ext/1360937237/main.o.d ${OBJECTDIR}/_ext/1360937237/app.o.d ${OBJECTDIR}/_ext/2102953168/datastream.o.d ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d ${OBJECTDIR}/_ext/100716713/bootloader.o.d ${OBJECTDIR}/_ext/100716713/nvm.o.d ${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d ${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d ${OBJECTDIR}/_ext/734223846/sys_clk.o.d ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d ${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d ${OBJECTDIR}/_ext/768830106/usb_device.o.d ${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d

# Object Files
OBJECTFILES=${OBJECTDIR}/_ext/658126509/sys_devcon.o ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o ${OBJECTDIR}/_ext/278102762/sys_ports_static.o ${OBJECTDIR}/_ext/612711379/sys_reset.o ${OBJECTDIR}/_ext/1340256719/system_init.o ${OBJECTDIR}/_ext/1340256719/system_interrupt.o ${OBJECTDIR}/_ext/1340256719/system_exceptions.o ${OBJECTDIR}/_ext/1340256719/system_tasks.o ${OBJECTDIR}/_ext/1360937237/main.o ${OBJECTDIR}/_ext/1360937237/app.o ${OBJECTDIR}/_ext/2102953168/datastream.o ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o ${OBJECTDIR}/_ext/100716713/bootloader.o ${OBJECTDIR}/_ext/100716713/nvm.o ${OBJECTDIR}/_ext/1562194298/drv_tmr.o ${OBJECTDIR}/_ext/273993777/drv_usbhs.o ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o ${OBJECTDIR}/_ext/734223846/sys_clk.o ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o ${OBJECTDIR}/_ext/1322988963/sys_tmr.o ${OBJECTDIR}/_ext/768830106/usb_device.o ${OBJECTDIR}/_ext/768830106/usb_device_hid.o

# Source Files
SOURCEFILES=../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_cache_pic32mz.S ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c ../src/main.c ../src/app.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c



CFLAGS=
ASFLAGS=
LDLIBSOPTIONS=

############# Tool locations ##########################################
# If you copy a project from one host to another, the path where the  #
# compiler is installed may be different.                             #
# If you open this project with MPLAB X in the new host, this         #
# makefile will be regenerated and the paths will be corrected.       #
#######################################################################
# fixDeps replaces a bunch of sed/cat/printf statements that slow down the build
FIXDEPS=fixDeps

.build-conf:  ${BUILD_SUBPROJECTS}
ifneq ($(INFORMATION_MESSAGE), )
	@echo $(INFORMATION_MESSAGE)
endif
	${MAKE}  -f nbproject/Makefile-usbdevice_pic32mz_ef_sk.mk ${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX}

MP_PROCESSOR_OPTION=32MZ2048EFM144
MP_LINKER_FILE_OPTION=,--script="..\src\system_config\usbdevice_pic32mz_ef_sk\btl_mz.ld"
# ------------------------------------------------------------------------------------
# Rules for buildStep: assemble
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
else
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: assembleWithPreprocess
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_cache_pic32mz.S  .generated_files/flags/usbdevice_pic32mz_ef_sk/50d9364db71b539f8e1a298d7706fb703b629c03 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.ok ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.err 
	${MP_CC} $(MP_EXTRA_AS_PRE)  -D__DEBUG  -c -mprocessor=$(MP_PROCESSOR_OPTION)  -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d"  -o ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_cache_pic32mz.S  -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  -Wa,--defsym=__MPLAB_BUILD=1$(MP_EXTRA_AS_POST),-MD="${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.asm.d",--defsym=__ICD2RAM=1,--defsym=__MPLAB_DEBUG=1,--gdwarf-2,--defsym=__DEBUG=1 -mdfp="${DFP_DIR}"
	@${FIXDEPS} "${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d" "${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.asm.d" -t $(SILENT) -rsi ${MP_CC_DIR}../ 
	
else
${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_cache_pic32mz.S  .generated_files/flags/usbdevice_pic32mz_ef_sk/a7366afb0168cfddb0d78f361ad6711263a068c9 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.ok ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.err 
	${MP_CC} $(MP_EXTRA_AS_PRE)  -c -mprocessor=$(MP_PROCESSOR_OPTION)  -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d"  -o ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_cache_pic32mz.S  -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  -Wa,--defsym=__MPLAB_BUILD=1$(MP_EXTRA_AS_POST),-MD="${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.asm.d",--gdwarf-2 -mdfp="${DFP_DIR}"
	@${FIXDEPS} "${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d" "${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.asm.d" -t $(SILENT) -rsi ${MP_CC_DIR}../ 
	
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: compile
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
${OBJECTDIR}/_ext/658126509/sys_devcon.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/8b8d325c6ea96b3d0e7c0ed5e7bb38e2feea342c .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/e591eaa3b0be6263546e062f935dfa76e2f44137 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/278102762/sys_ports_static.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/f7e81ec095b8f111423957fc3ed28a10adbcb646 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/278102762" 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d" -o ${OBJECTDIR}/_ext/278102762/sys_ports_static.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/612711379/sys_reset.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/47993db0cbf422b48ff39d4f63b2cb7eeb99becc .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/612711379" 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o.d 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/612711379/sys_reset.o.d" -o ${OBJECTDIR}/_ext/612711379/sys_reset.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_init.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/1f64a481586575187451d17dcf5ba989ba5df81f .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_init.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_init.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_interrupt.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/ab49f827e4be5a6829f9aa1ed9d4b94bff16e909 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_interrupt.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_exceptions.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/fb9596ecd6d86acda6f9c45a562f0ca99192dfb1 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_exceptions.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_tasks.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/4cf62f9546adf2a6b6a00d03623081f4ba74e6ce .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_tasks.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_tasks.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/main.o: ../src/main.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/662499cb2fc1eae594000e52c688dd758a4333fa .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/main.o.d" -o ${OBJECTDIR}/_ext/1360937237/main.o ../src/main.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/app.o: ../src/app.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/5784f51c6360ce5f05da81536b24abb21c153c0e .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/app.o.d" -o ${OBJECTDIR}/_ext/1360937237/app.o ../src/app.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/95bde16938a66f0c9d26129f14a72ec42b21093 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/37caf5c593e0d7f27c3985b0e62f97d05a6fb759 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/bootloader.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/2f10d9cf284816741978fdf05e054f89e25d9855 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/bootloader.o.d" -o ${OBJECTDIR}/_ext/100716713/bootloader.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/nvm.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/bca0e769e42225dc31963a22d18361bf7f6528d0 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/nvm.o.d" -o ${OBJECTDIR}/_ext/100716713/nvm.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1562194298/drv_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/d2263ded59b40af1be9e573d99dda291fba747fb .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1562194298" 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d" -o ${OBJECTDIR}/_ext/1562194298/drv_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/1b38433d24ebe4f0db5377b85626eb84f480f2cf .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/b5e81b742020ae469f161cbf05307f5767e0c143 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/f6319f92d52430158c3f9c30cf2b2401896b4e96 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/e77f1530238e0c8fc2768e6c9ee31e3f3c13af3c .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/c1b3013e4358c5dd46be809b95a46c7dac777295 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1829848627" 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d" -o ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1322988963/sys_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/7fa9b77cb0b037b05b8fcb445e3b123edd954e28 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1322988963" 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d" -o ${OBJECTDIR}/_ext/1322988963/sys_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/5a9d00999bf54a8b14d65e9d1bb9c82ce5e8cb4 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/bc2308321bea7e2f843e2e630f965dcb1672dece .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
else
${OBJECTDIR}/_ext/658126509/sys_devcon.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/cb2bd24943dc0b613098d36762787832d7d80b6b .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/b570dbe7c46e3d3df8b5c43b37a2dd7af6bf4d1a .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/278102762/sys_ports_static.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/5858b8a8b96683d35ae93b95c10723011700bcdc .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/278102762" 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d" -o ${OBJECTDIR}/_ext/278102762/sys_ports_static.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/612711379/sys_reset.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/c3687c50a270f650cc91adb1fd1be5ef2040d2ff .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/612711379" 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o.d 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/612711379/sys_reset.o.d" -o ${OBJECTDIR}/_ext/612711379/sys_reset.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_init.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/97eb8eae9aa373101240d567f0c2d55ab4cdabcb .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_init.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_init.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_interrupt.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/e2f106d766920a400bac6e0a6b743b1a3bd008e7 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_interrupt.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_exceptions.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/3edb3fdb93afa5da10059eb73d12cd4e9fde2c51 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_exceptions.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_tasks.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/ca1062a26f0c7cbd0f02534697b0d0a2673333c1 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_tasks.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_tasks.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/main.o: ../src/main.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/c676cf27adc085b0aa1f8e8e6f2a03255a56432f .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/main.o.d" -o ${OBJECTDIR}/_ext/1360937237/main.o ../src/main.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/app.o: ../src/app.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/1f050cf1efea31fbb3e6f65afe9412eb711b8eee .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/app.o.d" -o ${OBJECTDIR}/_ext/1360937237/app.o ../src/app.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/9fd939974525660d84973cfb1c3e14a637e2abbf .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/dacc84cd20f420e641deaf13eceb9fff00cded2e .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/bootloader.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/7aa4ac998fb2cbf2798f69019d4e01edbb243563 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/bootloader.o.d" -o ${OBJECTDIR}/_ext/100716713/bootloader.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/nvm.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/50e617a19f6dc9f54d1091968c79c13886ddd521 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/nvm.o.d" -o ${OBJECTDIR}/_ext/100716713/nvm.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1562194298/drv_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/ac84bad662dcc466eff0bcd24fc227b0c7174d7a .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1562194298" 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d" -o ${OBJECTDIR}/_ext/1562194298/drv_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/550f7a4cd3c620d9e9d5df2a8b9903b2aaa6c261 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/a0ec633046f284476d22723dd8bf2d6a45986fbf .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/ab42f983d47f82e0a81c3c42ebe49688de8c60a8 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/8a1371aa44c33f864ca3df5410c4e7fbb6ca77dd .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/e8269760dbd585cf4246e7c73470d9909134f355 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1829848627" 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d" -o ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1322988963/sys_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/6f9f05c5c35280a501f6eef301739a47fe124432 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1322988963" 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d" -o ${OBJECTDIR}/_ext/1322988963/sys_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/417a10dcec65b91686dce64b2eec963b3373f487 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/701cddf42fad694d3aa98336017da88d8962da70 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: compileCPP
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
else
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: link
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX}: ${OBJECTFILES}  nbproject/Makefile-${CND_CONF}.mk  ../src/system_config/usbdevice_pic32mz_ef_sk/framework/peripheral/PIC32MZ2048EFM144_peripherals.a  ../src/system_config/usbdevice_pic32mz_ef_sk/btl_mz.ld
	@${MKDIR} ${DISTDIR} 
	${MP_CC} $(MP_EXTRA_LD_PRE) -g   -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -o ${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX} ${OBJECTFILES_QUOTED_IF_SPACED}    ..\src\system_config\usbdevice_pic32mz_ef_sk\framework\peripheral\PIC32MZ2048EFM144_peripherals.a      -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)   -mreserve=data@0x0:0x37F   -Wl,--defsym=__MPLAB_BUILD=1$(MP_EXTRA_LD_POST)$(MP_LINKER_FILE_OPTION),--defsym=__MPLAB_DEBUG=1,--defsym=__DEBUG=1,-D=__DEBUG_D,--defsym=_min_heap_size=0,--gc-sections,--no-code-in-dinit,--no-dinit-in-serial-mem,-Map="${DISTDIR}/${PROJECTNAME}.${IMAGE_TYPE}.map",--memorysummary,${DISTDIR}/memoryfile.xml -mdfp="${DFP_DIR}"
	
else
${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX}: ${OBJECTFILES}  nbproject/Makefile-${CND_CONF}.mk  ../src/system_config/usbdevice_pic32mz_ef_sk/framework/peripheral/PIC32MZ2048EFM144_peripherals.a ../src/system_config/usbdevice_pic32mz_ef_sk/btl_mz.ld ../../../firmware/daqifi.X/dist/default/production/daqifi.X.production.hex
	@${MKDIR} ${DISTDIR} 
	${MP_CC} $(MP_EXTRA_LD_PRE)  -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -o ${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${DEBUGGABLE_SUFFIX} ${OBJECTFILES_QUOTED_IF_SPACED}    ..\src\system_config\usbdevice_pic32mz_ef_sk\framework\peripheral\PIC32MZ2048EFM144_peripherals.a      -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -Wl,--defsym=__MPLAB_BUILD=1$(MP_EXTRA_LD_POST)$(MP_LINKER_FILE_OPTION),--defsym=_min_heap_size=0,--gc-sections,--no-code-in-dinit,--no-dinit-in-serial-mem,-Map="${DISTDIR}/${PROJECTNAME}.${IMAGE_TYPE}.map",--memorysummary,${DISTDIR}/memoryfile.xml -mdfp="${DFP_DIR}"
	${MP_CC_DIR}\\xc32-bin2hex ${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${DEBUGGABLE_SUFFIX} 
	@echo "Creating unified hex file"
	@"C:/Program Files/Microchip/MPLABX/v6.25/mplab_platform/platform/../mplab_ide/modules/../../bin/hexmate" --edf="C:/Program Files/Microchip/MPLABX/v6.25/mplab_platform/platform/../mplab_ide/modules/../../dat/en_msgs.txt" ${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.hex ../../../firmware/daqifi.X/dist/default/production/daqifi.X.production.hex -odist/${CND_CONF}/production/usb_bootloader.X.production.unified.hex

endif


# Subprojects
.build-subprojects:
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
	cd ../../../firmware/daqifi.X && ${MAKE}  -f Makefile CONF=default TYPE_IMAGE=DEBUG_RUN
else
	cd ../../../firmware/daqifi.X && ${MAKE}  -f Makefile CONF=default
endif


# Subprojects
.clean-subprojects:
	cd ../../../firmware/daqifi.X && ${MAKE}  -f Makefile CONF=default clean

# Clean Targets
.clean-conf: ${CLEAN_SUBPROJECTS}
	${RM} -r ${OBJECTDIR}
	${RM} -r ${DISTDIR}

# Enable dependency checking
.dep.inc: .depcheck-impl

DEPFILES=$(wildcard ${POSSIBLE_DEPFILES})
ifneq (${DEPFILES},)
include ${DEPFILES}
endif
