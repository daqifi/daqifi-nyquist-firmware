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
MKDIR=mkdir -p
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
MP_LINKER_FILE_OPTION=,--script="../src/system_config/usbdevice_pic32mz_ef_sk/btl_mz.ld"
# ------------------------------------------------------------------------------------
# Rules for buildStep: assemble
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
else
endif

# ------------------------------------------------------------------------------------
# Rules for buildStep: assembleWithPreprocess
ifeq ($(TYPE_IMAGE), DEBUG_RUN)
${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_cache_pic32mz.S  .generated_files/flags/usbdevice_pic32mz_ef_sk/8c88ac8af9fc786ecec4bd56b4c2536cf483cdb9 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.ok ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.err 
	${MP_CC} $(MP_EXTRA_AS_PRE)  -D__DEBUG  -c -mprocessor=$(MP_PROCESSOR_OPTION)  -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d"  -o ${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_cache_pic32mz.S  -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  -Wa,--defsym=__MPLAB_BUILD=1$(MP_EXTRA_AS_POST),-MD="${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.asm.d",--defsym=__ICD2RAM=1,--defsym=__MPLAB_DEBUG=1,--gdwarf-2,--defsym=__DEBUG=1 -mdfp="${DFP_DIR}"
	@${FIXDEPS} "${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.d" "${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o.asm.d" -t $(SILENT) -rsi ${MP_CC_DIR}../ 
	
else
${OBJECTDIR}/_ext/658126509/sys_devcon_cache_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_cache_pic32mz.S  .generated_files/flags/usbdevice_pic32mz_ef_sk/29b35adbb56fedc3dd3669397d16e9c892d0fead .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
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
${OBJECTDIR}/_ext/658126509/sys_devcon.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/6ed5790e2fc5775b1e1af7cca2794cfab7564fce .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/1a5d072d1f5060a2d014cdebb054127bf3449b43 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/278102762/sys_ports_static.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/8c4b175aec4e167132ea330d5ff87af31c05f3df .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/278102762" 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d" -o ${OBJECTDIR}/_ext/278102762/sys_ports_static.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/612711379/sys_reset.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/f6d36b6582ef4b23476889f73b4ed28dad8c146a .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/612711379" 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o.d 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/612711379/sys_reset.o.d" -o ${OBJECTDIR}/_ext/612711379/sys_reset.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_init.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/edfd0500881a8a44a57e357b78f9c1adc3623b03 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_init.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_init.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_interrupt.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/5f435541140d74eda70aac0cf4dec4cf5f6523ff .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_interrupt.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_exceptions.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/7636fc7a07ef1e8939cb82f695451177501625e3 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_exceptions.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_tasks.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/10c06bae6cbc7c228cec889a3beff48b27f309e2 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_tasks.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_tasks.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/main.o: ../src/main.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/6cd38f0d10aca594d734bb49ac510b0d43b2de3a .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/main.o.d" -o ${OBJECTDIR}/_ext/1360937237/main.o ../src/main.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/app.o: ../src/app.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/52b78594d38c76ca25f2a99fcc07fd64f15c74ea .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/app.o.d" -o ${OBJECTDIR}/_ext/1360937237/app.o ../src/app.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/d178bd3410ae53250cca86101d7c0d9c01a76ee2 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/b885373de81d59901cf55a7230914d8ccb2d78d8 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/bootloader.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/6ad40b78172502bc45dd9b29de411c521f0d1ac1 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/bootloader.o.d" -o ${OBJECTDIR}/_ext/100716713/bootloader.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/nvm.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/2118aded6dae46e3b5b1533872819cb3158c80ee .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/nvm.o.d" -o ${OBJECTDIR}/_ext/100716713/nvm.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1562194298/drv_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/3323ea48c24f2e007074478e20a9faa5f46812a4 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1562194298" 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d" -o ${OBJECTDIR}/_ext/1562194298/drv_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/adf10106d5230ce1717e024f06c6fad61b6bb38f .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/d45dd61ffd888239b151259b62a5d9743ff57ff3 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/b4f378545355aa5be3d6c5e2cac3dc320b402f7c .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/2637055a84cb13287658490b93b187f8166aa323 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/5e36ff8b09a4c3ca1553b736e832036e7f89afc5 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1829848627" 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d" -o ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1322988963/sys_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/3aee6a1954ac98b8d0c7090722a3a5f4edd24c58 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1322988963" 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d" -o ${OBJECTDIR}/_ext/1322988963/sys_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/435c96c85f40a85ca69fbd7320a8efe5dd283565 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/8ff28e2f7aa8f2aa09cc20c60ded7c775cb6b808 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
else
${OBJECTDIR}/_ext/658126509/sys_devcon.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/7d2aa1ea7df0a140917d328a604b9b75ee1d98b5 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/6f1d93097b91d11c9fdc574585a3ea98db6c2994 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/278102762/sys_ports_static.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/2f46228e27e60d014d6ec73ba06d6198a6ec69b8 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/278102762" 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d" -o ${OBJECTDIR}/_ext/278102762/sys_ports_static.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/612711379/sys_reset.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/45327211cb52b7fb4f1f318d0b7633401fd88748 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/612711379" 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o.d 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/612711379/sys_reset.o.d" -o ${OBJECTDIR}/_ext/612711379/sys_reset.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_init.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/a4a408869b682faf5d9e23f196297a82bc692dc .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_init.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_init.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_interrupt.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/e0ffa0dc5b41c16d89cc8a6d73422cb32fadf1bc .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_interrupt.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_exceptions.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/11710ba690eeaa07806e7c96d6c0bfb9d1a2919f .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_exceptions.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_tasks.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/20496ab3e5ac0b833fd5988045479a13a031599a .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_tasks.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_tasks.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/main.o: ../src/main.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/6c0500118be4205e612a6ac42222a839809a2885 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/main.o.d" -o ${OBJECTDIR}/_ext/1360937237/main.o ../src/main.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/app.o: ../src/app.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/ce27a18ac75b3e359a2cf9197035f9de7757b50b .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/app.o.d" -o ${OBJECTDIR}/_ext/1360937237/app.o ../src/app.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/645a8a7fe00d8d13493295c7474b89bf106d7107 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/5db92e6cd302c2335fd75ee681b94cff0dabc266 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/bootloader.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/f2721e86b834b0c6de3d12d23518e39a845f5089 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/bootloader.o.d" -o ${OBJECTDIR}/_ext/100716713/bootloader.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/nvm.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/defffa4c0d590e1e0acb93ed774a498bcc91c06f .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/nvm.o.d" -o ${OBJECTDIR}/_ext/100716713/nvm.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1562194298/drv_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/c945564f7df78621f5c651198f25764a0d13db60 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1562194298" 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d" -o ${OBJECTDIR}/_ext/1562194298/drv_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/14e0c50896e3a8843d72ae38d422dbec532f5b39 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/1c0631eaffc15101bba435a4afe91d10a4ad6bac .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/5d0ee1b7ec3cc50e41d7b2d64bf75239e8bc4122 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/8aa9629ba8809431440fcd3f0eb6f4a34a445cef .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/d1c1fdf283450efc4d9dece1fecc79ad7edbcd81 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1829848627" 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d" -o ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1322988963/sys_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/8981ea42f0fcb34a90c97d8d5a183ad10f19657c .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1322988963" 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d" -o ${OBJECTDIR}/_ext/1322988963/sys_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/3ad09001e36dc6cea943b281f2fe97bfc52ba231 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/c5042f00702dc09441a2ceb7d9095f95b48e3077 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
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
	${MP_CC} $(MP_EXTRA_LD_PRE) -g   -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -o ${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX} ${OBJECTFILES_QUOTED_IF_SPACED}    ../src/system_config/usbdevice_pic32mz_ef_sk/framework/peripheral/PIC32MZ2048EFM144_peripherals.a      -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)   -mreserve=data@0x0:0x37F   -Wl,--defsym=__MPLAB_BUILD=1$(MP_EXTRA_LD_POST)$(MP_LINKER_FILE_OPTION),--defsym=__MPLAB_DEBUG=1,--defsym=__DEBUG=1,-D=__DEBUG_D,--defsym=_min_heap_size=0,--gc-sections,--no-code-in-dinit,--no-dinit-in-serial-mem,-Map="${DISTDIR}/${PROJECTNAME}.${IMAGE_TYPE}.map",--memorysummary,${DISTDIR}/memoryfile.xml -mdfp="${DFP_DIR}"
	
else
${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${OUTPUT_SUFFIX}: ${OBJECTFILES}  nbproject/Makefile-${CND_CONF}.mk  ../src/system_config/usbdevice_pic32mz_ef_sk/framework/peripheral/PIC32MZ2048EFM144_peripherals.a ../src/system_config/usbdevice_pic32mz_ef_sk/btl_mz.ld ../../../firmware/daqifi.X/dist/default/production/daqifi.X.production.hex
	@${MKDIR} ${DISTDIR} 
	${MP_CC} $(MP_EXTRA_LD_PRE)  -mprocessor=$(MP_PROCESSOR_OPTION)  -O1 -o ${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${DEBUGGABLE_SUFFIX} ${OBJECTFILES_QUOTED_IF_SPACED}    ../src/system_config/usbdevice_pic32mz_ef_sk/framework/peripheral/PIC32MZ2048EFM144_peripherals.a      -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -Wl,--defsym=__MPLAB_BUILD=1$(MP_EXTRA_LD_POST)$(MP_LINKER_FILE_OPTION),--defsym=_min_heap_size=0,--gc-sections,--no-code-in-dinit,--no-dinit-in-serial-mem,-Map="${DISTDIR}/${PROJECTNAME}.${IMAGE_TYPE}.map",--memorysummary,${DISTDIR}/memoryfile.xml -mdfp="${DFP_DIR}"
	${MP_CC_DIR}/xc32-bin2hex ${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.${DEBUGGABLE_SUFFIX} 
	@echo "Creating unified hex file"
	@"/Applications/microchip/mplabx/v6.20/MPLAB X IDE v6.20.app/Contents/Resources/mplab_ide/platform/../mplab_ide/modules/../../bin/hexmate" --edf="/Applications/microchip/mplabx/v6.20/MPLAB X IDE v6.20.app/Contents/Resources/mplab_ide/platform/../mplab_ide/modules/../../dat/en_msgs.txt" ${DISTDIR}/usb_bootloader.X.${IMAGE_TYPE}.hex ../../../firmware/daqifi.X/dist/default/production/daqifi.X.production.hex -odist/${CND_CONF}/production/usb_bootloader.X.production.unified.hex

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
