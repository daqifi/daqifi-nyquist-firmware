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
${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_cache_pic32mz.S  .generated_files/flags/usbdevice_pic32mz_ef_sk/26642f1cf20836a87985853bed58d10cb52897c6 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.ok ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.err 
	${MP_CC} $(MP_EXTRA_AS_PRE)  -D__DEBUG  -c -mprocessor=$(MP_PROCESSOR_OPTION)  -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d"  -o ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_cache_pic32mz.S  -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  -Wa,--defsym=__MPLAB_BUILD=1$(MP_EXTRA_AS_POST),-MD="${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.asm.d",--defsym=__ICD2RAM=1,--defsym=__MPLAB_DEBUG=1,--gdwarf-2,--defsym=__DEBUG=1 -mdfp="${DFP_DIR}"
	@${FIXDEPS} "${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d" "${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.asm.d" -t $(SILENT) -rsi ${MP_CC_DIR}../ 
	
else
${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_cache_pic32mz.S  .generated_files/flags/usbdevice_pic32mz_ef_sk/a52007b1db029afea53f211f0b5303b8e9f68615 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
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
${OBJECTDIR}/_ext/658126509/sys_devcon.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/4e9fe3eac25b24be6910562549666ed87c61181c .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/5aec2f08fbdf7fc57e876aa73072d8cee233e8d7 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/278102762/sys_ports_static.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/df004c3bc815e2d2a9691ab7ea2854cbd378bd23 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/278102762" 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d" -o ${OBJECTDIR}/_ext/278102762/sys_ports_static.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/612711379/sys_reset.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/f16a1f45c50fcc891cb5d2cd49990b3e9242d822 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/612711379" 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o.d 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/612711379/sys_reset.o.d" -o ${OBJECTDIR}/_ext/612711379/sys_reset.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_init.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/4f7822966b4d90e5d6251cd166580ba6741d95a5 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_init.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_init.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_interrupt.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/7bfabdd2e928877567bf2e2f649818557374edeb .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_interrupt.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_exceptions.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/4b91586c965da3639928e38d2be3538aa0123640 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_exceptions.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_tasks.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/41af61a9993d21a0f96f94b21529b4e78a86a85c .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_tasks.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_tasks.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/main.o: ../src/main.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/4e59a850beda43fd9695721f44c7d150c461f63f .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/main.o.d" -o ${OBJECTDIR}/_ext/1360937237/main.o ../src/main.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/app.o: ../src/app.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/3c7416cff43eb0b61a5d834b6867ed905c4396a3 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/app.o.d" -o ${OBJECTDIR}/_ext/1360937237/app.o ../src/app.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/1bb0e486eabc07fb374bd75178bf48c97ab5d6c5 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/afab9a53b91438a7155a6c7c78360d53fa341c83 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/bootloader.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/dcf80148c80aa868ed0eaf7b9f3c909d779ed72a .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/bootloader.o.d" -o ${OBJECTDIR}/_ext/100716713/bootloader.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/nvm.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/d1dc7291b809b69e06d70c850ba71dd0e537ddde .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/nvm.o.d" -o ${OBJECTDIR}/_ext/100716713/nvm.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1562194298/drv_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/bd0073cfcd62d3bf2d88416c66f28c1dcce294c8 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1562194298" 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d" -o ${OBJECTDIR}/_ext/1562194298/drv_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/ea9d4a3df799fbd5cf82d89caa7606b909f0a53c .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/c42effb5867e3ca88346ecfd0b3abb7f4f35c1f7 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/5baaa7470d1179015dc7457ac6a7917162854406 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/c0d95c020577af71f2df21a2b50921b37ee31d40 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/a8538ce86a6331c5e75bf4c57e4a68db27650793 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1829848627" 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d" -o ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1322988963/sys_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/c6fe899fcfaaa976165231916bc6ec6a6dd3052d .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1322988963" 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d" -o ${OBJECTDIR}/_ext/1322988963/sys_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/7c5e28cec1309bfbfd3d56377123a8ed8dfd3c8b .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/317035e3feb19f9efbc8d54ac3c0690690760b84 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
else
${OBJECTDIR}/_ext/658126509/sys_devcon.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/7ad5e0a48c2fa1727c89192d495cad3fd7dae6c3 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/c05b8b439b0d9de431803566004fb7ec72b74f83 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/278102762/sys_ports_static.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/ee1529407b7c8394db2fb6cb67a6de2fb7c50d7 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/278102762" 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d" -o ${OBJECTDIR}/_ext/278102762/sys_ports_static.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/612711379/sys_reset.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/3d7913da97b59f7383d97d6151d3728908fa1172 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/612711379" 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o.d 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/612711379/sys_reset.o.d" -o ${OBJECTDIR}/_ext/612711379/sys_reset.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_init.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/6e112cc428779b7a076ed6a03ded02ecd2a69037 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_init.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_init.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_interrupt.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/1629631687ce67ad706b85cbdd8dd807de19b4ce .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_interrupt.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_exceptions.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/efb9c9a91ae8a7d548fce1803351f776301753e4 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_exceptions.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_tasks.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/9e9bc5ec05536132ea987429be1835aebce6deb9 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_tasks.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_tasks.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/main.o: ../src/main.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/192d48c8ac10989fc7e7e564702b68424722e87 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/main.o.d" -o ${OBJECTDIR}/_ext/1360937237/main.o ../src/main.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/app.o: ../src/app.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/8b6f3be35f21f22a5ec91f18c3c7122a220d1259 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/app.o.d" -o ${OBJECTDIR}/_ext/1360937237/app.o ../src/app.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/4e23e193a0c758da3ddf56ecd50152615c8e7bdf .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/193440538dbc6f714d4b763e8a2829ba838b2b56 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/bootloader.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/af8ec21989287161502f8c06efbc91a76f9866db .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/bootloader.o.d" -o ${OBJECTDIR}/_ext/100716713/bootloader.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/nvm.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/7813c9185af3f7a3d382fe6e8089d016567ebe23 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/nvm.o.d" -o ${OBJECTDIR}/_ext/100716713/nvm.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1562194298/drv_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/da50916762f89509f38a5fd13e427da2b2264a26 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1562194298" 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d" -o ${OBJECTDIR}/_ext/1562194298/drv_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/86cd86412899ea687e535d36e78a904c1a20b9d4 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/9174258252c99c22f5bdec750c8a5771f8fdccb8 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/bc18ef07a1f085e328213308d47e3511fd7dbdda .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/59e23b48b3216aed16cc7e879165f913eeeaed74 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/91173392b1906cd2e2493b931c5afe552d442e05 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1829848627" 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d" -o ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1322988963/sys_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/2d21146e8981157397e6683b46e26f210d06616a .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1322988963" 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d" -o ${OBJECTDIR}/_ext/1322988963/sys_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/4d0738e3352dd06cdf59abb048589e0b97f5c971 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/1593bf184ee6a7fd2b99a0eb0b7f35af2fcc487a .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
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
${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX}: ${OBJECTFILES}  nbproject/Makefile-${CND_CONF}.mk  ../src/system_config/usbdevice_pic32mz_ef_sk/framework/peripheral/PIC32MZ2048EFM144_peripherals.a ../src/system_config/usbdevice_pic32mz_ef_sk/btl_mz.ld ../../../../dev_v3/firmware/daqifi.X/dist/default/production/daqifi.X.production.hex
	@${MKDIR} ${DISTDIR} 
	${MP_CC} $(MP_EXTRA_LD_PRE)  -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -o ${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${DEBUGGABLE_SUFFIX} ${OBJECTFILES_QUOTED_IF_SPACED}    ..\src\system_config\usbdevice_pic32mz_ef_sk\framework\peripheral\PIC32MZ2048EFM144_peripherals.a      -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -Wl,--defsym=__MPLAB_BUILD=1$(MP_EXTRA_LD_POST)$(MP_LINKER_FILE_OPTION),--defsym=_min_heap_size=0,--gc-sections,--no-code-in-dinit,--no-dinit-in-serial-mem,-Map="${DISTDIR}/${PROJECTNAME}.${IMAGE_TYPE}.map",--memorysummary,${DISTDIR}/memoryfile.xml -mdfp="${DFP_DIR}"
	${MP_CC_DIR}\\xc32-bin2hex ${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${DEBUGGABLE_SUFFIX} 
	@echo "Creating unified hex file"
	@"C:/Program Files/Microchip/MPLABX/v6.20/mplab_platform/platform/../mplab_ide/modules/../../bin/hexmate" --edf="C:/Program Files/Microchip/MPLABX/v6.20/mplab_platform/platform/../mplab_ide/modules/../../dat/en_msgs.txt" ${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.hex ../../../../dev_v3/firmware/daqifi.X/dist/default/production/daqifi.X.production.hex -odist/${CND_CONF}/production/usb_bootloader.X.production.unified.hex

endif


# Subprojects
.build-subprojects:
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
	cd ../../../../dev_v3/firmware/daqifi.X && ${MAKE}  -f Makefile CONF=default TYPE_IMAGE=DEBUG_RUN
else
	cd ../../../../dev_v3/firmware/daqifi.X && ${MAKE}  -f Makefile CONF=default
endif


# Subprojects
.clean-subprojects:
	cd ../../../../dev_v3/firmware/daqifi.X && ${MAKE}  -f Makefile CONF=default clean

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
