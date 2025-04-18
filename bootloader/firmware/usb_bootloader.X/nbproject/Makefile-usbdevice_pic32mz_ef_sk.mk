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
${OBJECTDIR}/_ext/658126509/sys_devcon.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/d6f1e0e77c943e37f6c22ac948085ea2baa55372 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/86e51438f65c1e4b9ef88c0e5450553d1b196741 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/278102762/sys_ports_static.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/1039009c51566a943f915236cab900b9883f373a .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/278102762" 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d" -o ${OBJECTDIR}/_ext/278102762/sys_ports_static.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/612711379/sys_reset.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/52ce3b0fc3c3ed5de2c309d09e5b4990b0832fd2 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/612711379" 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o.d 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/612711379/sys_reset.o.d" -o ${OBJECTDIR}/_ext/612711379/sys_reset.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_init.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/7f495effb45460c270d2d2034db20ea4b9d5720c .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_init.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_init.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_interrupt.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/c86a4dc60dfef7d753de8bfb22f2c23343bb6945 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_interrupt.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_exceptions.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/e40ab551adf7a4ba946a88e8fd885a1041d105a7 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_exceptions.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_tasks.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/9480c6d4182a6b2c70da003fb2085964b8ccc511 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_tasks.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_tasks.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/main.o: ../src/main.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/efc9ab6497318a06c3905d6589715e59dc32f360 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/main.o.d" -o ${OBJECTDIR}/_ext/1360937237/main.o ../src/main.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/app.o: ../src/app.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/1c90f1c8190ad8d13ebaad081ced9b7b2984f20d .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/app.o.d" -o ${OBJECTDIR}/_ext/1360937237/app.o ../src/app.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/386eac50f6de3726d05da1bda11780db53eaf136 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/6fc5d89434d91f41034a11ddcf5a92402d3241f2 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/bootloader.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/eb839cd5a6c6fa0e990abd1b49bdb128c7d65da0 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/bootloader.o.d" -o ${OBJECTDIR}/_ext/100716713/bootloader.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/nvm.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/f1921583a0d66a68297de534bcc0d1805d673b9d .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/nvm.o.d" -o ${OBJECTDIR}/_ext/100716713/nvm.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1562194298/drv_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/7ce0eab61657f27402e59b8cf483bff5a70a86d7 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1562194298" 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d" -o ${OBJECTDIR}/_ext/1562194298/drv_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/13561fdd2438f86216995e87fd3a48a20323424 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/f6808a7f8a07ff1f9de5a4685816c9c6391a74c .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/f0653846439a3698750173dc9f89dd2151cf7c7f .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/72df94c84a8eef06d7c7629cb09e80073a6ac5a9 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/18617148d6075389e6ab58550e7a10caa8446aad .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1829848627" 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d" -o ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1322988963/sys_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/19ceb259cd8e12e4549017ea27a839f56a35fb64 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1322988963" 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d" -o ${OBJECTDIR}/_ext/1322988963/sys_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/5b848f8abc0bb4106da3adef470270b6de051237 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/e81bc833b12196cba771320b84099d79a6e2dbc1 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE) -g -D__DEBUG   -fframe-base-loclist  -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device_hid.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
else
${OBJECTDIR}/_ext/658126509/sys_devcon.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/8d0396dcbc2921a056f63c21d529be6324b804f8 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/ab02484dc88252ff87f3da519e3dad06c347513b .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/658126509" 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o.d" -o ${OBJECTDIR}/_ext/658126509/sys_devcon_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/devcon/src/sys_devcon_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/278102762/sys_ports_static.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/436b72be0c5d4f5ce6c6e9d454ad85ea733acfd2 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/278102762" 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d 
	@${RM} ${OBJECTDIR}/_ext/278102762/sys_ports_static.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/278102762/sys_ports_static.o.d" -o ${OBJECTDIR}/_ext/278102762/sys_ports_static.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/ports/src/sys_ports_static.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/612711379/sys_reset.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/c6f48a40a179018374887edc8152cbfb8621d85c .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/612711379" 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o.d 
	@${RM} ${OBJECTDIR}/_ext/612711379/sys_reset.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/612711379/sys_reset.o.d" -o ${OBJECTDIR}/_ext/612711379/sys_reset.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/reset/src/sys_reset.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_init.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/6b4489b99be86c3a0d31c1019b7cbafa835bbb95 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_init.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_init.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_init.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_init.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_interrupt.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/36c0c659857025da156403d2695aa4d5cddeff48 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_interrupt.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_interrupt.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_interrupt.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_interrupt.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_exceptions.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/b7847f8468c58a71f2bb50347ca7e6d8458f78ad .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_exceptions.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_exceptions.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_exceptions.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_exceptions.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1340256719/system_tasks.o: ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/d0db90c50a95b6f212a511175a60243700a1a56 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1340256719" 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o.d 
	@${RM} ${OBJECTDIR}/_ext/1340256719/system_tasks.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1340256719/system_tasks.o.d" -o ${OBJECTDIR}/_ext/1340256719/system_tasks.o ../src/system_config/usbdevice_pic32mz_ef_sk/system_tasks.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/main.o: ../src/main.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/afc15033e9066882e260b044d01e7d276bfb4164 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/main.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/main.o.d" -o ${OBJECTDIR}/_ext/1360937237/main.o ../src/main.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1360937237/app.o: ../src/app.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/1a4013fa92daefa77919266a51128b3d45b604d3 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1360937237" 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o.d 
	@${RM} ${OBJECTDIR}/_ext/1360937237/app.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1360937237/app.o.d" -o ${OBJECTDIR}/_ext/1360937237/app.o ../src/app.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/3c3a1e5a9b8ff6d9a4163ba7e0675227d4f417a7 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/ae79c7c5346fab919b0cb7089e0bd725876bc20a .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/2102953168" 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d 
	@${RM} ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o.d" -o ${OBJECTDIR}/_ext/2102953168/datastream_usb_hid.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/datastream/datastream_usb_hid.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/bootloader.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/9f93950647aef5ee189e2230a325c37b12190d3d .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/bootloader.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/bootloader.o.d" -o ${OBJECTDIR}/_ext/100716713/bootloader.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/bootloader.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/100716713/nvm.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/bd053e35cd194d6796926775bc32597014d202d6 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/100716713" 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o.d 
	@${RM} ${OBJECTDIR}/_ext/100716713/nvm.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/100716713/nvm.o.d" -o ${OBJECTDIR}/_ext/100716713/nvm.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/bootloader/src/nvm.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1562194298/drv_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/69593c3bbdd93cd0cfe6bc3ebf568d980d71e021 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1562194298" 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1562194298/drv_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1562194298/drv_tmr.o.d" -o ${OBJECTDIR}/_ext/1562194298/drv_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/tmr/src/dynamic/drv_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/68efc210d59f3d0375ccd0baf11827155f2ceb87 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/6dd243c5143b5c5fa37fa106c17162762b2709d9 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/273993777" 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o.d" -o ${OBJECTDIR}/_ext/273993777/drv_usbhs_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/driver/usb/usbhs/src/dynamic/drv_usbhs_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/95c994215241603a738456cd88b6b3a7b949ae11 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/23b85aca41a5e8ff429f03875048521a4341fdd7 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/734223846" 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d 
	@${RM} ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o.d" -o ${OBJECTDIR}/_ext/734223846/sys_clk_pic32mz.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/clk/src/sys_clk_pic32mz.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/3f9ee55839184e7fba2fb0d6adc36c748eb6b597 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1829848627" 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d 
	@${RM} ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o.d" -o ${OBJECTDIR}/_ext/1829848627/sys_int_pic32.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/int/src/sys_int_pic32.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/1322988963/sys_tmr.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/4ee6e8f459c36d058fd50c88dfd15813038266e1 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/1322988963" 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d 
	@${RM} ${OBJECTDIR}/_ext/1322988963/sys_tmr.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/1322988963/sys_tmr.o.d" -o ${OBJECTDIR}/_ext/1322988963/sys_tmr.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/system/tmr/src/sys_tmr.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/e7f377fcd4205548c107820871dca010cfe2f6f9 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
	@${MKDIR} "${OBJECTDIR}/_ext/768830106" 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o.d 
	@${RM} ${OBJECTDIR}/_ext/768830106/usb_device.o 
	${MP_CC}  $(MP_EXTRA_CC_PRE)  -g -x c -c -mprocessor=$(MP_PROCESSOR_OPTION)  -ffunction-sections -fdata-sections -O1 -DFORCE_BOOTLOADER_FLAG_ADDR="0x80000000+0x80000-0x10" -DFORCE_BOOTLOADER_FLAG_VALUE="0xF4CEB007" -I"../src" -I"../src/system_config/usbdevice_pic32mz_ef_sk" -I"../src/usbdevice_pic32mz_ef_sk" -I"../../../../../framework" -I"../src/system_config/usbdevice_pic32mz_ef_sk/framework" -I"../../../../../../../../microchip/harmony/v2_06/framework" -Werror -Wall -MP -MMD -MF "${OBJECTDIR}/_ext/768830106/usb_device.o.d" -o ${OBJECTDIR}/_ext/768830106/usb_device.o ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device.c    -DXPRJ_usbdevice_pic32mz_ef_sk=$(CND_CONF)  -legacy-libc  $(COMPARISON_BUILD)  -mdfp="${DFP_DIR}"  
	
${OBJECTDIR}/_ext/768830106/usb_device_hid.o: ../src/system_config/usbdevice_pic32mz_ef_sk/framework/usb/src/dynamic/usb_device_hid.c  .generated_files/flags/usbdevice_pic32mz_ef_sk/c3f98007f0ac76a7caef14631208c187ba34b380 .generated_files/flags/usbdevice_pic32mz_ef_sk/da39a3ee5e6b4b0d3255bfef95601890afd80709
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
